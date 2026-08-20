#pragma once
#include <filesystem>
#include <string>
#include <cstdlib>
#include <vector>
#include <algorithm>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <shellapi.h>
  #include <cstdio>
  #define VW_POPEN  _popen
  #define VW_PCLOSE _pclose
#else
  #include <unistd.h>
  #include <cstdio>
  #define VW_POPEN  popen
  #define VW_PCLOSE pclose
#endif

namespace vw::plat {

inline std::string u8str(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

inline std::filesystem::path path_from_u8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

#ifdef _WIN32
inline std::string acp_str(const std::filesystem::path& p) {
    const std::wstring w = p.wstring();
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(),
                                      nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(),
                                   s.data(), n, nullptr, nullptr);
    return s;
}
#endif

inline std::filesystem::path home_dir() {
#ifdef _WIN32
    if (const wchar_t* up = ::_wgetenv(L"USERPROFILE")) return up;
    if (const wchar_t* hd = ::_wgetenv(L"HOMEDRIVE")) {
        const wchar_t* hp = ::_wgetenv(L"HOMEPATH");
        return std::wstring(hd) + (hp ? hp : L"\\");
    }
    return "C:\\";
#else
    const char* h = std::getenv("HOME");
    return h ? h : "/";
#endif
}

inline std::filesystem::path exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
        return std::filesystem::path(buf).parent_path();
    return {};
#else
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = 0; return std::filesystem::path(buf).parent_path(); }
    return {};
#endif
}

inline std::filesystem::path data_dir() {
#ifdef _WIN32
    if (const wchar_t* la = ::_wgetenv(L"LOCALAPPDATA"))
        return std::filesystem::path(la) / "dine";
    return home_dir() / "AppData/Local/dine";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    return (xdg && *xdg ? std::filesystem::path(xdg)
                        : home_dir() / ".local/share") / "dine";
#endif
}

inline std::filesystem::path default_saves_dir() {
#ifdef _WIN32
    if (const wchar_t* ad = ::_wgetenv(L"APPDATA")) {
        auto p = std::filesystem::path(ad) / ".minecraft/saves";
        if (std::filesystem::exists(p)) return p;
    }
    return home_dir();
#else
    for (const char* rel : {".minecraft/saves", ".local/share/minecraft/saves",
                            ".var/app/com.mojang.Minecraft/.minecraft/saves"}) {
        auto p = home_dir() / rel;
        if (std::filesystem::exists(p)) return p;
    }
    return home_dir();
#endif
}

inline std::string find_system_font() {
#ifdef _WIN32
    for (const char* f : {"C:\\Windows\\Fonts\\segoeui.ttf",
                          "C:\\Windows\\Fonts\\tahoma.ttf",
                          "C:\\Windows\\Fonts\\arial.ttf",
                          "C:\\Windows\\Fonts\\calibri.ttf"})
        if (std::filesystem::exists(f)) return f;
    return {};
#else
    if (FILE* p = VW_POPEN("fc-match -f '%{file}' sans 2>/dev/null", "r")) {
        char buf[512] = {};
        const size_t n = fread(buf, 1, sizeof(buf) - 1, p);
        VW_PCLOSE(p);
        buf[n] = 0;
        std::string f(buf);
        if (!f.empty() && std::filesystem::exists(f) &&
            (f.ends_with(".ttf") || f.ends_with(".otf") || f.ends_with(".ttc")))
            return f;
    }
    for (const char* c : {"/usr/share/fonts/TTF/DejaVuSans.ttf",
                          "/usr/share/fonts/noto/NotoSans-Regular.ttf",
                          "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
                          "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                          "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                          "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                          "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                          "/usr/share/fonts/google-noto/NotoSans-Regular.ttf"})
        if (std::filesystem::exists(c)) return c;
    return {};
#endif
}

inline std::vector<std::filesystem::path> launcher_version_dirs() {
    std::vector<std::filesystem::path> out;
#ifdef _WIN32
    if (const wchar_t* ad = ::_wgetenv(L"APPDATA")) {
        auto p = std::filesystem::path(ad) / ".minecraft/versions";
        out.push_back(p);
        auto p2 = std::filesystem::path(ad) / ".minecraft" / "versions";
        if (p2 != p) out.push_back(p2);
    }
    if (const wchar_t* la = ::_wgetenv(L"LOCALAPPDATA")) {
        out.push_back(std::filesystem::path(la) / "PrismLauncher/instances");
        out.push_back(std::filesystem::path(la) / "PolyMC/instances");
    }
    auto home = home_dir();
    out.push_back(home / ".minecraft/versions");
    out.push_back(home / "AppData/Roaming/.minecraft/versions");
#else
    auto home = home_dir();
    out.push_back(home / ".minecraft/versions");
    out.push_back(home / ".var/app/com.mojang.Minecraft/.minecraft/versions");
    out.push_back(home / ".local/share/PrismLauncher/instances");
    out.push_back(home / ".local/share/PolyMC/instances");
    out.push_back(home / "snap/minecraft-launcher/current/.minecraft/versions");
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        out.push_back(std::filesystem::path(xdg) / ".minecraft/versions");
    }
#endif
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}
