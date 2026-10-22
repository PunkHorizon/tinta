// #273: Settings > Appearance > Show notifications. Off, the bottom-right
// information and confirmations stay away for good (across restarts);
// warnings, errors and messages that ask something still show.
#include "d2d_init.h"
#include "input.h"
#include "overlays.h"
#include "settings.h"
#include "settings_preview.h"
#include "signals.h"
#include "utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL notifications: " << message << '\n'; ++failures; }
}
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
}

// Clicks the Appearance section's Show notifications switch through the
// real settings overlay and returns its row's hit rectangle
D2D1_RECT_F clickNotificationsSwitch(App& app) {
    app.showSettings = true;
    app.settingsSection = 1;
    app.settingsAnimation = 1.0f;
    app.renderTarget->BeginDraw();
    renderSettingsOverlay(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "the Appearance settings draw");
    D2D1_RECT_F hitRect{};
    for (const auto& [rect, id] : app.settingsHits)
        if (id == SET_TOGGLE_NOTIFICATIONS) hitRect = rect;
    check(hitRect.right > hitRect.left, "Appearance offers the Show notifications switch");
    const LPARAM point = MAKELPARAM((int)((hitRect.left + hitRect.right) / 2),
                                    (int)((hitRect.top + hitRect.bottom) / 2));
    handleMouseDown(app, app.hwnd, MK_LBUTTON, point);
    handleMouseUp(app, app.hwnd, 0, point);
    return hitRect;
}

size_t chipsShown(const App& app) { return app.signalChips.size(); }
}  // namespace

int runNotificationSettingTests() {
    namespace fs = std::filesystem;
    const auto dir = fs::current_path();
    const std::string marker = read(dir / "settings.ini");
    if (marker.rfind("; Isolated notification setting test configuration", 0) != 0) {
        std::cerr << "Run through CTest's isolated notification setting wrapper\n";
        return 2;
    }
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Notification setting tests", WS_POPUP, 0, 0, 1050, 900,
                               nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 900;
    app.currentFile = (dir / "settings.ini").string();  // a document, not the start page
    updateTextFormats(app);

    // On by default, as before
    check(app.showNotifications && loadSettings().showNotifications, "notifications are on by default");
    signalPushKey(app, SIG_SUCCESS, SIGI_COPY, "toast.copied");
    signalHintKey(app, SIGI_INFO, "toast.copied");
    check(chipsShown(app) == 2, "a confirmation and a hint show while on");

    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650}) {
            app.width = width;
            app.showSettings = true;
            app.settingsSection = 1;
            app.settingsAnimation = 1.0f;
            check(saveSettingsPreview(app, dir / ("appearance-" + std::to_string(theme) + "-" +
                                                  std::to_string(width) + ".png")),
                  "the Appearance section renders to a preview");
            app.renderTarget->BeginDraw();
            renderSettingsOverlay(app);
            app.renderTarget->EndDraw();
            const D2D1_RECT_F panel = settingsPanelRect(app);
            for (const auto& [rect, id] : app.settingsHits)
                if (id == SET_TOGGLE_NOTIFICATIONS)
                    // The switch sits mid-card: the card ends 19 DIPs below
                    // it, the footer text starts 32 DIPs above the bottom
                    check(rect.bottom + dpi(app, 19.0f) <= panel.bottom - dpi(app, 32.0f) + 0.5f,
                          "the Show notifications row stays clear of the settings footer");
        }
    }
    applyTheme(app, 0);
    app.width = 1050;

    // Off through the switch: the ones up go, and it is saved at once
    clickNotificationsSwitch(app);
    check(!app.showNotifications, "the switch turns notifications off");
    check(chipsShown(app) == 0, "turning them off clears the confirmations already up");
    check(read(dir / "settings.ini").find("showNotifications=0") != std::string::npos &&
              !loadSettings().showNotifications,
          "the choice is saved for the next launch and for other windows");

    // Off: information and confirmations stay away
    signalPushKey(app, SIG_SUCCESS, SIGI_COPY, "toast.copied");
    signalPushKey(app, SIG_INFO, SIGI_INFO, "toast.copied");
    signalHintKey(app, SIGI_INFO, "toast.copied");
    const D2D1_RECT_F anchor = D2D1::RectF(40, 300, 600, 360);
    signalPush(app, SIG_SUCCESS, SIGI_CHECK, L"Copied", L"", L"", SIGA_NONE, SIGC_NONE, true, &anchor);
    copyFilePath(app, app.hwnd, (dir / "settings.ini").string());
    check(chipsShown(app) == 0, "copy confirmations, information and hints stay away");

    // ...while anything that matters still shows
    signalPushKey(app, SIG_WARN, SIGI_WARNING, "toast.copied");
    check(chipsShown(app) == 1, "a warning still shows");
    signalPushKey(app, SIG_ERROR, SIGI_WARNING, "toast.file_missing");
    check(chipsShown(app) == 2, "an error still shows");
    signalPush(app, SIG_INFO, SIGI_FILE, L"Save failed", L"", L"", SIGA_RETRY_SAVE);
    check(chipsShown(app) == 3, "a message with an action still shows");
    signalPush(app, SIG_INFO, SIGI_FILE, L"Drafts found", L"", L"", SIGA_NONE, SIGC_NONE, false);
    check(chipsShown(app) == 4, "a message that waits for an answer still shows");

    // Back on
    app.signalChips.clear();
    clickNotificationsSwitch(app);
    check(app.showNotifications && loadSettings().showNotifications, "the switch turns them back on, saved");
    signalPushKey(app, SIG_SUCCESS, SIGI_COPY, "toast.copied");
    check(chipsShown(app) == 1, "confirmations show again");

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Notification setting: " << failures << " failures\n";
    return failures ? 1 : 0;
}
