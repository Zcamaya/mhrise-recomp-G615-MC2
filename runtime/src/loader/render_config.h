#pragma once
// MH Rise 16.0.1: catches the game's via.render.RenderConfig (the PC options menu's settings: motion blur, depth of
// field, bloom…), which unlike the Renderer isn't at a fixed address. Its bool getters are patched to also leave the
// object they were asked about in slot() (NnsdkHost::render_config, read by nnsdk/nvn/game_tweaks.cpp). Nothing else
// changes: the getter then does what it always did.

#include <cstdint>
#include <string>

#include "loader/nso.h"

namespace render_config {

// Before tls::patch (the patch area is still writable then). Leaves main alone if it isn't the known build.
bool install(Module& mod, std::string& err);

const volatile uint64_t* slot();

}  // namespace render_config
