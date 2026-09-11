#include "adapter.hpp"

namespace ax {
const std::vector<std::unique_ptr<DeviceAdapter>>& deviceAdapters(){
    static auto adapters=[](){std::vector<std::unique_ptr<DeviceAdapter>> r;
        r.push_back(makeRedmiAx6000Adapter());
        // Register additional independently validated device adapters here.
        return r;
    }();return adapters;
}
const DeviceAdapter* findAdapter(const Json& meta){const DeviceAdapter* match=nullptr;
    for(auto& adapter:deviceAdapters())if(adapter->matches(meta)){need(!match,"多个设备适配器同时匹配，已停止。");match=adapter.get();}return match;
}
Json adapterCatalog(){Json entries=Json::array();for(auto& adapter:deviceAdapters()){auto i=adapter->info();entries.push_back({{"id",i.id},{"device",i.device},{"distributions",i.distributions},{"source_layout",i.sourceLayout},{"target_layout",i.targetLayout},{"output_board",i.outputBoard}});}return entries;}
Json inspectFirmware(const fs::path& path){auto bytes=read(path,512ull*1024*1024);auto meta=firmwareMetadata(bytes);auto v=meta["version"];
    Json result={{"filename",utf8(path.filename().wstring())},{"sha256",sha(bytes)},{"bytes",bytes.size()},{"distribution",v.value("dist",std::string("Unknown"))},{"version",v.value("version",std::string())},{"target",v.value("target",std::string())},{"board",v.value("board",std::string())},{"supported_devices",meta["supported_devices"]},{"metadata_crc_verified",true},{"conversion_supported",false}};
    if(auto adapter=findAdapter(meta)){auto info=adapter->info();result["conversion_supported"]=true;result["adapter_id"]=info.id;result["device"]=info.device;result["source_layout"]=info.sourceLayout;result["target_layout"]=info.targetLayout;result["status"]="已匹配设备规则；转换时继续核对真实分区与系统文件。";}
    else{result["device"]=result["board"];result["status"]="已识别固件；此设备与固件组合暂无已验证的转换适配器。";}
    return result;
}
}
