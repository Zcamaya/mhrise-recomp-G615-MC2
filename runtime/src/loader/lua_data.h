#pragma once
#include <filesystem>
struct lua_State;
namespace mod_lua {
void open_data(lua_State *, const std::filesystem::path &);
}
