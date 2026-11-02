#pragma once
#include <functional>
namespace rex::ui { class Presenter; }
namespace ffxiii2::capture {
// Starts the test-capture thread when the cvar ffxiii2_shots lists seconds (see ffxiii2_capture.cpp).
void Start(std::function<rex::ui::Presenter*()> get_presenter);
}
