#pragma once
#include <windows.h>
#include <shellapi.h>
#include <flutter/flutter_view_controller.h>
void InstallNative(flutter::FlutterViewController*,HWND);
void NativeDrop(HDROP);
void NativeClose();

void NativeRequestClose();
