// #276: Ctrl+G goes to a line, in the editor and in the reading view
#include "d2d_init.h"
#include "editor.h"
#include "frame_preview.h"
#include "input.h"
#include "render.h"
#include "settings.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) { if (failures < 40) std::cerr << "FAIL go to line: " << message << '\n'; ++failures; }
}

void key(App& app, unsigned vk, bool ctrl = false) {
    BYTE original[256], pressed[256];
    GetKeyboardState(original);
    memcpy(pressed, original, sizeof pressed);
    pressed[VK_CONTROL] = ctrl ? 0x80 : 0;
    pressed[VK_SHIFT] = pressed[VK_MENU] = 0;
    SetKeyboardState(pressed);
    handleKeyDown(app, app.hwnd, vk);
    SetKeyboardState(original);
}

// What Windows sends for typed text: a key-down, then its character
void type(App& app, const std::wstring& text) {
    for (wchar_t c : text) {
        key(app, (unsigned)towupper(c));
        handleCharInput(app, app.hwnd, c);
    }
}

void paint(App& app) {
    app.renderTarget->BeginDraw();
    if (app.editMode) renderEditor(app, editorPaneWidth(app));
    renderGotoLinePrompt(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "the view and the prompt paint");
}

// One frame with the prompt over it, saved beside the test for inspection
void savePreview(App& app, const std::string& name) {
    check(saveFramePreview(app, std::filesystem::current_path() / (name + ".png"),
                           [&] { renderGotoLinePrompt(app); }),
          "a preview is saved");
}

std::string previewName(const App& app, const char* view, int width) {
    return std::string(view) + "-" + std::to_string(app.currentThemeIndex) + "-" + std::to_string(width);
}

size_t caretLine(const App& app) {
    size_t line = 0;
    while (line + 1 < app.editorLineStarts.size() && app.editorLineStarts[line + 1] <= app.editorCursorPos) ++line;
    return line + 1;
}

void inEditor(App& app, const char* path, int width, bool wrap) {
    const std::string where = " (" + std::to_string(width) + (wrap ? ", wrap)" : ")");
    app.width = width;
    app.currentFile = path;
    enterEditMode(app);
    app.editorShowPreview = true;
    app.editorWordWrap = wrap;
    rebuildLineStarts(app);
    app.editorScrollY = 0;
    paint(app);
    const size_t lines = app.editorLineStarts.size();
    for (size_t target : {120u, 41u, 7u, 190u}) {
        key(app, 'G', true);
        check(app.gotoLineOpen, "Ctrl+G opens the prompt" + where);
        handleCharInput(app, app.hwnd, 0x07);  // Ctrl+G's own character
        type(app, std::to_wstring(target));
        paint(app);
        const std::string preview = previewName(app, wrap ? "editor-wrap" : "editor", width);
        if (target == 120) savePreview(app, preview + "-prompt");
        const std::wstring before = app.editorText;
        key(app, VK_RETURN);
        handleCharInput(app, app.hwnd, L'\r');  // Enter's character follows
        check(!app.gotoLineOpen, "Enter closes the prompt" + where);
        check(app.editorText == before, "Enter's character does not reach the text" + where);
        check(caretLine(app) == target, "the caret lands on line " + std::to_string(target) + where);
        paint(app);
        if (target == 120) savePreview(app, preview + "-jump");
        D2D1_POINT_2F point{};
        check(editorCaretPoint(app, point) && point.y >= chromeTopHeight(app) && point.y < app.height,
              "the line is on screen" + where);
    }
    // Out of range clamps; typed letters are ignored; Backspace edits
    key(app, 'G', true);
    type(app, L"99999");
    key(app, VK_RETURN);
    check(caretLine(app) == lines, "a number past the end goes to the last line" + where);
    key(app, 'G', true);
    type(app, L"0");
    key(app, VK_RETURN);
    check(caretLine(app) == 1, "zero goes to the first line" + where);
    key(app, 'G', true);
    type(app, L"5a");
    check(app.gotoLineInput == L"5", "letters are not typed into the prompt" + where);
    key(app, VK_BACK);
    check(app.gotoLineInput.empty(), "Backspace removes a digit" + where);
    type(app, L"33");
    key(app, VK_ESCAPE);
    check(!app.gotoLineOpen && caretLine(app) == 1, "Esc closes the prompt without moving" + where);
    // A click elsewhere puts it away
    key(app, 'G', true);
    handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM(300, 400));
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM(300, 400));
    check(!app.gotoLineOpen, "a click elsewhere closes the prompt" + where);
    // Nothing above typed into the document; if it did, the next run's
    // keys would land on the save question instead
    check(!app.editorDirty, "the prompt leaves the document unchanged" + where);
    app.editorDirty = false;
    exitEditMode(app);
}

// The reading view scrolls to where the line is drawn: each fixture
// heading names its own line, `added` lines further down when lines were
// added above it
void jumpToHeadings(App& app, const char* view, int width, size_t added) {
    const std::string where = " (" + std::string(view) + ", " + std::to_string(width) + ")";
    ensureLayoutComplete(app);
    const float maxScroll = std::max(0.0f, app.contentHeight - (float)app.height);
    int checked = 0;
    for (const auto& heading : app.headings) {
        const std::wstring prefix = L"Section at line ";
        if (heading.text.rfind(prefix, 0) != 0) continue;
        const std::wstring named = heading.text.substr(prefix.size());
        const std::wstring line = std::to_wstring(std::stoul(named) + added);
        app.scrollY = app.targetScrollY = 0;
        key(app, 'G', true);
        check(app.gotoLineOpen, "Ctrl+G opens the prompt" + where);
        type(app, line);
        paint(app);
        const bool preview = named == L"101";
        if (preview) savePreview(app, previewName(app, view, width) + "-prompt");
        key(app, VK_RETURN);
        if (preview) savePreview(app, previewName(app, view, width) + "-jump");
        const float expected = std::clamp(heading.y - documentContentTop(app), 0.0f, maxScroll);
        check(std::abs(app.scrollY - expected) <= 40.0f,
              "the view scrolls to the heading on line " + toUtf8(line) + where + ": at " +
              std::to_string((int)app.scrollY) + ", the heading at " + std::to_string((int)expected));
        ++checked;
    }
    check(checked >= 20, "the fixture's headings are reached" + where);
}

void inReader(App& app, const char* path, int width) {
    app.width = width;
    app.currentFile = path;
    enterEditMode(app);
    exitEditMode(app);
    jumpToHeadings(app, "reading", width, 0);
}

// Edit mode's reading view shows the editor's text, edits the viewer's
// source has not seen included: lines added at the top move every heading
void inEditModeReading(App& app, const char* path, int width) {
    app.width = width;
    app.currentFile = path;
    enterEditMode(app);
    const size_t added = 10;
    for (size_t i = 0; i < added; ++i) app.editorText.insert(0, L"An added line\n");
    rebuildLineStarts(app);
    app.editorDirty = true;
    setEditorReadingPreview(app, true);
    check(gotoLineCount(app) == app.editorLineStarts.size(), "the prompt counts the editor's lines (edit-mode reading)");
    jumpToHeadings(app, "edit-reading", width, added);
    app.editorDirty = false;
    exitEditMode(app);
}
}  // namespace

int runGotoLineTests(const char* path) {
    // The reading-view checks trust the fixture: each heading on the line
    // it names
    {
        std::ifstream file(path);
        std::string text;
        int number = 0, named = 0;
        while (std::getline(file, text)) {
            ++number;
            const std::string prefix = "## Section at line ";
            if (text.rfind(prefix, 0) != 0) continue;
            check(text.substr(prefix.size()) == std::to_string(number),
                  "the fixture's heading on line " + std::to_string(number) + " names its own line");
            ++named;
        }
        check(named >= 20, "the fixture has its numbered headings");
    }
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Go to line tests", WS_POPUP, 0, 0, 1050, 900,
                               nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 900;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650}) {
            for (bool wrap : {false, true}) inEditor(app, path, width, wrap);
            inReader(app, path, width);
            inEditModeReading(app, path, width);
        }
    }
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Go to line: " << failures << " failures\n";
    return failures ? 1 : 0;
}
