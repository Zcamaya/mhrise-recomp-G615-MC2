#pragma once
struct lua_State;
namespace mod_lua {
void open_imgui(lua_State *);
void service_ui_frame();
int call_ui_callback(lua_State *);
bool ui_visible();
} // namespace mod_lua
