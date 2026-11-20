#include "loader/application_lua.h"
#include "loader/lua_mod_hooks.h"
#include "loader/mod_ui.h"
#include "loader/nso.h"
#include "loader/tdb71.h"
#include <android/log.h>
#include <array>
#include <atomic>
#include <cstring>
#include <lua.hpp>
#include <string_view>
#include <vector>

namespace mod_lua {
namespace {
enum class Phase { Before, After, Ui, Reset, Frame, Config };
using Fn = void (*)(void *);
struct Entry {
  std::string_view name;
  Fn *slot{};
  Fn original{};
  bool observed = false;
};
constexpr size_t slot_count = 269;
std::array<Entry, slot_count> entries;
int update_scene = -1, begin_rendering = -1;
int entry_index(std::string_view name) {
  for (size_t i = 0; i < entries.size(); ++i)
    if (entries[i].name == name)
      return static_cast<int>(i);
  return -1;
}
void observe(int index);
char callbacks_key;
std::atomic<lua_State *> active{};
std::recursive_mutex mutex;
bool attached = false;
// Each state owns its registry references. Failed loads can discard just their
// registrations.
void list(lua_State *L) { lua_rawgetp(L, LUA_REGISTRYINDEX, &callbacks_key); }
int add_callback(lua_State *L) {
  auto phase = static_cast<Phase>(lua_tointeger(L, lua_upvalueindex(1)));
  int index = -1, fn = 1;
  if (phase == Phase::Before || phase == Phase::After) {
    const char *name = luaL_checkstring(L, 1);
    index = entry_index(name);
    if (index < 0)
      return luaL_error(L, "Unknown application entry: %s", name);
    observe(index);
    fn = 2;
  }
  if (phase == Phase::Frame && begin_rendering >= 0)
    observe(begin_rendering);
  luaL_checktype(L, fn, LUA_TFUNCTION);
  list(L);
  auto pos = lua_rawlen(L, -1) + 1;
  lua_createtable(L, 3, 0);
  lua_pushinteger(L, static_cast<int>(phase));
  lua_rawseti(L, -2, 1);
  lua_pushinteger(L, index);
  lua_rawseti(L, -2, 2);
  lua_pushvalue(L, fn);
  lua_rawseti(L, -2, 3);
  lua_rawseti(L, -2, pos);
  return 0;
}
void fire(lua_State *L, Phase phase, int index) {
  if (!L)
    return;
  const int top = lua_gettop(L);
  list(L);
  const int table = lua_gettop(L);
  const size_t count = lua_rawlen(L, table);
  for (size_t k = 1; k <= count; ++k) {
    lua_rawgeti(L, table, k);
    const int record = lua_gettop(L);
    lua_rawgeti(L, record, 1);
    lua_rawgeti(L, record, 2);
    bool match = lua_tointeger(L, -2) == static_cast<int>(phase) &&
                 lua_tointeger(L, -1) == index;
    lua_pop(L, 2);
    if (match) {
      lua_rawgeti(L, record, 3);
      if (lua_isfunction(L, -1)) {
        if ((phase == Phase::Ui ? call_ui_callback(L)
                                : lua_pcall(L, 0, 0, 0)) != LUA_OK) {
          lua_rawgeti(L, record, 4);
          bool repeated = lua_rawequal(L, -1, -2);
          lua_pop(L, 1);
          const char *msg = lua_tostring(L, -1);
          if (!repeated)
            log_message(ANDROID_LOG_ERROR,
                        msg ? msg : "Lua event callback failed");
          lua_rawseti(L, record, 4);
        } else {
          lua_pushnil(L);
          lua_rawseti(L, record, 4);
        }
      } else
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
  lua_settop(L, top);
}
void dispatch_index(size_t index, void *instance) {
  {
    std::lock_guard lock(mutex);
    fire(active.load(), Phase::Before, index);
  }
  entries[index].original(instance);
  std::lock_guard lock(mutex);
  fire(active.load(), Phase::After, index);
  if (int(index) == update_scene)
    service_ui_frame();
  if (int(index) == begin_rendering)
    fire(active.load(), Phase::Frame, -1);
}
template <size_t Index> void dispatch(void *instance) {
  dispatch_index(Index, instance);
}
template <size_t... I> constexpr auto make_wrappers(std::index_sequence<I...>) {
  return std::array<Fn, sizeof...(I)>{dispatch<I>...};
}
constexpr auto wrappers = make_wrappers(std::make_index_sequence<slot_count>{});
void observe(int index) {
  auto &entry = entries.at(index);
  if (!entry.observed) {
    __atomic_store_n(entry.slot, wrappers[index], __ATOMIC_RELEASE);
    entry.observed = true;
  }
}
} // namespace
std::recursive_mutex &lua_mutex() { return mutex; }
void select_event_state(lua_State *L) { active.store(L); }
void open_events(lua_State *L) {
  list(L);
  bool first = lua_isnil(L, -1);
  lua_pop(L, 1);
  if (first) {
    lua_newtable(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &callbacks_key);
  }
  lua_newtable(L);
  for (auto [name, phase] :
       {std::pair{"on_pre_application_entry", Phase::Before},
        {"on_application_entry", Phase::After},
        {"on_draw_ui", Phase::Ui},
        {"on_script_reset", Phase::Reset},
        {"on_frame", Phase::Frame},
        {"on_config_save", Phase::Config}}) {
    lua_pushinteger(L, static_cast<int>(phase));
    lua_pushcclosure(L, add_callback, 1);
    lua_setfield(L, -2, name);
  }
  lua_setglobal(L, "re");
  lua_newtable(L);
  lua_pushcfunction(L, [](lua_State *s) {
    lua_pushboolean(s, ui_visible());
    return 1;
  });
  lua_setfield(L, -2, "is_drawing_ui");
  lua_pushcfunction(L, [](lua_State *s) {
    lua_pushliteral(s, "mhrise");
    return 1;
  });
  lua_setfield(L, -2, "get_game_name");
  lua_setglobal(L, "reframework");
}
size_t event_checkpoint(lua_State *L) {
  list(L);
  auto n = lua_rawlen(L, -1);
  lua_pop(L, 1);
  return n;
}
void rollback_events(lua_State *L, size_t n) {
  list(L);
  for (auto k = lua_rawlen(L, -1); k > n; --k) {
    lua_pushnil(L);
    lua_rawseti(L, -2, k);
  }
  lua_pop(L, 1);
}
void fire_reset(lua_State *L) { fire(L, Phase::Reset, -1); }
void reset_callbacks() {
  std::lock_guard lock(mutex);
  if (auto *L = active.load())
    fire_reset(L);
}
void save_config() {
  std::lock_guard lock(mutex);
  fire(active.load(), Phase::Config, -1);
}
void draw_ui() {
  std::lock_guard lock(mutex);
  fire(active.load(), Phase::Ui, -1);
}
void install_app_entries(const Module &m, rise::Tdb &db) {
  std::lock_guard lock(mutex);
  if (attached)
    return;
  auto app_slot = *reinterpret_cast<const uintptr_t *>(m.addr(0x12674850));
  auto app = app_slot ? *reinterpret_cast<const uintptr_t *>(app_slot) : 0;
  if (!app)
    throw std::runtime_error("via.Application not initialized");
  auto ro = m.addr(m.seg_off[1]), end = ro + m.seg_size[1];
  auto type = db.find("via.ModuleEntry");
  auto [first, count] = db.field_range(type);
  std::array<Entry, slot_count> found;
  for (size_t i = 0; i < slot_count; ++i) {
    auto row = app + 0x8e0 + 0xd0 * i;
    auto str = *reinterpret_cast<const uintptr_t *>(row);
    auto priority = *reinterpret_cast<const uint16_t *>(row + 8);
    auto slot = reinterpret_cast<Fn *>(row - 16);
    auto address = reinterpret_cast<uintptr_t>(*slot);
    if (priority + 1 >= count || str < ro || str >= end ||
        address < m.addr(0) || address >= m.addr(m.seg_size[0]) || address % 4)
      throw std::runtime_error("Invalid via.Application entry");
    auto expected = db.field(first + priority + 1, type).name;
    if (expected.size() + 1 > end - str ||
        memcmp(reinterpret_cast<const void *>(str), expected.data(),
               expected.size() + 1))
      throw std::runtime_error("via.ModuleEntry name and priority disagree");
    found[i] = {expected, slot, *slot};
  }
  entries = found;
  update_scene = entry_index("UpdateScene");
  begin_rendering = entry_index("BeginRendering");
  if (update_scene < 0 || begin_rendering < 0)
    throw std::runtime_error("Missing game frame entries");
  observe(update_scene);
  attached = true;
  log_message(
      ANDROID_LOG_INFO,
      "via.Application: 269 named entries verified against via.ModuleEntry");
}
} // namespace mod_lua
