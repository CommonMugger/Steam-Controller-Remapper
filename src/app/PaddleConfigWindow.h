#pragma once
#include "VirtualController.h"
#include <Windows.h>
#include <commctrl.h>
#include <array>
#include <functional>
#include <string>

class PaddleConfigWindow {
public:
    using SaveFn = std::function<void(const PaddleMappings&, const PaddleActionBindings&)>;
    using LoadMappingsFn = std::function<PaddleMappings()>;
    using LoadActionsFn = std::function<PaddleActionBindings()>;
    using ControllerChordFn = std::function<std::wstring()>;

    PaddleConfigWindow(LoadMappingsFn loadMappings,
                       LoadActionsFn loadActions,
                       ControllerChordFn controllerChordFn,
                       SaveFn onSave);
    ~PaddleConfigWindow() = default;

    void Show(HINSTANCE hInstance, HWND owner);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void CreateControls();
    void RefreshFromModel();
    void RefreshEditorForSelectedPaddle();
    void UpdateControlState();
    void RecordMacro();
    void ClearSelection();
    void PersistCurrentState();
    void ApplySelection();
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
    HWND m_checkRapid = nullptr;
    HWND m_buttonRecord = nullptr;
    HWND m_buttonClear = nullptr;
    HWND m_tooltip = nullptr;

    LoadMappingsFn m_loadMappings;
    LoadActionsFn  m_loadActions;
    ControllerChordFn m_controllerChordFn;
    SaveFn         m_onSave;

    PaddleMappings       m_mappings{};
    PaddleActionBindings m_actions{};
    int                  m_selectedPaddle = 0;
    int                  m_hoveredTooltipPaddle = -1;
    bool                 m_updatingControls = false;
    std::array<int, 5>   m_modeSelections = { 0, 0, 0, 0, 0 };
    std::wstring         m_tooltipText;
};
