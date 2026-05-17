#pragma once
#include "VirtualController.h"
#include <Windows.h>
#include <commctrl.h>
#include <array>
#include <functional>
#include <string>
#include <vector>

class PaddleConfigWindow {
public:
    using SaveFn = std::function<void(const PaddleMappings&, const PaddleActionBindings&)>;
    using LoadMappingsFn = std::function<PaddleMappings()>;
    using LoadActionsFn = std::function<PaddleActionBindings()>;
    using ControllerChordFn = std::function<std::wstring()>;
    using LoadProfileIdFn = std::function<std::wstring()>;
    using ListInstalledGamesFn = std::function<std::vector<std::wstring>()>;
    using RefreshInstalledGamesFn = std::function<std::vector<std::wstring>()>;
    using LoadGameSourcesFn = std::function<std::vector<std::wstring>()>;
    using SaveGameSourcesFn = std::function<void(const std::vector<std::wstring>&)>;
    using LoadAutoSwitchFn = std::function<bool()>;
    using SaveAutoSwitchFn = std::function<void(bool)>;
    using SelectProfileFn = std::function<void(const std::wstring&)>;
    using DeleteProfileFn = std::function<void(const std::wstring&)>;

    PaddleConfigWindow(LoadMappingsFn loadMappings,
                       LoadActionsFn loadActions,
                       ControllerChordFn controllerChordFn,
                       LoadProfileIdFn loadProfileId,
                       ListInstalledGamesFn listInstalledGames,
                       RefreshInstalledGamesFn refreshInstalledGames,
                       LoadGameSourcesFn loadGameSources,
                       SaveGameSourcesFn saveGameSources,
                       LoadAutoSwitchFn loadAutoSwitch,
                       SaveAutoSwitchFn saveAutoSwitch,
                       SelectProfileFn onSelectProfile,
                       DeleteProfileFn onDeleteProfile,
                       SaveFn onSave);
    ~PaddleConfigWindow() = default;

    void Show(HINSTANCE hInstance, HWND owner);
    void Close();
    void ReloadFromModel();
    bool IsOpen() const { return m_hwnd != nullptr; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void CreateControls();
    void RefreshFromModel();
    void RefreshProfileState();
    void RefreshEditorForSelectedPaddle();
    void UpdateControlState();
    void RecordMacro();
    void ClearSelection();
    void CommitPendingChanges();
    void PersistCurrentState();
    void ApplySelection();
    void UseSelectedGameProfile();
    void RefreshInstalledGames(bool forceRefresh = false);
    void AddGameFolderSource();
    void AddGameExeSource();
    void RemoveSelectedGameSource();
    void RefreshGameSourcesUi();
    void SetRefreshUiState(bool refreshing, const wchar_t* statusText = nullptr);
    int CurrentModeSelection() const;
    void SetModeSelectionForCurrent(int modeIndex);
    void Paint(HDC hdc);
    RECT PaddleRect(int paddleIndex) const;
    POINT PaddleAnchor(int paddleIndex) const;
    PaddleAction* SelectedAction();
    PaddleMapping* SelectedMapping();
    std::wstring PaddleLabelText(int paddleIndex) const;
    int HitTestPaddleLabel(POINT pt) const;

    HWND m_hwnd = nullptr;
    HWND m_owner = nullptr;
    HINSTANCE m_hInstance = nullptr;

    HWND m_comboMode = nullptr;
    HWND m_comboGamepad = nullptr;
    HWND m_editBinding = nullptr;
    HWND m_staticBinding = nullptr;
    HWND m_staticSelected = nullptr;
    HWND m_staticProfile = nullptr;
    HWND m_comboGameProfiles = nullptr;
    HWND m_buttonRefreshLibrary = nullptr;
    HWND m_listGameSources = nullptr;
    HWND m_buttonAddFolder = nullptr;
    HWND m_buttonAddExe = nullptr;
    HWND m_buttonRemoveSource = nullptr;
    HWND m_staticRefreshStatus = nullptr;
    HWND m_progressRefresh = nullptr;
    HWND m_checkAutoSwitch = nullptr;
    HWND m_checkRapid = nullptr;
    HWND m_buttonRecord = nullptr;
    HWND m_buttonClear = nullptr;
    HWND m_hoverLabelPopup = nullptr;
    HWND m_tooltip = nullptr;

    LoadMappingsFn m_loadMappings;
    LoadActionsFn  m_loadActions;
    ControllerChordFn m_controllerChordFn;
    LoadProfileIdFn m_loadProfileId;
    ListInstalledGamesFn m_listInstalledGames;
    RefreshInstalledGamesFn m_refreshInstalledGames;
    LoadGameSourcesFn m_loadGameSources;
    SaveGameSourcesFn m_saveGameSources;
    LoadAutoSwitchFn m_loadAutoSwitch;
    SaveAutoSwitchFn m_saveAutoSwitch;
    SelectProfileFn m_onSelectProfile;
    SaveFn         m_onSave;

    PaddleMappings       m_mappings{};
    PaddleActionBindings m_actions{};
    int                  m_selectedPaddle = 0;
    int                  m_hoveredTooltipPaddle = -1;
    bool                 m_updatingControls = false;
    bool                 m_autoSwitchProfiles = false;
    std::array<int, 5>   m_modeSelections = { 0, 0, 0, 0, 0 };
    std::wstring         m_tooltipText;
    std::wstring         m_editProfileId = L"default";
    std::vector<std::wstring> m_gameSourceSpecs;
    std::vector<std::wstring> m_installedGames;
};
