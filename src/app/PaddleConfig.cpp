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
    return ConfigDirectory() + L"\\paddles.ini";
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
    out << L";   macro:CTRL+L, CTRL+C\n\n";

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
        writeAction(L"L4", profile.actions.l4);
        writeAction(L"L5", profile.actions.l5);
        writeAction(L"R4", profile.actions.r4);
        writeAction(L"R5", profile.actions.r5);
        writeAction(L"QAM", profile.actions.qam);
        out << L"\n";
    }
}
