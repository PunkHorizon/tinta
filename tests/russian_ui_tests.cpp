#include "d2d_init.h"
#include "editor.h"
#include "i18n.h"
#include "input.h"
#include "overlays.h"
#include "print.h"
#include "settings.h"
#include "settings_preview.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

int main() {
    namespace fs = std::filesystem;
    const auto dir = fs::current_path();
    std::ifstream ini(dir / "settings.ini");
    std::string guard;
    std::getline(ini, guard);
    if (guard != "; Isolated Russian UI test configuration") return 2;
    ini.close();
    loadLanguageOverrides();
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Russian UI tests", WS_POPUP,
        0, 0, 1050, 900, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 1;
    applyTheme(app, app.currentThemeIndex);
    check(openDocumentInViewer(app, fs::path(TINTA_RUSSIAN_UI_FIXTURE).wstring()),
          "mixed Russian fixture opens in the real viewer");
    app.width = 1050;
    app.height = 900;
    app.showSettings = true;
    app.settingsAnimation = 1;
    app.settingsLangOpen = true;
    app.currentLanguageIndex = languageIndexById("en");
    app.languageSetting = app.currentLanguageIndex;
    const int russian = languageIndexById("ru");
    check(saveSettingsPreview(app, dir / "languages-with-russian.png"),
          "language chooser renders Russian");
    bool choseRussian = false;
    for (const auto hit : app.settingsHits) {
        if (hit.second != SET_LANG_PICK_BASE + russian + 1) continue;
        const auto point = MAKELPARAM((int)((hit.first.left + hit.first.right) / 2),
                                     (int)((hit.first.top + hit.first.bottom) / 2));
        handleMouseUp(app, app.hwnd, 0, point);
        choseRussian = true;
        break;
    }
    check(choseRussian && app.currentLanguageIndex == russian && app.languageSetting == russian,
          "Russian can be selected through the language dropdown");
    Settings stored = loadSettings();
    stored.language = languageIdAt(app.languageSetting);
    saveSettings(stored);
    check(loadSettings().language == "ru", "Russian preference persists by language ID");
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {650, 1050}) {
            app.width = width;
            for (int section : {0, 1, 2, 3}) {
                app.settingsSection = section;
                check(saveSettingsPreview(app, dir / ("russian-settings-" +
                    std::to_string(theme) + "-" + std::to_string(width) + "-" +
                    std::to_string(section) + ".png")),
                    "Russian settings sections render for visual inspection");
            }
        }
    }
    app.showSettings = false;
    check(printDebugPages(app, (dir / "fixture-pages").wstring()) > 0,
          "Russian fixture renders through the native print harness");
    DestroyWindow(app.hwnd);
    std::cout << "Russian UI: " << failures << " failures\n";
    return failures ? 1 : 0;
}
