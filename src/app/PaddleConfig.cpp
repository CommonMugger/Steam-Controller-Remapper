#include "PaddleConfig.h"
#include <Windows.h>
#include <ShlObj.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

namespace {
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
    if (upper == L"A" || upper == L"CROSS") { mapping = PaddleMapping::A; return true; }
    if (upper == L"B" || upper == L"CIRCLE") { mapping = PaddleMapping::B; return true; }
    if (upper == L"X" || upper == L"SQUARE") { mapping = PaddleMapping::X; return true; }
    if (upper == L"Y" || upper == L"TRIANGLE") { mapping = PaddleMapping::Y; return true; }
    if (upper == L"LB" || upper == L"LEFTSHOULDER") { mapping = PaddleMapping::LeftShoulder; return true; }
    if (upper == L"RB" || upper == L"RIGHTSHOULDER") { mapping = PaddleMapping::RightShoulder; return true; }
    if (upper == L"VIEW" || upper == L"BACK" || upper == L"SHARE") { mapping = PaddleMapping::View; return true; }
    if (upper == L"MENU" || upper == L"START" || upper == L"OPTIONS") { mapping = PaddleMapping::Menu; return true; }
    if (upper == L"L3" || upper == L"LEFTTHUMB") { mapping = PaddleMapping::LeftThumb; return true; }
    if (upper == L"R3" || upper == L"RIGHTTHUMB") { mapping = PaddleMapping::RightThumb; return true; }
    if (upper == L"GUIDE" || upper == L"PS") { mapping = PaddleMapping::Guide; return true; }
    if (upper == L"DPADUP") { mapping = PaddleMapping::DPadUp; return true; }
    if (upper == L"DPADRIGHT") { mapping = PaddleMapping::DPadRight; return true; }
    if (upper == L"DPADDOWN") { mapping = PaddleMapping::DPadDown; return true; }
    if (upper == L"DPADLEFT") { mapping = PaddleMapping::DPadLeft; return true; }
    if (upper == L"NONE" || upper == L"UNMAPPED") { mapping = PaddleMapping::None; return true; }
    return false;
}

bool TryParseVirtualKey(const std::wstring& token, uint16_t& vk) {
    const std::wstring upper = Upper(token);
    if (upper.size() == 1 && upper[0] >= L'A' && upper[0] <= L'Z') { vk = static_cast<uint16_t>(upper[0]); return true; }
    if (upper.size() == 1 && upper[0] >= L'0' && upper[0] <= L'9') { vk = static_cast<uint16_t>(upper[0]); return true; }
    if (upper == L"CTRL" || upper == L"CONTROL") { vk = VK_CONTROL; return true; }
    if (upper == L"SHIFT") { vk = VK_SHIFT; return true; }
    if (upper == L"ALT") { vk = VK_MENU; return true; }
    if (upper == L"WIN" || upper == L"WINDOWS") { vk = VK_LWIN; return true; }
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
    if (upper.size() >= 2 && upper[0] == L'F') {
        const int num = _wtoi(upper.c_str() + 1);
        if (num >= 1 && num <= 24) {
            vk = static_cast<uint16_t>(VK_F1 + (num - 1));
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
    if (upper == L"L4") return &bindings.l4;
    if (upper == L"L5") return &bindings.l5;
    if (upper == L"R4") return &bindings.r4;
    if (upper == L"R5") return &bindings.r5;
    if (upper == L"QAM") return &bindings.qam;
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

std::wstring DescribeChord(const std::vector<uint16_t>& chord) {
    std::wstring result;
    auto append = [&](const wchar_t* text) {
        if (!result.empty())
            result += L"+";
        result += text;
    };

    for (uint16_t vk : chord) {
        if (vk >= L'A' && vk <= L'Z') append(std::wstring(1, static_cast<wchar_t>(vk)).c_str());
        else if (vk >= L'0' && vk <= L'9') append(std::wstring(1, static_cast<wchar_t>(vk)).c_str());
        else if (vk == VK_CONTROL) append(L"CTRL");
        else if (vk == VK_SHIFT) append(L"SHIFT");
        else if (vk == VK_MENU) append(L"ALT");
        else if (vk == VK_LWIN) append(L"WIN");
        else if (vk == VK_TAB) append(L"TAB");
        else if (vk == VK_RETURN) append(L"ENTER");
        else if (vk == VK_SPACE) append(L"SPACE");
        else if (vk == VK_ESCAPE) append(L"ESC");
        else if (vk == VK_BACK) append(L"BACKSPACE");
        else if (vk == VK_UP) append(L"UP");
        else if (vk == VK_RIGHT) append(L"RIGHT");
        else if (vk == VK_DOWN) append(L"DOWN");
        else if (vk == VK_LEFT) append(L"LEFT");
        else if (vk >= VK_F1 && vk <= VK_F24) {
            std::wstring key = L"F" + std::to_wstring((vk - VK_F1) + 1);
            append(key.c_str());
        } else {
            std::wstring key = L"VK_" + std::to_wstring(vk);
            append(key.c_str());
        }
    }

    return result;
}
}

std::wstring PaddleConfig::GetPath() {
    wchar_t localAppData[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData);
    std::wstring dir = localAppData;
    dir += L"\\XboxModeSteamlessController";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\paddles.ini";
}

void PaddleConfig::EnsureExists() {
    const std::wstring path = GetPath();
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
        return;

    std::wofstream out(path);
    out << L"; Remapped button actions\n";
    out << L"; Use one of:\n";
    out << L";   menu\n";
    out << L";   none\n";
    out << L";   gamepad:A\n";
    out << L";   key:CTRL+SHIFT+S\n";
    out << L";   modifier:ALT\n";
    out << L";   macro:CTRL+L, CTRL+C\n";
    out << L"L4=menu\n";
    out << L"L5=menu\n";
    out << L"R4=menu\n";
    out << L"R5=menu\n";
    out << L"QAM=menu\n";
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

    writeAction(L"L4", bindings.l4);
    writeAction(L"L5", bindings.l5);
    writeAction(L"R4", bindings.r4);
    writeAction(L"R5", bindings.r5);
    writeAction(L"QAM", bindings.qam);
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
