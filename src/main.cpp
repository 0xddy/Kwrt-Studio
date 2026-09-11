#include "adapter.hpp"
#include "package_view.hpp"
#include "ui.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <thread>
#include <memory>
#include <algorithm>
#include <chrono>
#include <cstdio>

#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"comdlg32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"uxtheme.lib")
#pragma comment(lib,"uuid.lib")

using namespace ax;
static HINSTANCE instance;
static fs::path appDir;
static constexpr UINT WM_PROGRESS=WM_APP+1,WM_FINISH=WM_APP+2;
enum Id {File=100,Output,BrowseFile,BrowseOutput,Account,PppPassword,Lan,Mask,Ssid,WifiPassword,AdminPassword,Country,Ipv6,ShowPasswords,Remember,Start,Cancel,OpenOutput,Save,Load,ProgressBar,Status,Detail,DeviceInfo,Detect,Devices,ViewPlugins,Hostname,Signature,RemoveAuthorLinks,RoutingMode,SideGateway,SideDns,SideDhcp,RightTitle,SideNote,CountryHint,ModeHint,PageNetwork,PageDevice,WelcomeNote};
struct Completion {bool success=false,listing=false,detecting=false;std::wstring text;fs::path output;Json inventory;};
static COLORREF ink=RGB(29,43,66),muted=RGB(105,119,139),blue=RGB(42,98,226),background=ui::canvas;
struct App {
    HWND window=nullptr;std::map<int,HWND> controls;std::vector<HWND> labels;std::map<int,HWND> fieldLabels;HFONT normal=nullptr,smallFont=nullptr,heading=nullptr,title=nullptr;
    HBRUSH backBrush=CreateSolidBrush(background),whiteBrush=CreateSolidBrush(RGB(255,255,255));
    double scale=1;bool running=false,closing=false,demo=false,deviceSupported=false,firmwareReadable=false,created=false;std::atomic_bool cancel{false};std::thread worker;fs::path lastOutput,cachedSource;Json cachedInventory;bool manualMode=false;int page=1,creatingPage=0,detectedMode=0,viewHeight=0;std::map<int,std::vector<HWND>> pageWidgets;std::map<HWND,RECT> editBoxes;
    ~App(){cancel=true;if(worker.joinable())worker.join();for(auto f:{normal,smallFont,heading,title})if(f)DeleteObject(f);DeleteObject(backBrush);DeleteObject(whiteBrush);}
    int s(int value)const{return int(value*scale+0.5);}
    std::wstring get(int id){int n=GetWindowTextLengthW(controls.at(id));std::wstring text(n+1,0);GetWindowTextW(controls.at(id),text.data(),n+1);text.resize(n);return text;}
    void set(int id,const std::wstring& text){SetWindowTextW(controls.at(id),text.c_str());}
    bool checked(int id){return SendMessageW(controls.at(id),BM_GETCHECK,0,0)==BST_CHECKED;}
    void check(int id,bool value){SendMessageW(controls.at(id),BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);}
    HWND control(const wchar_t* cls,const wchar_t* text,int id,int x,int y,int w,int h,DWORD style=0,DWORD ex=0){
        auto c=CreateWindowExW(ex,cls,text,WS_CHILD|WS_VISIBLE|style,s(x),s(y),s(w),s(h),window,(HMENU)(INT_PTR)id,instance,nullptr);need(c!=nullptr,"无法创建窗口控件。");SendMessageW(c,WM_SETFONT,(WPARAM)normal,TRUE);if(id)controls[id]=c;else labels.push_back(c);if(creatingPage)pageWidgets[creatingPage].push_back(c);return c;
    }
    void label(const wchar_t* text,int x,int y,int w,int h=24,HFONT font=nullptr){auto c=control(L"STATIC",text,0,x,y,w,h,SS_LEFT);if(font)SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);}
    void fieldLabel(int id,const wchar_t* text,int x,int y,int width){fieldLabels[id]=control(L"STATIC",text,0,x,y,width,24,SS_LEFT);}
    void edit(int id,int x,int y,int w,bool password=false,bool readonly=false){
        auto c=control(L"EDIT",L"",id,x+12,y+9,w-24,23,ES_AUTOHSCROLL|WS_TABSTOP|(password?ES_PASSWORD:0)|(readonly?ES_READONLY:0));
        editBoxes[c]={s(x),s(y),s(x+w),s(y+40)};SendMessageW(c,EM_SETLIMITTEXT,4096,0);
        SendMessageW(c,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);SetWindowSubclass(c,ui::inputProc,1,0);
    }
    void button(const wchar_t* text,int id,int x,int y,int w,int h=38){
        auto c=control(L"BUTTON",text,id,x,y,w,h,BS_OWNERDRAW|WS_TABSTOP);SetWindowSubclass(c,ui::buttonProc,1,0);
    }
    bool hasSource(){return controls.count(File)&&!get(File).empty();}
    void resizeView(bool source){
        int height=source?764:572;if(viewHeight==height)return;viewHeight=height;
        RECT frame{0,0,s(1040),s(height)},position,work;
        AdjustWindowRectEx(&frame,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,0);GetWindowRect(window,&position);
        auto monitor=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);MONITORINFO info{sizeof(info)};
        if(GetMonitorInfoW(monitor,&info))work=info.rcWork;else SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        int width=frame.right-frame.left,totalHeight=frame.bottom-frame.top;
        int y=std::max(work.top,std::min(position.top,work.bottom-totalHeight));
        SetWindowPos(window,nullptr,position.left,y,width,totalHeight,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    void showPage(int selected){
        page=selected;if(!created)return;bool source=hasSource();
        for(auto& [group,widgets]:pageWidgets)for(auto c:widgets)ShowWindow(c,(source?(group==3||group==page):group==-1)?SW_SHOW:SW_HIDE);
        MoveWindow(controls[BrowseFile],s(source?872:426),s(source?109:307),s(source?124:188),s(source?38:46),TRUE);
        ShowWindow(controls[BrowseFile],SW_SHOW);resizeView(source);
        updateMode();InvalidateRect(controls[PageNetwork],nullptr,TRUE);InvalidateRect(controls[PageDevice],nullptr,TRUE);InvalidateRect(window,nullptr,FALSE);
    }
    int effectiveMode(){int selected=int(SendMessageW(controls[RoutingMode],CB_GETCURSEL,0,0));return selected==0?detectedMode:selected;}
    void setAutomaticLabel(){
        int selected=int(SendMessageW(controls[RoutingMode],CB_GETCURSEL,0,0));
        auto label=detectedMode==1?L"自动识别 · 主路由":detectedMode==2?L"自动识别 · 旁路由":L"自动识别";
        SendMessageW(controls[RoutingMode],CB_DELETESTRING,0,0);SendMessageW(controls[RoutingMode],CB_INSERTSTRING,0,(LPARAM)label);
        SendMessageW(controls[RoutingMode],CB_SETCURSEL,selected<0?0:selected,0);
    }
    Json profile(){Json p;for(auto& [id,key]:std::initializer_list<std::pair<int,const char*>>{{Account,"pppoe_username"},{PppPassword,"pppoe_password"},{Lan,"lan_ip"},{Mask,"netmask"},{Ssid,"wifi_ssid"},{WifiPassword,"wifi_password"},{AdminPassword,"admin_password"},{Country,"country"}})p[key]=utf8(get(id));p["ipv6"]=checked(Ipv6);p["hostname"]=utf8(get(Hostname));p["signature"]=utf8(get(Signature));p["remove_author_links"]=checked(RemoveAuthorLinks);int mode=effectiveMode();p["routing_mode"]=mode==2?"side":mode==1?"router":"unknown";p["side_gateway"]=utf8(get(SideGateway));p["side_dns"]=utf8(get(SideDns));p["side_dhcp"]=checked(SideDhcp);return p;}
    void loadProfile(const Json& p){validateProfile(p);for(auto& [id,key]:std::initializer_list<std::pair<int,const char*>>{{Account,"pppoe_username"},{PppPassword,"pppoe_password"},{Lan,"lan_ip"},{Mask,"netmask"},{Ssid,"wifi_ssid"},{WifiPassword,"wifi_password"},{AdminPassword,"admin_password"},{Country,"country"}})set(id,wide(p[key]));check(Ipv6,p["ipv6"]);set(Hostname,wide(p.value("hostname",std::string())));set(Signature,wide(p.value("signature",std::string())));check(RemoveAuthorLinks,p.value("remove_author_links",false));SendMessageW(controls[RoutingMode],CB_SETCURSEL,p.value("routing_mode",std::string("router"))=="side"?2:1,0);set(SideGateway,wide(p.value("side_gateway",std::string())));set(SideDns,wide(p.value("side_dns",std::string())));check(SideDhcp,p.value("side_dhcp",false));updateMode();}
    void error(const std::wstring& msg){MessageBoxW(window,msg.c_str(),L"请检查设置",MB_OK|MB_ICONINFORMATION);}
    void create(){
        normal=CreateFontW(-s(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        smallFont=CreateFontW(-s(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        heading=CreateFontW(-s(17),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        title=CreateFontW(-s(28),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        label(L"固件转换",32,22,400,40,title);label(L"保留插件，提前设置拨号和 Wi-Fi。",34,67,680,22,smallFont);
        button(L"支持机型",Devices,902,23,114);
        creatingPage=3;
        label(L"原始固件",44,119,100);edit(File,160,108,578);button(L"重新识别",Detect,752,109,108);button(L"选择固件",BrowseFile,872,109,124);
        label(L"保存到",44,173,100);edit(Output,160,162,700);button(L"选择目录",BrowseOutput,872,163,124);
        auto device=control(L"STATIC",L"支持拖入文件，或点击“选择固件”。",DeviceInfo,160,216,663,22,SS_LEFT|SS_ENDELLIPSIS);SendMessageW(device,WM_SETFONT,(WPARAM)smallFont,TRUE);
        button(L"查看插件",ViewPlugins,842,210,154,30);EnableWindow(controls[ViewPlugins],FALSE);
        button(L"网络设置",PageNetwork,24,264,144);button(L"设备信息",PageDevice,180,264,144);

        creatingPage=1;
        label(L"网络模式",44,336,112,26,heading);
        auto modes=control(L"COMBOBOX",L"",RoutingMode,162,328,330,144,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
        for(auto option:{L"自动识别",L"主路由（拨号）",L"旁路由"})SendMessageW(modes,CB_ADDSTRING,0,(LPARAM)option);
        SendMessageW(modes,CB_SETCURSEL,0,0);SetWindowTheme(modes,L"Explorer",nullptr);
        fieldLabel(Account,L"宽带账号",44,388,108);edit(Account,162,377,330);
        fieldLabel(PppPassword,L"宽带密码",44,434,108);edit(PppPassword,162,423,330);
        label(L"后台地址",44,480,108);edit(Lan,162,469,330);
        label(L"子网掩码",44,526,108);edit(Mask,162,515,330);
        control(L"BUTTON",L"启用 IPv6",Ipv6,44,570,150,26,BS_AUTOCHECKBOX|WS_TABSTOP);
        label(L"WAN / LAN 网口自动配置",253,575,239,22,smallFont);

        auto rightTitle=control(L"STATIC",L"Wi-Fi 和管理后台",RightTitle,552,336,430,26,SS_LEFT);SendMessageW(rightTitle,WM_SETFONT,(WPARAM)heading,TRUE);
        fieldLabel(Ssid,L"Wi-Fi 名称",552,388,108);edit(Ssid,678,377,314);
        fieldLabel(WifiPassword,L"Wi-Fi 密码",552,434,108);edit(WifiPassword,678,423,314);
        label(L"后台密码",552,480,108);edit(AdminPassword,678,469,314);
        fieldLabel(Country,L"国家 / 地区",552,526,112);edit(Country,678,515,86);
        control(L"STATIC",L"CN · 中国",CountryHint,782,526,190,24,SS_LEFT);
        control(L"BUTTON",L"显示密码",ShowPasswords,552,570,130,26,BS_AUTOCHECKBOX|WS_TABSTOP);check(ShowPasswords,true);
        auto hint=control(L"STATIC",L"生成 2.4G 和 5G 双频 Wi-Fi",ModeHint,733,575,260,22,SS_LEFT);SendMessageW(hint,WM_SETFONT,(WPARAM)smallFont,TRUE);
        fieldLabel(SideGateway,L"主路由网关",44,388,108);edit(SideGateway,162,377,330);
        fieldLabel(SideDns,L"DNS",44,434,108);edit(SideDns,162,423,330);
        SendMessageW(controls[SideDns],EM_SETCUEBANNER,FALSE,(LPARAM)L"留空使用主路由网关");
        control(L"BUTTON",L"启用 DHCP 服务器",SideDhcp,552,520,420,28,BS_AUTOCHECKBOX|WS_TABSTOP);
        control(L"STATIC",L"",SideNote,552,389,420,70,SS_LEFT);

        creatingPage=2;
        label(L"让这份固件有自己的名字",48,338,870,30,heading);
        label(L"这些项目可以留空，保留固件原来的设置。",48,376,910,24,smallFont);
        label(L"主机名",48,432,110);edit(Hostname,170,421,420);
        label(L"在系统中显示的设备名称，例如 HomeRouter。",616,433,350,44,smallFont);
        label(L"自定义签名",48,494,112);edit(Signature,170,483,420);
        label(L"显示在支持的系统信息与终端欢迎页中。",616,495,350,44,smallFont);
        control(L"BUTTON",L"去除作者外链",RemoveAuthorLinks,48,559,190,30,BS_AUTOCHECKBOX|WS_TABSTOP);
        label(L"关闭 KWRT 状态页内置的作者链接",255,566,650,22,smallFont);
        SendMessageW(controls[Hostname],EM_SETCUEBANNER,FALSE,(LPARAM)L"留空保留原主机名");
        SendMessageW(controls[Signature],EM_SETCUEBANNER,FALSE,(LPARAM)L"留空保留原签名");

        creatingPage=3;
        auto st=control(L"STATIC",L"请选择固件",Status,44,637,250,28,SS_LEFT);SendMessageW(st,WM_SETFONT,(WPARAM)heading,TRUE);
        auto detail=control(L"STATIC",L"选择固件后会自动识别型号、网络模式和插件。",Detail,308,637,684,36,SS_LEFT);SendMessageW(detail,WM_SETFONT,(WPARAM)smallFont,TRUE);
        control(PROGRESS_CLASSW,L"",ProgressBar,44,677,948,5,PBS_SMOOTH);SendMessageW(controls[ProgressBar],PBM_SETRANGE,0,MAKELPARAM(0,100));SetWindowTheme(controls[ProgressBar],L"",L"");SendMessageW(controls[ProgressBar],PBM_SETBARCOLOR,0,ui::accent);SendMessageW(controls[ProgressBar],PBM_SETBKCOLOR,0,RGB(236,240,247));
        control(L"BUTTON",L"记住这些设置",Remember,24,714,157,28,BS_AUTOCHECKBOX|WS_TABSTOP);check(Remember,true);
        button(L"保存设置",Save,191,708,102);button(L"读取设置",Load,305,708,102);
        button(L"打开结果",OpenOutput,609,708,120);button(L"取消",Cancel,741,708,90);button(L"开始转换",Start,845,706,171,44);
        EnableWindow(controls[Cancel],FALSE);EnableWindow(controls[OpenOutput],FALSE);EnableWindow(controls[Start],FALSE);
        creatingPage=-1;
        auto welcome=control(L"STATIC",L"选择要转换的固件",0,64,215,912,40,SS_CENTER);SendMessageW(welcome,WM_SETFONT,(WPARAM)title,TRUE);
        control(L"STATIC",L"将 KWRT 固件拖到这里，或点击下方按钮",0,64,263,912,28,SS_CENTER);
        auto note=control(L"STATIC",L"支持 sysupgrade.bin 固件",WelcomeNote,64,373,912,24,SS_CENTER);SendMessageW(note,WM_SETFONT,(WPARAM)smallFont,TRUE);
        for(int step=0;step<3;step++){
            const wchar_t* names[]={L"选择固件",L"配置网络",L"生成固件"};
            const wchar_t* descriptions[]={L"自动识别机型，查看插件",L"预设拨号、Wi-Fi 和后台",L"保留原有插件，另存新固件"};
            int x=88+step*324;label(names[step],x,466,240,26,heading);label(descriptions[step],x,497,246,24,smallFont);
        }
        creatingPage=0;
        Json p={{"lan_ip","192.168.6.1"},{"netmask","255.255.255.0"},{"pppoe_username","demo-user"},{"pppoe_password","demo-password"},{"wifi_ssid","MyHome"},{"wifi_password","demo-wifi-key"},{"admin_password","demo-admin"},{"country","CN"},{"ipv6",true}};
        if(demo){loadProfile(p);}
        else if(fs::exists(appDir/L"profile.json")){try{loadProfile(readJson(appDir/L"profile.json"));set(WelcomeNote,L"已载入上次设置，选择固件后可继续编辑");}catch(const Error& e){set(Detail,wide(e.what()));}}
        else{set(Lan,L"192.168.6.1");set(Mask,L"255.255.255.0");set(Country,L"CN");check(Ipv6,true);}
        set(Output,(appDir/L"生成固件").wstring());
        if(!demo&&fs::exists(appDir/L"ui.json")){try{auto state=readJson(appDir/L"ui.json");auto source=wide(state.value("input",std::string()));if(fs::is_regular_file(source))set(File,source);}catch(...) {}}
        created=true;showPage(1);DragAcceptFiles(window,TRUE);if(hasSource())SetTimer(window,1,500,nullptr);
    }
    void paint(){
        PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);RECT whole;GetClientRect(window,&whole);FillRect(dc,&whole,backBrush);
        auto card=[&](RECT r){r={s(r.left),s(r.top),s(r.right),s(r.bottom)};ui::rounded(dc,r,RGB(255,255,255),ui::line,s(16));};
        if(!hasSource()){
            card({24,112,1016,428});
            auto brush=CreateSolidBrush(ui::tint);auto pen=CreatePen(PS_SOLID,s(2),ui::accent);
            auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
            Ellipse(dc,s(484),s(134),s(556),s(206));
            SelectObject(dc,GetStockObject(NULL_BRUSH));SelectObject(dc,pen);
            POINT file[]={{s(507),s(150)},{s(524),s(150)},{s(534),s(160)},{s(534),s(189)},{s(507),s(189)},{s(507),s(150)}};
            Polyline(dc,file,6);MoveToEx(dc,s(524),s(150),nullptr);LineTo(dc,s(524),s(160));LineTo(dc,s(534),s(160));
            MoveToEx(dc,s(514),s(170),nullptr);LineTo(dc,s(527),s(170));MoveToEx(dc,s(514),s(179),nullptr);LineTo(dc,s(527),s(179));
            SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(pen);
            auto font=SelectObject(dc,heading);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,ui::accent);
            for(int step=0;step<3;step++){
                RECT badge{s(36+step*324),s(469),s(70+step*324),s(503)};ui::rounded(dc,badge,ui::tint,ui::tint,s(12));
                auto number=std::to_wstring(step+1);DrawTextW(dc,number.c_str(),-1,&badge,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
            SelectObject(dc,font);
        }else{
            card({24,96,1016,250});card({24,622,1016,695});
            if(page==1){card({24,316,516,610});card({532,316,1016,610});}else card({24,316,1016,610});
        }
        for(auto& [field,box]:editBoxes)if(IsWindowVisible(field)){
            bool focused=GetFocus()==field;ui::rounded(dc,box,RGB(255,255,255),focused?ui::accent:ui::line,s(10),focused?2:1);
        }
        EndPaint(window,&ps);
    }
    void drawButton(DRAWITEMSTRUCT* d){
        ui::button(d,normal,scale,d->CtlID==Start||(d->CtlID==BrowseFile&&!hasSource()),(d->CtlID==PageNetwork&&page==1)||(d->CtlID==PageDevice&&page==2));
    }
    void updateMode(){if(!controls.count(RoutingMode)||!controls.count(SideNote))return;
        int mode=effectiveMode();bool side=mode==2,router=mode==1,visible=hasSource()&&page==1;
        for(int id:{Account,PppPassword,Ssid,WifiPassword,Country}){ShowWindow(controls[id],visible&&router?SW_SHOW:SW_HIDE);ShowWindow(fieldLabels[id],visible&&router?SW_SHOW:SW_HIDE);}
        for(int id:{SideGateway,SideDns}){ShowWindow(controls[id],visible&&side?SW_SHOW:SW_HIDE);ShowWindow(fieldLabels[id],visible&&side?SW_SHOW:SW_HIDE);}
        ShowWindow(controls[SideDhcp],visible&&side?SW_SHOW:SW_HIDE);ShowWindow(controls[SideNote],visible&&!router?SW_SHOW:SW_HIDE);
        ShowWindow(controls[CountryHint],visible&&router?SW_SHOW:SW_HIDE);ShowWindow(controls[Ipv6],visible&&router?SW_SHOW:SW_HIDE);
        set(RightTitle,side?L"旁路由和管理后台":router?L"Wi-Fi 和管理后台":L"管理后台");
        set(SideNote,side?L"通过 LAN 口连接主路由。\n保留固件原有的 Wi-Fi，无需填写无线设置。":L"选择固件，等待网络模式识别。\n也可以在左侧手动选择主路由或旁路由。");
        set(ModeHint,side?L"开启前请关闭主路由 DHCP":router?L"生成 2.4G 和 5G 双频 Wi-Fi":L"");
        EnableWindow(controls[Start],!running&&deviceSupported&&mode>0);InvalidateRect(window,nullptr,FALSE);
    }
    void sourceChanged(){
        if(!created)return;KillTimer(window,1);deviceSupported=false;firmwareReadable=false;manualMode=false;detectedMode=0;
        cachedInventory=nullptr;cachedSource.clear();setAutomaticLabel();set(ViewPlugins,L"查看插件");
        SendMessageW(controls[RoutingMode],CB_SETCURSEL,0,0);SendMessageW(controls[ProgressBar],PBM_SETPOS,0,0);
        set(Status,hasSource()?L"等待识别固件":L"请选择固件");set(DeviceInfo,L"选择固件后会显示机型、版本和插件。");
        set(Detail,L"选择固件后会自动识别型号、网络模式和插件。");showPage(1);
        EnableWindow(controls[Start],FALSE);EnableWindow(controls[ViewPlugins],FALSE);
        if(hasSource())SetTimer(window,1,500,nullptr);else SetFocus(controls[BrowseFile]);
    }
    void applyDetectedMode(const Json& inventory){auto defaults=inventory["network_defaults"];auto mode=defaults["mode"].get<std::string>();
        detectedMode=mode=="side"?2:mode=="router"?1:0;setAutomaticLabel();
        if(!manualMode){SendMessageW(controls[RoutingMode],CB_SETCURSEL,0,0);
            if(mode=="side"){auto gateway=defaults["gateway"].get<std::string>(),dns=defaults["dns"].get<std::string>();
                if(!gateway.empty())set(SideGateway,wide(gateway));if(!dns.empty())set(SideDns,wide(dns));check(SideDhcp,defaults["dhcp"]);}}
        updateMode();set(Detail,wide(defaults["reason"]));
    }
    void scanNetwork(){if(running)return;fs::path source=get(File);if(worker.joinable())worker.join();cancel=false;setRunning(true);
        set(Status,L"正在识别网络模式");set(Detail,L"正在检查固件是否启用了旁路由。");
        worker=std::thread([this,source](){auto done=new Completion;done->detecting=true;done->output=source;
            try{done->inventory=readFirmwarePackages(source,appDir,[this](const Progress& p){PostMessageW(window,WM_PROGRESS,0,(LPARAM)new Progress(p));},cancel);done->success=true;}
            catch(const Error& e){done->text=cancel?L"已取消识别，请手动选择网络模式。":wide(e.what());}
            catch(...){done->text=L"无法确定网络模式，请手动选择主路由或旁路由。";}
            PostMessageW(window,WM_FINISH,0,(LPARAM)done);
        });
    }
    void detect(){if(running)return;KillTimer(window,1);deviceSupported=false;firmwareReadable=false;EnableWindow(controls[Start],FALSE);EnableWindow(controls[ViewPlugins],FALSE);try{fs::path path=get(File);if(!fs::is_regular_file(path)){set(Status,hasSource()?L"找不到固件文件":L"请选择固件");set(DeviceInfo,L"请选择一个本地原始固件。");set(Detail,L"点击“选择固件”，或将固件文件拖入窗口。");return;}auto info=inspectFirmware(path);deviceSupported=info["conversion_supported"];firmwareReadable=true;EnableWindow(controls[ViewPlugins],TRUE);
        set(DeviceInfo,wide(info["distribution"].get<std::string>()+" / "+info["version"].get<std::string>()+" / "+info["device"].get<std::string>()));
        if(deviceSupported){set(Status,L"固件可以转换");set(Detail,L"将转换为原厂分区（stock）版本，原有插件全部保留。");}
        else{set(Status,L"暂不支持这款固件");set(Detail,L"这款固件目前无法转换。点击“支持机型”查看可用的型号和版本。");}
        SendMessageW(controls[ProgressBar],PBM_SETPOS,0,0);EnableWindow(controls[Start],FALSE);scanNetwork();
    }catch(const Error& e){set(Status,L"无法读取这份固件");set(DeviceInfo,wide(e.what()));set(Detail,L"请确认选择的是完整下载的原始 KWRT 固件。");}catch(...){set(Status,L"无法识别这份固件");set(DeviceInfo,L"文件可能不完整，或使用了暂不支持的格式。");}}
    void catalog(){std::wstring text=L"目前支持以下固件：\n\n";for(auto& row:adapterCatalog()){
        text+=wide(row["device"].get<std::string>())+L"\n";
        std::string distributions;for(auto& d:row["distributions"]){if(!distributions.empty())distributions+=" / ";distributions+=d.get<std::string>();}
        text+=wide(distributions)+L" → 原厂分区（stock）版本\n\n";
    }text+=L"选择固件后，软件会自动检查是否兼容。其他型号暂时不能转换。\n\n转换后的固件用于已采用对应原厂分区的路由器。";MessageBoxW(window,text.c_str(),L"支持机型",MB_OK|MB_ICONINFORMATION);}
    void browse(bool folder){IFileDialog* dialog=nullptr;HRESULT h=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));if(FAILED(h)){error(L"无法打开文件选择窗口。");return;}
        DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|(folder?FOS_PICKFOLDERS:FOS_FILEMUSTEXIST));dialog->SetTitle(folder?L"选择保存结果的目录":L"选择 KWRT 固件");
        if(!folder){COMDLG_FILTERSPEC filter[]={ {L"原始固件 (*.bin)",L"*.bin"} };dialog->SetFileTypes(1,filter);}
        try{fs::path initial=folder?fs::path(get(Output)):fs::path(get(File)).parent_path();if(!fs::is_directory(initial))initial=appDir;IShellItem* location=nullptr;if(SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(),nullptr,IID_PPV_ARGS(&location)))){dialog->SetFolder(location);location->Release();}}catch(...){}
        if(SUCCEEDED(dialog->Show(window))){IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){set(folder?Output:File,path);CoTaskMemFree(path);}item->Release();}}
        dialog->Release();
    }
    void setRunning(bool active){running=active;for(auto [id,c]:controls)if(id!=Status&&id!=Detail&&id!=ProgressBar&&id!=DeviceInfo)EnableWindow(c,!active);EnableWindow(controls[Start],!active&&deviceSupported&&effectiveMode()>0);EnableWindow(controls[ViewPlugins],!active&&firmwareReadable);EnableWindow(controls[Cancel],active);EnableWindow(controls[OpenOutput],!active&&!lastOutput.empty());InvalidateRect(window,nullptr,FALSE);}
    void viewPlugins(){if(running)return;try{fs::path source=get(File);need(fs::is_regular_file(source),"请先选择固件。");
        if(!cachedInventory.is_null()&&source==cachedSource&&shaFile(source)==cachedInventory["source_sha256"].get<std::string>()){showPackageViewer(window,cachedInventory,normal,scale);return;}
        if(worker.joinable())worker.join();cancel=false;setRunning(true);set(Status,L"正在读取插件列表");
        set(Detail,L"读取完成后会打开列表，可搜索插件名称和版本。");SendMessageW(controls[ProgressBar],PBM_SETPOS,0,0);
        worker=std::thread([this,source](){auto done=new Completion;done->listing=true;
            try{done->inventory=readFirmwarePackages(source,appDir,[this](const Progress& p){PostMessageW(window,WM_PROGRESS,0,(LPARAM)new Progress(p));},cancel);
                done->success=true;done->text=L"已读取 "+std::to_wstring(done->inventory["plugin_count"].get<size_t>())+L" 个插件。";
            }catch(const Error& e){done->text=cancel?L"已取消读取插件列表。":wide(e.what());}
            catch(...){done->text=L"插件列表未能读取，请检查固件是否完整。";}
            PostMessageW(window,WM_FINISH,0,(LPARAM)done);
        });
    }catch(const Error& e){error(wide(e.what()));}catch(...){error(L"无法读取所选固件。");}}
    void start(){if(running)return;try{auto p=profile();validateProfile(p);fs::path source=get(File);need(fs::is_regular_file(source),"请先选择有效的原始固件。");fs::path base=get(Output);need(!base.empty(),"请选择输出文件夹。");fs::create_directories(base);
        SYSTEMTIME t;GetLocalTime(&t);wchar_t stamp[64];swprintf_s(stamp,L"%04u%02u%02u_%02u%02u%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);auto dest=base/(std::wstring(stamp)+L"_"+wide(randomToken().substr(0,6)));fs::create_directory(dest);
        if(checked(Remember)){write(appDir/L"profile.json",p.dump(2)+'\n');write(appDir/L"ui.json",Json({{"input",utf8(source.wstring())}}).dump(2));}
        Request r{source,dest,appDir,p,16};if(worker.joinable())worker.join();cancel=false;setRunning(true);set(Status,L"正在转换");set(Detail,L"正在电脑上处理固件，完成后可打开结果文件夹。");SendMessageW(controls[ProgressBar],PBM_SETPOS,0,0);
        worker=std::thread([this,r](){auto done=new Completion;try{auto result=convert(r,[this](const Progress& p){PostMessageW(window,WM_PROGRESS,0,(LPARAM)new Progress(p));},cancel);done->success=true;done->output=result.firmware.parent_path();done->text=L"固件已生成，原有插件全部保留。刷入时请取消“保留配置”。";}catch(const Error& e){done->text=wide(e.what());}catch(...){done->text=L"转换未完成。请检查固件是否完整，以及输出文件夹是否可以写入。";}PostMessageW(window,WM_FINISH,0,(LPARAM)done);});
    }catch(const Error& e){error(wide(e.what()));}catch(...){error(L"无法读取或保存文件，请检查文件位置和剩余空间。");}}
    void command(int id){try{switch(id){case PageNetwork:showPage(1);break;case PageDevice:showPage(2);break;case BrowseFile:browse(false);break;case BrowseOutput:browse(true);break;case Detect:detect();break;case Devices:catalog();break;case ViewPlugins:viewPlugins();break;case Start:start();break;case Cancel:cancel=true;set(Status,L"正在取消…");EnableWindow(controls[Cancel],FALSE);break;
        case ShowPasswords:for(int field:{PppPassword,WifiPassword,AdminPassword}){SendMessageW(controls[field],EM_SETPASSWORDCHAR,checked(ShowPasswords)?0:L'●',0);InvalidateRect(controls[field],nullptr,TRUE);}break;
        case Save:{auto p=profile();validateProfile(p);write(appDir/L"profile.json",p.dump(2)+'\n');set(Detail,L"设置已保存，下次打开软件会自动填好。");break;}
        case Load:loadProfile(readJson(appDir/L"profile.json"));set(Detail,L"已恢复上次保存的设置。");break;
        case OpenOutput:if(!lastOutput.empty())ShellExecuteW(window,L"open",lastOutput.c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;}
    }catch(const Error& e){error(wide(e.what()));}catch(...){error(L"无法保存或读取设置，请将软件解压到可写入的文件夹。");}}
};
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM wp,LPARAM lp){auto app=(App*)GetWindowLongPtrW(w,GWLP_USERDATA);if(m==WM_NCCREATE){app=(App*)((CREATESTRUCTW*)lp)->lpCreateParams;app->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)app);}if(!app)return DefWindowProcW(w,m,wp,lp);
    switch(m){case WM_CREATE:try{app->create();}catch(...){return -1;}return 0;case WM_PAINT:app->paint();return 0;case WM_ERASEBKGND:return 1;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:{auto dc=(HDC)wp;SetBkMode(dc,TRANSPARENT);SetTextColor(dc,GetCurrentObject(dc,OBJ_FONT)==app->smallFont?muted:ink);RECT r;GetWindowRect((HWND)lp,&r);MapWindowPoints(nullptr,w,(POINT*)&r,2);bool card=app->hasSource()?r.top>=app->s(96)&&((r.top<app->s(250))||(r.top>=app->s(316)&&r.top<app->s(695))):r.top>=app->s(112)&&r.top<app->s(428);return (LRESULT)(card?app->whiteBrush:app->backBrush);}
    case WM_CTLCOLOREDIT:SetTextColor((HDC)wp,ink);SetBkColor((HDC)wp,RGB(255,255,255));return (LRESULT)app->whiteBrush;
    case WM_DRAWITEM:app->drawButton((DRAWITEMSTRUCT*)lp);return TRUE;
    case WM_COMMAND:if(LOWORD(wp)==RoutingMode&&HIWORD(wp)==CBN_SELCHANGE){app->manualMode=SendMessageW(app->controls[RoutingMode],CB_GETCURSEL,0,0)!=0;if(!app->manualMode&&!app->cachedInventory.is_null())app->applyDetectedMode(app->cachedInventory);else {app->updateMode();app->set(Detail,app->manualMode?L"已使用手动选择的网络模式。":L"选择固件后会自动识别型号、网络模式和插件。");}
        if(app->deviceSupported)app->set(Status,app->effectiveMode()==2?L"旁路由 · 已就绪":app->effectiveMode()==1?L"主路由 · 已就绪":L"请选择网络模式");return 0;}if(LOWORD(wp)==File&&HIWORD(wp)==EN_CHANGE){app->sourceChanged();return 0;}if(HIWORD(wp)==BN_CLICKED)app->command(LOWORD(wp));return 0;
    case WM_TIMER:if(wp==1){KillTimer(w,1);app->detect();}return 0;
    case WM_DROPFILES:{auto drop=(HDROP)wp;if(!app->running&&DragQueryFileW(drop,0xffffffff,nullptr,0)==1){wchar_t path[32768];DragQueryFileW(drop,0,path,32768);app->set(File,path);}DragFinish(drop);return 0;}
    case WM_PROGRESS:{std::unique_ptr<Progress> p((Progress*)lp);SendMessageW(app->controls[ProgressBar],PBM_SETPOS,p->percent,0);app->set(Status,wide(p->text));return 0;}
    case WM_FINISH:{std::unique_ptr<Completion> p((Completion*)lp);if(app->worker.joinable())app->worker.join();
        if(p->detecting){app->setRunning(false);SendMessageW(app->controls[ProgressBar],PBM_SETPOS,0,0);if(app->closing){DestroyWindow(w);return 0;}
            if(p->success){app->cachedSource=p->output;app->cachedInventory=p->inventory;app->applyDetectedMode(p->inventory);
                auto mode=p->inventory["network_defaults"]["mode"].get<std::string>();
                app->set(ViewPlugins,L"查看插件 · "+std::to_wstring(p->inventory["plugin_count"].get<size_t>()));
                app->set(Status,!app->deviceSupported?L"此型号暂不能转换":mode=="side"?L"旁路由 · 已就绪":mode=="router"?L"主路由 · 已就绪":L"请选择网络模式");
                if(!app->deviceSupported)app->set(Detail,L"可以查看插件列表；转换范围请查看“支持机型”。");}
            else{app->set(Status,L"请手动选择网络模式");app->set(Detail,p->text);app->updateMode();}return 0;}
        if(p->success&&!p->listing)app->lastOutput=p->output;app->setRunning(false);
        app->set(Status,p->success?(p->listing?L"插件列表已读取":L"转换完成"):(app->cancel?L"已取消":(p->listing?L"读取未完成":L"转换已停止")));
        app->set(Detail,p->text);if(app->closing){DestroyWindow(w);return 0;}
        if(p->listing&&p->success)showPackageViewer(w,p->inventory,app->normal,app->scale);return 0;}
    case WM_CLOSE:if(app->running){app->closing=true;app->cancel=true;app->set(Status,L"正在取消，请稍候…");}else DestroyWindow(w);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,m,wp,lp);
}
static void print(const std::string& text){auto h=GetStdHandle(STD_OUTPUT_HANDLE);if(h&&h!=INVALID_HANDLE_VALUE){DWORD n;auto line=text+'\n';WriteFile(h,line.data(),DWORD(line.size()),&n,nullptr);}}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,LPWSTR,int show){instance=h;wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);appDir=fs::path(exe).parent_path();int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);std::vector<std::wstring> args;for(int i=1;i<argc;i++)args.emplace_back(argv[i]);LocalFree(argv);
    if(!args.empty()&&args[0]==L"--self-test"){try{print(selfTest().dump(2));return 0;}catch(const Error& e){print(e.what());return 2;}catch(...){print("Self-test failed unexpectedly.");return 3;}}
    if(!args.empty()&&args[0]==L"--catalog"){print(adapterCatalog().dump(2));return 0;}
    if(!args.empty()&&args[0]==L"--inspect"){try{need(args.size()==2,"请选择一个原始固件。");print(inspectFirmware(args[1]).dump(2));return 0;}catch(const Error& e){print(e.what());return 2;}catch(...){print("固件识别失败。");return 3;}}
    if(!args.empty()&&args[0]==L"--packages"){try{need(args.size()==2,"请选择一个固件文件。");std::atomic_bool cancelled=false;
        print(readFirmwarePackages(args[1],appDir,{},cancelled).dump(2));return 0;
    }catch(const Error& e){print(e.what());return 2;}catch(...){print("无法读取固件里的软件包列表。");return 3;}}
    if(!args.empty()&&args[0]==L"--convert"){try{need(args.size()>=2,"缺少输入文件。");Request r;r.input=args[1];r.appDirectory=appDir;fs::path config=appDir/L"profile.json";unsigned cancelMs=0;
        for(size_t i=2;i<args.size();i++){need(i+1<args.size(),"命令参数缺失。");auto v=args[++i];if(args[i-1]==L"--profile")config=v;else if(args[i-1]==L"--output")r.outputDirectory=v;else if(args[i-1]==L"--minimum-data-mib")r.minimumDataMiB=std::stoul(v);else if(args[i-1]==L"--cancel-after-ms")cancelMs=std::stoul(v);else throw Error("未知参数。");}
        need(!r.outputDirectory.empty(),"缺少输出目录。");fs::create_directories(r.outputDirectory);r.profile=readJson(config);std::atomic_bool cancelled=false,finished=false;
        std::thread timer;if(cancelMs)timer=std::thread([&]{auto start=std::chrono::steady_clock::now();while(!finished){if(std::chrono::steady_clock::now()-start>=std::chrono::milliseconds(cancelMs)){cancelled=true;break;}Sleep(10);}});
        try{auto result=convert(r,[](auto& p){print(std::to_string(p.percent)+"% "+p.text);},cancelled);finished=true;if(timer.joinable())timer.join();print("OUTPUT "+utf8(result.firmware.wstring()));return 0;}catch(...){finished=true;if(timer.joinable())timer.join();throw;}
    }catch(const Error& e){print(std::string("停止：")+e.what());return 2;}catch(...){print("停止：格式或运行错误，没有跳过检查。");return 3;}}
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES|ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&ic);App app;app.demo=!args.empty()&&args[0]==L"--demo";
    RECT work;SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);app.scale=std::min(double(GetDpiForSystem())/96.0,std::min(double(work.right-work.left-32)/1056,double(work.bottom-work.top-48)/804));
    WNDCLASSEXW c{sizeof(c)};c.lpfnWndProc=proc;c.hInstance=h;c.lpszClassName=L"KwrtStudio";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hIcon=LoadIconW(h,MAKEINTRESOURCEW(1));if(!c.hIcon)c.hIcon=LoadIconW(nullptr,IDI_APPLICATION);c.hIconSm=c.hIcon;RegisterClassExW(&c);
    RECT r{0,0,app.s(1040),app.s(764)};AdjustWindowRectEx(&r,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,FALSE,0);
    auto window=CreateWindowExW(0,c.lpszClassName,L"Kwrt Studio · KWRT 固件转换",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,(work.right-r.right+r.left)/2,(work.bottom-r.bottom+r.top)/2,r.right-r.left,r.bottom-r.top,nullptr,nullptr,h,&app);
    if(!window){MessageBoxW(nullptr,L"软件无法启动，请重新完整解压后再试。",L"Kwrt Studio",MB_OK);return 1;}
    GetWindowRect(window,&r);SetWindowPos(window,nullptr,work.left+(work.right-work.left-(r.right-r.left))/2,work.top+(work.bottom-work.top-(r.bottom-r.top))/2,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(window,show);UpdateWindow(window);if(!app.hasSource())SetFocus(app.controls[BrowseFile]);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}CoUninitialize();return int(msg.wParam);
}
