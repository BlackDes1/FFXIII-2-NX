// FFXIII-2-Native-Switch (2026-11-10, the user: "Make a Menu which can be open with start and select. The user can
// toggle our changes ..."; renut-nx as the reference: its overlays are ImGui dialogs of the app like this one).
//
// Start + Select (PLUS + MINUS) on the Switch, F8 on the PC, opens and closes it. While it is open the game gets no
// controller input (rex/ui/app_menu.h); the D-pad moves, A changes a line (a list opens), B goes back. Every line is a
// key of the port: lines marked "(restart)" are read when the game starts and take effect at the next start; the others
// change at once. "Save" writes the lines' keys into the player's settings file (ffxiii2.toml next to the NRO / exe),
// every other line of that file stays as it is; without saving, a change lasts until the game closes.
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/ui/app_menu.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/keybinds.h>

#include "imgui.h"
#include "renut_engine/Fps.h"

class OptionsMenuDialog : public rex::ui::ImGuiDialog {
 public:
  OptionsMenuDialog(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path, FPSManager* fps)
      : rex::ui::ImGuiDialog(drawer), config_path_(std::move(config_path)), fps_(fps) {
    BuildItems();
    rex::ui::RegisterBind("bind_options_menu", "F8", "Options menu (Start + Select on the controller)",
                          [this] { Toggle(); });
  }
  ~OptionsMenuDialog() override {
    rex::ui::UnregisterBind("bind_options_menu");
    rex::ui::SetAppMenuOpen(false);
  }

  void Toggle() {
    visible_ = !visible_;
    if (visible_) {
      just_opened_ = true;
      ReadCurrent();
      message_.clear();
    }
    rex::ui::SetAppMenuOpen(visible_);
  }

  void OnDraw(ImGuiIO& io) override {
    if (!visible_) return;
    const float w = std::min(io.DisplaySize.x - 40.0f, 900.0f);
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    if (just_opened_) ImGui::SetNextWindowFocus();
    bool open = true;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    if (ImGui::Begin("FINAL FANTASY XIII-2  -  Port options###ffns_options", &open, flags)) {
      ImGui::SetWindowFontScale(1.4f);
      const char* section = nullptr;
      int first = -1;
      for (size_t i = 0; i < items_.size(); ++i) {
        Item& item = items_[i];
        if (!section || std::string(section) != item.section) {
          section = item.section;
          ImGui::SeparatorText(section);
        }
        ImGui::PushID(int(i));
        ImGui::AlignTextToFramePadding();
        if (!item.available) ImGui::BeginDisabled();
        ImGui::TextUnformatted(item.label);
        if (item.restart) {
          ImGui::SameLine();
          ImGui::TextDisabled("(restart)");
        }
        ImGui::SameLine(w * 0.45f);
        ImGui::SetNextItemWidth(w * 0.5f);
        const char* shown = item.available ? item.choices[item.pending].label : item.unavailable;
        if (ImGui::BeginCombo("##v", shown)) {
          for (size_t c = 0; c < item.choices.size(); ++c) {
            const bool selected = int(c) == item.pending;
            if (ImGui::Selectable(item.choices[c].label, selected)) Choose(item, int(c));
            if (selected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
        if (first < 0 && item.available) first = int(i);
        if (!item.available) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || ImGui::IsItemFocused()) hint_ = item.help;
        ImGui::PopID();
      }
      ImGui::Separator();
      ImGui::TextWrapped("%s", hint_ ? hint_ : "");
      ImGui::Separator();
      if (ImGui::Button("Save")) Save();
      ImGui::SameLine();
      if (ImGui::Button("Close")) open = false;
      if (!message_.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted(message_.c_str());
      }
      ImGui::TextDisabled("Start + Select: open / close    A: change    B: back    (PC: F8)");
    }
    ImGui::End();
    just_opened_ = false;
    if (!open) Toggle();
  }

 private:
  struct Choice {
    const char* label;
    std::vector<std::pair<const char*, std::string>> sets;  // key -> value
  };
  struct Item {
    const char* section;
    const char* label;
    const char* help;
    bool restart;
    bool available;
    const char* unavailable;
    std::vector<Choice> choices;
    int pending = 0;
    bool read = false;  // chosen in this session: kept as chosen (a restart line's key keeps its running value)
  };

  void BuildItems() {
    auto add = [this](const char* section, const char* label, const char* help, bool restart,
                      std::vector<Choice> choices) {
      items_.push_back(Item{section, label, help, restart, true, "", std::move(choices)});
    };
    auto later = [this](const char* section, const char* label, const char* help, const char* why) {
      items_.push_back(Item{section, label, help, false, false, why, {Choice{why, {}}}});
    };
    add("Graphics", "Render resolution",
        "The 3D image's height on the Switch (the HUD stays at 720p). Lower = less GPU time, a softer picture. "
        "Fixed for the whole session: changing it while playing broke the battle picture once.",
        true,
        {Choice{"720p (native)", {{"banjo_renut_dynamic_resolution", "false"}}},
         Choice{"648p", {{"banjo_renut_dynamic_resolution", "true"}, {"ffns_dynres_fixed_height", "648"}}},
         Choice{"612p (default)", {{"banjo_renut_dynamic_resolution", "true"}, {"ffns_dynres_fixed_height", "612"}}},
         Choice{"540p", {{"banjo_renut_dynamic_resolution", "true"}, {"ffns_dynres_fixed_height", "540"}}}});
    add("Graphics", "Shadows",
        "How often the large (far) shadow map is redrawn. Every frame = the original; every 2nd frame (default) "
        "saves about 1.3 ms a frame; far shadows then trail moving objects by a frame.",
        false,
        {Choice{"Every frame (original)", {{"ffns_shadow_far_every", "1"}}},
         Choice{"Far map every 2nd frame", {{"ffns_shadow_far_every", "2"}}},
         Choice{"Far map every 4th frame", {{"ffns_shadow_far_every", "4"}}}});
    add("Graphics", "Draw distance",
        "Objects placed in the scene (people, props) farther from the camera than this are not drawn. The town and "
        "landscape themselves always are. Full = the original.",
        false,
        {Choice{"Full (original)", {{"ffns_draw_distance", "0"}}},
         Choice{"150 m", {{"ffns_draw_distance", "150"}}},
         Choice{"100 m", {{"ffns_draw_distance", "100"}}},
         Choice{"60 m", {{"ffns_draw_distance", "60"}}}});
    later("Graphics", "Animate distant characters", "Distant characters animated at half rate.",
          "not available yet");
    add("Graphics", "Level of detail (textures)",
        "Distant textures switch to their smaller versions sooner: less GPU memory traffic, blurrier distance.",
        true,
        {Choice{"Original", {{"ffns_texture_lod_bias", "0"}}}, Choice{"Lower (+0.5)", {{"ffns_texture_lod_bias", "2"}}},
         Choice{"Lowest (+1.0)", {{"ffns_texture_lod_bias", "4"}}}});
    later("Graphics", "Level of detail (models)", "Lower-detail models at a distance.", "not available yet");
    add("Graphics", "MSAA",
        "The game's own 2x multisampling of the 3D image. Off: single-sampled targets (rougher edges, less GPU "
        "time and memory).",
        true, {Choice{"2x (original)", {{"msaa_level", "2"}}}, Choice{"Off", {{"msaa_level", "1"}}}});
    add("Graphics", "SMAA",
        "Edge smoothing (SMAA 1x) of the final 3D image, before the HUD is drawn: smoother edges for a little GPU "
        "time.",
        false, {Choice{"Off", {{"banjo_renut_smaa", "false"}}}, Choice{"On", {{"banjo_renut_smaa", "true"}}}});
    add("Graphics", "Upscaling",
        "How the picture is scaled up to the screen when the render resolution is below 720p. Sharpen: the usual "
        "filter, then AMD FidelityFX RCAS sharpening (about 1 ms of GPU). FSR 1: AMD FidelityFX edge-adaptive "
        "upscaling and sharpening, the crispest (about 6 ms of GPU). No effect at 720p.",
        false,
        {Choice{"Bilinear", {{"ffns_fsr", "0"}}},
         Choice{"Bilinear + sharpen", {{"ffns_fsr", "1"}, {"ffns_fsr_sharpness", "50"}}},
         Choice{"FSR 1", {{"ffns_fsr", "2"}, {"ffns_fsr_sharpness", "50"}}},
         Choice{"FSR 1 sharp", {{"ffns_fsr", "2"}, {"ffns_fsr_sharpness", "20"}}}});
    add("Graphics", "Text and HUD",
        "With a render resolution below 720p: text, HUD and menus are drawn at the screen's full 720p over the 3D "
        "picture (sharp letters; about 1 ms of GPU). Off: everything at the render resolution. Only with Upscaling "
        "set to Bilinear.",
        false, {Choice{"720p (default)", {{"ffns_ui_native", "true"}}}, Choice{"Render resolution", {{"ffns_ui_native", "false"}}}});
    // 2026-11-12 (the user: "remove the fps counter"): no "FPS counter" line; the counter stays off.
    add("System", "CPU clock",
        "Overclock: the game raises the CPU clock itself (no sys-clk needed). Faster, but more battery use and heat. "
        "The console's own clock comes back when the game closes.",
        false,
        {Choice{"1020 MHz (stock)", {{"ffns_cpu_mhz", "0"}}}, Choice{"1224 MHz", {{"ffns_cpu_mhz", "1224"}}},
         Choice{"1428 MHz", {{"ffns_cpu_mhz", "1428"}}}, Choice{"1581 MHz", {{"ffns_cpu_mhz", "1581"}}}});
    add("System", "GPU clock",
        "Overclock: the game raises the GPU clock itself (no sys-clk needed). Fewer frame drops in heavy scenes, but "
        "more battery use and heat. The console's own clock comes back when the game closes.",
        false, {Choice{"460 MHz (stock)", {{"ffns_gpu_mhz", "0"}}}, Choice{"537 MHz", {{"ffns_gpu_mhz", "537"}}}});
    add("Game", "Language",
        "The game's language (text and menus) of the European disc.", true,
        {Choice{"English", {{"user_language", "1"}}}, Choice{"French", {{"user_language", "4"}}},
         Choice{"German", {{"user_language", "3"}}}, Choice{"Spanish", {{"user_language", "5"}}},
         Choice{"Italian", {{"user_language", "6"}}}});
    add("Game", "DLC",
        "The downloadable content in the dlc folder next to the game (installed at the first start). Off: the game "
        "sees none; nothing is deleted.",
        true, {Choice{"On", {{"dlc_enabled", "true"}}}, Choice{"Off", {{"dlc_enabled", "false"}}}});
    add("Controls", "Button layout",
        "Xbox positions: the bottom face button (B) confirms, as on the Xbox 360. By letter: A confirms.", false,
        {Choice{"Xbox positions", {{"input_xbox_layout", "true"}}},
         Choice{"By letter", {{"input_xbox_layout", "false"}}}});
    add("Controls", "Rumble", "Controller vibration.", false,
        {Choice{"On", {{"input_rumble", "true"}}}, Choice{"Off", {{"input_rumble", "false"}}}});
  }

  static std::string Current(const char* key) {
    std::string v = rex::cvar::GetFlagByName(key);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    return v;
  }

  // Each line's choice from the keys' values now (the first choice whose keys all match; else the first with the
  // first key's value).
  void ReadCurrent() {
    for (Item& item : items_) {
      if (!item.available || item.read) continue;
      int found = -1;
      for (size_t c = 0; c < item.choices.size() && found < 0; ++c) {
        bool all = !item.choices[c].sets.empty();
        for (const auto& [key, value] : item.choices[c].sets) all = all && Current(key) == value;
        if (all) found = int(c);
      }
      for (size_t c = 0; c < item.choices.size() && found < 0; ++c)
        if (!item.choices[c].sets.empty() && Current(item.choices[c].sets[0].first) == item.choices[c].sets[0].second)
          found = int(c);
      item.pending = found < 0 ? 0 : found;
    }
  }

  void Choose(Item& item, int choice) {
    item.pending = choice;
    item.read = true;  // keeps the choice for restart lines (their keys keep the running value)
    if (item.restart) {
      message_ = "Saved lines marked (restart) apply at the next start.";
      return;
    }
    for (const auto& [key, value] : item.choices[choice].sets) rex::cvar::SetFlagByName(key, value);
    if (fps_) fps_->showFPS = false;  // 2026-11-12: the FPS counter is removed
  }

  // The keys of every line into the player's file: an existing "key = ..." line is replaced, a missing one appended.
  void Save() {
    std::vector<std::string> lines;
    {
      std::ifstream in(config_path_);
      for (std::string line; std::getline(in, line);) lines.push_back(line);
    }
    bool header = false;
    for (const std::string& l : lines) header = header || l.find("# Options menu") != std::string::npos;
    for (const Item& item : items_) {
      if (!item.available) continue;
      for (const auto& [key, value] : item.choices[item.pending].sets) {
        const bool quoted = !value.empty() && value != "true" && value != "false" &&
                            value.find_first_not_of("-0123456789.") != std::string::npos;
        const std::string text = std::string(key) + " = " + (quoted ? "\"" + value + "\"" : value);
        bool replaced = false;
        for (std::string& l : lines) {
          size_t p = l.find_first_not_of(" \t");
          if (p == std::string::npos || l.compare(p, std::strlen(key), key) != 0) continue;
          size_t q = l.find_first_not_of(" \t", p + std::strlen(key));
          if (q == std::string::npos || l[q] != '=') continue;
          l = text;
          replaced = true;
        }
        if (!replaced) {
          if (!header) {
            lines.push_back("");
            lines.push_back("# Options menu (Start + Select): written by the game");
            header = true;
          }
          lines.push_back(text);
        }
      }
    }
    std::ofstream out(config_path_, std::ios::trunc);
    for (const std::string& l : lines) out << l << "\n";
    out.close();
    message_ = out ? "Saved." : "Could not write the settings file.";
  }

  std::filesystem::path config_path_;
  FPSManager* fps_ = nullptr;
  std::vector<Item> items_;
  bool visible_ = false;
  bool just_opened_ = false;
  const char* hint_ = nullptr;
  std::string message_;
};
