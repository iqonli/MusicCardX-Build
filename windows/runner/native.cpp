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
using Microsoft::WRL::ComPtr;
using V=flutter::EncodableValue;using M=flutter::EncodableMap;using L=flutter::EncodableList;
static HWND owner=nullptr,preview=nullptr;
static std::unique_ptr<flutter::MethodChannel<V>> channel;
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
static LRESULT CALLBACK previewProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_COMMAND){const char*action=LOWORD(w)==1?"saveImage":LOWORD(w)==2?"saveAsImage":"copyImage";channel->InvokeMethod("previewAction",std::make_unique<V>(action));return 0;}
 if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);FillRect(dc,&r,(HBRUSH)(COLOR_WINDOW+1));if(pw&&ph){double scale=std::min(double(r.right)/pw,double(std::max(1L,r.bottom-48))/ph);int width=int(pw*scale),height=int(ph*scale);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=pw;info.bmiHeader.biHeight=-LONG(ph);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;SetStretchBltMode(dc,HALFTONE);StretchDIBits(dc,(r.right-width)/2,48,width,height,0,0,pw,ph,previewPixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);}EndPaint(h,&ps);return 0;}
 if(m==WM_CLOSE){ShowWindow(h,SW_HIDE);return 0;}return DefWindowProc(h,m,w,l);
}
void NativeRequestClose(){
 if(!channel){PostMessage(owner,WM_APP+1,0,0);return;}
 channel->InvokeMethod("closing",nullptr,std::make_unique<flutter::MethodResultFunctions<V>>(
  [](const V*){PostMessage(owner,WM_APP+1,0,0);},
  [](const std::string&,const std::string&,const V*){PostMessage(owner,WM_APP+1,0,0);},
  [](){PostMessage(owner,WM_APP+1,0,0);}));
}
void NativeClose(){if(preview)DestroyWindow(preview);channel.reset();}
void InstallNative(flutter::FlutterViewController*c,HWND window){owner=window;
 channel=std::make_unique<flutter::MethodChannel<V>>(c->engine()->messenger(),"com.iqonli.musiccardx/windows",&flutter::StandardMethodCodec::GetInstance());
 channel->SetMethodCallHandler([](const flutter::MethodCall<V>&call,std::unique_ptr<flutter::MethodResult<V>>result){try{
 M empty;const M&a=call.arguments()&&std::holds_alternative<M>(*call.arguments())?std::get<M>(*call.arguments()):empty;const auto&m=call.method_name();
 if(m=="locations"){wchar_t exe[32768];GetModuleFileName(nullptr,exe,32768);PWSTR path=nullptr;check(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path));M v{{V("exe"),V(utf(std::filesystem::path(exe).parent_path().wstring()))},{V("appData"),V(utf(path))}};CoTaskMemFree(path);result->Success(V(v));}
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
 else if(m=="preview"){
 auto bytes=std::get<std::vector<uint8_t>>(get(a,"bytes"));previewPixels=decode(bytes,pw,ph);
 if(!preview){WNDCLASS wc{};wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName=L"MusicCardXPreview";wc.lpfnWndProc=previewProc;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClass(&wc);preview=CreateWindow(wc.lpszClassName,L"预览",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,900,600,nullptr,nullptr,wc.hInstance,nullptr);const wchar_t*labels[]={L"快速保存",L"保存",L"复制"};for(int i=0;i<3;i++)CreateWindow(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,8+i*98,8,92,30,preview,(HMENU)(INT_PTR)(i+1),wc.hInstance,nullptr);}
 SetWindowText(preview,wide(str(a,"title")).c_str());InvalidateRect(preview,nullptr,TRUE);if(get(a,"show")==V(true))ShowWindow(preview,SW_SHOWNORMAL);result->Success();}
 else result->NotImplemented();
 }catch(const std::exception&e){result->Error("windows",e.what());}});
}
