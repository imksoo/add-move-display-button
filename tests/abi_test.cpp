#include "platform.hpp"
#include <stddef.h>
static_assert(sizeof(void*) == 8, "This release targets x64");
static_assert(sizeof(wchar_t) == 2);
static_assert(sizeof(DWORD) == 4);
static_assert(sizeof(RECT) == 16);
static_assert(sizeof(MSG) == 48);
static_assert(sizeof(WNDCLASSEXW) == 80);
static_assert(sizeof(PAINTSTRUCT) == 72);
static_assert(sizeof(MONITORINFOEXW) == 104);
static_assert(sizeof(WINDOWPLACEMENT) == 44);
static_assert(sizeof(NOTIFYICONDATAW) == 976);
static_assert(sizeof(TRACKMOUSEEVENT) == 24);
static_assert(sizeof(GUITHREADINFO) == 72);
static_assert(offsetof(WNDCLASSEXW, lpfnWndProc) == 8);
static_assert(offsetof(WNDCLASSEXW, hInstance) == 24);
static_assert(offsetof(MSG, wParam) == 16);
static_assert(offsetof(MSG, lParam) == 24);
static_assert(offsetof(NOTIFYICONDATAW, hWnd) == 8);
static_assert(offsetof(NOTIFYICONDATAW, hIcon) == 32);
static_assert(offsetof(NOTIFYICONDATAW, szTip) == 40);
static_assert(offsetof(NOTIFYICONDATAW, szInfo) == 304);
static_assert(offsetof(NOTIFYICONDATAW, uVersion) == 816);
static_assert(offsetof(NOTIFYICONDATAW, guidItem) == 952);
static_assert(offsetof(NOTIFYICONDATAW, hBalloonIcon) == 968);
static_assert(offsetof(WINDOWPLACEMENT, rcNormalPosition) == 28);
static_assert(WS_EX_NOACTIVATE == 0x08000000);
static_assert(WM_NCHITTEST == 0x0084);
static_assert(DWMWA_CAPTION_BUTTON_BOUNDS == 5);
static_assert(DWMWA_EXTENDED_FRAME_BOUNDS == 9);
static_assert(DWMWA_CLOAKED == 14);
static_assert(WPF_ASYNCWINDOWPLACEMENT == 4);
static_assert(sizeof(TOKEN_ELEVATION) == 4);
static_assert(TokenElevation == 20);
static_assert(PROCESS_QUERY_LIMITED_INFORMATION == 0x1000);
static_assert(TOKEN_QUERY == 0x0008);
int main() { return 0; }
