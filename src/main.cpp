#include "app.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    try {
        const auto options = mtmb::win32::parse_options(GetCommandLineW());
        mtmb::Application application(instance);
        return application.run(options);
    } catch (const std::exception&) {
        MessageBoxW(nullptr, L"初期化または処理中にエラーが発生したため終了します。",
                    mtmb::kAppName, MB_OK | MB_ICONERROR);
        return 1;
    }
}
