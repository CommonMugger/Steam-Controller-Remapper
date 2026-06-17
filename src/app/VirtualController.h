#pragma once
#include "StandardGamepadState.h"
#include <array>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <mutex>
#include <vector>

enum class EmulationMode {
    Xbox360 = 0,
    DualShock4 = 1,
};

enum class PaddleMapping {
    None = 0,
    A,
    B,
    X,
    Y,
    LeftShoulder,
    RightShoulder,
    View,
    Menu,
    LeftThumb,
    RightThumb,
    Guide,
    DPadUp,
    DPadRight,
    DPadDown,
    DPadLeft,
};

struct PaddleMappings {
    PaddleMapping l4 = PaddleMapping::None;
    PaddleMapping l5 = PaddleMapping::None;
    PaddleMapping r4 = PaddleMapping::None;
    PaddleMapping r5 = PaddleMapping::None;
    PaddleMapping qam = PaddleMapping::None;
};

enum class PaddleActionType {
    UseMenuMapping = 0,
    None,
    Gamepad,
    KeyChord,
    Macro,
};

struct PaddleAction {
    PaddleActionType type = PaddleActionType::UseMenuMapping;
    std::vector<PaddleMapping> gamepadMappings;
    bool rapidFire = false;
    std::vector<uint16_t> chord;
    std::vector<std::vector<uint16_t>> macroSteps;
};

struct PaddleActionBindings {
    // Back paddles & QAM (indices 0–4)
    PaddleAction l4;
    PaddleAction l5;
    PaddleAction r4;
    PaddleAction r5;
    PaddleAction qam;
    // Standard controller buttons (indices 5–19)
    PaddleAction a;
    PaddleAction b;
    PaddleAction x;
    PaddleAction y;
    PaddleAction lb;
    PaddleAction rb;
    PaddleAction view;
    PaddleAction menu;
    PaddleAction guide;
    PaddleAction l3;
    PaddleAction r3;
    PaddleAction dpadUp;
    PaddleAction dpadDown;
    PaddleAction dpadLeft;
    PaddleAction dpadRight;
    // Analog triggers (indices 20–21)
    PaddleAction l2;
    PaddleAction r2;
};

constexpr int kPaddleCount       = 5;
constexpr int kTotalButtonCount  = 22;

// Maps a button index (0–21) to its PaddleAction slot.
inline PaddleAction* GetButtonAction(PaddleActionBindings& b, int i) {
    switch (i) {
    case  0: return &b.l4;
    case  1: return &b.l5;
    case  2: return &b.r4;
    case  3: return &b.r5;
    case  4: return &b.qam;
    case  5: return &b.a;
    case  6: return &b.b;
    case  7: return &b.x;
    case  8: return &b.y;
    case  9: return &b.lb;
    case 10: return &b.rb;
    case 11: return &b.view;
    case 12: return &b.menu;
    case 13: return &b.guide;
    case 14: return &b.l3;
    case 15: return &b.r3;
    case 16: return &b.dpadUp;
    case 17: return &b.dpadDown;
    case 18: return &b.dpadLeft;
    case 19: return &b.dpadRight;
    case 20: return &b.l2;
    case 21: return &b.r2;
    default: return nullptr;
    }
}
inline const PaddleAction* GetButtonAction(const PaddleActionBindings& b, int i) {
    return GetButtonAction(const_cast<PaddleActionBindings&>(b), i);
}

class VirtualController {
public:
    using RumbleCallback = std::function<void(uint8_t largeMotor, uint8_t smallMotor)>;

    explicit VirtualController(EmulationMode mode = EmulationMode::Xbox360,
                               PaddleMappings paddleMappings = {},
                               PaddleActionBindings paddleActions = {},
                               RumbleCallback onRumble = {});
    ~VirtualController();
    VirtualController(const VirtualController&) = delete;
    VirtualController& operator=(const VirtualController&) = delete;

    bool IsValid()          const { return m_valid; }
    bool IsDriverMissing()  const { return m_driverMissing; }
    EmulationMode GetMode() const { return m_mode; }
    void SetPaddleMappings(PaddleMappings mappings) { m_paddleMappings = mappings; }
    void SetPaddleActions(PaddleActionBindings actions) { m_paddleActions = std::move(actions); }

    void Update(const uint8_t* buf, size_t n, const StandardGamepadState* standardState = nullptr);
    void UpdateMouse(int16_t dx, int16_t dy, uint8_t buttons);
    void KeyChordDown(const std::vector<uint16_t>& vkChord);
    void KeyChordUp(const std::vector<uint16_t>& vkChord);

private:
    static void ViiperXboxRumbleCallback(std::uintptr_t handle, uint8_t leftMotor, uint8_t rightMotor);
    static void ViiperDs4OutputCallback(std::uintptr_t handle, uint8_t rumbleSmall, uint8_t rumbleLarge,
                                        uint8_t ledRed, uint8_t ledGreen, uint8_t ledBlue,
                                        uint8_t flashOn, uint8_t flashOff);

    void* m_module        = nullptr;
    std::uintptr_t m_serverHandle = 0;
    std::uintptr_t m_deviceHandle = 0;
    std::uintptr_t m_mouseHandle    = 0;
    std::uintptr_t m_keyboardHandle = 0;
    uint8_t        m_lastMouseButtons = 0;
    uint32_t m_busId = 0;
    bool  m_valid        = false;
    bool  m_driverMissing = false;
    EmulationMode m_mode = EmulationMode::Xbox360;
    PaddleMappings m_paddleMappings{};
    PaddleActionBindings m_paddleActions{};
    bool m_prevPaddlePressed[kTotalButtonCount] = {};
    bool m_loggedSdlState = false;
    RumbleCallback m_onRumble;
    std::mutex                 m_keyboardMutex;
    uint8_t                    m_kbModifiers = 0;
    std::array<uint8_t, 32>    m_kbBitmap{};
    void ApplyKeyVk(uint16_t vk, bool down);
};
