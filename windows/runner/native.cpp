#include "native.h"
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <wincodec.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <flutter/method_channel.h>
#include <flutter/method_result_functions.h>
#include <flutter/standard_method_codec.h>
#include <filesystem>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <climits>
#include <cstring>
#include <fstream>
#include <commdlg.h>
#include <commctrl.h>
using Microsoft::WRL::ComPtr;
using V=flutter::EncodableValue;using M=flutter::EncodableMap;using L=flutter::EncodableList;
static HWND owner=nullptr,preview=nullptr;
static std::unique_ptr<flutter::MethodChannel<V>> channel;
struct PaintCache { std::shared_ptr<void> bitmap; int width=0,height=0; };
static PaintCache previewCache;
static std::vector<uint8_t> previewPixels;static UINT pw=0,ph=0;
static std::wstring wide(const std::string&s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);std::wstring v(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),v.data(),n);return v;}
static std::string utf(const std::wstring&s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);std::string v(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),v.data(),n,nullptr,nullptr);return v;}
static V get(const M&a,const char*k){auto i=a.find(V(k));return i==a.end()?V():i->second;}
static std::string str(const M&a,const char*k){auto v=get(a,k);auto s=std::get_if<std::string>(&v);return s?*s:"";}
static void check(HRESULT h){if(FAILED(h)) throw std::runtime_error("Windows operation failed: "+std::to_string(h));}
static std::vector<uint8_t> decode(const std::vector<uint8_t>&b,UINT&w,UINT&h){
 ComPtr<IWICImagingFactory> f;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f)));
 ComPtr<IWICStream> s;check(f->CreateStream(&s));check(s->InitializeFromMemory(const_cast<BYTE*>(b.data()),(DWORD)b.size()));
 ComPtr<IWICBitmapDecoder>d;check(f->CreateDecoderFromStream(s.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&d));ComPtr<IWICBitmapFrameDecode> frame;check(d->GetFrame(0,&frame));check(frame->GetSize(&w,&h));
 if(uint64_t(w)*h>SIZE_MAX/4 || uint64_t(w)*h*4>UINT_MAX)throw std::runtime_error("Image too large for Windows clipboard");
 ComPtr<IWICFormatConverter>c;check(f->CreateFormatConverter(&c));check(c->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));std::vector<uint8_t> out(size_t(w)*h*4);check(c->CopyPixels(nullptr,w*4,(UINT)out.size(),out.data()));return out;
}
static void putClipboard(UINT format,const void* data,size_t size){HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,size);if(!memory)throw std::bad_alloc();void*p=GlobalLock(memory);if(!p){GlobalFree(memory);throw std::runtime_error("Clipboard allocation failed");}memcpy(p,data,size);GlobalUnlock(memory);if(!SetClipboardData(format,memory)){GlobalFree(memory);throw std::runtime_error("Clipboard write failed");}}
static L paths(HDROP drop){L list;UINT n=DragQueryFile(drop,0xffffffff,nullptr,0);for(UINT i=0;i<n;i++){UINT len=DragQueryFile(drop,i,nullptr,0);std::wstring s(len+1,0);DragQueryFile(drop,i,s.data(),len+1);s.resize(len);list.emplace_back(utf(s));}return list;}
void NativeDrop(HDROP drop){auto list=paths(drop);DragFinish(drop);if(channel)channel->InvokeMethod("dropFiles",std::make_unique<V>(list));}
static L pick(const M&a,bool save=false){
 ComPtr<IFileDialog> dialog;
 if(save){ComPtr<IFileSaveDialog>d;check(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&d)));dialog=d;}
 else{ComPtr<IFileOpenDialog>d;check(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&d)));dialog=d;}
 DWORD flags;check(dialog->GetOptions(&flags));flags|=FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR;
 const bool directory=get(a,"directory")==V(true);
 if(directory)flags|=FOS_PICKFOLDERS;
 if(!save&&!directory&&get(a,"multiple")==V(true))flags|=FOS_ALLOWMULTISELECT;
 if(save)flags|=FOS_OVERWRITEPROMPT;else flags|=FOS_FILEMUSTEXIST;
 check(dialog->SetOptions(flags));std::wstring pattern;
 auto extensions=get(a,"extensions");if(auto list=std::get_if<L>(&extensions))for(auto&e:*list)if(auto s=std::get_if<std::string>(&e)){if(!pattern.empty())pattern+=L";";pattern+=L"*."+wide(*s);}
 if(!directory&&!pattern.empty()){COMDLG_FILTERSPEC types[]={{L"支持的文件",pattern.c_str()},{L"所有文件",L"*.*"}};check(dialog->SetFileTypes(2,types));}
 const auto initial=wide(str(a,"initial"));if(!initial.empty()){ComPtr<IShellItem>item;if(SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(),nullptr,IID_PPV_ARGS(&item))))dialog->SetFolder(item.Get());}
 auto name=wide(str(a,"name"));if(!name.empty())dialog->SetFileName(name.c_str());
 HRESULT hr=dialog->Show(owner);if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};check(hr);L result;
 auto add=[&](IShellItem*item){PWSTR path=nullptr;check(item->GetDisplayName(SIGDN_FILESYSPATH,&path));result.emplace_back(utf(path));CoTaskMemFree(path);};
 if(save){ComPtr<IShellItem>item;check(dialog->GetResult(&item));add(item.Get());}else{ComPtr<IFileOpenDialog>d;check(dialog.As(&d));ComPtr<IShellItemArray>items;check(d->GetResults(&items));DWORD count;check(items->GetCount(&count));for(DWORD i=0;i<count;i++){ComPtr<IShellItem>item;check(items->GetItemAt(i,&item));add(item.Get());}}return result;
}
static L fontFiles(){
 ComPtr<IDWriteFactory>f;check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(f.GetAddressOf())));ComPtr<IDWriteFontCollection>collection;check(f->GetSystemFontCollection(&collection));L result;std::vector<std::wstring>seen;
 for(UINT32 i=0;i<collection->GetFontFamilyCount();i++){ComPtr<IDWriteFontFamily>family;check(collection->GetFontFamily(i,&family));for(UINT32 j=0;j<family->GetFontCount();j++){ComPtr<IDWriteFont>font;check(family->GetFont(j,&font));ComPtr<IDWriteFontFace>face;check(font->CreateFontFace(&face));UINT32 n=0;check(face->GetFiles(&n,nullptr));std::vector<IDWriteFontFile*>files(n);check(face->GetFiles(&n,files.data()));for(auto raw:files){ComPtr<IDWriteFontFile>file;file.Attach(raw);ComPtr<IDWriteFontFileLoader>loader;file->GetLoader(&loader);ComPtr<IDWriteLocalFontFileLoader>local;if(FAILED(loader.As(&local)))continue;const void*key=nullptr;UINT32 size=0;check(file->GetReferenceKey(&key,&size));UINT32 length=0;check(local->GetFilePathLengthFromKey(key,size,&length));std::wstring path(length+1,0);check(local->GetFilePathFromKey(key,size,path.data(),length+1));path.resize(length);if(std::find(seen.begin(),seen.end(),path)==seen.end()){seen.push_back(path);result.emplace_back(utf(path));}}}}
 return result;
}
static HFONT uiFont=nullptr;
static void setUiFont(HWND parent){
 if(!uiFont){NONCLIENTMETRICS n{};n.cbSize=sizeof(n);SystemParametersInfo(SPI_GETNONCLIENTMETRICS,sizeof(n),&n,0);n.lfMessageFont.lfQuality=ANTIALIASED_QUALITY;uiFont=CreateFontIndirect(&n.lfMessageFont);}
 EnumChildWindows(parent,[](HWND child,LPARAM)->BOOL{SendMessage(child,WM_SETFONT,(WPARAM)uiFont,TRUE);return TRUE;},0);
}
static void emit(const char*method,V value){if(channel)channel->InvokeMethod(method,std::make_unique<V>(std::move(value)));}
static std::vector<uint8_t> rgbaPixels(const M&a,UINT&w,UINT&h){
 auto width=get(a,"width"),height=get(a,"height");w=UINT(std::get<int32_t>(width));h=UINT(std::get<int32_t>(height));
 auto bytes=std::get<std::vector<uint8_t>>(get(a,"bytes"));if(w<1||h<1||size_t(w)*h*4!=bytes.size())throw std::runtime_error("Invalid RGBA buffer");
 for(size_t i=0;i<bytes.size();i+=4)std::swap(bytes[i],bytes[i+2]);return bytes;
}
static void drawImage(HDC dc,const std::vector<uint8_t>&pixels,UINT w,UINT h,RECT area,PaintCache&cache){
 if(!w||!h||pixels.empty()||area.right<=area.left||area.bottom<=area.top)return;
 const double scale=std::min(double(area.right-area.left)/w,double(area.bottom-area.top)/h);
 const int width=std::max(1,int(w*scale)),height=std::max(1,int(h*scale));
 BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-LONG(h);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 HDC buffer=CreateCompatibleDC(dc);
 if(!buffer)return;
 if(!cache.bitmap||cache.width!=width||cache.height!=height){
  HBITMAP bitmap=CreateCompatibleBitmap(dc,width,height);
  if(!bitmap){DeleteDC(buffer);return;}
  auto old=SelectObject(buffer,bitmap);
  SetStretchBltMode(buffer,HALFTONE);SetBrushOrgEx(buffer,0,0,nullptr);
  StretchDIBits(buffer,0,0,width,height,0,0,w,h,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
  SelectObject(buffer,old);
  cache.bitmap=std::shared_ptr<void>(bitmap,[](void*p){DeleteObject(static_cast<HBITMAP>(p));});
  cache.width=width;cache.height=height;
 }
 auto old=SelectObject(buffer,static_cast<HBITMAP>(cache.bitmap.get()));
 BitBlt(dc,area.left+(area.right-area.left-width)/2,area.top+(area.bottom-area.top-height)/2,width,height,buffer,0,0,SRCCOPY);
 SelectObject(buffer,old);DeleteDC(buffer);
}
static const char* previewActions[]={"Input","Copy seed","刷新","复制","快速保存","保存"};
static LRESULT CALLBACK previewProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_COMMAND){int id=LOWORD(w);if(id>=1&&id<=6){if(id==1)SetForegroundWindow(owner);emit("previewAction",V(previewActions[id-1]));}return 0;}
 if(m==WM_SIZE){int width=LOWORD(l);for(int i=0;i<6;i++)MoveWindow(GetDlgItem(h,i+1),8+i*92,8,88,28,TRUE);MoveWindow(GetDlgItem(h,20),8,40,std::max(1,width-16),24,TRUE);InvalidateRect(h,nullptr,FALSE);return 0;}
 if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);FillRect(dc,&r,(HBRUSH)(COLOR_WINDOW+1));r.top=72;drawImage(dc,previewPixels,pw,ph,r,previewCache);EndPaint(h,&ps);return 0;}
 if(m==WM_DPICHANGED){auto*r=(RECT*)l;SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
 if(m==WM_CLOSE){emit("previewClosed",V());ShowWindow(h,SW_HIDE);return 0;}return DefWindowProc(h,m,w,l);
}
struct FairItem{PaintCache cache;std::string seed;UINT width=0,height=0;std::vector<uint8_t>pixels;};
static HWND fair=nullptr;static std::vector<FairItem> fairItems(20);static int fairColumns=2,fairWorkers=1,fairLimit=1,fairScroll=0,fairTarget=0;static int fairEpoch=0;
static constexpr const wchar_t* preferences=L"Software\\IQ Online Studio\\MusicCardX";
static DWORD pref(const wchar_t*key,DWORD fallback){DWORD value=0,n=sizeof(value);return RegGetValue(HKEY_CURRENT_USER,preferences,key,RRF_RT_REG_DWORD,nullptr,&value,&n)==ERROR_SUCCESS?value:fallback;}
static void savePref(const wchar_t*key,DWORD value){HKEY h;if(RegCreateKeyEx(HKEY_CURRENT_USER,preferences,0,nullptr,0,KEY_SET_VALUE,nullptr,&h,nullptr)==ERROR_SUCCESS){RegSetValueEx(h,key,0,REG_DWORD,(BYTE*)&value,sizeof(value));RegCloseKey(h);}}
static void loadFair(){fairItems.assign(20,FairItem{});fairScroll=fairTarget=0;InvalidateRect(fair,nullptr,TRUE);emit("fairLoad",V(M{{V("workers"),V(fairWorkers)},{V("epoch"),V(++fairEpoch)}}));}
static int fairCellWidth(HWND h){RECT r;GetClientRect(h,&r);return std::max(20,int(r.right-24)/fairColumns);}
static void clampFair(){if(!fair)return;RECT r;GetClientRect(fair,&r);int rows=(20+fairColumns-1)/fairColumns;int maximum=std::max(0,rows*(fairCellWidth(fair)*9/16+36)-int(r.bottom-64));fairTarget=std::clamp(fairTarget,0,maximum);}
static LRESULT CALLBACK fairProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_COMMAND){int id=LOWORD(w);
  if(id==35){wchar_t text[20];GetWindowText(GetDlgItem(h,34),text,20);fairWorkers=std::clamp(_wtoi(text),1,fairLimit);loadFair();}
  if(id==32&&HIWORD(w)==EN_KILLFOCUS){wchar_t text[20];GetWindowText(GetDlgItem(h,32),text,20);fairColumns=std::clamp(_wtoi(text),1,10);SetWindowText(GetDlgItem(h,32),std::to_wstring(fairColumns).c_str());SendMessage(GetDlgItem(h,31),TBM_SETPOS,TRUE,fairColumns);clampFair();InvalidateRect(h,nullptr,TRUE);}
  return 0;
 }
 if(m==WM_HSCROLL){fairColumns=int(SendMessage(GetDlgItem(h,31),TBM_GETPOS,0,0));SetWindowText(GetDlgItem(h,32),std::to_wstring(fairColumns).c_str());clampFair();InvalidateRect(h,nullptr,TRUE);return 0;}
 if(m==WM_MOUSEWHEEL){fairTarget-=GET_WHEEL_DELTA_WPARAM(w);clampFair();SetTimer(h,7,16,nullptr);return 0;}
 if(m==WM_TIMER&&w==7){int distance=fairTarget-fairScroll;fairScroll+=std::clamp(distance,-19,19);InvalidateRect(h,nullptr,FALSE);if(fairScroll==fairTarget)KillTimer(h,7);return 0;}
 if(m==WM_SIZE){clampFair();InvalidateRect(h,nullptr,TRUE);return 0;}
 if(m==WM_LBUTTONUP){int x=short(LOWORD(l)),y=short(HIWORD(l));if(y<64)return 0;int cw=fairCellWidth(h),rh=cw*9/16+36;int index=((y-64+fairScroll)/rh)*fairColumns+(x-8)/cw;if(index>=0&&index<20&&!fairItems[index].seed.empty()){emit("fairSeed",V(fairItems[index].seed));SendMessage(h,WM_CLOSE,0,0);}return 0;}
 if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);HDC buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));auto old=SelectObject(buffer,bitmap);FillRect(buffer,&r,(HBRUSH)(COLOR_WINDOW+1));SelectObject(buffer,uiFont);SetBkMode(buffer,TRANSPARENT);
  int cw=fairCellWidth(h),rh=cw*9/16+36;IntersectClipRect(buffer,0,64,r.right,r.bottom);
  for(int i=0;i<20;i++){int y=64+(i/fairColumns)*rh-fairScroll;if(y+rh<64||y>r.bottom)continue;RECT cell{8+(i%fairColumns)*cw,y,8+(i%fairColumns+1)*cw-8,y+rh-28};drawImage(buffer,fairItems[i].pixels,fairItems[i].width,fairItems[i].height,cell,fairItems[i].cache);cell.top=cell.bottom;cell.bottom+=24;auto label=wide(fairItems[i].seed.empty()?"正在生成…":fairItems[i].seed);DrawText(buffer,label.c_str(),-1,&cell,DT_CENTER|DT_SINGLELINE);}
  BitBlt(dc,0,64,r.right,std::max(0L,r.bottom-64),buffer,0,64,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(h,&ps);return 0;
 }
 if(m==WM_CLOSE){WINDOWPLACEMENT pos{};pos.length=sizeof(pos);GetWindowPlacement(h,&pos);savePref(L"fairWidth",pos.rcNormalPosition.right-pos.rcNormalPosition.left);savePref(L"fairHeight",pos.rcNormalPosition.bottom-pos.rcNormalPosition.top);savePref(L"fairMaximized",IsZoomed(h));savePref(L"fairColumns",fairColumns);savePref(L"fairWorkers",fairWorkers);emit("fairClose",V());fairItems.assign(20,FairItem{});ShowWindow(h,SW_HIDE);return 0;}
 return DefWindowProc(h,m,w,l);
}
void NativeRequestClose(){
 if(!channel){PostMessage(owner,WM_APP+1,0,0);return;}
 channel->InvokeMethod("closing",nullptr,std::make_unique<flutter::MethodResultFunctions<V>>(
  [](const V*){PostMessage(owner,WM_APP+1,0,0);},
  [](const std::string&,const std::string&,const V*){PostMessage(owner,WM_APP+1,0,0);},
  [](){PostMessage(owner,WM_APP+1,0,0);}));
}
void NativeClose(){previewCache={};fairItems.clear();if(preview)DestroyWindow(preview);if(fair)DestroyWindow(fair);if(uiFont)DeleteObject(uiFont);channel.reset();}
void InstallNative(flutter::FlutterViewController*c,HWND window){owner=window;
 channel=std::make_unique<flutter::MethodChannel<V>>(c->engine()->messenger(),"com.iqonli.musiccardx/windows",&flutter::StandardMethodCodec::GetInstance());
 channel->SetMethodCallHandler([](const flutter::MethodCall<V>&call,std::unique_ptr<flutter::MethodResult<V>>result){try{
 M empty;const M&a=call.arguments()&&std::holds_alternative<M>(*call.arguments())?std::get<M>(*call.arguments()):empty;const auto&m=call.method_name();
 if(m=="locations"){wchar_t exe[32768];GetModuleFileName(nullptr,exe,32768);PWSTR path=nullptr;check(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path));NONCLIENTMETRICS metrics{};metrics.cbSize=sizeof(metrics);SystemParametersInfo(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0);M v{{V("uiFontFamily"),V(utf(metrics.lfMessageFont.lfFaceName))},{V("exe"),V(utf(std::filesystem::path(exe).parent_path().wstring()))},{V("appData"),V(utf(path))}};CoTaskMemFree(path);result->Success(V(v));}
 else if(m=="startupTrace"){
 wchar_t report[32768];DWORD n=GetEnvironmentVariable(L"MCX_SMOKE_REPORT",report,32768);
 if(n>0&&n<32768){std::ofstream trace{std::filesystem::path(std::wstring(report)+L".log"),std::ios::app};trace<<str(a,"text")<<std::endl;}
 result->Success();
 }
 else if(m=="editorReady"){
 wchar_t report[32768];DWORD n=GetEnvironmentVariable(L"MCX_SMOKE_REPORT",report,32768);
 if(n>0&&n<32768){std::ofstream f{std::filesystem::path(report)};f<<"editor-ready";}
 result->Success();
 }
 else if(m=="replaceFile"){if(!MoveFileEx(wide(str(a,"from")).c_str(),wide(str(a,"to")).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace config file");result->Success();}
 else if(m=="pickFiles"||m=="saveFile")result->Success(V(pick(a,m=="saveFile")));
 else if(m=="pickCover"){M args{{V("extensions"),V(L{V("png"),V("jpg"),V("jpeg"),V("webp"),V("bmp"),V("gif")})}};auto p=pick(args);if(p.empty())result->Success();else result->Success(p.front());}
 else if(m=="systemFontFiles")result->Success(V(fontFiles()));
 else if(m=="openUrl"){auto url=str(a,"url");if(url.rfind("https://",0)!=0&&url.rfind("http://",0)!=0)throw std::runtime_error("Unsupported URL");if((INT_PTR)ShellExecute(nullptr,L"open",wide(url).c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)throw std::runtime_error("Cannot open URL");result->Success();}
 else if(m=="copyImage"){
 auto bytes=std::get<std::vector<uint8_t>>(get(a,"bytes"));UINT w,h;auto pixels=decode(bytes,w,h);BITMAPV5HEADER header{};header.bV5Size=sizeof(header);header.bV5Width=w;header.bV5Height=-LONG(h);header.bV5Planes=1;header.bV5BitCount=32;header.bV5Compression=BI_BITFIELDS;header.bV5RedMask=0x00ff0000;header.bV5GreenMask=0x0000ff00;header.bV5BlueMask=0x000000ff;header.bV5AlphaMask=0xff000000;header.bV5CSType=LCS_sRGB;
 std::vector<uint8_t>dib(sizeof(header)+pixels.size());memcpy(dib.data(),&header,sizeof(header));memcpy(dib.data()+sizeof(header),pixels.data(),pixels.size());if(!OpenClipboard(owner))throw std::runtime_error("Clipboard busy");try{EmptyClipboard();putClipboard(CF_DIBV5,dib.data(),dib.size());if(str(a,"mime")=="image/png")putClipboard(RegisterClipboardFormat(L"PNG"),bytes.data(),bytes.size());CloseClipboard();}catch(...){CloseClipboard();throw;}result->Success(V("已复制图片"));}
 else if(m=="readClipboard"){
 if(!OpenClipboard(owner))throw std::runtime_error("Clipboard busy");M value;try{
 if(IsClipboardFormatAvailable(CF_HDROP)){auto list=paths((HDROP)GetClipboardData(CF_HDROP));value={{V("files"),V(list)}};}
 else if(IsClipboardFormatAvailable(RegisterClipboardFormat(L"PNG"))){HGLOBAL data=GetClipboardData(RegisterClipboardFormat(L"PNG"));auto*p=(uint8_t*)GlobalLock(data);if(p){value={{V("bytes"),V(std::vector<uint8_t>(p,p+GlobalSize(data)))}};GlobalUnlock(data);}}
 else if(IsClipboardFormatAvailable(CF_DIB)||IsClipboardFormatAvailable(CF_DIBV5)){
 UINT fmt=IsClipboardFormatAvailable(CF_DIBV5)?CF_DIBV5:CF_DIB;HGLOBAL data=GetClipboardData(fmt);auto*p=(uint8_t*)GlobalLock(data);if(p){size_t size=GlobalSize(data);if(size<sizeof(BITMAPINFOHEADER))throw std::runtime_error("Invalid DIB");auto*h=(BITMAPINFOHEADER*)p;size_t offset=h->biSize+(h->biClrUsed?h->biClrUsed:(h->biBitCount<=8?(1u<<h->biBitCount):0))*4;if(h->biSize==40&&h->biCompression==BI_BITFIELDS)offset+=12;BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfSize=DWORD(size+sizeof(file));file.bfOffBits=DWORD(sizeof(file)+offset);std::vector<uint8_t>b(sizeof(file)+size);memcpy(b.data(),&file,sizeof(file));memcpy(b.data()+sizeof(file),p,size);value={{V("bytes"),V(b)}};GlobalUnlock(data);}}
 else if(IsClipboardFormatAvailable(CF_BITMAP)){
 HBITMAP bitmap=(HBITMAP)GetClipboardData(CF_BITMAP);BITMAP bm{};
 if(!GetObject(bitmap,sizeof(bm),&bm)||bm.bmWidth<1||bm.bmHeight<1)throw std::runtime_error("Invalid clipboard bitmap");
 const size_t size=size_t(bm.bmWidth)*bm.bmHeight*4;if(size>MAXDWORD-sizeof(BITMAPINFOHEADER)-sizeof(BITMAPFILEHEADER))throw std::runtime_error("Bitmap too large");
 BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=bm.bmWidth;info.bmiHeader.biHeight=bm.bmHeight;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=DWORD(file.bfOffBits+size);
 std::vector<uint8_t>b(file.bfSize);memcpy(b.data(),&file,sizeof(file));memcpy(b.data()+sizeof(file),&info.bmiHeader,sizeof(info.bmiHeader));HDC dc=GetDC(nullptr);int lines=GetDIBits(dc,bitmap,0,bm.bmHeight,b.data()+file.bfOffBits,&info,DIB_RGB_COLORS);ReleaseDC(nullptr,dc);if(!lines)throw std::runtime_error("Cannot read clipboard bitmap");value={{V("bytes"),V(b)}};
 }
 else if(IsClipboardFormatAvailable(CF_UNICODETEXT)){HGLOBAL data=GetClipboardData(CF_UNICODETEXT);auto*p=(wchar_t*)GlobalLock(data);if(p){value={{V("text"),V(utf(p))}};GlobalUnlock(data);}}
 CloseClipboard();}catch(...){CloseClipboard();throw;}result->Success(V(value));}
 else if(m=="fontExists"){
 ComPtr<IDWriteFactory>factory;check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf())));ComPtr<IDWriteFontCollection>collection;check(factory->GetSystemFontCollection(&collection));UINT32 index=0;BOOL exists=FALSE;check(collection->FindFamilyName(wide(str(a,"family")).c_str(),&index,&exists));result->Success(V(exists!=FALSE));
 }
 else if(m=="chooseFont"){
 LOGFONT font{};auto name=wide(str(a,"family"));wcsncpy_s(font.lfFaceName,name.c_str(),_TRUNCATE);font.lfWeight=400;
 auto weight=get(a,"weight");if(auto value=std::get_if<int32_t>(&weight))font.lfWeight=*value;
 font.lfItalic=get(a,"italic")==V(true);font.lfHeight=-18;font.lfCharSet=DEFAULT_CHARSET;
 CHOOSEFONT dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=owner;dialog.lpLogFont=&font;dialog.Flags=CF_SCREENFONTS|CF_INITTOLOGFONTSTRUCT|CF_NOSCRIPTSEL|CF_NOVERTFONTS;
 if(ChooseFont(&dialog))result->Success(V(M{{V("font"),V(utf(font.lfFaceName))},{V("weight"),V(int32_t(font.lfWeight))},{V("italic"),V(font.lfItalic!=0)}}));else if(CommDlgExtendedError()!=0)throw std::runtime_error("Font dialog failed");else result->Success();
 }
 else if(m=="getRenderer"){result->Success(V(pref(L"impeller",0)?"Impeller":"Skia"));}
 else if(m=="setRenderer"){savePref(L"impeller",str(a,"renderer")=="Impeller");result->Success();}
 else if(m=="getGpuPreference"){result->Success(V(pref(L"highPerformanceGpu",1)!=0));}
 else if(m=="gpuPreference"){savePref(L"highPerformanceGpu",get(a,"enabled")==V(true)?1:0);result->Success();}
 else if(m=="fairOpen"){
 fairLimit=std::max(1,std::get<int32_t>(get(a,"limit")));fairColumns=std::clamp(int(pref(L"fairColumns",2)),1,10);fairWorkers=std::clamp(int(pref(L"fairWorkers",1)),1,fairLimit);
 if(!fair){WNDCLASS wc{};wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName=L"MusicCardXFair";wc.lpfnWndProc=fairProc;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=LoadIcon(wc.hInstance,MAKEINTRESOURCE(101));RegisterClass(&wc);fair=CreateWindow(wc.lpszClassName,L"种子博览会",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,0,0,1200,800,nullptr,nullptr,wc.hInstance,nullptr);
 auto control=[&](const wchar_t*type,const wchar_t*label,DWORD style,int x,int width,int id){CreateWindow(type,label,WS_CHILD|WS_VISIBLE|style,x,12,width,28,fair,(HMENU)(INT_PTR)id,wc.hInstance,nullptr);};
 control(L"STATIC",L"每行显示",0,8,70,30);control(TRACKBAR_CLASS,L"",TBS_HORZ,80,180,31);SendMessage(GetDlgItem(fair,31),TBM_SETRANGE,TRUE,MAKELPARAM(1,10));control(L"EDIT",L"2",WS_BORDER|ES_NUMBER,268,45,32);control(L"STATIC",L"渲染并发",0,330,70,33);control(L"EDIT",L"1",WS_BORDER|ES_NUMBER,406,45,34);control(L"BUTTON",L"生成20个",BS_PUSHBUTTON,466,100,35);setUiFont(fair);
 }
 SendMessage(GetDlgItem(fair,31),TBM_SETPOS,TRUE,fairColumns);SetWindowText(GetDlgItem(fair,32),std::to_wstring(fairColumns).c_str());SetWindowText(GetDlgItem(fair,34),std::to_wstring(fairWorkers).c_str());
 RECT screen;SystemParametersInfo(SPI_GETWORKAREA,0,&screen,0);int width=std::min(int(pref(L"fairWidth",1200)),int(screen.right-screen.left)),height=std::min(int(pref(L"fairHeight",800)),int(screen.bottom-screen.top));SetWindowPos(fair,nullptr,screen.left+(screen.right-screen.left-width)/2,screen.top+(screen.bottom-screen.top-height)/2,width,height,SWP_NOZORDER);ShowWindow(fair,pref(L"fairMaximized",1)?SW_MAXIMIZE:SW_SHOWNORMAL);SetForegroundWindow(fair);loadFair();result->Success();
 }
 else if(m=="fairItem"){
 if(std::get<int32_t>(get(a,"epoch"))!=fairEpoch){result->Success();return;}int index=std::get<int32_t>(get(a,"index"));if(index<0||index>=20)throw std::runtime_error("Invalid fair index");fairItems[index].cache={};fairItems[index].pixels=rgbaPixels(a,fairItems[index].width,fairItems[index].height);fairItems[index].seed=str(a,"seed");if(fair)InvalidateRect(fair,nullptr,FALSE);result->Success();
 }
 else if(m=="preview"){
 previewCache={};previewPixels=rgbaPixels(a,pw,ph);
 if(!preview){WNDCLASS wc{};wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName=L"MusicCardXPreview";wc.lpfnWndProc=previewProc;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=LoadIcon(wc.hInstance,MAKEINTRESOURCE(101));RegisterClass(&wc);preview=CreateWindow(wc.lpszClassName,L"预览",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1000,650,nullptr,nullptr,wc.hInstance,nullptr);const wchar_t*labels[]={L"Input",L"Copy seed",L"刷新",L"复制",L"快速保存",L"保存"};for(int i=0;i<6;i++)CreateWindow(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,8+i*92,8,88,28,preview,(HMENU)(INT_PTR)(i+1),wc.hInstance,nullptr);CreateWindow(L"STATIC",L"",WS_CHILD|WS_VISIBLE,8,40,950,24,preview,(HMENU)20,wc.hInstance,nullptr);setUiFont(preview);}
 SetWindowText(preview,wide(str(a,"title")).c_str());SetWindowText(GetDlgItem(preview,20),wide(str(a,"title")).c_str());InvalidateRect(preview,nullptr,FALSE);if(get(a,"show")==V(true))ShowWindow(preview,SW_SHOWNORMAL);result->Success();
 }
 else result->NotImplemented();
 }catch(const std::exception&e){result->Error("windows",e.what());}});
}
