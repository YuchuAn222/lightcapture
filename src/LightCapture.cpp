#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <objidl.h>
#include <gdiplus.h>
#include <psapi.h>
#include <algorithm>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
static std::wstring exePath();
#include "Startup.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"comdlg32.lib")
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"msimg32.lib")
#pragma comment(lib,"psapi.lib")

static const wchar_t* MAIN_CLASS=L"LightCapture.Tray.v1";
static const wchar_t* OVERLAY_CLASS=L"LightCapture.Overlay.v1";
static const wchar_t* RUN_KEY=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* RUN_NAME=L"LightCapture";
static const UINT TRAY_MSG=WM_APP+1, CAPTURE_MSG=WM_APP+2, QUIT_MSG=WM_APP+3, NOTICE_MSG=WM_APP+4;
static const UINT CMD_CAPTURE=100, CMD_AUTOSTART=101, CMD_RETRY=102, CMD_HELP=103, CMD_EXIT=104, CMD_PRIORITY=105;
static HWND mainWindow=nullptr, overlay=nullptr;
static HINSTANCE instanceHandle=nullptr;
static HICON appIcon=nullptr;
static bool hotkeyReady=false, trayAdded=false, dragging=false, selected=false, saving=false, pendingDrag=false;
static POINT anchor={};
static RECT selection={}, desktop={}, toolbar={}, buttons[3]={};
static HFONT uiFont=nullptr;
static double scale=1.0;
static UINT taskbarCreated=0;
static int lastHotkeyError=0;
static HANDLE saveWorkerProcess=nullptr;
static bool priorityEnabled=true;
static HANDLE hookThread=nullptr,hookStarted=nullptr;
static DWORD hookThreadId=0;
static HHOOK keyboardHook=nullptr;
static bool hookA=false;
static const UINT_PTR TRAY_RETRY_TIMER=1;
static int trayRetryCount=0,simulatedTrayFailures=0,processExitCode=0;
static void startupLog(const char* event,DWORD code=0){
    auto path=exePath();path=path.substr(0,path.find_last_of(L"\\/"))+L"\\startup.log";
    WIN32_FILE_ATTRIBUTE_DATA info={};bool large=GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&info) && (info.nFileSizeHigh || info.nFileSizeLow>65536);
    FILE* file=nullptr;if(_wfopen_s(&file,path.c_str(),large?L"w":L"a") || !file)return;
    SYSTEMTIME now;GetLocalTime(&now);
    fprintf(file,"%04u-%02u-%02u %02u:%02u:%02u pid=%lu %s code=%lu\n",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId(),event,code);fclose(file);
}
static bool shortcutReady(){return hotkeyReady || keyboardHook!=nullptr;}
static void readPriority(){
    DWORD value=1,size=sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\LightCapture",L"PriorityAltA",RRF_RT_REG_DWORD,nullptr,&value,&size);
    priorityEnabled=value!=0;
}
static LRESULT CALLBACK keyboardProc(int code,WPARAM wp,LPARAM lp){
    if(code==HC_ACTION){
        const KBDLLHOOKSTRUCT* key=(const KBDLLHOOKSTRUCT*)lp;
        if(key->vkCode=='A'){
            bool down=wp==WM_KEYDOWN || wp==WM_SYSKEYDOWN;
            bool up=wp==WM_KEYUP || wp==WM_SYSKEYUP;
            if(up && hookA){hookA=false;return 1;}
            if(down && hookA)return 1;
            if(down && (key->flags&LLKHF_ALTDOWN) && !(GetAsyncKeyState(VK_CONTROL)&0x8000) &&
                !(GetAsyncKeyState(VK_SHIFT)&0x8000) && !(GetAsyncKeyState(VK_LWIN)&0x8000) && !(GetAsyncKeyState(VK_RWIN)&0x8000)){
                hookA=true;PostMessageW(mainWindow,CAPTURE_MSG,0,0);return 1;
            }
        }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
static DWORD WINAPI hookLoop(LPVOID){
    MSG message;PeekMessageW(&message,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
    keyboardHook=SetWindowsHookExW(WH_KEYBOARD_LL,keyboardProc,instanceHandle,0);
    SetEvent(hookStarted);
    if(keyboardHook){
        while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
        UnhookWindowsHookEx(keyboardHook);keyboardHook=nullptr;
    }
    return 0;
}
static void stopPriorityHook(){
    if(hookThread){PostThreadMessageW(hookThreadId,WM_QUIT,0,0);WaitForSingleObject(hookThread,2000);CloseHandle(hookThread);hookThread=nullptr;}
    if(hookStarted){CloseHandle(hookStarted);hookStarted=nullptr;}hookA=false;
}
static void startPriorityHook(){
    hookStarted=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!hookStarted)return;
    hookThread=CreateThread(nullptr,0,hookLoop,nullptr,0,&hookThreadId);
    if(hookThread)WaitForSingleObject(hookStarted,2000);
}

struct Surface {
    HDC dc=nullptr;
    HBITMAP bitmap=nullptr;
    HGDIOBJ old=nullptr;
    int width=0,height=0;
    void* bits=nullptr;
    Surface()=default;
    Surface(const Surface&)=delete;
    Surface& operator=(const Surface&)=delete;
    ~Surface(){ clear(); }
    void clear(){
        if(dc && old) SelectObject(dc,old);
        if(bitmap) DeleteObject(bitmap);
        if(dc) DeleteDC(dc);
        dc=nullptr;bitmap=nullptr;old=nullptr;bits=nullptr;width=height=0;
    }
    bool create(int w,int h){
        clear();
        if(w<=0 || h<=0 || (unsigned long long)w*h>200000000) return false;
        BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        dc=CreateCompatibleDC(nullptr);
        if(!dc) return false;
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!bitmap){clear();return false;}
        old=SelectObject(dc,bitmap);width=w;height=h;return true;
    }
};
static Surface snapshot, shaded;
static int S(int n){return std::max<int>(1,(int)(n*scale+0.5));}
static std::wstring exePath(){wchar_t p[32768];DWORD n=GetModuleFileNameW(nullptr,p,32768);return std::wstring(p,n);}
static std::wstring startupCommand(){return L"\""+exePath()+L"\" --background";}
static void errorBox(const std::wstring& text){MessageBoxW(overlay?overlay:mainWindow,text.c_str(),L"轻截图",MB_OK|MB_ICONWARNING|MB_TOPMOST);}
static bool autoStartEnabled(){
    if(scheduledAutostartEnabled())return true;
    wchar_t value[32768];DWORD size=sizeof(value),type=0;
    return RegGetValueW(HKEY_CURRENT_USER,RUN_KEY,RUN_NAME,RRF_RT_REG_SZ,&type,value,&size)==ERROR_SUCCESS && startupCommand()==value;
}
static LONG setAutoStart(bool enabled){
    HRESULT taskResult=setScheduledAutostart(enabled);if(FAILED(taskResult))return taskResult;
    HKEY key;LONG result=RegOpenKeyExW(HKEY_CURRENT_USER,RUN_KEY,0,KEY_SET_VALUE,&key);
    if(result==ERROR_FILE_NOT_FOUND)return ERROR_SUCCESS;
    if(result!=ERROR_SUCCESS)return result;
    // Migrate the old entry only after the new logon task has been registered.
    result=RegDeleteValueW(key,RUN_NAME);if(result==ERROR_FILE_NOT_FOUND)result=ERROR_SUCCESS;
    RegCloseKey(key);return result;
}
static void balloon(const wchar_t* title,const wchar_t* text,DWORD type=NIIF_INFO){
    NOTIFYICONDATAW n={sizeof(n)};n.hWnd=mainWindow;n.uID=1;n.uFlags=NIF_INFO;n.dwInfoFlags=type|NIIF_NOSOUND;
    wcscpy_s(n.szInfoTitle,title);wcsncpy_s(n.szInfo,text,_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&n);
}
static bool addTray(){
    if(simulatedTrayFailures>0){--simulatedTrayFailures;startupLog("tray-not-ready-simulated");return false;}
    NOTIFYICONDATAW n={sizeof(n)};n.hWnd=mainWindow;n.uID=1;
    n.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;n.uCallbackMessage=TRAY_MSG;n.hIcon=appIcon;
    wcscpy_s(n.szTip,shortcutReady()?(keyboardHook?L"轻截图 · Alt+A 优先响应":L"轻截图 · Alt+A 截图"):L"轻截图 · 快捷键不可用，右键重试");
    trayAdded=Shell_NotifyIconW(NIM_ADD,&n)!=FALSE;
    if(trayAdded){KillTimer(mainWindow,TRAY_RETRY_TIMER);trayRetryCount=0;startupLog("tray-ready");}
    else startupLog("tray-not-ready",GetLastError());
    return trayAdded;
}
static void ensureTray(){
    if(!addTray())SetTimer(mainWindow,TRAY_RETRY_TIMER,2000,nullptr);
}
static void registerHotkey(){
    stopPriorityHook();
    if(hotkeyReady)UnregisterHotKey(mainWindow,1);
    hotkeyReady=RegisterHotKey(mainWindow,1,MOD_ALT|MOD_NOREPEAT,'A')!=FALSE;
    lastHotkeyError=hotkeyReady?0:GetLastError();
    if(!hotkeyReady && lastHotkeyError==ERROR_HOTKEY_ALREADY_REGISTERED && priorityEnabled)startPriorityHook();
    if(trayAdded){NOTIFYICONDATAW n={sizeof(n)};n.hWnd=mainWindow;n.uID=1;n.uFlags=NIF_TIP;
        wcscpy_s(n.szTip,shortcutReady()?(keyboardHook?L"轻截图 · Alt+A 优先响应":L"轻截图 · Alt+A 截图"):L"轻截图 · 快捷键不可用，右键重试");Shell_NotifyIconW(NIM_MODIFY,&n);}
}
static RECT normalized(POINT a,POINT b,int w,int h){
    RECT r={std::max<int>(0,std::min<int>(a.x,b.x)),std::max<int>(0,std::min<int>(a.y,b.y)),std::min<int>(w,std::max<int>(a.x,b.x)),std::min<int>(h,std::max<int>(a.y,b.y))};return r;
}
static bool validSelection(){return selected && selection.right>selection.left && selection.bottom>selection.top;}
static void fill(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
static void text(HDC dc,const std::wstring& value,RECT r,COLORREF color,UINT flags=DT_SINGLELINE|DT_VCENTER|DT_CENTER){
    HGDIOBJ old=SelectObject(dc,uiFont);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
    DrawTextW(dc,value.c_str(),-1,&r,flags|DT_NOPREFIX);SelectObject(dc,old);
}
static void layoutToolbar(){
    POINT p={desktop.left+selection.left,desktop.top+selection.bottom};HMONITOR mon=MonitorFromPoint(p,MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi={sizeof(mi)};GetMonitorInfoW(mon,&mi);RECT area=mi.rcMonitor;OffsetRect(&area,-desktop.left,-desktop.top);
    int w=3*S(112),h=S(42),gap=S(9);
    int x=std::max<int>(area.left+S(5),std::min<int>(selection.right-w,area.right-w-S(5)));
    int y=selection.bottom+gap;if(y+h>area.bottom-S(5))y=selection.top-h-gap;
    y=std::max<int>(area.top+S(5),std::min<int>(y,area.bottom-h-S(5)));
    toolbar={x,y,x+w,y+h};
    for(int i=0;i<3;i++)buttons[i]={x+i*S(112),y,x+(i+1)*S(112),y+h};
}
static void paintOverlay(HDC dc){
    if(!snapshot.dc)return;
    BitBlt(dc,0,0,snapshot.width,snapshot.height,shaded.dc,0,0,SRCCOPY);
    if(validSelection()){
        int w=selection.right-selection.left,h=selection.bottom-selection.top;
        BitBlt(dc,selection.left,selection.top,w,h,snapshot.dc,selection.left,selection.top,SRCCOPY);
        HPEN pen=CreatePen(PS_SOLID,S(2),RGB(63,222,183));HGDIOBJ oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
        Rectangle(dc,selection.left,selection.top,selection.right,selection.bottom);SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(pen);
        wchar_t size[64];swprintf_s(size,L"%d × %d",w,h);
        RECT label={selection.left,std::max<int>(0,selection.top-S(30)),selection.left+S(140),std::max<int>(S(28),selection.top)};
        fill(dc,label,RGB(24,30,42));text(dc,size,label,RGB(255,255,255));
        if(!dragging){
            layoutToolbar();fill(dc,toolbar,RGB(24,30,42));
            fill(dc,buttons[0],RGB(25,125,108));
            text(dc,L"复制  Enter",buttons[0],RGB(255,255,255));
            text(dc,L"保存  Ctrl+S",buttons[1],RGB(242,245,249));
            text(dc,L"取消  Esc",buttons[2],RGB(200,209,222));
        }
    } else {
        POINT cursor;GetCursorPos(&cursor);HMONITOR mon=MonitorFromPoint(cursor,MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi={sizeof(mi)};GetMonitorInfoW(mon,&mi);
        int x=mi.rcMonitor.left-desktop.left+(mi.rcMonitor.right-mi.rcMonitor.left-S(620))/2;
        int y=mi.rcMonitor.top-desktop.top+S(32);
        RECT hint={x,y,x+S(620),y+S(48)};fill(dc,hint,RGB(24,30,42));
        text(dc,L"拖动框选 · 双击 / 回车复制 · Ctrl+S 保存 · Esc 取消",hint,RGB(245,248,252));
    }
}
static HGLOBAL selectionDib(){
    if(!validSelection())return nullptr;
    int w=selection.right-selection.left,h=selection.bottom-selection.top;
    SIZE_T bytes=(SIZE_T)w*h*4;
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,sizeof(BITMAPINFOHEADER)+bytes);if(!memory)return nullptr;
    BYTE* data=(BYTE*)GlobalLock(memory);if(!data){GlobalFree(memory);return nullptr;}
    BITMAPINFOHEADER header={};header.biSize=sizeof(header);header.biWidth=w;header.biHeight=h;
    header.biPlanes=1;header.biBitCount=32;header.biCompression=BI_RGB;header.biSizeImage=(DWORD)bytes;
    memcpy(data,&header,sizeof(header));BYTE* output=data+sizeof(header);
    GdiFlush();
    for(int y=0;y<h;y++){
        const BYTE* row=(BYTE*)snapshot.bits+((SIZE_T)(selection.bottom-1-y)*snapshot.width+selection.left)*4;
        memcpy(output+(SIZE_T)y*w*4,row,(SIZE_T)w*4);
    }
    GlobalUnlock(memory);return memory;
}
static bool saveSelection(const wchar_t* path){
    if(!validSelection())return false;
    Surface crop;int w=selection.right-selection.left,h=selection.bottom-selection.top;
    if(!crop.create(w,h))return false;
    if(!BitBlt(crop.dc,0,0,w,h,snapshot.dc,selection.left,selection.top,SRCCOPY))return false;
    GdiFlush();Gdiplus::GdiplusStartupInput input;ULONG_PTR token;
    if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)return false;
    bool result=false;
    {
        // 32bpp RGB ignores the unused alpha bytes of a screen DIB.
        Gdiplus::Bitmap image(w,h,w*4,PixelFormat32bppRGB,(BYTE*)crop.bits);
        const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
        result=image.Save(path,&png,nullptr)==Gdiplus::Ok;
    }
    Gdiplus::GdiplusShutdown(token);return result;
}
static void endCapture(){
    if(overlay){HWND old=overlay;overlay=nullptr;DestroyWindow(old);}
    if(GetCapture())ReleaseCapture();
    snapshot.clear();shaded.clear();selected=dragging=pendingDrag=false;
}
static void copySelection(){
    HGLOBAL data=selectionDib();if(!data){errorBox(L"请先框选截图区域，或重新截图。");return;}
    bool opened=false;
    for(int i=0;i<6;i++){if(OpenClipboard(mainWindow)){opened=true;break;}Sleep(35);}
    if(!opened){GlobalFree(data);errorBox(L"剪贴板暂时被其他程序占用，请稍后再点复制。");return;}
    bool ok=EmptyClipboard() && SetClipboardData(CF_DIB,data)!=nullptr;
    CloseClipboard();if(!ok){GlobalFree(data);errorBox(L"复制失败，请再试一次，或使用 Ctrl+S 保存。");return;}
    endCapture();balloon(L"已复制截图",L"现在可以按 Ctrl+V 粘贴。");
}
struct SaveResult {DWORD accepted;DWORD error;wchar_t path[32768];};
static int choosePathWorker(const wchar_t* mappingName,HWND owner){
    HANDLE mapping=OpenFileMappingW(FILE_MAP_WRITE,FALSE,mappingName);if(!mapping)return 10;
    SaveResult* result=(SaveResult*)MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,sizeof(SaveResult));
    if(!result){CloseHandle(mapping);return 11;}
    SYSTEMTIME now;GetLocalTime(&now);wchar_t filename[32768];
    swprintf_s(filename,L"截图_%04d%02d%02d_%02d%02d%02d.png",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);
    OPENFILENAMEW ofn={sizeof(ofn)};ofn.hwndOwner=owner;ofn.lpstrFile=filename;ofn.nMaxFile=32768;
    ofn.lpstrFilter=L"PNG 图片 (*.png)\0*.png\0\0";ofn.lpstrDefExt=L"png";ofn.lpstrTitle=L"保存截图";
    ofn.Flags=OFN_EXPLORER|OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetSaveFileNameW(&ofn)){wcscpy_s(result->path,filename);result->accepted=1;}
    else result->error=CommDlgExtendedError();
    UnmapViewOfFile(result);CloseHandle(mapping);return 0;
}
static void saveDialog(){
    if(!validSelection() || saving)return;saving=true;
    std::wstring mappingName=L"Local\\LightCapture.Save."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SaveResult),mappingName.c_str());
    SaveResult* result=mapping?(SaveResult*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SaveResult)):nullptr;
    if(!result){if(mapping)CloseHandle(mapping);saving=false;errorBox(L"无法准备保存窗口，请重试。");return;}
    ZeroMemory(result,sizeof(SaveResult));
    std::wstring command=L"\""+exePath()+L"\" --save-dialog \""+mappingName+L"\" "+std::to_wstring((ULONG_PTR)overlay);
    STARTUPINFOW startup={sizeof(startup)};PROCESS_INFORMATION process={};
    bool started=CreateProcessW(exePath().c_str(),&command[0],nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)!=FALSE;
    DWORD exitCode=1;
    if(started){
        CloseHandle(process.hThread);saveWorkerProcess=process.hProcess;
        // Keep the owner responsive while the temporary process displays a modal dialog.
        bool done=false;
        while(!done){
            DWORD wait=MsgWaitForMultipleObjects(1,&process.hProcess,FALSE,INFINITE,QS_ALLINPUT);
            if(wait==WAIT_OBJECT_0)break;
            if(wait!=WAIT_OBJECT_0+1)break;
            MSG message;
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
                if(message.message==WM_QUIT){TerminateProcess(process.hProcess,12);PostQuitMessage((int)message.wParam);done=true;break;}
                TranslateMessage(&message);DispatchMessageW(&message);
            }
        }
        GetExitCodeProcess(process.hProcess,&exitCode);CloseHandle(process.hProcess);saveWorkerProcess=nullptr;
    }
    if(started && exitCode==0 && result->accepted && overlay){
        if(saveSelection(result->path)){endCapture();balloon(L"截图已保存",L"图片已保存到你选择的位置。");}
        else errorBox(L"无法保存图片，请检查目标文件夹是否可写。");
    } else if(!started || exitCode!=0 || result->error)errorBox(L"保存窗口打开失败，请重新尝试。");
    UnmapViewOfFile(result);CloseHandle(mapping);
    saving=false;
}
static LRESULT CALLBACK overlayProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    switch(message){
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);paintOverlay(dc);EndPaint(window,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_SETCURSOR:SetCursor(LoadCursorW(nullptr,IDC_CROSS));return TRUE;
    case WM_LBUTTONDOWN:{
        if(saving)return 0;POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if(validSelection() && !dragging){
            layoutToolbar();for(int i=0;i<3;i++)if(PtInRect(&buttons[i],p)){
                if(i==0)copySelection();else if(i==1)saveDialog();else endCapture();return 0;
            }
        }
        anchor=p;pendingDrag=true;SetCapture(window);
        if(!validSelection()){selection={p.x,p.y,p.x,p.y};dragging=true;selected=false;}
        InvalidateRect(window,nullptr,FALSE);return 0;
    }
    case WM_MOUSEMOVE:
        if(pendingDrag){POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(!dragging && abs(p.x-anchor.x)<GetSystemMetrics(SM_CXDRAG) && abs(p.y-anchor.y)<GetSystemMetrics(SM_CYDRAG))return 0;
            dragging=true;selection=normalized(anchor,p,snapshot.width,snapshot.height);
            selected=selection.right>selection.left && selection.bottom>selection.top;InvalidateRect(window,nullptr,FALSE);}return 0;
    case WM_LBUTTONUP:
        if(dragging){POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};selection=normalized(anchor,p,snapshot.width,snapshot.height);
            dragging=false;selected=selection.right>selection.left && selection.bottom>selection.top;InvalidateRect(window,nullptr,FALSE);}
        pendingDrag=false;ReleaseCapture();return 0;
    case WM_CAPTURECHANGED:pendingDrag=dragging=false;return 0;
    case WM_LBUTTONDBLCLK:
        if(validSelection()){POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(PtInRect(&selection,p))copySelection();}return 0;
    case WM_RBUTTONUP:if(!saving)endCapture();return 0;
    case WM_KEYDOWN:case WM_SYSKEYDOWN:
        if(saving)return 0;
        if(wp==VK_ESCAPE){endCapture();return 0;}
        if(wp==VK_RETURN && validSelection()){copySelection();return 0;}
        if(wp=='S' && GetKeyState(VK_CONTROL)<0){saveDialog();return 0;}
        return 0;
    case WM_CLOSE:if(!saving)endCapture();return 0;
    case WM_DISPLAYCHANGE:if(!saving)endCapture();return 0;
    case WM_DPICHANGED:return 0; // One pixel-aligned overlay spans the entire virtual desktop.
    }
    return DefWindowProcW(window,message,wp,lp);
}
static bool acquireDesktop(){
    desktop={GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),0,0};
    int w=GetSystemMetrics(SM_CXVIRTUALSCREEN),h=GetSystemMetrics(SM_CYVIRTUALSCREEN);desktop.right=desktop.left+w;desktop.bottom=desktop.top+h;
    if(!snapshot.create(w,h) || !shaded.create(w,h)){snapshot.clear();shaded.clear();return false;}
    HDC screen=GetDC(nullptr);bool ok=BitBlt(snapshot.dc,0,0,w,h,screen,desktop.left,desktop.top,SRCCOPY|CAPTUREBLT)!=FALSE;ReleaseDC(nullptr,screen);
    if(!ok){snapshot.clear();shaded.clear();return false;}
    // Produce one dimmed background per capture; idle mode retains no screen images.
    BitBlt(shaded.dc,0,0,w,h,snapshot.dc,0,0,SRCCOPY);
    HDC black=CreateCompatibleDC(nullptr);HBITMAP pixel=CreateCompatibleBitmap(shaded.dc,1,1);HGDIOBJ old=SelectObject(black,pixel);
    PatBlt(black,0,0,1,1,BLACKNESS);BLENDFUNCTION blend={AC_SRC_OVER,0,110,0};AlphaBlend(shaded.dc,0,0,w,h,black,0,0,1,1,blend);
    SelectObject(black,old);DeleteObject(pixel);DeleteDC(black);return true;
}
static void startCapture(){
    if(overlay){SetForegroundWindow(overlay);return;}
    if(!acquireDesktop()){errorBox(L"暂时无法读取桌面，请在解锁电脑后重试。");return;}
    selected=dragging=false;
    overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,OVERLAY_CLASS,L"轻截图 · 框选",WS_POPUP,
        desktop.left,desktop.top,snapshot.width,snapshot.height,nullptr,nullptr,instanceHandle,nullptr);
    if(!overlay){snapshot.clear();shaded.clear();errorBox(L"截图窗口创建失败。");return;}
    ShowWindow(overlay,SW_SHOW);SetForegroundWindow(overlay);SetFocus(overlay);UpdateWindow(overlay);
}
#include "ClipboardHistory.h"
static void showMenu(){
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,CMD_CAPTURE,L"立即截图\tAlt+A");
    std::wstring clipboardLabel=L"剪贴板历史\t"+std::wstring(clip::shortcutLabel());AppendMenuW(menu,MF_STRING,106,clipboardLabel.c_str());
    AppendMenuW(menu,MF_STRING|(autoStartEnabled()?MF_CHECKED:0),CMD_AUTOSTART,L"开机自启");
    AppendMenuW(menu,MF_STRING|(priorityEnabled?MF_CHECKED:0),CMD_PRIORITY,L"Alt+A 冲突时优先响应");
    AppendMenuW(menu,MF_STRING,CMD_RETRY,shortcutReady()?(keyboardHook?L"Alt+A 优先响应中（点击重试）":L"快捷键正常：Alt+A（点击重试）"):L"快捷键不可用：点击重试");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,CMD_HELP,L"使用说明");AppendMenuW(menu,MF_STRING,CMD_EXIT,L"退出");
    POINT p;GetCursorPos(&p);SetForegroundWindow(mainWindow);
    UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,mainWindow,nullptr);DestroyMenu(menu);
    PostMessageW(mainWindow,WM_NULL,0,0);if(command)PostMessageW(mainWindow,WM_COMMAND,command,0);
}
static LRESULT CALLBACK mainProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(taskbarCreated && message==taskbarCreated){trayAdded=false;trayRetryCount=0;startupLog("explorer-ready");ensureTray();return 0;}
    switch(message){
    case WM_HOTKEY:if(wp==1)startCapture();else if(wp==2)clip::show();return 0;
    case WM_CLIPBOARDUPDATE:clip::captureClipboard();return 0;
    case WM_APP+20:clip::show();return 0;
    case WM_APP+21:return clip::store?(LRESULT)clip::store->count():-1;
    case WM_APP+22:return clip::keyReady?1:0;
    case CAPTURE_MSG:startCapture();return 0;
    case QUIT_MSG:DestroyWindow(window);return 0;
    case WM_APP+5:return keyboardHook?2:(hotkeyReady?1:0);
    case WM_APP+6:return trayAdded?1:0;
    case WM_APP+7:return autoStartEnabled()?1:0;
    case WM_TIMER:
        if(wp==clip::RETRY_TIMER){KillTimer(window,clip::RETRY_TIMER);clip::captureClipboard();return 0;}
        if(wp==clip::SEARCH_TIMER){KillTimer(window,clip::SEARCH_TIMER);clip::refresh();return 0;}
        if(wp==TRAY_RETRY_TIMER && !trayAdded){
            if(!addTray() && ++trayRetryCount>=30){
                KillTimer(window,TRAY_RETRY_TIMER);startupLog("tray-retries-exhausted",5);processExitCode=5;DestroyWindow(window);
            }
        }return 0;
    case NOTICE_MSG:balloon(L"轻截图正在运行",shortcutReady()?L"按 Alt+A 即可截图，右键托盘图标可以管理自启。":L"快捷键不可用，请右键托盘图标重试，或点击托盘图标截图。");return 0;
    case TRAY_MSG:
        if(lp==WM_RBUTTONUP || lp==WM_CONTEXTMENU)showMenu();
        else if(lp==WM_LBUTTONUP)PostMessageW(window,CAPTURE_MSG,0,0);return 0;
    case WM_COMMAND:switch(LOWORD(wp)){
        case CMD_CAPTURE:startCapture();break;
        case 106:clip::show();break;
        case CMD_AUTOSTART:
            if(setAutoStart(!autoStartEnabled())!=ERROR_SUCCESS)errorBox(L"无法更改开机自启，请检查当前用户的任务计划程序权限。");
            break;
        case CMD_PRIORITY:{
            HKEY key;LONG result=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\LightCapture",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
            if(result==ERROR_SUCCESS){DWORD value=priorityEnabled?0:1;result=RegSetValueExW(key,L"PriorityAltA",0,REG_DWORD,(BYTE*)&value,sizeof(value));RegCloseKey(key);}
            if(result!=ERROR_SUCCESS){errorBox(L"设置保存失败，请检查当前用户的注册表写入权限。");break;}
            priorityEnabled=!priorityEnabled;registerHotkey();break;
        }
        case CMD_RETRY:registerHotkey();balloon(shortcutReady()?L"快捷键已就绪":L"快捷键不可用",shortcutReady()?L"现在按 Alt+A 即可截图。":L"可开启优先响应，或关闭其他截图软件的 Alt+A 快捷键后重试。",shortcutReady()?NIIF_INFO:NIIF_WARNING);break;
        case CMD_HELP:MessageBoxW(window,L"Alt+A：框选截图\n双击选区 / Enter / 点击复制：复制到剪贴板\nCtrl+S / 点击保存：保存 PNG\nEsc / 鼠标右键：取消截图\n重新拖动：重新框选\n\n右键托盘图标可开关自启或退出。\n关闭自启后再退出，即可删除整个软件文件夹。\n\n空闲时只等待系统消息，不轮询、不联网。\n截图时临时占用内存，结束后释放。",L"轻截图 · 使用说明",MB_OK|MB_ICONINFORMATION);break;
        case CMD_EXIT:DestroyWindow(window);break;
    }return 0;
    case WM_ENDSESSION:if(wp)DestroyWindow(window);return 0;
    case WM_DESTROY:
        clip::shutdown();
        KillTimer(window,TRAY_RETRY_TIMER);startupLog("exit",processExitCode);
        if(saveWorkerProcess)TerminateProcess(saveWorkerProcess,12);
        stopPriorityHook();endCapture();if(hotkeyReady)UnregisterHotKey(window,1);
        if(trayAdded){NOTIFYICONDATAW n={sizeof(n)};n.hWnd=window;n.uID=1;Shell_NotifyIconW(NIM_DELETE,&n);trayAdded=false;}
        PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static int selfTest(const wchar_t* reportPath){
    std::ofstream report(reportPath);int status=0;
    try{
        RECT r=normalized({200,150},{-20,-10},320,240);check(r.left==0 && r.top==0 && r.right==200 && r.bottom==150,"reverse and clamp selection");
        check(snapshot.create(320,240) && shaded.create(320,240),"DIB allocation");
        fill(snapshot.dc,{0,0,320,240},RGB(50,100,150));fill(snapshot.dc,{20,30,120,90},RGB(230,40,70));
        selection={20,30,120,90};selected=true;desktop={-320,-240,0,0};
        HGLOBAL dib=selectionDib();check(dib!=nullptr,"clipboard DIB extraction");
        BYTE* bytes=(BYTE*)GlobalLock(dib);BITMAPINFOHEADER* header=(BITMAPINFOHEADER*)bytes;
        check(header->biWidth==100 && header->biHeight==60 && header->biBitCount==32,"clipboard DIB dimensions");
        BYTE* color=bytes+sizeof(BITMAPINFOHEADER);check(color[0]==70 && color[1]==40 && color[2]==230,"clipboard DIB color and crop");
        GlobalUnlock(dib);GlobalFree(dib);
        std::wstring png=std::wstring(reportPath)+L".png";check(saveSelection(png.c_str()),"PNG encoding");
        {Gdiplus::GdiplusStartupInput input;ULONG_PTR token;check(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"GDI+ start");
            {Gdiplus::Bitmap image(png.c_str());check(image.GetLastStatus()==Gdiplus::Ok && image.GetWidth()==100 && image.GetHeight()==60,"PNG decode dimensions");
            Gdiplus::Color pixel;image.GetPixel(50,30,&pixel);check(pixel.GetR()==230 && pixel.GetG()==40 && pixel.GetB()==70 && pixel.GetA()==255,"PNG opaque correct pixels");}
            Gdiplus::GdiplusShutdown(token);}
        report<<"PASS reverse drag, bounds, negative-origin desktop, DIB crop/colors, PNG encoding/decode/opacity\n";
        selected=false;check(selectionDib()==nullptr,"reject empty selection");snapshot.clear();shaded.clear();
        DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        for(int i=0;i<100;i++){Surface test;check(test.create(1920,1080),"repeated screen allocation");}
        DWORD after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);check(after==before,"GDI resource leak");
        report<<"PASS 100 allocation/release cycles; GDI objects before="<<before<<" after="<<after<<"\n";
        mainWindow=CreateWindowExW(0,L"STATIC",L"LightCapture test",0,0,0,0,0,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(mainWindow!=nullptr,"test message target");
        KBDLLHOOKSTRUCT event={};event.vkCode='A';event.flags=LLKHF_ALTDOWN;
        check(keyboardProc(HC_ACTION,WM_SYSKEYDOWN,(LPARAM)&event)==1,"Alt+A event interception");
        MSG message;check(PeekMessageW(&message,mainWindow,CAPTURE_MSG,CAPTURE_MSG,PM_REMOVE)!=FALSE,"Alt+A posts capture action");
        check(keyboardProc(HC_ACTION,WM_SYSKEYDOWN,(LPARAM)&event)==1,"held key interception");
        check(!PeekMessageW(&message,mainWindow,CAPTURE_MSG,CAPTURE_MSG,PM_REMOVE),"suppress repeat capture");
        check(keyboardProc(HC_ACTION,WM_SYSKEYUP,(LPARAM)&event)==1 && !hookA,"key release");
        DestroyWindow(mainWindow);mainWindow=nullptr;
        report<<"PASS Alt+A event dispatch, held-key repeat suppression, matching key release\n";
        report<<"Clipboard itself was not modified. No screenshot was saved from the real desktop.\n";
    }catch(const std::exception& error){report<<"FAIL "<<error.what()<<"\n";status=1;}
    return status;
}
static int diagnose(const wchar_t* reportPath){
    std::ofstream report(reportPath);bool registered=RegisterHotKey(nullptr,2,MOD_ALT|MOD_NOREPEAT,'A')!=FALSE;
    DWORD error=registered?0:GetLastError();if(registered)UnregisterHotKey(nullptr,2);
    bool captured=acquireDesktop();report<<"Alt+A available: "<<(registered?"yes":"no")<<"; error="<<error<<"\n";
    report<<"Desktop capture: "<<(captured?"OK":"FAIL")<<"; origin="<<desktop.left<<","<<desktop.top<<"; size="<<snapshot.width<<"x"<<snapshot.height<<"\n";
    snapshot.clear();shaded.clear();report<<"Autostart entry matches executable: "<<(autoStartEnabled()?"yes":"no")<<"\n";
    return captured?0:1;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
    instanceHandle=instance;INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc==4 && !wcscmp(argv[1],L"--save-dialog")){
        int result=choosePathWorker(argv[2],(HWND)(ULONG_PTR)_wcstoui64(argv[3],nullptr,10));LocalFree(argv);return result;
    }
    bool background=false, enable=false, disable=false, quit=false, capture=false,configureOnly=false,clipboard=false;
    for(int i=1;i<argc;i++){
        if(!wcscmp(argv[i],L"--self-test") && i+1<argc){int r=selfTest(argv[i+1]);LocalFree(argv);return r;}
        if(!wcscmp(argv[i],L"--diagnose") && i+1<argc){int r=diagnose(argv[i+1]);LocalFree(argv);return r;}
        if(!wcscmp(argv[i],L"--background"))background=true;
        if(!wcscmp(argv[i],L"--enable-autostart"))enable=true;
        if(!wcscmp(argv[i],L"--disable-autostart"))disable=true;
        if(!wcscmp(argv[i],L"--quit"))quit=true;
        if(!wcscmp(argv[i],L"--capture"))capture=true;
        if(!wcscmp(argv[i],L"--clipboard"))clipboard=true;
        if(!wcscmp(argv[i],L"--clipboard-data-dir") && i+1<argc)clip::dataOverride=argv[++i];
        if(!wcscmp(argv[i],L"--configure-only"))configureOnly=true;
        if(!wcscmp(argv[i],L"--test-tray-failures") && i+1<argc)simulatedTrayFailures=_wtoi(argv[++i]);
    }
    LocalFree(argv);
    if(enable || disable){LONG result=setAutoStart(enable && !disable);
        startupLog(enable && !disable?"autostart-enable":"autostart-disable",(DWORD)result);
        if(result!=ERROR_SUCCESS){if(!configureOnly)errorBox(L"开机自启设置失败。错误代码："+std::to_wstring(result));return 2;}}
    if(configureOnly)return 0;
    if(quit){HWND existing=FindWindowW(MAIN_CLASS,nullptr);if(existing)PostMessageW(existing,QUIT_MSG,0,0);return 0;}
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\LightCapture.Desktop.v1");
    if(!mutex)return 3;
    if(GetLastError()==ERROR_ALREADY_EXISTS){HWND existing=FindWindowW(MAIN_CLASS,nullptr);if(existing && (capture || clipboard || !background))PostMessageW(existing,clipboard?clip::OPEN_MSG:(capture?CAPTURE_MSG:NOTICE_MSG),0,0);CloseHandle(mutex);return 0;}
    startupLog(background?"start-background":"start-manual");
    appIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));if(!appIcon)appIcon=LoadIconW(nullptr,IDI_APPLICATION);
    HDC screen=GetDC(nullptr);scale=GetDeviceCaps(screen,LOGPIXELSX)/96.0;ReleaseDC(nullptr,screen);
    uiFont=CreateFontW(-S(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    WNDCLASSEXW wc={sizeof(wc)};wc.lpfnWndProc=mainProc;wc.hInstance=instance;wc.lpszClassName=MAIN_CLASS;wc.hIcon=appIcon;
    if(!RegisterClassExW(&wc)){CloseHandle(mutex);return 4;}
    wc.lpfnWndProc=overlayProc;wc.lpszClassName=OVERLAY_CLASS;wc.style=CS_DBLCLKS;wc.hCursor=LoadCursorW(nullptr,IDC_CROSS);
    if(!RegisterClassExW(&wc)){DeleteObject(uiFont);CloseHandle(mutex);return 4;}
    mainWindow=CreateWindowExW(WS_EX_TOOLWINDOW,MAIN_CLASS,L"轻截图",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    if(!mainWindow){DeleteObject(uiFont);CloseHandle(mutex);return 4;}
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");readPriority();registerHotkey();
    ensureTray();
    clip::init();
    if(!shortcutReady())balloon(L"快捷键暂不可用",L"请右键托盘图标重试，或点击托盘图标截图。Alt+A 冲突时可开启优先响应。",NIIF_WARNING);
    else if(keyboardHook && !background)balloon(L"轻截图已启动 · Alt+A 优先响应",L"Alt+A 已被其他软件占用，轻截图将优先响应。可在托盘右键菜单关闭这一选项。");
    else if(!background)balloon(L"轻截图已启动",L"按 Alt+A 框选截图。右键托盘图标可以管理开机自启和退出。");
    if(capture)PostMessageW(mainWindow,CAPTURE_MSG,0,0);
    if(clipboard)PostMessageW(mainWindow,clip::OPEN_MSG,0,0);
    MSG message;BOOL result;while((result=GetMessageW(&message,nullptr,0,0))>0){if(!clip::translate(message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    DeleteObject(uiFont);CloseHandle(mutex);return result<0?6:processExitCode;
}
