#pragma once
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <string>

namespace ui {
inline constexpr COLORREF ink=RGB(28,40,61),muted=RGB(105,119,140),accent=RGB(48,91,226);
inline constexpr COLORREF canvas=RGB(245,247,251),line=RGB(223,229,239),tint=RGB(234,240,255);

inline void rounded(HDC dc,RECT r,COLORREF fill,COLORREF border,int radius=12,int stroke=1){
    auto brush=CreateSolidBrush(fill);auto pen=CreatePen(PS_SOLID,stroke,border);
    auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(pen);
}
inline LRESULT CALLBACK inputProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_ENABLE)InvalidateRect(GetParent(w),nullptr,FALSE);
    return DefSubclassProc(w,m,wp,lp);
}
inline LRESULT CALLBACK buttonProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(m==WM_MOUSEMOVE&&!GetPropW(w,L"SLS.Hover")){
        SetPropW(w,L"SLS.Hover",(HANDLE)1);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,w,0};TrackMouseEvent(&track);InvalidateRect(w,nullptr,FALSE);
    }else if(m==WM_MOUSELEAVE){RemovePropW(w,L"SLS.Hover");InvalidateRect(w,nullptr,FALSE);}
    else if(m==WM_NCDESTROY)RemovePropW(w,L"SLS.Hover");
    return DefSubclassProc(w,m,wp,lp);
}
inline void button(DRAWITEMSTRUCT* d,HFONT font,double scale,bool primary=false,bool selected=false){
    const bool disabled=(d->itemState&ODS_DISABLED)!=0,pressed=(d->itemState&ODS_SELECTED)!=0,hover=GetPropW(d->hwndItem,L"SLS.Hover")!=nullptr;
    COLORREF fill=RGB(255,255,255),border=line,text=ink;
    if(selected){fill=tint;border=tint;text=accent;}
    if(hover&&!disabled){fill=RGB(241,245,255);border=RGB(178,195,237);}
    if(primary){fill=pressed?RGB(33,70,187):hover?RGB(62,104,240):accent;border=fill;text=RGB(255,255,255);}
    if(disabled){fill=RGB(235,239,245);border=fill;text=RGB(154,165,182);}
    RECT r=d->rcItem;InflateRect(&r,-1,-1);rounded(d->hDC,r,fill,border,int(12*scale));
    wchar_t label[256];GetWindowTextW(d->hwndItem,label,256);
    auto old=SelectObject(d->hDC,font);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,text);
    DrawTextW(d->hDC,label,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(d->itemState&ODS_FOCUS){InflateRect(&r,-int(5*scale),-int(5*scale));DrawFocusRect(d->hDC,&r);}
    SelectObject(d->hDC,old);
}
}
