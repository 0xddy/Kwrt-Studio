#include "core.hpp"
#include <regex>
#include <sstream>

namespace ax {
// Read literal configuration only. Firmware scripts are never executed here.
Json detectNetworkDefaults(const Archive& files){
    std::map<std::string,std::string> values;std::set<std::string> sideValues;
    bool ambiguous=false;std::string localMode;std::map<std::string,std::string> localSettings;
    auto text=[&](const std::string& path){auto it=files.find(path);return it!=files.end()&&it->second.type=='0'?it->second.data:std::string();};
    auto unquote=[](std::string value){if(value.size()>=2&&((value.front()=='\''&&value.back()=='\'')||(value.front()=='"'&&value.back()=='"')))return value.substr(1,value.size()-2);return value;};
    for(auto config:{"wizard","network","dhcp"}){
        std::istringstream stream(text(std::string("etc/config/")+config));std::string line,section;
        std::regex sectionPattern("^[ \\t]*config[ \\t]+[^ \\t]+[ \\t]+([^ \\t]+)[ \\t]*$"),
            optionPattern("^[ \\t]*(?:option|list)[ \\t]+([^ \\t]+)[ \\t]+(.+?)[ \\t]*$");std::smatch match;
        while(std::getline(stream,line)){
            if(std::regex_match(line,match,sectionPattern))section=unquote(match[1]);
            else if(!section.empty()&&std::regex_match(line,match,optionPattern))values[std::string(config)+"."+section+"."+match[1].str()]=unquote(match[2]);
        }
    }
    if(values.count("wizard.default.siderouter"))sideValues.insert(values["wizard.default.siderouter"]);
    const std::regex assignment("^[ \\t]*uci[ \\t]+(?:-q[ \\t]+)?set[ \\t]+(wizard\\.default\\.(?:siderouter|lan_gateway|lan_dns|dhcp)|network\\.lan\\.(?:gateway|dns)|dhcp\\.lan\\.ignore)=['\"]?([A-Za-z0-9_.:-]+)['\"]?[ \\t]*$");
    const std::regex local("^LOCAL_ROUTING_MODE='(router|side)'[ \\t]*$");
    const std::regex localSetting("^(SIDE_GATEWAY|SIDE_DNS|SIDE_DHCP)='([0-9. ]*)'[ \\t]*$");
    for(auto& [path,entry]:files){
        if(entry.type!='0'||path.compare(0,17,"etc/uci-defaults/")!=0)continue;
        std::istringstream stream(entry.data);std::string line;std::smatch match;
        while(std::getline(stream,line)){
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            if(path=="etc/uci-defaults/zzzz-local-network"&&std::regex_match(line,match,local))localMode=match[1];
            if(path=="etc/uci-defaults/zzzz-local-network"&&std::regex_match(line,match,localSetting))localSettings[match[1]]=match[2];
            if(std::regex_match(line,match,assignment)){
                values[match[1]]=match[2];if(match[1]=="wizard.default.siderouter")sideValues.insert(match[2]);
            }else if(line.find("wizard.default.siderouter=")!=line.npos&&line.find("uci")!=line.npos)ambiguous=true;
        }
    }
    std::string mode="unknown",reason="固件没有提供明确的网络模式，请手动选择。";
    if(!localMode.empty()){mode=localMode;reason="按本工具写入的网络模式识别。";}
    else if(!ambiguous&&sideValues==std::set<std::string>{"1"}){mode="side";reason="固件已启用旁路由。";}
    else if(!ambiguous&&sideValues==std::set<std::string>{"0"}){mode="router";reason="固件已设置为主路由。";}
    else if(!ambiguous&&sideValues.empty()){
        const auto wizard=text("etc/init.d/wizard");
        if(values["network.wan.proto"]=="pppoe"){mode="router";reason="固件的 WAN 已设置为拨号。";}
        else if(!values["network.lan.gateway"].empty()&&values["network.wan.proto"]=="none"){mode="side";reason="固件通过 LAN 网关上网，WAN 已停用。";}
        else if(wizard.find("config_get siderouter")!=wizard.npos&&wizard.find("${siderouter}")!=wizard.npos){mode="router";reason="固件使用 KWRT 默认主路由模式。";}
    }
    std::string gateway=values["wizard.default.lan_gateway"];if(gateway.empty())gateway=values["network.lan.gateway"];
    std::string dns=values["wizard.default.lan_dns"];if(dns.empty())dns=values["network.lan.dns"];
    bool dhcp=values["wizard.default.dhcp"]!="0"&&values["dhcp.lan.ignore"]!="1";
    if(localMode=="side"){
        if(localSettings.count("SIDE_GATEWAY"))gateway=localSettings["SIDE_GATEWAY"];
        if(localSettings.count("SIDE_DNS"))dns=localSettings["SIDE_DNS"];
        if(localSettings.count("SIDE_DHCP"))dhcp=localSettings["SIDE_DHCP"]=="1";
    }
    return {{"mode",mode},{"reason",reason},{"gateway",gateway},{"dns",dns},{"dhcp",dhcp}};
}
}
