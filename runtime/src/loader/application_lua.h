#pragma once
#include <cstdint>
#include <filesystem>
#include <mutex>
struct Module;
struct lua_State;
namespace rise {
struct Tdb;
}
namespace mod_lua {
std::recursive_mutex &lua_mutex();
void open_events(lua_State *);
void install_app_entries(const Module &, rise::Tdb &);
void draw_ui();
void draw_frame();
void reset_callbacks();
void save_config();
void fire_reset(lua_State *);
size_t event_checkpoint(lua_State *);
void rollback_events(lua_State *, size_t);
class FrameGc;
void select_event_state(lua_State *, FrameGc *);
} // namespace mod_lua
