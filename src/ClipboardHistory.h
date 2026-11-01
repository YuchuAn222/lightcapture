#pragma once
#include <memory>
#include <cwctype>
#include <commctrl.h>
#include "ClipboardStore.h"
#pragma comment(lib,"comctl32.lib")
namespace clip {
static std::unique_ptr<Store> store;
static HWND panel=nullptr,listbox=nullptr,search=nullptr,tabControl=nullptr,imageGrid=nullptr,starBox=nullptr,textPreview=nullptr,imagePreview=nullptr,status=nullptr,pauseButton=nullptr,pinButton=nullptr,detailTitle=nullptr,settingsWindow=nullptr,excludedEdit=nullptr,shortcutBox=nullptr;
static HIMAGELIST thumbnails=nullptr;
static int activeTab=0;
static bool favoritesOnly[2]={false,false},refreshing=false;
static std::wstring queries[2];
static long long selections[2]={0,0};
static HWND previousForeground=nullptr;
static HBITMAP previewBitmap=nullptr;
static std::vector<Item> rows;
static ULONG_PTR gdipToken=0;
static bool paused=false,listenReady=false,keyReady=false;
static int shortcutChoice=0,retryCount=0;
static std::wstring excludedApps,lastMessage,dataOverride;
static const wchar_t* PANEL_CLASS=L"LightCapture.Clipboard.v2";
static const UINT OPEN_MSG=WM_APP+20;
static const UINT_PTR RETRY_TIMER=20,SEARCH_TIMER=21;
static const UINT ID_SEARCH=201,ID_FILTER=202,ID_LIST=203,ID_PIN=204,ID_DELETE=205,ID_PAUSE=206,ID_SETTINGS=207,ID_CLEAR=208,ID_TABS=214,ID_STAR=215,ID_IMAGE_GRID=216,ID_IMAGE_SAVE=217;
static const wchar_t* shortcutLabel(){const wchar_t* keys[3]={L"Ctrl+Alt+V",L"Ctrl+Shift+V",L"Alt+V"};return keys[shortcutChoice];}
static std::vector<BYTE> pngBytes(Gdiplus::Bitmap& image){
 IStream* stream=nullptr;if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)))return {};
 const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
 std::vector<BYTE> output;
 if(image.Save(stream,&png,nullptr)==Gdiplus::Ok){HGLOBAL memory=nullptr;GetHGlobalFromStream(stream,&memory);STATSTG info={};
  if(SUCCEEDED(stream->Stat(&info,STATFLAG_NONAME)) && info.cbSize.QuadPart<=32*1024*1024){auto data=(BYTE*)GlobalLock(memory);if(data){output.assign(data,data+(size_t)info.cbSize.QuadPart);GlobalUnlock(memory);}}}
 stream->Release();return output;
}
static IStream* bytesStream(const std::vector<BYTE>& bytes){
 if(bytes.empty())return nullptr;HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,bytes.size());if(!mem)return nullptr;
 void* data=GlobalLock(mem);if(!data){GlobalFree(mem);return nullptr;}memcpy(data,bytes.data(),bytes.size());GlobalUnlock(mem);
 IStream* stream=nullptr;if(FAILED(CreateStreamOnHGlobal(mem,TRUE,&stream))){GlobalFree(mem);return nullptr;}return stream;
}
static std::unique_ptr<Gdiplus::Bitmap> scaleImage(Gdiplus::Bitmap& image,int maxWidth,int maxHeight){
 double factor=std::min(1.0,std::min((double)maxWidth/image.GetWidth(),(double)maxHeight/image.GetHeight()));
 int w=std::max(1,(int)(image.GetWidth()*factor)),h=std::max(1,(int)(image.GetHeight()*factor));
 auto result=std::make_unique<Gdiplus::Bitmap>(w,h,PixelFormat24bppRGB);
 Gdiplus::Graphics graphics(result.get());graphics.Clear(Gdiplus::Color(255,255,255,255));graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);graphics.DrawImage(&image,0,0,w,h);return result;
}
static std::wstring windowText(HWND w){int n=GetWindowTextLengthW(w);std::wstring value(n+1,L'\0');GetWindowTextW(w,&value[0],n+1);value.resize(n);return value;}
static Item* chosen(){int index=activeTab==0?(int)SendMessageW(listbox,LB_GETCURSEL,0,0):ListView_GetNextItem(imageGrid,-1,LVNI_SELECTED);return index>=0 && index<(int)rows.size()?&rows[index]:nullptr;}
static void updateStatus(){
 if(!panel || !status || !store)return;
 std::wstring label=(paused?L"记录已暂停":L"正在记录")+std::wstring(L"  ·  ")+(activeTab==0?L"文字":L"截图／图片")+L"当前 "+std::to_wstring(rows.size())+L" 条  ·  共 "+std::to_wstring(store->count())+L" 条  ·  "+std::to_wstring(store->bytes()/1024/1024)+L" / 300 MB";
 if(!keyReady)label+=L"  ·  快捷键被占用，可从托盘打开";
 if(!lastMessage.empty())label+=L"  ·  "+lastMessage;
 SetWindowTextW(status,label.c_str());SetWindowTextW(pauseButton,paused?L"继续记录":L"暂停记录");
}
static void showSelection(){
 if(previewBitmap){DeleteObject(previewBitmap);previewBitmap=nullptr;}
 Item* item=chosen();EnableWindow(GetDlgItem(panel,1),item!=nullptr);EnableWindow(pinButton,item!=nullptr);EnableWindow(GetDlgItem(panel,ID_DELETE),item!=nullptr);EnableWindow(GetDlgItem(panel,ID_IMAGE_SAVE),item!=nullptr && activeTab==1);
 ShowWindow(textPreview,SW_HIDE);ShowWindow(imagePreview,SW_HIDE);
 if(!item){SetWindowTextW(detailTitle,activeTab==0?L"文字历史":L"截图／图片历史");std::wstring empty;
  if(!windowText(search).empty())empty=L"没有匹配的内容，请换个关键词或清空搜索。";
  else if(favoritesOnly[activeTab])empty=L"这里还没有收藏内容。取消“只看收藏”后，选中一条并点击收藏。";
  else empty=activeTab==0?L"复制文字后，会自动保存在这一页。\r\n\r\n选择一条查看完整内容，双击重新复制。":L"Alt+A 截图并复制，或从其他应用复制图片后，会保存在这一页。\r\n\r\n点击缩略图查看大图，支持重新复制和保存 PNG。";
  SetWindowTextW(textPreview,empty.c_str());ShowWindow(textPreview,SW_SHOW);return;}
 SetWindowTextW(pinButton,item->pinned?L"取消收藏":L"收藏");
 std::wstring title=(item->kind==1?L"文字":L"图片")+std::wstring(item->pinned?L" · 已收藏":L"")+L"  ·  "+std::to_wstring(item->bytes/1024+1)+L" KB";
 SetWindowTextW(detailTitle,title.c_str());
 if(item->kind==1){SetWindowTextW(textPreview,store->text(item->id).c_str());ShowWindow(textPreview,SW_SHOW);}
 else {auto bytes=store->image(item->id);IStream* stream=bytesStream(bytes);if(stream){{Gdiplus::Bitmap image(stream);if(image.GetLastStatus()==Gdiplus::Ok){auto scaledPreview=scaleImage(image,1000,800);scaledPreview->GetHBITMAP(Gdiplus::Color(255,255,255,255),&previewBitmap);}}stream->Release();}ShowWindow(imagePreview,SW_SHOW);InvalidateRect(imagePreview,nullptr,TRUE);}
}
static void refresh(long long keep=0){
 if(!panel)return;
 try{
  if(!keep){auto current=chosen();if(current)keep=current->id;}
  rows=store->list(activeTab+1,windowText(search),favoritesOnly[activeTab]);refreshing=true;
  SendMessageW(listbox,WM_SETREDRAW,FALSE,0);SendMessageW(imageGrid,WM_SETREDRAW,FALSE,0);SendMessageW(listbox,LB_RESETCONTENT,0,0);ListView_DeleteAllItems(imageGrid);
  ListView_SetImageList(imageGrid,nullptr,LVSIL_NORMAL);if(thumbnails){ImageList_Destroy(thumbnails);thumbnails=nullptr;}
  int selectedIndex=0;
  if(activeTab==1){thumbnails=ImageList_Create(S(112),S(80),ILC_COLOR32,1,16);ListView_SetImageList(imageGrid,thumbnails,LVSIL_NORMAL);ListView_SetIconSpacing(imageGrid,S(148),S(136));}
  for(size_t i=0;i<rows.size();++i){
   if(activeTab==0)SendMessageW(listbox,LB_ADDSTRING,0,(LPARAM)rows[i].summary.c_str());
   else {
    int imageIndex=-1;IStream* stream=bytesStream(rows[i].thumb);
    if(stream){{Gdiplus::Bitmap source(stream);if(source.GetLastStatus()==Gdiplus::Ok){Gdiplus::Bitmap canvas(S(112),S(80),PixelFormat32bppARGB);
     {Gdiplus::Graphics g(&canvas);g.Clear(Gdiplus::Color(255,255,255,255));double fit=std::min((double)S(112)/source.GetWidth(),(double)S(80)/source.GetHeight());int width=(int)(source.GetWidth()*fit),height=(int)(source.GetHeight()*fit);g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);g.DrawImage(&source,(S(112)-width)/2,(S(80)-height)/2,width,height);}
     HBITMAP bitmap=nullptr;canvas.GetHBITMAP(Gdiplus::Color(255,255,255,255),&bitmap);if(bitmap){imageIndex=ImageList_Add(thumbnails,bitmap,nullptr);DeleteObject(bitmap);}}}stream->Release();}
    std::wstring title=rows[i].summary+(rows[i].pinned?L"\n已收藏":L"");LVITEMW item={};item.mask=LVIF_TEXT|LVIF_IMAGE;item.iItem=(int)i;item.pszText=&title[0];item.iImage=imageIndex;ListView_InsertItem(imageGrid,&item);
   }
   if(rows[i].id==keep)selectedIndex=(int)i;
  }
  if(!rows.empty()){if(activeTab==0)SendMessageW(listbox,LB_SETCURSEL,selectedIndex,0);else ListView_SetItemState(imageGrid,selectedIndex,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}
  SendMessageW(listbox,WM_SETREDRAW,TRUE,0);SendMessageW(imageGrid,WM_SETREDRAW,TRUE,0);refreshing=false;InvalidateRect(listbox,nullptr,TRUE);InvalidateRect(imageGrid,nullptr,TRUE);showSelection();updateStatus();
 }catch(...){refreshing=false;SendMessageW(listbox,WM_SETREDRAW,TRUE,0);SendMessageW(imageGrid,WM_SETREDRAW,TRUE,0);lastMessage=L"读取历史失败，请检查本机存储空间";updateStatus();}
}
static bool excludedOwner(){
 if(excludedApps.empty())return false;DWORD pid=0;GetWindowThreadProcessId(GetClipboardOwner(),&pid);if(!pid)return false;
 HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return false;
 wchar_t path[32768];DWORD size=32768;BOOL ok=QueryFullProcessImageNameW(process,0,path,&size);CloseHandle(process);if(!ok)return false;
 std::wstring exe=path;exe=exe.substr(exe.find_last_of(L"\\/")+1);std::transform(exe.begin(),exe.end(),exe.begin(),towlower);
 std::wstring configured=excludedApps;std::transform(configured.begin(),configured.end(),configured.begin(),towlower);
 for(wchar_t& ch:configured)if(ch==L'\r'||ch==L'\n'||ch==L','||ch==L' ')ch=L';';
 return (L";"+configured+L";").find(L";"+exe+L";")!=std::wstring::npos;
}
static bool noHistoryFlag(){
 if(IsClipboardFormatAvailable(RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing")))return true;
 UINT format=RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
 if(IsClipboardFormatAvailable(format)){HANDLE h=GetClipboardData(format);if(h && GlobalSize(h)>=4){auto value=(DWORD*)GlobalLock(h);if(value){bool excluded=*value==0;GlobalUnlock(h);if(excluded)return true;}}}
 return false;
}
static HBITMAP readDib(UINT format){
 HANDLE h=GetClipboardData(format);SIZE_T size=h?GlobalSize(h):0;if(size<sizeof(BITMAPINFOHEADER))return nullptr;
 auto data=(BYTE*)GlobalLock(h);if(!data)return nullptr;auto header=(BITMAPINFOHEADER*)data;HBITMAP result=nullptr;
 if(header->biSize>=40 && header->biSize<=124 && header->biWidth>0 && header->biHeight!=0 && header->biHeight!=LONG_MIN && header->biPlanes==1 &&
    (header->biBitCount==1 || header->biBitCount==4 || header->biBitCount==8 || header->biBitCount==16 || header->biBitCount==24 || header->biBitCount==32) &&
    (header->biCompression==BI_RGB || header->biCompression==BI_BITFIELDS) && (long long)header->biWidth*abs(header->biHeight)<=16000000){
  size_t colors=header->biClrUsed?header->biClrUsed:(header->biBitCount<=8?(1u<<header->biBitCount):0);
  size_t offset=header->biSize+colors*4+(header->biSize==40 && header->biCompression==BI_BITFIELDS?12:0);
  size_t stride=((size_t)header->biWidth*header->biBitCount+31)/32*4;
  if(offset<=size && stride*(size_t)abs(header->biHeight)<=size-offset){HDC dc=GetDC(nullptr);result=CreateDIBitmap(dc,header,CBM_INIT,data+offset,(BITMAPINFO*)data,DIB_RGB_COLORS);ReleaseDC(nullptr,dc);}
 }
 GlobalUnlock(h);return result;
}
static void captureClipboard(){
 if(!store || paused)return;
 if(!OpenClipboard(mainWindow)){
  if(retryCount++<5)SetTimer(mainWindow,RETRY_TIMER,120,nullptr);else {lastMessage=L"剪贴板忙，本次未记录";retryCount=0;updateStatus();}return;
 }
 KillTimer(mainWindow,RETRY_TIMER);retryCount=0;
 std::wstring value;HBITMAP bitmap=nullptr;std::vector<BYTE> rawPng;bool ignored=noHistoryFlag() || excludedOwner();
 if(!ignored){
  if(IsClipboardFormatAvailable(CF_DIBV5) || IsClipboardFormatAvailable(CF_DIB))bitmap=readDib(IsClipboardFormatAvailable(CF_DIBV5)?CF_DIBV5:CF_DIB);
  else if(IsClipboardFormatAvailable(CF_BITMAP)){HBITMAP source=(HBITMAP)GetClipboardData(CF_BITMAP);if(source){BITMAP info={};GetObjectW(source,sizeof(info),&info);if(info.bmWidth>0 && info.bmHeight>0 && (long long)info.bmWidth*info.bmHeight<=16000000)bitmap=(HBITMAP)CopyImage(source,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);}}
  else if(IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG"))){HANDLE h=GetClipboardData(RegisterClipboardFormatW(L"PNG"));SIZE_T bytes=h?GlobalSize(h):0;if(bytes && bytes<=32*1024*1024){auto data=(BYTE*)GlobalLock(h);if(data){rawPng.assign(data,data+bytes);GlobalUnlock(h);}}}
  else if(IsClipboardFormatAvailable(CF_UNICODETEXT)){HANDLE h=GetClipboardData(CF_UNICODETEXT);SIZE_T bytes=h?GlobalSize(h):0;if(bytes && bytes<=2*1024*1024){auto data=(const wchar_t*)GlobalLock(h);if(data){size_t n=wcsnlen_s(data,bytes/2);if(n<bytes/2)value.assign(data,n);GlobalUnlock(h);}}}
 }
 CloseClipboard();if(ignored)return;
 if(!rawPng.empty()){
  auto read32=[&](size_t p){return ((unsigned long long)rawPng[p]<<24)|((unsigned long long)rawPng[p+1]<<16)|((unsigned long long)rawPng[p+2]<<8)|rawPng[p+3];};
  const BYTE signature[8]={137,80,78,71,13,10,26,10};
  if(rawPng.size()<24 || memcmp(rawPng.data(),signature,8)!=0 || read32(16)==0 || read32(20)==0 || read32(16)*read32(20)>16000000){lastMessage=L"图片尺寸过大或格式不支持，本次未记录";updateStatus();return;}
 }
 try{
  bool stored=false;
  if(!value.empty()){
   std::wstring summary=value.substr(0,140);for(auto& ch:summary)if(ch==L'\r'||ch==L'\n'||ch==L'\t')ch=L' ';
   stored=store->add(1,value,{},{},summary);
  }else if(bitmap || !rawPng.empty()){
   IStream* stream=rawPng.empty()?nullptr:bytesStream(rawPng);
   {std::unique_ptr<Gdiplus::Bitmap> image=bitmap?std::make_unique<Gdiplus::Bitmap>(bitmap,nullptr):std::make_unique<Gdiplus::Bitmap>(stream);
    if(image->GetLastStatus()==Gdiplus::Ok && image->GetWidth() && image->GetHeight() && (long long)image->GetWidth()*image->GetHeight()<=16000000){
     auto normalized=scaleImage(*image,(int)image->GetWidth(),(int)image->GetHeight());auto thumbnail=scaleImage(*image,112,72);
     auto encoded=pngBytes(*normalized);
     stored=store->add(2,L"",encoded,pngBytes(*thumbnail),L"图片  "+std::to_wstring(image->GetWidth())+L" × "+std::to_wstring(image->GetHeight()));}}
   if(stream)stream->Release();
  }
  if(stored){lastMessage.clear();refresh();}
  else if(!value.empty() || bitmap || !rawPng.empty()){lastMessage=L"本次未保存：内容过大或收藏已占满空间";updateStatus();}
 }catch(...){lastMessage=L"保存失败：请检查存储空间或清理历史";updateStatus();}
 if(bitmap)DeleteObject(bitmap);
}
static bool setShortcut(int choice){
 UINT modifiers[3]={MOD_CONTROL|MOD_ALT,MOD_CONTROL|MOD_SHIFT,MOD_ALT};
 UnregisterHotKey(mainWindow,2);keyReady=RegisterHotKey(mainWindow,2,modifiers[choice]|MOD_NOREPEAT,'V')!=FALSE;
 if(keyReady)shortcutChoice=choice;return keyReady;
}
static void copyChosen(bool closeAfter){
 Item* item=chosen();if(!item)return;HGLOBAL memory=nullptr;UINT format=CF_UNICODETEXT;
 if(item->kind==1){auto value=store->text(item->id);size_t size=(value.size()+1)*2;memory=GlobalAlloc(GMEM_MOVEABLE,size);if(memory){auto data=GlobalLock(memory);if(data){memcpy(data,value.c_str(),size);GlobalUnlock(memory);}else{GlobalFree(memory);memory=nullptr;}}}
 else{
  auto bytes=store->image(item->id);IStream* stream=bytesStream(bytes);if(stream){
   {Gdiplus::Bitmap image(stream);int w=(int)image.GetWidth(),h=(int)image.GetHeight();
    if(image.GetLastStatus()==Gdiplus::Ok && w>0 && h>0 && (long long)w*h<=16000000){
     BITMAPINFOHEADER header={};header.biSize=sizeof(header);header.biWidth=w;header.biHeight=-h;header.biPlanes=1;header.biBitCount=32;header.biCompression=BI_RGB;
     size_t size=sizeof(header)+(size_t)w*h*4;memory=GlobalAlloc(GMEM_MOVEABLE,size);if(memory){BYTE* data=(BYTE*)GlobalLock(memory);if(data){memcpy(data,&header,sizeof(header));Gdiplus::Bitmap target(w,h,w*4,PixelFormat32bppRGB,data+sizeof(header));Gdiplus::Graphics graphics(&target);graphics.DrawImage(&image,0,0,w,h);graphics.Flush(Gdiplus::FlushIntentionSync);GlobalUnlock(memory);}else{GlobalFree(memory);memory=nullptr;}}}}
   stream->Release();}format=CF_DIB;
 }
 if(!memory){lastMessage=L"内容读取失败";updateStatus();return;}
 if(!OpenClipboard(mainWindow)){GlobalFree(memory);lastMessage=L"剪贴板忙，请再试一次";updateStatus();return;}
 bool ok=EmptyClipboard() && SetClipboardData(format,memory)!=nullptr;CloseClipboard();if(!ok)GlobalFree(memory);
 lastMessage=ok?L"已复制，按 Ctrl+V 粘贴":L"复制失败，请重试";updateStatus();
 if(ok && closeAfter)PostMessageW(panel,WM_CLOSE,0,0);
}
static bool exportPng(const std::vector<BYTE>& bytes,const std::wstring& target){
 if(bytes.empty())return false;
 std::wstring temp=target+L".LightCapture-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".tmp";
 HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);bool ok=false;
 if(file!=INVALID_HANDLE_VALUE){DWORD written=0;ok=WriteFile(file,bytes.data(),(DWORD)bytes.size(),&written,nullptr) && written==bytes.size() && FlushFileBuffers(file);CloseHandle(file);
  if(ok)ok=MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;if(!ok)DeleteFileW(temp.c_str());}
 return ok;
}
static void saveChosenImage(){
 Item* item=chosen();if(!item || item->kind!=2 || saveWorkerProcess)return;
 auto bytes=store->image(item->id);if(bytes.empty())return;
 std::wstring name=L"Local\\LightCapture.ImageSave."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
 HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SaveResult),name.c_str());
 auto result=mapping?(SaveResult*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SaveResult)):nullptr;
 if(!result){if(mapping)CloseHandle(mapping);lastMessage=L"无法打开保存窗口";updateStatus();return;}
 ZeroMemory(result,sizeof(*result));std::wstring command=L"\""+exePath()+L"\" --save-dialog \""+name+L"\" "+std::to_wstring((ULONG_PTR)panel);
 STARTUPINFOW startup={sizeof(startup)};PROCESS_INFORMATION process={};DWORD exit=1;
 bool started=CreateProcessW(exePath().c_str(),&command[0],nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)!=FALSE;
 if(started){CloseHandle(process.hThread);saveWorkerProcess=process.hProcess;bool done=false;
  while(!done){DWORD wait=MsgWaitForMultipleObjects(1,&process.hProcess,FALSE,INFINITE,QS_ALLINPUT);if(wait==WAIT_OBJECT_0)break;if(wait!=WAIT_OBJECT_0+1)break;
   MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){TerminateProcess(process.hProcess,12);PostQuitMessage((int)message.wParam);done=true;break;}TranslateMessage(&message);DispatchMessageW(&message);}}
  GetExitCodeProcess(process.hProcess,&exit);CloseHandle(process.hProcess);saveWorkerProcess=nullptr;
 }
 if(started && exit==0 && result->accepted){
  bool ok=exportPng(bytes,result->path);
  lastMessage=ok?L"PNG 已保存":L"保存失败，请检查目标文件夹";
 }else if(!started || exit!=0 || result->error)lastMessage=L"保存窗口未能完成，请重试";
 UnmapViewOfFile(result);CloseHandle(mapping);updateStatus();
}
static LRESULT CALLBACK imageProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
 if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT r;GetClientRect(w,&r);fill(dc,r,RGB(248,249,250));
  if(previewBitmap){Gdiplus::Bitmap image(previewBitmap,nullptr);Gdiplus::Graphics g(dc);double f=std::min(1.0,std::min((double)(r.right-24)/image.GetWidth(),(double)(r.bottom-24)/image.GetHeight()));int width=(int)(image.GetWidth()*f),height=(int)(image.GetHeight()*f);g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);g.DrawImage(&image,(r.right-width)/2,(r.bottom-height)/2,width,height);}EndPaint(w,&ps);return 0;}
 return DefWindowProcW(w,m,wp,lp);
}
static HWND control(HWND parent,const wchar_t* cls,const wchar_t* label,DWORD style,int id){
 HWND w=CreateWindowExW((wcscmp(cls,L"EDIT")==0)?WS_EX_CLIENTEDGE:0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,10,10,parent,(HMENU)(INT_PTR)id,instanceHandle,nullptr);SendMessageW(w,WM_SETFONT,(WPARAM)uiFont,TRUE);return w;
}
static void layout(){
 if(!panel)return;RECT r;GetClientRect(panel,&r);int w=r.right,h=r.bottom,g=S(20),left=S(activeTab==0?330:400),head=S(110),tools=S(42),foot=S(32);
 MoveWindow(tabControl,g,S(16),S(360),S(36),TRUE);
 MoveWindow(search,g,S(66),left-S(128),S(30),TRUE);MoveWindow(starBox,g+left-S(120),S(66),S(120),S(30),TRUE);
 MoveWindow(pauseButton,w-S(220),S(18),S(96),S(30),TRUE);MoveWindow(GetDlgItem(panel,ID_SETTINGS),w-S(112),S(18),S(92),S(30),TRUE);
 MoveWindow(listbox,g,head,left,h-head-foot-g,TRUE);
 MoveWindow(imageGrid,g,head,left,h-head-foot-g,TRUE);ShowWindow(listbox,activeTab==0?SW_SHOW:SW_HIDE);ShowWindow(imageGrid,activeTab==1?SW_SHOW:SW_HIDE);
 int x=g+left+g,available=w-x-g;
 MoveWindow(detailTitle,x,head,available-(activeTab==1?S(104):0),S(28),TRUE);MoveWindow(GetDlgItem(panel,ID_IMAGE_SAVE),x+available-S(96),head,S(96),S(28),TRUE);ShowWindow(GetDlgItem(panel,ID_IMAGE_SAVE),activeTab==1?SW_SHOW:SW_HIDE);
 MoveWindow(GetDlgItem(panel,1),x,head+S(36),S(100),S(30),TRUE);MoveWindow(pinButton,x+S(112),head+S(36),S(100),S(30),TRUE);MoveWindow(GetDlgItem(panel,ID_DELETE),x+S(224),head+S(36),S(80),S(30),TRUE);
 MoveWindow(textPreview,x,head+tools+S(36),available,h-head-tools-S(36)-foot-g,TRUE);MoveWindow(imagePreview,x,head+tools+S(36),available,h-head-tools-S(36)-foot-g,TRUE);
 RECT textArea;GetClientRect(textPreview,&textArea);InflateRect(&textArea,-S(8),-S(6));SendMessageW(textPreview,EM_SETRECT,0,(LPARAM)&textArea);
 MoveWindow(status,g,h-foot,w-2*g,foot-S(4),TRUE);InvalidateRect(panel,nullptr,TRUE);
}
static void switchTab(){
 int next=TabCtrl_GetCurSel(tabControl);if(next<0 || next>1 || next==activeTab)return;
 queries[activeTab]=windowText(search);auto current=chosen();if(current)selections[activeTab]=current->id;
 activeTab=next;store->setSetting(L"active_tab",activeTab==0?L"0":L"1");
 SendMessageW(starBox,BM_SETCHECK,favoritesOnly[activeTab]?BST_CHECKED:BST_UNCHECKED,0);SetWindowTextW(search,queries[activeTab].c_str());SendMessageW(search,EM_SETCUEBANNER,TRUE,(LPARAM)(activeTab==0?L"搜索复制过的文字…":L"搜索图片尺寸…"));
 KillTimer(mainWindow,SEARCH_TIMER);lastMessage.clear();layout();refresh(selections[activeTab]?selections[activeTab]:-1);
}
static LRESULT CALLBACK settingsProcImpl(HWND w,UINT m,WPARAM wp,LPARAM lp){
 if(m==WM_COMMAND){
  if(LOWORD(wp)==1){int previous=shortcutChoice,choice=(int)SendMessageW(shortcutBox,CB_GETCURSEL,0,0);
   if(!setShortcut(choice)){setShortcut(previous);MessageBoxW(w,L"这个快捷键已被占用，请选择另一个。",L"剪贴板设置",MB_OK|MB_ICONINFORMATION);return 0;}
   excludedApps=windowText(excludedEdit);store->setSetting(L"excluded",excludedApps.c_str());store->setSetting(L"shortcut",std::to_wstring(shortcutChoice).c_str());DestroyWindow(w);return 0;
  }if(LOWORD(wp)==2){DestroyWindow(w);return 0;}
  if(LOWORD(wp)==ID_CLEAR){int answer=MessageBoxW(w,L"是：清空全部（包括收藏）\n否：仅清空未收藏内容\n取消：保留所有历史",L"清空剪贴板历史",MB_YESNOCANCEL|MB_ICONWARNING);
   if(answer!=IDCANCEL){store->clear(answer==IDYES);lastMessage=L"历史已清理";refresh();}return 0;}
 }
 if(m==WM_CLOSE){DestroyWindow(w);return 0;}
 if(m==WM_DESTROY){settingsWindow=nullptr;EnableWindow(panel,TRUE);SetForegroundWindow(panel);updateStatus();return 0;}
 return DefWindowProcW(w,m,wp,lp);
}
static LRESULT CALLBACK settingsProc(HWND w,UINT m,WPARAM wp,LPARAM lp){try{return settingsProcImpl(w,m,wp,lp);}catch(...){MessageBoxW(w,L"设置未能保存，请检查存储空间后重试。",L"剪贴板设置",MB_OK|MB_ICONWARNING);return 0;}}
static void showSettings(){
 if(settingsWindow){SetForegroundWindow(settingsWindow);return;}
 RECT parent;GetWindowRect(panel,&parent);settingsWindow=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LightCapture.ClipboardSettings",L"剪贴板设置",WS_CAPTION|WS_SYSMENU,parent.left+S(100),parent.top+S(60),S(550),S(425),panel,nullptr,instanceHandle,nullptr);
 control(settingsWindow,L"STATIC",L"打开历史的快捷键",0,0);MoveWindow(GetWindow(settingsWindow,GW_CHILD),S(20),S(18),S(200),S(24),TRUE);
 shortcutBox=control(settingsWindow,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,301);MoveWindow(shortcutBox,S(20),S(48),S(300),S(150),TRUE);
 for(auto label:{L"Ctrl + Alt + V",L"Ctrl + Shift + V",L"Alt + V"})SendMessageW(shortcutBox,CB_ADDSTRING,0,(LPARAM)label);SendMessageW(shortcutBox,CB_SETCURSEL,shortcutChoice,0);
 HWND label=control(settingsWindow,L"STATIC",L"不记录这些应用（进程名，每行一个，例如 KeePass.exe）",0,0);MoveWindow(label,S(20),S(92),S(510),S(26),TRUE);
 excludedEdit=control(settingsWindow,L"EDIT",excludedApps.c_str(),ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,302);MoveWindow(excludedEdit,S(20),S(122),S(490),S(108),TRUE);
 label=control(settingsWindow,L"STATIC",L"最多 500 条 / 300 MB；收藏不自动删除。仅在本机保存。",0,0);MoveWindow(label,S(20),S(244),S(500),S(26),TRUE);
 HWND clear=control(settingsWindow,L"BUTTON",L"清空历史…",WS_TABSTOP,ID_CLEAR);MoveWindow(clear,S(20),S(298),S(112),S(32),TRUE);
 HWND save=control(settingsWindow,L"BUTTON",L"保存",BS_DEFPUSHBUTTON|WS_TABSTOP,1);MoveWindow(save,S(306),S(298),S(96),S(32),TRUE);
 HWND cancel=control(settingsWindow,L"BUTTON",L"取消",WS_TABSTOP,2);MoveWindow(cancel,S(414),S(298),S(96),S(32),TRUE);
 EnableWindow(panel,FALSE);ShowWindow(settingsWindow,SW_SHOW);SetForegroundWindow(settingsWindow);
}
static LRESULT CALLBACK panelProcImpl(HWND w,UINT m,WPARAM wp,LPARAM lp){
 switch(m){
 case WM_SIZE:layout();return 0;
 case WM_GETMINMAXINFO:{auto info=(MINMAXINFO*)lp;info->ptMinTrackSize={S(820),S(500)};return 0;}
 case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetTextColor((HDC)wp,RGB(32,37,42));SetBkColor((HDC)wp,RGB(255,255,255));return (LRESULT)GetStockObject(WHITE_BRUSH);
 case WM_DRAWITEM:{auto d=(DRAWITEMSTRUCT*)lp;if(d->CtlID!=ID_LIST || d->itemID>=rows.size())return 0;auto& item=rows[d->itemID];RECT r=d->rcItem;fill(d->hDC,r,(d->itemState&ODS_SELECTED)?RGB(232,238,237):RGB(255,255,255));
  int x=r.left+S(12);if(item.kind==2 && !item.thumb.empty()){IStream* stream=bytesStream(item.thumb);if(stream){{Gdiplus::Bitmap image(stream);Gdiplus::Graphics g(d->hDC);g.DrawImage(&image,x,r.top+S(10),S(60),S(42));}stream->Release();}x+=S(72);}
  RECT title={x,r.top+S(10),r.right-S(12),r.top+S(35)};text(d->hDC,item.summary,title,RGB(30,36,40),DT_LEFT|DT_END_ELLIPSIS|DT_SINGLELINE);
  FILETIME utc={(DWORD)item.stamp,(DWORD)((unsigned long long)item.stamp>>32)},local;SYSTEMTIME date;FileTimeToLocalFileTime(&utc,&local);FileTimeToSystemTime(&local,&date);wchar_t when[80];swprintf_s(when,L"%02u/%02u %02u:%02u   %s",date.wMonth,date.wDay,date.wHour,date.wMinute,item.pinned?L"已收藏":(item.kind==1?L"文字":L"图片"));RECT meta={x,r.top+S(38),r.right-S(12),r.bottom-S(8)};text(d->hDC,when,meta,RGB(94,102,108),DT_LEFT|DT_SINGLELINE);if(d->itemState&ODS_FOCUS)DrawFocusRect(d->hDC,&r);return TRUE;}
 case WM_MEASUREITEM:((MEASUREITEMSTRUCT*)lp)->itemHeight=S(72);return TRUE;
 case WM_NOTIFY:{auto info=(NMHDR*)lp;
  if(info->idFrom==ID_TABS && info->code==TCN_SELCHANGE){switchTab();return 0;}
  if(info->idFrom==ID_IMAGE_GRID && panel && !refreshing){if(info->code==LVN_ITEMCHANGED)showSelection();else if(info->code==NM_DBLCLK)copyChosen(true);else if(info->code==LVN_KEYDOWN && ((NMLVKEYDOWN*)lp)->wVKey==VK_RETURN)copyChosen(true);return 0;}
  break;}
 case WM_COMMAND:
  switch(LOWORD(wp)){
  case ID_SEARCH:if(HIWORD(wp)==EN_CHANGE)SetTimer(mainWindow,SEARCH_TIMER,180,nullptr);return 0;
  case ID_STAR:favoritesOnly[activeTab]=SendMessageW(starBox,BM_GETCHECK,0,0)==BST_CHECKED;store->setSetting(activeTab==0?L"favorites_text":L"favorites_images",favoritesOnly[activeTab]?L"1":L"0");refresh();return 0;
  case ID_LIST:if(HIWORD(wp)==LBN_SELCHANGE)showSelection();else if(HIWORD(wp)==LBN_DBLCLK)copyChosen(true);return 0;
  case 1:copyChosen(false);return 0;
  case 2:PostMessageW(w,WM_CLOSE,0,0);return 0;
  case ID_PIN:{auto i=chosen();if(i){long long id=i->id;store->pin(id,!i->pinned);refresh(id);}return 0;}
  case ID_DELETE:{auto i=chosen();if(i){store->erase(i->id);refresh();}return 0;}
  case ID_PAUSE:paused=!paused;store->setSetting(L"paused",paused?L"1":L"0");lastMessage.clear();updateStatus();return 0;
  case ID_SETTINGS:showSettings();return 0;
  case ID_IMAGE_SAVE:saveChosenImage();return 0;
  }break;
 case WM_CLOSE:if(settingsWindow)DestroyWindow(settingsWindow);DestroyWindow(w);return 0;
 case WM_DESTROY:panel=nullptr;if(thumbnails){ListView_SetImageList(imageGrid,nullptr,LVSIL_NORMAL);ImageList_Destroy(thumbnails);thumbnails=nullptr;}rows.clear();rows.shrink_to_fit();if(previewBitmap){DeleteObject(previewBitmap);previewBitmap=nullptr;}if(previousForeground && IsWindow(previousForeground))SetForegroundWindow(previousForeground);return 0;
 }
 return DefWindowProcW(w,m,wp,lp);
}
static LRESULT CALLBACK panelProc(HWND w,UINT m,WPARAM wp,LPARAM lp){try{return panelProcImpl(w,m,wp,lp);}catch(...){MessageBoxW(w,L"操作未完成，请检查本机存储空间后重试。",L"剪贴板历史",MB_OK|MB_ICONWARNING);return 0;}}
static void show(){
 if(!store){errorBox(L"剪贴板历史未能打开，请检查本机存储空间。");return;}
 if(panel){ShowWindow(panel,SW_RESTORE);SetForegroundWindow(settingsWindow?settingsWindow:panel);return;}
 previousForeground=GetForegroundWindow();POINT cursor;GetCursorPos(&cursor);MONITORINFO mi={sizeof(mi)};GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&mi);
 int width=S(980),height=S(650);panel=CreateWindowExW(WS_EX_CONTROLPARENT,PANEL_CLASS,L"轻截图 · 剪贴板历史",WS_OVERLAPPEDWINDOW,mi.rcWork.left+(mi.rcWork.right-mi.rcWork.left-width)/2,mi.rcWork.top+(mi.rcWork.bottom-mi.rcWork.top-height)/2,width,height,nullptr,nullptr,instanceHandle,nullptr);
 search=control(panel,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,ID_SEARCH);SendMessageW(search,EM_SETCUEBANNER,TRUE,(LPARAM)L"搜索复制过的文字…");
 tabControl=control(panel,WC_TABCONTROL,L"",WS_TABSTOP|TCS_FOCUSNEVER,ID_TABS);TCITEMW tab={};tab.mask=TCIF_TEXT;tab.pszText=(LPWSTR)L"文字";TabCtrl_InsertItem(tabControl,0,&tab);tab.pszText=(LPWSTR)L"截图／图片";TabCtrl_InsertItem(tabControl,1,&tab);TabCtrl_SetCurSel(tabControl,activeTab);
 starBox=control(panel,L"BUTTON",L"只看收藏",BS_AUTOCHECKBOX|WS_TABSTOP,ID_STAR);SendMessageW(starBox,BM_SETCHECK,favoritesOnly[activeTab]?BST_CHECKED:BST_UNCHECKED,0);
 SendMessageW(search,EM_SETCUEBANNER,TRUE,(LPARAM)(activeTab==0?L"搜索复制过的文字…":L"搜索图片尺寸…"));
 listbox=control(panel,L"LISTBOX",L"",LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_TABSTOP,ID_LIST);SendMessageW(listbox,LB_SETITEMHEIGHT,0,S(72));
 imageGrid=control(panel,WC_LISTVIEW,L"",LVS_ICON|LVS_AUTOARRANGE|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_SHAREIMAGELISTS|WS_TABSTOP,ID_IMAGE_GRID);ListView_SetBkColor(imageGrid,RGB(255,255,255));ListView_SetTextBkColor(imageGrid,CLR_NONE);
 detailTitle=control(panel,L"STATIC",L"",0,0);control(panel,L"BUTTON",L"复制内容",WS_TABSTOP|BS_DEFPUSHBUTTON,1);pinButton=control(panel,L"BUTTON",L"收藏",WS_TABSTOP,ID_PIN);control(panel,L"BUTTON",L"删除",WS_TABSTOP,ID_DELETE);
 textPreview=control(panel,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,210);SendMessageW(textPreview,EM_SETLIMITTEXT,1024*1024,0);
 imagePreview=control(panel,L"LightCapture.ClipboardImage",L"",0,211);status=control(panel,L"STATIC",L"",SS_CENTERIMAGE,212);
 pauseButton=control(panel,L"BUTTON",L"暂停记录",WS_TABSTOP,ID_PAUSE);control(panel,L"BUTTON",L"设置",WS_TABSTOP,ID_SETTINGS);
 control(panel,L"BUTTON",L"保存 PNG",WS_TABSTOP,ID_IMAGE_SAVE);
 layout();refresh();ShowWindow(panel,SW_SHOW);SetForegroundWindow(panel);SetFocus(search);
}
static void init(){
 try{
  std::wstring directory=dataOverride;if(directory.empty()){PWSTR local=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_CREATE,nullptr,&local)))throw std::runtime_error("Local data unavailable");directory=std::wstring(local)+L"\\LightCapture\\Clipboard";CoTaskMemFree(local);}
  store=std::make_unique<Store>();store->open(directory);paused=store->getSetting(L"paused")==L"1";excludedApps=store->getSetting(L"excluded");shortcutChoice=_wtoi(store->getSetting(L"shortcut",L"0").c_str());if(shortcutChoice<0||shortcutChoice>2)shortcutChoice=0;
  activeTab=store->getSetting(L"active_tab")==L"1"?1:0;favoritesOnly[0]=store->getSetting(L"favorites_text")==L"1";favoritesOnly[1]=store->getSetting(L"favorites_images")==L"1";
  INITCOMMONCONTROLSEX commonControls={sizeof(commonControls),ICC_TAB_CLASSES|ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&commonControls);
  Gdiplus::GdiplusStartupInput input;if(Gdiplus::GdiplusStartup(&gdipToken,&input,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Image engine unavailable");
  WNDCLASSEXW cls={sizeof(cls)};cls.hInstance=instanceHandle;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);cls.hIcon=appIcon;cls.lpszClassName=PANEL_CLASS;cls.lpfnWndProc=panelProc;RegisterClassExW(&cls);
  cls.lpszClassName=L"LightCapture.ClipboardImage";cls.lpfnWndProc=imageProc;RegisterClassExW(&cls);cls.lpszClassName=L"LightCapture.ClipboardSettings";cls.lpfnWndProc=settingsProc;RegisterClassExW(&cls);
  setShortcut(shortcutChoice);listenReady=AddClipboardFormatListener(mainWindow)!=FALSE;
  if(!listenReady)lastMessage=L"监听未能启动，请重新打开工具";
 }catch(...){startupLog("clipboard-init-failed");store.reset();}
}
static void shutdown(){
 KillTimer(mainWindow,RETRY_TIMER);KillTimer(mainWindow,SEARCH_TIMER);if(listenReady)RemoveClipboardFormatListener(mainWindow);UnregisterHotKey(mainWindow,2);
 if(panel)DestroyWindow(panel);store.reset();if(gdipToken){Gdiplus::GdiplusShutdown(gdipToken);gdipToken=0;}
}
static bool translate(MSG& m){
 if(settingsWindow && IsDialogMessageW(settingsWindow,&m))return true;
 if(panel && (m.hwnd==panel || IsChild(panel,m.hwnd))){
  if(m.message==WM_KEYDOWN && m.wParam==VK_ESCAPE){PostMessageW(panel,WM_CLOSE,0,0);return true;}
  if(m.message==WM_KEYDOWN && m.wParam==VK_RETURN && m.hwnd==listbox){copyChosen(true);return true;}
  if(IsDialogMessageW(panel,&m))return true;
 }return false;
}
}
