#include "TrayApp.h"
#include "ControllerManager.h"
#include "PaddleConfig.h"
#include "PaddleConfigWindow.h"
#include "SteamLibrary.h"
#include "logging/Log.h"
#include "resource.h"
#include <algorithm>
#include <dbt.h>
#include <shellapi.h>
#include <string>
#include <tlhelp32.h>
#include <winreg.h>

static TrayApp* g_app = nullptr;

static constexpr wchar_t WNDCLASS_NAME[] = L"SteamControllerRemapperTray";
static constexpr wchar_t REG_KEY[]       = L"Software\\SteamControllerRemapper";
static constexpr wchar_t LEGACY_REG_KEY[] = L"Software\\XboxModeSteamlessController";
static constexpr wchar_t REG_RUN_KEY[]   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr wchar_t APP_NAME[]      = L"Steam Controller Remapper";
static constexpr wchar_t LEGACY_APP_NAME[] = L"Xbox Mode Steamless Controller";
static constexpr wchar_t OLD_APP_NAME[]  = L"SteamlessController";
static constexpr wchar_t REG_LAST_PROFILE[] = L"LastActiveProfileId";
static constexpr wchar_t REG_MANUAL_OVERRIDE[] = L"ManualProfileOverride";

static bool HasRunEntry(const wchar_t* name) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;

    bool exists = RegQueryValueExW(key, name, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return exists;
}

static bool HasAnyRunEntry() {
    return HasRunEntry(APP_NAME) || HasRunEntry(LEGACY_APP_NAME) || HasRunEntry(OLD_APP_NAME);
}

static bool OpenSettingsKeyForRead(HKEY& key) {
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS)
        return true;
    return RegOpenKeyExW(HKEY_CURRENT_USER, LEGACY_REG_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS;
}

static bool IsProcessRunningByName(const wchar_t* processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, processName) == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return running;
}

static RemapProfile* FindProfileById(std::vector<RemapProfile>& profiles, const std::wstring& id) {
    const std::wstring normalized = PaddleConfig::NormalizeProfileId(id);
    for (RemapProfile& profile : profiles) {
        if (profile.id == normalized)
            return &profile;
    }
    return nullptr;
}

static std::wstring GetProcessPathById(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};

    wchar_t buffer[MAX_PATH] = {};
    DWORD size = static_cast<DWORD>(std::size(buffer));
    std::wstring path;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size))
        path.assign(buffer, size);
    CloseHandle(process);
    return path;
}

static std::vector<std::wstring> GetRunningProcessPaths() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return {};

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::vector<std::wstring> processPaths;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == 0 || entry.th32ProcessID == GetCurrentProcessId())
                continue;
            std::wstring processPath = GetProcessPathById(entry.th32ProcessID);
            if (!processPath.empty())
                processPaths.push_back(std::move(processPath));
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return processPaths;
}

static void LogLaunchContext() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    HWND shellWindow = GetShellWindow();
    DWORD shellPid = 0;
    wchar_t shellClass[128] = {};
    if (shellWindow) {
        GetWindowThreadProcessId(shellWindow, &shellPid);
        GetClassNameW(shellWindow, shellClass, static_cast<int>(std::size(shellClass)));
    }

    std::wstring shellProcess;
    if (shellPid != 0) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snapshot, &entry)) {
                do {
                    if (entry.th32ProcessID == shellPid) {
                        shellProcess = entry.szExeFile;
                        break;
                    }
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
        }
    }
    bool explorerRunning = IsProcessRunningByName(L"explorer.exe");
    bool steamRunning = IsProcessRunningByName(L"steam.exe");

    const char* sessionKind =
        (explorerRunning && _wcsicmp(shellProcess.c_str(), L"explorer.exe") == 0)
            ? "desktop-like"
            : "alternate-shell";

    logging::Logf(
        "[LaunchContext] exe=%s shellHwnd=%p shellPid=%lu shellProcess=%s shellClass=%s explorerRunning=%d steamRunning=%d inferredSession=%s",
        logging::Narrow(exePath).c_str(),
        shellWindow,
        static_cast<unsigned long>(shellPid),
        logging::Narrow(shellProcess).c_str(),
        logging::Narrow(shellClass).c_str(),
        explorerRunning ? 1 : 0,
        steamRunning ? 1 : 0,
        sessionKind);
}

TrayApp::TrayApp() {
    g_app = this;
}

TrayApp::~TrayApp() {
    RemoveTrayIcon();
    g_app = nullptr;
}

bool TrayApp::Init(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    m_iconOff   = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON_OFF));
    m_iconOn    = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON_ON));
    m_wmTaskbar = RegisterWindowMessageW(L"TaskbarCreated");
    LogLaunchContext();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = WNDCLASS_NAME;
    if (!RegisterClassExW(&wc)) return false;

    // Message-only window — invisible, never shown.
    m_hwnd = CreateWindowExW(0, WNDCLASS_NAME, APP_NAME,
                             0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    if (!m_hwnd) return false;

    // Register for HID device arrival/removal notifications.
    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size       = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    // HID device interface GUID
    filter.dbcc_classguid  = {0x4D1E55B2, 0xF16F, 0x11CF,
                              {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
    RegisterDeviceNotificationW(m_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    m_controller = std::make_unique<ControllerManager>(
        [this](bool connected, bool gameModeActive, bool vigemMissing) {
            UpdateTrayIcon(connected, gameModeActive, vigemMissing);
        });

    m_steamRunning = IsSteamRunning();
    LoadSettings();
    LoadPaddleConfig();
    ApplyProfileById(m_activeProfileId, true);
    if ((HasRunEntry(LEGACY_APP_NAME) || HasRunEntry(OLD_APP_NAME)) && !HasRunEntry(APP_NAME))
        SetStartupEnabled(true);
    AddTrayIcon();
    UpdateTrayIcon(m_controller->IsConnected(), m_controller->IsGameModeActive(), false);
    SetTimer(m_hwnd, TIMER_STEAM_POLL, STEAM_POLL_MS, nullptr);
    ReconcileAutoMode();
    return true;
}

int TrayApp::Run() {
    MSG msg;
    BOOL ret;
    while ((ret = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (ret == -1) return -1;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK TrayApp::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_app) return g_app->HandleMessage(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TrayApp::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == m_wmTaskbar) {
        AddTrayIcon();
        return 0;
    }

    switch (msg) {
    case WM_TRAY:
        if (LOWORD(lp) == NIN_BALLOONUSERCLICK)
            ShellExecuteW(nullptr, L"open", L"https://github.com/nefarius/ViGEmBus/releases/latest",
                          nullptr, nullptr, SW_SHOWNORMAL);
        else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP)
            ShowContextMenu();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_TOGGLE:
            if (m_controller->IsGameModeActive())
                m_controller->DisableGameMode();
            else if (!m_steamRunning)
                m_controller->EnableGameMode();
            break;
        case IDM_TRACKPAD:
            m_controller->SetTrackpadMouseEnabled(!m_controller->IsTrackpadMouseEnabled());
            SaveSettings();
            break;
        case IDM_LEFT_TRACKPAD:
            m_controller->SetUseLeftTrackpad(!m_controller->IsUseLeftTrackpad());
            SaveSettings();
            break;
        case IDM_STARTUP:
            SetStartupEnabled(!IsStartupEnabled());
            break;
        case IDM_AUTOENABLE:
            m_autoEnableSteamlessMode = !m_autoEnableSteamlessMode;
            SaveSettings();
            ReconcileAutoMode();
            break;
        case IDM_OUTPUT_X360:
            m_controller->SetEmulationMode(EmulationMode::Xbox360);
            SaveSettings();
            ReconcileAutoMode();
            break;
        case IDM_OUTPUT_DS4:
            m_controller->SetEmulationMode(EmulationMode::DualShock4);
            SaveSettings();
            ReconcileAutoMode();
            break;
        case IDM_CONFIGURE_PADDLES:
            if (!m_steamRunning)
                ShowPaddleConfigWindow();
            break;
        case IDM_EXIT:
            m_controller->DisableGameMode();
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_DEVICECHANGE:
        if (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE) {
            m_lastReconnectAttemptTick = GetTickCount64();
            m_controller->OnDeviceChange();
            ReconcileAutoMode();
        }
        return TRUE;

    case WM_POWERBROADCAST:
        switch (wp) {
        case PBT_APMSUSPEND:
            m_controller->OnSuspend();
            return TRUE;
        case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMESUSPEND:
            m_controller->OnResume();
            ReconcileAutoMode();
            return TRUE;
        }
        break;

    case WM_TIMER:
        if (wp == TIMER_STEAM_POLL) {
            // Retry controller discovery on the timer as well. On cold boot the
            // first open attempt can race HID initialization, and there may be
            // no later arrival event if the controller was already present.
            const ULONGLONG now = GetTickCount64();
            if (m_controller->IsConnected() ||
                (now - m_lastReconnectAttemptTick) >= RECONNECT_BACKOFF_MS) {
                m_lastReconnectAttemptTick = now;
                m_controller->OnDeviceChange();
                ReconcileAutoMode();
            }

            bool steamRunning = IsSteamRunning();
            if (steamRunning != m_steamRunning) {
                m_steamRunning = steamRunning;
                if (m_steamRunning && m_paddleConfigWindow)
                    m_paddleConfigWindow->Close();
                ReconcileAutoMode();
            }

            if (m_autoSwitchProfiles) {
                const std::wstring detectedProfileId = GetDetectedGameProfileId();
                if (!detectedProfileId.empty()) {
                    m_manualProfileOverride = false;
                    ApplyProfileById(detectedProfileId);
                } else if (!m_manualProfileOverride) {
                    ApplyProfileById(L"default");
                }
            }
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_STEAM_POLL);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void TrayApp::AddTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = m_hwnd;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon            = m_iconOff;
    wcscpy_s(nid.szTip, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void TrayApp::RemoveTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = m_hwnd;
    nid.uID    = TRAY_UID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void TrayApp::UpdateTrayIcon(bool connected, bool gameModeActive, bool vigemMissing) {
    if (vigemMissing) { ShowViGEmBalloon(); return; }
    bool gameModeOn = gameModeActive;

    const wchar_t* tip = gameModeOn  ? L"Steam Controller Remapper - Steamless Mode ON"
                       : connected   ? L"Steam Controller Remapper - Connected (Steamless Mode OFF)"
                                     : L"Steam Controller Remapper - No controller found";

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = m_hwnd;
    nid.uID    = TRAY_UID;
    nid.uFlags = NIF_TIP | NIF_ICON;
    nid.hIcon  = gameModeOn ? m_iconOn : m_iconOff;
    wcscpy_s(nid.szTip, tip);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayApp::ShowViGEmBalloon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = m_hwnd;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_INFO;
    nid.dwInfoFlags      = NIIF_WARNING;
    wcscpy_s(nid.szInfoTitle, L"Driver required");
    wcscpy_s(nid.szInfo,      L"ViGEmBus is not installed. Click here to download it.");
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

bool TrayApp::IsSteamRunning() const {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"steam.exe") == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return running;
}

std::vector<std::wstring> TrayApp::GetInstalledGames() const {
    return SteamLibrary::ListInstalledGameNames();
}

std::vector<std::wstring> TrayApp::RefreshInstalledGames() const {
    return SteamLibrary::RefreshInstalledGameNames();
}

std::vector<std::wstring> TrayApp::GetGameSourceSpecs() const {
    return SteamLibrary::GetConfiguredSourceSpecs();
}

void TrayApp::SetGameSourceSpecs(const std::vector<std::wstring>& specs) {
    SteamLibrary::SetConfiguredSourceSpecs(specs);
}

bool TrayApp::GetAutoSwitchProfiles() const {
    return m_autoSwitchProfiles;
}

void TrayApp::SetAutoSwitchProfiles(bool enabled) {
    m_autoSwitchProfiles = enabled;
    if (!enabled)
        m_manualProfileOverride = false;
    SaveSettings();
}

std::wstring TrayApp::GetDetectedGameProfileId() const {
    std::vector<std::wstring> candidatePaths;
    HWND foreground = GetForegroundWindow();
    if (foreground) {
        DWORD pid = 0;
        GetWindowThreadProcessId(foreground, &pid);
        if (pid != 0 && pid != GetCurrentProcessId()) {
            const std::wstring processPath = GetProcessPathById(pid);
            if (!processPath.empty())
                candidatePaths.push_back(processPath);
        }
    }

    const std::wstring foregroundMatch =
        PaddleConfig::NormalizeProfileId(SteamLibrary::MatchProcessListToInstalledGame(candidatePaths));
    if (!foregroundMatch.empty() && foregroundMatch != L"default") {
        logging::Logf("[Profiles] Detected foreground profile id=%s",
                      logging::Narrow(foregroundMatch).c_str());
        return foregroundMatch;
    }

    if (m_activeProfileId != L"default") {
        const std::wstring runningMatch =
            PaddleConfig::NormalizeProfileId(SteamLibrary::MatchProcessListToInstalledGame(GetRunningProcessPaths()));
        if (runningMatch == m_activeProfileId) {
            logging::Logf("[Profiles] Keeping active running profile id=%s",
                          logging::Narrow(runningMatch).c_str());
            return runningMatch;
        }
    }

    return {};
}

RemapProfile* TrayApp::EnsureProfileExists(const std::wstring& profileId, const std::wstring& baseProfileId) {
    const std::wstring normalizedId = PaddleConfig::NormalizeProfileId(profileId);
    if (normalizedId.empty())
        return nullptr;

    if (RemapProfile* existing = FindProfileById(m_profiles, normalizedId))
        return existing;

    const std::wstring normalizedBaseId = PaddleConfig::NormalizeProfileId(baseProfileId);
    const RemapProfile* base = FindProfileById(m_profiles, normalizedBaseId);
    if (!base)
        base = FindProfileById(m_profiles, L"default");

    RemapProfile profile = base ? *base : RemapProfile{};
    profile.id = normalizedId;
    m_profiles.push_back(std::move(profile));
    PersistProfiles();
    logging::Logf("[Profiles] Created profile id=%s base=%s",
                  logging::Narrow(normalizedId).c_str(),
                  logging::Narrow(base ? base->id : std::wstring(L"default")).c_str());
    return &m_profiles.back();
}

void TrayApp::ApplyProfileById(const std::wstring& profileId, bool force) {
    const std::wstring normalizedId = PaddleConfig::NormalizeProfileId(profileId);
    if (!force && normalizedId == m_activeProfileId)
        return;

    const RemapProfile* profile = EnsureProfileExists(normalizedId);
    if (!profile)
        return;

    m_activeProfileId = normalizedId;
    m_controller->SetPaddleMapping(0, profile->mappings.l4);
    m_controller->SetPaddleMapping(1, profile->mappings.l5);
    m_controller->SetPaddleMapping(2, profile->mappings.r4);
    m_controller->SetPaddleMapping(3, profile->mappings.r5);
    m_controller->SetPaddleMapping(4, profile->mappings.qam);
    m_controller->SetPaddleActions(profile->actions);
    SaveSettings();
    if (m_paddleConfigWindow && m_paddleConfigWindow->IsOpen())
        m_paddleConfigWindow->ReloadFromModel();
    logging::Logf("[Profiles] Applied profile id=%s",
                  logging::Narrow(m_activeProfileId).c_str());
}

void TrayApp::PersistProfiles() {
    PaddleConfig::SaveProfiles(m_profiles);
}

void TrayApp::ReconcileAutoMode() {
    if (!m_controller)
        return;

    bool shouldAutoEnable = m_autoEnableSteamlessMode &&
                            m_controller->IsConnected() &&
                            !m_steamRunning;

    if (shouldAutoEnable && !m_controller->IsGameModeActive()) {
        m_controller->EnableGameMode();
    } else if (m_steamRunning && m_controller->IsGameModeActive()) {
        m_controller->DisableGameMode();
    }
}

bool TrayApp::IsStartupEnabled() const {
    return HasAnyRunEntry();
}

void TrayApp::SetStartupEnabled(bool enabled) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_WRITE, &key) != ERROR_SUCCESS)
        return;

    if (enabled) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring command = L"\"";
        command += path;
        command += L"\"";
        RegSetValueExW(key, APP_NAME, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        RegDeleteValueW(key, LEGACY_APP_NAME);
        RegDeleteValueW(key, OLD_APP_NAME);
    } else {
        RegDeleteValueW(key, APP_NAME);
        RegDeleteValueW(key, LEGACY_APP_NAME);
        RegDeleteValueW(key, OLD_APP_NAME);
    }

    RegCloseKey(key);
}

void TrayApp::LoadSettings() {
    HKEY key;
    if (!OpenSettingsKeyForRead(key))
        return;

    auto readBool = [&](const wchar_t* name, bool def) -> bool {
        DWORD val = 0, size = sizeof(val);
        if (RegQueryValueExW(key, name, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS)
            return val != 0;
        return def;
    };
    auto readDword = [&](const wchar_t* name, DWORD def) -> DWORD {
        DWORD val = 0, size = sizeof(val);
        if (RegQueryValueExW(key, name, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS)
            return val;
        return def;
    };
    m_controller->SetTrackpadMouseEnabled(readBool(L"TrackpadMouse",   true));
    m_controller->SetBackButtonsEnabled  (readBool(L"BackButtons",     false));
    m_controller->SetUseLeftTrackpad     (readBool(L"UseLeftTrackpad", false));
    m_autoEnableSteamlessMode            = readBool(L"AutoEnableSteamlessMode", true);
    m_autoSwitchProfiles                 = readBool(L"AutoSwitchProfiles", false);
    m_controller->SetEmulationMode(readBool(L"UseDs4Emulation", false)
        ? EmulationMode::DualShock4
        : EmulationMode::Xbox360);
    m_controller->SetPaddleMapping(0, static_cast<PaddleMapping>(readDword(L"PaddleMapL4", 0)));
    m_controller->SetPaddleMapping(1, static_cast<PaddleMapping>(readDword(L"PaddleMapL5", 0)));
    m_controller->SetPaddleMapping(2, static_cast<PaddleMapping>(readDword(L"PaddleMapR4", 0)));
    m_controller->SetPaddleMapping(3, static_cast<PaddleMapping>(readDword(L"PaddleMapR5", 0)));
    m_controller->SetPaddleMapping(4, static_cast<PaddleMapping>(readDword(L"PaddleMapQAM", 0)));
    m_activeProfileId = L"default";
    m_manualProfileOverride = false;

    RegCloseKey(key);
}

void TrayApp::SaveSettings() {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return;

    auto writeBool = [&](const wchar_t* name, bool val) {
        DWORD dw = val ? 1 : 0;
        RegSetValueExW(key, name, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
    };
    auto writeDword = [&](const wchar_t* name, DWORD val) {
        RegSetValueExW(key, name, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&val), sizeof(val));
    };
    auto writeString = [&](const wchar_t* name, const std::wstring& value) {
        RegSetValueExW(key, name, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    };
    const PaddleMappings paddles = m_controller->GetPaddleMappings();

    writeBool(L"TrackpadMouse",   m_controller->IsTrackpadMouseEnabled());
    writeBool(L"BackButtons",     m_controller->IsBackButtonsEnabled());
    writeBool(L"UseLeftTrackpad", m_controller->IsUseLeftTrackpad());
    writeBool(L"AutoEnableSteamlessMode", m_autoEnableSteamlessMode);
    writeBool(L"AutoSwitchProfiles", m_autoSwitchProfiles);
    writeBool(L"UseDs4Emulation",
              m_controller->GetEmulationMode() == EmulationMode::DualShock4);
    writeDword(L"PaddleMapL4", static_cast<DWORD>(paddles.l4));
    writeDword(L"PaddleMapL5", static_cast<DWORD>(paddles.l5));
    writeDword(L"PaddleMapR4", static_cast<DWORD>(paddles.r4));
    writeDword(L"PaddleMapR5", static_cast<DWORD>(paddles.r5));
    writeDword(L"PaddleMapQAM", static_cast<DWORD>(paddles.qam));
    writeString(REG_LAST_PROFILE, m_activeProfileId);
    writeBool(REG_MANUAL_OVERRIDE, m_manualProfileOverride);

    RegCloseKey(key);
}

void TrayApp::LoadPaddleConfig() {
    PaddleConfig::EnsureExists();
    PaddleActionBindings legacyActions = PaddleConfig::Load();
    PaddleMappings legacyMappings = m_controller->GetPaddleMappings();
    m_profiles = PaddleConfig::LoadProfiles(legacyMappings, legacyActions);
}

void TrayApp::ShowPaddleConfigWindow() {
    if (!m_paddleConfigWindow) {
        m_paddleConfigWindow = std::make_unique<PaddleConfigWindow>(
            [this]() {
                const RemapProfile* profile = FindProfileById(m_profiles, m_activeProfileId);
                return profile ? profile->mappings : PaddleMappings{};
            },
            [this]() {
                const RemapProfile* profile = FindProfileById(m_profiles, m_activeProfileId);
                return profile ? profile->actions : PaddleActionBindings{};
            },
            [this]() { return m_controller->GetCurrentMacroCaptureChord(); },
            [this]() { return m_activeProfileId; },
            [this]() { return GetInstalledGames(); },
            [this]() { return RefreshInstalledGames(); },
            [this]() { return GetGameSourceSpecs(); },
            [this](const std::vector<std::wstring>& specs) { SetGameSourceSpecs(specs); },
            [this]() { return GetAutoSwitchProfiles(); },
            [this](bool enabled) { SetAutoSwitchProfiles(enabled); },
            [this](const std::wstring& profileId) {
                const std::wstring normalizedId = PaddleConfig::NormalizeProfileId(profileId);
                EnsureProfileExists(normalizedId);
                m_manualProfileOverride = true;
                ApplyProfileById(normalizedId, true);
            },
            [this](const std::wstring& profileId) {
                const std::wstring normalizedId = PaddleConfig::NormalizeProfileId(profileId);
                m_profiles.erase(std::remove_if(m_profiles.begin(), m_profiles.end(),
                    [&](const RemapProfile& profile) { return profile.id == normalizedId; }),
                    m_profiles.end());
                PersistProfiles();
                ApplyProfileById(L"default", true);
            },
            [this](const PaddleMappings& mappings, const PaddleActionBindings& actions) {
                RemapProfile* profile = FindProfileById(m_profiles, m_activeProfileId);
                if (!profile) {
                    m_profiles.push_back(RemapProfile{ m_activeProfileId, mappings, actions });
                    profile = &m_profiles.back();
                } else {
                    profile->mappings = mappings;
                    profile->actions = actions;
                }
                PersistProfiles();
                ApplyProfileById(m_activeProfileId, true);
            });
    }

    m_paddleConfigWindow->Show(m_hInstance, m_hwnd);
}

void TrayApp::ShowContextMenu() {
    bool connected      = m_controller->IsConnected();
    bool gameModeOn     = m_controller->IsGameModeActive();
    bool trackpadOn     = m_controller->IsTrackpadMouseEnabled();
    bool leftTrackpad   = m_controller->IsUseLeftTrackpad();
    bool startupOn      = IsStartupEnabled();
    bool canEnableMode  = connected && !m_steamRunning;
    bool canConfigure   = !m_steamRunning;
    bool ds4Mode        = m_controller->GetEmulationMode() == EmulationMode::DualShock4;

    HMENU menu = CreatePopupMenu();

    UINT toggleFlags = MF_STRING | ((gameModeOn || canEnableMode) ? MF_ENABLED : MF_GRAYED);
    AppendMenuW(menu, toggleFlags, IDM_TOGGLE,
                gameModeOn ? L"Disable Steamless Mode" : L"Enable Steamless Mode");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    UINT autoEnableFlags = MF_STRING | (m_autoEnableSteamlessMode ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, autoEnableFlags, IDM_AUTOENABLE, L"Auto-enable Steamless Mode");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    UINT x360Flags = MF_STRING | (ds4Mode ? MF_UNCHECKED : MF_CHECKED);
    AppendMenuW(menu, x360Flags, IDM_OUTPUT_X360, L"Emulate Xbox 360 Controller");

    UINT ds4Flags = MF_STRING | (ds4Mode ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, ds4Flags, IDM_OUTPUT_DS4, L"Emulate DualShock 4");
    AppendMenuW(menu, MF_STRING | (canConfigure ? MF_ENABLED : MF_GRAYED), IDM_CONFIGURE_PADDLES, L"Remap Buttons...");

    UINT trackpadFlags = MF_STRING | (trackpadOn ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, trackpadFlags, IDM_TRACKPAD, L"Enable Trackpad Mouse");

    UINT leftFlags = MF_STRING | (leftTrackpad ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, leftFlags, IDM_LEFT_TRACKPAD, L"Use Left Trackpad Instead");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    UINT startupFlags = MF_STRING | (startupOn ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, startupFlags, IDM_STARTUP, L"Start with Windows");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    // SetForegroundWindow is required for the menu to dismiss on click-away.
    SetForegroundWindow(m_hwnd);

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON,
                   pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
}
