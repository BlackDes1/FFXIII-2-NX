// See src/ffxiii2_switch_cvars.cpp.
#pragma once

namespace ffxiii2 {
// Sets the environment variables of the cvar ffxiii2_mesa_env (Mesa/NVK options); call before the graphics system starts.
void ApplyMesaEnvironment();
}  // namespace ffxiii2
