#pragma once
#include <cstdint>
#include <cstddef>

class TrackpadMouse {
public:
    void SetTrackpadEnabled(bool enabled)     { m_trackpadEnabled    = enabled; }
    void SetBackButtonsEnabled(bool enabled)  { m_backButtonsEnabled = enabled; }
    void SetUseLeftTrackpad(bool enabled)     { m_useLeftTrackpad    = enabled; }

    void Update(const uint8_t* buf, size_t n);
    void Reset();

private:
    bool     m_trackpadEnabled    = false;
    bool     m_backButtonsEnabled = false;
    bool     m_useLeftTrackpad    = false;

    bool     m_touching   = false;
    bool     m_prevClick  = false;
    bool     m_prevR4     = false;
    bool     m_prevR5     = false;
    int16_t  m_prevX      = 0;
    int16_t  m_prevY      = 0;

    static constexpr float SENSITIVITY = 0.015f;
};
