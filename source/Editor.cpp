#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include "NabdGate.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include <cstdio>
#include <cstring>
#include <string>

extern HINSTANCE ghInst;
namespace Nabd {
namespace {
constexpr int W=920,BaseHeight=520,RowPitch=74;
constexpr COLORREF Background=RGB(15,19,24), Panel=RGB(23,29,36), Edge=RGB(44,53,63);
constexpr COLORREF White=RGB(231,238,241), Muted=RGB(133,149,160), Green=RGB(98,235,185);
constexpr COLORREF Violet=RGB(195,159,255);
struct Box {
    int x,y,w,h;
    RECT rect() const { return {x,y,x+w,y+h}; }
    bool hit(int px,int py) const { return px>=x && py>=y && px<x+w && py<y+h; }
};
Box pad(int row,int i) { return {36+i*52+(i/4)*6,238+row*RowPitch,46,44}; }
Box rateBox(int i) { return {157+i*82,123,74,32}; }
Box countBox(int i) { return {157+i*42,174,34,26}; }
constexpr Box AddBox{350,174,124,26};
constexpr Box GateTab{36,77,112,25},RepeatTab{158,77,158,25},ModuleBox{753,77,130,25};
constexpr Box BypassBox{753,40,130,34};
Box smoothBox(int rows) { return {36,369+(rows-1)*RowPitch,409,86}; }
Box wetBox(int rows) { return {465,369+(rows-1)*RowPitch,418,86}; }
constexpr Box Buttons[] {{36,316,95,30},{139,316,95,30},{242,316,95,30},
                          {588,316,92,30},{688,316,92,30},{788,316,95,30}};
void fill(HDC dc,Box b,COLORREF color) {
    auto brush=CreateSolidBrush(color); auto r=b.rect(); FillRect(dc,&r,brush); DeleteObject(brush);
}
void rounded(HDC dc,Box b,COLORREF color,COLORREF stroke,int radius=10) {
    auto brush=CreateSolidBrush(color); auto pen=CreatePen(PS_SOLID,1,stroke);
    auto ob=SelectObject(dc,brush),op=SelectObject(dc,pen);
    RoundRect(dc,b.x,b.y,b.x+b.w,b.y+b.h,radius,radius);
    SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(brush); DeleteObject(pen);
}
void text(HDC dc,const std::wstring& s,Box b,int size,COLORREF color,bool bold=false,UINT align=DT_LEFT) {
    auto font=CreateFontW(-size,0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto old=SelectObject(dc,font); SetTextColor(dc,color); SetBkMode(dc,TRANSPARENT);
    auto r=b.rect(); DrawTextW(dc,s.c_str(),-1,&r,align|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
    SelectObject(dc,old); DeleteObject(font);
}
std::wstring wide(const char* s) { std::wstring result; for (;*s;++s) result+=wchar_t(*s); return result; }

class View final : public EditorView, public IPlugViewContentScaleSupport {
public:
    explicit View(Controller* c) : EditorView(c), owner(c) {
        displayRows=patternCount(value(PatternCount)); rect={0,0,W,logicalHeight()};
    }
    ~View() override { detach(); }
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override {
        return type && std::strcmp(type,kPlatformTypeHWND)==0 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API attached(void* parent,FIDString type) override {
        if (!parent || hwnd || isPlatformTypeSupported(type)!=kResultTrue) return kResultFalse;
        WNDCLASSW wc{}; wc.lpfnWndProc=windowProc; wc.hInstance=ghInst;
        wc.lpszClassName=L"NabdGate.Editor.1"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        RegisterClassW(&wc);
        hwnd=CreateWindowExW(0,wc.lpszClassName,L"Nabd Gate",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,
            0,0,rect.getWidth(),rect.getHeight(),static_cast<HWND>(parent),nullptr,ghInst,this);
        if (!hwnd) return kResultFalse;
        systemWindow=parent; SetTimer(hwnd,1,33,nullptr); attachedToParent(); return kResultOk;
    }
    tresult PLUGIN_API removed() override {
        detach(); systemWindow=nullptr; removedFromParent(); return kResultOk;
    }
    tresult PLUGIN_API onSize(ViewRect* r) override {
        if (!r || r->getWidth()<1 || r->getHeight()<1) return kInvalidArgument;
        rect=*r;
        if (hwnd) SetWindowPos(hwnd,nullptr,0,0,r->getWidth(),r->getHeight(),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        return kResultOk;
    }
    tresult PLUGIN_API checkSizeConstraint(ViewRect* r) override {
        if (!r) return kInvalidArgument;
        r->right=r->left+int(W*scale); r->bottom=r->top+int(logicalHeight()*scale); return kResultOk;
    }
    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override {
        if (!std::isfinite(factor) || factor<0.5f || factor>4.f) return kInvalidArgument;
        scale=factor;
        ViewRect next(0,0,int(W*scale),int(logicalHeight()*scale));
        if (plugFrame) plugFrame->resizeView(this,&next); else onSize(&next);
        return kResultOk;
    }
    OBJ_METHODS(View,EditorView)
    DEFINE_INTERFACES
        DEF_INTERFACE(IPlugViewContentScaleSupport)
    END_DEFINE_INTERFACES(EditorView)
    REFCOUNT_METHODS(EditorView)
private:
    Controller* owner;
    HWND hwnd=nullptr;
    float scale=1.f;
    int dragParam=-1, lastPad=-1;
    int displayRows=2, selectedRow=0;
    bool drawOn=false, painting=false;
    bool repeatView=false;
    double value(ParamID id) { return owner->getParamNormalized(id); }
    int extra() const { return repeatView?48:0; }
    COLORREF accent() const { return repeatView?Violet:Green; }
    uint32_t cellParam(int row,int step) const { return repeatView?repeatParam(row,step):stepParam(row,step); }
    Box shifted(Box b) const { b.y+=extra();return b; }
    Box padBox(int row,int i) const { return shifted(pad(row,i)); }
    Box patternButton(int i) const { return shifted(countBox(i)); }
    Box sliceBox(int i) const {auto b=rateBox(i);b.y+=48;return b;}
    int logicalHeight() const { return BaseHeight+(displayRows-1)*RowPitch+extra(); }
    Box actionBox(int i) const { auto b=Buttons[i]; b.y+=(displayRows-1)*RowPitch+extra(); return b; }
    void refreshSize(bool force=false) {
        const int nextRows=patternCount(value(PatternCount));
        if (nextRows==displayRows && !force) return;
        finishDrag();
        displayRows=nextRows; selectedRow=std::min(selectedRow,displayRows-1);
        ViewRect next(0,0,int(W*scale),int(logicalHeight()*scale));
        if (plugFrame) plugFrame->resizeView(this,&next); else onSize(&next);
    }
    void finishDrag() {
        if (dragParam>=0) { owner->endEdit(dragParam); dragParam=-1; }
        painting=false; lastPad=-1;
    }
    void detach() {
        finishDrag();
        if (hwnd) {
            if (GetCapture()==hwnd) ReleaseCapture();
            KillTimer(hwnd,1); DestroyWindow(hwnd); hwnd=nullptr;
        }
    }
    void updateSlider(int x) {
        const auto b=shifted(dragParam==Smooth ? smoothBox(displayRows) : wetBox(displayRows));
        const double v=normalized(double(x-(b.x+18))/(b.w-36));
        owner->setParamNormalized(dragParam,v); owner->performEdit(dragParam,v);
    }
    void paintPad(int x,int y) {
        for (int row=0;row<displayRows;++row) for (int i=0;i<Steps;++i) {
            const int id=cellParam(row,i);
            if (padBox(row,i).hit(x,y) && lastPad!=id) {
                owner->edit(id,drawOn ? 1. : 0.); lastPad=id; selectedRow=row; return;
            }
        }
    }
    void preset(int index) {
        owner->startGroupEdit();
        for (int i=0;i<Steps;++i) {
            double v=0;
            switch(index) {
                case 0:v=i%2==0;break;
                case 1:v=i%4>=2;break;
                case 2:v=repeatView?(i%4==3?1.:0.):LegacyDefaults[i];break;
                case 3:v=1;break;
                case 4:v=0;break;
                case 5:v=value(cellParam(selectedRow,i))<0.5;break;
            }
            owner->edit(cellParam(selectedRow,i),v);
        }
        owner->finishGroupEdit();
    }
    void down(int x,int y) {
        if (GateTab.hit(x,y) || RepeatTab.hit(x,y)) {
            finishDrag();repeatView=RepeatTab.hit(x,y);refreshSize(true);return;
        }
        if (ModuleBox.hit(x,y)) {
            const auto id=repeatView?RepeatEnabled:GateEnabled;owner->edit(id,value(id)<0.5?1.:0.);return;
        }
        if(repeatView)for(int i=0;i<6;++i)if(sliceBox(i).hit(x,y)){owner->edit(RepeatSize,double(i)/5.);return;}
        for (int i=0;i<MaxPatterns;++i) if (patternButton(i).hit(x,y)) {
            owner->edit(PatternCount,double(i)/3.); refreshSize(); return;
        }
        if (shifted(AddBox).hit(x,y)) {
            if (displayRows<MaxPatterns) { owner->edit(PatternCount,double(displayRows)/3.); refreshSize(); selectedRow=displayRows-1; }
            return;
        }
        for (int row=0;row<displayRows;++row) if (Box{36,210+row*RowPitch+extra(),847,22}.hit(x,y)) {
            selectedRow=row; return;
        }
        if (BypassBox.hit(x,y)) { owner->edit(Bypass,value(Bypass)<0.5?1.:0.); return; }
        for (int i=0;i<6;++i) {
            if (rateBox(i).hit(x,y)) { owner->edit(Rate,double(i)/5.); return; }
            if (actionBox(i).hit(x,y)) { preset(i); return; }
        }
        if (shifted(smoothBox(displayRows)).hit(x,y) || shifted(wetBox(displayRows)).hit(x,y)) {
            dragParam=shifted(smoothBox(displayRows)).hit(x,y)?Smooth:Wet;
            owner->beginEdit(dragParam); SetCapture(hwnd); updateSlider(x); return;
        }
        for (int row=0;row<displayRows;++row) for (int i=0;i<Steps;++i) if (padBox(row,i).hit(x,y)) {
            painting=true; drawOn=value(cellParam(row,i))<0.5; lastPad=-1;
            SetCapture(hwnd); paintPad(x,y); return;
        }
    }
    void slider(HDC dc,Box b,const wchar_t* title,ParamID id,const std::wstring& display) {
        rounded(dc,b,Panel,Edge,12);
        text(dc,title,{b.x+18,b.y+10,200,25},14,Muted,true);
        text(dc,display,{b.x+b.w-130,b.y+8,112,28},19,White,true,DT_RIGHT);
        const int left=b.x+18,right=b.x+b.w-18,cy=b.y+59;
        fill(dc,{left,cy-2,right-left,4},Edge);
        const int knob=left+int(value(id)*(right-left));
        fill(dc,{left,cy-2,knob-left,4},Green);
        rounded(dc,{knob-5,cy-7,10,14},White,White,6);
    }
    void paint(HDC dc,int width,int height) {
        const int saved=SaveDC(dc);
        SetMapMode(dc,MM_ANISOTROPIC); SetWindowExtEx(dc,W,logicalHeight(),nullptr); SetViewportExtEx(dc,width,height,nullptr);
        fill(dc,{0,0,W,logicalHeight()},Background);
        for (int i=0;i<4;++i) fill(dc,{36+i*7,43+(i%2)*7,4,26-(i%2)*14},Green);
        text(dc,L"NABD",{76,25,120,47},34,White,true);
        text(dc,L"GATE",{195,32,110,38},27,Green);
        rounded(dc,GateTab,repeatView?Panel:RGB(29,65,53),repeatView?Edge:RGB(58,139,105),8);
        text(dc,L"VOLUME / GATE",GateTab,10,repeatView?Muted:Green,true,DT_CENTER);
        rounded(dc,RepeatTab,repeatView?RGB(52,38,74):Panel,repeatView?RGB(104,78,144):Edge,8);
        text(dc,L"REPEAT / STUTTER",RepeatTab,10,repeatView?Violet:Muted,true,DT_CENTER);
        const bool moduleOn=value(repeatView?RepeatEnabled:GateEnabled)>=0.5;
        rounded(dc,ModuleBox,moduleOn?(repeatView?RGB(52,38,74):RGB(29,65,53)):Panel,Edge,8);
        text(dc,repeatView?(moduleOn?L"REPEAT ON":L"REPEAT OFF"):(moduleOn?L"GATE ON":L"GATE OFF"),ModuleBox,11,moduleOn?accent():Muted,true,DT_CENTER);
        const std::wstring modules=std::wstring(L"GATE ")+(value(GateEnabled)>=0.5?L"ON":L"OFF")+L"  /  REPEAT "+(value(RepeatEnabled)>=0.5?L"ON":L"OFF");
        text(dc,modules,{350,78,384,24},10,Muted,false,DT_RIGHT);
        const double bpm=value(Tempo)*1000.;
        const int active=int(std::lround(value(Playhead)*(Steps*MaxPatterns)))-1;
        const bool bypass=value(Bypass)>=0.5;
        wchar_t tempoText[80];
        if (bpm>0.) std::swprintf(tempoText,80,L"%.1f BPM  /  %ls",bpm,active>=0 ? L"HOST SYNC" : L"STOPPED");
        else std::swprintf(tempoText,80,L"WAITING FOR HOST");
        text(dc,tempoText,{465,42,271,29},13,Muted,true,DT_RIGHT);
        rounded(dc,BypassBox,bypass?RGB(67,43,33):RGB(26,54,46),bypass?RGB(125,77,52):RGB(46,99,77));
        text(dc,bypass?L"BYPASSED":L"FX ACTIVE",BypassBox,12,bypass?RGB(245,179,119):Green,true,DT_CENTER);
        fill(dc,{36,106,847,1},Edge);
        text(dc,L"STEP RATE",{36,122,110,34},12,Muted,true);
        const int selected=rateIndex(value(Rate));
        for (int i=0;i<6;++i) {
            auto b=rateBox(i); rounded(dc,b,i==selected?RGB(29,65,53):Panel,i==selected?RGB(58,139,105):Edge,8);
            text(dc,wide(RateNames[i]),b,13,i==selected?Green:Muted,true,DT_CENTER);
        }
        const double quarters=16.*displayRows/StepRates[selected]; wchar_t length[64];
        std::swprintf(length,64,L"%d steps / %.2g beats",Steps*displayRows,quarters);
        text(dc,length,{664,122,219,34},12,Muted,false,DT_RIGHT);
        if (repeatView) {
            text(dc,L"REPEAT SIZE",{36,171,110,32},12,Violet,true);
            const int size=rateIndex(value(RepeatSize));
            for(int i=0;i<6;++i){auto b=sliceBox(i);rounded(dc,b,i==size?RGB(52,38,74):Panel,i==size?RGB(104,78,144):Edge,8);text(dc,wide(RepeatNames[i]),b,13,i==size?Violet:Muted,true,DT_CENTER);}
            text(dc,L"SMALLER = FASTER",{664,171,219,32},10,Muted,false,DT_RIGHT);
        }
        text(dc,L"PATTERNS",shifted({36,173,110,28}),12,Muted,true);
        for (int row=0;row<MaxPatterns;++row) {
            auto b=patternButton(row); const bool chosen=row+1==displayRows;
            rounded(dc,b,chosen?RGB(29,65,53):Panel,chosen?RGB(58,139,105):Edge,8);
            text(dc,std::to_wstring(row+1),b,12,chosen?Green:Muted,true,DT_CENTER);
        }
        rounded(dc,shifted(AddBox),Panel,Edge,8);
        text(dc,displayRows<MaxPatterns?L"+ PATTERN":L"4 PATTERNS MAX",shifted(AddBox),11,displayRows<MaxPatterns?accent():Muted,true,DT_CENTER);
        std::wstring chain=L"LOOP  ";
        for (int row=0;row<displayRows;++row) {if(row)chain+=L"  >  ";chain+=std::to_wstring(row+1);}
        text(dc,chain,shifted({505,173,378,28}),12,White,true,DT_RIGHT);
        for (int row=0;row<displayRows;++row) {
            const bool rowPlaying=active>=0 && active/Steps==row && !bypass;
            const std::wstring title=L"PATTERN 0"+std::to_wstring(row+1)+(row==selectedRow?L"  /  EDITING":L"");
            text(dc,title,shifted({36,210+row*RowPitch,340,22}),11,row==selectedRow?accent():Muted,true);
            text(dc,rowPlaying?L"PLAYING":L"CLICK OR DRAG TO PAINT",shifted({535,210+row*RowPitch,348,22}),10,rowPlaying?accent():Muted,false,DT_RIGHT);
        for (int i=0;i<Steps;++i) {
            const auto b=padBox(row,i); const bool on=value(cellParam(row,i))>=0.5;
            const bool current=row*Steps+i==active && !bypass;
            wchar_t number[8]; std::swprintf(number,8,L"%02d",i+1);
            rounded(dc,b,on?accent():Panel,current?White:(on?accent():Edge),8);
            text(dc,number,{b.x,b.y+2,b.w,24},15,on?RGB(15,53,38):Muted,true,DT_CENTER);
            text(dc,repeatView?(on?L"LOOP":L"LIVE"):(on?L"ON":L"OFF"),{b.x,b.y+25,b.w,14},8,on?(repeatView?RGB(64,37,105):RGB(31,105,73)):RGB(89,104,117),true,DT_CENTER);
            fill(dc,{b.x,b.y+b.h+2,b.w,3},current?White:Edge);
            if (current) fill(dc,{b.x+18,b.y-8,10,3},accent());
        }
        }
        const wchar_t* labels[]{L"PULSE",L"OFFBEAT",L"SCATTER",L"ALL ON",L"ALL OFF",L"INVERT"};
        for (int i=0;i<6;++i) {
            const auto b=actionBox(i); rounded(dc,b,Panel,Edge,8); text(dc,labels[i],b,11,Muted,true,DT_CENTER);
        }
        text(dc,L"EDIT PATTERN 0"+std::to_wstring(selectedRow+1),{345,actionBox(0).y,232,30},10,Muted,false,DT_CENTER);
        wchar_t smooth[32],wet[32];
        std::swprintf(smooth,32,L"%.1f ms",value(Smooth)*20.);
        std::swprintf(wet,32,L"%.0f %%",value(Wet)*100.);
        slider(dc,shifted(smoothBox(displayRows)),L"SMOOTHING",Smooth,smooth);
        slider(dc,shifted(wetBox(displayRows)),L"WET / DRY",Wet,wet);
        text(dc,repeatView?L"LOOP = REPEAT A SLICE     /     LIVE = ORIGINAL SOUND":L"ON = SOUND PASSES     /     OFF = SOUND MUTES",{36,logicalHeight()-32,590,20},10,Muted);
        text(dc,L"NABD AUDIO  /  v1.2",{650,logicalHeight()-32,233,20},10,Muted,false,DT_RIGHT);
        RestoreDC(dc,saved);
    }
    static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<View*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (message==WM_NCCREATE) {
            self=static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window,message,wp,lp);
        RECT client{}; GetClientRect(window,&client);
        const int width=std::max(1L,client.right),height=std::max(1L,client.bottom);
        const int x=int(double(GET_X_LPARAM(lp))*W/width),y=int(double(GET_Y_LPARAM(lp))*self->logicalHeight()/height);
        switch (message) {
            case WM_ERASEBKGND:return 1;
            case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
            case WM_TIMER:self->refreshSize();InvalidateRect(window,nullptr,FALSE);return 0;
            case WM_PRINTCLIENT:self->paint(reinterpret_cast<HDC>(wp),width,height);return 0;
            case WM_PAINT: {
                PAINTSTRUCT ps; auto dc=BeginPaint(window,&ps);
                auto memory=CreateCompatibleDC(dc); auto bitmap=CreateCompatibleBitmap(dc,width,height);
                auto old=SelectObject(memory,bitmap); self->paint(memory,width,height);
                BitBlt(dc,0,0,width,height,memory,0,0,SRCCOPY);
                SelectObject(memory,old); DeleteObject(bitmap); DeleteDC(memory); EndPaint(window,&ps); return 0;
            }
            case WM_LBUTTONDOWN:self->down(x,y);InvalidateRect(window,nullptr,FALSE);return 0;
            case WM_MOUSEMOVE:
                if (self->dragParam>=0) self->updateSlider(x);
                else if (self->painting) self->paintPad(x,y);
                InvalidateRect(window,nullptr,FALSE);return 0;
            case WM_LBUTTONUP:
                self->finishDrag(); if (GetCapture()==window) ReleaseCapture(); return 0;
            case WM_CAPTURECHANGED:self->finishDrag();return 0;
            case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
        }
        return DefWindowProcW(window,message,wp,lp);
    }
};
}
IPlugView* createEditor(Controller* controller) { return new View(controller); }
void releaseEditorClass() { UnregisterClassW(L"NabdGate.Editor.1",ghInst); }
} // namespace Nabd
