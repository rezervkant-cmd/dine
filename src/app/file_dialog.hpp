#pragma once
#include <imgui.h>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include "core/platform.hpp"

namespace vw::app {

class DirPicker {
public:

    void open(const std::string& title, const std::filesystem::path& start) {
        try {
            title_ = title;
            std::error_code ec;
            cur_ = std::filesystem::exists(start, ec) ? start : plat::home_dir();
            cur_ = std::filesystem::weakly_canonical(cur_, ec);
            if (ec || cur_.empty()) cur_ = plat::home_dir();
            show_ = true;
            std::snprintf(path_input_, sizeof(path_input_), "%s", plat::u8str(cur_).c_str());
            refresh();
        } catch (...) {
            cur_.clear();
            try { cur_ = plat::home_dir(); } catch (...) {}
            show_ = true;
            try { refresh(); } catch (...) { dirs_.clear(); selected_idx_ = -1; }
        }
    }

    bool draw() {
        if (!show_) return false;
        bool picked = false;
        ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title_.c_str(), &show_)) {

            // Вставь путь мира; Enter откроет его напрямую.
            const bool submit_path = ImGui::InputText("Путь", path_input_, sizeof(path_input_),
                                                       ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Открыть путь")) {
                const std::filesystem::path p = plat::path_from_u8(path_input_);
                std::error_code ec;
                if (std::filesystem::exists(p / "level.dat", ec) && !ec) {
                    result_ = p;
                    picked = true;
                    show_ = false;
                } else {
                    navigate(p);
                }
            }
            if (submit_path && show_) {
                const std::filesystem::path p = plat::path_from_u8(path_input_);
                std::error_code ec;
                if (std::filesystem::exists(p / "level.dat", ec) && !ec) {
                    result_ = p;
                    picked = true;
                    show_ = false;
                } else {
                    navigate(p);
                }
            }

            if (ImGui::Button("Домой")) { navigate(plat::home_dir()); }
            ImGui::SameLine();
            if (ImGui::Button("Вверх") && cur_.has_parent_path()) navigate(cur_.parent_path());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", u8cur_.c_str());

            ImGui::Separator();
            ImGui::BeginChild("##dirs", ImVec2(0, -74), ImGuiChildFlags_Borders);
            for (size_t i = 0; i < dirs_.size(); ++i) {
                const auto& e = dirs_[i];
                std::string label = std::string(e.is_world ? "[МИР] " : "[D] ") + e.u8name;
                if (e.is_world)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));

                label += "##" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), selected_idx_ == (int)i,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) navigate(e.path);
                    else selected_idx_ = (int)i;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", e.u8full.c_str());
                if (e.is_world) ImGui::PopStyleColor();
            }
            if (dirs_.empty())
                ImGui::TextDisabled("(пусто или нет доступа)");
            ImGui::EndChild();

            const bool has_sel = selected_idx_ >= 0 && selected_idx_ < (int)dirs_.size();
            ImGui::Text("Выбрано: %s", has_sel ? dirs_[selected_idx_].u8name.c_str()
                                               : "(текущая папка)");
            if (ImGui::Button("Выбрать эту папку", ImVec2(180, 0))) {
                result_ = has_sel ? dirs_[selected_idx_].path : cur_;
                picked = true;
                show_ = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Отмена", ImVec2(100, 0))) show_ = false;
        }
        ImGui::End();
        return picked;
    }

    const std::filesystem::path& result() const { return result_; }
    bool is_open() const { return show_; }

private:
    struct Entry {
        std::filesystem::path path;
        std::string u8name;
        std::string u8full;
        bool is_world = false;
    };

    void navigate(const std::filesystem::path& p) {
        try {
            std::error_code ec;
            if (!std::filesystem::is_directory(p, ec) || ec) return;
            auto canon = std::filesystem::weakly_canonical(p, ec);
            cur_ = (ec || canon.empty()) ? p : canon;
            std::snprintf(path_input_, sizeof(path_input_), "%s", plat::u8str(cur_).c_str());
            refresh();
        } catch (...) {}
    }
    void refresh() {
        dirs_.clear();
        selected_idx_ = -1;
        try { u8cur_ = plat::u8str(cur_); } catch (...) { u8cur_.clear(); }
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(cur_, ec);
             it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (ec) break;
            try {
                std::error_code ec2;
                if (!it->is_directory(ec2) || ec2) continue;
                Entry e;
                e.path = it->path();
                e.u8name = plat::u8str(e.path.filename());
                if (e.u8name.empty() || e.u8name[0] == '.') continue;
                e.is_world = std::filesystem::exists(e.path / "level.dat", ec2) && !ec2;
                e.u8full = plat::u8str(e.path);
                dirs_.push_back(std::move(e));
            } catch (...) { continue; }
        }
        std::sort(dirs_.begin(), dirs_.end(),
                  [](const Entry& a, const Entry& b) { return a.u8name < b.u8name; });
    }

    bool show_ = false;
    std::string title_;
    std::filesystem::path cur_, result_;
    char path_input_[2048]{};
    std::string u8cur_;
    std::vector<Entry> dirs_;
    int selected_idx_ = -1;
};

}
