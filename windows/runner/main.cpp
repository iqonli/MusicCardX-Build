#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include "flutter/generated_plugin_registrant.h"
#include "native.h"
#include <memory>
static std::unique_ptr<flutter::FlutterViewController> controller;
static bool closeRequested=false;
LRESULT CALLBACK WindowProc(HWND h,UINT m,WPARAM w,LPARAM l) {
 if(m==WM_CLOSE){if(!closeRequested){closeRequested=true;SetTimer(h,41,5000,nullptr);NativeRequestClose();}return 0;}
 if(m==WM_TIMER&&w==41){KillTimer(h,41);PostMessage(h,WM_APP+1,0,0);return 0;}
 if(m==WM_APP+1){KillTimer(h,41);NativeClose();DestroyWindow(h);return 0;}
 if(controller) { auto r=controller->HandleTopLevelWindowProc(h,m,w,l); if(r) return *r; }
 switch(m) {
 case WM_DPICHANGED: {auto*r=reinterpret_cast<RECT*>(l);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
 case WM_SIZE: if(controller) MoveWindow(controller->view()->GetNativeWindow(),0,0,LOWORD(l),HIWORD(l),TRUE); return 0;
 case WM_DROPFILES: NativeDrop(reinterpret_cast<HDROP>(w)); return 0;
 case WM_DESTROY: PostQuitMessage(0); return 0;
 }
 return DefWindowProc(h,m,w,l);
}
int APIENTRY wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 WNDCLASS wc{};wc.hInstance=instance;wc.lpszClassName=L"MusicCardXDesktop";wc.lpfnWndProc=WindowProc;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=LoadIcon(instance,MAKEINTRESOURCE(101));RegisterClass(&wc);
 HWND window=CreateWindow(wc.lpszClassName,L"MusicCardX",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1280,800,nullptr,nullptr,instance,nullptr);
 wchar_t exe[32768];GetModuleFileName(nullptr,exe,32768);std::wstring path(exe);path=path.substr(0,path.find_last_of(L"\\/"));
 flutter::DartProject project(path+L"\\data");DWORD prefer=1,bytes=sizeof(prefer);RegGetValue(HKEY_CURRENT_USER,L"Software\\IQ Online Studio\\MusicCardX",L"highPerformanceGpu",RRF_RT_REG_DWORD,nullptr,&prefer,&bytes);
 project.set_gpu_preference(prefer?flutter::GpuPreference::HighPerformancePreference:flutter::GpuPreference::NoPreference);
 project.set_ui_thread_policy(flutter::UIThreadPolicy::RunOnSeparateThread);
 controller=std::make_unique<flutter::FlutterViewController>(1280,800,project);
 if(!controller->engine()||!controller->view()) return 1;
 RegisterPlugins(controller->engine());SetParent(controller->view()->GetNativeWindow(),window);
 RECT rect;GetClientRect(window,&rect);MoveWindow(controller->view()->GetNativeWindow(),0,0,rect.right,rect.bottom,TRUE);
 InstallNative(controller.get(),window);DragAcceptFiles(window,TRUE);DragAcceptFiles(controller->view()->GetNativeWindow(),TRUE);
 // Flutter's child HWND receives shell drops; forward them to the same importer.
 SetWindowSubclass(controller->view()->GetNativeWindow(),[](HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR)->LRESULT {if(m==WM_DROPFILES){NativeDrop(reinterpret_cast<HDROP>(w));return 0;}return DefSubclassProc(h,m,w,l);},1,0);
 // Attaching an HWND does not make the Flutter child visible.
 ShowWindow(controller->view()->GetNativeWindow(),SW_SHOW);
 controller->engine()->SetNextFrameCallback([window,show](){ShowWindow(window,show);UpdateWindow(window);});
 controller->ForceRedraw();
 ShowWindow(window,show);SetFocus(controller->view()->GetNativeWindow());
 MSG msg;while(GetMessage(&msg,nullptr,0,0)){TranslateMessage(&msg);DispatchMessage(&msg);} controller.reset();CoUninitialize();return 0;
}
