#include "PaddleOverlay.h"
#include "logging/Log.h"
#include "steam/SteamController.h"
#include <Windows.h>
#include <array>
#include <thread>
#include <utility>
#include <vector>

namespace {
bool IsPressed(const uint8_t* buf, size_t n, int index, const StandardGamepadState* standardState) {
    if (standardState && standardState->connected) {
        switch (index) {
        case  0: return standardState->leftPaddle1;
        case  1: return standardState->leftPaddle2;
        case  2: return standardState->rightPaddle1;
        case  3: return standardState->rightPaddle2;
        case  4: return standardState->misc1 || standardState->touchpadButton;
        case  5: return standardState->a;
        case  6: return standardState->b;
        case  7: return standardState->x;
        case  8: return standardState->y;
        case  9: return standardState->leftShoulder;
        case 10: return standardState->rightShoulder;
        case 11: return standardState->back;
        case 12: return standardState->start;
        case 13: return standardState->guide;
        case 14: return standardState->leftStick;
        case 15: return standardState->rightStick;
        case 16: return standardState->dpadUp;
        case 17: return standardState->dpadDown;
        case 18: return standardState->dpadLeft;
        case 19: return standardState->dpadRight;
        case 20: return standardState->leftTrigger  > 50;
        case 21: return standardState->rightTrigger > 50;
        default: return false;
        }
    }
    if (!SteamController::UsesLegacyStateLayout(buf, n))
        return false;
    const uint8_t b0 = buf[2];
    const uint8_t b1 = buf[3];
    const uint8_t b2 = buf[4];
    switch (index) {
    case  0: return (b2 & SteamController::BTN_L4)       != 0;
    case  1: return (b2 & SteamController::BTN_L5)       != 0;
    case  2: return (b0 & SteamController::BTN_R4)       != 0;
    case  3: return (b1 & SteamController::BTN_R5)       != 0;
    case  4: return (b0 & SteamController::BTN_QAM)      != 0;
    case  5: return (b0 & SteamController::BTN_A)        != 0;
    case  6: return (b0 & SteamController::BTN_B)        != 0;
    case  7: return (b0 & SteamController::BTN_X)        != 0;
    case  8: return (b0 & SteamController::BTN_Y)        != 0;
    case  9: return (b2 & SteamController::BTN_LB)       != 0;
    case 10: return (b1 & SteamController::BTN_RB)       != 0;
    case 11: return (b1 & SteamController::BTN_VIEW)     != 0;
    case 12: return (b0 & SteamController::BTN_MENU)     != 0;
    case 13: return (b2 & SteamController::BTN_STEAM)    != 0;
    case 14: return (b1 & SteamController::BTN_LS)       != 0;
    case 15: return (b0 & SteamController::BTN_RS)       != 0;
    case 16: return (b1 & SteamController::BTN_DPAD_UP)  != 0;
    case 17: return (b1 & SteamController::BTN_DPAD_DN)  != 0;
    case 18: return (b1 & SteamController::BTN_DPAD_LT)  != 0;
    case 19: return (b1 & SteamController::BTN_DPAD_RT)  != 0;
    case 20: return n > 7 && (static_cast<int16_t>(buf[6] | (buf[7] << 8))) > 0x1000;
    case 21: return n > 9 && (static_cast<int16_t>(buf[8] | (buf[9] << 8))) > 0x1000;
    default: return false;
    }
}

const wchar_t* ButtonName(int index) {
    switch (index) {
    case  0: return L"L4";
    case  1: return L"L5";
    case  2: return L"R4";
    case  3: return L"R5";
    case  4: return L"QAM";
    case  5: return L"A";
    case  6: return L"B";
    case  7: return L"X";
    case  8: return L"Y";
    case  9: return L"LB";
    case 10: return L"RB";
    case 11: return L"View";
    case 12: return L"Menu";
    case 13: return L"Guide";
    case 14: return L"L3";
    case 15: return L"R3";
    case 16: return L"DPadUp";
    case 17: return L"DPadDown";
    case 18: return L"DPadLeft";
    case 19: return L"DPadRight";
    case 20: return L"L2";
    case 21: return L"R2";
    default: return L"?";
    }
}

const char* ActionTypeName(PaddleActionType type) {
    switch (type) {
    case PaddleActionType::UseMenuMapping: return "menu";
    case PaddleActionType::None: return "none";
    case PaddleActionType::Gamepad: return "gamepad";
    case PaddleActionType::KeyChord: return "key";
    case PaddleActionType::Macro: return "macro";
    }
    return "unknown";
}

void SendKeyEvent(uint16_t vk, DWORD flags) {
    HWND fgWnd = GetForegroundWindow();
    DWORD fgThread = fgWnd ? GetWindowThreadProcessId(fgWnd, nullptr) : 0;
    DWORD myThread = GetCurrentThreadId();
    if (fgThread && fgThread != myThread)
        AttachThreadInput(myThread, fgThread, TRUE);

    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(vk);
    input.ki.dwFlags = flags;
    SendInput(1, &input, sizeof(INPUT));

    if (fgThread && fgThread != myThread)
        AttachThreadInput(myThread, fgThread, FALSE);
}

void SendChordDown(const std::vector<uint16_t>& chord) {
    for (uint16_t vk : chord) SendKeyEvent(vk, 0);
}

void SendChordUp(const std::vector<uint16_t>& chord) {
    for (auto it = chord.rbegin(); it != chord.rend(); ++it)
        SendKeyEvent(*it, KEYEVENTF_KEYUP);
}

using KeyChordCb     = std::function<void(const std::vector<uint16_t>&, bool)>;
using GamepadChordCb = std::function<void(const std::vector<PaddleMapping>&, bool)>;

static bool IsGamepadEncoded(uint16_t v) { return (v & 0x8000u) != 0; }
static PaddleMapping DecodeGamepadMapping(uint16_t v) {
    return static_cast<PaddleMapping>(static_cast<int>(v & 0x7FFFu));
}

void RunMacro(std::vector<std::vector<uint16_t>> steps, KeyChordCb keyCb, GamepadChordCb gpCb) {
    std::thread([steps = std::move(steps), keyCb, gpCb]() {
        for (const auto& chord : steps) {
            std::vector<uint16_t> keyPart;
            std::vector<PaddleMapping> gpPart;
            for (uint16_t v : chord) {
                if (IsGamepadEncoded(v)) gpPart.push_back(DecodeGamepadMapping(v));
                else keyPart.push_back(v);
            }
            if (!keyPart.empty()) { if (keyCb) keyCb(keyPart, true); else SendChordDown(keyPart); }
            if (!gpPart.empty() && gpCb) gpCb(gpPart, true);
            Sleep(20);
            if (!keyPart.empty()) { if (keyCb) keyCb(keyPart, false); else SendChordUp(keyPart); }
            if (!gpPart.empty() && gpCb) gpCb(gpPart, false);
            Sleep(30);
        }
    }).detach();
}

void TapChord(const std::vector<uint16_t>& chord, const KeyChordCb& cb) {
    if (cb) cb(chord, true); else SendChordDown(chord);
    Sleep(15);
    if (cb) cb(chord, false); else SendChordUp(chord);
}
}

void PaddleOverlay::SetBindings(PaddleActionBindings bindings) {
    m_bindings = std::move(bindings);
}

void PaddleOverlay::Reset() {
    for (int i = 0; i < kTotalButtonCount; ++i) {
        const PaddleAction* ap = GetButtonAction(m_bindings, i);
        if (ap && m_prevPressed[i] &&
            ap->type == PaddleActionType::KeyChord &&
            !ap->rapidFire) {
            if (m_keyChordCallback) m_keyChordCallback(ap->chord, false);
            else SendChordUp(ap->chord);
        }
        m_prevPressed[i] = false;
        m_lastFireTickMs[i] = 0;
    }
    m_hasSeededState = false;
}

void PaddleOverlay::Update(const uint8_t* buf, size_t n, const StandardGamepadState* standardState) {
    if ((!standardState || !standardState->connected) && !SteamController::UsesLegacyStateLayout(buf, n))
        return;

    if (!m_hasSeededState) {
        for (int i = 0; i < kTotalButtonCount; ++i)
            m_prevPressed[i] = IsPressed(buf, n, i, standardState);
        m_hasSeededState = true;
        return;
    }

    for (int i = 0; i < kTotalButtonCount; ++i) {
        const PaddleAction* ap = GetButtonAction(m_bindings, i);
        if (!ap) continue;
        const PaddleAction& action = *ap;

        // PaddleOverlay only handles key/macro actions. Gamepad remaps and
        // UseMenuMapping are handled inside VirtualController::Update().
        if (action.type != PaddleActionType::KeyChord &&
            action.type != PaddleActionType::Macro)
            continue;

        const bool pressed = IsPressed(buf, n, i, standardState);
        const ULONGLONG now = GetTickCount64();

        if (pressed) {
            const bool rising = !m_prevPressed[i];
            const bool rapidReady = action.rapidFire &&
                (rising || (now - m_lastFireTickMs[i]) >= 90);

            if (action.type == PaddleActionType::KeyChord) {
                if (action.rapidFire && rapidReady) {
                    logging::Logf("[PaddleOverlay] Fire btn=%S action=%s rapid=1", ButtonName(i), ActionTypeName(action.type));
                    TapChord(action.chord, m_keyChordCallback);
                    m_lastFireTickMs[i] = now;
                } else if (rising) {
                    logging::Logf("[PaddleOverlay] Down btn=%S action=%s", ButtonName(i), ActionTypeName(action.type));
                    if (m_keyChordCallback) m_keyChordCallback(action.chord, true);
                    else SendChordDown(action.chord);
                }
            } else if (action.type == PaddleActionType::Macro) {
                if (rising || rapidReady) {
                    logging::Logf("[PaddleOverlay] Fire btn=%S action=%s rapid=%d", ButtonName(i), ActionTypeName(action.type), action.rapidFire ? 1 : 0);
                    RunMacro(action.macroSteps, m_keyChordCallback, m_gamepadChordCallback);
                    m_lastFireTickMs[i] = now;
                }
            }
        } else if (!pressed && m_prevPressed[i]) {
            if (action.type == PaddleActionType::KeyChord && !action.rapidFire) {
                logging::Logf("[PaddleOverlay] Up btn=%S action=%s", ButtonName(i), ActionTypeName(action.type));
                if (m_keyChordCallback) m_keyChordCallback(action.chord, false);
                else SendChordUp(action.chord);
            }
        }

        m_prevPressed[i] = pressed;
    }
}
