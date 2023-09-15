// pch.h: 这是预编译标头文件。
// 下方列出的文件仅编译一次，提高了将来生成的生成性能。
// 这还将影响 IntelliSense 性能，包括代码完成和许多代码浏览功能。
// 但是，如果此处列出的文件中的任何一个在生成之间有更新，它们全部都将被重新编译。
// 请勿在此处添加要频繁更新的文件，这将使得性能优势无效。

#ifndef PCH_H
#define PCH_H

// 添加要在此处预编译的标头
#include "framework.h"

#include <iostream>
#include <sstream>
#include <fstream>

#include <string>
#include <thread>
#include <filesystem>
#include <queue>
#include <vector>
#include <optional>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <iostream>
#include <sstream>
#include <format>
#include <concepts>
#include <locale>

#include <Windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <DispatcherQueue.h>

#include <atlbase.h>
#include <atlcom.h>
#include <atlcomcli.h>
#include <atlimage.h>
#include <dwmapi.h>
#include <wchar.h>
#include <Psapi.h>
#include <atlimage.h>

#include <windows.graphics.capture.h>
#include <Windows.Graphics.Capture.Interop.h>
#include <windows.graphics.directx.direct3d11.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/windows.system.h>
#include <winrt/windows.foundation.h>
#include <winrt/windows.graphics.capture.h>
#include <winrt/windows.graphics.directx.direct3d11.h>

#include <opencv2/opencv.hpp>

#define _Cat_(a, b) a##b
#define _Cat(a, b) _Cat_(a, b)
#define _CatVarNameWithLine(Var) _Cat(Var, __LINE__)
#define LogTraceScope ::LoggerAux _CatVarNameWithLine(_func_aux_)
#define LogTraceFunction LogTraceScope(__FUNCTION__)

#define WindowWidthDefault 1280
#define WindowHeightDefault 720

using tstring = std::wstring;
using TouchedPointID = UINT64;

#define LIBWSACONTROLLER_EXPORT

#endif //PCH_H
