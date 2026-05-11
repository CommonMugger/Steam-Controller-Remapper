#include "TrackpadMouse.h"
#include "steam/SteamController.h"
#include <Windows.h>
#include <cstring>

static constexpr uint8_t BTN_TP_RT_CLICK = 0x40;  // buf[4] bit 6 — hard press

static void SendMouseButton(DWORD flags) {
    INPUT input{};
    input.type       = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    SendInput(1, &input, sizeof(INPUT));
}

void TrackpadMouse::Reset() {
    if (m_prevClick) SendMouseButton(MOUSEEVENTF_LEFTUP);
    if (m_prevR4)    SendMouseButton(MOUSEEVENTF_LEFTUP);
    if (m_prevR5)    SendMouseButton(MOUSEEVENTF_RIGHTUP);
    m_touching  = false;
    m_prevClick = false;
    m_prevR4    = false;
    m_prevR5    = false;
    m_prevX     = 0;
    m_prevY     = 0;
}

void TrackpadMouse::Update(const uint8_t* buf, size_t n) {
    if (n < 30) return;

    const uint8_t b0 = buf[2];
    const uint8_t b1 = buf[3];
    const uint8_t b2 = buf[4];
    const uint8_t b3 = buf[5];

    // --- Trackpad mouse movement and click ---
    if (m_trackpadEnabled) {
        const bool touching = m_useLeftTrackpad
            ? (b3 & SteamController::BTN_TP_LT)       != 0
            : (b2 & SteamController::BTN_TP_RT)        != 0;
        const bool clicking = m_useLeftTrackpad
            ? (b3 & SteamController::BTN_TP_LT_CLICK)  != 0
            : (b2 & BTN_TP_RT_CLICK)                    != 0;

        int16_t x = 0, y = 0;
        if (m_useLeftTrackpad) {
            memcpy(&x, buf + 18, 2);
            memcpy(&y, buf + 20, 2);
        } else {
            memcpy(&x, buf + 24, 2);
            memcpy(&y, buf + 26, 2);
        }

        if (touching && m_touching) {
            const int dx =  static_cast<int>(x - m_prevX);
            const int dy = -static_cast<int>(y - m_prevY);  // negate: up = up
            if (dx != 0 || dy != 0) {
                INPUT input{};
                input.type       = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_MOVE;
                input.mi.dx      = static_cast<LONG>(dx * SENSITIVITY);
                input.mi.dy      = static_cast<LONG>(dy * SENSITIVITY);
                SendInput(1, &input, sizeof(INPUT));
            }
        }

        if (touching) { m_prevX = x; m_prevY = y; }
        m_touching = touching;

        if (clicking != m_prevClick) {
            SendMouseButton(clicking ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
            m_prevClick = clicking;
        }
    }

    // --- Back buttons: left side uses L4/L5, right side uses R4/R5 ---
    if (m_backButtonsEnabled) {
        const bool btn1 = m_useLeftTrackpad
            ? (b2 & SteamController::BTN_L4) != 0   // L4 = left click
            : (b0 & SteamController::BTN_R4) != 0;  // R4 = left click
        const bool btn2 = m_useLeftTrackpad
            ? (b2 & SteamController::BTN_L5) != 0   // L5 = right click
            : (b1 & SteamController::BTN_R5) != 0;  // R5 = right click

        if (btn1 != m_prevR4) {
            SendMouseButton(btn1 ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
            m_prevR4 = btn1;
        }
        if (btn2 != m_prevR5) {
            SendMouseButton(btn2 ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP);
            m_prevR5 = btn2;
        }
    }
}
