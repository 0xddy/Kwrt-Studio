#include "router_check.hpp"
#include <sstream>
#include <iomanip>

namespace ax {
Json routerCheckTests(){
    Json tests=Json::array();auto check=[&](const char* name,bool ok){need(ok,std::string("自检失败：")+name);tests.push_back(name);};
    check("probe binary Base64 roundtrip",base64Decode(base64Encode(Bytes("\0\xff\xfe\x01\x02",5)))==Bytes("\0\xff\xfe\x01\x02",5));
    for(auto bad:{"a","ab=c","====","Zh=="}){bool rejected=false;try{base64Decode(bad);}catch(...){rejected=true;}check("reject invalid Base64 framing",rejected);}
    auto nul=[](const std::string& v){return v+'\0';};
    Fdt tree;tree.reservations=Bytes(16,0);tree.nodes.push_back({"/",{}});
    for(auto path:{"/soc","/soc/spi@1100a000","/soc/spi@1100a000/flash@0","/soc/spi@1100a000/flash@0/partitions"})tree.add(path);
    const std::string parent="/soc/spi@1100a000/flash@0/partitions";
    tree.set("/","model",nul("Xiaomi Redmi Router AX6000 (stock layout)"));
    tree.set("/","compatible",nul("xiaomi,redmi-router-ax6000-stock")+nul("mediatek,mt7986a"));
    tree.set("/soc/spi@1100a000/flash@0","mediatek,nmbm","");tree.set(parent,"compatible",nul("fixed-partitions"));
    tree.set(parent,"#address-cells",cells({1}));tree.set(parent,"#size-cells",cells({1}));
    struct Part{const char* name;uint32_t offset,size;};
    Part parts[]={{"BL2",0,0x100000},{"Nvram",0x100000,0x40000},{"Bdata",0x140000,0x40000},{"Factory",0x180000,0x200000},{"FIP",0x380000,0x200000},{"crash",0x580000,0x40000},{"crash_log",0x5c0000,0x40000},{"ubi_kernel",0x600000,0x1e00000},{"ubi",0x2400000,0x5000000}};
    std::string mtd="dev: size erasesize name\nmtd0: 08000000 00020000 \"spi0.1\"\n",offsets;
    int index=1;for(auto p:parts){std::ostringstream hexOffset;hexOffset<<std::hex<<p.offset;auto path=parent+"/partition@"+hexOffset.str();tree.add(path);tree.set(path,"label",nul(p.name));tree.set(path,"reg",cells({p.offset,p.size}));
        std::ostringstream row;row<<"mtd"<<index<<": "<<std::hex<<std::setw(8)<<std::setfill('0')<<p.size<<" 00020000 \""<<p.name<<"\"\n";mtd+=row.str();
        offsets+="mtd"+std::to_string(index++)+" "+std::to_string(p.offset)+"\n";
    }
    std::map<std::string,Bytes> fields={{"board","xiaomi,redmi-router-ax6000-stock\n"},{"mtd",mtd},{"fdt",tree.encode()},{"cmdline","console=ttyS0,115200\n"},{"mounts","/dev/root /rom squashfs ro 0 0\noverlayfs:/overlay / overlay rw 0 0\n"},{"offsets",offsets}};
    auto frame=[](const auto& data){std::string text="KWRTSTUDIO-PROBE-1\n";for(const auto& [key,value]:data)text+="@"+key+"\n"+base64Encode(value)+"\n";return text+"KWRTSTUDIO-END\n";};
    auto snapshot=parseRouterProbe(frame(fields));check("MTD parent device does not shift child offsets",snapshot["partitions"][8]["offset"]==0x600000);
    check("AX6000 stock layout matched from independent DT and MTD",adviseRouter(snapshot)["level"]=="match");
    auto changed=fields;changed["offsets"]="";check("DT offsets are accepted without inventing MTD offsets",adviseRouter(parseRouterProbe(frame(changed)))["level"]=="match");
    changed["fdt"]="";check("partition names and sizes alone are insufficient",adviseRouter(parseRouterProbe(frame(changed)))["level"]=="unknown");
    changed=fields;changed["fdt"]="broken";check("malformed DT cannot pass",adviseRouter(parseRouterProbe(frame(changed)))["level"]=="unknown");
    auto wrong=snapshot;wrong["partitions"][8]["offset"]=0x800000;check("actual offset conflict cannot pass",adviseRouter(wrong)["level"]=="unknown");
    wrong=snapshot;wrong["tree_partitions"][8]["offset"]=0x2500000;wrong["partitions"][9].erase("offset");check("same size with shifted partition rejected",adviseRouter(wrong)["level"]=="mismatch");
    wrong=snapshot;wrong["partitions"][9]["size"]=0x5100000;check("DT and MTD size conflict cannot pass",adviseRouter(wrong)["level"]=="unknown");
    wrong=snapshot;wrong["partitions"].push_back(wrong["partitions"][9]);check("duplicate partition names cannot pass",adviseRouter(wrong)["level"]=="unknown");
    wrong=snapshot;wrong["partitions"].push_back({{"id","mtd20"},{"name","unknown-overlap"},{"size",0x5000000},{"erase",0x20000}});check("extra unknown MTD partition cannot pass",adviseRouter(wrong)["level"]=="unknown");
    wrong=snapshot;wrong["board"]="xiaomi,redmi-router-ax6000-ubootmod";check("U-Boot mod is not confused with stock",adviseRouter(wrong)["level"]=="unknown");
    wrong=snapshot;wrong["board"]="other,router";check("unrecognized router gets no positive recommendation",adviseRouter(wrong)["level"]=="unknown");
    wrong=snapshot;wrong["compatible"]=nul("other,router");check("board and DT identity conflict cannot pass",adviseRouter(wrong)["level"]=="unknown");
    wrong=snapshot;wrong["mounts"]="rootfs / rootfs rw 0 0\n";check("recovery boot needs further verification",adviseRouter(wrong)["level"]=="unknown");
    wrong=snapshot;wrong["cmdline"]="mtdparts=spi0.1:-(other)";check("boot argument partition override cannot pass",adviseRouter(wrong)["level"]=="unknown");
    auto single=snapshot;single["board"]="xiaomi,redmi-router-ax6000";single["compatible"]=nul("xiaomi,redmi-router-ax6000")+nul("mediatek,mt7986a");
    single["tree_partitions"].erase(8);single["tree_partitions"][7]["name"]="ubi";single["tree_partitions"][7]["size"]=0x6e00000;
    single["partitions"].erase(9);single["partitions"][8]["name"]="ubi";single["partitions"][8]["size"]=0x6e00000;
    check("single 110 MiB layout advises against stock conversion",adviseRouter(single)["level"]=="mismatch");
    check("bad selected firmware cannot get matching recommendation",adviseRouter(snapshot,fs::path(L"?:/missing-firmware.bin"))["level"]=="unknown");
    for(auto malformed:{std::string("KWRTSTUDIO-PROBE-1\n"),frame(fields)+"extra",std::string("banner\n")+frame(fields)}){
        bool rejected=false;try{parseRouterProbe(malformed);}catch(...){rejected=true;}check("incomplete or contaminated probe rejected",rejected);}
    auto duplicate=frame(fields);duplicate.insert(duplicate.find("KWRTSTUDIO-END"),"@board\n\n");bool rejected=false;try{parseRouterProbe(duplicate);}catch(...){rejected=true;}check("duplicate probe fields rejected",rejected);
    std::string command;for(auto& item:routerProbeCommands())command+=item.second;
    check("fixed probe excludes write and flash commands",command.find("sysupgrade")==command.npos&&command.find("fw_setenv")==command.npos&&command.find("uci ")==command.npos&&command.find("reboot")==command.npos);
    check("router does not need encoding utilities",command.find("base64")==command.npos&&command.find("openssl")==command.npos&&command.find("python")==command.npos);
    return tests;
}
}
