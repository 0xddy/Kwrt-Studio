#include "adapter.hpp"
#include "init_template.hpp"
#include <algorithm>
#include <regex>
#include <sstream>

namespace ax {
namespace {
static const std::string oldBoard="xiaomi,redmi-router-ax6000",newBoard=oldBoard+"-stock";
static const std::string partition="/soc/spi@1100a000/flash@0/partitions";
static const std::string initPath="etc/uci-defaults/zzzz-local-network";
static const std::vector<std::string> aliasFiles={"etc/board.d/02_network","etc/board.d/01_leds","lib/preinit/05_set_preinit_iface","etc/uci-defaults/30_uboot-envtools"};
static Bytes z(const std::string& s){return s+'\0';}
static bool starts(const std::string& s,const std::string& pre){return s.compare(0,pre.size(),pre)==0;}
static std::string parent(const std::string& p){auto n=p.find_last_of('/');return n==0?"/":p.substr(0,n);}
static Bytes payloadHash(const Bytes& data,const Bytes& alg){if(alg==z("crc32"))return cells({crc(data)});if(alg==z("sha1"))return hash(data,BCRYPT_SHA1_ALGORITHM);if(alg==z("sha256"))return hash(data);throw Error("FIT 使用未支持的校验方式。");}
Fdt verifyFit(const Bytes& b){auto f=Fdt::parse(b);std::set<std::string> images,configs;
    for(auto& n:f.nodes){need(n.path.find("signature")==n.path.npos,"固件含有签名，转换会破坏签名，已停止。");if(parent(n.path)=="/images")images.insert(n.path);if(starts(n.path,"/configurations/"))configs.insert(n.path);}
    need(images==std::set<std::string>{"/images/kernel-1","/images/fdt-1"}&&configs==std::set<std::string>{"/configurations/config-1"},"FIT 镜像或启动配置结构变化，需要更新工具。");
    need(f.get("/configurations","default")==z("config-1")&&f.get("/configurations/config-1","kernel")==z("kernel-1")&&f.get("/configurations/config-1","fdt")==z("fdt-1"),"FIT 默认启动配置不受支持。");
    need(f.get("/images/kernel-1","type")==z("kernel")&&f.get("/images/kernel-1","arch")==z("arm64")&&f.get("/images/kernel-1","compression")==z("lzma"),"只支持 ARM64 / LZMA 内核。");
    need(f.get("/images/fdt-1","type")==z("flat_dt")&&f.get("/images/fdt-1","compression")==z("none"),"设备树封装方式变化。");
    need(std::regex_search(f.get("/images/kernel-1","description"),std::regex("Linux-6\\.12\\.[0-9]+\\b")),"本版本只验证过 Linux 6.12 系列，需要更新工具后才能处理其他系列。");
    for(auto& node:images){auto data=f.get(node,"data");need(!data.empty(),"FIT 载荷缺失。");for(auto k:{"data-offset","data-position","data-size"})need(f.get(node,k).empty(),"不支持外置 FIT 载荷。");int count=0;
        for(auto& n:f.nodes)if(starts(n.path,node+"/")){need(parent(n.path)==node,"FIT 校验节点结构变化。");need(f.get(n.path,"value")==payloadHash(data,f.get(n.path,"algo")),"FIT 校验失败，输入文件可能损坏。");count++;}need(count>0,"FIT 缺少载荷校验。");}
    return f;
}
void validateTree(const Fdt& f){
    need(f.get("/","compatible")==z(oldBoard)+z("mediatek,mt7986a")&&f.get("/","model")==z("Xiaomi Redmi Router AX6000"),"不是普通版 Redmi AX6000；不接受 stock 或 U-Boot layout 镜像。");
    auto m=f.map();auto flash="/soc/spi@1100a000/flash@0";
    need(m.count(flash)&&m[flash].count("mediatek,nmbm")&&f.get(partition,"compatible")==z("fixed-partitions"),"不是已验证的 NMBM 固定分区结构。");
    std::map<uint32_t,std::pair<std::string,uint32_t>> expected={{0,{"BL2",0x100000}},{0x100000,{"Nvram",0x40000}},{0x140000,{"Bdata",0x40000}},{0x180000,{"Factory",0x200000}},{0x380000,{"FIP",0x200000}},{0x580000,{"crash",0x40000}},{0x5c0000,{"crash_log",0x40000}},{0x600000,{"ubi",0x6e00000}}};
    size_t count=0;for(auto& n:f.nodes)if(parent(n.path)==partition)count++;
    need(count==expected.size(),"原固件的分区数量变化，需要重新核对。");
    for(auto& [addr,v]:expected){std::ostringstream s;s<<std::hex<<addr;auto p=partition+"/partition@"+s.str();need(f.get(p,"label")==z(v.first)&&f.get(p,"reg")==cells({addr,v.second}),"原固件不符合已验证的 110 MiB 分区布局。");}
    need(m[partition+"/partition@600000"].size()==2,"UBI 分区增加了未知属性。");
}
Bytes patchKernel(const Bytes& source){auto fit=verifyFit(source),oldFit=fit;auto board=Fdt::parse(fit.get("/images/fdt-1","data"));validateTree(board);auto expected=board.map();
    board.set("/","compatible",z(newBoard)+z("mediatek,mt7986a"));board.set("/","model",z("Xiaomi Redmi Router AX6000 (stock layout)"));
    board.set(partition+"/partition@600000","label",z("ubi_kernel"));board.set(partition+"/partition@600000","reg",cells({0x600000,0x1e00000}));
    board.add(partition+"/partition@2400000");board.set(partition+"/partition@2400000","label",z("ubi"));board.set(partition+"/partition@2400000","reg",cells({0x2400000,0x5000000}));
    expected["/"]["compatible"]=z(newBoard)+z("mediatek,mt7986a");expected["/"]["model"]=z("Xiaomi Redmi Router AX6000 (stock layout)");expected[partition+"/partition@600000"]={{"label",z("ubi_kernel")},{"reg",cells({0x600000,0x1e00000})}};expected[partition+"/partition@2400000"]={{"label",z("ubi")},{"reg",cells({0x2400000,0x5000000})}};
    auto dtb=board.encode();need(Fdt::parse(dtb).map()==expected,"分区适配影响了其他硬件描述。");fit.set("/images/fdt-1","data",dtb);
    auto expectedFit=oldFit.map();expectedFit["/images/fdt-1"]["data"]=dtb;
    for(auto& n:oldFit.nodes)if(starts(n.path,"/images/fdt-1/")){auto v=payloadHash(dtb,fit.get(n.path,"algo"));fit.set(n.path,"value",v);expectedFit[n.path]["value"]=v;}
    auto output=fit.encode();need(verifyFit(output).map()==expectedFit,"FIT 适配影响了预期以外的内容。");need(fit.get("/images/kernel-1","data")==oldFit.get("/images/kernel-1","data"),"Linux 核心载荷发生变化。");return output;
}

static std::vector<std::string> patchRoot(Archive& a,const Json& p,uint32_t epoch){
    auto& platform=regular(a,"lib/upgrade/platform.sh").data;
    std::regex branch("xiaomi,redmi-router-ax6000-stock\\)\\s*CI_KERN_UBIPART=ubi_kernel\\s+CI_ROOT_UBIPART=ubi\\s+CI_DATA_UBIPART=ubi\\s+nand_do_upgrade \\\"\\$1\\\"\\s*;;");
    need(std::regex_search(platform,branch),"KWRT 不再包含已验证的 stock 升级分支，需要更新转换工具。");
    auto& nand=regular(a,"lib/upgrade/nand.sh").data;for(auto k:{"CI_KERN_UBIPART","CI_ROOT_UBIPART","CI_DATA_UBIPART"})need(nand.find(k)!=nand.npos,"NAND 升级方式发生变化。");
    need(!a.count(initPath),"已经存在本工具初始化文件，请选择未转换过的原始固件。");std::vector<std::string> changed;
    std::regex alias("^([ \\t]*)xiaomi,redmi-router-ax6000(\\|\\\\|\\))([ \\t]*)$");
    for(auto& path:aliasFiles){auto& text=regular(a,path).data;need(text.find(newBoard)==text.npos,"固件已含 stock 型号或已转换过，需要重新核对："+path);std::string output;size_t pos=0;int count=0;
        while(pos<text.size()){auto end=text.find('\n',pos);bool newline=end!=text.npos;if(!newline)end=text.size();auto line=text.substr(pos,end-pos);std::smatch m;
            if(std::regex_match(line,m,alias)){output+=m[1].str()+newBoard+"|\\\n";count++;}output+=line;if(newline)output+='\n';pos=end+1;}
        need(count>0,"型号匹配方式发生变化："+path);text=std::move(output);changed.push_back(path);
    }
    need(regular(a,"etc/board.d/02_network").data.find("ucidef_set_interfaces_lan_wan \"lan2 lan3 lan4\" wan")!=std::string::npos,"WAN / LAN 默认网口定义已改变。");
    std::string asu="etc/uci-defaults/zz-asu-defaults";
    if(a.count(asu)){auto& text=regular(a,asu).data;std::string output;std::regex bg("^([ \\t]*\\. /sbin/mywifi)[ \\t]+&[ \\t]*$");size_t pos=0;int count=0;
        while(pos<text.size()){auto end=text.find('\n',pos);bool newline=end!=text.npos;if(!newline)end=text.size();auto line=text.substr(pos,end-pos);std::smatch m;if(std::regex_match(line,m,bg)){output+=m[1];count++;}else output+=line;if(newline)output+='\n';pos=end+1;}
        if(text.find("/sbin/mywifi")!=text.npos){need(count==1,"KWRT 首次 Wi-Fi 初始化方式变化，需要核对。");text=std::move(output);changed.push_back(asu);}
    }
    auto presentation=applyFirmwarePresentation(a,p);changed.insert(changed.end(),presentation.begin(),presentation.end());
    Entry init;init.name=initPath;init.mode=0700;init.mtime=epoch;init.data=renderInit(p,initTemplate);a.emplace(init.name,std::move(init));return changed;
}

class RedmiAx6000 final : public DeviceAdapter {
public:
    AdapterInfo info() const override {
        return {"redmi-ax6000.kwrt-110m-to-stock", "Xiaomi Redmi Router AX6000",
                "NMBM / 单 UBI 110 MiB", "stock / 内核 30 MiB + 系统 80 MiB",
                "xiaomi_redmi-router-ax6000-stock", {"KWRT"}};
    }
    bool matches(const Json& meta) const override {
        auto v=meta.value("version",Json::object());auto dist=v.value("dist",std::string());
        std::transform(dist.begin(),dist.end(),dist.begin(),[](unsigned char c){return char(tolower(c));});
        return dist=="kwrt"&&v.value("target","")=="mediatek/filogic"&&v.value("board","")=="xiaomi_redmi-router-ax6000"
               &&meta.value("supported_devices",Json())==Json::array({oldBoard});
    }
    void validateInput(const Firmware& fw) const override {
        need(matches(fw.metadata),"固件与 Redmi AX6000 适配器不匹配。");
        auto& root=fw.files.at(fw.archiveRoot+"/root").data;
        need(le32(root,12)==262144,"此设备适配器尚未验证该 SquashFS 块大小。");
        validateTree(Fdt::parse(verifyFit(fw.files.at(fw.archiveRoot+"/kernel").data).get("/images/fdt-1","data")));
    }
    KernelPatch transformKernel(const Bytes& kernel) const override {
        auto original=verifyFit(kernel);return {patchKernel(kernel),sha(original.get("/images/kernel-1","data"))};
    }
    void verifyOutputKernel(const Bytes& kernel) const override { verifyFit(kernel); }
    RootPatch transformFilesystem(Archive& files,const Json& p,uint32_t epoch) const override {
        return {patchRoot(files,p,epoch),{initPath}};
    }
    uint64_t availableDataBytes(uint64_t kernelBytes,uint64_t rootBytes) const override {
        need(kernelBytes<28ull*1024*1024,"适配后的内核超出 AX6000 stock 内核分区容量。");
        uint64_t rootLebs=(rootBytes+126975)/126976;
        need(rootLebs<617,"固件超出 AX6000 stock 系统分区容量，未删除插件。");
        return (617-rootLebs)*126976;
    }
    Json outputMetadata(Json meta) const override {
        meta["supported_devices"]=Json::array({newBoard});meta["version"]["board"]=info().outputBoard;return meta;
    }
    Json partitionDescription() const override {
        return {{"ubi_kernel",{"0x600000","0x1e00000"}},{"ubi",{"0x2400000","0x5000000"}}};
    }
};
} // Device-specific implementation is private to this translation unit.
std::unique_ptr<DeviceAdapter> makeRedmiAx6000Adapter(){return std::make_unique<RedmiAx6000>();}
}
