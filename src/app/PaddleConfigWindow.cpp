#include "PaddleConfigWindow.h"
#include "MacroRecorder.h"
#include "PaddleConfig.h"
#include "resource.h"
#include <gdiplus.h>
#include <memory>
#include <windowsx.h>
#include <string>

namespace {
constexpr wchar_t kClassName[] = L"XboxModeSteamlessControllerPaddleConfig";
constexpr int IDC_MODE = 2001;
constexpr int IDC_GAMEPAD = 2002;
constexpr int IDC_BINDING = 2003;
constexpr int IDC_CLOSE = 2005;
constexpr int IDC_SELECTED = 2006;
constexpr int IDC_RAPID = 2008;
constexpr int IDC_RECORD = 2009;
constexpr int IDC_CLEAR = 2010;
constexpr UINT_PTR TOOLTIP_BASE_ID = 5000;
constexpr int kButtonCount = 5;

ULONG_PTR EnsureGdiplus() {
    static ULONG_PTR token = 0;
    static bool initialized = false;
    if (!initialized) {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
        initialized = true;
    }
    return token;
}

Gdiplus::Image* LoadControllerImage() {
    EnsureGdiplus();
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_CONTROLLER_IMAGE), reinterpret_cast<LPCWSTR>(RT_RCDATA));
    if (!resource)
        return nullptr;
    const DWORD size = SizeofResource(nullptr, resource);
    HGLOBAL handle = LoadResource(nullptr, resource);
    if (!handle)
        return nullptr;
    void* data = LockResource(handle);
    if (!data)
        return nullptr;

    HGLOBAL buffer = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!buffer)
        return nullptr;
    void* dest = GlobalLock(buffer);
    memcpy(dest, data, size);
    GlobalUnlock(buffer);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(buffer, TRUE, &stream) != S_OK) {
        GlobalFree(buffer);
        return nullptr;
    }

    Gdiplus::Image* image = Gdiplus::Image::FromStream(stream);
    stream->Release();
    return image;
}

const wchar_t* PaddleName(int index) {
    switch (index) {
    case 0: return L"L4";
    case 1: return L"L5";
    case 2: return L"R4";
    case 3: return L"R5";
    default: return L"QAM";
    }
}

const PaddleMapping kGamepadOptions[] = {
    PaddleMapping::None,
    PaddleMapping::A,
    PaddleMapping::B,
    PaddleMapping::X,
    PaddleMapping::Y,
    PaddleMapping::LeftShoulder,
    PaddleMapping::RightShoulder,
    PaddleMapping::View,
    PaddleMapping::Menu,
    PaddleMapping::LeftThumb,
    PaddleMapping::RightThumb,
    PaddleMapping::Guide,
    PaddleMapping::DPadUp,
    PaddleMapping::DPadRight,
    PaddleMapping::DPadDown,
    PaddleMapping::DPadLeft,
};

const wchar_t* GamepadName(PaddleMapping mapping) {
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

std::wstring ActionTextForEditor(const PaddleAction& action, PaddleMapping fallback) {
    if (action.type == PaddleActionType::Macro) {
        std::wstring text = PaddleConfig::Describe(action, fallback);
        if (text.rfind(L"Macro: ", 0) == 0)
            text.erase(0, 7);
        const size_t suffix = text.find(L" [");
        if (suffix != std::wstring::npos)
            text.erase(suffix);
        return text;
    }
    if (action.type == PaddleActionType::KeyChord) {
        std::wstring text = PaddleConfig::Describe(action, fallback);
        const size_t suffix = text.find(L" [");
        if (suffix != std::wstring::npos)
            text.erase(suffix);
        return text;
    }
    return L"";
}
}

PaddleConfigWindow::PaddleConfigWindow(LoadMappingsFn loadMappings,
                                       LoadActionsFn loadActions,
                                       ControllerChordFn controllerChordFn,
                                       SaveFn onSave)
    : m_loadMappings(std::move(loadMappings)),
      m_loadActions(std::move(loadActions)),
      m_controllerChordFn(std::move(controllerChordFn)),
      m_onSave(std::move(onSave)) {}

void PaddleConfigWindow::Show(HINSTANCE hInstance, HWND owner) {
    m_owner = owner;
    m_hInstance = hInstance;

    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(m_hwnd);
        RefreshFromModel();
        return;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        kClassName,
        L"Remap Buttons",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 920, 620,
        owner, nullptr, hInstance, this);

    CreateControls();
    RefreshFromModel();
    ShowWindow(m_hwnd, SW_SHOWNORMAL);
    UpdateWindow(m_hwnd);
}

LRESULT CALLBACK PaddleConfigWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PaddleConfigWindow* self = reinterpret_cast<PaddleConfigWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lp);
        self = reinterpret_cast<PaddleConfigWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self)
        return self->HandleMessage(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT PaddleConfigWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case IDC_RECORD:
            RecordMacro();
            return 0;
        case IDC_CLEAR:
            ClearSelection();
            return 0;
        case IDC_MODE:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                SetModeSelectionForCurrent(static_cast<int>(SendMessageW(m_comboMode, CB_GETCURSEL, 0, 0)));
                UpdateControlState();
                if (!m_updatingControls && CurrentModeSelection() != 1 && CurrentModeSelection() != 2)
                    ApplySelection();
            }
            return 0;
        case IDC_GAMEPAD:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                InvalidateRect(hwnd, nullptr, TRUE);
                if (!m_updatingControls)
                    ApplySelection();
            }
            return 0;
        case IDC_RAPID:
            if (HIWORD(wp) == BN_CLICKED && !m_updatingControls)
                ApplySelection();
            return 0;
        case IDC_BINDING:
            if (HIWORD(wp) == EN_KILLFOCUS && !m_updatingControls)
                ApplySelection();
            return 0;
        }
        break;
    case WM_LBUTTONDOWN: {
        const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        for (int i = 0; i < kButtonCount; ++i) {
            RECT rect = PaddleRect(i);
            if (PtInRect(&rect, pt)) {
                m_selectedPaddle = i;
                RefreshEditorForSelectedPaddle();
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
        }
        break;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&tme);
        const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        const int hovered = HitTestPaddleLabel(pt);
        if (hovered != m_hoveredTooltipPaddle && m_tooltip) {
            m_hoveredTooltipPaddle = hovered;
            if (hovered >= 0) {
                std::wstring text = PaddleLabelText(hovered);
                m_tooltipText = std::move(text);
                TOOLINFOW ti{};
                ti.cbSize = sizeof(ti);
                ti.hwnd = m_hwnd;
                ti.uId = TOOLTIP_BASE_ID;
                ti.lpszText = const_cast<wchar_t*>(m_tooltipText.c_str());
                SendMessageW(m_tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
                SendMessageW(m_tooltip, TTM_TRACKPOSITION, 0, MAKELONG(pt.x + 16, pt.y + 24));
                SendMessageW(m_tooltip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&ti));
            } else {
                TOOLINFOW ti{};
                ti.cbSize = sizeof(ti);
                ti.hwnd = m_hwnd;
                ti.uId = TOOLTIP_BASE_ID;
                SendMessageW(m_tooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&ti));
            }
        } else if (hovered >= 0 && m_tooltip) {
            SendMessageW(m_tooltip, TTM_TRACKPOSITION, 0, MAKELONG(pt.x + 16, pt.y + 24));
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (m_tooltip) {
            TOOLINFOW ti{};
            ti.cbSize = sizeof(ti);
            ti.hwnd = m_hwnd;
            ti.uId = TOOLTIP_BASE_ID;
            SendMessageW(m_tooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&ti));
        }
        m_hoveredTooltipPaddle = -1;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        Paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        m_hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void PaddleConfigWindow::CreateControls() {
    CreateWindowW(L"STATIC", L"Selected button:", WS_CHILD | WS_VISIBLE,
                  620, 30, 120, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    m_staticSelected = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                     740, 30, 150, 20, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_SELECTED))), m_hInstance, nullptr);

    CreateWindowW(L"STATIC", L"Action type:", WS_CHILD | WS_VISIBLE,
                  620, 70, 120, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    m_comboMode = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                620, 92, 240, 220, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_MODE))), m_hInstance, nullptr);
    SendMessageW(m_comboMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Gamepad button"));
    SendMessageW(m_comboMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Key chord / modifier"));
    SendMessageW(m_comboMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Macro"));
    SendMessageW(m_comboMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Unmapped"));

    CreateWindowW(L"STATIC", L"Gamepad target:", WS_CHILD | WS_VISIBLE,
                  620, 130, 120, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    m_comboGamepad = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                   620, 152, 240, 300, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_GAMEPAD))), m_hInstance, nullptr);
    for (const PaddleMapping mapping : kGamepadOptions)
        SendMessageW(m_comboGamepad, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(GamepadName(mapping)));

    m_staticBinding = CreateWindowW(L"STATIC", L"Binding:", WS_CHILD | WS_VISIBLE,
                                    620, 190, 120, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    m_editBinding = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                                  620, 212, 240, 24, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_BINDING))), m_hInstance, nullptr);

    m_checkRapid = CreateWindowW(L"BUTTON", L"Rapid fire", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                 620, 258, 160, 22, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_RAPID))), m_hInstance, nullptr);
    m_buttonRecord = CreateWindowW(L"BUTTON", L"Record Macro...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                   620, 290, 120, 26, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_RECORD))), m_hInstance, nullptr);
    m_buttonClear = CreateWindowW(L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  750, 290, 110, 26, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_CLEAR))), m_hInstance, nullptr);

    CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  760, 338, 100, 30, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_CLOSE))), m_hInstance, nullptr);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    m_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                m_hwnd, nullptr, m_hInstance, nullptr);
    SetWindowPos(m_tooltip, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_TRACK;
    ti.hwnd = m_hwnd;
    ti.uId = TOOLTIP_BASE_ID;
    ti.lpszText = const_cast<wchar_t*>(L"");
    SendMessageW(m_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

void PaddleConfigWindow::RefreshFromModel() {
    m_mappings = m_loadMappings();
    m_actions = m_loadActions();
    const PaddleAction actionList[] = { m_actions.l4, m_actions.l5, m_actions.r4, m_actions.r5, m_actions.qam };
    for (int i = 0; i < kButtonCount; ++i) {
        switch (actionList[i].type) {
        case PaddleActionType::UseMenuMapping:
        case PaddleActionType::Gamepad:
            m_modeSelections[i] = 0;
            break;
        case PaddleActionType::KeyChord:
            m_modeSelections[i] = 1;
            break;
        case PaddleActionType::Macro:
            m_modeSelections[i] = 2;
            break;
        case PaddleActionType::None:
            m_modeSelections[i] = 3;
            break;
        }
    }
    RefreshEditorForSelectedPaddle();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

PaddleAction* PaddleConfigWindow::SelectedAction() {
    switch (m_selectedPaddle) {
    case 0: return &m_actions.l4;
    case 1: return &m_actions.l5;
    case 2: return &m_actions.r4;
    case 3: return &m_actions.r5;
    default: return &m_actions.qam;
    }
}

PaddleMapping* PaddleConfigWindow::SelectedMapping() {
    switch (m_selectedPaddle) {
    case 0: return &m_mappings.l4;
    case 1: return &m_mappings.l5;
    case 2: return &m_mappings.r4;
    case 3: return &m_mappings.r5;
    default: return &m_mappings.qam;
    }
}

void PaddleConfigWindow::RefreshEditorForSelectedPaddle() {
    if (!m_hwnd)
        return;

    m_updatingControls = true;
    SetWindowTextW(m_staticSelected, PaddleName(m_selectedPaddle));
    PaddleAction* action = SelectedAction();
    PaddleMapping* mapping = SelectedMapping();

    int modeIndex = m_modeSelections[m_selectedPaddle];
    PaddleMapping gamepadTarget = *mapping;

    if (modeIndex == 0 && action->type == PaddleActionType::Gamepad) {
        gamepadTarget = action->gamepadMapping;
    }

    SendMessageW(m_comboMode, CB_SETCURSEL, modeIndex, 0);

    int gamepadIndex = 0;
    for (int i = 0; i < static_cast<int>(std::size(kGamepadOptions)); ++i) {
        if (kGamepadOptions[i] == gamepadTarget) {
            gamepadIndex = i;
            break;
        }
    }
    SendMessageW(m_comboGamepad, CB_SETCURSEL, gamepadIndex, 0);

    SetWindowTextW(m_editBinding, ActionTextForEditor(*action, *mapping).c_str());
    Button_SetCheck(m_checkRapid, action->rapidFire ? BST_CHECKED : BST_UNCHECKED);
    UpdateControlState();
    m_updatingControls = false;
}

void PaddleConfigWindow::UpdateControlState() {
    const int modeIndex = CurrentModeSelection();
    const bool gamepadMode = (modeIndex == 0);
    const bool noneMode = (modeIndex == 3);
    const bool macroMode = (modeIndex == 2);
    const bool keyMode = (modeIndex == 1);
    EnableWindow(m_comboGamepad, gamepadMode);
    EnableWindow(m_editBinding, keyMode);
    ShowWindow(m_staticBinding, keyMode ? SW_SHOW : SW_HIDE);
    ShowWindow(m_editBinding, keyMode ? SW_SHOW : SW_HIDE);
    EnableWindow(m_checkRapid, !noneMode);
    EnableWindow(m_buttonRecord, macroMode);
    ShowWindow(m_buttonRecord, macroMode ? SW_SHOW : SW_HIDE);
    ShowWindow(m_buttonClear, noneMode ? SW_HIDE : SW_SHOW);
}

int PaddleConfigWindow::CurrentModeSelection() const {
    return m_modeSelections[m_selectedPaddle];
}

void PaddleConfigWindow::SetModeSelectionForCurrent(int modeIndex) {
    if (modeIndex < 0 || modeIndex > 3)
        modeIndex = 0;
    m_modeSelections[m_selectedPaddle] = modeIndex;
}

void PaddleConfigWindow::RecordMacro() {
    wchar_t buffer[512] = {};
    GetWindowTextW(m_editBinding, buffer, static_cast<int>(std::size(buffer)));
    std::wstring macroText;
    if (!MacroRecorder::Record(m_hwnd, macroText, buffer, m_controllerChordFn))
        return;

    SetWindowTextW(m_editBinding, macroText.c_str());
    SetModeSelectionForCurrent(2);
    ApplySelection();
}

void PaddleConfigWindow::ClearSelection() {
    PaddleAction* action = SelectedAction();
    PaddleMapping* mapping = SelectedMapping();
    *mapping = PaddleMapping::None;
    *action = PaddleAction{PaddleActionType::None};
    SetModeSelectionForCurrent(3);
    PersistCurrentState();
    RefreshEditorForSelectedPaddle();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void PaddleConfigWindow::PersistCurrentState() {
    m_onSave(m_mappings, m_actions);
}

void PaddleConfigWindow::ApplySelection() {
    PaddleAction* action = SelectedAction();
    PaddleMapping* mapping = SelectedMapping();
    const int modeIndex = CurrentModeSelection();

    if (modeIndex == 0) {
        const int gamepadIndex = static_cast<int>(SendMessageW(m_comboGamepad, CB_GETCURSEL, 0, 0));
        const PaddleMapping selected = (gamepadIndex >= 0 && gamepadIndex < static_cast<int>(std::size(kGamepadOptions)))
            ? kGamepadOptions[gamepadIndex]
            : PaddleMapping::None;
        *mapping = selected;
        action->type = PaddleActionType::Gamepad;
        action->gamepadMapping = selected;
        action->chord.clear();
        action->macroSteps.clear();
    } else if (modeIndex == 1 || modeIndex == 2) {
        wchar_t buffer[512] = {};
        GetWindowTextW(m_editBinding, buffer, static_cast<int>(std::size(buffer)));
        std::wstring raw = buffer;
        if (raw.empty()) {
            RefreshEditorForSelectedPaddle();
            InvalidateRect(m_hwnd, nullptr, TRUE);
            return;
        }
        std::wstring prefixed = (modeIndex == 1 ? L"key:" : L"macro:") + raw;
        PaddleAction parsed{};
        if (PaddleConfig::ParseActionString(prefixed, parsed)) {
            *action = std::move(parsed);
        } else {
            RefreshEditorForSelectedPaddle();
            InvalidateRect(m_hwnd, nullptr, TRUE);
            return;
        }
    } else {
        *action = PaddleAction{PaddleActionType::None};
    }

    action->rapidFire = Button_GetCheck(m_checkRapid) == BST_CHECKED;
    if (action->type == PaddleActionType::None || action->type == PaddleActionType::UseMenuMapping) {
        action->rapidFire = false;
    }

    PersistCurrentState();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

RECT PaddleConfigWindow::PaddleRect(int paddleIndex) const {
    switch (paddleIndex) {
    case 0: return RECT{28, 224, 152, 260};
    case 1: return RECT{28, 278, 152, 314};
    case 2: return RECT{438, 224, 562, 260};
    case 3: return RECT{438, 278, 562, 314};
    default: return RECT{234, 328, 358, 364};
    }
}

POINT PaddleConfigWindow::PaddleAnchor(int paddleIndex) const {
    switch (paddleIndex) {
    case 0: return POINT{192, 251};
    case 1: return POINT{180, 306};
    case 2: return POINT{410, 251};
    case 3: return POINT{421, 306};
    default: return POINT{295, 300};
    }
}

std::wstring PaddleConfigWindow::PaddleLabelText(int paddleIndex) const {
    const PaddleAction paddleActions[] = { m_actions.l4, m_actions.l5, m_actions.r4, m_actions.r5, m_actions.qam };
    const PaddleMapping paddleMappings[] = { m_mappings.l4, m_mappings.l5, m_mappings.r4, m_mappings.r5, m_mappings.qam };
    if (paddleIndex < 0 || paddleIndex >= kButtonCount)
        return {};
    return std::wstring(PaddleName(paddleIndex)) + L": " +
        PaddleConfig::Describe(paddleActions[paddleIndex], paddleMappings[paddleIndex]);
}

int PaddleConfigWindow::HitTestPaddleLabel(POINT pt) const {
    for (int i = 0; i < kButtonCount; ++i) {
        RECT rect = PaddleRect(i);
        if (PtInRect(&rect, pt))
            return i;
    }
    return -1;
}

void PaddleConfigWindow::Paint(HDC hdc) {
    static std::unique_ptr<Gdiplus::Image> controllerImage(LoadControllerImage());
    RECT client{};
    GetClientRect(m_hwnd, &client);
    HBRUSH bg = CreateSolidBrush(RGB(245, 246, 248));
    FillRect(hdc, &client, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(30, 33, 38));

    if (controllerImage && controllerImage->GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(controllerImage.get(), Gdiplus::Rect(20, 32, 560, 379));
    } else {
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(60, 68, 80));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HBRUSH body = CreateSolidBrush(RGB(205, 211, 220));
        HGDIOBJ oldBrush = SelectObject(hdc, body);
        RoundRect(hdc, 60, 80, 540, 410, 120, 120);
        SelectObject(hdc, oldBrush);
        DeleteObject(body);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    for (int i = 0; i < kButtonCount; ++i) {
        RECT rect = PaddleRect(i);
        COLORREF fill = (i == m_selectedPaddle) ? RGB(30, 120, 200) : RGB(255, 255, 255);
        COLORREF text = (i == m_selectedPaddle) ? RGB(255, 255, 255) : RGB(20, 24, 28);
        HBRUSH pill = CreateSolidBrush(fill);
        FillRect(hdc, &rect, pill);
        DeleteObject(pill);
        FrameRect(hdc, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        POINT anchor = PaddleAnchor(i);
        HPEN arrowPen = CreatePen(PS_SOLID, 2, fill);
        HGDIOBJ oldArrowPen = SelectObject(hdc, arrowPen);
        const int midY = (rect.top + rect.bottom) / 2;
        const int targetX = (i < 2) ? rect.right : ((i < 4) ? rect.left : (rect.left + rect.right) / 2);
        MoveToEx(hdc, anchor.x, anchor.y, nullptr);
        LineTo(hdc, targetX, midY);
        if (i < 2) {
            LineTo(hdc, targetX - 10, midY - 6);
            MoveToEx(hdc, targetX, midY, nullptr);
            LineTo(hdc, targetX - 10, midY + 6);
        } else if (i < 4) {
            LineTo(hdc, targetX + 10, midY - 6);
            MoveToEx(hdc, targetX, midY, nullptr);
            LineTo(hdc, targetX + 10, midY + 6);
        } else {
            LineTo(hdc, targetX - 6, midY + 10);
            MoveToEx(hdc, targetX, midY, nullptr);
            LineTo(hdc, targetX + 6, midY + 10);
        }
        SelectObject(hdc, oldArrowPen);
        DeleteObject(arrowPen);

        std::wstring label = PaddleLabelText(i);
        SetTextColor(hdc, text);
        RECT textRect = rect;
        textRect.left += 6;
        DrawTextW(hdc, label.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}
