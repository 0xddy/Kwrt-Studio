#include "core.hpp"
#include "router_check.hpp"

namespace ax {
Json selfTest(){Json tests=Json::array();
    auto check=[&](const char* name,bool ok){need(ok,std::string("自检失败：")+name);tests.push_back(name);};
    check("SHA256",sha("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check("SHA1",hex(hash("abc",BCRYPT_SHA1_ALGORITHM))=="a9993e364706816aba3e25717850c26c9cd0d89d");
    check("CRC32",crc("123456789")==0xcbf43926);
    check("SHA512-crypt",crypt512("test-password","testsalt")=="$6$testsalt$qzmynZ3SU0S5D.QBAsFplf6HVa.jpeEdx88KlHvhGfddFSPHoEWMArwiVQ1PLzZDrJJ9Vs/zKBgHPMSwmFddx.");
    Archive a;Entry e;e.name=".";e.type='5';e.mode=0755;a[e.name]=e;e.name="dir";e.uid=101;e.gid=102;a[e.name]=e;
    e.name="dir/test";e.type='0';e.mode=04755;e.data=Bytes("a\0b",3);e.mtime=1780000000;a[e.name]=e;
    e.name="dir/link";e.type='2';e.data.clear();e.link="/dir/test";a[e.name]=e;
    e.name="dir/device";e.type='3';e.link.clear();e.major=5;e.minor=1;a[e.name]=e;
    e.name="dir/"+std::string(90,'a')+"/"+std::string(50,'b');e.type='0';e.data="test";e.major=e.minor=0;a[e.name]=e;
    e.name="dir/"+std::string(160,'c');e.type='2';e.data.clear();e.link="/"+std::string(160,'d');a[e.name]=e;
    auto b=tar(a);check("USTAR metadata and GNU long paths/links",manifest(untar(b))==manifest(a));
    b[10]^=1;bool bad=false;try{untar(b);}catch(const Error&){bad=true;}check("USTAR corruption rejection",bad);
    Json p={{"lan_ip","192.168.6.1"},{"netmask","255.255.255.0"},{"pppoe_username","test-user"},{"pppoe_password","test-password"},{"wifi_ssid","Test"},{"wifi_password","password123"},{"admin_password","test-admin"},{"country","CN"},{"ipv6",true}};
    validateProfile(p);check("valid profile",true);for(auto v:{"192.168.6.0","192.168.6.255","999.1.1.1","127.0.0.1"}){auto q=p;q["lan_ip"]=v;bad=false;try{validateProfile(q);}catch(const Error&){bad=true;}check("invalid LAN rejection",bad);}
    p["wifi_password"]="abc ' $(touch /tmp/never) ; #";auto init=renderInit(p,"@@SETTINGS@@\n");check("shell literal quoting",init.find("'\"'\"'")!=init.npos&&init.find("@@SETTINGS@@")==init.npos);
    Fdt tree;tree.nodes.push_back({"/",{{"model",Bytes("Test\0",5)}}});tree.reservations=Bytes(16,0);tree.add("/test");tree.set("/test","reg",cells({1,2}));check("FDT roundtrip",Fdt::parse(tree.encode()).map()==tree.map());
    auto opkg=parseInstalledPackages("Package: luci-app-openclash\r\nVersion: 1.2-r3\r\nStatus: install ok installed\r\nDescription: Proxy\r\n continuation\r\n\r\nPackage: removed\r\nVersion: 1\r\nStatus: deinstall ok config-files\r\n\r\nPackage: sing-box\nVersion: 1.0\nStatus: hold ok installed", "opkg");
    check("opkg inventory preserves and labels package states",opkg.size()==3&&opkg[0]["display_name"]=="OpenClash"&&opkg[0]["description"]=="Proxy continuation"&&opkg[1]["installed"]==false&&opkg[1]["state"]=="config-files");
    check("plugin versus dependency classification",opkg[0]["is_plugin"]==true&&opkg[2]["is_plugin"]==false);
    auto apk=parseInstalledPackages("P:luci-app-passwall2\nV:26.1-r1\nT:Proxy service\nF:usr/bin\nR:one\nR:two\n\nP:libc\nV:1.2.5\nT:System library\n", "apk");
    check("apk installed database ignores file records",apk.size()==2&&apk[1]["display_name"]=="PassWall2"&&apk[1]["version"]=="26.1-r1");
    bad=false;try{parseInstalledPackages("Package: incomplete\nStatus: install ok installed\n", "opkg");}catch(const Error&){bad=true;}
    check("incomplete package database rejection",bad);
    std::atomic_bool stopped=true;bad=false;try{readFirmwarePackages({}, {}, {}, stopped);}catch(const Error& e){bad=std::string(e.what()).find("取消")!=std::string::npos;}
    check("package read cancellation before IO",bad);
    auto side=p;side["routing_mode"]="side";side["side_gateway"]="192.168.6.254";side["side_dns"]="1.1.1.1 8.8.8.8";side["side_dhcp"]=false;
    for(auto key:{"pppoe_username","pppoe_password","wifi_ssid","wifi_password","country"})side[key]="";
    side["hostname"]="HomeRouter";side["signature"]="Home \" $(`never`) & Router";side["remove_author_links"]=false;
    validateProfile(side);check("side router does not require PPPoE or wireless settings",true);
    auto rendered=renderInit(side,"@@SETTINGS@@\n");check("side routing options rendered",rendered.find("LOCAL_ROUTING_MODE='side'")!=rendered.npos&&rendered.find("PPPOE_PASS=''\n")!=rendered.npos);
    auto invalid=side;invalid["side_gateway"]="192.168.6.1";bad=false;try{validateProfile(invalid);}catch(const Error&){bad=true;}check("side gateway cannot equal management address",bad);
    invalid=side;invalid["hostname"]="bad$(command)";bad=false;try{validateProfile(invalid);}catch(const Error&){bad=true;}check("invalid hostname rejected",bad);
    Archive sample;for(auto name:{"etc","usr","usr/lib"}){Entry item;item.name=name;item.type='5';sample[name]=item;}
    auto sampleFile=[&](const char* name,const char* data){Entry item;item.name=name;item.data=data;sample[name]=item;};
    sampleFile("etc/banner","Kwrt by Kiddin'\n");sampleFile("usr/lib/os-release","NAME=\"Kwrt\"\nOPENWRT_DEVICE_MANUFACTURER=\"Kiddin'\"\nOPENWRT_RELEASE=\"Kwrt 25.12 by Kiddin'\"\n");
    auto changed=applyFirmwarePresentation(sample,side);check("signature changes only declared branding files",changed.size()==2&&sample["etc/banner"].data.find(side["signature"].get<std::string>())!=std::string::npos);
    check("signature escapes shell expansion in os-release",sample["usr/lib/os-release"].data.find("\\$\\(")==std::string::npos&&sample["usr/lib/os-release"].data.find("\\$(\\`never\\`)")!=std::string::npos&&sample["usr/lib/os-release"].data.find("NAME=\"Kwrt\"")!=std::string::npos);
    sampleFile("etc/uci-defaults/zz-asu-defaults","uci -q set wizard.default.siderouter='1'\nuci -q set wizard.default.lan_gateway='192.168.6.254'\nuci -q set wizard.default.dhcp='0'\n");
    auto detected=detectNetworkDefaults(sample);check("detect KWRT side-router defaults",detected["mode"]=="side"&&detected["gateway"]=="192.168.6.254"&&detected["dhcp"]==false);
    sampleFile("etc/uci-defaults/zz-asu-defaults","uci -q set wizard.default.siderouter=\"$MODE\"\n");check("dynamic network mode is not guessed",detectNetworkDefaults(sample)["mode"]=="unknown");
    sampleFile("etc/uci-defaults/zzzz-local-network","LOCAL_ROUTING_MODE='side'\n");check("detect this tool's recorded mode",detectNetworkDefaults(sample)["mode"]=="side");
    sampleFile("etc/uci-defaults/zzzz-local-network","LOCAL_ROUTING_MODE='side'\nSIDE_GATEWAY='192.168.6.254'\nSIDE_DNS='1.1.1.1 8.8.8.8'\nSIDE_DHCP='1'\n");
    auto recorded=detectNetworkDefaults(sample);check("reread side gateway DNS and DHCP",recorded["gateway"]=="192.168.6.254"&&recorded["dns"]=="1.1.1.1 8.8.8.8"&&recorded["dhcp"]==true);
    side["side_dns"]="   ";check("blank DNS falls back to gateway",renderInit(side,"@@SETTINGS@@").find("SIDE_DNS=''\n")!=std::string::npos);
    for(const auto& test:routerCheckTests())tests.push_back(test);
    return {{"passed",tests.size()},{"failed",0},{"tests",tests}};
}
}
