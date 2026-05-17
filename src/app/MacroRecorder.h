#pragma once
#include <Windows.h>
#include <functional>
#include <string>

class MacroRecorder {
public:
    using ControllerChordFn = std::function<std::wstring()>;
    static bool Record(HWND owner, std::wstring& macroText, const std::wstring& initialMacroText = L"",
                       ControllerChordFn controllerChordFn = {});
};
