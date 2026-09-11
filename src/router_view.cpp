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
enum {Address=3001,Port,Username,Password,ShowPassword,Run,Close,GatewayChoices};
constexpr UINT Finished=WM_APP+61;
struct Completion {Json result;std::string error,fingerprint;bool changed=false;};
// Session-only trust cache. No credentials or router information are written to disk.
std::map<std::string,std::string> trustedHosts;
struct Viewer {
    HWND window=nullptr,owner=nullptr,list=nullptr,advice=nullptr,status=nullptr,device=nullptr,gatewayHint=nullptr;
    HWND resultTitle=nullptr,layoutNote=nullptr,comparisonNote=nullptr,emptyList=nullptr;
    HFONT font=nullptr,heading=nullptr,sectionFont=nullptr,smallFont=nullptr;double scale=1;fs::path firmware;std::map<int,HWND> fields;
    HBRUSH canvasBrush=CreateSolidBrush(ui::canvas),resultBrush=CreateSolidBrush(ui::tint);
    COLORREF resultFill=ui::tint,resultBorder=RGB(216,226,251),resultInk=ui::accent;
    HIMAGELIST rowHeight=nullptr;std::set<HWND> canvasControls,resultControls,mutedControls;
    std::map<HWND,RECT> editBoxes;
    std::vector<Gateway> availableGateways;
    std::thread worker;std::atomic_bool cancel{false};bool running=false,closing=false,hasResult=false;
    std::string endpoint;
    Viewer(HWND parent,fs::path input,HFONT f,double dpi):owner(parent),font(f),scale(dpi),firmware(std::move(input)){}
    ~Viewer(){cancel=true;if(worker.joinable())worker.join();for(auto f:{heading,sectionFont,smallFont})if(f)DeleteObject(f);DeleteObject(canvasBrush);DeleteObject(resultBrush);if(rowHeight)ImageList_Destroy(rowHeight);}
    int s(int v)const{return int(v*scale+0.5);}
    HWND control(const wchar_t* cls,const wchar_t* text,int id,int x,int y,int width,int height,DWORD style=0,DWORD ex=0){
        auto w=CreateWindowExW(ex,cls,text,WS_CHILD|WS_VISIBLE|style,s(x),s(y),s(width),s(height),window,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
        need(w!=nullptr,"无法打开路由器检测窗口。");SendMessageW(w,WM_SETFONT,(WPARAM)font,TRUE);if(id)fields[id]=w;if(y<100||y>=700)canvasControls.insert(w);return w;
    }
    void button(const wchar_t* text,int id,int x,int y,int width){auto w=control(L"BUTTON",text,id,x,y,width,36,BS_OWNERDRAW|WS_TABSTOP);SetWindowSubclass(w,ui::buttonProc,1,0);}
    void edit(int id,const wchar_t* text,int x,int y,int width){auto w=control(L"EDIT",text,id,x+12,y+9,width-24,23,ES_AUTOHSCROLL|WS_TABSTOP);
        editBoxes[w]={s(x),s(y),s(x+width),s(y+40)};SetWindowSubclass(w,ui::inputProc,1,0);SendMessageW(w,EM_SETLIMITTEXT,id==Password?4096:128,0);}
    std::string get(int id){auto w=fields.at(id);int n=GetWindowTextLengthW(w);std::wstring value(n+1,0);GetWindowTextW(w,value.data(),n+1);value.resize(n);auto result=utf8(value);if(id==Password&&!value.empty())SecureZeroMemory(value.data(),value.size()*sizeof(wchar_t));return result;}
    void gateways(){
        resetResult();
        try{availableGateways=localGateways();EnableWindow(fields[GatewayChoices],!availableGateways.empty());
            if(availableGateways.empty()){SetWindowTextW(gatewayHint,L"未发现网关，请手动填写");return;}
            SetWindowTextW(fields[Address],wide(availableGateways.front().address).c_str());
            auto note="来自 "+availableGateways.front().adapter;
            SetWindowTextW(gatewayHint,wide(note).c_str());
        }catch(const std::exception& e){availableGateways.clear();EnableWindow(fields[GatewayChoices],FALSE);SetWindowTextW(gatewayHint,wide(e.what()).c_str());}
    }
    void chooseGateway(){
        if(running||availableGateways.empty())return;
        auto menu=CreatePopupMenu();need(menu!=nullptr,"无法打开网关列表。");
        auto current=get(Address);
        for(size_t i=0;i<availableGateways.size();i++){
            auto& gateway=availableGateways[i];auto label=wide(gateway.address+"  ·  "+gateway.adapter);
            for(size_t p=0;(p=label.find(L'&',p))!=label.npos;p+=2)label.insert(p,1,L'&');
            AppendMenuW(menu,MF_STRING|(current==gateway.address?MF_CHECKED:0),UINT_PTR(i+1),label.c_str());
        }
        auto bounds=editBoxes.at(fields[Address]);MapWindowPoints(window,nullptr,(POINT*)&bounds,2);
        auto selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN|TPM_TOPALIGN,bounds.left,bounds.bottom,0,window,nullptr);DestroyMenu(menu);
        if(selected>0&&size_t(selected)<=availableGateways.size()){
            auto& gateway=availableGateways[size_t(selected)-1];SetWindowTextW(fields[Address],wide(gateway.address).c_str());
            SetWindowTextW(gatewayHint,wide("来自 "+gateway.adapter).c_str());SetFocus(fields[Address]);
        }
    }
    static LRESULT CALLBACK addressProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto v=(Viewer*)data;
        if((m==WM_KEYDOWN&&wp==VK_F4)||(m==WM_SYSKEYDOWN&&wp==VK_DOWN)){
            PostMessageW(v->window,WM_COMMAND,MAKEWPARAM(GatewayChoices,BN_CLICKED),0);return 0;
        }
        return DefSubclassProc(w,m,wp,lp);
    }
    void drawGatewayButton(DRAWITEMSTRUCT* d){
        const bool disabled=(d->itemState&ODS_DISABLED)!=0,hover=GetPropW(d->hwndItem,L"SLS.Hover")!=nullptr;
        auto fill=hover&&!disabled?ui::tint:RGB(255,255,255);auto r=d->rcItem;
        ui::rounded(d->hDC,r,fill,fill,s(8));
        auto pen=CreatePen(PS_SOLID,std::max(1,s(1)),disabled?RGB(177,186,198):ui::muted);auto old=SelectObject(d->hDC,pen);
        int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;MoveToEx(d->hDC,x-s(4),y-s(2),nullptr);LineTo(d->hDC,x,y+s(2));LineTo(d->hDC,x+s(4),y-s(2));
        SelectObject(d->hDC,old);DeleteObject(pen);
        // The surrounding input border already indicates keyboard focus.
    }
    void create(){
        heading=CreateFontW(-s(22),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        sectionFont=CreateFontW(-s(18),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        smallFont=CreateFontW(-s(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        auto title=control(L"STATIC",L"检测路由器",0,28,22,500,34);SendMessageW(title,WM_SETFONT,(WPARAM)heading,TRUE);
        auto subtitle=control(L"STATIC",L"查看当前分区，了解是否适合转换后的固件。",0,28,65,780,24);mutedControls.insert(subtitle);
        auto section=control(L"STATIC",L"SSH 连接",0,46,128,260,28);SendMessageW(section,WM_SETFONT,(WPARAM)sectionFont,TRUE);
        control(L"STATIC",L"路由器地址",0,46,176,262,24);
        edit(Address,L"",46,204,262);SendMessageW(fields[Address],EM_SETLIMITTEXT,127,0);
        MoveWindow(fields[Address],s(58),s(213),s(206),s(23),FALSE);
        SetWindowSubclass(fields[Address],addressProc,2,(DWORD_PTR)this);
        auto arrow=control(L"BUTTON",L"选择网关",GatewayChoices,270,210,32,28,BS_OWNERDRAW|WS_TABSTOP);SetWindowSubclass(arrow,ui::buttonProc,1,0);SetWindowSubclass(arrow,ui::inputProc,2,0);
        gatewayHint=control(L"STATIC",L"",0,46,252,262,24,SS_ENDELLIPSIS);SendMessageW(gatewayHint,WM_SETFONT,(WPARAM)smallFont,TRUE);mutedControls.insert(gatewayHint);
        control(L"STATIC",L"SSH 端口",0,46,302,90,24);edit(Port,L"22",46,330,90);
        control(L"STATIC",L"用户名",0,152,302,156,24);edit(Username,L"root",152,330,156);
        control(L"STATIC",L"SSH 密码",0,46,394,262,24);edit(Password,L"",46,422,262);
        control(L"BUTTON",L"显示密码",ShowPassword,46,482,240,26,BS_AUTOCHECKBOX|WS_TABSTOP);SendMessageW(fields[ShowPassword],BM_SETCHECK,BST_CHECKED,0);
        auto passwordNote=control(L"STATIC",L"密码仅用于连接，不会保存。",0,46,523,262,24);SendMessageW(passwordNote,WM_SETFONT,(WPARAM)smallFont,TRUE);mutedControls.insert(passwordNote);
        button(L"开始检测",Run,46,584,262);MoveWindow(fields[Run],s(46),s(584),s(262),s(42),FALSE);
        status=control(L"STATIC",L"准备连接",0,46,643,262,22,SS_CENTER);SendMessageW(status,WM_SETFONT,(WPARAM)smallFont,TRUE);mutedControls.insert(status);

        resultTitle=control(L"STATIC",L"等待检测",0,374,128,620,28,SS_ENDELLIPSIS);SendMessageW(resultTitle,WM_SETFONT,(WPARAM)sectionFont,TRUE);
        layoutNote=control(L"STATIC",L"填写连接信息后，点击“开始检测”",0,374,164,620,22,SS_ENDELLIPSIS);
        advice=control(L"EDIT",L"将读取路由器型号、分区容量和起始位置，并给出匹配建议。",0,374,190,620,64,ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_TABSTOP);
        comparisonNote=control(L"STATIC",firmware.empty()?L"未选择固件 · 可先查看路由器布局":wide("所选固件："+utf8(firmware.filename().wstring())).c_str(),0,374,264,620,20,SS_ENDELLIPSIS);
        SendMessageW(comparisonNote,WM_SETFONT,(WPARAM)smallFont,TRUE);
        resultControls={resultTitle,layoutNote,advice,comparisonNote};

        auto partitions=control(L"STATIC",L"分区信息",0,374,338,620,25);SendMessageW(partitions,WM_SETFONT,(WPARAM)sectionFont,TRUE);
        device=control(L"STATIC",L"连接后显示路由器型号",0,374,370,620,24,SS_ENDELLIPSIS);mutedControls.insert(device);
        list=control(WC_LISTVIEWW,L"路由器分区",0,374,402,620,262,LVS_REPORT|LVS_SINGLESEL|LVS_SHAREIMAGELISTS|WS_TABSTOP);
        ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);SetWindowTheme(list,L"Explorer",nullptr);
        rowHeight=ImageList_Create(1,s(25),ILC_COLOR32,1,1);if(rowHeight)ListView_SetImageList(list,rowHeight,LVSIL_SMALL);
        const wchar_t* headers[]={L"分区",L"名称",L"容量",L"起始位置"};int widths[]={68,190,120,218};
        for(int i=0;i<4;i++){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.cx=s(widths[i]);c.pszText=const_cast<wchar_t*>(headers[i]);ListView_InsertColumn(list,i,&c);}
        emptyList=control(L"STATIC",L"尚未读取分区\r\n\r\n检测完成后，信息会显示在这里",0,424,480,520,96,SS_CENTER);mutedControls.insert(emptyList);
        ShowWindow(list,SW_HIDE);
        auto footer=control(L"STATIC",L"只读取信息，不修改路由器。分区匹配不代表完整刷机兼容性。",0,24,711,820,22);SendMessageW(footer,WM_SETFONT,(WPARAM)smallFont,TRUE);mutedControls.insert(footer);
        button(L"关闭",Close,902,703,114);gateways();
    }
    void paint(){PAINTSTRUCT ps;auto dc=BeginPaint(window,&ps);RECT bounds;GetClientRect(window,&bounds);FillRect(dc,&bounds,canvasBrush);
        ui::rounded(dc,{s(24),s(108),s(330),s(684)},RGB(255,255,255),ui::line,s(14));
        ui::rounded(dc,{s(352),s(108),s(1016),s(302)},resultFill,resultBorder,s(14));
        ui::rounded(dc,{s(352),s(318),s(1016),s(684)},RGB(255,255,255),ui::line,s(14));
        for(auto& [w,r]:editBoxes){bool focused=GetFocus()==w||(w==fields[Address]&&GetFocus()==fields[GatewayChoices]);ui::rounded(dc,r,RGB(255,255,255),focused?ui::accent:ui::line,s(10));}EndPaint(window,&ps);
    }
    HBRUSH color(HDC dc,HWND child){SetBkMode(dc,TRANSPARENT);SetTextColor(dc,mutedControls.count(child)?ui::muted:ui::ink);
        if(resultControls.count(child)){SetBkColor(dc,resultFill);if(child==resultTitle)SetTextColor(dc,resultInk);return resultBrush;}
        if(canvasControls.count(child)){SetBkColor(dc,ui::canvas);return canvasBrush;}SetBkColor(dc,RGB(255,255,255));return (HBRUSH)GetStockObject(WHITE_BRUSH);
    }
    void outcome(const std::string& title,const std::string& layout,const std::string& body,const std::string& note,const std::string& level){
        resultFill=ui::tint;resultBorder=RGB(216,226,251);resultInk=ui::accent;
        if(level=="match"){resultFill=RGB(237,248,242);resultBorder=RGB(207,232,219);resultInk=RGB(30,116,79);}
        else if(level=="mismatch"||level=="unknown"){resultFill=RGB(255,248,235);resultBorder=RGB(244,227,191);resultInk=RGB(149,95,23);}
        else if(level=="error"){resultFill=RGB(255,242,242);resultBorder=RGB(243,218,218);resultInk=RGB(171,60,60);}
        DeleteObject(resultBrush);resultBrush=CreateSolidBrush(resultFill);
        SetWindowTextW(resultTitle,wide(title).c_str());SetWindowTextW(layoutNote,wide(layout).c_str());SetWindowTextW(advice,wide(body).c_str());SetWindowTextW(comparisonNote,wide(note).c_str());
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }
    void busy(bool value){running=value;for(auto& [id,w]:fields)if(id!=Close&&id!=Run)EnableWindow(w,!value);
        EnableWindow(fields[GatewayChoices],!value&&!availableGateways.empty());
        SetWindowTextW(fields[Run],value?L"取消检测":L"开始检测");InvalidateRect(window,nullptr,FALSE);}
    void resetResult(){if(!hasResult||running)return;hasResult=false;ListView_DeleteAllItems(list);ShowWindow(list,SW_HIDE);ShowWindow(emptyList,SW_SHOW);SetWindowTextW(emptyList,L"等待重新检测");
        SetWindowTextW(device,L"连接后显示路由器型号");SetWindowTextW(status,L"连接信息已更改");outcome("等待重新检测","连接信息已更改","点击“开始检测”，读取当前地址对应的设备信息。","不影响固件转换。","ready");}
    void start(){
        if(running){cancel=true;EnableWindow(fields[Run],FALSE);SetWindowTextW(status,L"正在取消…");return;}
        SshOptions options;options.address=get(Address);options.username=get(Username);auto port=get(Port);
        need(!port.empty()&&port.size()<=5&&port.find_first_not_of("0123456789")==port.npos,"请填写 1–65535 之间的 SSH 端口。");
        auto number=std::stoul(port);need(number>0&&number<=65535,"请填写 1–65535 之间的 SSH 端口。");options.port=uint16_t(number);
        endpoint="["+options.address+"]:"+std::to_string(options.port);options.fingerprint=trustedHosts[endpoint];options.password=get(Password);
        if(worker.joinable())worker.join();cancel=false;hasResult=false;busy(true);ListView_DeleteAllItems(list);ShowWindow(list,SW_HIDE);ShowWindow(emptyList,SW_SHOW);SetWindowTextW(emptyList,L"正在读取分区…");
        SetWindowTextW(device,L"等待路由器返回信息");SetWindowTextW(status,L"正在读取设备信息…");
        outcome("正在检测",options.address+":"+std::to_string(options.port),"正在连接 SSH 并读取分区信息，请稍候。","可随时取消，不会修改路由器。","ready");
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
            if(cancel){SetWindowTextW(status,L"已取消检测。");outcome("已取消","尚未取得检测结果","可以调整连接信息后重新检测。","取消不影响固件转换。","ready");SetWindowTextW(emptyList,L"尚未读取分区");return;}
            auto text=(c.changed?std::string("此地址的 SSH 身份已变化。请确认连接的是你的路由器。\n\n"):std::string("首次连接这台设备，请核对 SSH 指纹。\n\n"))+endpoint+"\n"+c.fingerprint+"\n\n是否信任并继续？";
            if(MessageBoxW(window,wide(text).c_str(),L"确认设备身份",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES){trustedHosts[endpoint]=c.fingerprint;start();}
            else{SetWindowTextW(status,L"未连接；尚未发送密码。");SetWindowTextW(device,L"等待检测");SetWindowTextW(emptyList,L"尚未读取分区");outcome("尚未连接","设备身份未确认","核对路由器的 SSH 指纹后，可以重新开始检测。","尚未发送密码。","ready");}return;
        }
        if(!c.error.empty()){SetWindowTextW(status,cancel?L"已取消检测":L"未完成检测");SetWindowTextW(device,L"未取得有效检测结果");SetWindowTextW(emptyList,L"尚未读取分区");
            outcome(cancel?"已取消":"未能完成检测","尚未取得检测结果",c.error,"请检查连接信息后重试。",cancel?"ready":"error");return;}
        auto& snapshot=c.result.at("snapshot");auto title=snapshot.value("model",snapshot.value("board",std::string("未知设备")));
        SetWindowTextW(device,wide(title).c_str());int row=0;
        for(auto& p:snapshot.at("partitions")){
            auto id=wide(p.at("id")),name=wide(p.at("name"));std::ostringstream size;size<<std::fixed<<std::setprecision(2)<<double(p.at("size").get<uint64_t>())/1048576<<" MiB";
            std::wstring offset=L"未取得";if(p.contains("offset")){std::ostringstream value;value<<"0x"<<std::hex<<p.at("offset").get<uint64_t>();offset=wide(value.str());}
            else if(snapshot.value("tree_valid",false)){size_t count=0;uint64_t start=0;for(auto& t:snapshot.at("tree_partitions"))if(t.at("name")==p.at("name")&&t.at("size")==p.at("size")){count++;start=t.at("offset");}
                if(count==1){std::ostringstream value;value<<"0x"<<std::hex<<start<<" (设备树)";offset=wide(value.str());}}
            auto capacity=wide(size.str());LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=row;item.pszText=id.data();ListView_InsertItem(list,&item);
            ListView_SetItemText(list,row,1,name.data());ListView_SetItemText(list,row,2,capacity.data());ListView_SetItemText(list,row,3,offset.data());row++;
        }
        ShowWindow(emptyList,row?SW_HIDE:SW_SHOW);ShowWindow(list,row?SW_SHOW:SW_HIDE);if(!row)SetWindowTextW(emptyList,L"路由器未返回 MTD 分区信息");
        SetWindowTextW(status,L"检测完成 · 仅供参考");hasResult=true;
        const auto note=firmware.empty()?std::string("未选择固件 · 当前只核对路由器布局"):c.result.value("title","")=="所选固件暂时无法完成比对"?c.result.value("firmware_note",""):"所选固件："+utf8(firmware.filename().wstring());
        auto body=c.result.value("level","")=="match"?std::string("当前分区符合本工具的 stock 目标布局，无需再次调整分区。刷入前仍需确认所选固件与设备匹配。"):c.result.value("advice","");
        outcome(c.result.value("title",""),c.result.value("layout",""),body,note,c.result.value("level","unknown"));
    }
    void close(){if(running){closing=true;cancel=true;EnableWindow(fields[Close],FALSE);EnableWindow(fields[Run],FALSE);SetWindowTextW(status,L"正在结束连接…");}else DestroyWindow(window);}
};
LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM wp,LPARAM lp){
    auto v=(Viewer*)GetWindowLongPtrW(w,GWLP_USERDATA);if(m==WM_NCCREATE){v=(Viewer*)((CREATESTRUCTW*)lp)->lpCreateParams;v->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)v);}
    if(!v)return DefWindowProcW(w,m,wp,lp);
    try{switch(m){
    case WM_CREATE:v->create();return 0;
    case WM_PAINT:v->paint();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_DRAWITEM:if(((DRAWITEMSTRUCT*)lp)->CtlID==GatewayChoices)v->drawGatewayButton((DRAWITEMSTRUCT*)lp);else ui::button((DRAWITEMSTRUCT*)lp,v->font,v->scale,((DRAWITEMSTRUCT*)lp)->CtlID==Run);return TRUE;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORBTN:return (LRESULT)v->color((HDC)wp,(HWND)lp);
    case WM_NOTIFY:{auto header=(NMHDR*)lp;if(header->hwndFrom==v->list&&header->code==NM_CUSTOMDRAW){auto draw=(NMLVCUSTOMDRAW*)lp;
        if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
        if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){draw->clrText=ui::ink;draw->clrTextBk=draw->nmcd.dwItemSpec%2?RGB(246,248,252):RGB(255,255,255);return CDRF_NEWFONT;}
    }return 0;}
    case WM_COMMAND:
        if((LOWORD(wp)==Address||LOWORD(wp)==Port||LOWORD(wp)==Username)&&HIWORD(wp)==EN_CHANGE)v->resetResult();
        else if(LOWORD(wp)==GatewayChoices&&HIWORD(wp)==BN_CLICKED)v->chooseGateway();
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
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));cls.hbrBackground=nullptr;
    if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
    Viewer viewer(owner,firmware,font,scale);RECT parent;GetWindowRect(owner,&parent);RECT bounds{0,0,viewer.s(1040),viewer.s(758)};
    DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN;AdjustWindowRectEx(&bounds,style,FALSE,WS_EX_DLGMODALFRAME);
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    int x=std::max(monitor.rcWork.left,std::min(parent.left+(parent.right-parent.left-width)/2,monitor.rcWork.right-width)),y=std::max(monitor.rcWork.top,std::min(parent.top,monitor.rcWork.bottom-height));
    auto window=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,L"Kwrt Studio · 路由器检测",style,x,y,width,height,owner,nullptr,instance,&viewer);
    need(window!=nullptr,"无法打开路由器检测窗口。");EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);SetFocus(window);
    MSG message{};int result=1;while(IsWindow(window)&&(result=GetMessageW(&message,nullptr,0,0))>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    if(IsWindow(window)){viewer.cancel=true;DestroyWindow(window);}if(IsWindow(owner)){EnableWindow(owner,TRUE);SetActiveWindow(owner);}if(result==0)PostQuitMessage(int(message.wParam));
}
