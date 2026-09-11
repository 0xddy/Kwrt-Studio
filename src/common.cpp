#include "core.hpp"
#include <fstream>
#include <algorithm>
#include <array>
#include <regex>
#include <sstream>
#include <memory>

namespace ax {
void need(bool c, const std::string& m) { if(!c) throw Error(m); }
std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    need(n>0,"文本不是有效的 UTF-8 格式。"); std::wstring r(n,0);
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),r.data(),n); return r;
}
std::string utf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    need(n>0,"文本包含无效字符。"); std::string r(n,0);
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),r.data(),n,nullptr,nullptr); return r;
}
Bytes read(const fs::path& p,size_t limit) {
    std::ifstream f(p,std::ios::binary|std::ios::ate); need(bool(f),"无法读取文件，请检查路径和访问权限。");
    auto n=f.tellg(); need(n>=0 && uint64_t(n)<=limit,"文件过大或无法读取，已停止。");
    Bytes b(size_t(n),'\0'); f.seekg(0); if(n) f.read(b.data(),n);
    need(bool(f),"文件读取不完整。"); return b;
}
void write(const fs::path& p,const Bytes& b) {
    std::ofstream f(p,std::ios::binary|std::ios::trunc); need(bool(f),"无法写入文件，请检查输出目录。");
    f.write(b.data(),std::streamsize(b.size())); f.flush(); need(bool(f),"磁盘写入失败，请检查剩余空间。");
}
void writeNew(const fs::path& p,const Bytes& b) {
    HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    need(h!=INVALID_HANDLE_VALUE,"输出文件已存在或目录不可写，不会覆盖已有文件。");
    DWORD written=0; bool ok=WriteFile(h,b.data(),DWORD(b.size()),&written,nullptr) && written==b.size() && FlushFileBuffers(h);
    CloseHandle(h); need(ok,"磁盘写入失败，未生成有效的刷机文件。");
}
uint32_t be32(const Bytes& b,size_t p) {need(p<=b.size() && b.size()-p>=4,"镜像结构越界。");uint32_t r=0;for(int i=0;i<4;i++)r=(r<<8)|uint8_t(b[p+i]);return r;}
uint32_t le32(const Bytes& b,size_t p) {need(p<=b.size() && b.size()-p>=4,"镜像结构越界。");uint32_t r=0;for(int i=3;i>=0;i--)r=(r<<8)|uint8_t(b[p+i]);return r;}
uint64_t le64(const Bytes& b,size_t p) {return le32(b,p)|(uint64_t(le32(b,p+4))<<32);}
void put32(Bytes& b,size_t p,uint32_t v) {need(p<=b.size() && b.size()-p>=4,"写入结构越界。");for(int i=3;i>=0;i--){b[p+i]=char(v);v>>=8;}}
Bytes cells(std::initializer_list<uint32_t> v) {Bytes r(v.size()*4,0);size_t i=0;for(auto x:v){put32(r,i,x);i+=4;}return r;}
std::string hex(const Bytes& b) {const char* h="0123456789abcdef";std::string r;r.reserve(b.size()*2);for(unsigned char c:b){r+=h[c>>4];r+=h[c&15];}return r;}

class Hasher {
    BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE handle=nullptr;
    std::vector<UCHAR> object; DWORD size=0;
public:
    explicit Hasher(const wchar_t* name) {
        need(BCryptOpenAlgorithmProvider(&alg,name,nullptr,0)>=0,"Windows 加密服务不可用。");
        DWORD n=0,len=0; auto a=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&len,sizeof(len),&n,0);
        auto b=BCryptGetProperty(alg,BCRYPT_HASH_LENGTH,(PUCHAR)&size,sizeof(size),&n,0);object.resize(len);
        if(a<0||b<0||BCryptCreateHash(alg,&handle,object.data(),len,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(alg,0);throw Error("校验计算初始化失败。");}
    }
    ~Hasher(){if(handle)BCryptDestroyHash(handle);if(alg)BCryptCloseAlgorithmProvider(alg,0);}
    void add(const char* p,size_t n){need(n<=UINT32_MAX && BCryptHashData(handle,(PUCHAR)p,ULONG(n),0)>=0,"校验计算失败。");}
    Bytes finish(){Bytes r(size,0);need(BCryptFinishHash(handle,(PUCHAR)r.data(),size,0)>=0,"校验计算失败。");return r;}
};
Bytes hash(const Bytes& b,const wchar_t* name){Hasher h(name);h.add(b.data(),b.size());return h.finish();}
std::string sha(const Bytes& b){return hex(hash(b));}
std::string shaFile(const fs::path& p){std::ifstream f(p,std::ios::binary);need(bool(f),"无法读取校验文件。");Hasher h(BCRYPT_SHA256_ALGORITHM);std::vector<char>b(1024*1024);while(f){f.read(b.data(),b.size());h.add(b.data(),size_t(f.gcount()));}need(f.eof(),"校验文件读取失败。");return hex(h.finish());}
uint32_t crc(const Bytes& b,bool complement){static const auto table=[](){std::array<uint32_t,256>t{};for(uint32_t i=0;i<256;i++){uint32_t v=i;for(int j=0;j<8;j++)v=(v>>1)^((v&1)?0xedb88320:0);t[i]=v;}return t;}();uint32_t v=0xffffffff;for(unsigned char c:b)v=table[(v^c)&255]^(v>>8);return complement?~v:v;}
std::string randomToken(){std::array<unsigned char,16>b{};need(BCryptGenRandom(nullptr,b.data(),ULONG(b.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"无法生成随机值。");return hex(Bytes((char*)b.data(),b.size()));}

// SHA-512 crypt with the standard default of 5000 rounds. Password hashing
// uses Windows CNG; no external executable or password command line is used.
std::string crypt512(const std::string& pw,const std::string& salt) {
    need(!pw.empty()&&pw.size()<=1024 && !salt.empty()&&salt.size()<=16,"后台密码或散列参数不合法。");
    auto h=[](const Bytes& b){return hash(b,BCRYPT_SHA512_ALGORITHM);};
    auto repeat=[](const Bytes& s,size_t n){Bytes r;while(r.size()<n)r.append(s,0,std::min(s.size(),n-r.size()));return r;};
    Bytes alt=h(pw+salt+pw), a=pw+salt+repeat(alt,pw.size());
    for(size_t n=pw.size();n;n>>=1)a+=(n&1)?alt:pw;
    alt=h(a); Bytes dp;for(size_t i=0;i<pw.size();i++)dp+=pw;Bytes p=repeat(h(dp),pw.size());
    Bytes ds;for(size_t i=0;i<16+uint8_t(alt[0]);i++)ds+=salt;Bytes s=repeat(h(ds),salt.size());
    for(int i=0;i<5000;i++){Bytes t=(i&1)?p:alt;if(i%3)t+=s;if(i%7)t+=p;t+=(i&1)?alt:p;alt=h(t);}
    const char* enc="./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string out="$6$"+salt+"$";
    auto emit=[&](int b2,int b1,int b0,int count){uint32_t n=(uint32_t(b2)<<16)|(uint32_t(b1)<<8)|uint32_t(b0);while(count--){out+=enc[n&63];n>>=6;}};
    const int order[][3]={{0,21,42},{22,43,1},{44,2,23},{3,24,45},{25,46,4},{47,5,26},{6,27,48},{28,49,7},{50,8,29},{9,30,51},{31,52,10},{53,11,32},{12,33,54},{34,55,13},{56,14,35},{15,36,57},{37,58,16},{59,17,38},{18,39,60},{40,61,19},{62,20,41}};
    for(auto& q:order)emit(uint8_t(alt[q[0]]),uint8_t(alt[q[1]]),uint8_t(alt[q[2]]),4);emit(0,0,uint8_t(alt[63]),2);return out;
}
Json readJson(const fs::path& p){try{return Json::parse(read(p,128*1024));}catch(const Error&){throw;}catch(...){throw Error("配置文件格式不正确，请使用有效的 JSON 配置。");}}
static uint32_t ipv4(const std::string& s){std::regex r("([0-9]{1,3})\\.([0-9]{1,3})\\.([0-9]{1,3})\\.([0-9]{1,3})");std::smatch m;need(std::regex_match(s,m,r),"后台地址或子网掩码格式不正确。");uint32_t v=0;for(int i=1;i<5;i++){auto x=std::stoul(m[i]);need(x<=255,"IP 地址必须在 0–255 范围内。");v=(v<<8)|x;}return v;}
void validateProfile(const Json& p){
    const std::set<std::string> keys={"lan_ip","netmask","pppoe_username","pppoe_password","wifi_ssid","wifi_password","admin_password","country","ipv6"};
    const std::set<std::string> optional={"hostname","signature","remove_author_links","routing_mode","side_gateway","side_dns","side_dhcp"};
    need(p.is_object()&&p.size()>=keys.size(),"配置字段不完整，请检查网络设置。");
    for(auto it=p.begin();it!=p.end();++it)need(keys.count(it.key())||optional.count(it.key()),"配置包含不支持的设置。");
    for(auto key:{"hostname","signature"})if(p.contains(key)){
        need(p[key].is_string(),"主机名和签名应为文本。");auto value=p[key].get<std::string>();
        need(value.size()<=128,"主机名或签名过长。");for(unsigned char c:value)need(c>=32&&c!=127,"主机名和签名不能包含换行或控制字符。");wide(value);
    }
    if(p.contains("remove_author_links"))need(p["remove_author_links"].is_boolean(),"去除作者外链应为开或关。");
    auto hostname=p.value("hostname",std::string());
    need(hostname.empty()||std::regex_match(hostname,std::regex("[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?")),"主机名请使用 1–63 位字母、数字或短横线，首尾不能是短横线。");
    auto mode=p.value("routing_mode",std::string("router"));need(mode=="router"||mode=="side","请先选择主路由或旁路由模式。");
    if(p.contains("side_dhcp"))need(p["side_dhcp"].is_boolean(),"DHCP 设置应为开或关。");
    for(auto key:{"side_gateway","side_dns"})if(p.contains(key))need(p[key].is_string(),"网关和 DNS 应为文本。");
    for(auto& k:keys){need(p.contains(k),"配置字段不完整。");if(k=="ipv6"){need(p[k].is_boolean(),"IPv6 配置应为开或关。");continue;}
        need(p[k].is_string(),"配置必须为文本。");auto s=p[k].get<std::string>();bool unused=mode=="side"&&(k=="pppoe_username"||k=="pppoe_password"||k=="wifi_ssid"||k=="wifi_password"||k=="country");need((unused||!s.empty())&&s.size()<=1024&&s.find_first_of(Bytes("\0\r\n",3))==s.npos,"各项配置不能为空，也不能包含换行。");wide(s);
        need(s.find("填写")==s.npos,"请先填写账号和密码。");}
    auto ip=ipv4(p["lan_ip"]),mask=ipv4(p["netmask"]),inv=~mask;
    need(mask && (inv&(inv+1))==0 && inv>=3,"子网掩码必须连续，且至少可容纳两台设备。");
    bool priv=(ip>>24)==10||(ip>>20)==0xac1||(ip>>16)==0xc0a8;
    need(priv&&(ip&inv)!=0&&(ip&inv)!=inv,"后台地址需为局域网内有效的主机地址。");
    if(mode=="side"){
        need(!p.value("side_gateway",std::string()).empty(),"请填写主路由网关地址。");
        auto gateway=ipv4(p.value("side_gateway",std::string()));
        need(gateway!=ip&&(gateway&mask)==(ip&mask)&&(gateway&inv)!=0&&(gateway&inv)!=inv,"主路由网关需与后台地址在同一网段，且不能使用相同地址。");
        auto dns=p.value("side_dns",std::string());need(dns.size()<=128&&dns.find_first_of(Bytes("\0\r\n",3))==dns.npos,"DNS 格式不正确。");
        std::istringstream servers(dns);std::string address;int count=0;while(servers>>address){auto value=ipv4(address);need(value&&value!=0xffffffff&&(value>>24)<224,"DNS 地址不正确。");count++;}need(count<=3,"最多填写三个 DNS 地址，用空格分隔。");return;
    }
    auto ssid=p["wifi_ssid"].get<std::string>();need(ssid.size()+5<=32,"Wi-Fi 名称加频段后不得超过 32 字节。");
    auto key=p["wifi_password"].get<std::string>();need((key.size()>=8&&key.size()<=63)||std::regex_match(key,std::regex("[0-9a-fA-F]{64}")),"Wi-Fi 密码需要 8–63 字节，或 64 位十六进制 PSK。");
    need(std::regex_match(p["country"].get<std::string>(),std::regex("[A-Z]{2}")),"无线国家代码应为两位大写字母，例如 CN。");
}
static std::string quote(const std::string& s){std::string r="'";for(char c:s){if(c=='\'')r+="'\"'\"'";else r+=c;}return r+"'";}
Bytes renderInit(const Json& p,Bytes initTemplate){validateProfile(p);std::map<std::string,std::string> values={{"LAN_IP",p["lan_ip"]},{"LAN_MASK",p["netmask"]},{"PPPOE_USER",p["pppoe_username"]},{"PPPOE_PASS",p["pppoe_password"]},{"SSID",p["wifi_ssid"]},{"WIFI_KEY",p["wifi_password"]},{"COUNTRY",p["country"]},{"IPV6",p["ipv6"].get<bool>()?"1":"0"},{"ROOT_HASH",crypt512(p["admin_password"],randomToken().substr(0,16))}};
    values["LOCAL_ROUTING_MODE"]=p.value("routing_mode",std::string("router"));values["SIDE_GATEWAY"]=p.value("side_gateway",std::string());values["SIDE_DNS"]=p.value("side_dns",std::string());values["SIDE_DHCP"]=p.value("side_dhcp",false)?"1":"0";
    {std::istringstream servers(values["SIDE_DNS"]);std::string address,normalized;while(servers>>address){if(!normalized.empty())normalized+=' ';normalized+=address;}values["SIDE_DNS"]=normalized;}
    if(values["LOCAL_ROUTING_MODE"]=="side"){for(auto key:{"PPPOE_USER","PPPOE_PASS","SSID","WIFI_KEY","COUNTRY"})values[key]="";values["IPV6"]="0";}
    values["LOCAL_HOSTNAME"]=p.value("hostname",std::string());values["REMOVE_AUTHOR_LINKS"]=p.value("remove_author_links",false)?"1":"0";
    Bytes t=initTemplate,s;for(auto& [k,v]:values)s+=k+"="+quote(v)+"\n";auto pos=t.find("@@SETTINGS@@");need(pos!=t.npos,"初始化模板损坏。");t.replace(pos,12,s);return t;
}
}
