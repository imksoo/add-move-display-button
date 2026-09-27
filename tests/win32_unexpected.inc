// Unused Win32 API doubles abort so tests cannot silently accept unexpected calls.
extern "C" {
__declspec(noreturn) void WINAPI ExitProcess(UINT) { std::cerr << "Unexpected API: ExitProcess\n"; std::abort(); }
HMODULE WINAPI GetModuleHandleW(LPCWSTR) { std::cerr << "Unexpected API: GetModuleHandleW\n"; std::abort(); }
HANDLE WINAPI CreateMutexW(void*, BOOL, LPCWSTR) { std::cerr << "Unexpected API: CreateMutexW\n"; std::abort(); }
BOOL WINAPI SetProcessDpiAwarenessContext(HANDLE) { std::cerr << "Unexpected API: SetProcessDpiAwarenessContext\n"; std::abort(); }
int WINAPI GetSystemMetricsForDpi(int, UINT) { std::cerr << "Unexpected API: GetSystemMetricsForDpi\n"; std::abort(); }
ATOM WINAPI RegisterClassExW(const WNDCLASSEXW*) { std::cerr << "Unexpected API: RegisterClassExW\n"; std::abort(); }
HWND WINAPI CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) { std::cerr << "Unexpected API: CreateWindowExW\n"; std::abort(); }
LRESULT WINAPI DefWindowProcW(HWND, UINT, WPARAM, LPARAM) { std::cerr << "Unexpected API: DefWindowProcW\n"; std::abort(); }
HWND WINAPI FindWindowW(LPCWSTR, LPCWSTR) { std::cerr << "Unexpected API: FindWindowW\n"; std::abort(); }
BOOL WINAPI GetClientRect(HWND, RECT*) { std::cerr << "Unexpected API: GetClientRect\n"; std::abort(); }
int WINAPI GetWindowTextW(HWND, LPWSTR, int) { std::cerr << "Unexpected API: GetWindowTextW\n"; std::abort(); }
BOOL WINAPI EnumDisplayMonitors(HDC, const RECT*, MONITORENUMPROC, LPARAM) { std::cerr << "Unexpected API: EnumDisplayMonitors\n"; std::abort(); }
BOOL WINAPI GetMonitorInfoW(HMONITOR, MONITORINFO*) { std::cerr << "Unexpected API: GetMonitorInfoW\n"; std::abort(); }
BOOL WINAPI PostMessageW(HWND, UINT, WPARAM, LPARAM) { std::cerr << "Unexpected API: PostMessageW\n"; std::abort(); }
HWINEVENTHOOK WINAPI SetWinEventHook(DWORD, DWORD, HMODULE, WINEVENTPROC, DWORD, DWORD, DWORD) { std::cerr << "Unexpected API: SetWinEventHook\n"; std::abort(); }
UINT WINAPI RegisterWindowMessageW(LPCWSTR) { std::cerr << "Unexpected API: RegisterWindowMessageW\n"; std::abort(); }
BOOL WINAPI GetMessageW(MSG*, HWND, UINT, UINT) { std::cerr << "Unexpected API: GetMessageW\n"; std::abort(); }
BOOL WINAPI TranslateMessage(const MSG*) { std::cerr << "Unexpected API: TranslateMessage\n"; std::abort(); }
LRESULT WINAPI DispatchMessageW(const MSG*) { std::cerr << "Unexpected API: DispatchMessageW\n"; std::abort(); }
void WINAPI PostQuitMessage(int) { std::cerr << "Unexpected API: PostQuitMessage\n"; std::abort(); }
BOOL WINAPI GetCursorPos(POINT*) { std::cerr << "Unexpected API: GetCursorPos\n"; std::abort(); }
BOOL WINAPI ScreenToClient(HWND, POINT*) { std::cerr << "Unexpected API: ScreenToClient\n"; std::abort(); }
BOOL WINAPI ClientToScreen(HWND, POINT*) { std::cerr << "Unexpected API: ClientToScreen\n"; std::abort(); }
BOOL WINAPI TrackMouseEvent(TRACKMOUSEEVENT*) { std::cerr << "Unexpected API: TrackMouseEvent\n"; std::abort(); }
BOOL WINAPI InvalidateRect(HWND, const RECT*, BOOL) { std::cerr << "Unexpected API: InvalidateRect\n"; std::abort(); }
HDC WINAPI BeginPaint(HWND, PAINTSTRUCT*) { std::cerr << "Unexpected API: BeginPaint\n"; std::abort(); }
BOOL WINAPI EndPaint(HWND, const PAINTSTRUCT*) { std::cerr << "Unexpected API: EndPaint\n"; std::abort(); }
int WINAPI FillRect(HDC, const RECT*, HBRUSH) { std::cerr << "Unexpected API: FillRect\n"; std::abort(); }
HCURSOR WINAPI LoadCursorW(HINSTANCE, LPCWSTR) { std::cerr << "Unexpected API: LoadCursorW\n"; std::abort(); }
HICON WINAPI LoadIconW(HINSTANCE, LPCWSTR) { std::cerr << "Unexpected API: LoadIconW\n"; std::abort(); }
BOOL WINAPI SystemParametersInfoW(UINT, UINT, void*, UINT) { std::cerr << "Unexpected API: SystemParametersInfoW\n"; std::abort(); }
DWORD WINAPI GetSysColor(int) { std::cerr << "Unexpected API: GetSysColor\n"; std::abort(); }
HMENU WINAPI CreatePopupMenu() { std::cerr << "Unexpected API: CreatePopupMenu\n"; std::abort(); }
BOOL WINAPI AppendMenuW(HMENU, UINT, uintptr_t, LPCWSTR) { std::cerr << "Unexpected API: AppendMenuW\n"; std::abort(); }
BOOL WINAPI DestroyMenu(HMENU) { std::cerr << "Unexpected API: DestroyMenu\n"; std::abort(); }
BOOL WINAPI TrackPopupMenuEx(HMENU, UINT, int, int, HWND, void*) { std::cerr << "Unexpected API: TrackPopupMenuEx\n"; std::abort(); }
HBRUSH WINAPI CreateSolidBrush(COLORREF) { std::cerr << "Unexpected API: CreateSolidBrush\n"; std::abort(); }
HPEN WINAPI CreatePen(int, int, COLORREF) { std::cerr << "Unexpected API: CreatePen\n"; std::abort(); }
BOOL WINAPI DeleteObject(HGDIOBJ) { std::cerr << "Unexpected API: DeleteObject\n"; std::abort(); }
HGDIOBJ WINAPI SelectObject(HDC, HGDIOBJ) { std::cerr << "Unexpected API: SelectObject\n"; std::abort(); }
HGDIOBJ WINAPI GetStockObject(int) { std::cerr << "Unexpected API: GetStockObject\n"; std::abort(); }
BOOL WINAPI Rectangle(HDC, int, int, int, int) { std::cerr << "Unexpected API: Rectangle\n"; std::abort(); }
BOOL WINAPI MoveToEx(HDC, int, int, POINT*) { std::cerr << "Unexpected API: MoveToEx\n"; std::abort(); }
BOOL WINAPI LineTo(HDC, int, int) { std::cerr << "Unexpected API: LineTo\n"; std::abort(); }
}

extern "C" {
HLOCAL WINAPI LocalFree(HLOCAL) { std::abort(); }
LPWSTR* WINAPI CommandLineToArgvW(LPCWSTR, int*) { std::abort(); }
HBRUSH WINAPI GetSysColorBrush(int) { std::abort(); }
LONG_PTR WINAPI SetWindowLongPtrW(HWND, int, LONG_PTR) { std::abort(); }
}
