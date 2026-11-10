// banjo-nx: the on-screen profiler, ported from UnleashedRecomp's DrawProfiler() (gpu/video.cpp).
//
// Same layout: a "Frame Time" graph of every series over the last 256 UI frames, then the current and
// average value of each, then renderer/device facts. Unleashed plots with ImPlot; the SDK's ImGui
// (1.92) is newer than any ImPlot release Unleashed carries, so the graph is drawn with the window's
// draw list instead. F5 on PC (F1 is reNut's FPS overlay); banjo_show_profiler everywhere.
#pragma once

#include <algorithm>
#include <array>
#include <cstdio>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/keybinds.h>

#include "imgui.h"
#include "profiler/profiler.h"

REXCVAR_DECLARE(bool, banjo_show_profiler);
REXCVAR_DECLARE(bool, banjo_profiler_pad_toggle);           // ffxiii2-nx (2026-11-05): L3+R3 on the console
REXCVAR_DECLARE(std::string, banjo_profiler_pad_combo);

class ProfilerOverlayDialog : public rex::ui::ImGuiDialog {
 public:
  explicit ProfilerOverlayDialog(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {
    rex::ui::RegisterBind("bind_profiler", "F5", "Toggle CPU/GPU profiler", [] {
      rex::cvar::SetFlagByName("banjo_show_profiler", REXCVAR_GET(banjo_show_profiler) ? "false" : "true");
    });
  }
  ~ProfilerOverlayDialog() override { rex::ui::UnregisterBind("bind_profiler"); }

  void OnDraw(ImGuiIO& io) override {
    using banjo::profiler::Series;
    constexpr size_t kSeries = static_cast<size_t>(Series::kCount);
    constexpr size_t kHistory = 256;

    // Sample every series once per UI frame, as Unleashed's window did.
    for (size_t s = 0; s < kSeries; ++s) {
      const auto series = static_cast<Series>(s);
      history_[s][index_] = banjo::profiler::HasValue(series)
                                ? static_cast<float>(banjo::profiler::Current(series))
                                : 0.0f;
    }
    index_ = (index_ + 1) % kHistory;

    PadToggle();
    if (!REXCVAR_GET(banjo_show_profiler)) return;

    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 580.0f, 20.0f), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin("Profiler", &open)) {
      DrawGraph();

      ImGui::Separator();
      for (size_t s = 0; s < kSeries; ++s) {
        const auto series = static_cast<Series>(s);
        if (!banjo::profiler::HasValue(series)) {
          ImGui::TextDisabled("%-28s n/a", banjo::profiler::SeriesName(series));
          continue;
        }
        const double cur = banjo::profiler::Current(series);
        const double avg = Average(s);
        ImGui::TextColored(Color(s), "%-28s", banjo::profiler::SeriesName(series));
        ImGui::SameLine();
        if (series == Series::kLimiterWait || series == Series::kRenderThread) {
          ImGui::Text("%7.2f ms  (avg %7.2f ms)", cur, avg);
        } else {
          ImGui::Text("%7.2f ms  (avg %7.2f ms, %5.1f FPS)", cur, avg, avg > 0.0 ? 1000.0 / avg : 0.0);
        }
      }

      ImGui::Separator();
      ImGui::Text("Renderer: %s", banjo::profiler::RendererApi().c_str());
      const std::string device = banjo::profiler::RendererDevice();
      if (!device.empty()) ImGui::Text("Device: %s", device.c_str());

      // CPU use is measured since the previous query, so ask once a second (Unleashed: once per report).
      if (io.DeltaTime > 0.0f) threads_age_ += io.DeltaTime;
      if (threads_age_ >= 1.0f) {
        threads_age_ = 0.0f;
        const std::string threads = banjo::profiler::ThreadCpuUsage();
        if (!threads.empty()) last_threads_ = threads;
      }
      if (!last_threads_.empty() && ImGui::TreeNode("Thread CPU use")) {
        ImGui::TextWrapped("%s", last_threads_.c_str());
        ImGui::TreePop();
      }
      DrawCounters(io);
    }
    ImGui::End();
    if (!open) rex::cvar::SetFlagByName("banjo_show_profiler", "false");
  }

 private:
  // ffxiii2-nx (2026-11-05): banjo_profiler_pad_combo held (all of its buttons down, edge-triggered) toggles the window
  // when banjo_profiler_pad_toggle is on. ImGui gets the pad from switch_imgui_input.h on the console.
  void PadToggle() {
    if (!REXCVAR_GET(banjo_profiler_pad_toggle)) return;
    const std::string combo = REXCVAR_GET(banjo_profiler_pad_combo);
    if (combo != combo_text_) {
      combo_text_ = combo;
      combo_keys_.clear();
      size_t start = 0;
      while (start <= combo.size()) {
        size_t end = combo.find('+', start);
        if (end == std::string::npos) end = combo.size();
        std::string name = combo.substr(start, end - start);
        for (auto &ch : name) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        static const std::pair<const char *, ImGuiKey> kNames[] = {
            {"L3", ImGuiKey_GamepadL3},          {"R3", ImGuiKey_GamepadR3},           {"L", ImGuiKey_GamepadL1},
            {"R", ImGuiKey_GamepadR1},           {"ZL", ImGuiKey_GamepadL2},           {"ZR", ImGuiKey_GamepadR2},
            {"PLUS", ImGuiKey_GamepadStart},     {"MINUS", ImGuiKey_GamepadBack},      {"UP", ImGuiKey_GamepadDpadUp},
            {"DOWN", ImGuiKey_GamepadDpadDown},  {"LEFT", ImGuiKey_GamepadDpadLeft},   {"RIGHT", ImGuiKey_GamepadDpadRight},
            {"A", ImGuiKey_GamepadFaceRight},    {"B", ImGuiKey_GamepadFaceDown},      {"X", ImGuiKey_GamepadFaceUp}};
        for (const auto &[n, key] : kNames)
          if (name == n) combo_keys_.push_back(key);
        start = end + 1;
      }
    }
    bool all = !combo_keys_.empty();
    for (ImGuiKey key : combo_keys_) all = all && ImGui::IsKeyDown(key);
    if (all && !combo_held_)
      rex::cvar::SetFlagByName("banjo_show_profiler", REXCVAR_GET(banjo_show_profiler) ? "false" : "true");
    combo_held_ = all;
  }

  // Counters, each marked "not measured" when this build has no source for it (nothing is estimated).
  void DrawCounters(ImGuiIO &io) {
    using banjo::profiler::Series;
    // ffxiii2-nx (2026-11-05): Renut's renderer has no Application series (n/a in the window); the frame is then the
    // Present series (the time between two presents), which the console showed at 37-40 ms in the battle.
    const Series frame_series =
        banjo::profiler::HasValue(Series::kApplication) ? Series::kApplication : Series::kPresent;
    const size_t app = static_cast<size_t>(frame_series);
    int over45 = 0, over100 = 0;
    for (float v : history_[app]) {
      over45 += v > 45.0f;
      over100 += v > 100.0f;
    }
    ImGui::Separator();
    ImGui::Text("Stutter: %d of the last %zu frames over 45 ms, %d over 100 ms", over45, kHistoryCount, over100);
    const banjo::profiler::ExtraCounters c = banjo::profiler::GetExtraCounters();
    if (c.memory_known)
      ImGui::Text("Memory: %llu / %llu MB used by the process", (unsigned long long)c.memory_used_mb,
                  (unsigned long long)c.memory_total_mb);
    else
      ImGui::TextDisabled("Memory: not measured on this platform");
    if (c.renderer_known) {
      if (io.DeltaTime > 0.0f) counters_age_ += io.DeltaTime;
      if (counters_age_ >= 1.0f || !counters_prev_.renderer_known) {
        const float dt = counters_prev_.renderer_known ? counters_age_ : 0.0f;
        if (dt > 0.0f) {
          draws_per_s_ = double(c.draws - counters_prev_.draws) / dt;
          compiles_per_s_ = double(c.shader_compiles - counters_prev_.shader_compiles) / dt;
          uploads_per_s_ = double(c.texture_uploads - counters_prev_.texture_uploads) / dt;
        }
        counters_prev_ = c;
        counters_age_ = 0.0f;
      }
      const double fps = banjo::profiler::HasValue(frame_series) && Average(app) > 0.0 ? 1000.0 / Average(app) : 0.0;
      ImGui::Text("Draw calls: %.0f/s (%.0f a frame)", draws_per_s_, fps > 0.0 ? draws_per_s_ / fps : 0.0);
      ImGui::Text("Shader compiles: %llu (%.1f/s now, avg %.0f ms, %llu failed); DKSH cache %llu hits, %llu added",
                  (unsigned long long)c.shader_compiles, compiles_per_s_, c.shader_compile_avg_ms,
                  (unsigned long long)c.shader_compile_failures, (unsigned long long)c.cache_hits,
                  (unsigned long long)c.cache_added);
      ImGui::Text("GPU memory: images %llu MB, buffers %llu MB; texture uploads %.0f/s",
                  (unsigned long long)c.image_mb, (unsigned long long)c.buffer_mb, uploads_per_s_);
    } else {
      ImGui::TextDisabled("Draw calls, shader compiles, GPU memory: not measured by this renderer");
    }
    // ffxiii2-nx (2026-11-07): dynamic resolution (banjo_renut_dynamic_resolution).
    if (c.dynamic_resolution_known) {
      const uint64_t all = c.dynamic_frames_at[0] + c.dynamic_frames_at[1] + c.dynamic_frames_at[2];
      const auto pct = [&](int i) { return all ? 100.0 * double(c.dynamic_frames_at[i]) / double(all) : 0.0; };
      ImGui::Text("Render height: %u of 720 (dynamic: GPU %.1f ms a frame, budget %.1f; %llu changes)",
                  c.render_height, c.dynamic_gpu_avg_ms, c.dynamic_budget_ms, (unsigned long long)c.dynamic_changes);
      ImGui::Text("Frames at %u / %u / %u lines: %.0f / %.0f / %.0f %%", c.dynamic_heights[0], c.dynamic_heights[1],
                  c.dynamic_heights[2], pct(0), pct(1), pct(2));
    } else {
      ImGui::TextDisabled("Render height: 720 (dynamic resolution off)");
    }
    ImGui::TextDisabled("Allocations, asset loading: not measured (see logs/rex/rex_profile.log)");
  }

  std::string combo_text_;
  std::vector<ImGuiKey> combo_keys_;
  bool combo_held_ = false;
  banjo::profiler::ExtraCounters counters_prev_{};
  float counters_age_ = 0.0f;
  double draws_per_s_ = 0.0, compiles_per_s_ = 0.0, uploads_per_s_ = 0.0;

  static constexpr size_t kSeriesCount = static_cast<size_t>(banjo::profiler::Series::kCount);
  static constexpr size_t kHistoryCount = 256;

  static ImVec4 Color(size_t s) {
    static const ImVec4 kColors[] = {
        ImVec4(0.95f, 0.95f, 0.95f, 1.0f),  // Application
        ImVec4(0.35f, 0.75f, 1.00f, 1.0f),  // Update
        ImVec4(1.00f, 0.65f, 0.25f, 1.0f),  // Draw
        ImVec4(0.55f, 0.55f, 0.55f, 1.0f),  // Limiter
        ImVec4(0.40f, 1.00f, 0.40f, 1.0f),  // GPU
        ImVec4(1.00f, 0.40f, 0.75f, 1.0f),  // Present
        ImVec4(1.00f, 0.90f, 0.30f, 1.0f),  // Render thread (Renut's renderer)
    };
    return kColors[s % (sizeof(kColors) / sizeof(kColors[0]))];
  }

  double Average(size_t s) const {
    double sum = 0.0;
    size_t n = 0;
    for (float v : history_[s]) {
      if (v > 0.0f) {
        sum += v;
        ++n;
      }
    }
    return n ? sum / double(n) : 0.0;
  }

  void DrawGraph() {
    ImGui::TextUnformatted("Frame Time (ms)");
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 160.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(20, 20, 24, 220));

    // Unleashed fixes the axis at 0-20 ms for 60 FPS; Banjo runs at 30, so grow to fit (at least 40 ms).
    float top = 40.0f;
    for (size_t s = 0; s < kSeriesCount; ++s)
      for (float v : history_[s]) top = std::max(top, std::min(v, 250.0f));
    for (float ms : {16.67f, 33.33f, 50.0f, 100.0f, 200.0f}) {
      if (ms >= top) break;
      const float y = p1.y - (ms / top) * size.y;
      dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), IM_COL32(80, 80, 90, 160));
      char label[16];
      std::snprintf(label, sizeof(label), "%.0f", ms);
      dl->AddText(ImVec2(p0.x + 2.0f, y - ImGui::GetTextLineHeight()), IM_COL32(150, 150, 160, 255), label);
    }

    for (size_t s = 0; s < kSeriesCount; ++s) {
      if (!banjo::profiler::HasValue(static_cast<banjo::profiler::Series>(s))) continue;
      const ImU32 col = ImGui::ColorConvertFloat4ToU32(Color(s));
      ImVec2 prev{};
      for (size_t i = 0; i < kHistoryCount; ++i) {
        const float v = std::min(history_[s][(index_ + i) % kHistoryCount], top);
        const ImVec2 pt(p0.x + size.x * float(i) / float(kHistoryCount - 1), p1.y - (v / top) * size.y);
        if (i) dl->AddLine(prev, pt, col, 1.5f);
        prev = pt;
      }
    }
    ImGui::Dummy(size);
  }

  std::array<std::array<float, kHistoryCount>, kSeriesCount> history_{};
  size_t index_ = 0;
  std::string last_threads_;
  float threads_age_ = 0.0f;
};
