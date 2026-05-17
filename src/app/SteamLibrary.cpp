#include "SteamLibrary.h"
#include <Windows.h>
#include <ShlObj.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <string>

namespace {
constexpr wchar_t kConfigDirName[] = L"SteamControllerRemapper";
constexpr wchar_t kLegacyConfigDirName[] = L"XboxModeSteamlessController";

std::wstring Trim(std::wstring value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](wchar_t ch) {
        return !iswspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](wchar_t ch) {
        return !iswspace(ch);
    }).base(), value.end());
    return value;
}

std::wstring AppDataDirectory(const wchar_t* leafName) {
    wchar_t localAppData[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData);
    std::wstring dir = localAppData;
    dir += L"\\";
    dir += leafName;
    return dir;
}

void MigrateIfMissing(const std::wstring& targetPath, const std::wstring& legacyPath) {
    if (GetFileAttributesW(targetPath.c_str()) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(legacyPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        CopyFileW(legacyPath.c_str(), targetPath.c_str(), TRUE);
    }
}

std::wstring ConfigDirectory() {
    const std::wstring dir = AppDataDirectory(kConfigDirName);
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring legacyDir = AppDataDirectory(kLegacyConfigDirName);
    MigrateIfMissing(dir + L"\\steam-games-cache.txt", legacyDir + L"\\steam-games-cache.txt");
    MigrateIfMissing(dir + L"\\steam-library-path.txt", legacyDir + L"\\steam-library-path.txt");
    return dir;
}

std::wstring CachedGamesPath() {
    return ConfigDirectory() + L"\\steam-games-cache.txt";
}

std::wstring LibraryPathConfigPath() {
    return ConfigDirectory() + L"\\steam-library-path.txt";
}

struct ConfiguredSource {
    enum class Type {
        Folder,
        Exe,
    };

    Type type = Type::Folder;
    std::wstring path;
};

std::wstring ReadRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* valueName, REGSAM wow64Flags = 0) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | wow64Flags, &key) != ERROR_SUCCESS)
        return {};

    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) ||
        size == 0) {
        RegCloseKey(key);
        return {};
    }

    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, valueName, nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(value.data()), &size) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return {};
    }

    RegCloseKey(key);
    while (!value.empty() && value.back() == L'\0')
        value.pop_back();
    return value;
}

std::wstring GetSteamPath() {
    std::wstring path = ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (path.empty())
        path = ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamExe");
    if (path.empty())
        path = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath", KEY_WOW64_32KEY);
    if (path.empty())
        return {};

    const size_t steamExe = path.rfind(L"\\steam.exe");
    if (steamExe != std::wstring::npos)
        path.erase(steamExe);
    return path;
}

std::wstring DecodeText(const std::string& bytes) {
    if (bytes.empty())
        return {};

    auto decodeWith = [&](UINT codePage) -> std::wstring {
        const int count = MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (count <= 0)
            return {};
        std::wstring text(count, L'\0');
        if (MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), count) <= 0)
            return {};
        return text;
    };

    std::wstring text = decodeWith(CP_UTF8);
    if (!text.empty())
        return text;
    return decodeWith(CP_ACP);
}

std::wstring ReadTextFile(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};

    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    return DecodeText(bytes);
}

std::wstring ExtractQuotedValue(const std::wstring& line) {
    const size_t first = line.find(L'"');
    if (first == std::wstring::npos)
        return {};
    const size_t second = line.find(L'"', first + 1);
    if (second == std::wstring::npos)
        return {};
    const size_t third = line.find(L'"', second + 1);
    if (third == std::wstring::npos)
        return {};
    const size_t fourth = line.find(L'"', third + 1);
    if (fourth == std::wstring::npos)
        return {};
    return line.substr(third + 1, fourth - third - 1);
}

std::vector<std::wstring> ReadLibraryFolders(const std::wstring& steamPath) {
    std::vector<std::wstring> libraries;
    if (steamPath.empty())
        return libraries;

    libraries.push_back(steamPath);
    const std::wstring text = ReadTextFile(steamPath + L"\\steamapps\\libraryfolders.vdf");
    if (text.empty())
        return libraries;

    bool inLibraryBlock = false;
    std::wstring currentKey;
    std::wstring currentPath;

    auto flushCurrent = [&]() {
        if (!currentKey.empty() &&
            std::all_of(currentKey.begin(), currentKey.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; }) &&
            !currentPath.empty()) {
            std::replace(currentPath.begin(), currentPath.end(), L'/', L'\\');
            libraries.push_back(currentPath);
        }
        currentKey.clear();
        currentPath.clear();
    };

    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos)
            end = text.size();
        std::wstring line = Trim(text.substr(start, end - start));
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();

        if (!inLibraryBlock) {
            const size_t first = line.find(L'"');
            const size_t second = first == std::wstring::npos ? std::wstring::npos : line.find(L'"', first + 1);
            if (first != std::wstring::npos && second != std::wstring::npos) {
                std::wstring key = line.substr(first + 1, second - first - 1);
                if (std::all_of(key.begin(), key.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; })) {
                    currentKey = key;
                    if (line.find(L'{', second) != std::wstring::npos)
                        inLibraryBlock = true;
                }
            }
        } else {
            if (line.find(L'}') != std::wstring::npos) {
                flushCurrent();
                inLibraryBlock = false;
            } else if (line.find(L"\"path\"") != std::wstring::npos) {
                currentPath = ExtractQuotedValue(line);
            }
        }

        start = end + 1;
    }

    std::sort(libraries.begin(), libraries.end());
    libraries.erase(std::unique(libraries.begin(), libraries.end()), libraries.end());
    return libraries;
}

bool DirectoryExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring NormalizeLibraryPath(std::wstring path) {
    path = Trim(path);
    if (path.empty())
        return {};

    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();

    return path;
}

bool FileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::vector<ConfiguredSource> ReadConfiguredSources() {
    std::vector<ConfiguredSource> sources;
    std::wifstream in(LibraryPathConfigPath());
    std::wstring line;
    while (std::getline(in, line)) {
        const std::wstring trimmed = Trim(line);
        if (trimmed.empty())
            continue;

        ConfiguredSource source;
        std::wstring path = trimmed;
        if (trimmed.rfind(L"DIR|", 0) == 0) {
            source.type = ConfiguredSource::Type::Folder;
            path = trimmed.substr(4);
        } else if (trimmed.rfind(L"EXE|", 0) == 0) {
            source.type = ConfiguredSource::Type::Exe;
            path = trimmed.substr(4);
        } else {
            source.type = ConfiguredSource::Type::Folder;
        }

        path = NormalizeLibraryPath(path);
        if (path.empty())
            continue;
        if (source.type == ConfiguredSource::Type::Folder && !DirectoryExists(path))
            continue;
        if (source.type == ConfiguredSource::Type::Exe && !FileExists(path))
            continue;
        source.path = path;
        sources.push_back(std::move(source));
    }
    return sources;
}

std::wstring ExtractManifestField(const std::wstring& manifestPath, const std::wstring& fieldName) {
    const std::wstring text = ReadTextFile(manifestPath);
    if (text.empty())
        return {};

    size_t start = 0;
    const std::wstring needle = L"\"" + fieldName + L"\"";
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos)
            end = text.size();
        std::wstring line = Trim(text.substr(start, end - start));
        if (line.find(needle) != std::wstring::npos) {
            std::wstring value = ExtractQuotedValue(line);
            if (!value.empty())
                return value;
        }
        start = end + 1;
    }

    return {};
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

bool HasManifestFiles(const std::wstring& manifestDir) {
    WIN32_FIND_DATAW findData{};
    HANDLE find = FindFirstFileW((manifestDir + L"\\appmanifest_*.acf").c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE)
        return false;
    FindClose(find);
    return true;
}

bool IsIgnoredGameName(const std::wstring& name) {
    const std::wstring lower = Lower(name);
    return lower.empty() ||
           lower[0] == L'$' ||
           lower.find(L"redistributable") != std::wstring::npos ||
           lower.find(L"steamworks common") != std::wstring::npos ||
           lower.find(L"steam controller") != std::wstring::npos ||
           lower.find(L"proton") != std::wstring::npos ||
           lower.find(L"steam linux runtime") != std::wstring::npos ||
           lower.find(L"directx") != std::wstring::npos ||
           lower.find(L"vc redist") != std::wstring::npos ||
           lower == L"system volume information" ||
           lower == L"recovery" ||
           lower == L"msdownld.tmp";
}

bool IsGameManifestType(const std::wstring& type) {
    if (type.empty())
        return true;
    const std::wstring lower = Lower(type);
    return lower == L"game" || lower == L"demo" || lower == L"dlc";
}

struct GameEntry {
    std::wstring displayName;
    std::wstring installDir;
    std::wstring matchPath;
    std::vector<std::wstring> exeStems;
};

bool HasLikelyGameExe(const std::wstring& fileName) {
    const std::wstring lower = Lower(fileName);
    if (lower.size() < 5 || lower.substr(lower.size() - 4) != L".exe")
        return false;
    return lower.find(L"setup") == std::wstring::npos &&
           lower.find(L"unins") == std::wstring::npos &&
           lower.find(L"redist") == std::wstring::npos &&
           lower.find(L"crashreport") == std::wstring::npos;
}

bool DirectoryHasGameExe(const std::wstring& directory) {
    auto scan = [&](const std::wstring& dir) {
        WIN32_FIND_DATAW findData{};
        HANDLE find = FindFirstFileW((dir + L"\\*.exe").c_str(), &findData);
        if (find == INVALID_HANDLE_VALUE)
            return false;
        bool found = false;
        do {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                HasLikelyGameExe(findData.cFileName)) {
                found = true;
                break;
            }
        } while (FindNextFileW(find, &findData));
        FindClose(find);
        return found;
    };

    if (scan(directory))
        return true;

    WIN32_FIND_DATAW childData{};
    HANDLE childFind = FindFirstFileW((directory + L"\\*").c_str(), &childData);
    if (childFind == INVALID_HANDLE_VALUE)
        return false;

    bool found = false;
    do {
        if ((childData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            continue;
        if (wcscmp(childData.cFileName, L".") == 0 || wcscmp(childData.cFileName, L"..") == 0)
            continue;
        if (scan(directory + L"\\" + childData.cFileName)) {
            found = true;
            break;
        }
    } while (FindNextFileW(childFind, &childData));
    FindClose(childFind);
    return found;
}

bool DirectoryHasDirectGameExe(const std::wstring& directory) {
    WIN32_FIND_DATAW findData{};
    HANDLE find = FindFirstFileW((directory + L"\\*.exe").c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE)
        return false;

    bool found = false;
    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
            HasLikelyGameExe(findData.cFileName)) {
            found = true;
            break;
        }
    } while (FindNextFileW(find, &findData));
    FindClose(find);
    return found;
}

std::vector<std::wstring> CollectGameExeStems(const std::wstring& directory) {
    std::vector<std::wstring> exeStems;
    auto fileStem = [](const std::wstring& path) {
        size_t slash = path.find_last_of(L"\\/");
        std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
        size_t dot = file.find_last_of(L'.');
        return dot == std::wstring::npos ? file : file.substr(0, dot);
    };

    auto addFrom = [&](const std::wstring& dir) {
        WIN32_FIND_DATAW findData{};
        HANDLE find = FindFirstFileW((dir + L"\\*.exe").c_str(), &findData);
        if (find == INVALID_HANDLE_VALUE)
            return;
        do {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                HasLikelyGameExe(findData.cFileName)) {
                std::wstring stem = Lower(fileStem(findData.cFileName));
                if (!stem.empty())
                    exeStems.push_back(std::move(stem));
            }
        } while (FindNextFileW(find, &findData));
        FindClose(find);
    };

    addFrom(directory);

    WIN32_FIND_DATAW childData{};
    HANDLE childFind = FindFirstFileW((directory + L"\\*").c_str(), &childData);
    if (childFind != INVALID_HANDLE_VALUE) {
        do {
            if ((childData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                continue;
            if (wcscmp(childData.cFileName, L".") == 0 || wcscmp(childData.cFileName, L"..") == 0)
                continue;
            addFrom(directory + L"\\" + childData.cFileName);
        } while (FindNextFileW(childFind, &childData));
        FindClose(childFind);
    }

    std::sort(exeStems.begin(), exeStems.end());
    exeStems.erase(std::unique(exeStems.begin(), exeStems.end()), exeStems.end());
    return exeStems;
}

std::wstring FileStem(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
    size_t dot = file.find_last_of(L'.');
    return dot == std::wstring::npos ? file : file.substr(0, dot);
}

std::wstring LeafName(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring ParentDirectory(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}

bool IsDriveRootPath(const std::wstring& path) {
    return path.size() == 2 && path[1] == L':' ||
           (path.size() == 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'));
}

bool LooksLikeLibraryContainer(const std::wstring& path) {
    const std::wstring leaf = Lower(LeafName(path));
    return HasManifestFiles(path + L"\\steamapps") ||
           HasManifestFiles(path) ||
           DirectoryExists(path + L"\\steamapps\\common") ||
           DirectoryExists(path + L"\\common") ||
           leaf == L"xboxgames";
}

void AddGameEntry(std::vector<GameEntry>& games,
                  const std::wstring& displayName,
                  const std::wstring& installDir,
                  const std::wstring& matchPath,
                  bool requireRecursiveExe = true) {
    if (displayName.empty() || installDir.empty() || matchPath.empty() || IsIgnoredGameName(displayName))
        return;
    const bool hasExe = requireRecursiveExe ? DirectoryHasGameExe(installDir) : DirectoryHasDirectGameExe(installDir);
    if (!hasExe)
        return;

    const std::wstring lowerMatchPath = Lower(matchPath);
    for (const GameEntry& entry : games) {
        if (Lower(entry.matchPath) == lowerMatchPath)
            return;
    }
    games.push_back(GameEntry{ displayName, installDir, matchPath, CollectGameExeStems(installDir) });
}

void CollectManifestGames(const std::wstring& manifestDir, const std::wstring& commonDir, std::vector<GameEntry>& games) {
    WIN32_FIND_DATAW findData{};
    HANDLE find = FindFirstFileW((manifestDir + L"\\appmanifest_*.acf").c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE)
        return;

    do {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        const std::wstring manifestPath = manifestDir + L"\\" + findData.cFileName;
        const std::wstring name = ExtractManifestField(manifestPath, L"name");
        const std::wstring type = ExtractManifestField(manifestPath, L"type");
        const std::wstring installDirName = ExtractManifestField(manifestPath, L"installdir");
        if (name.empty() || !IsGameManifestType(type) || installDirName.empty())
            continue;
        const std::wstring installDir = commonDir + L"\\" + installDirName;
        AddGameEntry(games, name, installDir, installDir, true);
    } while (FindNextFileW(find, &findData));

    FindClose(find);
}

void CollectDirectoryGames(const std::wstring& directory, std::vector<GameEntry>& games) {
    WIN32_FIND_DATAW findData{};
    HANDLE find = FindFirstFileW((directory + L"\\*").c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE)
        return;

    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            continue;
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
            continue;
        const std::wstring installDir = directory + L"\\" + findData.cFileName;
        AddGameEntry(games, findData.cFileName, installDir, installDir, true);
    } while (FindNextFileW(find, &findData));

    FindClose(find);
}

void ScanSourcePath(const std::wstring& sourcePath, std::vector<GameEntry>& games) {
    if (!DirectoryExists(sourcePath))
        return;

    const std::wstring steamappsDir = sourcePath + L"\\steamapps";
    const std::wstring commonDir = steamappsDir + L"\\common";
    const std::wstring siblingCommonDir = sourcePath + L"\\common";
    const std::wstring sourceLeaf = Lower(LeafName(sourcePath));

    if (HasManifestFiles(steamappsDir)) {
        CollectManifestGames(steamappsDir, commonDir, games);
        if (DirectoryExists(commonDir))
            CollectDirectoryGames(commonDir, games);
        return;
    }

    if (HasManifestFiles(sourcePath)) {
        CollectManifestGames(sourcePath, siblingCommonDir, games);
        if (DirectoryExists(siblingCommonDir))
            CollectDirectoryGames(siblingCommonDir, games);
        return;
    }

    if (DirectoryExists(commonDir)) {
        CollectDirectoryGames(commonDir, games);
        return;
    }

    if (DirectoryExists(siblingCommonDir)) {
        CollectDirectoryGames(siblingCommonDir, games);
        return;
    }

    if (sourceLeaf == L"xboxgames") {
        CollectDirectoryGames(sourcePath, games);
        return;
    }

    WIN32_FIND_DATAW findData{};
    HANDLE find = FindFirstFileW((sourcePath + L"\\*").c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE)
        return;

    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            continue;
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
            continue;
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0 ||
            (findData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0) {
            continue;
        }

        const std::wstring childPath = sourcePath + L"\\" + findData.cFileName;
        const std::wstring childSteamappsDir = childPath + L"\\steamapps";
        const std::wstring childCommonDir = childSteamappsDir + L"\\common";
        const std::wstring childSiblingCommonDir = childPath + L"\\common";

        if (LooksLikeLibraryContainer(childPath)) {
            ScanSourcePath(childPath, games);
            continue;
        }

        if (!IsDriveRootPath(sourcePath))
            AddGameEntry(games, findData.cFileName, childPath, childPath, false);
    } while (FindNextFileW(find, &findData));

    FindClose(find);
}

void SaveCachedGameNames(const std::vector<std::wstring>& games) {
    std::wofstream out(CachedGamesPath(), std::ios::trunc);
    for (const std::wstring& game : games) {
        if (!game.empty())
            out << game << L"\n";
    }
}

std::vector<std::wstring> ReadCachedGameNames() {
    std::vector<std::wstring> games;
    std::wifstream in(CachedGamesPath());
    std::wstring line;
    while (std::getline(in, line)) {
        std::wstring name = Trim(line);
        if (!name.empty())
            games.push_back(name);
    }

    std::sort(games.begin(), games.end());
    games.erase(std::unique(games.begin(), games.end()), games.end());
    return games;
}

std::vector<GameEntry> ScanInstalledGames() {
    std::vector<GameEntry> games;
    std::vector<ConfiguredSource> configuredSources = ReadConfiguredSources();
    for (const ConfiguredSource& source : configuredSources) {
        if (source.type == ConfiguredSource::Type::Folder)
            ScanSourcePath(source.path, games);
        else
            AddGameEntry(games, FileStem(source.path), ParentDirectory(source.path), source.path, true);
    }

    std::vector<std::wstring> libraries;
    for (const ConfiguredSource& source : configuredSources) {
        if (source.type == ConfiguredSource::Type::Folder)
            libraries.push_back(source.path);
    }
    const std::wstring steamPath = GetSteamPath();
    std::vector<std::wstring> steamLibraries = ReadLibraryFolders(steamPath);
    libraries.insert(libraries.end(), steamLibraries.begin(), steamLibraries.end());
    std::sort(libraries.begin(), libraries.end());
    libraries.erase(std::unique(libraries.begin(), libraries.end()), libraries.end());

    for (const std::wstring& library : libraries) {
        ScanSourcePath(library, games);
    }

    std::sort(games.begin(), games.end(), [](const GameEntry& a, const GameEntry& b) {
        return _wcsicmp(a.displayName.c_str(), b.displayName.c_str()) < 0;
    });
    return games;
}

std::vector<std::wstring> ToNameList(const std::vector<GameEntry>& games) {
    std::vector<std::wstring> names;
    names.reserve(games.size());
    for (const GameEntry& game : games)
        names.push_back(game.displayName);
    return names;
}

std::wstring MatchProcessPathToGame(const std::wstring& processPath, const std::vector<GameEntry>& games) {
    const std::wstring lowerPath = Lower(processPath);
    const std::wstring lowerStem = Lower(FileStem(processPath));
    for (const GameEntry& game : games) {
        const std::wstring lowerMatchPath = Lower(game.matchPath);
        const std::wstring lowerDir = Lower(game.installDir);
        if (!lowerMatchPath.empty() && lowerPath == lowerMatchPath)
            return game.displayName;
        if (!lowerDir.empty() && lowerPath.rfind(lowerDir + L"\\", 0) == 0) {
            return game.displayName;
        }
        if (!lowerStem.empty() &&
            std::find(game.exeStems.begin(), game.exeStems.end(), lowerStem) != game.exeStems.end()) {
            return game.displayName;
        }
    }
    return {};
}

std::wstring MatchProcessPathsToGame(const std::vector<std::wstring>& processPaths, const std::vector<GameEntry>& games) {
    for (const std::wstring& processPath : processPaths) {
        const std::wstring match = MatchProcessPathToGame(processPath, games);
        if (!match.empty())
            return match;
    }
    return {};
}
}

std::vector<std::wstring> SteamLibrary::GetConfiguredSourceSpecs() {
    std::vector<std::wstring> specs;
    for (const ConfiguredSource& source : ReadConfiguredSources()) {
        specs.push_back((source.type == ConfiguredSource::Type::Folder ? L"DIR|" : L"EXE|") + source.path);
    }
    return specs;
}

void SteamLibrary::SetConfiguredSourceSpecs(const std::vector<std::wstring>& specs) {
    std::wofstream out(LibraryPathConfigPath(), std::ios::trunc);
    for (const std::wstring& spec : specs) {
        const std::wstring trimmed = Trim(spec);
        if (!trimmed.empty())
            out << trimmed << L"\n";
    }
}

std::vector<std::wstring> SteamLibrary::LoadCachedGameNames() {
    return ReadCachedGameNames();
}

std::vector<std::wstring> SteamLibrary::ListInstalledGameNames() {
    std::vector<std::wstring> games = ToNameList(ScanInstalledGames());
    if (!games.empty()) {
        SaveCachedGameNames(games);
        return games;
    }
    return ReadCachedGameNames();
}

std::vector<std::wstring> SteamLibrary::RefreshInstalledGameNames() {
    std::vector<std::wstring> games = ToNameList(ScanInstalledGames());
    if (!games.empty())
        SaveCachedGameNames(games);
    else
        games = ReadCachedGameNames();
    return games;
}

std::wstring SteamLibrary::MatchProcessToInstalledGame(const std::wstring& processPath) {
    if (processPath.empty())
        return {};
    return MatchProcessPathToGame(processPath, ScanInstalledGames());
}

std::wstring SteamLibrary::MatchProcessListToInstalledGame(const std::vector<std::wstring>& processPaths) {
    if (processPaths.empty())
        return {};
    return MatchProcessPathsToGame(processPaths, ScanInstalledGames());
}
