#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>

enum class EmulationMode {
    Xbox360 = 0,
    DualShock4 = 1,
};

class VirtualController {
public:
    using RumbleCallback = std::function<void(uint8_t largeMotor, uint8_t smallMotor)>;

    explicit VirtualController(EmulationMode mode = EmulationMode::Xbox360,
                               RumbleCallback onRumble = {});
    ~VirtualController();
    VirtualController(const VirtualController&) = delete;
    VirtualController& operator=(const VirtualController&) = delete;

    bool IsValid()          const { return m_valid; }
    bool IsDriverMissing()  const { return m_driverMissing; }
    EmulationMode GetMode() const { return m_mode; }

    void Update(const uint8_t* buf, size_t n);

private:
    static void ViGEmNotification(void* client, void* target, uint8_t largeMotor, uint8_t smallMotor, uint8_t ledNumber);
    static void ViGEmDs4Notification(void* client, void* target, uint8_t largeMotor, uint8_t smallMotor, uint32_t lightbarColor);

    void* m_client       = nullptr;
    void* m_target       = nullptr;
    bool  m_valid        = false;
    bool  m_driverMissing = false;
    EmulationMode m_mode = EmulationMode::Xbox360;
    RumbleCallback m_onRumble;
};
