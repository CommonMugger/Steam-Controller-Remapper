#include "PaddleOverlay.h"
#include "steam/SteamController.h"
#include <Windows.h>
#include <array>
#include <thread>
#include <utility>
#include <vector>

namespace {
bool IsPressed(const uint8_t* buf, int paddleIndex) {
    const uint8_t b0 = buf[2];
    const uint8_t b1 = buf[3];
    const uint8_t b2 = buf[4];

    switch (paddleIndex) {
    case 0: return (b2 & SteamController::BTN_L4) != 0;
    case 1: return (b2 & SteamController::BTN_L5) != 0;
    case 2: return (b0 & SteamController::BTN_R4) != 0;
    case 3: return (b1 & SteamController::BTN_R5) != 0;
    case 4: return (b0 & SteamController::BTN_QAM) != 0;
    default: return false;
    }
}

const PaddleAction& GetAction(const PaddleActionBindings& bindings, int paddleIndex) {
    switch (paddleIndex) {
    case 0: return bindings.l4;
    case 1: return bindings.l5;
    case 2: return bindings.r4;
    case 3: return bindings.r5;
    default: return bindings.qam;
    }
}

void SendKeyEvent(uint16_t vk, DWORD flags) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(vk);
    input.ki.dwFlags = flags;
    SendInput(1, &input, sizeof(INPUT));
}

void SendChordDown(const std::vector<uint16_t>& chord) {
    for (const uint16_t vk : chord)
        SendKeyEvent(vk, 0);
}

void SendChordUp(const std::vector<uint16_t>& chord) {
    for (auto it = chord.rbegin(); it != chord.rend(); ++it)
        SendKeyEvent(*it, KEYEVENTF_KEYUP);
}

void RunMacro(std::vector<std::vector<uint16_t>> steps) {
    std::thread([steps = std::move(steps)]() {
        for (const auto& chord : steps) {
            SendChordDown(chord);
            Sleep(20);
            SendChordUp(chord);
            Sleep(30);
        }
    }).detach();
}

void TapChord(const std::vector<uint16_t>& chord) {
    SendChordDown(chord);
    Sleep(15);
    SendChordUp(chord);
}
}

void PaddleOverlay::SetBindings(PaddleActionBindings bindings) {
    m_bindings = std::move(bindings);
}

void PaddleOverlay::Reset() {
    for (int i = 0; i < 5; ++i) {
        const PaddleAction& action = GetAction(m_bindings, i);
        if (m_prevPressed[i] &&
            (action.type == PaddleActionType::KeyChord) &&
            !action.rapidFire) {
            SendChordUp(action.chord);
        }
        m_prevPressed[i] = false;
        m_lastFireTickMs[i] = 0;
    }
}

void PaddleOverlay::Update(const uint8_t* buf, size_t n) {
    if (n < 5)
        return;

    for (int i = 0; i < 5; ++i) {
        const PaddleAction& action = GetAction(m_bindings, i);
        const bool pressed = IsPressed(buf, i);
        const ULONGLONG now = GetTickCount64();

        if (pressed) {
            const bool rising = !m_prevPressed[i];
            const bool rapidReady = action.rapidFire &&
                (rising || (now - m_lastFireTickMs[i]) >= 90);

            if (action.type == PaddleActionType::KeyChord) {
                if (action.rapidFire && rapidReady) {
                    TapChord(action.chord);
                    m_lastFireTickMs[i] = now;
                } else if (rising) {
                    SendChordDown(action.chord);
                }
            } else if (action.type == PaddleActionType::Macro) {
                if (rising || rapidReady) {
                    RunMacro(action.macroSteps);
                    m_lastFireTickMs[i] = now;
                }
            }
        } else if (!pressed && m_prevPressed[i]) {
            if (action.type == PaddleActionType::KeyChord &&
                !action.rapidFire) {
                SendChordUp(action.chord);
            }
        }

        m_prevPressed[i] = pressed;
    }
}
