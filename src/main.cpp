#include "app.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    try {
        const auto options = mtmb::win32::parse_options(GetCommandLineW());
        mtmb::Application application(instance);
        return application.run(options);
    } catch (const std::exception&) {
        const auto preference = mtmb::load_language_preference();
        const auto language =
            preference == mtmb::Language::System ? mtmb::system_ui_language() : preference;
        MessageBoxW(nullptr, mtmb::translate(language, mtmb::Text::FatalError), mtmb::kAppName,
                    MB_OK | MB_ICONERROR);
        return 1;
    }
}
