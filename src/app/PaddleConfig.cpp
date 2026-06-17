#include "PaddleConfig.h"
#include <Windows.h>
#include <ShlObj.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

namespace {
constexpr wchar_t kConfigDirName[] = L"SteamControllerRemapper";
constexpr wchar_t kLegacyConfigDirName[] = L"XboxModeSteamlessController";

std::wstring Trim(std::wstring value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](wchar_t ch) {
        return !std::iswspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](wchar_t ch) {
        return !std::iswspace(ch);
    }).base(), value.end());
    return value;
}

std::wstring Upper(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towupper(ch));
    });
    return value;
}

std::wstring AppDataDirectory(const wchar_t* leafName) {
    wchar_t localAppData[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData);
    std::wstring dir = localAppData;
    dir += L"\\";
    dir += leafName;
    return dir;
}

void MigrateIfMissing(const std::wstring& targetPath, const std::wstring& legacyPath) {
    if (GetFileAttributesW(targetPath.c_str()) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(legacyPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        CopyFileW(legacyPath.c_str(), targetPath.c_str(), TRUE);
    }
}

std::wstring ConfigDirectory() {
    const std::wstring dir = AppDataDirectory(kConfigDirName);
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring legacyDir = AppDataDirectory(kLegacyConfigDirName);
    MigrateIfMissing(dir + L"\\paddles.ini", legacyDir + L"\\paddles.ini");
    MigrateIfMissing(dir + L"\\profiles.ini", legacyDir + L"\\profiles.ini");
    return dir;
}

std::wstring GetProfilesPath() {
    return ConfigDirectory() + L"\\profiles.ini";
}

bool SplitOnce(const std::wstring& input, wchar_t delim, std::wstring& left, std::wstring& right) {
    const size_t pos = input.find(delim);
    if (pos == std::wstring::npos)
        return false;
    left = Trim(input.substr(0, pos));
    right = Trim(input.substr(pos + 1));
    return true;
}

std::vector<std::wstring> Split(const std::wstring& input, wchar_t delim) {
    std::vector<std::wstring> parts;
    std::wstringstream stream(input);
    std::wstring item;
    while (std::getline(stream, item, delim))
        parts.push_back(Trim(item));
    return parts;
}

bool TryParseGamepad(const std::wstring& value, PaddleMapping& mapping) {
    const std::wstring upper = Upper(value);
    std::wstring compact;
    compact.reserve(upper.size());
    for (wchar_t ch : upper) {
        if (ch != L' ' && ch != L'\t' && ch != L'/' && ch != L'-')
            compact.push_back(ch);
    }

    auto matches = [&](std::initializer_list<const wchar_t*> options) {
        for (const wchar_t* option : options) {
            const std::wstring text = option;
            if (upper == text || compact == text)
                return true;
        }
        return false;
    };
    if (matches({ L"A", L"CROSS", L"ACROSS" })) { mapping = PaddleMapping::A; return true; }
    if (matches({ L"B", L"CIRCLE", L"BCIRCLE" })) { mapping = PaddleMapping::B; return true; }
    if (matches({ L"X", L"SQUARE", L"XSQUARE" })) { mapping = PaddleMapping::X; return true; }
    if (matches({ L"Y", L"TRIANGLE", L"YTRIANGLE" })) { mapping = PaddleMapping::Y; return true; }
    if (matches({ L"LB", L"LEFTSHOULDER" })) { mapping = PaddleMapping::LeftShoulder; return true; }
    if (matches({ L"RB", L"RIGHTSHOULDER" })) { mapping = PaddleMapping::RightShoulder; return true; }
    if (matches({ L"VIEW", L"BACK", L"SHARE", L"VIEWSHARE" })) { mapping = PaddleMapping::View; return true; }
    if (matches({ L"MENU", L"START", L"OPTIONS", L"MENUOPTIONS" })) { mapping = PaddleMapping::Menu; return true; }
    if (matches({ L"L3", L"LEFTTHUMB", L"LEFTSTICKCLICK" })) { mapping = PaddleMapping::LeftThumb; return true; }
    if (matches({ L"R3", L"RIGHTTHUMB", L"RIGHTSTICKCLICK" })) { mapping = PaddleMapping::RightThumb; return true; }
    if (matches({ L"GUIDE", L"PS", L"GUIDEPS" })) { mapping = PaddleMapping::Guide; return true; }
    if (matches({ L"DPADUP" })) { mapping = PaddleMapping::DPadUp; return true; }
    if (matches({ L"DPADRIGHT" })) { mapping = PaddleMapping::DPadRight; return true; }
    if (matches({ L"DPADDOWN" })) { mapping = PaddleMapping::DPadDown; return true; }
    if (matches({ L"DPADLEFT" })) { mapping = PaddleMapping::DPadLeft; return true; }
    if (matches({ L"NONE", L"UNMAPPED" })) { mapping = PaddleMapping::None; return true; }
    return false;
}

bool TryParseVirtualKey(const std::wstring& token, uint16_t& vk) {
    const std::wstring upper = Upper(token);
    if (upper.size() == 1 && upper[0] >= L'A' && upper[0] <= L'Z') { vk = static_cast<uint16_t>(upper[0]); return true; }
    if (upper.size() == 1 && upper[0] >= L'0' && upper[0] <= L'9') { vk = static_cast<uint16_t>(upper[0]); return true; }
    // Modifiers
    if (upper == L"CTRL" || upper == L"CONTROL") { vk = VK_CONTROL; return true; }
    if (upper == L"SHIFT") { vk = VK_SHIFT; return true; }
    if (upper == L"ALT") { vk = VK_MENU; return true; }
    if (upper == L"WIN" || upper == L"WINDOWS") { vk = VK_LWIN; return true; }
    if (upper == L"LCTRL" || upper == L"LCONTROL") { vk = VK_LCONTROL; return true; }
    if (upper == L"RCTRL" || upper == L"RCONTROL") { vk = VK_RCONTROL; return true; }
    if (upper == L"LSHIFT") { vk = VK_LSHIFT; return true; }
    if (upper == L"RSHIFT") { vk = VK_RSHIFT; return true; }
    if (upper == L"LALT") { vk = VK_LMENU; return true; }
    if (upper == L"RALT") { vk = VK_RMENU; return true; }
    if (upper == L"LWIN") { vk = VK_LWIN; return true; }
    if (upper == L"RWIN") { vk = VK_RWIN; return true; }
    if (upper == L"APPS" || upper == L"MENU") { vk = VK_APPS; return true; }
    // Navigation / editing
    if (upper == L"TAB") { vk = VK_TAB; return true; }
    if (upper == L"ENTER" || upper == L"RETURN") { vk = VK_RETURN; return true; }
    if (upper == L"SPACE") { vk = VK_SPACE; return true; }
    if (upper == L"ESC" || upper == L"ESCAPE") { vk = VK_ESCAPE; return true; }
    if (upper == L"BACKSPACE") { vk = VK_BACK; return true; }
    if (upper == L"UP") { vk = VK_UP; return true; }
    if (upper == L"RIGHT") { vk = VK_RIGHT; return true; }
    if (upper == L"DOWN") { vk = VK_DOWN; return true; }
    if (upper == L"LEFT") { vk = VK_LEFT; return true; }
    if (upper == L"HOME") { vk = VK_HOME; return true; }
    if (upper == L"END") { vk = VK_END; return true; }
    if (upper == L"PGUP" || upper == L"PAGEUP") { vk = VK_PRIOR; return true; }
    if (upper == L"PGDN" || upper == L"PAGEDOWN") { vk = VK_NEXT; return true; }
    if (upper == L"INSERT") { vk = VK_INSERT; return true; }
    if (upper == L"DELETE" || upper == L"DEL") { vk = VK_DELETE; return true; }
    // Lock keys
    if (upper == L"CAPSLOCK" || upper == L"CAPS") { vk = VK_CAPITAL; return true; }
    if (upper == L"NUMLOCK") { vk = VK_NUMLOCK; return true; }
    if (upper == L"SCROLLLOCK" || upper == L"SCRLK") { vk = VK_SCROLL; return true; }
    // System keys
    if (upper == L"PRINTSCREEN" || upper == L"PRTSC" || upper == L"PRTSCN") { vk = VK_SNAPSHOT; return true; }
    if (upper == L"PAUSE" || upper == L"BREAK") { vk = VK_PAUSE; return true; }
    // Function keys
    if (upper.size() >= 2 && upper[0] == L'F') {
        const int num = _wtoi(upper.c_str() + 1);
        if (num >= 1 && num <= 24) {
            vk = static_cast<uint16_t>(VK_F1 + (num - 1));
            return true;
        }
    }
    // Numpad
    if (upper == L"NUM0" || upper == L"NUMPAD0") { vk = VK_NUMPAD0; return true; }
    if (upper == L"NUM1" || upper == L"NUMPAD1") { vk = VK_NUMPAD1; return true; }
    if (upper == L"NUM2" || upper == L"NUMPAD2") { vk = VK_NUMPAD2; return true; }
    if (upper == L"NUM3" || upper == L"NUMPAD3") { vk = VK_NUMPAD3; return true; }
    if (upper == L"NUM4" || upper == L"NUMPAD4") { vk = VK_NUMPAD4; return true; }
    if (upper == L"NUM5" || upper == L"NUMPAD5") { vk = VK_NUMPAD5; return true; }
    if (upper == L"NUM6" || upper == L"NUMPAD6") { vk = VK_NUMPAD6; return true; }
    if (upper == L"NUM7" || upper == L"NUMPAD7") { vk = VK_NUMPAD7; return true; }
    if (upper == L"NUM8" || upper == L"NUMPAD8") { vk = VK_NUMPAD8; return true; }
    if (upper == L"NUM9" || upper == L"NUMPAD9") { vk = VK_NUMPAD9; return true; }
    if (upper == L"NUMMUL" || upper == L"NUM*") { vk = VK_MULTIPLY; return true; }
    if (upper == L"NUMADD" || upper == L"NUM+") { vk = VK_ADD; return true; }
    if (upper == L"NUMSUB" || upper == L"NUM-") { vk = VK_SUBTRACT; return true; }
    if (upper == L"NUMDEC" || upper == L"NUM.") { vk = VK_DECIMAL; return true; }
    if (upper == L"NUMDIV" || upper == L"NUM/") { vk = VK_DIVIDE; return true; }
    if (upper == L"NUMENTER") { vk = VK_RETURN; return true; }
    // Media / volume
    if (upper == L"MEDIA_PLAY" || upper == L"PLAY_PAUSE" || upper == L"MEDIAPLAY") { vk = VK_MEDIA_PLAY_PAUSE; return true; }
    if (upper == L"MEDIA_NEXT" || upper == L"NEXT_TRACK" || upper == L"MEDIANEXT") { vk = VK_MEDIA_NEXT_TRACK; return true; }
    if (upper == L"MEDIA_PREV" || upper == L"PREV_TRACK" || upper == L"MEDIAPREV") { vk = VK_MEDIA_PREV_TRACK; return true; }
    if (upper == L"MEDIA_STOP" || upper == L"MEDIASTOP") { vk = VK_MEDIA_STOP; return true; }
    if (upper == L"VOL_UP" || upper == L"VOLUMEUP") { vk = VK_VOLUME_UP; return true; }
    if (upper == L"VOL_DOWN" || upper == L"VOLUMEDOWN") { vk = VK_VOLUME_DOWN; return true; }
    if (upper == L"VOL_MUTE" || upper == L"MUTE" || upper == L"VOLUMEMUTE") { vk = VK_VOLUME_MUTE; return true; }
    // OEM punctuation (US layout)
    if (upper == L"SEMICOLON" || upper == L"OEM_1") { vk = VK_OEM_1; return true; }
    if (upper == L"EQUALS" || upper == L"OEM_PLUS") { vk = VK_OEM_PLUS; return true; }
    if (upper == L"COMMA" || upper == L"OEM_COMMA") { vk = VK_OEM_COMMA; return true; }
    if (upper == L"MINUS" || upper == L"OEM_MINUS") { vk = VK_OEM_MINUS; return true; }
    if (upper == L"PERIOD" || upper == L"OEM_PERIOD") { vk = VK_OEM_PERIOD; return true; }
    if (upper == L"SLASH" || upper == L"OEM_2") { vk = VK_OEM_2; return true; }
    if (upper == L"TILDE" || upper == L"GRAVE" || upper == L"OEM_3") { vk = VK_OEM_3; return true; }
    if (upper == L"LBRACKET" || upper == L"OEM_4") { vk = VK_OEM_4; return true; }
    if (upper == L"BACKSLASH" || upper == L"OEM_5") { vk = VK_OEM_5; return true; }
    if (upper == L"RBRACKET" || upper == L"OEM_6") { vk = VK_OEM_6; return true; }
    if (upper == L"QUOTE" || upper == L"APOSTROPHE" || upper == L"OEM_7") { vk = VK_OEM_7; return true; }
    // Numeric VK code fallback: VK_NNN
    if (upper.size() >= 4 && upper.rfind(L"VK_", 0) == 0) {
        const int code = _wtoi(upper.c_str() + 3);
        if (code > 0 && code <= 0xFF) {
            vk = static_cast<uint16_t>(code);
            return true;
        }
    }
    return false;
}

std::vector<uint16_t> ParseChord(const std::wstring& value) {
    std::vector<uint16_t> chord;
    for (const std::wstring& token : Split(value, L'+')) {
        uint16_t vk = 0;
        if (TryParseVirtualKey(token, vk))
            chord.push_back(vk);
    }
    return chord;
}

PaddleAction ParseAction(const std::wstring& value) {
    const std::wstring trimmed = Trim(value);
    const std::wstring upper = Upper(trimmed);
    if (trimmed.empty() || upper == L"MENU")
        return {};
    if (upper == L"NONE")
        return {PaddleActionType::None};

    const std::vector<std::wstring> segments = Split(trimmed, L'|');
    if (segments.empty())
        return {};

    std::wstring kind;
    std::wstring payload;
    if (!SplitOnce(segments[0], L':', kind, payload))
        return {};

    const std::wstring upperKind = Upper(kind);
    PaddleAction action{};

    if (upperKind == L"GAMEPAD") {
        PaddleMapping mapping = PaddleMapping::None;
        if (TryParseGamepad(payload, mapping)) {
            action.type = PaddleActionType::Gamepad;
            action.gamepadMapping = mapping;
        }
    } else if (upperKind == L"KEY" || upperKind == L"MODIFIER") {
        action.type = PaddleActionType::KeyChord;
        action.chord = ParseChord(payload);
        if (action.chord.empty())
            action.type = PaddleActionType::UseMenuMapping;
    } else if (upperKind == L"MACRO") {
        action.type = PaddleActionType::Macro;
        for (const std::wstring& step : Split(payload, L',')) {
            std::vector<uint16_t> chord = ParseChord(step);
            if (!chord.empty())
                action.macroSteps.push_back(std::move(chord));
        }
        if (action.macroSteps.empty())
            action.type = PaddleActionType::UseMenuMapping;
    }

    for (size_t i = 1; i < segments.size(); ++i) {
        const std::wstring opt = Upper(Trim(segments[i]));
        if (opt == L"RAPID" || opt == L"RAPIDFIRE")
            action.rapidFire = true;
    }

    return action;
}

PaddleAction* GetBinding(PaddleActionBindings& bindings, const std::wstring& name) {
    const std::wstring upper = Upper(name);
    if (upper == L"L4")       return &bindings.l4;
    if (upper == L"L5")       return &bindings.l5;
    if (upper == L"R4")       return &bindings.r4;
    if (upper == L"R5")       return &bindings.r5;
    if (upper == L"QAM")      return &bindings.qam;
    if (upper == L"A")        return &bindings.a;
    if (upper == L"B")        return &bindings.b;
    if (upper == L"X")        return &bindings.x;
    if (upper == L"Y")        return &bindings.y;
    if (upper == L"LB")       return &bindings.lb;
    if (upper == L"RB")       return &bindings.rb;
    if (upper == L"VIEW")     return &bindings.view;
    if (upper == L"MENU")     return &bindings.menu;
    if (upper == L"GUIDE")    return &bindings.guide;
    if (upper == L"L3")       return &bindings.l3;
    if (upper == L"R3")       return &bindings.r3;
    if (upper == L"DPADUP")   return &bindings.dpadUp;
    if (upper == L"DPADDOWN") return &bindings.dpadDown;
    if (upper == L"DPADLEFT") return &bindings.dpadLeft;
    if (upper == L"DPADRIGHT")return &bindings.dpadRight;
    if (upper == L"L2")       return &bindings.l2;
    if (upper == L"R2")       return &bindings.r2;
    return nullptr;
}

PaddleMapping* GetMapping(PaddleMappings& mappings, const std::wstring& name) {
    const std::wstring upper = Upper(name);
    if (upper == L"L4MAP") return &mappings.l4;
    if (upper == L"L5MAP") return &mappings.l5;
    if (upper == L"R4MAP") return &mappings.r4;
    if (upper == L"R5MAP") return &mappings.r5;
    if (upper == L"QAMMAP") return &mappings.qam;
    return nullptr;
}

std::wstring DescribeGamepad(PaddleMapping mapping) {
    switch (mapping) {
    case PaddleMapping::None: return L"Unmapped";
    case PaddleMapping::A: return L"A / Cross";
    case PaddleMapping::B: return L"B / Circle";
    case PaddleMapping::X: return L"X / Square";
    case PaddleMapping::Y: return L"Y / Triangle";
    case PaddleMapping::LeftShoulder: return L"Left Shoulder";
    case PaddleMapping::RightShoulder: return L"Right Shoulder";
    case PaddleMapping::View: return L"View / Share";
    case PaddleMapping::Menu: return L"Menu / Options";
    case PaddleMapping::LeftThumb: return L"Left Stick Click";
    case PaddleMapping::RightThumb: return L"Right Stick Click";
    case PaddleMapping::Guide: return L"Guide / PS";
    case PaddleMapping::DPadUp: return L"D-Pad Up";
    case PaddleMapping::DPadRight: return L"D-Pad Right";
    case PaddleMapping::DPadDown: return L"D-Pad Down";
    case PaddleMapping::DPadLeft: return L"D-Pad Left";
    }
    return L"Unmapped";
}

PaddleMapping ParseGamepadMapping(const std::wstring& value, PaddleMapping fallback = PaddleMapping::None) {
    PaddleMapping mapping = fallback;
    if (TryParseGamepad(value, mapping))
        return mapping;
    return fallback;
}

void WriteProfileSection(std::wofstream& out, const RemapProfile& profile) {
    out << L"[" << profile.id << L"]\n";
    out << L"L4Map=" << DescribeGamepad(profile.mappings.l4) << L"\n";
    out << L"L5Map=" << DescribeGamepad(profile.mappings.l5) << L"\n";
    out << L"R4Map=" << DescribeGamepad(profile.mappings.r4) << L"\n";
    out << L"R5Map=" << DescribeGamepad(profile.mappings.r5) << L"\n";
    out << L"QAMMap=" << DescribeGamepad(profile.mappings.qam) << L"\n";
}

std::wstring DescribeChord(const std::vector<uint16_t>& chord) {
    std::wstring result;
    auto append = [&](const wchar_t* text) {
        if (!result.empty())
            result += L"+";
        result += text;
    };

    for (uint16_t vk : chord) {
        if (vk >= L'A' && vk <= L'Z') { append(std::wstring(1, static_cast<wchar_t>(vk)).c_str()); continue; }
        if (vk >= L'0' && vk <= L'9') { append(std::wstring(1, static_cast<wchar_t>(vk)).c_str()); continue; }
        // Modifiers
        if (vk == VK_CONTROL)  { append(L"CTRL"); continue; }
        if (vk == VK_SHIFT)    { append(L"SHIFT"); continue; }
        if (vk == VK_MENU)     { append(L"ALT"); continue; }
        if (vk == VK_LWIN)     { append(L"LWIN"); continue; }
        if (vk == VK_RWIN)     { append(L"RWIN"); continue; }
        if (vk == VK_LCONTROL) { append(L"LCTRL"); continue; }
        if (vk == VK_RCONTROL) { append(L"RCTRL"); continue; }
        if (vk == VK_LSHIFT)   { append(L"LSHIFT"); continue; }
        if (vk == VK_RSHIFT)   { append(L"RSHIFT"); continue; }
        if (vk == VK_LMENU)    { append(L"LALT"); continue; }
        if (vk == VK_RMENU)    { append(L"RALT"); continue; }
        if (vk == VK_APPS)     { append(L"APPS"); continue; }
        // Navigation / editing
        if (vk == VK_TAB)      { append(L"TAB"); continue; }
        if (vk == VK_RETURN)   { append(L"ENTER"); continue; }
        if (vk == VK_SPACE)    { append(L"SPACE"); continue; }
        if (vk == VK_ESCAPE)   { append(L"ESC"); continue; }
        if (vk == VK_BACK)     { append(L"BACKSPACE"); continue; }
        if (vk == VK_UP)       { append(L"UP"); continue; }
        if (vk == VK_RIGHT)    { append(L"RIGHT"); continue; }
        if (vk == VK_DOWN)     { append(L"DOWN"); continue; }
        if (vk == VK_LEFT)     { append(L"LEFT"); continue; }
        if (vk == VK_HOME)     { append(L"HOME"); continue; }
        if (vk == VK_END)      { append(L"END"); continue; }
        if (vk == VK_PRIOR)    { append(L"PGUP"); continue; }
        if (vk == VK_NEXT)     { append(L"PGDN"); continue; }
        if (vk == VK_INSERT)   { append(L"INSERT"); continue; }
        if (vk == VK_DELETE)   { append(L"DELETE"); continue; }
        // Lock / system
        if (vk == VK_CAPITAL)  { append(L"CAPSLOCK"); continue; }
        if (vk == VK_NUMLOCK)  { append(L"NUMLOCK"); continue; }
        if (vk == VK_SCROLL)   { append(L"SCROLLLOCK"); continue; }
        if (vk == VK_SNAPSHOT) { append(L"PRINTSCREEN"); continue; }
        if (vk == VK_PAUSE)    { append(L"PAUSE"); continue; }
        // Function keys
        if (vk >= VK_F1 && vk <= VK_F24) {
            append((L"F" + std::to_wstring((vk - VK_F1) + 1)).c_str());
            continue;
        }
        // Numpad
        if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
            append((L"NUM" + std::to_wstring(vk - VK_NUMPAD0)).c_str());
            continue;
        }
        if (vk == VK_MULTIPLY) { append(L"NUMMUL"); continue; }
        if (vk == VK_ADD)      { append(L"NUMADD"); continue; }
        if (vk == VK_SUBTRACT) { append(L"NUMSUB"); continue; }
        if (vk == VK_DECIMAL)  { append(L"NUMDEC"); continue; }
        if (vk == VK_DIVIDE)   { append(L"NUMDIV"); continue; }
        // Media / volume
        if (vk == VK_MEDIA_PLAY_PAUSE)  { append(L"MEDIA_PLAY"); continue; }
        if (vk == VK_MEDIA_NEXT_TRACK)  { append(L"MEDIA_NEXT"); continue; }
        if (vk == VK_MEDIA_PREV_TRACK)  { append(L"MEDIA_PREV"); continue; }
        if (vk == VK_MEDIA_STOP)        { append(L"MEDIA_STOP"); continue; }
        if (vk == VK_VOLUME_UP)         { append(L"VOL_UP"); continue; }
        if (vk == VK_VOLUME_DOWN)       { append(L"VOL_DOWN"); continue; }
        if (vk == VK_VOLUME_MUTE)       { append(L"VOL_MUTE"); continue; }
        // OEM punctuation (US layout)
        if (vk == VK_OEM_1)      { append(L"SEMICOLON"); continue; }
        if (vk == VK_OEM_PLUS)   { append(L"EQUALS"); continue; }
        if (vk == VK_OEM_COMMA)  { append(L"COMMA"); continue; }
        if (vk == VK_OEM_MINUS)  { append(L"MINUS"); continue; }
        if (vk == VK_OEM_PERIOD) { append(L"PERIOD"); continue; }
        if (vk == VK_OEM_2)      { append(L"SLASH"); continue; }
        if (vk == VK_OEM_3)      { append(L"TILDE"); continue; }
        if (vk == VK_OEM_4)      { append(L"LBRACKET"); continue; }
        if (vk == VK_OEM_5)      { append(L"BACKSLASH"); continue; }
        if (vk == VK_OEM_6)      { append(L"RBRACKET"); continue; }
        if (vk == VK_OEM_7)      { append(L"QUOTE"); continue; }
        // Fallback: numeric VK code, parseable as VK_NNN
        append((L"VK_" + std::to_wstring(vk)).c_str());
    }

    return result;
}
}

std::wstring PaddleConfig::GetPath() {
    return ConfigDirectory() + L"\\paddles.ini";
}

void PaddleConfig::EnsureExists() {
    const std::wstring path = GetPath();
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
        return;

    std::wofstream out(path);
    out << L"; Remapped button actions\n";
    out << L"; Buttons: L4, L5, R4, R5, QAM, A, B, X, Y, LB, RB, VIEW, MENU, GUIDE,\n";
    out << L";          L3, R3, DPADUP, DPADDOWN, DPADLEFT, DPADRIGHT, L2, R2\n";
    out << L"; Use one of:\n";
    out << L";   menu               (passthrough / use default gamepad mapping)\n";
    out << L";   none               (unmapped)\n";
    out << L";   gamepad:A          (remap to a gamepad button: A B X Y LB RB L3 R3\n";
    out << L";                       DPADUP DPADDOWN DPADLEFT DPADRIGHT START BACK GUIDE)\n";
    out << L";   key:CTRL+SHIFT+S   (send a keyboard shortcut)\n";
    out << L";   macro:CTRL+L, CTRL+C  (send a sequence of shortcuts)\n";
    out << L"; Key names: A-Z, 0-9, F1-F24, CTRL, SHIFT, ALT, WIN, TAB, ENTER, SPACE, ESC,\n";
    out << L";   BACKSPACE, UP, DOWN, LEFT, RIGHT, HOME, END, PGUP, PGDN, INSERT, DELETE,\n";
    out << L";   CAPSLOCK, NUMLOCK, SCROLLLOCK, PRINTSCREEN, PAUSE,\n";
    out << L";   NUM0-NUM9, NUMMUL, NUMADD, NUMSUB, NUMDEC, NUMDIV,\n";
    out << L";   MEDIA_PLAY, MEDIA_NEXT, MEDIA_PREV, MEDIA_STOP, VOL_UP, VOL_DOWN, VOL_MUTE,\n";
    out << L";   SEMICOLON, EQUALS, COMMA, MINUS, PERIOD, SLASH, TILDE,\n";
    out << L";   LBRACKET, BACKSLASH, RBRACKET, QUOTE,\n";
    out << L";   LCTRL, RCTRL, LSHIFT, RSHIFT, LALT, RALT, LWIN, RWIN, APPS,\n";
    out << L";   or VK_NNN for any other Windows virtual key code\n";
    out << L"L4=menu\n";
    out << L"L5=menu\n";
    out << L"R4=menu\n";
    out << L"R5=menu\n";
    out << L"QAM=menu\n";
    out << L"A=menu\n";
    out << L"B=menu\n";
    out << L"X=menu\n";
    out << L"Y=menu\n";
    out << L"LB=menu\n";
    out << L"RB=menu\n";
    out << L"VIEW=menu\n";
    out << L"MENU=menu\n";
    out << L"GUIDE=menu\n";
    out << L"L3=menu\n";
    out << L"R3=menu\n";
    out << L"DPADUP=menu\n";
    out << L"DPADDOWN=menu\n";
    out << L"DPADLEFT=menu\n";
    out << L"DPADRIGHT=menu\n";
    out << L"L2=menu\n";
    out << L"R2=menu\n";
}

PaddleActionBindings PaddleConfig::Load() {
    EnsureExists();
    PaddleActionBindings bindings{};
    std::wifstream in(GetPath());
    std::wstring line;
    while (std::getline(in, line)) {
        const std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'#')
            continue;

        std::wstring key;
        std::wstring value;
        if (!SplitOnce(trimmed, L'=', key, value))
            continue;

        PaddleAction* target = GetBinding(bindings, key);
        if (!target)
            continue;

        *target = ParseAction(value);
    }

    return bindings;
}

void PaddleConfig::Save(const PaddleActionBindings& bindings) {
    EnsureExists();
    std::wofstream out(GetPath(), std::ios::trunc);
    out << L"; Remapped button actions\n";
    out << L"; Use one of:\n";
    out << L";   menu\n";
    out << L";   none\n";
    out << L";   gamepad:A\n";
    out << L";   key:CTRL+SHIFT+S\n";
    out << L";   modifier:ALT\n";
    out << L";   macro:CTRL+L, CTRL+C\n";

    auto writeAction = [&](const wchar_t* name, const PaddleAction& action) {
        std::wstring value = L"menu";
        switch (action.type) {
        case PaddleActionType::UseMenuMapping:
            value = L"menu";
            break;
        case PaddleActionType::None:
            value = L"none";
            break;
        case PaddleActionType::Gamepad:
            value = L"gamepad:" + DescribeGamepad(action.gamepadMapping);
            break;
        case PaddleActionType::KeyChord:
            value = L"key:" + DescribeChord(action.chord);
            break;
        case PaddleActionType::Macro: {
            value = L"macro:";
            bool first = true;
            for (const auto& step : action.macroSteps) {
                if (!first)
                    value += L", ";
                first = false;
                value += DescribeChord(step);
            }
            break;
        }
        }
        if (action.type != PaddleActionType::UseMenuMapping &&
            action.type != PaddleActionType::None) {
            if (action.rapidFire)
                value += L"|rapid";
        }
        out << name << L"=" << value << L"\n";
    };

    writeAction(L"L4",        bindings.l4);
    writeAction(L"L5",        bindings.l5);
    writeAction(L"R4",        bindings.r4);
    writeAction(L"R5",        bindings.r5);
    writeAction(L"QAM",       bindings.qam);
    writeAction(L"A",         bindings.a);
    writeAction(L"B",         bindings.b);
    writeAction(L"X",         bindings.x);
    writeAction(L"Y",         bindings.y);
    writeAction(L"LB",        bindings.lb);
    writeAction(L"RB",        bindings.rb);
    writeAction(L"VIEW",      bindings.view);
    writeAction(L"MENU",      bindings.menu);
    writeAction(L"GUIDE",     bindings.guide);
    writeAction(L"L3",        bindings.l3);
    writeAction(L"R3",        bindings.r3);
    writeAction(L"DPADUP",    bindings.dpadUp);
    writeAction(L"DPADDOWN",  bindings.dpadDown);
    writeAction(L"DPADLEFT",  bindings.dpadLeft);
    writeAction(L"DPADRIGHT", bindings.dpadRight);
    writeAction(L"L2",        bindings.l2);
    writeAction(L"R2",        bindings.r2);
}

std::wstring PaddleConfig::Describe(const PaddleAction& action, PaddleMapping fallbackMapping) {
    switch (action.type) {
    case PaddleActionType::UseMenuMapping:
        return DescribeGamepad(fallbackMapping);
    case PaddleActionType::None:
        return L"Unmapped";
    case PaddleActionType::Gamepad:
        return DescribeGamepad(action.gamepadMapping) +
            (action.rapidFire ? L" [Rapid]" : L"");
    case PaddleActionType::KeyChord:
        return DescribeChord(action.chord) +
            (action.rapidFire ? L" [Rapid]" : L"");
    case PaddleActionType::Macro: {
        std::wstring text = L"Macro: ";
        bool first = true;
        for (const auto& step : action.macroSteps) {
            if (!first)
                text += L", ";
            first = false;
            text += DescribeChord(step);
        }
        if (action.rapidFire)
            text += L" [Rapid]";
        return text;
    }
    }
    return L"Unmapped";
}

bool PaddleConfig::ParseActionString(const std::wstring& value, PaddleAction& action) {
    action = ParseAction(value);
    return action.type != PaddleActionType::UseMenuMapping || Upper(Trim(value)) == L"MENU";
}

std::wstring PaddleConfig::NormalizeProfileId(const std::wstring& profileId) {
    std::wstring id = Upper(Trim(profileId));
    if (id.empty() || id == L"DEFAULT")
        return L"default";

    std::transform(id.begin(), id.end(), id.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return id;
}

std::vector<RemapProfile> PaddleConfig::LoadProfiles(const PaddleMappings& defaultMappings,
                                                     const PaddleActionBindings& defaultActions) {
    const std::wstring path = GetProfilesPath();
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        std::vector<RemapProfile> profiles;
        profiles.push_back(RemapProfile{ L"default", defaultMappings, defaultActions });
        SaveProfiles(profiles);
        return profiles;
    }

    std::vector<RemapProfile> profiles;
    std::wifstream in(path);
    std::wstring line;
    RemapProfile* current = nullptr;

    while (std::getline(in, line)) {
        const std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'#')
            continue;

        if (trimmed.front() == L'[' && trimmed.back() == L']') {
            std::wstring id = NormalizeProfileId(trimmed.substr(1, trimmed.size() - 2));
            profiles.push_back(RemapProfile{ id });
            current = &profiles.back();
            continue;
        }

        if (!current)
            continue;

        std::wstring key;
        std::wstring value;
        if (!SplitOnce(trimmed, L'=', key, value))
            continue;

        if (PaddleAction* action = GetBinding(current->actions, key)) {
            *action = ParseAction(value);
            continue;
        }
        if (PaddleMapping* mapping = GetMapping(current->mappings, key)) {
            *mapping = ParseGamepadMapping(value);
        }
    }

    if (profiles.empty())
        profiles.push_back(RemapProfile{ L"default", defaultMappings, defaultActions });

    auto defaultIt = std::find_if(profiles.begin(), profiles.end(), [](const RemapProfile& profile) {
        return profile.id == L"default";
    });

    if (defaultIt == profiles.end()) {
        profiles.insert(profiles.begin(), RemapProfile{ L"default", defaultMappings, defaultActions });
    } else {
        if (defaultIt->mappings.l4 == PaddleMapping::None && defaultIt->actions.l4.type == PaddleActionType::UseMenuMapping &&
            defaultIt->actions.l5.type == PaddleActionType::UseMenuMapping &&
            defaultIt->actions.r4.type == PaddleActionType::UseMenuMapping &&
            defaultIt->actions.r5.type == PaddleActionType::UseMenuMapping &&
            defaultIt->actions.qam.type == PaddleActionType::UseMenuMapping) {
            defaultIt->mappings = defaultMappings;
            defaultIt->actions = defaultActions;
        }
    }

    return profiles;
}

void PaddleConfig::SaveProfiles(const std::vector<RemapProfile>& profiles) {
    std::wofstream out(GetProfilesPath(), std::ios::trunc);
    out << L"; Remap profiles\n";
    out << L"; One section per profile id. Use [default] for the fallback profile.\n";
    out << L"; Use Steam game names like [Elden Ring] for per-game profiles.\n";
    out << L"; Actions:\n";
    out << L";   menu\n";
    out << L";   none\n";
    out << L";   gamepad:A\n";
    out << L";   key:CTRL+SHIFT+S\n";
    out << L";   macro:CTRL+L, CTRL+C\n";
    out << L"; Key names: A-Z, 0-9, F1-F24, CTRL, SHIFT, ALT, WIN, TAB, ENTER, SPACE, ESC,\n";
    out << L";   BACKSPACE, UP, DOWN, LEFT, RIGHT, HOME, END, PGUP, PGDN, INSERT, DELETE,\n";
    out << L";   CAPSLOCK, NUMLOCK, SCROLLLOCK, PRINTSCREEN, PAUSE,\n";
    out << L";   NUM0-NUM9, NUMMUL, NUMADD, NUMSUB, NUMDEC, NUMDIV,\n";
    out << L";   MEDIA_PLAY, MEDIA_NEXT, MEDIA_PREV, MEDIA_STOP, VOL_UP, VOL_DOWN, VOL_MUTE,\n";
    out << L";   SEMICOLON, EQUALS, COMMA, MINUS, PERIOD, SLASH, TILDE,\n";
    out << L";   LBRACKET, BACKSLASH, RBRACKET, QUOTE,\n";
    out << L";   LCTRL, RCTRL, LSHIFT, RSHIFT, LALT, RALT, LWIN, RWIN, APPS,\n";
    out << L";   or VK_NNN for any other Windows virtual key code\n\n";

    auto writeAction = [&](const wchar_t* name, const PaddleAction& action) {
        std::wstring value = L"menu";
        switch (action.type) {
        case PaddleActionType::UseMenuMapping:
            value = L"menu";
            break;
        case PaddleActionType::None:
            value = L"none";
            break;
        case PaddleActionType::Gamepad:
            value = L"gamepad:" + DescribeGamepad(action.gamepadMapping);
            break;
        case PaddleActionType::KeyChord:
            value = L"key:" + DescribeChord(action.chord);
            break;
        case PaddleActionType::Macro: {
            value = L"macro:";
            bool first = true;
            for (const auto& step : action.macroSteps) {
                if (!first)
                    value += L", ";
                first = false;
                value += DescribeChord(step);
            }
            break;
        }
        }
        if (action.type != PaddleActionType::UseMenuMapping &&
            action.type != PaddleActionType::None &&
            action.rapidFire) {
            value += L"|rapid";
        }
        out << name << L"=" << value << L"\n";
    };

    for (const RemapProfile& profile : profiles) {
        WriteProfileSection(out, profile);
        writeAction(L"L4",        profile.actions.l4);
        writeAction(L"L5",        profile.actions.l5);
        writeAction(L"R4",        profile.actions.r4);
        writeAction(L"R5",        profile.actions.r5);
        writeAction(L"QAM",       profile.actions.qam);
        writeAction(L"A",         profile.actions.a);
        writeAction(L"B",         profile.actions.b);
        writeAction(L"X",         profile.actions.x);
        writeAction(L"Y",         profile.actions.y);
        writeAction(L"LB",        profile.actions.lb);
        writeAction(L"RB",        profile.actions.rb);
        writeAction(L"VIEW",      profile.actions.view);
        writeAction(L"MENU",      profile.actions.menu);
        writeAction(L"GUIDE",     profile.actions.guide);
        writeAction(L"L3",        profile.actions.l3);
        writeAction(L"R3",        profile.actions.r3);
        writeAction(L"DPADUP",    profile.actions.dpadUp);
        writeAction(L"DPADDOWN",  profile.actions.dpadDown);
        writeAction(L"DPADLEFT",  profile.actions.dpadLeft);
        writeAction(L"DPADRIGHT", profile.actions.dpadRight);
        writeAction(L"L2",        profile.actions.l2);
        writeAction(L"R2",        profile.actions.r2);
        out << L"\n";
    }
}
