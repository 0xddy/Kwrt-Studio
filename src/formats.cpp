#include "core.hpp"
#include <algorithm>
#include <regex>
#include <sstream>
#include <iomanip>

namespace ax {
static Bytes z(const std::string& s){return s+'\0';}
static bool starts(const std::string& s,const std::string& pre){return s.compare(0,pre.size(),pre)==0;}
static std::string parent(const std::string& p){auto n=p.find_last_of('/');return n==0?"/":p.substr(0,n);}
static void align4(Bytes& b){b.resize((b.size()+3)&~size_t(3),0);}
Fdt Fdt::parse(const Bytes& b){
    need(b.size()>=40&&be32(b,0)==0xd00dfeed,"不是有效的设备树或 FIT 文件。");
    size_t total=be32(b,4),st=be32(b,8),names=be32(b,12),res=be32(b,16),ns=be32(b,32),ts=be32(b,36);
    need(total>=40&&total<=b.size()&&be32(b,20)==17&&be32(b,24)<=17,"设备树版本或长度变化，需要更新工具。");
    need(st>=40&&names>=40&&res>=40&&st<=total&&ts<=total-st&&names<=total&&ns<=total-names&&res<total,"设备树数据越界。");
    Fdt f;f.bootCpu=be32(b,28);size_t endRes=res;
    while(true){need(endRes<=total&&total-endRes>=16,"设备树保留区未结束。");bool end=true;for(int i=0;i<16;i++)end&=b[endRes+i]==0;endRes+=16;if(end)break;}
    need(endRes<=st,"设备树保留区结构变化。");f.reservations=b.substr(res,endRes-res);
    size_t pos=st,end=st+ts;std::vector<std::string> stack;std::set<std::string> paths;bool done=false;
    while(pos+4<=end){uint32_t token=be32(b,pos);pos+=4;
        if(token==1){auto n=b.find('\0',pos);need(n!=b.npos&&n<end,"设备树节点名未结束。");auto name=b.substr(pos,n-pos);
            need(name.find('/')==name.npos&&name.find('\\')==name.npos,"设备树节点名无效。");
            if(stack.empty()){need(name.empty()&&f.nodes.empty(),"设备树根节点异常。");stack.push_back("/");}
            else{need(!name.empty()&&stack.size()<64,"设备树层级异常。");stack.push_back(stack.back()=="/"?"/"+name:stack.back()+"/"+name);}
            need(paths.insert(stack.back()).second,"设备树存在重复节点。");f.nodes.push_back({stack.back(),{}});pos=(n+4)&~size_t(3);
        }else if(token==2){need(!stack.empty(),"设备树层级未配对。");stack.pop_back();}
        else if(token==3){need(!stack.empty()&&pos+8<=end,"设备树属性异常。");size_t len=be32(b,pos),off=be32(b,pos+4);pos+=8;
            need(len<=end-pos&&off<ns,"设备树属性越界。");auto nul=b.find('\0',names+off);need(nul!=b.npos&&nul<names+ns,"设备树属性名越界。");
            auto name=b.substr(names+off,nul-(names+off));auto it=std::find_if(f.nodes.begin(),f.nodes.end(),[&](auto& n){return n.path==stack.back();});
            need(!name.empty()&&std::none_of(it->props.begin(),it->props.end(),[&](auto& v){return v.first==name;}),"设备树属性重复。");
            it->props.push_back({name,b.substr(pos,len)});pos=(pos+len+3)&~size_t(3);
        }else if(token==4)continue;
        else if(token==9){need(stack.empty(),"设备树没有闭合。");done=true;break;}
        else throw Error("设备树包含未知标记。");
    }
    need(done,"设备树结束标记缺失。");return f;
}
bool Fdt::has(const std::string& p)const{return std::any_of(nodes.begin(),nodes.end(),[&](auto& n){return n.path==p;});}
Bytes Fdt::get(const std::string& p,const std::string& key)const{for(auto& n:nodes)if(n.path==p)for(auto& v:n.props)if(v.first==key)return v.second;return {};}
void Fdt::set(const std::string& p,const std::string& key,const Bytes& value){for(auto& n:nodes)if(n.path==p){for(auto& v:n.props)if(v.first==key){v.second=value;return;}n.props.push_back({key,value});return;}throw Error("需要修改的设备树节点不存在。");}
void Fdt::add(const std::string& p){need(!has(p)&&has(parent(p)),"设备树新分区位置异常。");nodes.push_back({p,{}});}
std::map<std::string,std::map<std::string,Bytes>> Fdt::map()const{std::map<std::string,std::map<std::string,Bytes>> r;for(auto& n:nodes){r[n.path];for(auto& v:n.props)r[n.path][v.first]=v.second;}return r;}
Bytes Fdt::encode()const{
    Bytes names,tree;std::map<std::string,uint32_t> offsets;
    for(auto& n:nodes)for(auto& v:n.props)if(!offsets.count(v.first)){offsets[v.first]=uint32_t(names.size());names+=z(v.first);}
    std::set<std::string> visited;
    std::function<void(const std::string&)> emit=[&](auto& path){
        auto n=std::find_if(nodes.begin(),nodes.end(),[&](auto& item){return item.path==path;});need(n!=nodes.end()&&visited.insert(path).second,"设备树编码层级异常。");
        tree+=cells({1});tree+=z(path=="/"?"":path.substr(path.find_last_of('/')+1));align4(tree);
        for(auto& v:n->props){tree+=cells({3,uint32_t(v.second.size()),offsets.at(v.first)});tree+=v.second;align4(tree);}
        for(auto& child:nodes)if(child.path!="/"&&parent(child.path)==path)emit(child.path);
        tree+=cells({2});
    };
    emit(std::string("/"));need(visited.size()==nodes.size(),"设备树存在孤立节点。");tree+=cells({9});
    Bytes r(40,0);r+=reservations;align4(r);size_t os=r.size();r+=tree;size_t on=r.size();r+=names;
    for(auto [off,val]:std::initializer_list<std::pair<size_t,uint32_t>>{{0,0xd00dfeed},{4,uint32_t(r.size())},{8,uint32_t(os)},{12,uint32_t(on)},{16,40},{20,17},{24,16},{28,bootCpu},{32,uint32_t(names.size())},{36,uint32_t(tree.size())}})put32(r,off,val);
    need(parse(r).map()==map(),"设备树编码回读不一致。");return r;
}
static uint64_t octal(const Bytes& b,size_t p,size_t len){need(p<=b.size()&&len<=b.size()-p,"TAR 字段越界。");uint64_t n=0;bool ended=false;for(size_t i=0;i<len;i++){unsigned char c=b[p+i];if(c==0||c==' '){if(n)ended=true;continue;}need(!ended&&c>='0'&&c<='7'&&n<(1ull<<58),"TAR 数值编码不受支持。");n=n*8+c-'0';}return n;}
static std::string field(const Bytes& b,size_t p,size_t len){auto v=b.substr(p,len);auto n=v.find('\0');if(n!=v.npos)v.resize(n);return v;}
static std::string normalize(std::string p){while(p.size()>2&&starts(p,"./"))p.erase(0,2);while(p.size()>1&&p.back()=='/')p.pop_back();need(!p.empty()&&p.front()!='/'&&p.find('\\')==p.npos&&p.find('\0')==p.npos,"TAR 路径不合法。");std::stringstream stream(p);std::string part;while(std::getline(stream,part,'/'))need(!part.empty()&&part!=".."&&(part!="."||p=="."),"TAR 路径不合法。");return p;}
Archive untar(const Bytes& b){Archive files;size_t p=0;bool ended=false;uint64_t sumData=0;std::string pendingName,pendingLink;
    while(p+512<=b.size()){Bytes header=b.substr(p,512);if(header==Bytes(512,0)){need(p+1024<=b.size()&&b.substr(p+512,512)==Bytes(512,0),"TAR 结束块不完整。");for(size_t i=p;i<b.size();i++)need(b[i]==0,"TAR 结束后出现非零数据。");ended=true;break;}
        auto checksum=octal(header,148,8);std::fill(header.begin()+148,header.begin()+156,' ');uint32_t sum=0;for(unsigned char c:header)sum+=c;need(sum==checksum,"TAR 头校验失败。");
        need(header.substr(257,5)=="ustar","只支持标准 USTAR 归档。");
        auto rawSize=octal(header,124,12);char rawType=header[156]?header[156]:'0';
        if(rawType=='L'||rawType=='K'){
            need(rawSize>0&&rawSize<=4096&&p+512<=b.size()&&rawSize<=b.size()-p-512,"GNU 长路径记录异常。");
            auto value=b.substr(p+512,rawSize);while(!value.empty()&&value.back()=='\0')value.pop_back();need(!value.empty()&&value.find('\0')==value.npos,"GNU 长路径包含无效字符。");
            auto& target=rawType=='L'?pendingName:pendingLink;need(target.empty(),"GNU 长路径记录重复。");target=std::move(value);p+=512+((rawSize+511)/512)*512;continue;
        }
        Entry e;e.name=field(header,0,100);auto pre=field(header,345,155);if(!pre.empty())e.name=pre+"/"+e.name;if(!pendingName.empty()){e.name=std::move(pendingName);pendingName.clear();}e.name=normalize(e.name);
        e.mode=uint32_t(octal(header,100,8));e.uid=uint32_t(octal(header,108,8));e.gid=uint32_t(octal(header,116,8));uint64_t size=octal(header,124,12);e.mtime=octal(header,136,12);e.type=header[156]?header[156]:'0';e.link=field(header,157,100);e.major=uint32_t(octal(header,329,8));e.minor=uint32_t(octal(header,337,8));
        if(!pendingLink.empty()){e.link=std::move(pendingLink);pendingLink.clear();}
        need(e.mode<=07777&&std::string("023456").find(e.type)!=std::string::npos,"归档包含未知类型、硬链接或扩展记录，需要更新工具。");
        need(size<=512ull*1024*1024&&p+512<=b.size()&&size<=b.size()-p-512,"TAR 文件内容越界。");need(e.type=='0'||size==0,"特殊文件包含异常数据。");
        sumData+=size;need(sumData<=512ull*1024*1024&&files.size()<100000,"解压后的文件系统超过本工具的处理范围。");
        e.data=b.substr(p+512,size);need(files.emplace(e.name,std::move(e)).second,"归档包含重复路径。");p+=512+((size+511)/512)*512;
    }
    need(ended&&pendingName.empty()&&pendingLink.empty(),"TAR 归档没有正常结束。");return files;
}
static void number(Bytes& h,size_t p,size_t n,uint64_t v){std::ostringstream s;s<<std::oct<<v;auto t=s.str();need(t.size()<n,"TAR 数值过大。");h.replace(p,n,Bytes(n-1-t.size(),'0')+t+'\0');}
static void strfield(Bytes& h,size_t p,size_t len,const std::string& s){need(s.size()<=len,"归档文件名或链接过长，需要更新工具。");h.replace(p,s.size(),s);}
Bytes tar(const Archive& files,size_t padBlock){Bytes r;
    auto emit=[&](const Entry& e,const std::string& base,const std::string& pre){Bytes h(512,0);
        strfield(h,0,100,base);strfield(h,345,155,pre);number(h,100,8,e.mode);number(h,108,8,e.uid);number(h,116,8,e.gid);number(h,124,12,e.data.size());number(h,136,12,e.mtime);h[156]=e.type;strfield(h,157,100,e.link.substr(0,100));h.replace(257,8,Bytes("ustar\0" "00",8));number(h,329,8,e.major);number(h,337,8,e.minor);h.replace(148,8,8,' ');uint32_t sum=0;for(unsigned char c:h)sum+=c;number(h,148,7,sum);h[155]=' ';r+=h;r+=e.data;r.resize((r.size()+511)&~size_t(511),0);
    };
    for(auto& [name,e]:files){need(name==e.name&&normalize(name)==name,"待打包路径无效。");std::string base=name,pre;
        auto longRecord=[&](char type,const std::string& value){need(value.size()<4096,"文件名或链接超出处理范围。");Entry record;record.type=type;record.data=value+'\0';record.mtime=e.mtime;emit(record,"././@LongLink","");};
        if(base.size()>100){bool found=false;for(auto n=name.rfind('/');n!=name.npos;n=n?name.rfind('/',n-1):name.npos){if(n<=155&&name.size()-n-1<=100){base=name.substr(n+1);pre=name.substr(0,n);found=true;break;}}if(!found){longRecord('L',name);base=name.substr(0,100);}}
        if(e.link.size()>100)longRecord('K',e.link);
        emit(e,base,pre);
    }
    r.resize(r.size()+1024,0);r.resize((r.size()+padBlock-1)/padBlock*padBlock,0);return r;
}
Json manifest(const Archive& files){Json j=Json::object();for(auto& [name,e]:files){Json v={{"type",std::string(1,e.type)},{"mode",e.mode},{"uid",e.uid},{"gid",e.gid},{"mtime",e.mtime}};if(e.type=='0'){v["sha256"]=sha(e.data);v["size"]=e.data.size();}if(e.type=='2')v["link"]=e.link;if(e.type=='3'||e.type=='4'){v["major"]=e.major;v["minor"]=e.minor;}j[name]=v;}return j;}
Json firmwareMetadata(const Bytes& b){need(b.size()>=24&&b.size()<=512ull*1024*1024,"固件文件大小超出当前解析范围。");size_t p=b.size()-16;need(be32(b,p)==0x46577830,"未找到可识别的 OpenWrt 元数据；此镜像格式暂不支持。");need(uint8_t(b[p+8])==1,"固件含签名或未知尾部，需要专门适配。");
    size_t size=be32(b,p+12);need(size>=24&&size<=30*1024+24&&size<=b.size(),"固件元数据长度异常。");need(be32(b,p+4)==crc(b.substr(0,p),false),"固件 CRC 校验失败，请重新下载。");
    size_t start=b.size()-size;need(be32(b,start)==0&&be32(b,start+4)==0,"固件元数据版本变化。");
    Json meta;try{meta=Json::parse(b.substr(start+8,size-24));}catch(...){throw Error("固件元数据不是有效的 JSON。");}
    need(meta.is_object()&&meta.contains("version")&&meta["version"].is_object(),"镜像元数据缺少版本信息。");
    need(meta.contains("supported_devices")&&meta["supported_devices"].is_array()&&!meta["supported_devices"].empty(),"镜像元数据缺少设备标识。");
    for(auto& id:meta["supported_devices"])need(id.is_string()&&!id.get<std::string>().empty(),"镜像设备标识无效。");return meta;
}
Firmware unpackFirmware(const Bytes& b){auto meta=firmwareMetadata(b);auto size=be32(b,b.size()-4);auto entries=untar(b.substr(0,b.size()-size));std::string dir;
    for(auto& [name,e]:entries)if(e.type=='5'&&name.compare(0,11,"sysupgrade-")==0){need(dir.empty(),"固件包含多个根目录，需要专用容器适配。");dir=name;}
    need(!dir.empty()&&entries.size()==4&&entries.count(dir+"/CONTROL")&&entries.count(dir+"/kernel")&&entries.count(dir+"/root"),"当前转换引擎支持含 CONTROL / kernel / root 的 sysupgrade TAR；此容器需要单独适配。");
    for(auto n:{"CONTROL","kernel","root"})need(entries.at(dir+"/"+n).type=='0',"固件载荷类型错误。");
    need(entries.at(dir+"/CONTROL").data=="BOARD="+dir.substr(11)+"\n","CONTROL 与归档设备标识不匹配。");return {meta,std::move(entries),dir};
}
Bytes appendMetadata(Bytes b,const Json& meta){auto m=meta.dump()+"\n";need(m.size()<=30*1024,"元数据超出长度限制。");b+=Bytes(8,0);b+=m;Bytes t(16,0);put32(t,0,0x46577830);put32(t,4,crc(b,false));t[8]=1;put32(t,12,uint32_t(m.size()+24));b+=t;return b;}
}
