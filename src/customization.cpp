#include "core.hpp"
#include <sstream>

namespace ax {
static std::string osReleaseEscape(const std::string& value){
    std::string out;for(char c:value){if(c=='\\'||c=='"'||c=='$'||c=='`')out+='\\';out+=c;}return out;
}
std::vector<std::string> applyFirmwarePresentation(Archive& files,const Json& profile){
    std::vector<std::string> changed;
    if(profile.value("remove_author_links",false)){
        auto& config=regular(files,"etc/config/base_config").data;
        auto& view=regular(files,"usr/lib/lua/luci/view/admin_status/index/links.htm").data;
        need(config.find("config status")!=config.npos&&view.find("base_config.@status[0].links")!=view.npos,
             "这份固件没有已支持的作者外链开关，暂时不能使用此选项。");
    }
    auto signature=profile.value("signature",std::string());if(signature.empty())return changed;
    auto& os=regular(files,"usr/lib/os-release").data;
    auto& banner=regular(files,"etc/banner").data;
    std::istringstream stream(os);std::string line,revised;int manufacturer=0,release=0;
    while(std::getline(stream,line)){
        if(line.rfind("OPENWRT_DEVICE_MANUFACTURER=\"",0)==0){
            line="OPENWRT_DEVICE_MANUFACTURER=\""+osReleaseEscape(signature)+"\"";manufacturer++;
        }else if(line.compare(0,17,"OPENWRT_RELEASE=\"")==0){
            auto position=line.find(" by Kiddin'");
            need(position!=line.npos&&line.substr(position)==" by Kiddin'\"",
                 "这份固件的签名位置已变化，暂时无法替换签名。");
            line=line.substr(0,position)+" by "+osReleaseEscape(signature)+"\"";release++;
        }
        revised+=line+'\n';
    }
    const std::string author="Kiddin'";auto position=banner.find(author);
    need(manufacturer==1&&release==1&&position!=banner.npos&&banner.find(author,position+author.size())==banner.npos,
         "这份固件的签名位置与已支持的版本不同，未修改签名。");
    os=std::move(revised);banner.replace(position,author.size(),signature);
    changed.push_back("usr/lib/os-release");changed.push_back("etc/banner");return changed;
}
}
