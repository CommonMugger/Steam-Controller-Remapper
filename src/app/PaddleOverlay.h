#pragma once
#include "StandardGamepadState.h"
#include "VirtualController.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class PaddleOverlay {
public:
    using KeyChordCallback     = std::function<void(const std::vector<uint16_t>& vkChord, bool down)>;
    using GamepadChordCallback = std::function<void(const std::vector<PaddleMapping>& mappings, bool down)>;

    void SetBindings(PaddleActionBindings bindings);
    void SetKeyChordCallback(KeyChordCallback cb)         { m_keyChordCallback     = std::move(cb); }
    void SetGamepadChordCallback(GamepadChordCallback cb) { m_gamepadChordCallback = std::move(cb); }
    void Reset();
    void Update(const uint8_t* buf, size_t n, const StandardGamepadState* standardState = nullptr);

private:
    PaddleActionBindings m_bindings{};
    KeyChordCallback     m_keyChordCallback;
    GamepadChordCallback m_gamepadChordCallback;
    bool                 m_hasSeededState = false;
    bool                 m_prevPressed[kTotalButtonCount] = {};
    unsigned long long   m_lastFireTickMs[kTotalButtonCount] = {};
};
