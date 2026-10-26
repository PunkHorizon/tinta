// Keys and typing for tests, delivered the way the main message loop
// delivers them (main_d2d.cpp, #195): the key-down reaches handleKeyDown
// first, and only a key it leaves unconsumed is translated into WM_CHAR.
// Tests go through here instead of calling handleCharInput themselves.
// Characters handed over regardless of the key-down, or with no key-down
// at all, hide a handler that consumes keys it should let through: Ctrl+G's
// prompt swallowed every digit that way while its tests typed into it
// (#282). input_tests fails on a direct handleCharInput call elsewhere.
#pragma once

#include "app.h"
#include "input.h"

#include <windows.h>

#include <cstring>
#include <string>

struct KeyMods {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

// What TranslateMessage makes of a key on the US layout: the character it
// types, a control character for Ctrl with a letter, or 0 for none. Alt
// keys arrive as WM_SYSKEYDOWN, and Ctrl+Alt is AltGr, which differs by
// layout.
inline wchar_t usLayoutCharacter(unsigned vk, KeyMods mods) {
    if (mods.alt) return 0;
    if (vk >= 'A' && vk <= 'Z') {
        if (mods.ctrl) return (wchar_t)(vk - 'A' + 1);
        return (wchar_t)(mods.shift ? vk : vk - 'A' + 'a');
    }
    if (mods.ctrl) {
        switch (vk) {
            case VK_RETURN: return L'\n';
            case VK_BACK: return 0x7F;
            case VK_SPACE: return L' ';
            case VK_OEM_4: return 0x1B;
            case VK_OEM_5: return 0x1C;
            case VK_OEM_6: return 0x1D;
            default: return 0;
        }
    }
    if (vk >= '0' && vk <= '9') return mods.shift ? L")!@#$%^&*("[vk - '0'] : (wchar_t)vk;
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return (wchar_t)(L'0' + (vk - VK_NUMPAD0));
    switch (vk) {
        case VK_SPACE: return L' ';
        case VK_RETURN: return L'\r';
        case VK_TAB: return L'\t';
        case VK_BACK: return 0x08;
        case VK_ESCAPE: return 0x1B;
        case VK_MULTIPLY: return L'*';
        case VK_ADD: return L'+';
        case VK_SUBTRACT: return L'-';
        case VK_DECIMAL: return L'.';
        case VK_DIVIDE: return L'/';
    }
    static const struct { unsigned vk; wchar_t plain, shifted; } oem[] = {
        {VK_OEM_3, L'`', L'~'},      {VK_OEM_MINUS, L'-', L'_'}, {VK_OEM_PLUS, L'=', L'+'},
        {VK_OEM_4, L'[', L'{'},      {VK_OEM_6, L']', L'}'},     {VK_OEM_5, L'\\', L'|'},
        {VK_OEM_1, L';', L':'},      {VK_OEM_7, L'\'', L'"'},    {VK_OEM_COMMA, L',', L'<'},
        {VK_OEM_PERIOD, L'.', L'>'}, {VK_OEM_2, L'/', L'?'},
    };
    for (const auto& key : oem) {
        if (key.vk == vk) return mods.shift ? key.shifted : key.plain;
    }
    return 0;
}

// Marks "the character the US layout types for this key"
constexpr wchar_t kUsLayout = 0xFFFF;

// One key: its key-down with the modifiers held, then its character if the
// key-down left it unconsumed. The character is the US layout's unless one
// is given, to model another layout (0 for none). Returns whether the
// key-down consumed the key.
inline bool keystroke(App& app, unsigned vk, KeyMods mods = {}, wchar_t character = kUsLayout) {
    if (character == kUsLayout) character = usLayoutCharacter(vk, mods);
    BYTE original[256], pressed[256];
    GetKeyboardState(original);
    std::memcpy(pressed, original, sizeof pressed);
    pressed[VK_CONTROL] = mods.ctrl ? 0x80 : 0;
    pressed[VK_SHIFT] = mods.shift ? 0x80 : 0;
    pressed[VK_MENU] = mods.alt ? 0x80 : 0;
    SetKeyboardState(pressed);
    const bool consumed = handleKeyDown(app, app.hwnd, vk);
    if (!consumed && character) handleCharInput(app, app.hwnd, character);
    SetKeyboardState(original);
    return consumed;
}

// Typed text, each character a whole keystroke on the US layout's key for
// it, with Shift where that layout needs it. Characters the layout has no
// key for come as VK_PACKET, as injected and composed text does.
inline void typeKeys(App& app, const std::wstring& text) {
    for (wchar_t c : text) {
        const wchar_t typed = c == L'\n' ? L'\r' : c;  // Enter types a carriage return
        unsigned vk = VK_PACKET;
        bool shift = false;
        for (unsigned candidate = 1; candidate < 0xFF && vk == VK_PACKET; ++candidate) {
            if (candidate >= VK_NUMPAD0 && candidate <= VK_DIVIDE) continue;  // the main keys, as a typist would
            for (bool withShift : {false, true}) {
                if (usLayoutCharacter(candidate, {false, withShift, false}) == typed) {
                    vk = candidate;
                    shift = withShift;
                    break;
                }
            }
        }
        keystroke(app, vk, {false, shift, false}, typed);
    }
}
