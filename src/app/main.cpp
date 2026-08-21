#include "anvil/world.hpp"
#include "mods/mod_assets.hpp"
#include "mods/block_model.hpp"
#include "mesh/grid.hpp"
#include "render/mc_mesher.hpp"
#include "render/top_map.hpp"
#include "export/mc_export.hpp"
#include "app/renderer.hpp"
#include "app/file_dialog.hpp"
#include "app/vanilla_fetch.hpp"
#include "core/thread_pool.hpp"
#include "core/platform.hpp"
#include "core/biome_colors.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <future>

#include <stb_image.h>
#include "app_icon.hpp"

using namespace vw;

namespace {

std::optional<mods::TextureRGBA> stb_decode_png(std::span<const u8> bytes) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                        &w, &h, &comp, 4);
    if (!px) return std::nullopt;
    mods::TextureRGBA t;
    t.w = w; t.h = h;
    t.pixels.assign(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    return t;
}

std::string find_system_font() {
    if (auto f = plat::find_system_font(); !f.empty()) return f;
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
    static const char* candidates[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
    };
    for (const char* c : candidates)
        if (std::filesystem::exists(c)) return c;
    return {};
}

void setup_fonts(ImGuiIO& io) {
    const std::string font = find_system_font();
    if (font.empty()) {
        std::fprintf(stderr,
            "dine: системный TTF не найден, UI останется латиницей.\n"
            "Установите шрифт: pacman -S ttf-dejavu\n");
        io.Fonts->AddFontDefault();
        return;
    }
    static const ImWchar ranges[] = {
        0x0020, 0x00FF,
        0x0400, 0x052F,
        0x2010, 0x2265,
        0,
    };
    if (!io.Fonts->AddFontFromFileTTF(font.c_str(), 17.0f, nullptr, ranges)) {
        std::fprintf(stderr, "dine: не удалось загрузить %s\n", font.c_str());
        io.Fonts->AddFontDefault();
    }
}

struct BuildResult {
    render::MCMesh tmesh;
    std::shared_ptr<mesh::TextureAtlas> atlas;
    std::vector<render::BlockRender> renders;
    size_t chunk_count = 0;
    size_t textured = 0, total_mats = 0;
    bool palette_overflow = false;
    BBox box{};
};

struct MapView2D {
    GLuint tex = 0;
    render::TopMap img;
    bool stale = true;
    bool building = false;
    std::future<render::TopMap> future;

    f32 zoom = 4.0f;
    f32 pan[2] = {0.0f, 0.0f};

    bool dragging = false;
    i32 drag_ax = 0, drag_az = 0;
    bool fit_requested = false;

    bool hover_valid = false;
    i32 hover_x = 0, hover_z = 0;
};

// Результат пересборки реестра ассетов. Фоновая задача НЕ трогает поля
// AppState: она возвращает этот объект, а главный поток применяет его
// (иначе vanilla_info/vanilla_loaded читаются GUI без синхронизации).
struct AssetRebuildResult {
    bool vanilla_loaded = false;
    std::filesystem::path vanilla_jar_path;
    std::string vanilla_info;
};

struct AppState {
    std::unique_ptr<anvil::World> world;
    mods::AssetRegistry assets;
    std::unique_ptr<mods::ModelResolver> models;
    ThreadPool pool;

    render::MCMesh tmesh;
    std::shared_ptr<mesh::TextureAtlas> gpu_atlas;
    std::vector<render::BlockRender> renders;
    int sel[6] = {-64, 0, -64, 63, 128, 63};
    bool skip_fluids = false;
    bool hide_decor = false;
    bool hide_barriers = false;
    bool fancy_leaves = true;
    int texture_mode = 0;
    bool follow_area = true;
    bool dirty = false;
    double last_edit = -1e9;
    bool building = false;
    std::future<BuildResult> build_future;

    int view_mode = 0;
    MapView2D map;

    bool scanning = false;
    std::future<AssetRebuildResult> mods_future;
    std::filesystem::path pending_mods;

    app::OrbitCamera cam;
    app::MCRenderer renderer;

    app::DirPicker world_picker;
    app::DirPicker mods_picker;
    app::DirPicker export_picker;
    std::filesystem::path mods_dir;

    char export_dir[512] = "export";
    std::string export_base = "world";
    std::string status = "Откройте мир кнопкой \"Открыть мир...\"";
    std::string vanilla_info = "client.jar не найден — у ванильных блоков будут цвета";
    std::filesystem::path vanilla_jar_path;
    bool vanilla_loaded = false;
    bool fetching_vanilla = false;
    std::future<std::pair<std::filesystem::path, std::string>> vanilla_future;
    // Скачанный client.jar, ожидающий применения, пока идёт построение меша.
    // Храним путь, а не deferred-future: deferred никогда не станет ready.
    std::filesystem::path pending_vanilla_jar;
    uint64_t world_gen = 0;
    uint64_t vanilla_fetch_gen = 0;
    std::string vanilla_fetch_ver;
};

std::filesystem::path default_saves_dir() {
    return plat::default_saves_dir();
}

static std::vector<int> parse_ver(const std::string& s) {
    std::vector<int> v;
    int cur = 0;
    bool have = false;
    for (char c : s) {
        if (c >= '0' && c <= '9') { cur = cur * 10 + (c - '0'); have = true; }
        else if (have) { v.push_back(cur); cur = 0; have = false; }
    }
    if (have) v.push_back(cur);
    return v;
}

std::filesystem::path find_bundle_for(const std::string& world_ver) {
    std::error_code ec;
    std::filesystem::path exe_dir = plat::exe_dir();

    const auto want = parse_ver(world_ver);
    std::filesystem::path best;
    int best_score = -1;
    std::vector<int> best_ver;
    for (const auto& dir : {exe_dir, std::filesystem::current_path(ec)}) {
        if (dir.empty() || !std::filesystem::exists(dir, ec)) continue;
        for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
            const auto fn = plat::u8str(e.path().filename());
            if (!fn.starts_with("vanilla-assets") || !fn.ends_with(".jar")) continue;
            const auto ver = parse_ver(fn);
            int score = 0;
            for (size_t i = 0; i < std::min(want.size(), ver.size()); ++i) {
                if (want[i] == ver[i]) ++score;
                else break;
            }
            const bool newer = ver > best_ver;
            if (score > best_score || (score == best_score && newer)) {
                best_score = score;
                best_ver = ver;
                best = e.path();
            }
        }
    }
    return best;
}

std::filesystem::path find_vanilla_jar() {
    std::error_code ec;

    if (auto b = find_bundle_for(""); !b.empty()) return b;

    {
        std::filesystem::path cache = plat::data_dir();
        if (std::filesystem::exists(cache, ec)) {
            std::filesystem::path best;
            std::filesystem::file_time_type bt{};
            for (auto& e : std::filesystem::directory_iterator(cache, ec))
                if (e.path().extension() == ".jar") {
                    auto t = std::filesystem::last_write_time(e.path(), ec);
                    if (best.empty() || t > bt) { best = e.path(); bt = t; }
                }
            if (!best.empty()) return best;
        }
    }
    const std::filesystem::path home = plat::home_dir();

    std::vector<std::filesystem::path> version_dirs = plat::launcher_version_dirs();
    std::filesystem::path best;
    std::filesystem::file_time_type best_time{};
    auto consider = [&](const std::filesystem::path& jar) {
        std::error_code e2;
        if (!std::filesystem::exists(jar, e2)) return;
        const auto sz = std::filesystem::file_size(jar, e2);
        if (e2 || sz < 4 * 1024 * 1024) return;
        const auto t = std::filesystem::last_write_time(jar, e2);
        if (best.empty() || t > best_time) { best = jar; best_time = t; }
    };
    for (const auto& vers : version_dirs) {
        if (!std::filesystem::exists(vers, ec)) continue;
        for (auto& e : std::filesystem::directory_iterator(vers, ec)) {
            if (!e.is_directory(ec)) continue;

            {
                auto dname = e.path().filename();
                dname += ".jar";
                consider(e.path() / dname);
            }

            for (auto& f : std::filesystem::directory_iterator(e.path(), ec))
                if (f.path().extension() == ".jar" &&
                    plat::u8str(f.path().filename()).find("client") != std::string::npos)
                    consider(f.path());
        }
    }
    return best;
}

void load_world(AppState& st, const std::filesystem::path& path);
void start_rebuild(AppState& st);

// Один мир = один ванильный jar + текущие моды.
// Может выполняться в фоне: работает только с AssetRegistry (у него свой
// мьютекс) и с локальными копиями путей.
AssetRebuildResult rebuild_assets_job(mods::AssetRegistry& assets, ThreadPool& pool,
                                      std::filesystem::path mods_dir,
                                      std::filesystem::path vanilla) {
    AssetRebuildResult r;
    assets.clear();
    if (!vanilla.empty()) {
        assets.scan_jar(vanilla);
        r.vanilla_loaded = true;
        r.vanilla_jar_path = vanilla;
        r.vanilla_info = "Ваниль: " + plat::u8str(vanilla.filename());
    } else {
        r.vanilla_info = "client.jar не найден — у ванильных блоков будут цвета";
    }
    if (!mods_dir.empty()) assets.scan_mods_dir(mods_dir, pool);
    return r;
}

// Применение результата — только из главного потока.
void apply_asset_rebuild(AppState& st, const AssetRebuildResult& r) {
    st.vanilla_loaded = r.vanilla_loaded;
    st.vanilla_jar_path = r.vanilla_jar_path;
    st.vanilla_info = r.vanilla_info;
    if (st.models) st.models->clear_cache();
}

// Синхронный вариант для главного потока (загрузка мира и т. п.).
void rebuild_asset_registry(AppState& st, const std::filesystem::path& vanilla) {
    if (st.models) st.models->clear_cache();
    apply_asset_rebuild(st, rebuild_assets_job(st.assets, st.pool, st.mods_dir, vanilla));
}

// Реестр ассетов можно менять только когда ни одна фоновая задача его не
// читает: построение меша, 2D-карта и сканирование модов держат сырой
// указатель на AssetRegistry.
bool assets_idle(const AppState& st) {
    return !st.building && !st.scanning && !st.map.building;
}

// Палитра цветов биомов мира.
std::vector<BiomeColors> build_biome_palette(mods::AssetRegistry& assets,
                                             const std::vector<std::string>& names) {
    auto grass_cm = assets.load_texture("minecraft:colormap/grass");
    auto foliage_cm = assets.load_texture("minecraft:colormap/foliage");
    auto sample = [](const std::optional<mods::TextureRGBA>& cm, f32 x, f32 y,
                     u8 dr, u8 dg, u8 db) -> BiomeRGB {
        if (cm && cm->w > 0 && cm->h > 0 &&
            cm->pixels.size() >= static_cast<size_t>(cm->w) * cm->h * 4) {
            const int px = std::clamp(static_cast<int>(x * (cm->w - 1) + 0.5f), 0, cm->w - 1);
            const int py = std::clamp(static_cast<int>(y * (cm->h - 1) + 0.5f), 0, cm->h - 1);
            const u8* p = &cm->pixels[(static_cast<size_t>(py) * cm->w + px) * 4];
            return {p[0], p[1], p[2]};
        }
        return {dr, dg, db};
    };
    std::vector<BiomeColors> pal;
    pal.reserve(names.size() + 1);
    for (const auto& n : names) {
        const auto [t, d] = biome_temp_rain(n);
        const auto [cx, cy] = biome_colormap_xy(t, d);
        BiomeColors bc;
        bc.grass = sample(grass_cm, cx, cy, 0x91, 0xBD, 0x59);
        bc.foliage = sample(foliage_cm, cx, cy, 0x77, 0xAB, 0x2F);
        bc.water = biome_water_rgb(n);
        pal.push_back(bc);
    }
    if (pal.empty()) pal.push_back({{0x91, 0xBD, 0x59}, {0x77, 0xAB, 0x2F}, {0x3F, 0x76, 0xE4}});
    return pal;
}

void start_rebuild(AppState& st) {
    if (!st.world || st.building) return;
    if (st.map.building) {
        st.dirty = true;
        return;
    }
    if (st.scanning) {
        st.dirty = true;
        return;
    }
    st.building = true;

    BBox box{std::min(st.sel[0], st.sel[3]), std::min(st.sel[1], st.sel[4]),
             std::min(st.sel[2], st.sel[5]), std::max(st.sel[0], st.sel[3]),
             std::max(st.sel[1], st.sel[4]), std::max(st.sel[2], st.sel[5])};
    st.status = "Построение: X[" + std::to_string(box.x0) + ".." + std::to_string(box.x1) +
                "] Y[" + std::to_string(box.y0) + ".." + std::to_string(box.y1) +
                "] Z[" + std::to_string(box.z0) + ".." + std::to_string(box.z1) + "]...";

    const i64 vol = i64(box.sx()) * box.sy() * box.sz();
    const i64 kMaxVol = 512LL * 1024 * 1024;
    if (vol > kMaxVol) {
        st.building = false;
        st.status = "Область слишком велика (" + std::to_string(vol) +
                    " блоков). Уменьшите X/Z или диапазон Y.";
        return;
    }
    const int texmode = st.texture_mode;
    render::MCMeshOptions topts;
    topts.skip_fluids = st.skip_fluids;
    topts.skip_decor = st.hide_decor;
    topts.skip_bedrock = true;
    topts.skip_barriers = st.hide_barriers;
    topts.fancy_leaves = st.fancy_leaves;

    anvil::World* world = st.world.get();
    ThreadPool* pool = &st.pool;
    mods::AssetRegistry* assets = &st.assets;
    mods::ModelResolver* models = st.models.get();
    try {
        st.build_future = std::async(std::launch::async,
            [world, pool, assets, models, box, topts, texmode]() -> BuildResult {
            BuildResult r;
        r.box = box;
        auto chunks = world->load_area(*pool, box.x0 >> 4, box.z0 >> 4,
                                       box.x1 >> 4, box.z1 >> 4);
        r.chunk_count = chunks.size();
        auto grid = mesh::make_grid(chunks, box);
        grid.biome_pal = build_biome_palette(*assets, world->biome_names());

        r.atlas = std::make_shared<mesh::TextureAtlas>();
        auto rmats = render::build_block_renders(world->palette(), *assets, *models,
                                                 *r.atlas, texmode);
        r.total_mats = rmats.size();
        r.atlas->bake();
        r.textured = r.atlas->real_texture_count();
        r.tmesh = render::build_mc_mesh(grid, rmats, *r.atlas, topts, pool);
        r.renders = std::move(rmats);
        r.palette_overflow = world->palette().overflowed();
        return r;
    });
    } catch (const std::exception& e) {
        // std::async может бросить (bad_alloc и т.п.) до возврата future —
        // без этого сброса st.building остался бы true навсегда и кнопка
        // «Перестроить» была бы заблокирована до перезапуска.
        st.building = false;
        st.status = std::string("Не удалось запустить построение: ") + e.what();
    }
}

void start_map_build(AppState& st) {
    if (!st.world || st.map.building || st.scanning || st.building) return;
    const i32 cx = (st.sel[0] + st.sel[3]) / 2;
    const i32 cz = (st.sel[2] + st.sel[5]) / 2;
    const i32 span = std::max({st.sel[3] - st.sel[0] + 1, st.sel[5] - st.sel[2] + 1, 16});

    const i32 r = std::clamp(i32(span * 1.6 / 2), 64, 1024);
    st.map.building = true;
    st.map.stale = false;
    st.status = "Построение 2D-карты...";
    anvil::World* world = st.world.get();
    const mods::AssetRegistry* assets = &st.assets;
    ThreadPool* pool = &st.pool;
    try {
        st.map.future = std::async(std::launch::async,
            [world, assets, cx, cz, r, pool]() -> render::TopMap {
                return render::build_top_map(*world, assets, cx - r, cz - r,
                                             cx + r - 1, cz + r - 1, 319, pool);
            });
    } catch (const std::exception& e) {
        // См. start_rebuild: без сброса флаг map.building завис бы навсегда.
        st.map.building = false;
        st.status = std::string("Не удалось запустить построение карты: ") + e.what();
    }
}

void load_world(AppState& st, const std::filesystem::path& path) {

    st.export_base = plat::u8str(path.filename());
    if (st.export_base.empty()) st.export_base = "world";
    ++st.world_gen;
    st.pending_vanilla_jar.clear();  // новый мир — новый источник ванильных ассетов
    if (st.building && st.build_future.valid()) {
        st.status = "Ожидание завершения построения перед сменой мира...";
        try { st.build_future.get(); } catch (...) {}
        st.building = false;
    }
    if (st.map.building && st.map.future.valid()) {
        st.status = "Ожидание завершения построения карты...";
        try { st.map.future.get(); } catch (...) {}
        st.map.building = false;
    }
    if (st.scanning && st.mods_future.valid()) {
        st.status = "Ожидание сканирования модов перед сменой мира...";
        try { st.mods_future.get(); } catch (...) {}
        st.scanning = false;
    }
    try {
        st.world = std::make_unique<anvil::World>(path);
        const auto& wi = st.world->info();

        // Один мир = один источник ванильных ассетов.
        std::filesystem::path vanilla;
        if (!wi.version_name.empty()) vanilla = find_bundle_for(wi.version_name);
        if (vanilla.empty()) vanilla = find_vanilla_jar();
        try {
            rebuild_asset_registry(st, vanilla);
            if (st.vanilla_loaded && !wi.version_name.empty())
                st.vanilla_info = "Ваниль (" + wi.version_name + "): " +
                                  plat::u8str(vanilla.filename());
        } catch (const std::exception& e) {
            st.assets.clear();
            if (st.models) st.models->clear_cache();
            st.vanilla_loaded = false;
            st.vanilla_jar_path.clear();
            st.vanilla_info = std::string("Не удалось загрузить vanilla assets: ") + e.what();
        }
        st.sel[0] = wi.spawn_x - 64; st.sel[3] = wi.spawn_x + 63;
        st.sel[2] = wi.spawn_z - 64; st.sel[5] = wi.spawn_z + 63;
        st.cam.target[0] = static_cast<f32>(wi.spawn_x);
        st.cam.target[1] = static_cast<f32>(wi.spawn_y);
        st.cam.target[2] = static_cast<f32>(wi.spawn_z);
        st.status = "Мир загружен: " + wi.name;
        st.map.stale = true;
        st.map.pan[0] = st.map.pan[1] = 0.0f;
        start_rebuild(st);
    } catch (const std::exception& e) {
        st.status = std::string("Ошибка загрузки мира: ") + e.what();
    } catch (...) {
        st.status = "Ошибка загрузки мира (неизвестная причина).";
    }
}

void do_export(AppState& st) {

    if (!st.gpu_atlas || st.tmesh.vertices.empty()) {
        st.status = "Нечего экспортировать: постройте меш.";
        return;
    }
    exporter::MCExportContext ctx{&st.tmesh, st.gpu_atlas.get(), 1.0f,
                                  st.renders.empty() ? nullptr : &st.renders};
    std::filesystem::path dir = plat::path_from_u8(st.export_dir);
    if (dir.empty()) dir = ".";
    std::filesystem::create_directories(dir);
    std::filesystem::path p = dir / plat::path_from_u8(st.export_base + ".glb");
    const bool ok = exporter::export_mc_glb(ctx, p);
    st.status = ok ? ("Экспортировано GLB (как в вьюпорте): " + plat::u8str(p))
                   : ("Ошибка экспорта: " + plat::u8str(p));
}

void do_export_obj(AppState& st) {
    if (!st.gpu_atlas || st.tmesh.vertices.empty()) {
        st.status = "Нечего экспортировать: постройте меш.";
        return;
    }
    exporter::MCExportContext ctx{&st.tmesh, st.gpu_atlas.get(), 1.0f,
                                  st.renders.empty() ? nullptr : &st.renders};
    std::filesystem::path dir = plat::path_from_u8(st.export_dir);
    if (dir.empty()) dir = ".";
    std::filesystem::create_directories(dir);
    std::filesystem::path p = dir / plat::path_from_u8(st.export_base + ".obj");
    const bool ok = exporter::export_mc_obj(ctx, p);
    st.status = ok ? ("Экспортировано OBJ + отдельные PNG для MCprep: " + plat::u8str(p))
                   : ("Ошибка экспорта OBJ: " + plat::u8str(p));
}

void draw_map_window(AppState& st) {
    ImGuiViewport* vport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vport->WorkPos);
    ImGui::SetNextWindowSize(vport->WorkSize);
    const ImGuiWindowFlags wf =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (!ImGui::Begin("##dine_map2d", nullptr, wf)) { ImGui::End(); return; }

    if (ImGui::RadioButton("3D", st.view_mode == 0)) st.view_mode = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("2D", st.view_mode == 1)) st.view_mode = 1;
    ImGui::SameLine(0, 18);
    if (ImGui::Button("Обновить карту")) st.map.stale = true;
    ImGui::SameLine();
    if (ImGui::Button("К выделению")) st.map.fit_requested = true;
    ImGui::SameLine(0, 18);
    ImGui::TextDisabled("ЛКМ — область | ПКМ/СКМ — сдвиг | колесо — масштаб");
    ImGui::SameLine(0, 18);
    if (st.map.hover_valid)
        ImGui::Text("X: %d  Z: %d", st.map.hover_x, st.map.hover_z);
    if (!st.map.img.empty()) {
        ImGui::SameLine(0, 18);
        ImGui::TextDisabled("поверхность Y %d..%d", st.map.img.y_min, st.map.img.y_peak);
    }
    ImGui::Separator();

    ImGui::BeginChild("##mapcanvas", ImVec2(0, 0), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
    auto& m = st.map;
    const ImVec2 cpos = ImGui::GetCursorScreenPos();
    const ImVec2 csize = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(cpos, ImVec2(cpos.x + csize.x, cpos.y + csize.y),
                      IM_COL32(18, 19, 22, 255));

    const bool have_img = !m.img.empty() && m.tex != 0;
    const f32 map_cx = have_img ? m.img.x0 + m.img.w * 0.5f
                                : (st.sel[0] + st.sel[3] + 1) * 0.5f;
    const f32 map_cz = have_img ? m.img.z0 + m.img.h * 0.5f
                                : (st.sel[2] + st.sel[5] + 1) * 0.5f;
    const ImVec2 cbase{cpos.x + csize.x * 0.5f, cpos.y + csize.y * 0.5f};
    auto w2s = [&](f32 wx, f32 wz) -> ImVec2 {
        return {cbase.x + m.pan[0] + (wx - map_cx) * m.zoom,
                cbase.y + m.pan[1] + (wz - map_cz) * m.zoom};
    };
    auto s2w = [&](ImVec2 p) -> ImVec2 {
        return {map_cx + (p.x - cbase.x - m.pan[0]) / m.zoom,
                map_cz + (p.y - cbase.y - m.pan[1]) / m.zoom};
    };

    if (m.fit_requested) {
        m.fit_requested = false;
        const i32 sx0 = std::min(st.sel[0], st.sel[3]), sx1 = std::max(st.sel[0], st.sel[3]);
        const i32 sz0 = std::min(st.sel[2], st.sel[5]), sz1 = std::max(st.sel[2], st.sel[5]);
        const f32 bw = f32(sx1 - sx0 + 33), bh = f32(sz1 - sz0 + 33);
        m.zoom = std::clamp(std::min(csize.x / bw, csize.y / bh), 0.25f, 64.0f);
        const f32 scx = (sx0 + sx1 + 1) * 0.5f, scz = (sz0 + sz1 + 1) * 0.5f;
        m.pan[0] = (map_cx - scx) * m.zoom;
        m.pan[1] = (map_cz - scz) * m.zoom;
    }

    ImGui::InvisibleButton("##mapinput", csize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mp = io.MousePos;
    m.hover_valid = hovered;
    if (hovered) {
        const ImVec2 w = s2w(mp);
        m.hover_x = (i32)std::floor(w.x);
        m.hover_z = (i32)std::floor(w.y);
    }

    if (hovered && io.MouseWheel != 0.0f) {
        const f32 old = m.zoom;
        m.zoom = std::clamp(old * (io.MouseWheel > 0 ? 1.25f : 0.8f), 0.25f, 64.0f);
        const f32 k = 1.0f - m.zoom / old;
        m.pan[0] += k * (mp.x - cbase.x - m.pan[0]);
        m.pan[1] += k * (mp.y - cbase.y - m.pan[1]);
    }

    if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                    ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {
        m.pan[0] += io.MouseDelta.x;
        m.pan[1] += io.MouseDelta.y;
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 w = s2w(mp);
        m.dragging = true;
        m.drag_ax = (i32)std::floor(w.x);
        m.drag_az = (i32)std::floor(w.y);
        st.sel[0] = st.sel[3] = m.drag_ax;
        st.sel[2] = st.sel[5] = m.drag_az;
    }
    if (m.dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 w = s2w(mp);
        st.sel[0] = m.drag_ax;                  st.sel[3] = (i32)std::floor(w.x);
        st.sel[2] = m.drag_az;                  st.sel[5] = (i32)std::floor(w.y);
    }
    if (m.dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m.dragging = false;
        st.dirty = true;
        st.last_edit = ImGui::GetTime();
    }

    if (have_img) {
        const ImVec2 p0 = w2s(f32(m.img.x0), f32(m.img.z0));
        const ImVec2 p1 = w2s(f32(m.img.x0 + m.img.w), f32(m.img.z0 + m.img.h));
        dl->AddImage(static_cast<ImTextureID>(static_cast<ImU64>(m.tex)), p0, p1);

        if (m.zoom >= 2.0f) {
            const ImU32 gc = IM_COL32(255, 255, 255, 26);
            for (i32 x = m.img.x0 & ~15; x <= m.img.x0 + m.img.w; x += 16)
                dl->AddLine(w2s(f32(x), f32(m.img.z0)),
                            w2s(f32(x), f32(m.img.z0 + m.img.h)), gc);
            for (i32 z = m.img.z0 & ~15; z <= m.img.z0 + m.img.h; z += 16)
                dl->AddLine(w2s(f32(m.img.x0), f32(z)),
                            w2s(f32(m.img.x0 + m.img.w), f32(z)), gc);
        }
    } else if (m.building || m.stale) {
        dl->AddText(ImVec2(cpos.x + 12, cpos.y + 12), IM_COL32(220, 220, 220, 255),
                    "Построение 2D-карты...");
    } else {
        dl->AddText(ImVec2(cpos.x + 12, cpos.y + 12), IM_COL32(220, 220, 220, 255),
                    "Нажмите \"Обновить карту\".");
    }

    const i32 sx0 = std::min(st.sel[0], st.sel[3]), sx1 = std::max(st.sel[0], st.sel[3]);
    const i32 sz0 = std::min(st.sel[2], st.sel[5]), sz1 = std::max(st.sel[2], st.sel[5]);
    const ImVec2 s0 = w2s(f32(sx0), f32(sz0));
    const ImVec2 s1 = w2s(f32(sx1 + 1), f32(sz1 + 1));
    dl->AddRectFilled(s0, s1, IM_COL32(255, 64, 64, 32));
    dl->AddRect(s0, s1, IM_COL32(255, 80, 80, 235), 0.0f, 0, 2.0f);

    ImGui::EndChild();
    ImGui::End();
}

void ui_frame(AppState& st) {
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                 ImGuiDockNodeFlags_PassthruCentralNode);

    if (st.view_mode == 1 && st.world) draw_map_window(st);

    ImGui::Begin("Мир / Выделение");
    if (ImGui::Button("Открыть мир..."))
        st.world_picker.open("Выбор папки мира (зелёные — миры)", default_saves_dir());
    ImGui::SameLine();
    if (ImGui::Button("Папка модов..."))
        st.mods_picker.open("Выбор папки mods", st.mods_dir.empty()
            ? plat::home_dir()
            : st.mods_dir);
    ImGui::Separator();
    if (st.world) {
        const auto& wi = st.world->info();
        ImGui::Text("Мир: %s", wi.name.c_str());
        ImGui::Text("Версия: %s (DataVersion %d)",
                    wi.version_name.empty() ? "?" : wi.version_name.c_str(), wi.data_version);
        const auto& dims = st.world->dimensions();
        if (dims.size() > 1) {
            if (ImGui::BeginCombo("Измерение", st.world->dimension().c_str())) {
                for (const auto& dim : dims) {
                    const bool selected = dim == st.world->dimension();
                    if (ImGui::Selectable(dim.c_str(), selected) && !st.building && !st.scanning && !st.map.building) {
                        st.world->set_dimension(dim);
                        st.map.stale = true;
                        st.dirty = true;
                        st.last_edit = ImGui::GetTime();
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::Text("Измерение: %s", st.world->dimension().c_str());
        }
        ImGui::Text("Палитра: %zu блок-состояний", st.world->palette_size());
        ImGui::Separator();

        if (ImGui::RadioButton("3D  вид", st.view_mode == 0)) st.view_mode = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton("2D  карта", st.view_mode == 1)) st.view_mode = 1;
        ImGui::SameLine();
        ImGui::TextDisabled(st.view_mode == 1 ? "(ЛКМ по карте — область)" : "");
        ImGui::Separator();

        ImGui::TextUnformatted("Область экспорта (блоки):");

        bool edited = false;
        edited |= ImGui::DragIntRange2("X", &st.sel[0], &st.sel[3]);
        edited |= ImGui::DragIntRange2("Z", &st.sel[2], &st.sel[5]);
        edited |= ImGui::DragIntRange2("Y (высота)", &st.sel[1], &st.sel[4], 1.0f, -64, 320);
        edited |= ImGui::Checkbox("Убрать жидкости", &st.skip_fluids);
        edited |= ImGui::Checkbox("Убрать барьеры", &st.hide_barriers);
        edited |= ImGui::Checkbox("Скрыть растения и декор", &st.hide_decor);
        edited |= ImGui::Checkbox("Густая листва (cutout)", &st.fancy_leaves);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Внутренние грани кроны сохраняются: листва не считается\n"
                              "окклюдером. Выключите для быстрого внешнего контура.");
        const char* texmodes[] = {"Текстуры блоков", "Средние цвета", "Плоские цвета"};
        edited |= ImGui::Combo("Текстуры", &st.texture_mode, texmodes, 3);
        ImGui::Checkbox("Камера следует за областью", &st.follow_area);
        if (edited) {
            st.dirty = true;
            st.last_edit = ImGui::GetTime();
        }

        if (ImGui::Button(st.building ? "Построение..." : "Перестроить меш") && !st.building) {
            st.dirty = false;
            start_rebuild(st);
        }
        if (st.dirty && !st.building) {
            ImGui::SameLine();
            ImGui::TextDisabled("(изменено — перестроится...)");
        }
        const size_t quad_count = (st.tmesh.indices.size() + st.tmesh.indices_alpha.size()) / 6;
        ImGui::Text("Квадов: %zu  (GLB: %zu треугольников)", quad_count, quad_count * 2);

        if (ImGui::TreeNode("Топ блоков в сцене")) {
            const auto& q = st.tmesh.quads_by_block;
            std::vector<std::pair<u32, u16>> top;
            for (size_t i = 0; i < q.size(); ++i)
                if (q[i]) top.emplace_back(q[i], static_cast<u16>(i));
            std::sort(top.rbegin(), top.rend());
            for (size_t i = 0; i < std::min<size_t>(top.size(), 12); ++i)
                ImGui::Text("%7u  %s", top[i].first,
                            st.world->palette_get(top[i].second).name.c_str());
            ImGui::TreePop();
        }
    } else {
        ImGui::TextUnformatted("Мир не загружен.");
        ImGui::TextDisabled("Нажмите \"Открыть мир...\" и выберите папку\nсохранения (подсвечены зелёным).");
    }
    ImGui::End();

    if (st.world_picker.draw())
        load_world(st, st.world_picker.result());
    if (st.export_picker.draw())
        std::snprintf(st.export_dir, sizeof(st.export_dir), "%s",
                      plat::u8str(st.export_picker.result()).c_str());
    if (st.mods_picker.draw()) {

        if (st.building) st.pending_mods = st.mods_picker.result();
        else st.pending_mods = st.mods_picker.result();
    }
    // Реестр перестраиваем только когда его не читает ни одна фоновая
    // задача — включая построение 2D-карты (иначе AssetRegistry::clear()
    // рвёт JAR-объекты под ногами у потока карты).
    if (!st.pending_mods.empty() && assets_idle(st)) {
        st.mods_dir = st.pending_mods;
        st.pending_mods.clear();
        st.scanning = true;
        st.status = "Сканирование модов...";
        const auto vanilla = st.vanilla_jar_path;
        const auto mods_dir = st.mods_dir;
        mods::AssetRegistry* assets = &st.assets;
        ThreadPool* pool = &st.pool;
        // Фоновая задача не трогает AppState: результат применяется ниже,
        // уже в главном потоке.
        try {
            st.mods_future = std::async(std::launch::async, [assets, pool, mods_dir, vanilla] {
                return rebuild_assets_job(*assets, *pool, mods_dir, vanilla);
            });
        } catch (const std::exception& e) {
            // Иначе st.scanning завис бы в true до перезапуска.
            st.scanning = false;
            st.vanilla_info = std::string("Не удалось запустить сканирование модов: ") + e.what();
        }
    }

    if (st.scanning && st.mods_future.valid() &&
        st.mods_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try { apply_asset_rebuild(st, st.mods_future.get()); }
        catch (const std::exception& e) {
            st.vanilla_info = std::string("Ошибка сканирования модов: ") + e.what();
            if (st.models) st.models->clear_cache();
        }
        st.scanning = false;
        st.status = "Моды: просканировано " + std::to_string(st.assets.mods().size()) + " jar";
        if (st.world) { st.dirty = true; st.last_edit = ImGui::GetTime() - 1.0;
                        st.map.stale = true; }
    }

    ImGui::Begin("Моды");
    ImGui::TextWrapped("%s", st.vanilla_info.c_str());
    ImGui::Separator();
    ImGui::Text("Загружено jar: %zu", st.assets.mods().size());
    for (const auto& mi : st.assets.mods()) {
        if (ImGui::TreeNode(mi.file_name.c_str())) {
            ImGui::Text("Загрузчик: %s", mi.loader.c_str());
            ImGui::Text("blockstates: %zu, models: %zu, textures: %zu",
                        mi.blockstates, mi.models, mi.textures);
            for (const auto& id : mi.mod_ids) ImGui::BulletText("%s", id.c_str());
            ImGui::TreePop();
        }
    }
    ImGui::End();

    ImGui::Begin("Экспорт");
    ImGui::InputText("Папка", st.export_dir, sizeof(st.export_dir));
    const bool can = st.world && !st.tmesh.vertices.empty();
    ImGui::BeginDisabled(!can);
    if (ImGui::Button("Экспорт GLTF")) do_export(st);
    ImGui::SameLine();
    if (ImGui::Button("Экспорт OBJ")) do_export_obj(st);
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::Checkbox("Ортографическая проекция", &st.cam.ortho);
    if (!st.status.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", st.status.c_str());
    }
    ImGui::End();
}

}

void show_opengl_help() {
#ifdef _WIN32
    const wchar_t* msg =
        L"Dine \u043d\u0435 \u0441\u043c\u043e\u0433 \u0441\u043e\u0437\u0434\u0430\u0442\u044c OpenGL-\u043a\u043e\u043d\u0442\u0435\u043a\u0441\u0442 (\u043d\u0443\u0436\u0435\u043d OpenGL 3.3+).\n\n"
        L"\u041e\u0431\u044b\u0447\u043d\u043e \u044d\u0442\u043e \u0437\u043d\u0430\u0447\u0438\u0442, \u0447\u0442\u043e \u043d\u0435 \u0443\u0441\u0442\u0430\u043d\u043e\u0432\u043b\u0435\u043d \u0432\u0438\u0434\u0435\u043e\u0434\u0440\u0430\u0439\u0432\u0435\u0440.\n\n"
        L"\u041e\u0442\u043a\u0440\u044b\u0442\u044c \u0441\u0442\u0440\u0430\u043d\u0438\u0446\u0443 \u0437\u0430\u0433\u0440\u0443\u0437\u043a\u0438 \u0434\u0440\u0430\u0439\u0432\u0435\u0440\u0430 \u0441\u0435\u0439\u0447\u0430\u0441?\n"
        L"(NVIDIA/AMD/Intel \u2014 \u0432\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u0441\u0432\u043e\u044e \u0432\u0438\u0434\u0435\u043e\u043a\u0430\u0440\u0442\u0443;\n"
        L"\u0434\u043b\u044f \u0432\u0438\u0440\u0442\u0443\u0430\u043b\u043a\u0438/\u0441\u0442\u0430\u0440\u043e\u0433\u043e GPU \u2014 Mesa3D for Windows)";
    if (MessageBoxW(nullptr, msg, L"Dine \u2014 \u043d\u0443\u0436\u0435\u043d OpenGL",
                    MB_YESNO | MB_ICONWARNING) == IDYES) {

        ShellExecuteW(nullptr, L"open",
                      L"https://www.google.com/search?q=download+opengl+graphics+driver",
                      nullptr, nullptr, SW_SHOWNORMAL);
        ShellExecuteW(nullptr, L"open",
                      L"https://github.com/pal1000/mesa-dist-win/releases",
                      nullptr, nullptr, SW_SHOWNORMAL);
    }
#else
    std::fprintf(stderr,
        "\n=== Dine: OpenGL 3.3+ \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u0435\u043d ===\n"
        "\u0423\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u0435 Mesa/\u0434\u0440\u0430\u0439\u0432\u0435\u0440:\n"
        "  CachyOS/Arch: sudo pacman -S mesa            (Intel/AMD)\n"
        "                sudo pacman -S nvidia-utils     (NVIDIA)\n"
        "  Debian/Ubuntu: sudo apt install mesa-utils libgl1-mesa-dri\n"
        "\u041f\u0440\u043e\u0432\u0435\u0440\u043a\u0430: glxinfo -B | grep OpenGL\n\n");
#endif
}

int main(int argc, char** argv) {
    AppState st;

    std::filesystem::path world_path, mods_path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--mods" && i + 1 < argc) mods_path = argv[++i];
        else world_path = a;
    }

    glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
#define VW_BUILD_TAG "1.0.0"

    bool gl33 = false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    GLFWwindow* win = glfwCreateWindow(1600, 950,
        "Dine " VW_BUILD_TAG " — by rosey", nullptr, nullptr);
    if (!win) {
        gl33 = true;
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        win = glfwCreateWindow(1600, 950,
            "Dine " VW_BUILD_TAG " — by rosey (OpenGL 3.3)", nullptr, nullptr);
    }
    if (!win) {

        show_opengl_help();
        glfwTerminate();
        return 1;
    }
    std::fprintf(stderr, "Dine %s (by rosey)%s\n", VW_BUILD_TAG,
                 gl33 ? " [OpenGL 3.3 fallback]" : "");
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    {
        int iw = 0, ih = 0;
        if (stbi_uc* px = stbi_load_from_memory(kAppIconPng, static_cast<int>(kAppIconPngSize),
                                                &iw, &ih, nullptr, 4)) {
            GLFWimage gi{iw, ih, px};
            glfwSetWindowIcon(win, 1, &gi);
            stbi_image_free(px);
        }
    }

    if (!vw_gl_load(reinterpret_cast<void* (*)(const char*)>(glfwGetProcAddress))) {
        std::fprintf(stderr, "failed to load GL functions\n");
        return 1;
    }
    glEnable(GL_MULTISAMPLE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    setup_fonts(io);
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init(gl33 ? "#version 330" : "#version 450");

    if (!st.renderer.init(gl33)) {
        std::fprintf(stderr, "renderer init failed\n");
        show_opengl_help();
        return 1;
    }

    st.assets.set_png_decoder(stb_decode_png);
    st.models = std::make_unique<mods::ModelResolver>(st.assets);
    if (auto vj = find_vanilla_jar(); !vj.empty()) {
        try {
            st.assets.scan_jar(vj);
            st.vanilla_loaded = true;
            st.vanilla_jar_path = vj;
            st.vanilla_info = "Ваниль: " + plat::u8str(vj);
        } catch (const std::exception& e) {
            st.vanilla_info = std::string("client.jar не прочитался: ") + e.what();
        }
    }

    if (!mods_path.empty()) {
        st.mods_dir = mods_path;
        st.assets.scan_mods_dir(mods_path, st.pool);
    }
    if (!world_path.empty())
        load_world(st, world_path);

    double lx = 0, ly = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        if (st.building && st.build_future.valid() &&
            st.build_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                BuildResult r = st.build_future.get();
                st.tmesh = std::move(r.tmesh);
                st.renders = std::move(r.renders);
                st.gpu_atlas = r.atlas;
                if (r.atlas) st.renderer.upload(st.tmesh, *r.atlas);
                const size_t tris =
                    (st.tmesh.indices.size() + st.tmesh.indices_alpha.size()) / 3;
                if (r.chunk_count == 0) {
                    st.status = "В области нет сгенерированных чанков — "
                                "сдвиньте X/Z туда, где вы бывали в игре.";
                } else if (tris == 0) {
                    st.status = "Чанков: " + std::to_string(r.chunk_count) +
                                ", но меш пуст — проверьте диапазон Y "
                                "(поверхность обычно Y 60..120).";
                } else {
                    st.status = "Готово: " + std::to_string(tris) +
                                " трис, чанков: " + std::to_string(r.chunk_count) +
                                ", текстур в атласе: " + std::to_string(r.textured);
                }
                if (r.palette_overflow)
                    st.status += "  ВНИМАНИЕ: превышен предел палитры "
                                 "(65534 состояний блоков) — часть чанков "
                                 "пропущена, уменьшите область.";
                if (st.follow_area && tris != 0) {

                    st.cam.target[0] = 0.5f * (r.box.x0 + r.box.x1);
                    st.cam.target[1] = 0.5f * (r.box.y0 + r.box.y1);
                    st.cam.target[2] = 0.5f * (r.box.z0 + r.box.z1);
                    const f32 span = std::max({(f32)r.box.sx(), (f32)r.box.sy(),
                                               (f32)r.box.sz()});
                    st.cam.dist = std::max(st.cam.dist, span * 1.2f);
                }
            } catch (const std::exception& e) {
                st.status = std::string("Ошибка построения: ") + e.what();
            }
            st.building = false;
        }

        if (st.map.building && st.map.future.valid() &&
            st.map.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                st.map.img = st.map.future.get();
                if (!st.map.img.empty()) {
                    if (!st.map.tex) glGenTextures(1, &st.map.tex);
                    glBindTexture(GL_TEXTURE_2D, st.map.tex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, st.map.img.w, st.map.img.h,
                                 0, GL_RGBA, GL_UNSIGNED_BYTE, st.map.img.rgba.data());
                    glBindTexture(GL_TEXTURE_2D, 0);
                    st.status = "2D-карта: " + std::to_string(st.map.img.w) + "x" +
                                std::to_string(st.map.img.h);
                }
            } catch (const std::exception& e) {
                st.status = std::string("Ошибка карты: ") + e.what();
            }
            st.map.building = false;
        }

        if (st.view_mode == 1 && st.world && st.map.stale && !st.map.building &&
            !st.building && !st.scanning)
            start_map_build(st);

        if (st.world && !st.vanilla_loaded && !st.fetching_vanilla &&
            !st.vanilla_future.valid() && st.pending_vanilla_jar.empty()) {
            st.fetching_vanilla = true;
            st.vanilla_fetch_gen = st.world_gen;
            st.vanilla_fetch_ver = st.world->info().version_name;
            st.vanilla_info = "Скачивание ванильных текстур с mojang.com...";
            const std::string ver = st.vanilla_fetch_ver;
            const uint64_t fetch_gen = st.vanilla_fetch_gen;
            st.vanilla_future = std::async(std::launch::async, [ver, fetch_gen] {
                std::string err;
                auto p = app::download_vanilla_jar(ver, err);
                (void)fetch_gen;
                return std::make_pair(p, err);
            });
        }
        if (st.fetching_vanilla && st.vanilla_future.valid() &&
            st.vanilla_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto [jar, err] = st.vanilla_future.get();
            if (st.world_gen != st.vanilla_fetch_gen) {
                st.fetching_vanilla = false;  // мир сменился — результат выбрасываем
            } else {
                st.fetching_vanilla = false;
                if (!jar.empty()) {
                    // Применим, когда закончится построение/сканирование.
                    st.pending_vanilla_jar = jar;
                } else {
                    st.vanilla_info = "Текстуры Mojang не скачались: " + err +
                                      " (используются цвета)";
                }
            }
        }

        // Применяем скачанный client.jar, как только приложение освободилось.
        if (!st.pending_vanilla_jar.empty() && assets_idle(st)) {
            const auto jar = st.pending_vanilla_jar;
            st.pending_vanilla_jar.clear();
            try {
                rebuild_asset_registry(st, jar);
                st.vanilla_info = "Ваниль (Mojang): " + plat::u8str(jar.filename());
                st.dirty = true;
                st.last_edit = ImGui::GetTime() - 1.0;
                st.map.stale = true;
            } catch (const std::exception& e) {
                st.vanilla_info = std::string("client.jar не прочитался: ") + e.what();
            }
        }

        if (st.dirty && !st.building && st.world &&
            ImGui::GetTime() - st.last_edit > 0.35) {
            st.dirty = false;
            start_rebuild(st);
        }

        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        const f32 dx = static_cast<f32>(mx - lx), dy = static_cast<f32>(my - ly);
        lx = mx; ly = my;
        if (!io.WantCaptureMouse && st.view_mode == 0) {
            const bool lmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            const bool mmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
            const bool rmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            const bool shift = glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                               glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            if (rmb)                       st.cam.look(dx, dy);
            else if (mmb || (lmb && shift)) st.cam.pan(-dx, dy);
            else if (lmb)                  st.cam.orbit(dx, dy);
            if (io.MouseWheel != 0)        st.cam.zoom(io.MouseWheel);
        }

        if (st.view_mode == 0 && !io.WantTextInput) {
            auto held = [&](int k) { return glfwGetKey(win, k) == GLFW_PRESS; };
            const f32 fwd   = f32(held(GLFW_KEY_W)) - f32(held(GLFW_KEY_S));
            const f32 right = f32(held(GLFW_KEY_D)) - f32(held(GLFW_KEY_A));
            const f32 up    = f32(held(GLFW_KEY_SPACE) || held(GLFW_KEY_E)) -
                              f32(held(GLFW_KEY_Q) || held(GLFW_KEY_C));
            if (fwd != 0.0f || right != 0.0f || up != 0.0f) {
                const bool shift = held(GLFW_KEY_LEFT_SHIFT) || held(GLFW_KEY_RIGHT_SHIFT);

                const f32 speed = std::max(st.cam.dist * 0.6f, 8.0f) *
                                  (shift ? 3.0f : 1.0f);
                st.cam.fly(fwd, right, up, speed * io.DeltaTime);
            }
        }

        int fbw, fbh;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const f32 aspect = fbh ? static_cast<f32>(fbw) / fbh : 1.0f;
        const auto vp = app::mat_mul(st.cam.proj(aspect), st.cam.view());
        if (st.view_mode == 0)
            st.renderer.draw(vp);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ui_frame(st);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(win);
    }

    st.renderer.destroy();
    // Обнуляем после удаления: иначе любая поздняя проверка
    // `if (st.map.tex)` увидит висячий ID и сочтёт текстуру живой.
    if (st.map.tex) { glDeleteTextures(1, &st.map.tex); st.map.tex = 0; }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}

