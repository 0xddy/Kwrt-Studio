#include "router_view.hpp"
#include "router_check.hpp"
#include "ssh_client.hpp"
#include "ui.hpp"
#include <thread>
#include <memory>
#include <sstream>
#include <iomanip>

using namespace ax;
namespace {
enum {Address=3001,Port,Username,Password,ShowPassword,RefreshGateway,Run,Close};
constexpr UINT Finished=WM_APP+61;
struct Completion {Json result;std::string error,fingerprint;bool changed=false;};
// Session-only trust cache. No credentials or router information are written to disk.
std::map<std::string,std::string> trustedHosts;
struct Viewer {
    HWND window=nullptr,owner=nullptr,list=nullptr,advice=nullptr,status=nullptr,device=nullptr,gatewayHint=nullptr;
    HFONT font=nullptr,heading=nullptr;double scale=1;fs::path firmware;std::map<int,HWND> fields;
    std::thread worker;std::atomic_bool cancel{false};bool running=false,closing=false;
    std::string endpoint;
    Viewer(HWND parent,fs::path input,HFONT f,double dpi):owner(parent),font(f),scale(dpi),firmware(std::move(input)){}
    ~Viewer(){cancel=true;if(worker.joinable())worker.join();if(heading)DeleteObject(heading);}
    int s(int v)const{return int(v*scale+0.5);}
    HWND control(const wchar_t* cls,const wchar_t* text,int id,int x,int y,int width,int height,DWORD style=0,DWORD ex=0){
        auto w=CreateWindowExW(ex,cls,text,WS_CHILD|WS_VISIBLE|style,s(x),s(y),s(width),s(height),window,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
        need(w!=nullptr,"无法打开路由器检测窗口。");SendMessageW(w,WM_SETFONT,(WPARAM)font,TRUE);if(id)fields[id]=w;return w;
    }
    void button(const wchar_t* text,int id,int x,int y,int width){auto w=control(L"BUTTON",text,id,x,y,width,36,BS_OWNERDRAW|WS_TABSTOP);SetWindowSubclass(w,ui::buttonProc,1,0);}
    void edit(int id,const wchar_t* text,int x,int y,int width){auto w=control(L"EDIT",text,id,x,y,width,32,ES_AUTOHSCROLL|WS_TABSTOP,WS_EX_CLIENTEDGE);SendMessageW(w,EM_SETLIMITTEXT,id==Password?4096:128,0);}
    std::string get(int id){auto w=fields.at(id);int n=GetWindowTextLengthW(w);std::wstring value(n+1,0);GetWindowTextW(w,value.data(),n+1);value.resize(n);auto result=utf8(value);if(id==Password&&!value.empty())SecureZeroMemory(value.data(),value.size()*sizeof(wchar_t));return result;}
    void gateways(){
        try{auto gateways=localGateways();SendMessageW(fields[Address],CB_RESETCONTENT,0,0);
            if(gateways.empty()){SetWindowTextW(gatewayHint,L"未发现以太网或 Wi-Fi 的默认网关，请填写路由器地址。");return;}
            for(auto& g:gateways){auto value=wide(g.address);SendMessageW(fields[Address],CB_ADDSTRING,0,(LPARAM)value.c_str());}
            SetWindowTextW(fields[Address],wide(gateways.front().address).c_str());
            auto note="已填入 "+gateways.front().adapter+" 的默认网关，可修改"+(gateways.size()>1?std::string("或从下拉列表选择。") : std::string("。"));
            SetWindowTextW(gatewayHint,wide(note).c_str());
        }catch(const std::exception& e){SetWindowTextW(gatewayHint,wide(e.what()).c_str());}
    }
    void create(){
        heading=CreateFontW(-s(22),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        auto title=control(L"STATIC",L"检测路由器",0,24,18,500,32);SendMessageW(title,WM_SETFONT,(WPARAM)heading,TRUE);
        control(L"STATIC",L"连接 SSH，查看分区与固件是否匹配。检测结果仅作建议。",0,24,59,750,25);
        control(L"STATIC",L"路由器地址",0,24,96,400,24);
        control(WC_COMBOBOXW,L"",Address,24,124,396,210,CBS_DROPDOWN|CBS_AUTOHSCROLL|WS_VSCROLL|WS_TABSTOP);SendMessageW(fields[Address],CB_LIMITTEXT,127,0);
        button(L"获取当前网关",RefreshGateway,434,122,148);
        control(L"STATIC",L"SSH 端口",0,606,96,170,24);edit(Port,L"22",606,124,170);
        gatewayHint=control(L"STATIC",L"",0,24,162,752,26,SS_ENDELLIPSIS);
        control(L"STATIC",L"用户名",0,24,200,210,24);edit(Username,L"root",24,228,210);
        control(L"STATIC",L"SSH 密码",0,260,200,300,24);edit(Password,L"",260,228,310);
        control(L"BUTTON",L"显示密码",ShowPassword,606,228,170,30,BS_AUTOCHECKBOX|WS_TABSTOP);SendMessageW(fields[ShowPassword],BM_SETCHECK,BST_CHECKED,0);
        button(L"开始检测",Run,24,278,140);status=control(L"STATIC",L"密码仅用于本次连接。",0,182,284,590,26,SS_ENDELLIPSIS);
        device=control(L"STATIC",L"连接后显示设备与分区信息",0,24,334,752,26,SS_ENDELLIPSIS);
        list=control(WC_LISTVIEWW,L"路由器分区",0,24,365,752,140,LVS_REPORT|LVS_SINGLESEL|WS_TABSTOP,WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);SetWindowTheme(list,L"Explorer",nullptr);
        const wchar_t* headers[]={L"分区",L"名称",L"容量",L"起始位置"};int widths[]={78,226,174,247};
        for(int i=0;i<4;i++){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.cx=s(widths[i]);c.pszText=const_cast<wchar_t*>(headers[i]);ListView_InsertColumn(list,i,&c);}
        advice=control(L"EDIT",L"选择固件后，检测时会一并比对转换目标。\r\n\r\n也可以先连接路由器，查看当前分区布局。",0,24,523,752,124,ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP);
        button(L"关闭",Close,656,663,120);gateways();
    }
    void busy(bool value){running=value;for(auto& [id,w]:fields)if(id!=Close&&id!=Run)EnableWindow(w,!value);
        SetWindowTextW(fields[Run],value?L"取消检测":L"开始检测");InvalidateRect(window,nullptr,FALSE);}
    void start(){
        if(running){cancel=true;EnableWindow(fields[Run],FALSE);SetWindowTextW(status,L"正在取消…");return;}
        SshOptions options;options.address=get(Address);options.username=get(Username);auto port=get(Port);
        need(!port.empty()&&port.size()<=5&&port.find_first_not_of("0123456789")==port.npos,"请填写 1–65535 之间的 SSH 端口。");
        auto number=std::stoul(port);need(number>0&&number<=65535,"请填写 1–65535 之间的 SSH 端口。");options.port=uint16_t(number);
        endpoint="["+options.address+"]:"+std::to_string(options.port);options.fingerprint=trustedHosts[endpoint];options.password=get(Password);
        if(worker.joinable())worker.join();cancel=false;busy(true);ListView_DeleteAllItems(list);SetWindowTextW(device,L"正在连接路由器…");SetWindowTextW(status,L"正在读取设备信息与分区…");SetWindowTextW(advice,L"");
        worker=std::thread([this,options=std::move(options)]()mutable{
            auto done=std::make_unique<Completion>();
            try{auto snapshot=parseRouterProbe(queryRouter(options,cancel));need(!cancel,"已取消检测。");done->result=adviseRouter(snapshot,firmware);need(!cancel,"已取消检测。");}
            catch(const HostKeyRequired& e){done->fingerprint=e.fingerprint;done->changed=e.changed;}
            catch(const std::exception& e){done->error=e.what();}
            if(!options.password.empty())SecureZeroMemory(options.password.data(),options.password.size());
            auto pointer=done.release();if(!PostMessageW(window,Finished,0,(LPARAM)pointer))delete pointer;
        });
    }
    void finish(Completion& c){
        if(worker.joinable())worker.join();busy(false);EnableWindow(fields[Run],TRUE);
        if(closing){DestroyWindow(window);return;}
        if(!c.fingerprint.empty()){
            if(cancel){SetWindowTextW(status,L"已取消检测。");return;}
            auto text=(c.changed?std::string("此地址的 SSH 身份已变化。请确认连接的是你的路由器。\n\n"):std::string("首次连接这台设备，请核对 SSH 指纹。\n\n"))+endpoint+"\n"+c.fingerprint+"\n\n是否信任并继续？";
            if(MessageBoxW(window,wide(text).c_str(),L"确认设备身份",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES){trustedHosts[endpoint]=c.fingerprint;start();}
            else{SetWindowTextW(status,L"未连接；尚未发送密码。");SetWindowTextW(device,L"等待检测");}return;
        }
        if(!c.error.empty()){SetWindowTextW(status,cancel?L"已取消检测":L"未完成检测");SetWindowTextW(device,L"未取得有效检测结果");SetWindowTextW(advice,wide(c.error).c_str());return;}
        auto& snapshot=c.result.at("snapshot");auto title=snapshot.value("model",snapshot.value("board",std::string("未知设备")))+"  ·  "+c.result.value("layout","");
        SetWindowTextW(device,wide(title).c_str());int row=0;
        for(auto& p:snapshot.at("partitions")){
            auto id=wide(p.at("id")),name=wide(p.at("name"));std::ostringstream size;size<<std::fixed<<std::setprecision(2)<<double(p.at("size").get<uint64_t>())/1048576<<" MiB";
            std::wstring offset=L"未取得";if(p.contains("offset")){std::ostringstream value;value<<"0x"<<std::hex<<p.at("offset").get<uint64_t>();offset=wide(value.str());}
            else if(snapshot.value("tree_valid",false)){size_t count=0;uint64_t start=0;for(auto& t:snapshot.at("tree_partitions"))if(t.at("name")==p.at("name")&&t.at("size")==p.at("size")){count++;start=t.at("offset");}
                if(count==1){std::ostringstream value;value<<"0x"<<std::hex<<start<<" (设备树)";offset=wide(value.str());}}
            auto capacity=wide(size.str());LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=row;item.pszText=id.data();ListView_InsertItem(list,&item);
            ListView_SetItemText(list,row,1,name.data());ListView_SetItemText(list,row,2,capacity.data());ListView_SetItemText(list,row,3,offset.data());row++;
        }
        SetWindowTextW(status,L"检测完成 · 仅供参考");SetWindowTextW(advice,wide(routerAdviceText(c.result)).c_str());
    }
    void close(){if(running){closing=true;cancel=true;EnableWindow(fields[Close],FALSE);EnableWindow(fields[Run],FALSE);SetWindowTextW(status,L"正在结束连接…");}else DestroyWindow(window);}
};
LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM wp,LPARAM lp){
    auto v=(Viewer*)GetWindowLongPtrW(w,GWLP_USERDATA);if(m==WM_NCCREATE){v=(Viewer*)((CREATESTRUCTW*)lp)->lpCreateParams;v->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)v);}
    if(!v)return DefWindowProcW(w,m,wp,lp);
    try{switch(m){
    case WM_CREATE:v->create();return 0;
    case WM_DRAWITEM:ui::button((DRAWITEMSTRUCT*)lp,v->font,v->scale,((DRAWITEMSTRUCT*)lp)->CtlID==Run);return TRUE;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:SetBkMode((HDC)wp,TRANSPARENT);SetTextColor((HDC)wp,ui::ink);return (LRESULT)GetStockObject(WHITE_BRUSH);
    case WM_COMMAND:
        if(LOWORD(wp)==RefreshGateway&&HIWORD(wp)==BN_CLICKED)v->gateways();
        else if(LOWORD(wp)==Run&&HIWORD(wp)==BN_CLICKED)v->start();
        else if(LOWORD(wp)==ShowPassword){SendMessageW(v->fields[Password],EM_SETPASSWORDCHAR,SendMessageW(v->fields[ShowPassword],BM_GETCHECK,0,0)==BST_CHECKED?0:L'●',0);InvalidateRect(v->fields[Password],nullptr,TRUE);}
        else if(LOWORD(wp)==Close||LOWORD(wp)==IDCANCEL)v->close();return 0;
    case Finished:{std::unique_ptr<Completion> done((Completion*)lp);v->finish(*done);return 0;}
    case WM_CLOSE:v->close();return 0;
    }}catch(const std::exception& e){if(m==WM_CREATE)return -1;MessageBoxW(w,wide(e.what()).c_str(),L"请检查连接设置",MB_OK|MB_ICONINFORMATION);}
    return DefWindowProcW(w,m,wp,lp);
}
}
void showRouterViewer(HWND owner,const fs::path& firmware,HFONT font,double scale){
    auto instance=GetModuleHandleW(nullptr);WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.lpszClassName=L"KwrtRouterViewer";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));cls.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);
    if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
    Viewer viewer(owner,firmware,font,scale);RECT parent;GetWindowRect(owner,&parent);RECT bounds{0,0,viewer.s(800),viewer.s(718)};
    DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN;AdjustWindowRectEx(&bounds,style,FALSE,WS_EX_DLGMODALFRAME);
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    int x=std::max(monitor.rcWork.left,parent.left+(parent.right-parent.left-width)/2),y=std::max(monitor.rcWork.top,std::min(parent.top,monitor.rcWork.bottom-height));
    auto window=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,L"Kwrt Studio · 路由器检测",style,x,y,width,height,owner,nullptr,instance,&viewer);
    need(window!=nullptr,"无法打开路由器检测窗口。");EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);SetFocus(window);
    MSG message{};int result=1;while(IsWindow(window)&&(result=GetMessageW(&message,nullptr,0,0))>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    if(IsWindow(window)){viewer.cancel=true;DestroyWindow(window);}if(IsWindow(owner)){EnableWindow(owner,TRUE);SetActiveWindow(owner);}if(result==0)PostQuitMessage(int(message.wParam));
}
