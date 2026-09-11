#include "package_view.hpp"
#include "ui.hpp"
#include <commctrl.h>
#include <algorithm>

using namespace ax;
namespace {
enum {Search=2001,OnlyPlugins,Table,Close,ClearSearch};
struct Viewer {
    HWND owner=nullptr,window=nullptr,search=nullptr,only=nullptr,list=nullptr,count=nullptr,description=nullptr,close=nullptr,clear=nullptr;
    HFONT font=nullptr,titleFont=nullptr;double scale=1;const Json& inventory;
    std::vector<size_t> visible;
    Viewer(HWND parent,const Json& data,HFONT f,double dpi):owner(parent),font(f),scale(dpi),inventory(data){}
    ~Viewer(){if(titleFont)DeleteObject(titleFont);}
    int s(int value)const{return int(value*scale+0.5);}
    HWND control(const wchar_t* cls,const wchar_t* text,int id,int x,int y,int width,int height,DWORD style=0,DWORD ex=0){
        auto handle=CreateWindowExW(ex,cls,text,WS_CHILD|WS_VISIBLE|style,s(x),s(y),s(width),s(height),window,
            (HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
        need(handle!=nullptr,"无法打开插件列表。");
        SendMessageW(handle,WM_SETFONT,(WPARAM)font,TRUE);return handle;
    }
    void create(){
        titleFont=CreateFontW(-s(21),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        auto title=control(L"STATIC",L"插件与软件包",0,20,18,700,30);
        SendMessageW(title,WM_SETFONT,(WPARAM)titleFont,TRUE);
        control(L"STATIC",wide(inventory["filename"]).c_str(),0,20,55,835,24,SS_ENDELLIPSIS);
        control(L"STATIC",L"搜索",0,20,98,55,24);
        search=control(L"EDIT",L"",Search,80,90,510,32,ES_AUTOHSCROLL|WS_TABSTOP,WS_EX_CLIENTEDGE);
        SendMessageW(search,EM_SETLIMITTEXT,512,0);
        SendMessageW(search,EM_SETCUEBANNER,FALSE,(LPARAM)L"搜索名称、包名或版本");
        clear=control(L"BUTTON",L"清空",ClearSearch,602,90,82,32,BS_OWNERDRAW|WS_TABSTOP);SetWindowSubclass(clear,ui::buttonProc,1,0);
        only=control(L"BUTTON",L"只看插件",OnlyPlugins,714,91,146,30,BS_AUTOCHECKBOX|WS_TABSTOP);
        SendMessageW(only,BM_SETCHECK,BST_CHECKED,0);
        count=control(L"STATIC",L"",0,20,132,820,24);
        list=control(WC_LISTVIEWW,L"插件列表",Table,20,160,840,302,
            LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
        SetWindowTheme(list,L"Explorer",nullptr);ListView_SetTextColor(list,ui::ink);
        int widths[]={160,250,220,168};const wchar_t* headers[]={L"名称",L"软件包",L"版本",L"状态"};
        for(int index=0;index<4;index++){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.cx=s(widths[index]);
            c.pszText=const_cast<wchar_t*>(headers[index]);ListView_InsertColumn(list,index,&c);}
        description=control(L"EDIT",L"",0,20,477,840,65,ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP);
        close=control(L"BUTTON",L"关闭",Close,750,535,110,32,BS_OWNERDRAW|WS_TABSTOP);SetWindowSubclass(close,ui::buttonProc,1,0);
        update();
    }
    static std::wstring lower(std::wstring value){if(!value.empty())CharLowerBuffW(value.data(),DWORD(value.size()));return value;}
    void update(){
        if(!list)return;
        int length=GetWindowTextLengthW(search);std::wstring query(length+1,0);
        GetWindowTextW(search,query.data(),length+1);query.resize(length);query=lower(query);
        bool pluginsOnly=SendMessageW(only,BM_GETCHECK,0,0)==BST_CHECKED;
        SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);visible.clear();
        const auto& packages=inventory["packages"];
        for(size_t i=0;i<packages.size();i++){
            const auto& package=packages[i];if(pluginsOnly&&!package["is_plugin"].get<bool>())continue;
            auto name=wide(package["display_name"]),packageName=wide(package["name"]),version=wide(package["version"]);
            if(!query.empty()&&lower(name+L" "+packageName+L" "+version).find(query)==std::wstring::npos)continue;
            int row=int(visible.size());visible.push_back(i);
            LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=row;item.pszText=name.data();ListView_InsertItem(list,&item);
            ListView_SetItemText(list,row,1,packageName.data());ListView_SetItemText(list,row,2,version.data());
            auto status=wide(package["status_label"]);ListView_SetItemText(list,row,3,status.data());
        }
        SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,TRUE);
        auto text=L"显示 "+std::to_wstring(visible.size())+(pluginsOnly?L" 个插件":L" 个软件包")+
            L"  ·  列表共 "+std::to_wstring(inventory["plugin_count"].get<size_t>())+L" 个插件、"+
            std::to_wstring(inventory["package_count"].get<size_t>())+L" 个软件包";
        SetWindowTextW(count,text.c_str());
        SetWindowTextW(description,visible.empty()?L"没有找到匹配项。可以换个关键词，或取消“只看插件”查看系统组件和核心。":
            L"选择项目查看说明。包记录中的安装状态不代表已经完成配置或可以运行。");
    }
    void select(){
        int row=ListView_GetNextItem(list,-1,LVNI_SELECTED);
        if(row<0||size_t(row)>=visible.size())return;
        const auto& package=inventory["packages"][visible[row]];
        auto text=wide(package["description"]);
        if(text.empty())text=L"这份固件未提供该软件包的说明。";
        if(!package["installed"].get<bool>())text=L"该项目未被标记为已安装，不能仅凭名称判断可用。 "+text;
        SetWindowTextW(description,text.c_str());
    }
    void layout(){
        if(!list)return;RECT r;GetClientRect(window,&r);int w=r.right,h=r.bottom;
        MoveWindow(search,s(80),s(90),std::max(s(160),w-s(390)),s(32),TRUE);
        MoveWindow(clear,w-s(292),s(90),s(82),s(32),TRUE);
        MoveWindow(only,w-s(174),s(91),s(150),s(30),TRUE);
        MoveWindow(count,s(20),s(132),w-s(40),s(24),TRUE);
        MoveWindow(list,s(20),s(160),w-s(40),std::max(s(140),h-s(300)),TRUE);
        MoveWindow(description,s(20),h-s(128),w-s(40),s(65),TRUE);
        MoveWindow(close,w-s(130),h-s(45),s(110),s(32),TRUE);
        int remaining=w-s(40)-GetSystemMetrics(SM_CXVSCROLL)-s(6);
        int fixed[]={160,250,168};
        ListView_SetColumnWidth(list,0,s(fixed[0]));ListView_SetColumnWidth(list,1,s(fixed[1]));
        ListView_SetColumnWidth(list,2,std::max(s(120),remaining-s(fixed[0]+fixed[1]+fixed[2])));ListView_SetColumnWidth(list,3,s(fixed[2]));
    }
};
LRESULT CALLBACK viewerProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto viewer=(Viewer*)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(message==WM_NCCREATE){viewer=(Viewer*)((CREATESTRUCTW*)lp)->lpCreateParams;viewer->window=window;
        SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)viewer);}
    if(!viewer)return DefWindowProcW(window,message,wp,lp);
    switch(message){
    case WM_CREATE:try{viewer->create();}catch(...){return -1;}return 0;
    case WM_DRAWITEM:ui::button((DRAWITEMSTRUCT*)lp,viewer->font,viewer->scale);return TRUE;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:
        SetBkMode((HDC)wp,TRANSPARENT);SetTextColor((HDC)wp,RGB(29,43,66));return (LRESULT)GetStockObject(WHITE_BRUSH);
    case WM_COMMAND:
        if((LOWORD(wp)==Search&&HIWORD(wp)==EN_CHANGE)||(LOWORD(wp)==OnlyPlugins&&HIWORD(wp)==BN_CLICKED))viewer->update();
        else if(LOWORD(wp)==ClearSearch){SetWindowTextW(viewer->search,L"");SetFocus(viewer->search);}
        else if(LOWORD(wp)==Close||LOWORD(wp)==IDCANCEL)DestroyWindow(window);
        return 0;
    case WM_NOTIFY:{
        auto header=(NMHDR*)lp;if(header->idFrom!=Table)return 0;
        if(header->code==LVN_ITEMCHANGED){viewer->select();return 0;}
        if(header->code==NM_CUSTOMDRAW){
            auto draw=(NMLVCUSTOMDRAW*)lp;
            if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){draw->clrTextBk=draw->nmcd.dwItemSpec%2?RGB(247,249,252):RGB(255,255,255);return CDRF_NOTIFYSUBITEMDRAW;}
            if(draw->nmcd.dwDrawStage==(CDDS_ITEMPREPAINT|CDDS_SUBITEM)){
                size_t row=draw->nmcd.dwItemSpec;draw->clrText=ui::ink;
                if(draw->iSubItem==3&&row<viewer->visible.size())draw->clrText=viewer->inventory["packages"][viewer->visible[row]]["installed"].get<bool>()?RGB(27,124,91):RGB(154,99,22);
                return CDRF_NEWFONT;
            }
        }return 0;
    }
    case WM_SIZE:viewer->layout();return 0;
    case WM_GETMINMAXINFO:{auto m=(MINMAXINFO*)lp;m->ptMinTrackSize={viewer->s(880),viewer->s(540)};return 0;}
    case WM_CLOSE:DestroyWindow(window);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
}
void showPackageViewer(HWND owner,const Json& inventory,HFONT font,double scale){
    auto instance=GetModuleHandleW(nullptr);WNDCLASSEXW cls{sizeof(cls)};
    cls.lpfnWndProc=viewerProc;cls.hInstance=instance;cls.lpszClassName=L"KwrtPackageViewer";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
    Viewer viewer(owner,inventory,font,scale);RECT parent;GetWindowRect(owner,&parent);
    RECT bounds{0,0,viewer.s(940),viewer.s(620)};
    DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_CLIPCHILDREN;
    AdjustWindowRectEx(&bounds,style,FALSE,WS_EX_DLGMODALFRAME);
    int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    auto window=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,L"固件插件列表",style,
        parent.left+(parent.right-parent.left-width)/2,parent.top+(parent.bottom-parent.top-height)/2,
        width,height,nullptr,nullptr,instance,&viewer);
    if(!window){MessageBoxW(owner,L"插件列表窗口未能打开。",L"无法显示列表",MB_OK|MB_ICONINFORMATION);return;}
    EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);SetFocus(viewer.search);
    MSG message{};int result=1;
    while(IsWindow(window)&&(result=GetMessageW(&message,nullptr,0,0))>0){
        if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    if(IsWindow(window))DestroyWindow(window);
    if(IsWindow(owner)){EnableWindow(owner,TRUE);SetActiveWindow(owner);}
    if(result==0)PostQuitMessage(int(message.wParam));
}
