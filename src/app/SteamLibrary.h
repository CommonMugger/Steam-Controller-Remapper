#pragma once
#include <string>
#include <vector>

class SteamLibrary {
public:
    static std::vector<std::wstring> GetConfiguredSourceSpecs();
    static void SetConfiguredSourceSpecs(const std::vector<std::wstring>& specs);
    static std::vector<std::wstring> LoadCachedGameNames();
    static std::vector<std::wstring> ListInstalledGameNames();
    static std::vector<std::wstring> RefreshInstalledGameNames();
    static std::wstring MatchProcessToInstalledGame(const std::wstring& processPath);
    static std::wstring MatchProcessListToInstalledGame(const std::vector<std::wstring>& processPaths);
};
