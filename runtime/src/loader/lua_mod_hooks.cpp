#include "loader/lua_mod_hooks.h"
#include "loader/application_lua.h"
#include "loader/live_read.h"
#include "loader/lua_values.h"
#include <lua.hpp>
extern "C" {
#include "lobject.h"
#include "lopcodes.h"
}
#include <android/log.h>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <memory>
#include <thread>
#include <unordered_map>
#include <tuple>
#include <unistd.h>

namespace {
using namespace rise;
enum class Result { Void, Float, Bool, DamageFlow };
struct Event {
  uint32_t method, receiver;
  size_t entry, leave;
  uint32_t instruction;
  Result result = Result::Void;
  size_t other_entry = 0, other_leave = 0;
};
// Entry instructions and return sites of the supported NS 16.0.1 game methods.
constexpr Event events[] = {
    {296817, 29756, 0x4dcf534, 0x4dcfd30, 0xd10183ff},
    {391169, 70719, 0x450ad94, 0, 0xfc1b0fe8},
    {550231, 215688, 0x28416f8, 0, 0xf81c0ff7},
    {580162, 126799, 0x4081d9c, 0x4081e6c, 0xf81c0ff7},
    {580208, 126799, 0x4083194, 0x4083544, 0xd104c3ff},
    {581110, 153593, 0x4217238, 0x4217a54, 0xf81c0ff7},
    {581098, 153593, 0x42134e8, 0x4214ce4, 0xd10143ff},
    {391293, 70719, 0x4516a14, 0x4516b68, 0xb941d848, Result::Float, 0,
     0x4516a50},
    {580970, 153593, 0x4223388, 0x4223458, 0xfc1e0fe8, Result::Float},
    // The first instruction of the stamina getter loads a global page into
    // scratch x8. The next instruction still sees the original receiver, SP and
    // float.
    {580971, 153593, 0x4223460, 0x422350c, 0xb986d908, Result::Float},
    {197404, 22668, 0x58015a4, 0x58015f8, 0xd10143ff},
    {478135, 164478, 0x68682bc, 0, 0xa9be4ff4, Result::Void, 0x68682b8},
    {391277, 70719, 0x2979ec, 0, 0xbd40bc40},
    {374064, 56674, 0x61d2bd8, 0, 0x6dbb23e9},
    {857048, 192054, 0x525c0a4, 0x525c63c, 0xd10143ff, Result::Bool},
    {391601, 70719, 0x452cd5c, 0x452cda4, 0xa9ba6ffc, Result::DamageFlow},
    {391315, 70719, 0x4518434, 0, 0xa9bc5ff8},
    {506438, 177193, 0x4bf21dc, 0, 0xf81b0ff9},
    {555102, 32871, 0x1aa60c, 0x1aa614, 0x12000048},
    {555109, 235077, 0x42a124c, 0x42a1ae4, 0xf81a0ffb},
    {214928, 81234, 0x2fcd058, 0x2fcd0f0, 0xf81d0ff5},
    {215317, 242856, 0x35ff4b8, 0x35ff698, 0xfc170fec},
    {214931, 81234, 0x2fcd0f4, 0, 0xd10103ff},
    {214932, 81234, 0x2fcce20, 0, 0xd10103ff},
};
struct Hook {
  const Event *event;
  uint32_t method;
  int pre, post;
  uint64_t serial;
  unsigned before = 0, after = 0;
};
std::deque<Hook> hooks;
struct Subscriptions { std::vector<size_t> game, reflection; };
std::unordered_map<uint32_t,Subscriptions> by_method;
Tdb *db = nullptr;
lua_State *vm = nullptr;
bool profile_matches = false;
uint64_t epoch = 0;
uint64_t next_hook_serial = 0;
char method_key;
// Entry and return see the same guest SP after the method's epilogue. Recursive
// calls, concurrent game threads and separate Lua hooks get independent tables.
using Slot = std::tuple<uintptr_t, uint64_t>;
struct PendingCall {
  Slot slot;
  int ref;
};
// A game worker may exit with an outstanding callback. It hands references back
// to the owning VM without running Lua from a thread-local destructor.
struct RetiredRefs {
  std::mutex mutex;
  std::vector<int> refs;
  std::atomic<bool> ready{false};
  void collect(lua_State *L) {
    if (!ready.load(std::memory_order_acquire))
      return;
    std::lock_guard lock(mutex);
    for (auto ref : refs)
      luaL_unref(L, LUA_REGISTRYINDEX, ref);
    refs.clear();
    ready.store(false, std::memory_order_release);
  }
};
std::shared_ptr<RetiredRefs> retired;
struct ThreadCalls {
  std::vector<PendingCall> pending;
  std::weak_ptr<RetiredRefs> owner;
  ~ThreadCalls() {
    if (pending.empty())
      return;
    if (auto sink = owner.lock()) {
      std::lock_guard lock(sink->mutex);
      for (auto &call : pending)
        if (call.ref != LUA_NOREF)
          sink->refs.push_back(call.ref);
      if (!sink->refs.empty())
        sink->ready.store(true, std::memory_order_release);
    }
  }
};
thread_local ThreadCalls thread_calls;
thread_local uint64_t local_epoch = 0;
thread_local int *active_storage = nullptr;
int get_storage(lua_State *L) {
  if (!active_storage) {
    lua_pushnil(L);
    return 1;
  }
  if (*active_storage == LUA_NOREF) {
    lua_newtable(L);
    *active_storage = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  lua_rawgeti(L, LUA_REGISTRYINDEX, *active_storage);
  return 1;
}
bool identity_callback(lua_State *L, int index) {
  // Lua 5.4's RETURN1 R0 as the first instruction simply returns the argument.
  if (!lua_isfunction(L, index) || lua_iscfunction(L, index))
    return false;
  auto p = static_cast<const LClosure *>(lua_topointer(L, index))->p;
  return p->numparams == 1 && !p->is_vararg && p->sizeupvalues == 0 &&
         p->sizecode > 0 && GET_OPCODE(p->code[0]) == OP_RETURN1 &&
         GETARG_A(p->code[0]) == 0;
}
int register_hook(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_rawgetp(L, 1, &method_key);
  auto id = luaL_checkinteger(L, -1);
  lua_pop(L, 1);
  if (!profile_matches)
    return luaL_error(L, "Hook profile does not match this NS build");
  const Event* event=nullptr;
  bool reflection=false;
  if(lua_istable(L,4)){
    lua_getfield(L,4,"scope");
    reflection=lua_type(L,-1)==LUA_TSTRING && std::string_view(lua_tostring(L,-1))=="reflection";
    lua_pop(L,1);
    if(!reflection)return luaL_error(L,"Unknown hook scope");
  }else if(!lua_isnoneornil(L,4) && (!lua_isboolean(L,4) || lua_toboolean(L,4))){
    return luaL_error(L,"Unsupported ignore_jmp option on Android");
  }
  if(reflection){
    bool valid=true;
    try { rise::validate_reflection_hook(L,id); }
    catch(const std::exception& e){lua_pushstring(L,e.what());valid=false;}
    if(!valid)return lua_error(L);
  }else{
    for(const auto& e:events)if(e.method==id){event=&e;break;}
    if(!event)return luaL_error(L,"Game-originated hook unavailable for TDB method %I",id);
  }
    bool pre = !lua_isnoneornil(L, 2), post = !lua_isnoneornil(L, 3);
    if (pre)
      luaL_checktype(L, 2, LUA_TFUNCTION);
    if (post)
      luaL_checktype(L, 3, LUA_TFUNCTION);
    if (!pre && !post)
      return luaL_error(L, "Expected a hook callback");
    if (event && pre && !event->entry)
      return luaL_error(L, "No verified pre-hook for this method");
    if (event && post && !event->leave) {
      if (event->method != 478135 || !identity_callback(L, 3))
        return luaL_error(L, "No verified post-hook for this method");
      post = false;
      mod_lua::log_message(
          ANDROID_LOG_INFO,
          "Quest status: elided side-effect-free identity post-hook");
    }
    auto ref = [&](int i, bool present) {
      if (!present)
        return LUA_NOREF;
      lua_pushvalue(L, i);
      return luaL_ref(L, LUA_REGISTRYINDEX);
    };
    auto& group=by_method[static_cast<uint32_t>(id)];
    (reflection?group.reflection:group.game).push_back(hooks.size());
    hooks.push_back({event,static_cast<uint32_t>(id), ref(2, pre), ref(3, post), ++next_hook_serial});
    return 0;
}
int number(lua_State *L) {
  lua_pushnumber(L, luaL_checknumber(L, 1));
  return 1;
}
int integer(lua_State *L) {
  if (lua_isboolean(L, 1))
    lua_pushinteger(L, lua_toboolean(L, 1));
  else
    lua_pushinteger(L, luaL_checkinteger(L, 1));
  return 1;
}
enum class ScalarType { Single, Boolean, Signed, Unsigned, QuestStatus };
struct ScalarArgument {
  unsigned index, slot;
  ScalarType type;
  uint32_t value{};
  bool changed{};
};
struct HookArguments {
  // AttackWork.initialize has the largest signature among the observed methods.
  std::array<ScalarArgument, 6> values{};
  size_t count{};
  struct ObjectSlot {
    unsigned index;
    int type;
    const void *identity;
  };
  std::array<ObjectSlot, 8> objects{};
  size_t object_count{};
  void read_only(lua_State *L, unsigned index) {
    objects.at(object_count++) = {index, lua_type(L, -1), lua_topointer(L, -1)};
  }
  void track(unsigned index, unsigned slot, ScalarType type) {
    values.at(count++) = {index, slot, type};
  }
  void apply(lua_State *L, int args, uint64_t *regs) {
    for (size_t i = 0; i < object_count; ++i) {
      auto &a = objects[i];
      lua_rawgeti(L, args, a.index);
      if (lua_type(L, -1) != a.type || lua_topointer(L, -1) != a.identity)
        throw std::runtime_error("Cannot replace hook argument " +
                                 std::to_string(a.index) + " at this event");
      lua_pop(L, 1);
    }
    for (size_t i = 0; i < count; ++i) {
      auto &a = values[i];
      lua_rawgeti(L, args, a.index);
      if (a.type == ScalarType::Single) {
        if (lua_type(L, -1) != LUA_TNUMBER)
          throw std::runtime_error("Expected a numeric hook argument");
        uint32_t original;
        memcpy(&original,
               reinterpret_cast<const uint8_t *>(regs) + 240 + 16 * a.slot,
               sizeof(original));
        float v = static_cast<float>(lua_tonumber(L, -1));
        memcpy(&a.value, &v, sizeof(v));
        float old;
        memcpy(&old, &original, sizeof(old));
        a.changed = original != a.value && !(std::isnan(old) && std::isnan(v));
        if (a.changed && !std::isfinite(v))
          throw std::runtime_error("Non-finite hook argument");
      } else if (a.type == ScalarType::Boolean && lua_isboolean(L, -1)) {
        a.value = lua_toboolean(L, -1);
      } else {
        if (!lua_isinteger(L, -1))
          throw std::runtime_error("Expected an integer hook argument");
        auto n = lua_tointeger(L, -1);
        bool sign =
            a.type == ScalarType::Signed || a.type == ScalarType::QuestStatus;
        int64_t hi = sign                            ? int64_t(INT32_MAX)
                     : a.type == ScalarType::Boolean ? 1
                                                     : int64_t(UINT32_MAX);
        if (n < (sign ? int64_t(INT32_MIN) : 0) || n > hi)
          throw std::runtime_error("Hook argument outside declared range");
        a.value = static_cast<uint32_t>(n);
        // This observer runs after the status-dependent branch; that input is
        // read-only.
        if (a.type == ScalarType::QuestStatus &&
            a.value != uint32_t(regs[a.slot]))
          throw std::runtime_error(
              "Quest status cannot be changed at this event");
      }
      if (a.type != ScalarType::Single) {
        auto old = uint32_t(regs[a.slot]);
        a.changed =
            a.value != (a.type == ScalarType::Boolean ? (old & 1u) : old);
      }
      lua_pop(L, 1);
    }
    // Commit only after every scalar has been checked and the Lua callback
    // succeeded.
    for (size_t i = 0; i < count; ++i) {
      const auto &a = values[i];
      if (!a.changed)
        continue;
      if (a.type == ScalarType::Single)
        memcpy(reinterpret_cast<uint8_t *>(regs) + 240 + 16 * a.slot, &a.value,
               sizeof(a.value));
      else if (a.type != ScalarType::QuestStatus)
        regs[a.slot] = a.value;
    }
  }
};
void push_args(lua_State *L, const Event &e, const uint64_t *regs,
               HookArguments &scalars) {
  auto sig = db->signature_offset(e.method);
  auto n = read<uint16_t>(db->byte_pool, sig);
  lua_createtable(L, n + 2, 0);
  if (regs[0])
    lua_pushlightuserdata(L, reinterpret_cast<void *>(regs[0]));
  else
    lua_pushnil(L);
  scalars.read_only(L, 1);
  lua_rawseti(L, -2, 1);
  push_live_object(L, regs[1], e.receiver);
  scalars.read_only(L, 2);
  lua_rawseti(L, -2, 2);
  unsigned gp = 2, fp = 0;
  for (unsigned i = 1; i <= n; ++i) {
    auto type = db->parameter_type(sig, i);
    switch (type) {
    case 145174: {
      float f;
      scalars.track(i + 2, fp, ScalarType::Single);
      memcpy(&f, (const uint8_t *)regs + 240 + 16 * fp++, 4);
      lua_pushnumber(L, f);
      break;
    }
    case 232978:
      scalars.track(i + 2, gp, ScalarType::Boolean);
      lua_pushinteger(L, regs[gp++] & 1);
      break;
    case 23215:
      scalars.track(i + 2, gp, ScalarType::Unsigned);
      lua_pushinteger(L, uint32_t(regs[gp++]));
      break;
    case 167229:
    case 35338:
      scalars.track(i + 2, gp,
                    type == 167229 ? ScalarType::QuestStatus
                                   : ScalarType::Signed);
      lua_pushinteger(L, int32_t(regs[gp++]));
      break;
    case 88755:
    case 20352: {
      auto p = reinterpret_cast<const float *>(regs[gp++]);
      if (!p)
        lua_pushnil(L);
      else
        mod_lua::push_components(L, type == 20352, p);
      scalars.read_only(L, i + 2);
      break;
    }
    default:
      push_live_object(L, regs[gp++], type);
      scalars.read_only(L, i + 2);
      break;
    }
    lua_rawseti(L, -2, i + 2);
  }
}
void push_result(lua_State *L, Result result, const uint64_t *regs) {
  if (result == Result::Void)
    lua_pushnil(L);
  else if (result == Result::Float) {
    float f;
    memcpy(&f, (const uint8_t *)regs + 240, 4);
    lua_pushnumber(L, f);
  } else
    lua_pushinteger(L,
                    result == Result::Bool ? (regs[0] & 1) : uint32_t(regs[0]));
}
void accept_result(lua_State *L, Result shape, uint64_t *regs) {
  if (lua_isnil(L, -1))
    return;
  if (shape == Result::Float && lua_type(L, -1) == LUA_TNUMBER) {
    float f = lua_tonumber(L, -1);
    if (!std::isfinite(f))
      throw std::runtime_error("Non-finite combat multiplier");
    memcpy((uint8_t *)regs + 240, &f, 4);
    return;
  }
  if ((shape == Result::Bool || shape == Result::DamageFlow) &&
      lua_isinteger(L, -1)) {
    auto v = lua_tointeger(L, -1);
    if (v < 0 || v > (shape == Result::Bool ? 1 : 2))
      throw std::runtime_error("Invalid game result");
    regs[0] = v;
    return;
  }
  throw std::runtime_error("Incorrect hook result type");
}
mod_lua::PreHookResult pre_action(lua_State* L) {
  using Action=mod_lua::PreHookResult;
  if(lua_isnil(L,-1))return Action::CallOriginal;
  if(lua_isinteger(L,-1)){
    auto n=lua_tointeger(L,-1);
    if(n==static_cast<int>(Action::CallOriginal))return Action::CallOriginal;
    if(n==static_cast<int>(Action::SkipOriginal))return Action::SkipOriginal;
  }
  throw std::runtime_error("Expected nil or an sdk.PreHookResult value");
}
class StorageScope {
public:
  explicit StorageScope(int& ref):prior(active_storage){active_storage=&ref;}
  ~StorageScope(){active_storage=prior;}
private:
  int* prior;
};
void call_hook(lua_State* L,int ref,int arg,int& storage){
  lua_rawgeti(L,LUA_REGISTRYINDEX,ref);lua_pushvalue(L,arg);
  StorageScope scope(storage);
  if(lua_pcall(L,1,1,0)!=LUA_OK)
    throw std::runtime_error(lua_isstring(L,-1)?lua_tostring(L,-1):"Non-string hook error");
}
} // namespace
bool mod_lua::has_reflection_hooks(uint32_t method){
  auto it=by_method.find(method);
  return it!=by_method.end() && !it->second.reflection.empty();
}
mod_lua::ReflectionHookCall::ReflectionHookCall(lua_State *state,uint32_t method):L(state){
  const auto& ids=by_method.at(method).reflection;
  callbacks.reserve(ids.size());
  for(auto i:ids)callbacks.push_back({hooks[i].pre,hooks[i].post,LUA_NOREF});
}
mod_lua::ReflectionHookCall::~ReflectionHookCall(){
  for(const auto& cb:callbacks)luaL_unref(L,LUA_REGISTRYINDEX,cb.storage);
}
mod_lua::PreHookResult mod_lua::ReflectionHookCall::before(int arguments){
  auto action=PreHookResult::CallOriginal;
  for(auto& cb:callbacks){
    if(cb.pre==LUA_NOREF)continue;
    call_hook(L,cb.pre,arguments,cb.storage);
    if(pre_action(L)==PreHookResult::SkipOriginal)action=PreHookResult::SkipOriginal;
    lua_pop(L,1);
  }
  return action;
}
bool mod_lua::ReflectionHookCall::after(int result){
  bool replaced=false;
  for(auto it=callbacks.rbegin();it!=callbacks.rend();++it){
    if(it->post==LUA_NOREF)continue;
    call_hook(L,it->post,result,it->storage);
    if(lua_isnil(L,-1) || lua_rawequal(L,-1,result))lua_pop(L,1);
    else {lua_replace(L,result);replaced=true;}
  }
  return replaced;
}
void mod_lua::tag_method(lua_State *L, uint32_t id) {
  lua_pushinteger(L, id);
  lua_rawsetp(L, -2, &method_key);
}
void mod_lua::install_api(lua_State *L, rise::Tdb &t, const Module &main) {
  vm = L;
  db = &t;
  ++epoch;
  retired = std::make_shared<RetiredRefs>();
  constexpr uint8_t build[32] = {0x55, 0xd5, 0x0c, 0xa1, 0x80, 0x5e, 0x9c,
                                 0x5b, 0xba, 0x50, 0x8d, 0x60, 0x1a, 0xdd,
                                 0x85, 0x2f, 0x25, 0x5a, 0x32, 0xba};
  profile_matches = !memcmp(build, main.build_id, sizeof build);
  lua_pushcfunction(L, register_hook);
  lua_setfield(L, -2, "hook");
  lua_pushcfunction(L, [](lua_State *s) {
    if (!lua_isnil(s, 1) && !rise::is_live_object(s, 1))
      return luaL_error(s, "Expected a managed game object");
    lua_settop(s, 1);
    return 1;
  });
  lua_setfield(L, -2, "to_managed_object");
  for (auto [name, fn] :
       {std::pair<const char *, lua_CFunction>{"to_int64", integer},
        {"to_ptr", integer},
        {"to_float", number},
        {"float_to_ptr", number}}) {
    lua_pushcfunction(L, fn);
    lua_setfield(L, -2, name);
  }
  lua_newtable(L);
  lua_pushinteger(L, static_cast<int>(PreHookResult::CallOriginal));
  lua_setfield(L, -2, "CALL_ORIGINAL");
  lua_pushinteger(L, static_cast<int>(PreHookResult::SkipOriginal));
  lua_setfield(L, -2, "SKIP_ORIGINAL");
  lua_setfield(L, -2, "PreHookResult");
  lua_newtable(L);
  lua_pushcfunction(L, [](lua_State *s) {
    lua_pushinteger(s, gettid());
    return 1;
  });
  lua_setfield(L, -2, "get_id");
  lua_pushcfunction(L, [](lua_State *s) {
    lua_pushinteger(s, static_cast<lua_Integer>(std::hash<std::thread::id>{}(
                           std::this_thread::get_id())));
    return 1;
  });
  lua_setfield(L, -2, "get_hash");
  lua_pushcfunction(L, get_storage);
  lua_setfield(L, -2, "get_hook_storage");
  lua_setglobal(L, "thread");
}
size_t mod_lua::checkpoint() { return hooks.size(); }
void mod_lua::rollback(lua_State *L, size_t count) {
  while (hooks.size() > count) {
    auto it=by_method.find(hooks.back().method);
    (hooks.back().event?it->second.game:it->second.reflection).pop_back();
    if(it->second.game.empty() && it->second.reflection.empty())by_method.erase(it);
    luaL_unref(L, LUA_REGISTRYINDEX, hooks.back().pre);
    luaL_unref(L, LUA_REGISTRYINDEX, hooks.back().post);
    hooks.pop_back();
  }
}
std::vector<size_t> lua_lifecycle_sites(const Module &main) {
  std::vector<size_t> sites;
  if (!profile_matches)
    return sites;
  Bytes text(main.base + main.seg_off[0], main.seg_size[0]);
  for (auto &e : events) {
    for (auto [site, instruction] : {std::pair{e.entry, e.instruction},
                                     {e.other_entry, 0xd65f03c0u},
                                     {e.leave, 0xd65f03c0u},
                                     {e.other_leave, 0xd65f03c0u}}) {
      if (!site)
        continue;
      if (read<uint32_t>(text, site) != instruction) {
        profile_matches = false;
        mod_lua::log_message(
            ANDROID_LOG_ERROR,
            ("Game hook profile mismatch for TDB " + std::to_string(e.method))
                .c_str());
        return {};
      }
      sites.push_back(site);
    }
  }
  return sites;
}
void on_lua_lifecycle(size_t site, uint64_t *regs, const void *caller_stack) {
  std::lock_guard lock(mod_lua::lua_mutex());
  auto &pending = thread_calls.pending;
  if (local_epoch != epoch) {
    pending.clear();
    thread_calls.owner = retired;
    local_epoch = epoch;
  }
  retired->collect(vm);
  const Event* event=nullptr;
  bool pre=false;
  for(const auto& candidate:events){
    pre=site==candidate.entry || site==candidate.other_entry;
    if(pre || site==candidate.leave || site==candidate.other_leave){event=&candidate;break;}
  }
  if(!event)return;
  auto group=by_method.find(event->method);
  if(group==by_method.end())return;
  const auto& ids=group->second.game;
  const size_t count=ids.size();
  const auto& e=*event;
  for(size_t k=0;k<count;++k){
    auto& h=hooks[ids[pre?k:count-1-k]];
    int ref = pre ? h.pre : h.post;
    // Record entry even for a post-only Lua subscription when both game sites
    // exist. A return from before reload must not call a newly installed hook.
    if (ref == LUA_NOREF && pre && h.post == LUA_NOREF)
      continue;
    auto key = Slot{reinterpret_cast<uintptr_t>(caller_stack), h.serial};
    int local = LUA_NOREF;
    auto existing =
        std::find_if(pending.begin(), pending.end(),
                     [&](const PendingCall &p) { return p.slot == key; });
    if (existing != pending.end()) {
      if (pre)
        luaL_unref(vm, LUA_REGISTRYINDEX, existing->ref);
      else
        local = existing->ref;
      pending.erase(existing);
    } else if (!pre && e.entry) {
      continue;
    }
    if (ref == LUA_NOREF) {
      if (pre)
        pending.push_back({key, LUA_NOREF});
      else
        luaL_unref(vm, LUA_REGISTRYINDEX, local);
      continue;
    }
    int top = lua_gettop(vm);
    try {
      HookArguments scalars;
      if (pre)
        push_args(vm, e, regs, scalars);
      if (!pre)
        push_result(vm, e.result, regs);
      call_hook(vm,ref,top+1,local);
      if (!pre)
        accept_result(vm, e.result, regs);
      else if(pre_action(vm)==mod_lua::PreHookResult::SkipOriginal)
        throw std::runtime_error("SKIP_ORIGINAL is unavailable at this game observation site");
      if (pre)
        scalars.apply(vm, top + 1, regs);
      if (++(pre ? h.before : h.after) == 1)
        mod_lua::log_message(ANDROID_LOG_INFO,
                             (std::string(db->method(e.method, e.receiver)) +
                              (pre ? " pre returned" : " post returned"))
                                 .c_str());
    } catch (const std::exception &ex) {
      mod_lua::log_message(ANDROID_LOG_ERROR,
                           (std::string(db->method(e.method, e.receiver)) +
                            " hook disabled: " + ex.what())
                               .c_str());
      luaL_unref(vm, LUA_REGISTRYINDEX, h.pre);
      luaL_unref(vm, LUA_REGISTRYINDEX, h.post);
      h.pre = h.post = LUA_NOREF;
    }
    lua_settop(vm, top);
    if (pre && h.post != LUA_NOREF)
      pending.push_back({key, local});
    else
      luaL_unref(vm, LUA_REGISTRYINDEX, local);
  }
}
