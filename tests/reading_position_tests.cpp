// #279: a document left for a new note, the start page or an untitled tab
// keeps its reading position, so coming back resumes there rather than at
// the place saved the last time another file took over or it was closed
#include "d2d_init.h"
#include "editor.h"
#include "frame_preview.h"
#include "input.h"
#include "render.h"
#include "settings.h"
#include "startpage.h"
#include "tabs.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) { std::cerr << "FAIL reading position: " << message << '\n'; ++failures; }
}

void key(App& app, unsigned vk, bool ctrl) {
    BYTE original[256], pressed[256];
    GetKeyboardState(original);
    memcpy(pressed, original, sizeof pressed);
    pressed[VK_CONTROL] = ctrl ? 0x80 : 0;
    pressed[VK_SHIFT] = pressed[VK_MENU] = 0;
    SetKeyboardState(pressed);
    handleKeyDown(app, app.hwnd, vk);
    SetKeyboardState(original);
}

// What the window's paint does once the layout reaches a saved position
void settle(App& app) {
    ensureLayoutComplete(app);
    if (app.pendingScrollRestore >= 0.0f) {
        app.scrollY = app.targetScrollY = app.pendingScrollRestore;
        app.pendingScrollRestore = -1.0f;
    }
}

int tabOf(const App& app, const std::string& path) {
    for (size_t i = 0; i < app.tabs.size(); ++i) {
        const std::string& tabPath = (int)i == app.activeTab ? app.currentFile : app.tabs[i].path;
        if (_stricmp(tabPath.c_str(), path.c_str()) == 0) return (int)i;
    }
    return -1;
}

// Scrolls the document to a share of its length, in whole pixels
float scrollToShare(App& app, float share) {
    settle(app);
    const float y = std::round(std::max(0.0f, app.contentHeight - (float)app.height) * share);
    app.scrollY = app.targetScrollY = y;
    return y;
}

void expectAt(const App& app, float y, const std::string& after) {
    check(std::abs(app.scrollY - y) <= 1.0f, "the document resumes where it was left after " + after +
          ": at " + std::to_string((int)app.scrollY) + ", left at " + std::to_string((int)y));
}

// Back to the document's tab, as a click on it does
void expectResume(App& app, const std::string& path, float y, const std::string& after) {
    const int tab = tabOf(app, path);
    check(tab >= 0, "the document's tab is still open after " + after);
    if (tab < 0) return;
    tabActivate(app, app.hwnd, tab);
    check(app.currentFile == path, "its tab shows the document again after " + after);
    settle(app);
    expectAt(app, y, after);
}

void scenario(App& app, const std::string& document, const std::string& other, int width) {
    const std::string where = " (" + std::to_string(width) + ")";
    app.width = width;
    check(openDocumentInViewer(app, toWide(document)), "the fixture opens" + where);
    tabsSeedSession(app, {document, other});
    check(app.tabs.size() == 2 && app.activeTab == 0, "two file tabs, the fixture active" + where);
    settle(app);
    check(app.contentHeight > app.height * 4.0f, "the fixture is several screens long" + where);

    // Another file takes over and gives the page back. That worked, and it
    // leaves a saved position for the cases below to go stale against
    float y = scrollToShare(app, 0.25f);
    tabActivate(app, app.hwnd, tabOf(app, other));
    expectResume(app, document, y, "another file" + where);

    // The reported case: a new note, nothing saved, then back
    y = scrollToShare(app, 0.6f);
    const std::filesystem::path previews = std::filesystem::current_path();
    check(saveFramePreview(app, previews / ("left-" + std::to_string(width) + ".png")), "a preview is saved");
    key(app, 'N', true);
    const int note = app.activeTab;
    check(app.editMode && app.currentFile.empty() && note == (int)app.tabs.size() - 1,
          "Ctrl+N opens an untitled note in a new tab" + where);
    expectResume(app, document, y, "a new note (Ctrl+N)" + where);
    check(saveFramePreview(app, previews / ("returned-" + std::to_string(width) + ".png")), "a preview is saved");

    // The start page in a new tab
    y = scrollToShare(app, 0.4f);
    key(app, 'T', true);
    check(startPageActive(app), "Ctrl+T opens the start page" + where);
    expectResume(app, document, y, "the start page (Ctrl+T)" + where);

    // The note's tab, already open
    y = scrollToShare(app, 0.8f);
    tabActivate(app, app.hwnd, note);
    check(app.editMode && app.currentFile.empty(), "the note's tab is active" + where);
    expectResume(app, document, y, "the note's tab" + where);

    // Left again before the layout reached the saved place, as a quick run
    // through the tabs does: the place is kept, not the top of the page the
    // view showed meanwhile
    y = scrollToShare(app, 0.5f);
    tabActivate(app, app.hwnd, tabOf(app, other));
    tabActivate(app, app.hwnd, tabOf(app, document));
    check(app.pendingScrollRestore >= 0.0f, "the saved place waits for the layout" + where);
    tabActivate(app, app.hwnd, tabOf(app, other));
    expectResume(app, document, y, "leaving at once for another file" + where);
    tabActivate(app, app.hwnd, tabOf(app, other));
    tabActivate(app, app.hwnd, tabOf(app, document));
    key(app, 'N', true);
    expectResume(app, document, y, "leaving at once for a new note" + where);

    // Closing the last tab shows the start page; the document opened from
    // there again resumes where it was
    for (int i = (int)app.tabs.size() - 1; i >= 0; --i) {
        if (i != tabOf(app, document)) tabCloseIndex(app, app.hwnd, i);
    }
    check(app.tabs.size() == 1 && app.currentFile == document, "only the document's tab is left" + where);
    y = scrollToShare(app, 0.3f);
    tabCloseIndex(app, app.hwnd, app.activeTab);
    check(startPageActive(app), "closing the last tab shows the start page" + where);
    tabOpenPath(app, app.hwnd, document);
    check(app.currentFile == document, "the document opens again" + where);
    settle(app);
    expectAt(app, y, "its tab was closed" + where);
}
}  // namespace

int runReadingPositionTests(const char* fixture) {
    namespace fs = std::filesystem;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const fs::path dir = fs::path(exe).parent_path();
    // Positions are written to settings.ini: only the one beside the test
    if (!fs::equivalent(tintaConfigDir(), dir)) {
        std::cerr << "Run through CTest's isolated reading position wrapper\n";
        return 2;
    }
    const fs::path other = dir / "another-file.md";
    fs::copy_file(fixture, other, fs::copy_options::overwrite_existing);

    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Reading position tests", WS_POPUP, 0, 0, 1050, 900,
                               nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 900;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    applyTheme(app, 0);
    const std::string document = toUtf8(fs::absolute(fixture).wstring());
    for (int width : {1050, 650}) scenario(app, document, toUtf8(other.wstring()), width);
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Reading position: " << failures << " failures\n";
    return failures ? 1 : 0;
}
