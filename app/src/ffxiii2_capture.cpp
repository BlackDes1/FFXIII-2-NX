// Test captures: at the seconds listed in ffxiii2_shots (e.g. "10,20,40"), the presented guest frame is written to
// captures/shot_<s>s.bmp next to the exe. Works while the window is minimized (boot_test.ps1 -Shots), so a test run
// never takes over the desktop. The SDK presenter's CaptureGuestOutput is called from this background thread.
#include "ffxiii2_capture.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>

REXCVAR_DEFINE_STRING(ffxiii2_shots, "", "FFXIII2",
                      "Seconds after start at which to save the game's frame to captures/shot_<s>s.bmp, e.g. \"10,20,40\"");

namespace ffxiii2::capture {
namespace {
bool WriteBmp(const std::filesystem::path& path, const rex::ui::RawImage& img) {
  const uint32_t w = img.width, h = img.height, row = (w * 3 + 3) & ~3u, size = 54 + row * h;
  std::vector<uint8_t> out(size, 0);
  auto put32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) out[o + i] = uint8_t(v >> (8 * i)); };
  out[0] = 'B'; out[1] = 'M'; put32(2, size); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
  out[26] = 1; out[28] = 24; put32(34, row * h);
  for (uint32_t y = 0; y < h; ++y) {               // BMP rows go bottom-up, pixels as B G R
    const uint8_t* src = img.data.data() + size_t(y) * img.stride;
    uint8_t* dst = out.data() + 54 + size_t(h - 1 - y) * row;
    for (uint32_t x = 0; x < w; ++x) { dst[3 * x] = src[4 * x + 2]; dst[3 * x + 1] = src[4 * x + 1]; dst[3 * x + 2] = src[4 * x]; }
  }
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
  return bool(f);
}
}  // namespace

void Start(std::function<rex::ui::Presenter*()> get_presenter) {
  std::vector<int> seconds;
  std::stringstream ss(REXCVAR_GET(ffxiii2_shots));
  for (std::string t; std::getline(ss, t, ',');) if (!t.empty()) seconds.push_back(std::stoi(t));
  if (seconds.empty()) return;
  std::thread([seconds, get_presenter = std::move(get_presenter)] {
    const auto start = std::chrono::steady_clock::now();
    const auto dir = rex::filesystem::GetExecutableFolder() / "captures";
    std::filesystem::create_directories(dir);
    for (int s : seconds) {
      std::this_thread::sleep_until(start + std::chrono::seconds(s));
      rex::ui::Presenter* p = get_presenter();
      rex::ui::RawImage img;
      if (!p || !p->CaptureGuestOutput(img) || !img.width) { REXLOG_WARN("[capture] {} s: no guest frame", s); continue; }
      const auto path = dir / ("shot_" + std::to_string(s) + "s.bmp");
      REXLOG_INFO("[capture] {} s: {}x{} -> {} ({})", s, img.width, img.height, path.string(), WriteBmp(path, img) ? "ok" : "write failed");
    }
  }).detach();
}
}  // namespace ffxiii2::capture
