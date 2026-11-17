#pragma once
// Wide view (Display -> Wide view): the camera sees as much as a screen of the wide_view value in mhr.ini (the
// phone's width / height, written by the settings menu). The game still draws its usual 16:9 picture, with the wider view
// squeezed into it, and the picture is stretched back out to the whole screen (executor.cpp, picture_rect), so things
// keep their shape and you see more at the sides. The HUD is stretched with it.

#include <string>

#include "loader/nso.h"

namespace wide_view {

// Before tls::patch (the patch area is still writable then). Without the file it does nothing.
bool install(Module& mod, std::string& err);

}  // namespace wide_view
