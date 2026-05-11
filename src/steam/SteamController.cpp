#include "SteamController.h"
#include <chrono>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Internal helper: build a 64-byte feature report command buffer.
//
// The 2026 Steam Controller routes firmware commands through Feature Report
// 0x01 (same channel the original SC used, now with an explicit report ID).
//
// Buffer layout:
//   [0] FEATURE_REPORT_CMD (0x01)  — HID feature report ID
//   [1] cmd                         — command byte (0x81, 0x87, etc.)
//   [2] payloadSize                 — number of payload bytes that follow
//   [3..3+payloadSize-1] payload    — command arguments
//   [rest] zeros
// ---------------------------------------------------------------------------

static void BuildCmd(uint8_t (&buf)[64], uint8_t cmd,
                     const uint8_t* payload = nullptr, uint8_t payloadSize = 0) {
    std::memset(buf, 0, 64);
    buf[0] = SteamController::FEATURE_REPORT_CMD;
    buf[1] = cmd;
    buf[2] = payloadSize;
    if (payload && payloadSize)
        std::memcpy(buf + 3, payload, payloadSize);
}

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

bool SteamController::Open() {
    auto paths = HidDevice::Enumerate(VALVE_VID, SC2026_PID, VENDOR_USAGE_PAGE);
    if (paths.empty()) {
        printf("No Steam Controller found (VID=%04X PID=%04X UsagePage=%04X).\n",
               VALVE_VID, SC2026_PID, VENDOR_USAGE_PAGE);
        return false;
    }

    printf("Found %zu interface(s). Opening: ", paths.size());
    wprintf(L"%s\n", paths[0].c_str());

    return m_device.Open(paths[0]);
}

void SteamController::Close() {
    if (m_running.exchange(false) && m_heartbeat.joinable())
        m_heartbeat.join();
    m_device.Close();
}

// ---------------------------------------------------------------------------
// Lizard mode
// ---------------------------------------------------------------------------

bool SteamController::DisableLizardMode() {
    uint8_t buf[64];

    // Step 1: CLEAR_DIGITAL_MAPPINGS — kills keyboard/mouse button emulation.
    BuildCmd(buf, CMD_CLEAR_DIGITAL_MAPPINGS);
    if (!m_device.SendFeatureReport(buf, sizeof(buf))) {
        printf("Failed to send CLEAR_DIGITAL_MAPPINGS.\n");
        return false;
    }

    // Step 2: SET_SETTINGS — set both trackpads to TRACKPAD_NONE.
    // Payload: pairs of [setting_id, val_lo, val_hi].
    const uint8_t settingsPayload[] = {
        SETTING_LEFT_TRACKPAD_MODE,  0x00, 0x00,
        SETTING_RIGHT_TRACKPAD_MODE, 0x00, 0x00,
    };
    BuildCmd(buf, CMD_SET_SETTINGS, settingsPayload, sizeof(settingsPayload));
    if (!m_device.SendFeatureReport(buf, sizeof(buf))) {
        printf("Failed to send SET_SETTINGS_VALUES.\n");
        return false;
    }

    if (!m_running.exchange(true))
        m_heartbeat = std::thread(&SteamController::HeartbeatLoop, this);

    return true;
}

bool SteamController::EnableLizardMode() {
    if (m_running.exchange(false) && m_heartbeat.joinable())
        m_heartbeat.join();

    uint8_t buf[64];
    BuildCmd(buf, CMD_SET_DEFAULT_MAPPINGS);
    return m_device.SendFeatureReport(buf, sizeof(buf));
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

size_t SteamController::ReadReport(uint8_t* buffer, size_t size, uint32_t timeoutMs) {
    return m_device.ReadInputReport(buffer, size, timeoutMs);
}


// ---------------------------------------------------------------------------
// Heartbeat
// ---------------------------------------------------------------------------

void SteamController::HeartbeatLoop() {
    uint8_t buf[64];
    BuildCmd(buf, CMD_CLEAR_DIGITAL_MAPPINGS);

    while (m_running.load()) {
        m_device.SendFeatureReport(buf, sizeof(buf));
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }
}
