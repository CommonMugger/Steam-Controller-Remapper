#include "MacroRecorder.h"
#include <algorithm>
#include <cwctype>
#include <sstream>
#include <set>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kClassName[] = L"XboxModeSteamlessControllerMacroRecorder";
constexpr int IDC_LIST = 3002;
constexpr int IDC_EDIT_STEP = 3004;
constexpr int IDC_DELETE_STEP = 3005;
constexpr int IDC_CLEAR_ALL = 3006;
constexpr UINT TIMER_CONTROLLER_POLL = 1;

bool IsModifier(WPARAM vk) {
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
           vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
           vk == VK_LWIN || vk == VK_RWIN;
}

bool IsControllerSaveChord(const std::wstring& chord) {
    return chord.find(L"MENU") != std::wstring::npos;
}

bool IsControllerCancelChord(const std::wstring& chord) {
    return chord.find(L"VIEW") != std::wstring::npos;
}

std::wstring VkName(UINT vk) {
    if (vk >= L'A' && vk <= L'Z') return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= L'0' && vk <= L'9') return std::wstring(1, static_cast<wchar_t>(vk));
    switch (vk) {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL: return L"CTRL";
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT: return L"SHIFT";
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU: return L"ALT";
    case VK_LWIN:
    case VK_RWIN: return L"WIN";
    case VK_TAB: return L"TAB";
    case VK_RETURN: return L"ENTER";
    case VK_SPACE: return L"SPACE";
    case VK_ESCAPE: return L"ESC";
    case VK_BACK: return L"BACKSPACE";
    case VK_UP: return L"UP";
    case VK_RIGHT: return L"RIGHT";
    case VK_DOWN: return L"DOWN";
    case VK_LEFT: return L"LEFT";
    default:
        if (vk >= VK_F1 && vk <= VK_F24)
            return L"F" + std::to_wstring((vk - VK_F1) + 1);
        return L"VK_" + std::to_wstring(vk);
    }
}

std::wstring JoinChord(const std::vector<UINT>& keys) {
    std::wstring text;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i != 0)
            text += L"+";
        text += VkName(keys[i]);
    }
    return text;
}

struct RecorderState {
    HWND hwnd = nullptr;
    HWND list = nullptr;
    bool accepted = false;
    std::set<UINT> held;
    std::vector<std::wstring> steps;
    std::wstring result;
    MacroRecorder::ControllerChordFn controllerChordFn;
    std::wstring lastControllerChord;
    int appendIndex = -1;
};

RecorderState* g_activeRecorder = nullptr;
HHOOK g_keyboardHook = nullptr;
HWND g_recorderWindow = nullptr;

std::vector<std::wstring> SplitChordTokens(const std::wstring& text) {
    std::vector<std::wstring> tokens;
    std::wstringstream stream(text);
    std::wstring item;
    while (std::getline(stream, item, L'+')) {
        if (!item.empty())
            tokens.push_back(item);
    }
    return tokens;
}

void SetStatus(RecorderState* state, const std::wstring& text) {
    (void)state;
    (void)text;
}

std::wstring Trim(std::wstring value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](wchar_t ch) {
        return !std::iswspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](wchar_t ch) {
        return !std::iswspace(ch);
    }).base(), value.end());
    return value;
}

void RenderList(RecorderState* state) {
    SendMessageW(state->list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < state->steps.size(); ++i) {
        std::wstring item = state->steps[i];
        if (static_cast<int>(i) == state->appendIndex)
            item = L"[Editing] " + item;
        SendMessageW(state->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }

    if (!state->steps.empty()) {
        const int selected = (state->appendIndex >= 0 && state->appendIndex < static_cast<int>(state->steps.size()))
            ? state->appendIndex
            : static_cast<int>(state->steps.size() - 1);
        SendMessageW(state->list, LB_SETCURSEL, static_cast<WPARAM>(selected), 0);
    }
}

void RefreshListItem(RecorderState* state, int index) {
    if (index < 0 || index >= static_cast<int>(state->steps.size())) return;
    RenderList(state);
    SendMessageW(state->list, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
}

std::wstring MergeSteps(const std::wstring& existing, const std::wstring& extra) {
    std::vector<std::wstring> merged = SplitChordTokens(existing);
    for (const std::wstring& token : SplitChordTokens(extra)) {
        if (std::find(merged.begin(), merged.end(), token) == merged.end())
            merged.push_back(token);
    }

    std::wstring result;
    for (size_t i = 0; i < merged.size(); ++i) {
        if (i != 0)
            result += L"+";
        result += merged[i];
    }
    return result;
}

void AddOrMergeStep(RecorderState* state, const std::wstring& step) {
    if (step.empty())
        return;

    if (state->appendIndex >= 0 && state->appendIndex < static_cast<int>(state->steps.size())) {
        state->steps[state->appendIndex] = MergeSteps(state->steps[state->appendIndex], step);
        RefreshListItem(state, state->appendIndex);
        SetStatus(state, L"Updated step: " + state->steps[state->appendIndex]);
        state->appendIndex = -1;
        return;
    }

    state->steps.push_back(step);
    RenderList(state);
    SendMessageW(state->list, LB_SETCURSEL, static_cast<WPARAM>(state->steps.size() - 1), 0);
    SetStatus(state, step);
}

void DeleteSelectedStep(RecorderState* state) {
    int index = static_cast<int>(SendMessageW(state->list, LB_GETCURSEL, 0, 0));
    if (index == LB_ERR) {
        if (state->steps.empty())
            return;
        index = static_cast<int>(state->steps.size() - 1);
    }

    state->steps.erase(state->steps.begin() + index);
    if (state->appendIndex == index)
        state->appendIndex = -1;
    else if (state->appendIndex > index)
        --state->appendIndex;
    RenderList(state);

    if (state->steps.empty()) {
        state->appendIndex = -1;
        SetStatus(state, L"Recording macro input");
        return;
    }

    const int newIndex = (index >= static_cast<int>(state->steps.size()))
        ? static_cast<int>(state->steps.size() - 1)
        : index;
    SendMessageW(state->list, LB_SETCURSEL, static_cast<WPARAM>(newIndex), 0);
    SetStatus(state, L"Selected: " + state->steps[newIndex]);
}

void BeginEditSelectedStep(RecorderState* state) {
    const int index = static_cast<int>(SendMessageW(state->list, LB_GETCURSEL, 0, 0));
    if (index == LB_ERR || index >= static_cast<int>(state->steps.size()))
        return;

    state->appendIndex = index;
    state->held.clear();
    RenderList(state);
    SendMessageW(state->list, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
    SetStatus(state, L"Append to selected step with next input...");
}

void ClearAllSteps(RecorderState* state) {
    state->held.clear();
    state->appendIndex = -1;
    state->steps.clear();
    RenderList(state);
    SetStatus(state, L"Recording macro input");
}

void FinishRecorder(RecorderState* state, bool accepted) {
    state->accepted = accepted;
    state->result.clear();
    if (accepted) {
        for (size_t i = 0; i < state->steps.size(); ++i) {
            if (i != 0)
                state->result += L", ";
            state->result += state->steps[i];
        }
    }
    DestroyWindow(state->hwnd);
}

LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wp, LPARAM lp) {
    if (code < 0 || !g_activeRecorder)
        return CallNextHookEx(g_keyboardHook, code, wp, lp);

    const KBDLLHOOKSTRUCT* key = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
    const UINT vk = key->vkCode;

    if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) {
        if (vk == VK_ESCAPE) {
            FinishRecorder(g_activeRecorder, false);
            return 1;
        }
        if (vk == VK_RETURN) {
            FinishRecorder(g_activeRecorder, true);
            return 1;
        }
        if (vk == VK_BACK) {
            g_activeRecorder->held.clear();
            DeleteSelectedStep(g_activeRecorder);
            return 1;
        }

        const bool inserted = g_activeRecorder->held.insert(vk).second;
        if (inserted && !IsModifier(vk)) {
            std::vector<UINT> chord(g_activeRecorder->held.begin(), g_activeRecorder->held.end());
            AddOrMergeStep(g_activeRecorder, JoinChord(chord));
            g_activeRecorder->held.clear();
        }
    } else if (wp == WM_KEYUP || wp == WM_SYSKEYUP) {
        if (IsModifier(vk) &&
            g_activeRecorder->held.size() == 1 &&
            g_activeRecorder->held.count(vk) != 0) {
            AddOrMergeStep(g_activeRecorder, JoinChord(std::vector<UINT>{vk}));
            g_activeRecorder->held.clear();
        }
        g_activeRecorder->held.erase(vk);
    }

    return 1;
}

LRESULT CALLBACK RecorderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    RecorderState* state = reinterpret_cast<RecorderState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        state = reinterpret_cast<RecorderState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    if (!state)
        return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_CREATE:
        state->list = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
                                    16, 16, 328, 200, hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_LIST))), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Edit Step", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                      16, 228, 92, 26, hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_EDIT_STEP))), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Delete Step", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                      116, 228, 92, 26, hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_DELETE_STEP))), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Clear All", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                      216, 228, 92, 26, hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_CLEAR_ALL))), nullptr, nullptr);
        RenderList(state);
        SetTimer(hwnd, TIMER_CONTROLLER_POLL, 30, nullptr);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_EDIT_STEP && HIWORD(wp) == BN_CLICKED) {
            BeginEditSelectedStep(state);
            return 0;
        }
        if (LOWORD(wp) == IDC_DELETE_STEP && HIWORD(wp) == BN_CLICKED) {
            DeleteSelectedStep(state);
            return 0;
        }
        if (LOWORD(wp) == IDC_CLEAR_ALL && HIWORD(wp) == BN_CLICKED) {
            ClearAllSteps(state);
            return 0;
        }
        if (LOWORD(wp) == IDC_LIST && HIWORD(wp) == LBN_DBLCLK) {
            BeginEditSelectedStep(state);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wp == TIMER_CONTROLLER_POLL && state->controllerChordFn) {
            const std::wstring chord = state->controllerChordFn();
            if (!chord.empty() && state->lastControllerChord.empty()) {
                if (IsControllerSaveChord(chord)) {
                    FinishRecorder(state, true);
                    return 0;
                }
                if (IsControllerCancelChord(chord)) {
                    FinishRecorder(state, false);
                    return 0;
                }
                AddOrMergeStep(state, chord);
            }
            state->lastControllerChord = chord;
        }
        return 0;
    case WM_CLOSE:
        state->accepted = false;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_CONTROLLER_POLL);
        g_recorderWindow = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

bool MacroRecorder::Record(HWND owner, std::wstring& macroText, const std::wstring& initialMacroText,
                           ControllerChordFn controllerChordFn) {
    if (g_recorderWindow && IsWindow(g_recorderWindow)) {
        ShowWindow(g_recorderWindow, SW_SHOWNORMAL);
        SetForegroundWindow(g_recorderWindow);
        return false;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = RecorderProc;
    wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    RecorderState state{};
    state.controllerChordFn = std::move(controllerChordFn);
    {
        std::wstringstream stream(initialMacroText);
        std::wstring step;
        while (std::getline(stream, step, L',')) {
            step = Trim(step);
            if (!step.empty())
                state.steps.push_back(step);
        }
    }
    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        kClassName,
        L"Record Macro",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 392, 310,
        owner, nullptr, wc.hInstance, &state);
    if (!hwnd)
        return false;

    g_recorderWindow = hwnd;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    g_activeRecorder = &state;
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandleW(nullptr), 0);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
    g_activeRecorder = nullptr;

    if (!state.accepted)
        return false;

    macroText = state.result;
    return true;
}
