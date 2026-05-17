#pragma once
#include "VirtualController.h"
#include <string>

class PaddleConfig {
public:
    static std::wstring GetPath();
    static void EnsureExists();
    static PaddleActionBindings Load();
    static void Save(const PaddleActionBindings& bindings);
    static std::wstring Describe(const PaddleAction& action, PaddleMapping fallbackMapping);
    static bool ParseActionString(const std::wstring& value, PaddleAction& action);
};
