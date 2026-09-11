#include "adapter.hpp"
#include "tool_hashes.hpp"
#include <algorithm>
#include <regex>
#include <chrono>
#include <fstream>
#include <sstream>

namespace ax {
struct Scratch {
    fs::path path;
    Scratch(){wchar_t p[32768];DWORD n=GetTempPathW(32768,p);need(n>0&&n<32768,"无法创建临时工作目录。");auto root=fs::absolute(p);path=root/(L"KwrtStudio-"+wide(randomToken()));need(fs::create_directory(path),"无法创建临时工作目录。");}
    ~Scratch(){std::error_code ec;for(auto name:{L"source.sqfs",L"source.tar",L"edited.tar",L"new-root.sqfs",L"roundtrip.tar",L"helper.log"})fs::remove(path/name,ec);fs::remove(path,ec);}
};
struct Handle {HANDLE h=INVALID_HANDLE_VALUE;Handle()=default;explicit Handle(HANDLE value):h(value){}~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;};
static std::wstring winQuote(const std::wstring& s){std::wstring r=L"\"";size_t slashes=0;for(auto c:s){if(c==L'\\'){slashes++;continue;}if(c==L'\"'){r.append(slashes*2+1,L'\\');r+=c;}else{r.append(slashes,L'\\');r+=c;}slashes=0;}r.append(slashes*2,L'\\');return r+L'\"';}
static void helper(const fs::path& exe,const std::vector<std::wstring>& arguments,const fs::path& cwd,const fs::path& input,const fs::path& output,const std::atomic_bool& cancel){
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    Handle in(CreateFileW(input.empty()?L"NUL":input.c_str(),GENERIC_READ,FILE_SHARE_READ,&sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    Handle out(CreateFileW(output.empty()?L"NUL":output.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,output.empty()?OPEN_EXISTING:CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    auto errfile=cwd/L"helper.log";Handle err(CreateFileW(errfile.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    need(in.h!=INVALID_HANDLE_VALUE&&out.h!=INVALID_HANDLE_VALUE&&err.h!=INVALID_HANDLE_VALUE,"无法准备固件处理文件。");
    std::wstring cmd=winQuote(exe.wstring());for(auto& a:arguments)cmd+=L" "+winQuote(a);
    STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.StartupInfo.wShowWindow=SW_HIDE;si.StartupInfo.hStdInput=in.h;si.StartupInfo.hStdOutput=out.h;si.StartupInfo.hStdError=err.h;
    SIZE_T count=0;InitializeProcThreadAttributeList(nullptr,1,0,&count);std::vector<unsigned char>attrs(count);si.lpAttributeList=(LPPROC_THREAD_ATTRIBUTE_LIST)attrs.data();
    need(InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&count),"无法准备本地处理进程。");
    HANDLE handles[]={in.h,out.h,err.h};bool attrOk=UpdateProcThreadAttribute(si.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr)!=0;
    if(!attrOk){DeleteProcThreadAttributeList(si.lpAttributeList);throw Error("无法隔离本地处理进程的文件句柄。");}
    Handle job(CreateJobObjectW(nullptr,nullptr));JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;need(job.h&&SetInformationJobObject(job.h,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"无法准备可取消的转换进程。");
    PROCESS_INFORMATION pi{};BOOL made=CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,cwd.c_str(),&si.StartupInfo,&pi);DeleteProcThreadAttributeList(si.lpAttributeList);
    need(made,"内置处理组件无法启动，请完整解压软件包后重试。");Handle process(pi.hProcess),thread(pi.hThread);
    if(!AssignProcessToJobObject(job.h,process.h)){TerminateProcess(process.h,2);throw Error("无法建立转换进程管理。");}
    ResumeThread(thread.h);auto start=std::chrono::steady_clock::now();
    while(WaitForSingleObject(process.h,100)==WAIT_TIMEOUT){
        if(cancel){TerminateJobObject(job.h,2);WaitForSingleObject(process.h,5000);throw Error("本次转换已取消，没有生成固件。");}
        if(std::chrono::steady_clock::now()-start>std::chrono::minutes(10)){TerminateJobObject(job.h,2);throw Error("处理超时，已停止转换。");}
        std::error_code ec;if(!output.empty()&&fs::file_size(output,ec)>600ull*1024*1024&&!ec){TerminateJobObject(job.h,2);throw Error("解压数据超出此设备的处理范围。");}
    }
    DWORD exit=1;need(GetExitCodeProcess(process.h,&exit)&&exit==0,"固件解包或压缩失败，未跳过检查。请核对固件格式和磁盘空间。");
}
Entry& regular(Archive& a,const std::string& path){auto it=a.find(path);need(it!=a.end()&&it->second.type=='0',"固件缺少预期的系统文件："+path);for(auto n=path.find('/');n!=path.npos;n=path.find('/',n+1)){auto p=path.substr(0,n);need(a.count(p)&&a.at(p).type=='5',"系统文件的上级路径不是普通目录。");}return it->second;}
Json packageList(Archive& a){auto text=regular(a,"usr/lib/opkg/status").data;std::vector<Json> list;size_t p=0;while(p<text.size()){auto end=text.find("\n\n",p);if(end==text.npos)end=text.size();std::stringstream stream(text.substr(p,end-p));std::map<std::string,std::string> fields;std::string line;while(std::getline(stream,line)){auto sep=line.find(": ");if(sep!=line.npos&&!line.empty()&&line[0]!=' ')fields[line.substr(0,sep)]=line.substr(sep+2);}if(fields.count("Package"))list.push_back({{"name",fields["Package"]},{"version",fields["Version"]},{"status",fields["Status"]}});p=end+2;}
    need(!list.empty(),"未找到 opkg 软件包列表，软件包格式可能已变化。");std::sort(list.begin(),list.end(),[](auto& a,auto& b){return a["name"].template get<std::string>()<b["name"].template get<std::string>();});return list;
}
Json readFirmwarePackages(const fs::path& inputPath,const fs::path& appDirectory,const Notify& notify,const std::atomic_bool& cancel){
    auto step=[&](int percent,const std::string& message){need(!cancel,"已取消读取插件列表。");if(notify)notify({percent,message});};
    step(5,"正在检查固件");
    for(auto& [name,digest]:toolHashes)need(shaFile(appDirectory/L"tools"/wide(name))==digest,
        "内置组件缺失，请完整解压软件包后重试。");
    auto input=read(inputPath,512ull*1024*1024);auto sourceHash=sha(input);
    auto fw=unpackFirmware(input);input.clear();input.shrink_to_fit();
    auto& root=fw.files.at(fw.archiveRoot+"/root").data;
    need(root.size()>=96&&root.substr(0,4)=="hsqs"&&uint8_t(root[28])==4&&root[29]==0,
         "暂时只能读取包含 SquashFS 系统的 sysupgrade 固件。");
    Scratch scratch;std::error_code ec;auto space=fs::space(scratch.path,ec);
    need(!ec&&space.available>=1024ull*1024*1024,"读取列表需要至少 1 GiB 临时磁盘空间。");
    write(scratch.path/L"source.sqfs",root);fw.files.clear();
    step(30,"正在读取固件里的插件");
    helper(appDirectory/L"tools"/L"sqfs2tar.exe",{L"--no-skip",L"--root-becomes",L".",L"source.sqfs"},scratch.path,{},scratch.path/L"source.tar",cancel);
    step(80,"正在整理插件列表");
    auto files=untar(read(scratch.path/L"source.tar"));std::string database,manager;
    if(files.count("usr/lib/opkg/status")){database="usr/lib/opkg/status";manager="opkg";}
    else if(files.count("lib/apk/db/installed")){database="lib/apk/db/installed";manager="apk";}
    else if(files.count("usr/lib/apk/db/installed")){database="usr/lib/apk/db/installed";manager="apk";}
    else throw Error("这份固件的软件包列表格式暂不支持，无法准确列出插件。");
    auto packages=parseInstalledPackages(regular(files,database).data,manager);size_t plugins=0;
    for(auto& package:packages)if(package["is_plugin"].get<bool>())plugins++;
    step(100,"插件列表已读取");
    return {{"filename",utf8(inputPath.filename().wstring())},{"source_sha256",sourceHash},
        {"firmware",fw.metadata["version"]},{"package_manager",manager},
        {"package_count",packages.size()},{"plugin_count",plugins},{"packages",packages},
        {"plugin_definition","luci-app-*"},{"network_defaults",detectNetworkDefaults(files)},{"read_only",true}};
}
Result convert(const Request& request,const Notify& notify,const std::atomic_bool& cancel){
    auto step=[&](int percent,const std::string& text){need(!cancel,"本次转换已取消，没有生成固件。");notify({percent,text});};
    validateProfile(request.profile);need(request.minimumDataMiB>=8&&request.minimumDataMiB<=64,"预留空间参数超出范围。");
    need(fs::is_regular_file(request.input)&&fs::is_directory(request.outputDirectory),"固件文件或输出目录不存在。");
    std::string sourceName=utf8(request.input.filename().wstring()),stem=utf8(request.input.stem().wstring());
    for(auto suffix:{std::string("-squashfs-sysupgrade"),std::string("-sysupgrade")})if(stem.size()>suffix.size()&&stem.compare(stem.size()-suffix.size(),suffix.size(),suffix)==0){stem.resize(stem.size()-suffix.size());break;}
    auto filename=stem+"-stock-auto-init-sysupgrade.bin";fs::path output=request.outputDirectory/wide(filename);
    for(auto n:{output,request.outputDirectory/L"转换报告.json",request.outputDirectory/L"SHA256.txt"})need(!fs::exists(n),"输出目录已有同名结果，请选择空目录。不会覆盖已有文件。");
    step(2,"正在检查固件");
    for(auto& [name,digest]:toolHashes)need(shaFile(request.appDirectory/L"tools"/wide(name))==digest,"内置组件缺失或校验不一致，请重新完整解压软件包。");
    Scratch scratch;std::error_code diskError;auto space=fs::space(scratch.path,diskError);need(!diskError&&space.available>=1024ull*1024*1024,"临时磁盘至少需要 1 GiB 可用空间。");
    auto input=read(request.input,512ull*1024*1024);auto sourceSha=sha(input);auto metadata=firmwareMetadata(input);auto adapter=findAdapter(metadata);
    need(adapter!=nullptr,"暂不支持这款固件，请点击“支持机型”查看可转换的型号和版本。");
    auto info=adapter->info();auto fw=unpackFirmware(input);adapter->validateInput(fw);input.clear();input.shrink_to_fit();
    auto oldDir=fw.archiveRoot,newDir="sysupgrade-"+info.outputBoard;
    auto& rootImage=fw.files.at(oldDir+"/root").data;need(rootImage.size()>=96&&rootImage.substr(0,4)=="hsqs"&&uint8_t(rootImage[20])==4&&rootImage[21]==0&&uint8_t(rootImage[28])==4&&rootImage[29]==0,"只支持 XZ SquashFS 4 文件系统。");
    need(le64(rootImage,56)==UINT64_MAX,"此文件系统含有尚未支持的扩展属性。");uint32_t epoch=le32(rootImage,8),blockSize=le32(rootImage,12);
    need(blockSize>=4096&&blockSize<=1048576&&(blockSize&(blockSize-1))==0,"文件系统块大小无效。");
    auto& kernel=fw.files.at(oldDir+"/kernel").data;auto kernelPatch=adapter->transformKernel(kernel);auto coreSha=kernelPatch.executableSha256;kernel=std::move(kernelPatch.bytes);
    write(scratch.path/L"source.sqfs",rootImage);rootImage.clear();rootImage.shrink_to_fit();
    step(14,"正在读取插件和系统文件");
    helper(request.appDirectory/L"tools"/L"sqfs2tar.exe",{L"--no-skip",L"--root-becomes",L".",L"source.sqfs"},scratch.path,{},scratch.path/L"source.tar",cancel);
    auto files=untar(read(scratch.path/L"source.tar"));auto before=manifest(files),packages=packageList(files);
    auto detectedNetwork=detectNetworkDefaults(files);
    need(detectedNetwork["mode"]!="side"||request.profile.contains("routing_mode"),"检测到旁路由固件，请明确选择旁路由模式并填写主路由网关。");
    step(30,"正在写入网络设置");auto patch=adapter->transformFilesystem(files,request.profile,epoch);auto changed=patch.changed;auto expected=manifest(files);
    need(expected.size()==before.size()+patch.added.size(),"文件数量出现非预期变化。");
    for(auto& path:patch.added)need(!before.contains(path)&&expected.contains(path),"适配器声明的新增文件与实际不符。");
    std::set<std::string> allow(changed.begin(),changed.end());for(auto it=before.begin();it!=before.end();++it){need(expected.contains(it.key()),"有原文件被删除，已停止。");if(!allow.count(it.key()))need(expected[it.key()]==it.value(),"插件或其他原始文件发生变化，已停止。");else for(auto k:{"mode","uid","gid","type","mtime"})need(expected[it.key()][k]==it.value()[k],"原文件权限、时间或类型发生变化。");}
    need(packageList(files)==packages,"软件包清单发生变化，已停止。");write(scratch.path/L"edited.tar",tar(files));files.clear();
    step(45,"正在生成固件，请稍候…");
    helper(request.appDirectory/L"tools"/L"tar2sqfs.exe",{L"--no-skip",L"--no-xattr",L"--no-pad",L"--compressor",L"xz",L"--block-size",std::to_wstring(blockSize),L"--num-jobs",L"4",L"--defaults",L"mtime="+std::to_wstring(epoch),L"--quiet",L"new-root.sqfs"},scratch.path,scratch.path/L"edited.tar",{},cancel);
    auto packed=read(scratch.path/L"new-root.sqfs",512ull*1024*1024);size_t rootSize=(packed.size()+1023)/1024*1024;uint64_t free=adapter->availableDataBytes(kernel.size(),rootSize);
    need(free>=request.minimumDataMiB*1024ull*1024,"保留全部插件后剩余数据卷不足 "+std::to_string(request.minimumDataMiB)+" MiB。未删除插件，未生成固件。");
    step(65,"正在检查生成结果");
    helper(request.appDirectory/L"tools"/L"sqfs2tar.exe",{L"--no-skip",L"--root-becomes",L".",L"new-root.sqfs"},scratch.path,{},scratch.path/L"roundtrip.tar",cancel);
    auto verified=untar(read(scratch.path/L"roundtrip.tar"));need(manifest(verified)==expected&&packageList(verified)==packages,"重打包后的文件、软件包或 Linux 元数据不一致，已停止。");verified.clear();
    step(85,"正在整理固件");packed.resize(rootSize,0);fw.files.at(oldDir+"/root").data=std::move(packed);fw.files.at(oldDir+"/CONTROL").data="BOARD="+info.outputBoard+"\n";
    Archive outer;for(auto& [name,e]:fw.files){e.name=newDir+name.substr(oldDir.size());e.uid=0;e.gid=0;e.mtime=epoch;std::string key=e.name;outer.emplace(key,std::move(e));}fw.files.clear();
    auto raw=tar(outer,10240);auto checkOuter=untar(raw);need(manifest(checkOuter)==manifest(outer),"最终固件归档回读不一致。");checkOuter.clear();adapter->verifyOutputKernel(outer.at(newDir+"/kernel").data);outer.clear();
    fw.metadata=adapter->outputMetadata(fw.metadata);auto assembled=appendMetadata(std::move(raw),fw.metadata);auto outputSha=sha(assembled);
    auto trailer=assembled.size()-16;need(be32(assembled,trailer+4)==crc(assembled.substr(0,trailer),false),"最终镜像尾部校验失败。");
    Json plugins=Json::array();for(auto& p:packages){auto n=p["name"].get<std::string>();if(n.compare(0,9,"luci-app-")==0)plugins.push_back(n);}
    Json cores=Json::object();for(auto n:{"usr/bin/sing-box","usr/bin/mihomo","usr/bin/xray"})cores[n]=before.contains(n);
    Json report={{"converter","Kwrt Studio / Native C++ 1.4.1"},{"source_filename",sourceName},{"source_sha256",sourceSha},{"output_filename",filename},{"output_sha256",outputSha},{"output_bytes",assembled.size()},{"distribution",fw.metadata["version"]},{"supported_devices",fw.metadata["supported_devices"]},{"adapter_id",info.id},{"device",info.device},{"source_layout",info.sourceLayout},{"target_layout",info.targetLayout},{"package_count",packages.size()},{"package_list",packages},{"plugins",plugins},{"all_original_packages_preserved",true},{"all_other_original_files_preserved",true},{"linux_payload_unchanged",true},{"linux_payload_sha256",coreSha},{"changed_system_files",changed},{"added_files",patch.added},{"file_count",expected.size()},{"core_presence_in_source_and_output",cores},{"root_bytes",rootSize},{"estimated_data_ubi_mib",double(free)/1048576.0},{"capacity_note","Raw UBI data-volume estimate before UBIFS overhead."},{"target_partitions",adapter->partitionDescription()},{"fit_hashes_verified",true},{"fwtool_crc_verified",true},{"filesystem_roundtrip_verified",true},{"file_timestamps_verified",true},{"configuration_values_logged",false},{"runtime_dependencies","Windows x64 and included native tools only"},{"router_flashed",false},{"physical_boot_and_proxy_test","not performed"}};
    step(95,"正在保存固件");auto partial=output;partial+=L".partial";need(!fs::exists(partial),"存在上次未完成的临时输出，请选择其他输出目录。");
    try{writeNew(partial,assembled);need(shaFile(partial)==outputSha,"输出磁盘回读校验失败。");need(!cancel,"本次转换已取消，没有生成固件。");need(MoveFileExW(partial.c_str(),output.c_str(),MOVEFILE_WRITE_THROUGH),"无法提交固件文件；不会覆盖已有文件。");}
    catch(...){std::error_code ec;fs::remove(partial,ec);throw;}
    writeNew(request.outputDirectory/L"转换报告.json",report.dump(2)+'\n');writeNew(request.outputDirectory/L"SHA256.txt",outputSha+"  "+filename+'\n');
    notify({100,"转换完成，已保留 "+std::to_string(packages.size())+" 个软件包。"});return {output,report};
}
}
