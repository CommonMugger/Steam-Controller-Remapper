#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
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
    PaddleMapping gamepadMapping = PaddleMapping::None;
    bool rapidFire = false;
    std::vector<uint16_t> chord;
    std::vector<std::vector<uint16_t>> macroSteps;
};

struct PaddleActionBindings {
    PaddleAction l4;
    PaddleAction l5;
    PaddleAction r4;
    PaddleAction r5;
    PaddleAction qam;
};

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

    void Update(const uint8_t* buf, size_t n);

private:
    static void ViGEmNotification(void* client, void* target, uint8_t largeMotor, uint8_t smallMotor, uint8_t ledNumber);
    static void ViGEmDs4Notification(void* client, void* target, uint8_t largeMotor, uint8_t smallMotor, uint32_t lightbarColor);

    void* m_client       = nullptr;
    void* m_target       = nullptr;
    bool  m_valid        = false;
    bool  m_driverMissing = false;
    EmulationMode m_mode = EmulationMode::Xbox360;
    PaddleMappings m_paddleMappings{};
    PaddleActionBindings m_paddleActions{};
    bool m_prevPaddlePressed[4] = {false, false, false, false};
    RumbleCallback m_onRumble;
};
