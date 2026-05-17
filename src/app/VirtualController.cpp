#include "VirtualController.h"
#include "logging/Log.h"
#include "steam/SteamController.h"
#include <ViGEmClient.h>
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Report translation — 0x42 → XUSB_REPORT
// ---------------------------------------------------------------------------

static XUSB_REPORT Translate(const uint8_t* buf, size_t n) {
    XUSB_REPORT r{};
    if (n < 18) return r;

    const uint8_t b0 = buf[2];
    const uint8_t b1 = buf[3];
    const uint8_t b2 = buf[4];

    // Face buttons
    if (b0 & SteamController::BTN_A) r.wButtons |= XUSB_GAMEPAD_A;
    if (b0 & SteamController::BTN_B) r.wButtons |= XUSB_GAMEPAD_B;
    if (b0 & SteamController::BTN_X) r.wButtons |= XUSB_GAMEPAD_X;
    if (b0 & SteamController::BTN_Y) r.wButtons |= XUSB_GAMEPAD_Y;

    // Bumpers
    if (b2 & SteamController::BTN_LB) r.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    if (b1 & SteamController::BTN_RB) r.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;

    // Menu / View (Start / Back)
    if (b0 & SteamController::BTN_MENU) r.wButtons |= XUSB_GAMEPAD_START;
    if (b1 & SteamController::BTN_VIEW) r.wButtons |= XUSB_GAMEPAD_BACK;

    // Stick clicks
    if (b1 & SteamController::BTN_LS) r.wButtons |= XUSB_GAMEPAD_LEFT_THUMB;
    if (b0 & SteamController::BTN_RS) r.wButtons |= XUSB_GAMEPAD_RIGHT_THUMB;

    // Steam / Guide button
    if (b2 & SteamController::BTN_STEAM) r.wButtons |= XUSB_GAMEPAD_GUIDE;

    // D-pad
    if (b1 & SteamController::BTN_DPAD_UP)  r.wButtons |= XUSB_GAMEPAD_DPAD_UP;
    if (b1 & SteamController::BTN_DPAD_DN)  r.wButtons |= XUSB_GAMEPAD_DPAD_DOWN;
    if (b1 & SteamController::BTN_DPAD_LT)  r.wButtons |= XUSB_GAMEPAD_DPAD_LEFT;
    if (b1 & SteamController::BTN_DPAD_RT)  r.wButtons |= XUSB_GAMEPAD_DPAD_RIGHT;

    // Triggers: 16-bit signed (0x0000–0x7FFF) → 8-bit (0–255)
    int16_t ltRaw, rtRaw;
    memcpy(&ltRaw, buf + 6, 2);
    memcpy(&rtRaw, buf + 8, 2);
    r.bLeftTrigger  = static_cast<uint8_t>(std::clamp<int>(ltRaw >> 7, 0, 255));
    r.bRightTrigger = static_cast<uint8_t>(std::clamp<int>(rtRaw >> 7, 0, 255));

    // Sticks: 16-bit signed, same range as XInput — pass through directly
    memcpy(&r.sThumbLX, buf + 10, 2);
    memcpy(&r.sThumbLY, buf + 12, 2);
    memcpy(&r.sThumbRX, buf + 14, 2);
    memcpy(&r.sThumbRY, buf + 16, 2);

    return r;
}

static void ApplyPaddleMapping(XUSB_REPORT& report, PaddleMapping mapping) {
    switch (mapping) {
    case PaddleMapping::None: return;
    case PaddleMapping::A: report.wButtons |= XUSB_GAMEPAD_A; return;
    case PaddleMapping::B: report.wButtons |= XUSB_GAMEPAD_B; return;
    case PaddleMapping::X: report.wButtons |= XUSB_GAMEPAD_X; return;
    case PaddleMapping::Y: report.wButtons |= XUSB_GAMEPAD_Y; return;
    case PaddleMapping::LeftShoulder: report.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER; return;
    case PaddleMapping::RightShoulder: report.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER; return;
    case PaddleMapping::View: report.wButtons |= XUSB_GAMEPAD_BACK; return;
    case PaddleMapping::Menu: report.wButtons |= XUSB_GAMEPAD_START; return;
    case PaddleMapping::LeftThumb: report.wButtons |= XUSB_GAMEPAD_LEFT_THUMB; return;
    case PaddleMapping::RightThumb: report.wButtons |= XUSB_GAMEPAD_RIGHT_THUMB; return;
    case PaddleMapping::Guide: report.wButtons |= XUSB_GAMEPAD_GUIDE; return;
    case PaddleMapping::DPadUp: report.wButtons |= XUSB_GAMEPAD_DPAD_UP; return;
    case PaddleMapping::DPadRight: report.wButtons |= XUSB_GAMEPAD_DPAD_RIGHT; return;
    case PaddleMapping::DPadDown: report.wButtons |= XUSB_GAMEPAD_DPAD_DOWN; return;
    case PaddleMapping::DPadLeft: report.wButtons |= XUSB_GAMEPAD_DPAD_LEFT; return;
    }
}

static PaddleMapping ResolvePaddleGamepadMapping(PaddleMapping menuMapping, const PaddleAction& action) {
    switch (action.type) {
    case PaddleActionType::UseMenuMapping:
        return menuMapping;
    case PaddleActionType::None:
    case PaddleActionType::KeyChord:
    case PaddleActionType::Macro:
        return PaddleMapping::None;
    case PaddleActionType::Gamepad:
        return action.gamepadMapping;
    }
    return PaddleMapping::None;
}

static bool IsPaddlePressed(const uint8_t* buf, int paddleIndex) {
    const uint8_t b0 = buf[2];
    const uint8_t b1 = buf[3];
    const uint8_t b2 = buf[4];
    switch (paddleIndex) {
    case 0: return (b2 & SteamController::BTN_L4) != 0;
    case 1: return (b2 & SteamController::BTN_L5) != 0;
    case 2: return (b0 & SteamController::BTN_R4) != 0;
    case 3: return (b1 & SteamController::BTN_R5) != 0;
    default: return (b0 & SteamController::BTN_QAM) != 0;
    }
}

static void ApplyPaddleMappings(XUSB_REPORT& report, const uint8_t* buf,
                                const PaddleMappings& mappings,
                                const PaddleActionBindings& actions,
                                bool prevPressed[4]) {
    const PaddleMapping menuMappings[] = {
        mappings.l4, mappings.l5, mappings.r4, mappings.r5, mappings.qam
    };
    const PaddleAction actionList[] = {
        actions.l4, actions.l5, actions.r4, actions.r5, actions.qam
    };

    for (int i = 0; i < 5; ++i) {
        const bool pressed = IsPaddlePressed(buf, i);
        const PaddleAction& action = actionList[i];
        const PaddleMapping mapping = ResolvePaddleGamepadMapping(menuMappings[i], action);
        const bool active = pressed && (
            action.rapidFire ? ((GetTickCount64() / 90) % 2 == 0) :
            (action.type == PaddleActionType::UseMenuMapping || action.type == PaddleActionType::Gamepad));

        if (active)
            ApplyPaddleMapping(report, mapping);

        prevPressed[i] = pressed;
    }
}

static DS4_REPORT TranslateDs4(const XUSB_REPORT& xusb) {
    DS4_REPORT ds4{};
    DS4_REPORT_INIT(&ds4);

    ds4.bThumbLX = static_cast<uint8_t>((static_cast<int>(xusb.sThumbLX) + 32768) / 257);
    ds4.bThumbLY = static_cast<uint8_t>((32767 - static_cast<int>(xusb.sThumbLY)) / 257);
    ds4.bThumbRX = static_cast<uint8_t>((static_cast<int>(xusb.sThumbRX) + 32768) / 257);
    ds4.bThumbRY = static_cast<uint8_t>((32767 - static_cast<int>(xusb.sThumbRY)) / 257);
    ds4.bTriggerL = xusb.bLeftTrigger;
    ds4.bTriggerR = xusb.bRightTrigger;

    if (xusb.wButtons & XUSB_GAMEPAD_BACK) ds4.wButtons |= DS4_BUTTON_SHARE;
    if (xusb.wButtons & XUSB_GAMEPAD_START) ds4.wButtons |= DS4_BUTTON_OPTIONS;
    if (xusb.wButtons & XUSB_GAMEPAD_LEFT_THUMB) ds4.wButtons |= DS4_BUTTON_THUMB_LEFT;
    if (xusb.wButtons & XUSB_GAMEPAD_RIGHT_THUMB) ds4.wButtons |= DS4_BUTTON_THUMB_RIGHT;
    if (xusb.wButtons & XUSB_GAMEPAD_LEFT_SHOULDER) ds4.wButtons |= DS4_BUTTON_SHOULDER_LEFT;
    if (xusb.wButtons & XUSB_GAMEPAD_RIGHT_SHOULDER) ds4.wButtons |= DS4_BUTTON_SHOULDER_RIGHT;
    if (xusb.wButtons & XUSB_GAMEPAD_GUIDE) ds4.bSpecial |= DS4_SPECIAL_BUTTON_PS;
    if (xusb.wButtons & XUSB_GAMEPAD_A) ds4.wButtons |= DS4_BUTTON_CROSS;
    if (xusb.wButtons & XUSB_GAMEPAD_B) ds4.wButtons |= DS4_BUTTON_CIRCLE;
    if (xusb.wButtons & XUSB_GAMEPAD_X) ds4.wButtons |= DS4_BUTTON_SQUARE;
    if (xusb.wButtons & XUSB_GAMEPAD_Y) ds4.wButtons |= DS4_BUTTON_TRIANGLE;
    if (xusb.bLeftTrigger > 0) ds4.wButtons |= DS4_BUTTON_TRIGGER_LEFT;
    if (xusb.bRightTrigger > 0) ds4.wButtons |= DS4_BUTTON_TRIGGER_RIGHT;

    const bool up = (xusb.wButtons & XUSB_GAMEPAD_DPAD_UP) != 0;
    const bool right = (xusb.wButtons & XUSB_GAMEPAD_DPAD_RIGHT) != 0;
    const bool down = (xusb.wButtons & XUSB_GAMEPAD_DPAD_DOWN) != 0;
    const bool left = (xusb.wButtons & XUSB_GAMEPAD_DPAD_LEFT) != 0;

    if (up && right) DS4_SET_DPAD(&ds4, DS4_BUTTON_DPAD_NORTHEAST);
    else if (right && down) DS4_SET_DPAD(&ds4, DS4_BUTTON_DPAD_SOUTHEAST);
    else if (down && left) DS4_SET_DPAD(&ds4, DS4_BUTTON_DPAD_SOUTHWEST);
    else if (left && up) DS4_SET_DPAD(&ds4, DS4_BUTTON_DPAD_NORTHWEST);
    else if (up) DS4_SET_DPAD(&ds4, DS4_BUTTON_DPAD_NORTH);
    else if (right) DS4_SET_DPAD(&ds4, DS4_BUTTON_DPAD_EAST);
    else if (down) DS4_SET_DPAD(&ds4, DS4_BUTTON_DPAD_SOUTH);
    else if (left) DS4_SET_DPAD(&ds4, DS4_BUTTON_DPAD_WEST);

    return ds4;
}

// ---------------------------------------------------------------------------
// VirtualController
// ---------------------------------------------------------------------------

namespace {
std::mutex g_notificationMutex;
std::unordered_map<void*, VirtualController*> g_targetOwners;
}

VirtualController::VirtualController(EmulationMode mode, PaddleMappings paddleMappings,
                                     PaddleActionBindings paddleActions,
                                     RumbleCallback onRumble)
    : m_mode(mode), m_paddleMappings(paddleMappings), m_paddleActions(std::move(paddleActions)),
      m_onRumble(std::move(onRumble)) {
    logging::Logf("[ViGEm] VirtualController ctor mode=%d", static_cast<int>(m_mode));
    m_client = vigem_alloc();
    if (!m_client) {
        printf("[ViGEm] alloc failed\n");
        logging::Logf("[ViGEm] alloc failed");
        return;
    }

    VIGEM_ERROR err = vigem_connect(static_cast<PVIGEM_CLIENT>(m_client));
    if (!VIGEM_SUCCESS(err)) {
        logging::Logf("[ViGEm] connect failed err=0x%08X", err);
        if (err == VIGEM_ERROR_BUS_NOT_FOUND || err == VIGEM_ERROR_BUS_ACCESS_FAILED)
            m_driverMissing = true;
        vigem_free(static_cast<PVIGEM_CLIENT>(m_client));
        m_client = nullptr;
        return;
    }

    m_target = (m_mode == EmulationMode::DualShock4)
        ? vigem_target_ds4_alloc()
        : vigem_target_x360_alloc();
    if (!m_target) {
        printf("[ViGEm] target alloc failed\n");
        logging::Logf("[ViGEm] target alloc failed");
        return;
    }

    err = vigem_target_add(static_cast<PVIGEM_CLIENT>(m_client),
                           static_cast<PVIGEM_TARGET>(m_target));
    if (!VIGEM_SUCCESS(err)) {
        printf("[ViGEm] target_add failed: 0x%08X\n", err);
        logging::Logf("[ViGEm] target_add failed err=0x%08X", err);
        vigem_target_free(static_cast<PVIGEM_TARGET>(m_target));
        m_target = nullptr;
        return;
    }

    if (m_mode == EmulationMode::DualShock4) {
        printf("[ViGEm] Virtual DualShock 4 controller connected\n");
        logging::Logf("[ViGEm] Virtual DualShock 4 controller connected");
        err = vigem_target_ds4_register_notification(
            static_cast<PVIGEM_CLIENT>(m_client),
            static_cast<PVIGEM_TARGET>(m_target),
            reinterpret_cast<PFN_VIGEM_DS4_NOTIFICATION>(&VirtualController::ViGEmDs4Notification));
    } else {
        printf("[ViGEm] Virtual Xbox 360 controller connected\n");
        logging::Logf("[ViGEm] Virtual Xbox 360 controller connected");
        err = vigem_target_x360_register_notification(
            static_cast<PVIGEM_CLIENT>(m_client),
            static_cast<PVIGEM_TARGET>(m_target),
            reinterpret_cast<PFN_VIGEM_X360_NOTIFICATION>(&VirtualController::ViGEmNotification));
    }
    if (!VIGEM_SUCCESS(err)) {
        logging::Logf("[ViGEm] register notification failed err=0x%08X", err);
    } else {
        std::lock_guard<std::mutex> lock(g_notificationMutex);
        g_targetOwners[m_target] = this;
    }

    m_valid = true;
}

VirtualController::~VirtualController() {
    logging::Logf("[ViGEm] VirtualController dtor valid=%d", m_valid ? 1 : 0);
    if (m_target) {
        {
            std::lock_guard<std::mutex> lock(g_notificationMutex);
            g_targetOwners.erase(m_target);
        }
        if (m_mode == EmulationMode::DualShock4)
            vigem_target_ds4_unregister_notification(static_cast<PVIGEM_TARGET>(m_target));
        else
            vigem_target_x360_unregister_notification(static_cast<PVIGEM_TARGET>(m_target));
    }
    if (m_client && m_target) {
        vigem_target_remove(static_cast<PVIGEM_CLIENT>(m_client),
                            static_cast<PVIGEM_TARGET>(m_target));
    }
    if (m_target) vigem_target_free(static_cast<PVIGEM_TARGET>(m_target));
    if (m_client) {
        vigem_disconnect(static_cast<PVIGEM_CLIENT>(m_client));
        vigem_free(static_cast<PVIGEM_CLIENT>(m_client));
    }
}

void VirtualController::Update(const uint8_t* buf, size_t n) {
    if (!m_valid) return;
    XUSB_REPORT xusb = Translate(buf, n);
    ApplyPaddleMappings(xusb, buf, m_paddleMappings, m_paddleActions, m_prevPaddlePressed);
    if (m_mode == EmulationMode::DualShock4) {
        DS4_REPORT report = TranslateDs4(xusb);
        vigem_target_ds4_update(static_cast<PVIGEM_CLIENT>(m_client),
                                static_cast<PVIGEM_TARGET>(m_target),
                                report);
    } else {
        vigem_target_x360_update(static_cast<PVIGEM_CLIENT>(m_client),
                                 static_cast<PVIGEM_TARGET>(m_target),
                                 xusb);
    }
}

void VirtualController::ViGEmNotification(void* client, void* target, uint8_t largeMotor, uint8_t smallMotor, uint8_t ledNumber) {
    (void)client;
    (void)ledNumber;

    std::lock_guard<std::mutex> lock(g_notificationMutex);
    auto it = g_targetOwners.find(target);
    if (it == g_targetOwners.end() || !it->second->m_onRumble)
        return;

    it->second->m_onRumble(largeMotor, smallMotor);
}

void VirtualController::ViGEmDs4Notification(void* client, void* target, uint8_t largeMotor, uint8_t smallMotor, uint32_t lightbarColor) {
    (void)client;
    (void)lightbarColor;

    std::lock_guard<std::mutex> lock(g_notificationMutex);
    auto it = g_targetOwners.find(target);
    if (it == g_targetOwners.end() || !it->second->m_onRumble)
        return;

    it->second->m_onRumble(largeMotor, smallMotor);
}
