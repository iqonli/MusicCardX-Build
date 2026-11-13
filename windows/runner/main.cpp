#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include "flutter/generated_plugin_registrant.h"
#include "native.h"
#include <memory>
static std::unique_ptr<flutter::FlutterViewController> controller;
LRESULT CALLBACK WindowProc(HWND h,UINT m,WPARAM w,LPARAM l) {
 if(controller) { auto r=controller->HandleTopLevelWindowProc(h,m,w,l); if(r) return *r; }
 switch(m) {
 case WM_SIZE: if(controller) MoveWindow(controller->view()->GetNativeWindow(),0,0,LOWORD(l),HIWORD(l),TRUE); return 0;
 case WM_DROPFILES: NativeDrop(reinterpret_cast<HDROP>(w)); return 0;
 case WM_CLOSE: NativeRequestClose(); return 0;
 case WM_APP+1: NativeClose(); DestroyWindow(h); return 0;
 case WM_DESTROY: PostQuitMessage(0); return 0;
 }
 return DefWindowProc(h,m,w,l);
}
int APIENTRY wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 WNDCLASS wc{};wc.hInstance=instance;wc.lpszClassName=L"MusicCardXDesktop";wc.lpfnWndProc=WindowProc;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClass(&wc);
 HWND window=CreateWindow(wc.lpszClassName,L"MusicCardX",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1280,800,nullptr,nullptr,instance,nullptr);
 wchar_t exe[32768];GetModuleFileName(nullptr,exe,32768);std::wstring path(exe);path=path.substr(0,path.find_last_of(L"\\/"));
 flutter::DartProject project(path+L"\\data");controller=std::make_unique<flutter::FlutterViewController>(1280,800,project);
 if(!controller->engine()||!controller->view()) return 1;
 RegisterPlugins(controller->engine());SetParent(controller->view()->GetNativeWindow(),window);
 RECT rect;GetClientRect(window,&rect);MoveWindow(controller->view()->GetNativeWindow(),0,0,rect.right,rect.bottom,TRUE);
 InstallNative(controller.get(),window);DragAcceptFiles(window,TRUE);DragAcceptFiles(controller->view()->GetNativeWindow(),TRUE);
 // Flutter's child HWND receives shell drops; forward them to the same importer.
 SetWindowSubclass(controller->view()->GetNativeWindow(),[](HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR)->LRESULT {if(m==WM_DROPFILES){NativeDrop(reinterpret_cast<HDROP>(w));return 0;}return DefSubclassProc(h,m,w,l);},1,0);
 ShowWindow(window,show);SetFocus(controller->view()->GetNativeWindow());
 MSG msg;while(GetMessage(&msg,nullptr,0,0)){TranslateMessage(&msg);DispatchMessage(&msg);} controller.reset();CoUninitialize();return 0;
}
