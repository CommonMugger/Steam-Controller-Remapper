#pragma once
#include "PaddleConfig.h"
#include <Windows.h>
#include <memory>
#include <string>
#include <vector>

class ControllerManager;
class PaddleConfigWindow;

class TrayApp {
public:
    TrayApp();
    ~TrayApp();

    bool Init(HINSTANCE hInstance);
    int  Run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void AddTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayIcon(bool connected, bool gameModeActive, bool vigemMissing = false);
    void ShowViGEmBalloon();
    void ShowContextMenu();
    void LoadSettings();
    void SaveSettings();
    void LoadPaddleConfig();
    void ShowPaddleConfigWindow();
    std::vector<std::wstring> GetInstalledGames() const;
    std::vector<std::wstring> RefreshInstalledGames() const;
    std::vector<std::wstring> GetGameSourceSpecs() const;
    void SetGameSourceSpecs(const std::vector<std::wstring>& specs);
    bool GetAutoSwitchProfiles() const;
    void SetAutoSwitchProfiles(bool enabled);
    bool IsSteamRunning() const;
    std::wstring GetDetectedGameProfileId() const;
    RemapProfile* EnsureProfileExists(const std::wstring& profileId, const std::wstring& baseProfileId = L"default");
    void ApplyProfileById(const std::wstring& profileId, bool force = false);
    void PersistProfiles();
    void ReconcileAutoMode();
    bool IsStartupEnabled() const;
    void SetStartupEnabled(bool enabled);

    HWND                               m_hwnd      = nullptr;
    HINSTANCE                          m_hInstance = nullptr;
    UINT                               m_wmTaskbar = 0;
    HICON                              m_iconOff   = nullptr;
    HICON                              m_iconOn    = nullptr;
    std::unique_ptr<ControllerManager> m_controller;
    std::unique_ptr<PaddleConfigWindow> m_paddleConfigWindow;
    bool                               m_autoEnableSteamlessMode = true;
    bool                               m_autoSwitchProfiles      = false;
    bool                               m_manualProfileOverride   = false;
    bool                               m_steamRunning            = false;
    ULONGLONG                          m_lastReconnectAttemptTick = 0;
    std::vector<RemapProfile>          m_profiles;
    std::wstring                       m_activeProfileId = L"default";

    static constexpr UINT IDM_TOGGLE        = 1001;
    static constexpr UINT IDM_EXIT          = 1002;
    static constexpr UINT IDM_TRACKPAD      = 1003;
    static constexpr UINT IDM_BACKBUTTONS   = 1004;
    static constexpr UINT IDM_LEFT_TRACKPAD = 1005;
    static constexpr UINT IDM_STARTUP       = 1006;
    static constexpr UINT IDM_AUTOENABLE    = 1007;
    static constexpr UINT IDM_OUTPUT_X360   = 1008;
    static constexpr UINT IDM_OUTPUT_DS4    = 1009;
    static constexpr UINT IDM_CONFIGURE_PADDLES   = 1400;
    static constexpr UINT WM_TRAY          = WM_APP + 1;
    static constexpr UINT TRAY_UID         = 1;
    static constexpr UINT TIMER_STEAM_POLL = 1;
    static constexpr UINT STEAM_POLL_MS    = 1000;
    static constexpr UINT RECONNECT_BACKOFF_MS = 3000;
};
