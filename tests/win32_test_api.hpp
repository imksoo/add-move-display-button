#pragma once
// TEST DOUBLES ONLY: native host wchar_t and fake handles, NOT a Windows ABI.
// Production and Windows integration tests include the official SDK instead.
#include <stdint.h>
#include <stddef.h>
#define WINAPI __stdcall
#define CALLBACK __stdcall
#define API __declspec(dllimport)
using BOOL = int; using BYTE = unsigned char; using WORD = unsigned short;
using DWORD = uint32_t; using LONG = int32_t; using UINT = unsigned int;
using ULONG_PTR = uintptr_t; using DWORD_PTR = uintptr_t; using LONG_PTR = intptr_t;
using WPARAM = uintptr_t; using LPARAM = intptr_t; using LRESULT = intptr_t;
using ATOM = unsigned short; using ULONGLONG = unsigned long long;
using HANDLE = void*; using HWND = HANDLE; using HINSTANCE = HANDLE; using HMODULE = HANDLE;
using HDC = HANDLE; using HMONITOR = HANDLE; using HICON = HANDLE; using HCURSOR = HANDLE;
using HBRUSH = HANDLE; using HPEN = HANDLE; using HGDIOBJ = HANDLE; using HMENU = HANDLE;
using HWINEVENTHOOK = HANDLE; using COLORREF = DWORD;
using LPCWSTR = const wchar_t*; using LPWSTR = wchar_t*; using LPCSTR = const char*;
using LPVOID = void*; using HRESULT = LONG;
struct POINT { LONG x, y; };
struct RECT { LONG left, top, right, bottom; };
struct MSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; DWORD lPrivate; };
using WNDPROC = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);
using WINEVENTPROC = void(CALLBACK*)(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
using MONITORENUMPROC = BOOL(CALLBACK*)(HMONITOR, HDC, RECT*, LPARAM);
using TIMERPROC = void(CALLBACK*)(HWND, UINT, uintptr_t, DWORD);
struct WNDCLASSEXW {
    UINT cbSize, style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra;
    HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground;
    LPCWSTR lpszMenuName, lpszClassName; HICON hIconSm;
};
struct PAINTSTRUCT { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate; BYTE rgbReserved[32]; };
struct MONITORINFO { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; };
struct MONITORINFOEXW { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; wchar_t szDevice[32]; };
struct WINDOWPLACEMENT { UINT length, flags, showCmd; POINT ptMinPosition, ptMaxPosition; RECT rcNormalPosition; };
struct GUID { DWORD Data1; WORD Data2, Data3; BYTE Data4[8]; };
struct NOTIFYICONDATAW {
    DWORD cbSize; HWND hWnd; UINT uID, uFlags, uCallbackMessage; HICON hIcon;
    wchar_t szTip[128]; DWORD dwState, dwStateMask; wchar_t szInfo[256];
    union { UINT uTimeout; UINT uVersion; }; wchar_t szInfoTitle[64]; DWORD dwInfoFlags;
    GUID guidItem; HICON hBalloonIcon;
};
struct TRACKMOUSEEVENT { DWORD cbSize, dwFlags; HWND hwndTrack; DWORD dwHoverTime; };
struct GUITHREADINFO { DWORD cbSize, flags; HWND hwndActive, hwndFocus, hwndCapture,
    hwndMenuOwner, hwndMoveSize, hwndCaret; RECT rcCaret; };
struct HIGHCONTRASTW { UINT cbSize; DWORD dwFlags; LPWSTR lpszDefaultScheme; };
struct TOKEN_ELEVATION { DWORD TokenIsElevated; };
enum TOKEN_INFORMATION_CLASS { TokenElevation = 20 };
extern "C" {
API __declspec(noreturn) void WINAPI ExitProcess(UINT);
API HMODULE WINAPI GetModuleHandleW(LPCWSTR);
API DWORD WINAPI GetCurrentProcessId();
API DWORD WINAPI GetLastError();
API void WINAPI SetLastError(DWORD);
API HANDLE WINAPI OpenProcess(DWORD, BOOL, DWORD);
API BOOL WINAPI OpenProcessToken(HANDLE, DWORD, HANDLE*);
API BOOL WINAPI GetTokenInformation(HANDLE, TOKEN_INFORMATION_CLASS, void*, DWORD, DWORD*);
API HANDLE WINAPI CreateMutexW(void*, BOOL, LPCWSTR);
API BOOL WINAPI CloseHandle(HANDLE);
API LPCWSTR WINAPI GetCommandLineW();
API ULONGLONG WINAPI GetTickCount64();
API void WINAPI OutputDebugStringW(LPCWSTR);
API BOOL WINAPI SetProcessDpiAwarenessContext(HANDLE);
API UINT WINAPI GetDpiForWindow(HWND);
API int WINAPI GetSystemMetricsForDpi(int, UINT);
API ATOM WINAPI RegisterClassExW(const WNDCLASSEXW*);
API HWND WINAPI CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
API BOOL WINAPI DestroyWindow(HWND);
API LRESULT WINAPI DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
API HWND WINAPI FindWindowW(LPCWSTR, LPCWSTR);
API HWND WINAPI GetForegroundWindow();
API BOOL WINAPI SetForegroundWindow(HWND);
API HWND WINAPI GetAncestor(HWND, UINT);
API HWND WINAPI GetWindow(HWND, UINT);
API HWND WINAPI WindowFromPoint(POINT);
API LONG_PTR WINAPI GetWindowLongPtrW(HWND, int);
API DWORD WINAPI GetWindowThreadProcessId(HWND, DWORD*);
API BOOL WINAPI IsWindow(HWND);
API BOOL WINAPI IsWindowVisible(HWND);
API BOOL WINAPI IsWindowEnabled(HWND);
API BOOL WINAPI IsIconic(HWND);
API BOOL WINAPI IsZoomed(HWND);
API BOOL WINAPI IsHungAppWindow(HWND);
API BOOL WINAPI GetWindowRect(HWND, RECT*);
API BOOL WINAPI GetClientRect(HWND, RECT*);
API BOOL WINAPI GetWindowPlacement(HWND, WINDOWPLACEMENT*);
API BOOL WINAPI SetWindowPlacement(HWND, const WINDOWPLACEMENT*);
API BOOL WINAPI ShowWindowAsync(HWND, int);
API BOOL WINAPI ShowWindow(HWND, int);
API BOOL WINAPI SetWindowPos(HWND, HWND, int, int, int, int, UINT);
API BOOL WINAPI GetGUIThreadInfo(DWORD, GUITHREADINFO*);
API int WINAPI GetClassNameW(HWND, LPWSTR, int);
API int WINAPI GetWindowTextW(HWND, LPWSTR, int);
API BOOL WINAPI EnumDisplayMonitors(HDC, const RECT*, MONITORENUMPROC, LPARAM);
API BOOL WINAPI GetMonitorInfoW(HMONITOR, MONITORINFO*);
API HMONITOR WINAPI MonitorFromWindow(HWND, DWORD);
API LRESULT WINAPI SendMessageTimeoutW(HWND, UINT, WPARAM, LPARAM, UINT, UINT, DWORD_PTR*);
API BOOL WINAPI PostMessageW(HWND, UINT, WPARAM, LPARAM);
API HWINEVENTHOOK WINAPI SetWinEventHook(DWORD, DWORD, HMODULE, WINEVENTPROC, DWORD, DWORD, DWORD);
API BOOL WINAPI UnhookWinEvent(HWINEVENTHOOK);
API UINT WINAPI RegisterWindowMessageW(LPCWSTR);
API uintptr_t WINAPI SetTimer(HWND, uintptr_t, UINT, TIMERPROC);
API BOOL WINAPI KillTimer(HWND, uintptr_t);
API BOOL WINAPI GetMessageW(MSG*, HWND, UINT, UINT);
API BOOL WINAPI TranslateMessage(const MSG*);
API LRESULT WINAPI DispatchMessageW(const MSG*);
API void WINAPI PostQuitMessage(int);
API BOOL WINAPI GetCursorPos(POINT*);
API BOOL WINAPI ScreenToClient(HWND, POINT*);
API BOOL WINAPI ClientToScreen(HWND, POINT*);
API BOOL WINAPI TrackMouseEvent(TRACKMOUSEEVENT*);
API HWND WINAPI SetCapture(HWND);
API BOOL WINAPI ReleaseCapture();
API HWND WINAPI GetCapture();
API BOOL WINAPI InvalidateRect(HWND, const RECT*, BOOL);
API HDC WINAPI BeginPaint(HWND, PAINTSTRUCT*);
API BOOL WINAPI EndPaint(HWND, const PAINTSTRUCT*);
API int WINAPI FillRect(HDC, const RECT*, HBRUSH);
API HCURSOR WINAPI LoadCursorW(HINSTANCE, LPCWSTR);
API HICON WINAPI LoadIconW(HINSTANCE, LPCWSTR);
API int WINAPI MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT);
API BOOL WINAPI SystemParametersInfoW(UINT, UINT, void*, UINT);
API DWORD WINAPI GetSysColor(int);
API HMENU WINAPI CreatePopupMenu();
API BOOL WINAPI AppendMenuW(HMENU, UINT, uintptr_t, LPCWSTR);
API BOOL WINAPI DestroyMenu(HMENU);
API BOOL WINAPI TrackPopupMenuEx(HMENU, UINT, int, int, HWND, void*);
API HBRUSH WINAPI CreateSolidBrush(COLORREF);
API HPEN WINAPI CreatePen(int, int, COLORREF);
API BOOL WINAPI DeleteObject(HGDIOBJ);
API HGDIOBJ WINAPI SelectObject(HDC, HGDIOBJ);
API HGDIOBJ WINAPI GetStockObject(int);
API BOOL WINAPI Rectangle(HDC, int, int, int, int);
API BOOL WINAPI MoveToEx(HDC, int, int, POINT*);
API BOOL WINAPI LineTo(HDC, int, int);
API BOOL WINAPI Shell_NotifyIconW(DWORD, NOTIFYICONDATAW*);
API HRESULT WINAPI DwmGetWindowAttribute(HWND, DWORD, void*, DWORD);
}
#define TRUE 1
#define FALSE 0
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GA_ROOT 2
#define WS_POPUP 0x80000000UL
#define WS_CHILD 0x40000000UL
#define WS_CAPTION 0x00C00000UL
#define WS_SYSMENU 0x00080000UL
#define WS_THICKFRAME 0x00040000UL
#define WS_MINIMIZEBOX 0x00020000UL
#define WS_MAXIMIZEBOX 0x00010000UL
#define WS_EX_TOOLWINDOW 0x00000080UL
#define WS_EX_TOPMOST 0x00000008UL
#define WS_EX_NOACTIVATE 0x08000000UL
#define WS_EX_LAYOUTRTL 0x00400000UL
#define HWND_TOPMOST reinterpret_cast<HWND>(static_cast<intptr_t>(-1))
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4))
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOWMAXIMIZED 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_RESTORE 9
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOACTIVATE 0x0010
#define SWP_SHOWWINDOW 0x0040
#define SWP_NOOWNERZORDER 0x0200
#define SWP_ASYNCWINDOWPOS 0x4000
#define WPF_ASYNCWINDOWPLACEMENT 0x0004
#define SMTO_BLOCK 0x0001
#define SMTO_ABORTIFHUNG 0x0002
#define HTCAPTION 2
#define MA_NOACTIVATE 3
#define MONITOR_DEFAULTTONEAREST 2
#define MONITORINFOF_PRIMARY 1
#define WM_NULL 0x0000
#define WM_DESTROY 0x0002
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_ENDSESSION 0x0016
#define WM_ERASEBKGND 0x0014
#define WM_SETTINGCHANGE 0x001A
#define WM_MOUSEACTIVATE 0x0021
#define WM_CONTEXTMENU 0x007B
#define WM_DISPLAYCHANGE 0x007E
#define WM_NCHITTEST 0x0084
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_RBUTTONUP 0x0205
#define WM_CAPTURECHANGED 0x0215
#define WM_DPICHANGED 0x02E0
#define WM_MOUSELEAVE 0x02A3
#define WM_USER 0x0400
#define WM_APP 0x8000
#define TME_LEAVE 0x00000002
#define EVENT_SYSTEM_FOREGROUND 0x0003
#define EVENT_SYSTEM_MOVESIZESTART 0x000A
#define EVENT_SYSTEM_MOVESIZEEND 0x000B
#define EVENT_OBJECT_DESTROY 0x8001
#define EVENT_OBJECT_HIDE 0x8003
#define EVENT_OBJECT_LOCATIONCHANGE 0x800B
#define OBJID_WINDOW 0
#define WINEVENT_OUTOFCONTEXT 0x0000
#define WINEVENT_SKIPOWNPROCESS 0x0002
#define GUI_INMOVESIZE 0x0002
#define GUI_INMENUMODE 0x0004
#define GUI_SYSTEMMENUMODE 0x0008
#define GUI_POPUPMENUMODE 0x0010
#define SM_CYCAPTION 4
#define SM_CXSIZE 30
#define SM_CYSIZE 31
#define SM_CXSIZEFRAME 32
#define SM_CYSIZEFRAME 33
#define SM_CXPADDEDBORDER 92
#define DWMWA_CAPTION_BUTTON_BOUNDS 5
#define DWMWA_EXTENDED_FRAME_BOUNDS 9
#define DWMWA_CLOAKED 14
#define MB_OK 0x00000000UL
#define MB_ICONERROR 0x00000010UL
#define MB_ICONINFORMATION 0x00000040UL
#define ERROR_ALREADY_EXISTS 183
#define ERROR_SUCCESS 0
#define ERROR_ACCESS_DENIED 5
#define ERROR_TIMEOUT 1460
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define TOKEN_QUERY 0x0008
#define IDC_ARROW reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(32512))
#define IDI_APPLICATION reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(32512))
#define MF_STRING 0x0000
#define MF_GRAYED 0x0001
#define MF_CHECKED 0x0008
#define MF_SEPARATOR 0x0800
#define TPM_RIGHTBUTTON 0x0002
#define TPM_NONOTIFY 0x0080
#define TPM_RETURNCMD 0x0100
#define NIM_ADD 0x00000000
#define NIM_MODIFY 0x00000001
#define NIM_DELETE 0x00000002
#define NIM_SETVERSION 0x00000004
#define NIF_MESSAGE 0x00000001
#define NIF_ICON 0x00000002
#define NIF_TIP 0x00000004
#define NIF_INFO 0x00000010
#define NIF_SHOWTIP 0x00000080
#define NIIF_WARNING 0x00000002
#define NOTIFYICON_VERSION_4 4
#define NIN_SELECT (WM_USER + 0)
#define NIN_KEYSELECT (WM_USER + 1)
#define SPI_GETHIGHCONTRAST 0x0042
#define HCF_HIGHCONTRASTON 0x00000001
#define COLOR_BTNFACE 15
#define COLOR_BTNTEXT 18
#define COLOR_HIGHLIGHT 13
#define COLOR_HIGHLIGHTTEXT 14
#define PS_SOLID 0
#define NULL_BRUSH 5
#define RGB(r,g,b) (static_cast<COLORREF>((r) | ((g) << 8) | ((b) << 16)))

#ifndef HTCLIENT
#define HTCLIENT 1
#endif
#ifndef HTTRANSPARENT
#define HTTRANSPARENT (-1)
#endif
#ifndef GW_OWNER
#define GW_OWNER 4
#endif

using UINT_PTR = uintptr_t;
using HLOCAL = HANDLE;
struct CREATESTRUCTW {
    LPVOID lpCreateParams; HINSTANCE hInstance; HMENU hMenu; HWND hwndParent;
    int cy, cx, y, x; LONG style; LPCWSTR lpszName, lpszClass; DWORD dwExStyle;
};
extern "C" {
API HLOCAL WINAPI LocalFree(HLOCAL);
API LPWSTR* WINAPI CommandLineToArgvW(LPCWSTR, int*);
API HRESULT WINAPI StringCchCopyW(LPWSTR, size_t, LPCWSTR);
API HBRUSH WINAPI GetSysColorBrush(int);
API BOOL WINAPI EqualRect(const RECT*, const RECT*);
API BOOL WINAPI PtInRect(const RECT*, POINT);
API BOOL WINAPI OffsetRect(RECT*, int, int);
API LONG_PTR WINAPI SetWindowLongPtrW(HWND, int, LONG_PTR);
}
#define _countof(array) (sizeof(array) / sizeof((array)[0]))
#define MAKEINTRESOURCEW(id) reinterpret_cast<LPWSTR>(static_cast<uintptr_t>(static_cast<WORD>(id)))
#define MAKELPARAM(lo, hi) static_cast<LPARAM>(static_cast<DWORD>(static_cast<WORD>(lo)) | (static_cast<DWORD>(static_cast<WORD>(hi)) << 16))
#define LOWORD(value) static_cast<WORD>(static_cast<uintptr_t>(value) & 0xffffU)
#define HGDI_ERROR reinterpret_cast<HGDIOBJ>(static_cast<intptr_t>(-1))
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define GWLP_USERDATA (-21)
#define SUCCEEDED(hr) (static_cast<HRESULT>(hr) >= 0)
