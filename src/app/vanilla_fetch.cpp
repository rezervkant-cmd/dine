#include "vanilla_fetch.hpp"
#include "mods/json.hpp"
#include "core/platform.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace vw::app {

namespace fs = std::filesystem;

namespace {

std::string http_get(const std::string& url) {
    std::string cmd = std::string("curl -sL --max-time 60 \"") + url + "\"";
    FILE* p = VW_POPEN(cmd.c_str(), "r");
    if (!p) return {};
    std::string out;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    if (VW_PCLOSE(p) != 0 || out.empty()) {

        cmd = std::string("wget -qO- --timeout=60 \"") + url + "\"";
        p = VW_POPEN(cmd.c_str(), "r");
        if (!p) return {};
        out.clear();
        while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
        VW_PCLOSE(p);
    }
    return out;
}

bool http_download(const std::string& url, const fs::path& dst) {

#ifdef _WIN32
    const std::string d = vw::plat::acp_str(dst);
#else
    const std::string& d = dst.native();
#endif
    std::string cmd = std::string("curl -sL --max-time 300 -o \"") + d + "\" \"" + url + "\"";
    if (std::system(cmd.c_str()) == 0 && fs::exists(dst) && fs::file_size(dst) > 1024 * 1024)
        return true;
    cmd = std::string("wget -q --timeout=300 -O \"") + d + "\" \"" + url + "\"";
    return std::system(cmd.c_str()) == 0 && fs::exists(dst) && fs::file_size(dst) > 1024 * 1024;
}

fs::path cache_dir() {
    return vw::plat::data_dir();
}

}

fs::path download_vanilla_jar(const std::string& version_hint, std::string& error_out) {
    error_out.clear();
    const fs::path dir = cache_dir();
    std::error_code ec;
    fs::create_directories(dir, ec);

    if (!version_hint.empty()) {
        const fs::path cached = dir / ("client-" + version_hint + ".jar");
        if (fs::exists(cached, ec) && fs::file_size(cached, ec) > 1024 * 1024) return cached;
    }

    const std::string manifest = http_get("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json");
    if (manifest.empty()) {
        error_out = "нет доступа к piston-meta.mojang.com (offline?)";
        return {};
    }
    json::Value mv;
    try { mv = json::parse(manifest); } catch (...) { error_out = "манифест не парсится"; return {}; }

    std::string want = version_hint;
    if (want.empty())
        if (const auto* l = mv.get("latest"))
            if (const auto* r = l->get("release"); r && r->is_string()) want = *r->str();

    std::string ver_url, ver_id;
    if (const auto* vers = mv.get("versions"); vers && vers->is_array()) {
        for (const auto& v : *vers->arr()) {
            const auto* id = v.get("id");
            const auto* u = v.get("url");
            if (!id || !u || !id->is_string() || !u->is_string()) continue;
            if (*id->str() == want) { ver_id = *id->str(); ver_url = *u->str(); break; }
        }

        if (ver_url.empty())
            for (const auto& v : *vers->arr()) {
                const auto* ty = v.get("type");
                if (ty && ty->is_string() && *ty->str() == "release") {
                    ver_id = *v.get("id")->str();
                    ver_url = *v.get("url")->str();
                    break;
                }
            }
    }
    if (ver_url.empty()) { error_out = "версия не найдена в манифесте"; return {}; }

    const fs::path cached = dir / ("client-" + ver_id + ".jar");
    if (fs::exists(cached, ec) && fs::file_size(cached, ec) > 1024 * 1024) return cached;

    const std::string meta = http_get(ver_url);
    if (meta.empty()) { error_out = "метаданные версии недоступны"; return {}; }
    std::string client_url;
    try {
        auto jm = json::parse(meta);
        if (const auto* dl = jm.get("downloads"))
            if (const auto* cl = dl->get("client"))
                if (const auto* u = cl->get("url"); u && u->is_string()) client_url = *u->str();
    } catch (...) {}
    if (client_url.empty()) { error_out = "нет downloads.client в метаданных"; return {}; }

    fs::path tmp = cached;
    tmp += ".part";
    if (!http_download(client_url, tmp)) {
        error_out = "скачивание client.jar сорвалось";
        fs::remove(tmp, ec);
        return {};
    }
    fs::rename(tmp, cached, ec);
    return ec ? fs::path{} : cached;
}

}
