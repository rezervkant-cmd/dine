#pragma once
#include "core/types.hpp"
#include "core/thread_pool.hpp"
#include "mods/zipfile.hpp"
#include "mods/json.hpp"
#include <filesystem>
#include <functional>
#include <mutex>

namespace vw::mods {

struct TextureRGBA {
    int w = 0, h = 0;
    std::vector<u8> pixels;
    std::string source;
    bool animated = false;

};

struct ModInfo {
    std::string file_name;
    std::vector<std::string> mod_ids;
    std::string loader;
    size_t blockstates = 0, models = 0, textures = 0;
};

class AssetRegistry {
public:

    using PngDecoder = std::function<std::optional<TextureRGBA>(std::span<const u8>)>;
    void set_png_decoder(PngDecoder d) { decoder_ = std::move(d); }

    void scan_mods_dir(const std::filesystem::path& mods_dir, ThreadPool& pool);

    void scan_jar(const std::filesystem::path& jar);

    void clear();

    std::vector<ModInfo> mods() const {
        std::lock_guard lk(mtx_);
        return mods_;
    }
    size_t mods_count() const {
        std::lock_guard lk(mtx_);
        return mods_.size();
    }

    std::optional<std::string> resolve_block_texture(const std::string& block_id);

    std::optional<TextureRGBA> load_texture(const std::string& tex_ref);

    std::optional<std::vector<u8>> raw_texture_png(const std::string& tex_ref);

    std::optional<std::array<u8, 4>> average_color(const std::string& block_id);

    json::Value load_blockstate_json(const std::string& key) { return load_json_asset(blockstates_, key); }
    json::Value load_model_json(const std::string& key)      { return load_json_asset(models_, key); }
    bool has_texture(const std::string& ref) const {
        std::lock_guard lk(mtx_);
        return textures_.count(ref) != 0;
    }

    std::optional<std::vector<u8>> raw_bbmodel(const std::string& key);

    struct DecoEntry {
        std::string model;
        std::string material;
        float scale = 1.0f;
    };

    // Возвращает копию: ссылка на элемент deco_regs_ становится висячей
    // после clear() из другого потока.
    std::optional<DecoEntry> find_deco_entry(const std::string& block_id);

private:
    struct AssetRef { size_t jar_index; std::string path; };

    json::Value load_json_asset(const std::unordered_map<std::string, AssetRef>& table,
                                const std::string& key);
    std::optional<std::string> resolve_model_texture(const std::string& model_ref, int depth);

    std::vector<std::unique_ptr<zip::ZipFile>> jars_;
    std::vector<ModInfo> mods_;

    std::unordered_map<std::string, AssetRef> blockstates_;
    std::unordered_map<std::string, AssetRef> models_;
    std::unordered_map<std::string, AssetRef> textures_;

    std::unordered_map<std::string, AssetRef> bbmodels_;

    std::unordered_map<std::string, bool> animated_;

    std::unordered_map<std::string, std::pair<bool, std::map<std::string, DecoEntry>>> deco_regs_;

    void load_deco_registry(const std::string& ns);
    bool has_animation_meta(const std::string& tex_ref) const;

    std::unordered_map<std::string, std::string> block_tex_cache_;
    std::unordered_map<std::string, std::array<u8, 4>> color_cache_;
    PngDecoder decoder_;
    mutable std::recursive_mutex mtx_;
};

}
