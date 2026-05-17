#pragma once
#include "VirtualController.h"
#include <cstddef>
#include <cstdint>

class PaddleOverlay {
public:
    void SetBindings(PaddleActionBindings bindings);
    void Reset();
    void Update(const uint8_t* buf, size_t n);

private:
    PaddleActionBindings m_bindings{};
    bool                 m_prevPressed[5] = {false, false, false, false, false};
    unsigned long long   m_lastFireTickMs[5] = {0, 0, 0, 0, 0};
};
