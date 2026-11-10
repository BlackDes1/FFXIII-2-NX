/**
 * @file        rex/ui/app_menu.h
 * @brief       FFXIII-2-Native-Switch (2026-11-10): the app's own options menu and the controller
 *
 * The app's options menu (app/src/renut_engine/overlays/options_menu_dialog.h) is an ImGui dialog
 * of the app, not one of the SDK's overlays, so the SDK's input gate (ReXApp's active callback,
 * rex_app.cpp) does not know about it. While it is open it sets this flag and the game gets no
 * controller input (the menu reads the pad through ImGui). Start + Select (PLUS + MINUS) opens and
 * closes it on the Switch (switch_input_driver.cpp sends F8, the menu's key on the PC).
 *
 * @license     BSD 3-Clause License
 */
#pragma once

#include <atomic>

namespace rex::ui {

inline std::atomic<bool>& AppMenuFlag() {
  static std::atomic<bool> open{false};
  return open;
}

// Whether the app's options menu is open (the game gets no controller input meanwhile).
inline bool AppMenuOpen() { return AppMenuFlag().load(std::memory_order_acquire); }
inline void SetAppMenuOpen(bool open) { AppMenuFlag().store(open, std::memory_order_release); }

}  // namespace rex::ui
