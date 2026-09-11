#include "router_check.hpp"
#include "adapter.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace ax {
std::string base64Encode(const Bytes& b){
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for(size_t i=0;i<b.size();i+=3){uint32_t v=uint32_t(uint8_t(b[i]))<<16;
        if(i+1<b.size())v|=uint32_t(uint8_t(b[i+1]))<<8;if(i+2<b.size())v|=uint8_t(b[i+2]);
        out+=alphabet[v>>18];out+=alphabet[(v>>12)&63];out+=i+1<b.size()?alphabet[(v>>6)&63]:'=';out+=i+2<b.size()?alphabet[v&63]:'=';
    }return out;
}
Bytes base64Decode(const std::string& text){
    need(text.size()%4==0,"检测数据编码不完整。");Bytes out;
    const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for(size_t i=0;i<text.size();i+=4){uint32_t v=0;int padding=0;
        for(size_t j=0;j<4;j++){char c=text[i+j];if(c=='='){need(j>=2&&i+4==text.size(),"检测数据编码错误。");padding++;v<<=6;}
            else{auto n=alphabet.find(c);need(n!=alphabet.npos&&padding==0,"检测数据编码错误。");v=(v<<6)|uint32_t(n);}}
        need(padding<=2,"检测数据编码错误。");out+=char(v>>16);if(padding<2)out+=char(v>>8);if(!padding)out+=char(v);
    }need(base64Encode(out)==text,"检测数据不是有效的 Base64 编码。");return out;
}
std::vector<std::pair<std::string,Bytes>> routerProbeCommands(){
    // Each file travels in its own binary-safe SSH channel. No encoding utility,
    // temporary remote file, or caller-provided shell fragment is needed.
    return {
        {"board","if [ -r /tmp/sysinfo/board_name ]; then head -c 1048576 /tmp/sysinfo/board_name; fi"},
        {"mtd","if [ -r /proc/mtd ]; then head -c 1048576 /proc/mtd; fi"},
        {"fdt","if [ -r /sys/firmware/fdt ]; then head -c 1048576 /sys/firmware/fdt; fi"},
        {"cmdline","if [ -r /proc/cmdline ]; then head -c 1048576 /proc/cmdline; fi"},
        {"mounts","if [ -r /proc/mounts ]; then head -c 1048576 /proc/mounts; fi"},
        {"offsets",R"SH(for p in /sys/class/mtd/mtd[0-9]*; do
  [ -d "$p" ] || continue
  [ -r "$p/offset" ] || continue
  printf '%s ' "${p##*/}"
  cat "$p/offset"
  printf '\n'
done
)SH"}};
}
static std::string clean(std::string s){
    while(!s.empty()&&(s.back()=='\n'||s.back()=='\r'||s.back()=='\0'))s.pop_back();
    for(auto& c:s)if(uint8_t(c)<32)c=' ';return s;
}
Json parseRouterProbe(const Bytes& output){
    need(output.size()<=3*1024*1024,"检测返回的数据过大。");
    std::istringstream input(output);std::string line,key;std::map<std::string,Bytes> fields;
    const std::set<std::string> allowed={"board","mtd","fdt","cmdline","mounts","offsets"};
    need(bool(std::getline(input,line))&&line=="KWRTSTUDIO-PROBE-1","路由器没有返回完整的检测数据。");bool ended=false;
    while(std::getline(input,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line=="KWRTSTUDIO-END"){ended=true;break;}
        if(line.empty())continue;
        if(line.front()=='@'){key=line.substr(1);need(allowed.count(key)&&!fields.count(key),"检测数据的字段重复或未知。");fields[key]="";}
        else{need(!key.empty(),"检测数据缺少字段名称。");fields[key]+=line;}
    }
    need(ended&&fields.size()==allowed.size(),"检测中断，未取得完整信息。");
    while(std::getline(input,line))need(line.empty()||line=="\r","检测数据结尾异常。");
    for(auto& [name,value]:fields)value=base64Decode(value);
    Json snapshot={{"board",clean(fields["board"])},{"cmdline",clean(fields["cmdline"])},{"mounts",fields["mounts"]},{"partitions",Json::array()},{"tree_partitions",Json::array()},{"tree_valid",false}};
    std::map<std::string,uint64_t> offsets;
    std::istringstream offsetLines(fields["offsets"]);
    std::regex offsetPattern(R"(^(mtd[0-9]+) ([0-9]+|0x[0-9a-fA-F]+)$)");
    while(std::getline(offsetLines,line)){if(line.empty())continue;std::smatch m;need(std::regex_match(line,m,offsetPattern),"MTD 偏移信息格式异常。");need(!offsets.count(m[1]),"MTD 偏移信息重复。");auto value=m[2].str();offsets[m[1]]=std::stoull(value,nullptr,value.rfind("0x",0)==0?16:10);}
    std::regex mtdPattern(R"re(^mtd([0-9]+):\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+"([^"]+)"\s*$)re");
    std::set<std::string> ids;std::istringstream mtdLines(fields["mtd"]);
    while(std::getline(mtdLines,line)){
        if(line.empty()||line.find("dev:")==0)continue;std::smatch m;need(std::regex_match(line,m,mtdPattern),"MTD 分区信息格式异常。");
        auto id="mtd"+m[1].str();need(ids.insert(id).second,"MTD 分区记录重复。");
        Json p={{"id",id},{"name",m[4].str()},{"size",std::stoull(m[2],nullptr,16)},{"erase",std::stoull(m[3],nullptr,16)}};
        if(offsets.count(id))p["offset"]=offsets[id];snapshot["partitions"].push_back(p);
    }
    if(!fields["fdt"].empty())try{
        auto tree=Fdt::parse(fields["fdt"]);snapshot["model"]=clean(tree.get("/","model"));
        snapshot["compatible"]=tree.get("/","compatible");
        for(const auto& node:tree.nodes){
            auto end=node.path.rfind('/');if(end==std::string::npos)continue;auto parent=node.path.substr(0,end);
            if(tree.get(parent,"compatible")!=Bytes("fixed-partitions\0",17))continue;
            auto reg=tree.get(node.path,"reg"),label=tree.get(node.path,"label");
            // This parser exposes only unambiguous single-cell offset/size layouts.
            need(tree.get(parent,"#address-cells")==cells({1})&&tree.get(parent,"#size-cells")==cells({1})&&reg.size()==8&&!label.empty(),"设备树分区格式尚不支持。");
            snapshot["tree_partitions"].push_back({{"path",node.path},{"parent",parent},{"name",clean(label)},{"offset",be32(reg,0)},{"size",be32(reg,4)}});
        }
        snapshot["nmbm"]=tree.map()["/soc/spi@1100a000/flash@0"].count("mediatek,nmbm")!=0;snapshot["tree_valid"]=true;
    }catch(const std::exception&){snapshot["tree_partitions"]=Json::array();snapshot["tree_error"]="未能读取受支持的设备树分区信息。";}
    return snapshot;
}
Json adviseRouter(const Json& snapshot,const fs::path& path){
    Json result={{"snapshot",snapshot},{"level","unknown"},{"layout","未识别"},{"title","暂时无法判断分区是否匹配"},{"advice","已显示读取到的信息。此机型或分区方式暂无匹配规则，请按设备的刷机说明核对。"}};
    const DeviceAdapter* routerAdapter=nullptr;
    for(auto& adapter:deviceAdapters()){auto advice=adapter->routerAdvice(snapshot);if(advice.is_null())continue;need(!routerAdapter,"设备检测规则存在歧义。");routerAdapter=adapter.get();result.update(advice);}
    if(path.empty())result["firmware_note"]="尚未选择固件；当前只显示路由器的布局建议。";
    else try{
        auto firmware=unpackFirmware(read(path,512ull*1024*1024));auto adapter=findAdapter(firmware.metadata);
        need(adapter,"所选固件暂无受支持的转换规则。");adapter->validateInput(firmware);
        auto kernel=firmware.files.at(firmware.archiveRoot+"/kernel").data.size();auto root=firmware.files.at(firmware.archiveRoot+"/root").data.size();
        adapter->availableDataBytes(kernel,root);
        result["firmware_note"]="所选固件："+utf8(path.filename().wstring())+"\n转换目标："+adapter->info().targetLayout;
        if(routerAdapter!=adapter){result["level"]="mismatch";result["title"]="路由器与所选固件尚未匹配";result["advice"]="不要将此转换结果用于这台路由器。请确认型号、分区及对应的固件。";}
    }catch(const std::exception& e){result["level"]="unknown";result["title"]="所选固件暂时无法完成比对";result["advice"]="当前只识别了路由器布局，尚未确认所选固件。请先选择受支持的原始固件再比对。";result["firmware_note"]=std::string(e.what());}
    return result;
}
std::string routerAdviceText(const Json& r){
    return r.value("title","")+"\r\n\r\n"+r.value("advice","")+"\r\n\r\n"+r.value("firmware_note","")+"\r\n\r\n此结果仅供参考，不确认引导程序或完整刷机兼容性；检测不会修改路由器，也不限制转换。";
}
}
