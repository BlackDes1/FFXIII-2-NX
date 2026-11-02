#pragma once
namespace ffxiii2::profile {
// Starts the sampling profiler thread when ffxiii2_profile_from_s > 0 (Windows only; see ffxiii2_profile.cpp).
void Start();
}
