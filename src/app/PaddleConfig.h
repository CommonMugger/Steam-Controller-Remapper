#pragma once
#include "VirtualController.h"
#include <string>
#include <vector>

struct RemapProfile {
    std::wstring id = L"default";
    PaddleMappings mappings{};
    PaddleActionBindings actions{};
};

class PaddleConfig {
public:
    static std::wstring GetPath();
    static void EnsureExists();
    static PaddleActionBindings Load();
    static void Save(const PaddleActionBindings& bindings);
    static std::wstring Describe(const PaddleAction& action, PaddleMapping fallbackMapping);
    static bool ParseActionString(const std::wstring& value, PaddleAction& action);
    static std::vector<RemapProfile> LoadProfiles(const PaddleMappings& defaultMappings,
                                                  const PaddleActionBindings& defaultActions);
    static void SaveProfiles(const std::vector<RemapProfile>& profiles);
    static std::wstring NormalizeProfileId(const std::wstring& profileId);
};
