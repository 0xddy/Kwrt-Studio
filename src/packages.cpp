#include "core.hpp"
#include <algorithm>
#include <sstream>

namespace ax {
static std::string trim(const std::string& value) {
    auto first=value.find_first_not_of(" \t\r\n");
    if(first==value.npos)return {};
    return value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
}
static std::string displayName(const std::string& package) {
    static const std::map<std::string,std::string> names={
        {"luci-app-openclash","OpenClash"},{"luci-app-passwall","PassWall"},
        {"luci-app-passwall2","PassWall2"},{"luci-app-nekobox","NeKoBox"},
        {"luci-app-homeproxy","HomeProxy"},{"luci-app-ssr-plus","SSR Plus+"},
        {"luci-app-adguardhome","AdGuard Home"},{"luci-app-nikki","Nikki"},
        {"luci-app-momo","Momo"},{"luci-app-turboacc","网络加速（Turbo ACC）"},
        {"luci-app-ttyd","网页终端（ttyd）"},{"luci-app-nlbwmon","流量统计"},
        {"luci-app-frpc","内网穿透（frpc）"},{"luci-app-diskman","磁盘管理"},
        {"luci-app-syncdial","多线多拨"},{"luci-app-aria2","Aria2 下载"},
        {"luci-app-ddns","动态域名（DDNS）"},{"luci-app-upnp","UPnP"},
        {"luci-app-samba4","文件共享（Samba）"},{"luci-app-sqm","智能队列管理（SQM）"},
        {"luci-app-firewall","防火墙"},{"luci-app-package-manager","软件包管理"},
        {"luci-app-opkg","软件包管理"},{"luci-app-wizard","设置向导"}
    };
    auto found=names.find(package);
    if(found!=names.end())return found->second;
    return package.compare(0,9,"luci-app-")==0?package.substr(9):package;
}
Json parseInstalledPackages(const Bytes& database,const std::string& manager) {
    need(manager=="opkg"||manager=="apk","暂不支持这种软件包列表。");
    need(database.size()<=32ull*1024*1024&&database.find('\0')==database.npos,
         "软件包列表使用了暂不支持的格式。");
    Json result=Json::array();std::map<std::string,std::string> fields;
    std::set<std::string> names;std::string current;
    auto finish=[&](){
        const auto nameKey=manager=="opkg"?"Package":"P";
        const auto versionKey=manager=="opkg"?"Version":"V";
        if(!fields.count(nameKey)){fields.clear();current.clear();return;}
        std::string state="installed";
        if(manager=="opkg"){
            std::istringstream status(fields["Status"]);std::string selection,error;
            state.clear();status>>selection>>error>>state;
        }
        auto name=trim(fields[nameKey]),version=trim(fields[versionKey]);
        need(!name.empty()&&!version.empty()&&name.size()<256&&version.size()<1024,
             "软件包列表不完整，无法准确显示名称和版本。");
        need(names.insert(name).second,"软件包列表包含重复记录，无法准确读取。");
        bool plugin=name.compare(0,9,"luci-app-")==0;
        auto description=fields[manager=="opkg"?"Description":"T"];
        wide(name);wide(version);wide(description);
        std::string statusLabel=state=="installed"?"已安装":state=="config-files"?"仅配置残留":
            state.empty()?"未提供状态":"未标记为已安装";
        result.push_back({{"name",name},{"display_name",displayName(name)},
            {"version",version},{"description",description},{"is_plugin",plugin},
            {"installed",state=="installed"},{"state",state},{"status_label",statusLabel}});
        fields.clear();current.clear();
    };
    std::istringstream stream(database);std::string line;
    while(std::getline(stream,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty()){finish();continue;}
        if(manager=="opkg"&&(line[0]==' '||line[0]=='\t')){
            if(!current.empty())fields[current]+=" "+trim(line);
            continue;
        }
        auto separator=line.find(':');
        if(separator==line.npos){current.clear();continue;}
        current=line.substr(0,separator);
        // apk databases repeat file-related records; package metadata appears once.
        if(manager=="apk"&&current!="P"&&current!="V"&&current!="T")continue;
        fields[current]=trim(line.substr(separator+1));
    }
    finish();need(!result.empty(),"没有读到软件包信息，无法列出插件。");
    std::sort(result.begin(),result.end(),[](const Json& a,const Json& b){
        return a["name"].get<std::string>()<b["name"].get<std::string>();
    });
    return result;
}
}
