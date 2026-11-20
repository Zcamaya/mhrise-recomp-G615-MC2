#include "loader/live_read.h"
#include "loader/lua_mod_hooks.h"
#include "loader/lua_values.h"
#include "loader/tls.h"
#include <cmath>
#include <codecvt>
#include <cstdio>
#include <limits>
#include <locale>
#include <lua.hpp>
#include <mutex>
#include <optional>
#include <vector>

namespace rise {
namespace {
constexpr char object_mt[] = "rise.sample.object";
char frame_key, object_cache_key;
struct Object {
  uintptr_t address;
  uint32_t type;
  uintptr_t release{};
};
std::mutex releases_mutex;
std::vector<std::pair<uintptr_t, uintptr_t>> releases;
ReadFrame &frame(lua_State *L) {
  return *static_cast<ReadFrame *>(lua_touserdata(L, lua_upvalueindex(1)));
}
bool isa(Tdb &t, uint32_t id, uint32_t expected) {
  if (!expected)
    return true;
  for (; id; id = t.parent(id))
    if (id == expected)
      return true;
  return false;
}
uint32_t actual_type(Tdb &t, uintptr_t obj) {
  auto info = *reinterpret_cast<const uintptr_t *>(obj);
  if (!info)
    throw std::runtime_error("Object has no class information");
  auto type = *reinterpret_cast<const uintptr_t *>(info);
  auto first = reinterpret_cast<uintptr_t>(t.types.data());
  if (type < first || type - first >= t.types.size() || (type - first) % 72)
    throw std::runtime_error("Object class is not in the active TDB");
  return (type - first) / 72;
}
void push_obj(lua_State *L, ReadFrame &f, uintptr_t obj, uint32_t expected) {
  if (!obj) {
    lua_pushnil(L);
    return;
  }
  if (f.persistent) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &object_cache_key);
    lua_rawgetp(L, -1, reinterpret_cast<void *>(obj));
    if (auto *cached = static_cast<Object *>(luaL_testudata(L, -1, object_mt));
        cached && isa(f.db, cached->type, expected)) {
      lua_remove(L, -2);
      return;
    }
    lua_pop(L, 1);
  }
  auto id = actual_type(f.db, obj);
  if (!isa(f.db, id, expected))
    throw std::runtime_error("Unexpected runtime object type");
  auto *handle = static_cast<Object *>(lua_newuserdatauv(L, sizeof(Object), 0));
  *handle = {obj, id};
  if (f.persistent) {
    reinterpret_cast<void (*)(uintptr_t)>(f.main.addr(0x6b43e80))(obj);
    handle->release = f.main.addr(0x6b44408);
  }
  luaL_setmetatable(L, object_mt);
  if (f.persistent) {
    lua_pushvalue(L, -1);
    lua_rawsetp(L, -3, reinterpret_cast<void *>(obj));
    lua_remove(L, -2);
  }
}
Object object(lua_State *L, int index) {
  auto *p = static_cast<Object *>(luaL_checkudata(L, index, object_mt));
  if (lua_rawlen(L, index) != sizeof(Object))
    luaL_error(L, "Invalid sample handle");
  return *p;
}
uintptr_t function(ReadFrame &f, uint32_t method) {
  auto offset = read<int32_t>(f.db.methods, size_t(method) * 12 + 8);
  uintptr_t address = f.encoded_base + offset;
  if (!offset || address < f.main.addr(f.main.seg_off[0]) ||
      address >= f.main.addr(f.main.seg_off[0] + f.main.seg_size[0]) ||
      address % 4)
    throw std::runtime_error("Game getter is not initialized");
  return address;
}
uint32_t owner(Tdb &t, uint32_t method) {
  return read<uint32_t>(t.methods, size_t(method) * 12) & id_mask;
}
// NS RuntimeMethodInfo.Invoke delegates to the VM with a TDB method row and
// boxed arguments. The VM handles virtual dispatch and the ARM64 ABI, including
// aggregate returns.
enum class ValueKind {
  Void,
  Reference,
  String,
  Bool,
  I8,
  U8,
  I16,
  U16,
  I32,
  U32,
  I64,
  U64,
  F32,
  F64,
  Vec2,
  Vec3,
  Vec4,
  Quat,
  Mat4,
  Struct
};
ValueKind value_kind(Tdb &db, uint32_t id) {
  auto name = db.full_name(id);
  if (name == "System.String")
    return ValueKind::String;
  if (name == "System.Void")
    return ValueKind::Void;
  if (name == "System.IntPtr" || name == "System.UIntPtr")
    throw std::runtime_error("Unmanaged pointer signature");
  if (db.parent(id) == db.find("System.Enum")) {
    auto [first, count] = db.field_range(id);
    if (!count || db.field(first, id).name != "value__")
      throw std::runtime_error("Invalid enum representation");
    return value_kind(db, db.field(first, id).type);
  }
  if (!isa(db, id, db.find("System.ValueType")))
    return ValueKind::Reference;
  static constexpr std::pair<std::string_view, ValueKind> values[] = {
      {"System.Boolean", ValueKind::Bool}, {"System.SByte", ValueKind::I8},
      {"System.Byte", ValueKind::U8},      {"System.Int16", ValueKind::I16},
      {"System.UInt16", ValueKind::U16},   {"System.Char", ValueKind::U16},
      {"System.Int32", ValueKind::I32},    {"System.UInt32", ValueKind::U32},
      {"System.Int64", ValueKind::I64},    {"System.UInt64", ValueKind::U64},
      {"System.Single", ValueKind::F32},   {"System.Double", ValueKind::F64},
      {"via.vec2", ValueKind::Vec2},       {"via.vec3", ValueKind::Vec3},
      {"via.vec4", ValueKind::Vec4},       {"via.Quaternion", ValueKind::Quat},
      {"via.mat4", ValueKind::Mat4}};
  for (auto [type, kind] : values)
    if (name == type)
      return kind;
  return ValueKind::Struct;
}
std::span<uint8_t> boxed_payload(ReadFrame &f, uintptr_t obj, uint32_t id) {
  auto class_info = *reinterpret_cast<const uintptr_t *>(obj);
  int32_t offset = *reinterpret_cast<const int32_t *>(class_info - 8);
  uint32_t size = read<uint32_t>(f.db.types, 72ull * id + 20);
  if (offset < 16 || uint32_t(offset) > size)
    throw std::runtime_error("Invalid boxed value layout");
  return {reinterpret_cast<uint8_t *>(obj + offset), size - uint32_t(offset)};
}
template <class T> void put_value(std::span<uint8_t> data, T value) {
  if (data.size() < sizeof(value))
    throw std::runtime_error("Value exceeds game type size");
  memcpy(data.data(), &value, sizeof(value));
}
template <class T> T integer_arg(lua_State *L, int arg) {
  if (!lua_isinteger(L, arg))
    throw std::runtime_error("Integer argument required");
  auto n = lua_tointeger(L, arg);
  if constexpr (sizeof(T) < sizeof(n)) {
    if (n < std::numeric_limits<T>::min() ||
        n > int64_t(std::numeric_limits<T>::max()))
      throw std::runtime_error("Integer outside game type range");
  }
  return static_cast<T>(n);
}
void write_scalar(lua_State *L, int arg, ValueKind kind,
                  std::span<uint8_t> bytes) {
  switch (kind) {
  case ValueKind::Mat4: {
    auto *matrix = luaL_testudata(L, arg, "rise.Matrix4x4f");
    if (!matrix || lua_rawlen(L, arg) != 16 * sizeof(float))
      throw std::runtime_error("Expected a Matrix4x4f value");
    slice(bytes, 0, 16 * sizeof(float));
    memcpy(bytes.data(), matrix, 16 * sizeof(float));
    return;
  }
  case ValueKind::Bool:
    if (!lua_isboolean(L, arg))
      throw std::runtime_error("Boolean argument required");
    return put_value(bytes, uint8_t(lua_toboolean(L, arg)));
  case ValueKind::I8:
    return put_value(bytes, integer_arg<int8_t>(L, arg));
  case ValueKind::U8:
    return put_value(bytes, integer_arg<uint8_t>(L, arg));
  case ValueKind::I16:
    return put_value(bytes, integer_arg<int16_t>(L, arg));
  case ValueKind::U16:
    return put_value(bytes, integer_arg<uint16_t>(L, arg));
  case ValueKind::I32:
    return put_value(bytes, integer_arg<int32_t>(L, arg));
  case ValueKind::U32:
    return put_value(bytes, integer_arg<uint32_t>(L, arg));
  case ValueKind::I64:
  case ValueKind::U64:
    return put_value(bytes, integer_arg<int64_t>(L, arg));
  case ValueKind::F32:
  case ValueKind::F64: {
    if (lua_type(L, arg) != LUA_TNUMBER)
      throw std::runtime_error("Numeric argument required");
    auto v = lua_tonumber(L, arg);
    if (kind == ValueKind::F32)
      return put_value(bytes, float(v));
    return put_value(bytes, v);
  }
  case ValueKind::Vec2:
  case ValueKind::Vec3:
  case ValueKind::Vec4:
  case ValueKind::Quat: {
    int dim = kind == ValueKind::Vec2 ? 2 : kind == ValueKind::Vec3 ? 3 : 4;
    bool quat = kind == ValueKind::Quat;
    float data[4]{};
    if (!mod_lua::read_components(L, arg, quat, data, dim))
      throw std::runtime_error("Incorrect vector or quaternion argument");
    auto n = dim * sizeof(float);
    slice(bytes, 0, n);
    memcpy(bytes.data(), data, n);
    return;
  }
  default:
    throw std::runtime_error("Expected a compatible managed value");
  }
}
void read_scalar(lua_State *L, ValueKind kind, Bytes bytes) {
  switch (kind) {
  case ValueKind::Mat4: {
    auto data = slice(bytes, 0, 16 * sizeof(float));
    auto *matrix = lua_newuserdatauv(L, data.size(), 0);
    memcpy(matrix, data.data(), data.size());
    luaL_setmetatable(L, "rise.Matrix4x4f");
    return;
  }
  case ValueKind::Bool:
    lua_pushboolean(L, read<uint8_t>(bytes, 0));
    return;
  case ValueKind::I8:
    lua_pushinteger(L, read<int8_t>(bytes, 0));
    return;
  case ValueKind::U8:
    lua_pushinteger(L, read<uint8_t>(bytes, 0));
    return;
  case ValueKind::I16:
    lua_pushinteger(L, read<int16_t>(bytes, 0));
    return;
  case ValueKind::U16:
    lua_pushinteger(L, read<uint16_t>(bytes, 0));
    return;
  case ValueKind::I32:
    lua_pushinteger(L, read<int32_t>(bytes, 0));
    return;
  case ValueKind::U32:
    lua_pushinteger(L, read<uint32_t>(bytes, 0));
    return;
  case ValueKind::I64:
  case ValueKind::U64:
    lua_pushinteger(L, read<int64_t>(bytes, 0));
    return;
  case ValueKind::F32:
    lua_pushnumber(L, read<float>(bytes, 0));
    return;
  case ValueKind::F64:
    lua_pushnumber(L, read<double>(bytes, 0));
    return;
  case ValueKind::Vec2:
  case ValueKind::Vec3:
  case ValueKind::Vec4:
  case ValueKind::Quat: {
    int dim = kind == ValueKind::Vec2 ? 2 : kind == ValueKind::Vec3 ? 3 : 4;
    bool quat = kind == ValueKind::Quat;
    auto data = slice(bytes, 0, dim * sizeof(float));
    mod_lua::push_components(L, quat,
                             reinterpret_cast<const float *>(data.data()), dim);
    return;
  }
  default:
    throw std::runtime_error("Expected a scalar or vector return type");
  }
}
void read_box(lua_State *L, ReadFrame &f, uintptr_t value, uint32_t expected) {
  if (!value) {
    lua_pushnil(L);
    return;
  }
  auto actual = actual_type(f.db, value);
  if (!isa(f.db, actual, expected))
    throw std::runtime_error("Incorrect managed return type");
  auto kind = value_kind(f.db, actual);
  if (kind == ValueKind::String) {
    auto n = *reinterpret_cast<const int32_t *>(value + 0x10);
    if (n < 0)
      throw std::runtime_error("Invalid string length");
    auto *chars = reinterpret_cast<const char16_t *>(value + 0x14);
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> codec;
    auto str = codec.to_bytes(chars, chars + n);
    lua_pushlstring(L, str.data(), str.size());
    return;
  }
  if (kind == ValueKind::Reference || kind == ValueKind::Struct) {
    push_obj(L, f, value, actual);
    return;
  }
  read_scalar(L, kind, boxed_payload(f, value, actual));
}
uintptr_t engine_context(ReadFrame &f) {
  auto ctx = f.persistent ? current_vm_context(f.main) : f.context;
  if (!ctx)
    throw std::runtime_error("Managed reflection requires an engine thread");
  return ctx;
}
uintptr_t managed_string(lua_State *L, ReadFrame &f, int index) {
  size_t len = 0;
  auto raw = lua_tolstring(L, index, &len);
  std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> codec;
  auto utf16 = codec.from_bytes(raw, raw + len);
  if (utf16.size() > INT32_MAX)
    throw std::runtime_error("Managed string too long");
  using NewString = uintptr_t (*)(uintptr_t, size_t, const char16_t *, int);
  auto str = reinterpret_cast<NewString>(f.main.addr(0x6b638b8))(
      engine_context(f), utf16.size(), nullptr, 0);
  if (!str || actual_type(f.db, str) != f.db.find("System.String") ||
      *reinterpret_cast<const int32_t *>(str + 0x10) != int32_t(utf16.size()))
    throw std::runtime_error("Invalid game string allocation");
  memcpy(reinterpret_cast<void *>(str + 0x14), utf16.data(), utf16.size() * 2);
  return str;
}
uintptr_t box_scalar(lua_State *L, ReadFrame &f, int arg, uint32_t expected) {
  auto kind = value_kind(f.db, expected);
  alignas(16) uint8_t encoded[16 * sizeof(float)]{};
  write_scalar(L, arg, kind, encoded);
  using Allocate = uintptr_t (*)(uintptr_t, const uint8_t *, int);
  auto box = reinterpret_cast<Allocate>(f.main.addr(0x6b64068))(
      engine_context(f), f.db.types.data() + 72ull * expected, 0);
  if (!box || actual_type(f.db, box) != expected)
    throw std::runtime_error("Game VM could not box argument");
  auto bytes = boxed_payload(f, box, expected);
  auto n = read<uint32_t>(f.db.impl(72ull * expected), 8);
  if (!n || n > sizeof(encoded) || n > bytes.size())
    throw std::runtime_error("Invalid boxed argument size");
  memcpy(bytes.data(), encoded, n);
  return box;
}
size_t callable_signature(Tdb &d, uint32_t method) {
  auto type = owner(d, method);
  auto declared = d.full_name(type);
  bool game = declared.starts_with("snow.") || declared.starts_with("via.");
  bool collection = declared == "System.Array" ||
                    declared.starts_with("System.Collections.Generic.");
  if (!game && !collection && declared != "System.Math" &&
      declared != "System.String" && declared != "System.Enum")
    throw std::runtime_error("Not a game reflection interface: " + declared);
  auto sig = d.signature_offset(method);
  if (!sig || !read<uint16_t>(d.byte_pool, sig + 2))
    throw std::runtime_error("Missing managed invoker");
  return sig;
}
void normalize_hook_result(lua_State *L, ReadFrame &f, uint32_t ret,
                           ValueKind kind) {
  if (lua_islightuserdata(L, -1)) {
    uint64_t value = reinterpret_cast<uintptr_t>(lua_touserdata(L, -1));
    if (kind == ValueKind::Reference || kind == ValueKind::String) {
      if (!value) {
        lua_pop(L, 1);
        lua_pushnil(L);
        return;
      }
      if (!push_cached_live_object(L, value, ret))
        throw std::runtime_error("Unknown managed hook result");
      lua_remove(L, -2);
    } else {
      lua_pop(L, 1);
      read_scalar(L, kind,
                  {reinterpret_cast<const uint8_t *>(&value), sizeof(value)});
      return;
    }
  }
  if ((kind == ValueKind::Reference || kind == ValueKind::String) &&
      lua_isinteger(L, -1) && lua_tointeger(L, -1) == 0) {
    lua_pop(L, 1);
    lua_pushnil(L);
    return;
  }
  if (auto *object = static_cast<Object *>(luaL_testudata(L, -1, object_mt))) {
    read_box(L, f, object->address, ret);
    lua_remove(L, -2);
    return;
  }
  if (kind == ValueKind::String && lua_type(L, -1) == LUA_TSTRING)
    return;
  if (kind == ValueKind::Reference || kind == ValueKind::String ||
      kind == ValueKind::Void)
    throw std::runtime_error("Incorrect hook return type for " +
                             f.db.full_name(ret));
  if (kind == ValueKind::Bool && lua_isinteger(L, -1)) {
    auto value = lua_tointeger(L, -1);
    if (value != 0 && value != 1)
      throw std::runtime_error("Boolean hook result outside 0..1");
    lua_pop(L, 1);
    lua_pushboolean(L, value);
  }
  alignas(16) uint8_t bytes[16 * sizeof(float)]{};
  write_scalar(L, -1, kind, bytes);
  lua_pop(L, 1);
  read_scalar(L, kind, bytes);
}
uintptr_t method_argument(lua_State *L, ReadFrame &f, int index,
                          uint32_t expected, bool materialize) {
  auto kind = value_kind(f.db, expected);
  if (lua_islightuserdata(L, index)) {
    uint64_t raw = reinterpret_cast<uintptr_t>(lua_touserdata(L, index));
    if (kind == ValueKind::Reference || kind == ValueKind::String) {
      if (!raw)
        return 0;
      if (!push_cached_live_object(L, raw, expected))
        throw std::runtime_error("Unknown managed hook argument");
    } else
      read_scalar(L, kind,
                  {reinterpret_cast<const uint8_t *>(&raw), sizeof(raw)});
    auto result = method_argument(L, f, lua_gettop(L), expected, materialize);
    lua_pop(L, 1);
    return result;
  }
  if (lua_isnil(L, index))
    return 0;
  if (auto *o = static_cast<Object *>(luaL_testudata(L, index, object_mt))) {
    if (!isa(f.db, o->type, expected))
      throw std::runtime_error("Incompatible managed argument");
    return o->address;
  }
  if (kind == ValueKind::String && lua_type(L, index) == LUA_TSTRING)
    return materialize ? managed_string(L, f, index) : 0;
  if (kind == ValueKind::Reference || kind == ValueKind::String)
    throw std::runtime_error("Expected a managed object or nil");
  if (materialize)
    return box_scalar(L, f, index, expected);
  alignas(16) uint8_t scratch[16 * sizeof(float)]{};
  write_scalar(L, index, kind, scratch);
  return 0;
}
int managed_call(lua_State *L, ReadFrame &f, uint32_t method,
                 uintptr_t receiver, int arg) {
  auto &d = f.db;
  auto type = owner(d, method);
  auto sig = callable_signature(d, method);
  auto count = read<uint16_t>(d.byte_pool, sig);
  if (lua_gettop(L) - arg + 1 != count)
    throw std::runtime_error("Incorrect method argument count");
  bool is_static = read<uint16_t>(d.method_impl(method), 4) & 0x10;
  if (!is_static && (!receiver || !isa(d, actual_type(d, receiver), type)))
    throw std::runtime_error("Incorrect method receiver");
  if (is_static)
    receiver = 0;
  auto ctx = engine_context(f);
  auto *exception_frame = *reinterpret_cast<uint8_t **>(ctx + 0x50);
  if (!exception_frame ||
      *reinterpret_cast<uintptr_t *>(exception_frame + 0x18))
    throw std::runtime_error("Game VM has a pending exception");
  if (!d.plain_parameter(sig, 0))
    throw std::runtime_error("Unmanaged or by-reference return type");
  auto ret = d.parameter_type(sig, 0);
  auto result_kind = value_kind(d, ret);
  std::optional<mod_lua::ReflectionHookCall> hooks;
  bool skip = false;
  if (f.persistent && mod_lua::has_reflection_hooks(method)) {
    hooks.emplace(L, method);
    lua_createtable(L, count + 2, 0);
    int table = lua_gettop(L);
    lua_pushlightuserdata(L, reinterpret_cast<void *>(ctx));
    lua_rawseti(L, table, 1);
    int first = is_static ? 2 : 3;
    if (!is_static) {
      push_obj(L, f, receiver, type);
      lua_rawseti(L, table, 2);
    }
    for (unsigned i = 0; i < count; ++i) {
      lua_pushvalue(L, arg + i);
      lua_rawseti(L, table, first + i);
    }
    auto validate = [&](int candidate) {
      lua_rawgeti(L, candidate, 1);
      if (!lua_islightuserdata(L, -1) ||
          lua_touserdata(L, -1) != reinterpret_cast<void *>(ctx))
        throw std::runtime_error("Hook changed the VM context");
      lua_pop(L, 1);
      if (!is_static) {
        lua_rawgeti(L, candidate, 2);
        auto *obj = static_cast<Object *>(luaL_testudata(L, -1, object_mt));
        if (!obj || obj->address != receiver)
          throw std::runtime_error("Hook changed the method receiver");
        lua_pop(L, 1);
      }
      for (unsigned i = 0; i < count; ++i) {
        lua_rawgeti(L, candidate, first + i);
        method_argument(L, f, lua_gettop(L), d.parameter_type(sig, i + 1),
                        false);
        lua_pop(L, 1);
      }
    };
    validate(table);
    skip = hooks->before(table, first + count - 1, validate) ==
           mod_lua::PreHookResult::SkipOriginal;
    for (unsigned i = 0; i < count; ++i) {
      lua_rawgeti(L, table, first + i);
      lua_replace(L, arg + i);
    }
    lua_pop(L, 1);
  }
  std::vector<uintptr_t> args(count);
  for (unsigned i = 0; i < count; ++i) {
    if (!d.plain_parameter(sig, i + 1))
      throw std::runtime_error("Unmanaged or by-reference parameter");
    if (!skip)
      args[i] =
          method_argument(L, f, arg + i, d.parameter_type(sig, i + 1), true);
  }
  using Invoke = uintptr_t (*)(uintptr_t, const uint8_t *, uintptr_t,
                               const uintptr_t *, uint32_t);
  auto result = skip ? uintptr_t{}
                     : reinterpret_cast<Invoke>(f.main.addr(0x6b61f2c))(
                           ctx, d.methods.data() + 12ull * method, receiver,
                           args.data(), count);
  auto &exception = *reinterpret_cast<uintptr_t *>(exception_frame + 0x18);
  if (exception) {
    exception = 0;
    throw std::runtime_error("Managed exception in " + d.full_name(type) + "." +
                             std::string(d.method(method, type)));
  }
  if (result_kind == ValueKind::Void && !hooks)
    return 0;
  if (!skip && !result && result_kind != ValueKind::Reference &&
      result_kind != ValueKind::String && result_kind != ValueKind::Void)
    throw std::runtime_error("Missing boxed return value");
  if (result_kind == ValueKind::Void || skip)
    lua_pushnil(L);
  else
    read_box(L, f, result, ret);
  if (hooks) {
    hooks->after(lua_gettop(L),
                 [&] { normalize_hook_result(L, f, ret, result_kind); });
    if (skip && lua_isnil(L, -1) && result_kind != ValueKind::Void &&
        result_kind != ValueKind::Reference && result_kind != ValueKind::String)
      throw std::runtime_error(
          "Skipped value-returning method needs a post-hook result");
    if (result_kind == ValueKind::Void) {
      lua_pop(L, 1);
      return 0;
    }
  }
  return 1;
}
int get_field(lua_State *L, ReadFrame &f, Object obj, std::string_view name,
              int write_arg = 0) {
  auto &t = f.db;
  auto pos = t.lookup_field(obj.type, name);
  if (pos >= 0) {
    auto id = read<uint32_t>(t.fields, 8ull * pos) & id_mask;
    auto field = t.field(pos, id);
    if (field.flags & (0x10 | 0x40))
      throw std::runtime_error("Expected an instance field");
    auto vt = read<uintptr_t>(t.types, size_t(id) * 72 + 64);
    int64_t offset = int64_t(field.offset) +
                     (vt ? *reinterpret_cast<const int32_t *>(vt - 8) : 0);
    auto size = read<uint32_t>(t.types, size_t(obj.type) * 72 + 20);
    if (offset < 16 || uint64_t(offset) >= size)
      throw std::runtime_error("Invalid instance field offset");
    auto bytes = std::span<uint8_t>(
        reinterpret_cast<uint8_t *>(obj.address + offset), size - offset);
    auto kind = value_kind(t, field.type);
    if (write_arg) {
      if (!f.persistent)
        throw std::runtime_error("Field update requires the live runtime");
      engine_context(f);
      write_scalar(L, write_arg, kind, bytes);
      return 0;
    }
    if (kind == ValueKind::Reference || kind == ValueKind::String)
      read_box(L, f, read<uintptr_t>(bytes, 0), field.type);
    else
      read_scalar(L, kind, bytes);
    return 1;
  }
  if (write_arg)
    throw std::runtime_error("Unknown player field: " + std::string(name));
  lua_pushnil(L);
  return 1;
}
int array_access(lua_State *L, ReadFrame &f, Object o, std::string_view name,
                 int arg) {
  auto size = *reinterpret_cast<const int32_t *>(o.address + 0x1c);
  if (size < 0)
    throw std::runtime_error("Invalid array size");
  if (name == "get_size" || name == "get_Count" || name == "get_Length") {
    lua_pushinteger(L, size);
    return 1;
  }
  auto base = f.db.find("System.Array");
  auto get = f.db.lookup_method(base, "GetValue(System.Int32)");
  if (name == "get_elements") {
    lua_createtable(L, size, 0);
    int table = lua_gettop(L);
    for (int i = 0; i < size; ++i) {
      lua_pushinteger(L, i);
      managed_call(L, f, get, o.address, table + 1);
      lua_rawseti(L, table, i + 1);
      lua_pop(L, 1);
    }
    return 1;
  }
  auto index = luaL_checkinteger(L, arg);
  if (index < 0 || index >= size)
    throw std::runtime_error("Array index out of range");
  if (name == "get_element" || name == "get_Item") {
    lua_pushinteger(L, index);
    return managed_call(L, f, get, o.address, lua_gettop(L));
  }
  auto elem = (read<uint64_t>(f.db.types, 72ull * o.type + 8) >> 19) & id_mask;
  if (!elem)
    throw std::runtime_error("Missing array element type");
  auto kind = value_kind(f.db, elem);
  if (is_live_object(L, arg + 1) || lua_isnil(L, arg + 1))
    lua_pushvalue(L, arg + 1);
  else if (kind == ValueKind::String)
    push_obj(L, f, managed_string(L, f, arg + 1), elem);
  else
    push_obj(L, f, box_scalar(L, f, arg + 1, elem), elem);
  int value = lua_gettop(L);
  lua_pushinteger(L, index);
  return managed_call(
      L, f, f.db.lookup_method(base, "SetValue(System.Object,System.Int32)"),
      o.address, value);
}
bool array_method(std::string_view name) {
  return name == "get_size" || name == "get_Count" || name == "get_Length" ||
         name == "get_elements" || name == "get_element" ||
         name == "get_Item" || name == "set_element" || name == "set_Item";
}
int invoke(lua_State *L) {
  auto &f = frame(L);
  auto obj = object(L, 1);
  std::string_view name = lua_tostring(L, lua_upvalueindex(2));
  int arg = 2;
  if (name == "call") {
    name = luaL_checkstring(L, 2);
    arg = 3;
  }
  try {
    if (name == "get_address") {
      lua_pushinteger(L, obj.address);
      return 1;
    }
    if (array_method(name) && isa(f.db, obj.type, f.db.find("System.Array")))
      return array_access(L, f, obj, name, arg);
    if (name == "get_type_definition") {
      mod_lua::push_type_definition(L, f.db, obj.type);
      return 1;
    }
    if (name == "get_field")
      return get_field(L, f, obj, luaL_checkstring(L, arg));
    if (name == "set_field")
      return get_field(L, f, obj, luaL_checkstring(L, arg), arg + 1);
    auto method = f.db.lookup_method(obj.type, std::string(name),
                                     lua_gettop(L) - arg + 1);
    if (method)
      return managed_call(L, f, method, obj.address, arg);
    // PC scripts also access an existing instance field as
    // get_<field>/set_<field>.
    if (name.starts_with("get_") && lua_gettop(L) < arg) {
      if (f.db.lookup_field(obj.type, name.substr(4)) >= 0)
        return get_field(L, f, obj, name.substr(4));
    }
    if (name.starts_with("set_") && lua_gettop(L) == arg)
      return get_field(L, f, obj, name.substr(4), arg);
    throw std::runtime_error("Unknown method: " + f.db.full_name(obj.type) +
                             "." + std::string(name));
  } catch (const std::exception &e) {
    lua_pushstring(L, e.what());
  }
  return lua_error(L);
}
int singleton(lua_State *L) {
  auto &f = frame(L);
  std::string_view name = luaL_checkstring(L, 1);
  try {
    auto type = f.db.find(name);
    auto method = type ? f.db.lookup_method(type, "get_Instance", 0) : 0;
    if (!method) {
      lua_pushnil(L);
      return 1;
    }
    return managed_call(L, f, method, 0, 2);
  } catch (const std::exception &e) {
    lua_pushstring(L, e.what());
  }
  return lua_error(L);
}
int runtime_type(lua_State *L) {
  auto &f = frame(L);
  const char *name = luaL_checkstring(L, 1);
  try {
    auto id = f.db.find(name);
    if (!id) {
      lua_pushnil(L);
      return 1;
    }
    // System.Object.GetType in this build indexes the managed System.Type table
    // with the actual type ID. Cross-check the table against the running
    // method.
    auto slot = *reinterpret_cast<uintptr_t *>(f.main.addr(0x12674830));
    auto table =
        slot ? *reinterpret_cast<const uintptr_t *const *>(slot) : nullptr;
    if (!table)
      throw std::runtime_error("Runtime type table unavailable");
    auto get = reinterpret_cast<uintptr_t (*)(uintptr_t, uintptr_t)>(
        function(f, 704591));
    if (!f.persistent &&
        get(f.context, f.manager) != table[actual_type(f.db, f.manager)])
      throw std::runtime_error("Runtime type table mismatch");
    push_obj(L, f, table[id], f.db.find("System.Type"));
    return 1;
  } catch (const std::exception &e) {
    lua_pushstring(L, e.what());
  }
  return lua_error(L);
}
int create_array(lua_State *L) {
  auto length = luaL_checkinteger(L, 2);
  if (length < 0 || length > 0x1000000)
    return luaL_error(L, "Invalid managed array length");
  try {
    auto &f = frame(L);
    if (lua_type(L, 1) == LUA_TSTRING) {
      lua_pushvalue(L, lua_upvalueindex(1));
      lua_pushcclosure(L, runtime_type, 1);
      lua_pushvalue(L, 1);
      lua_call(L, 1, 1);
    } else if (lua_istable(L, 1)) {
      lua_getfield(L, 1, "get_runtime_type");
      luaL_checktype(L, -1, LUA_TFUNCTION);
      lua_pushvalue(L, 1);
      lua_call(L, 1, 1);
    } else
      lua_pushvalue(L, 1);
    if (!is_live_object(L, -1))
      throw std::runtime_error("Unknown managed array element type");
    lua_pushinteger(L, length);
    auto method = f.db.lookup_method(
        f.db.find("System.Array"), "CreateInstance(System.Type,System.Int32)");
    return managed_call(L, f, method, 0, lua_gettop(L) - 1);
  } catch (const std::exception &e) {
    lua_pushstring(L, e.what());
  }
  return lua_error(L);
}
} // namespace
ReadFrame &live_frame(lua_State *L) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, &frame_key);
  auto *f = static_cast<ReadFrame *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  if (!f)
    throw std::runtime_error("Method requires the live game runtime");
  return *f;
}
bool is_live_object(lua_State *L, int index) {
  return luaL_testudata(L, index, object_mt) != nullptr;
}
uintptr_t native_method_address(lua_State *L, uint32_t method) {
  return function(live_frame(L), method);
}
int descriptor_function(lua_State *L) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, &frame_key);
  auto *frame = static_cast<ReadFrame *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  auto method = static_cast<uint32_t>(lua_tointeger(L, lua_upvalueindex(2)));
  try {
    if (frame && read<int32_t>(frame->db.methods, 12ull * method + 8))
      lua_pushlightuserdata(L, reinterpret_cast<void *>(function(*frame, method)));
    else
      lua_pushnil(L);
    return 1;
  } catch (const std::exception &e) {
    lua_pushstring(L, e.what());
  }
  return lua_error(L);
}
int access_value_field(lua_State *L, Tdb &t, uint32_t id, std::span<uint8_t> bytes,
                       std::string_view name, bool write) {
  auto pos = t.lookup_field(id, name);
  if (pos < 0)
    throw std::runtime_error("Unknown value field: " + std::string(name));
  auto owner = read<uint32_t>(t.fields, 8ull * pos) & id_mask;
  auto field = t.field(pos, owner);
  if (field.flags & (0x10 | 0x40) || field.offset >= bytes.size())
    throw std::runtime_error("Expected a field inside the value buffer");
  auto data = bytes.subspan(field.offset);
  auto kind = value_kind(t, field.type);
  if (write) {
    write_scalar(L, 4, kind, data);
    return 0;
  }
  read_scalar(L, kind, data);
  return 1;
}
uintptr_t live_object_address(lua_State *L, int index, uint32_t expected) {
  auto *obj = static_cast<Object *>(luaL_testudata(L, index, object_mt));
  if (!obj)
    throw std::runtime_error("Expected a live managed object");
  if (expected && !isa(live_frame(L).db, obj->type, expected))
    throw std::runtime_error("Incompatible managed hook argument");
  return obj->address;
}
bool push_cached_live_object(lua_State *L, uintptr_t address,
                             uint32_t expected) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, &object_cache_key);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  lua_rawgetp(L, -1, reinterpret_cast<void *>(address));
  auto *obj = static_cast<Object *>(luaL_testudata(L, -1, object_mt));
  if (obj && isa(live_frame(L).db, obj->type, expected)) {
    lua_remove(L, -2);
    return true;
  }
  lua_pop(L, 2);
  return false;
}
void validate_reflection_hook(lua_State *L, uint32_t method) {
  auto &f = live_frame(L);
  auto sig = callable_signature(f.db, method);
  auto n = read<uint16_t>(f.db.byte_pool, sig);
  for (unsigned i = 0; i <= n; ++i) {
    if (!f.db.plain_parameter(sig, i))
      throw std::runtime_error(
          "Hook signature contains a pointer or by-reference value");
    (void)value_kind(f.db, f.db.parameter_type(sig, i));
  }
}
void push_live_object(lua_State *L, uintptr_t address, uint32_t expected) {
  push_obj(L, live_frame(L), address, expected);
}
int invoke_descriptor(lua_State *L) {
  try {
    auto &f = live_frame(L);
    auto id = uint32_t(lua_tointeger(L, lua_upvalueindex(2)));
    auto receiver = lua_isnil(L, 2) ? uintptr_t(0) : object(L, 2).address;
    return managed_call(L, f, id, receiver, 3);
  } catch (const std::exception &e) {
    lua_pushstring(L, e.what());
  }
  return lua_error(L);
}
int read_descriptor_field(lua_State *L) {
  try {
    auto &f = live_frame(L);
    uint32_t i = lua_tointeger(L, lua_upvalueindex(2));
    auto declaring = read<uint32_t>(f.db.fields, 8ull * i) & id_mask;
    auto field = f.db.field(i, declaring);
    if ((field.flags & 0x50) == 0x50) {
      auto off = f.db.literal_offset(i);
      if (off < 0) {
        auto str = f.db.name(-int64_t(off));
        lua_pushlstring(L, str.data(), str.size());
      } else
        read_scalar(L, value_kind(f.db, field.type),
                    slice(f.db.byte_pool, off, f.db.byte_pool.size() - off));
      return 1;
    }
    auto o = object(L, 2);
    if (!isa(f.db, o.type, declaring))
      throw std::runtime_error("Incorrect field receiver");
    return get_field(L, f, o, f.db.field(i, declaring).name);
  } catch (const std::exception &e) {
    lua_pushstring(L, e.what());
  }
  return lua_error(L);
}
int descriptor_runtime_type(lua_State *L) {
  try {
    auto &f = live_frame(L);
    auto id = uint32_t(lua_tointeger(L, lua_upvalueindex(2)));
    auto slot = *reinterpret_cast<uintptr_t *>(f.main.addr(0x12674830));
    auto table =
        slot ? *reinterpret_cast<const uintptr_t *const *>(slot) : nullptr;
    if (!table)
      throw std::runtime_error("Runtime type table unavailable");
    push_obj(L, f, table[id], f.db.find("System.Type"));
    return 1;
  } catch (const std::exception &e) {
    lua_pushstring(L, e.what());
  }
  return lua_error(L);
}
int primary_camera(lua_State *L) {
  try {
    auto &f = frame(L);
    auto ctx = f.persistent ? current_vm_context(f.main) : f.context;
    if (!ctx)
      throw std::runtime_error("Camera lookup requires an engine thread");
    auto slot = *reinterpret_cast<const uintptr_t *>(f.main.addr(0x12674898));
    auto scene = slot ? *reinterpret_cast<const uintptr_t *>(slot) : 0;
    using Get = uintptr_t (*)(uintptr_t, uintptr_t);
    auto view =
        scene ? reinterpret_cast<Get>(function(f, 734924))(ctx, scene) : 0;
    auto cam = view ? reinterpret_cast<Get>(function(f, 734346))(ctx, view) : 0;
    push_obj(L, f, cam, f.db.parameter_type(f.db.signature_offset(734346), 0));
    return 1;
  } catch (const std::exception &e) {
    lua_pushstring(L, e.what());
  }
  return lua_error(L);
}
uintptr_t current_vm_context(const Module &m) {
  size_t words = 0;
  auto *layout = tls::observed_layout(words);
  if (!layout || words <= 4)
    return 0;
  int32_t index;
  memcpy(&index, reinterpret_cast<const char *>(layout) + 0x20, sizeof(index));
  auto vm = *reinterpret_cast<const uintptr_t *>(m.addr(0x12950358));
  if (!vm || index < 0 ||
      uint32_t(index) >= *reinterpret_cast<uint32_t *>(vm + 0x410))
    return 0;
  auto table = *reinterpret_cast<const uintptr_t *const *>(vm + 0x408);
  return table ? table[index] : 0;
}
void drain_managed_releases() {
  std::vector<std::pair<uintptr_t, uintptr_t>> batch;
  {
    std::lock_guard lock(releases_mutex);
    batch.swap(releases);
  }
  for (auto [release, obj] : batch)
    reinterpret_cast<void (*)(uintptr_t)>(release)(obj);
}
void open_live_read(lua_State *L, ReadFrame &f) {
  lua_pushlightuserdata(L, &f);
  lua_rawsetp(L, LUA_REGISTRYINDEX, &frame_key);
  lua_newtable(L);
  lua_newtable(L);
  lua_pushliteral(L, "v");
  lua_setfield(L, -2, "__mode");
  lua_setmetatable(L, -2);
  lua_rawsetp(L, LUA_REGISTRYINDEX, &object_cache_key);
  luaL_newmetatable(L, object_mt);
  lua_pushlightuserdata(L, &f);
  lua_pushcclosure(
      L,
      [](lua_State *s) {
        auto &f = frame(s);
        auto o = object(s, 1);
        try {
          if (lua_isinteger(s, 2) &&
              isa(f.db, o.type, f.db.find("System.Array")))
            return array_access(s, f, o, "get_element", 2);
          if (lua_type(s, 2) == LUA_TSTRING &&
              f.db.lookup_field(o.type, lua_tostring(s, 2)) >= 0)
            return get_field(s, f, o, lua_tostring(s, 2));
        } catch (const std::exception &e) {
          lua_pushstring(s, e.what());
          return lua_error(s);
        }
        lua_pushvalue(s, lua_upvalueindex(1));
        lua_pushvalue(s, 2);
        lua_pushcclosure(s, invoke, 2);
        return 1;
      },
      1);
  lua_setfield(L, -2, "__index");
  lua_pushlightuserdata(L, &f);
  lua_pushcclosure(
      L,
      [](lua_State *s) {
        try {
          auto &f = frame(s);
          auto o = object(s, 1);
          if (lua_isinteger(s, 2) &&
              isa(f.db, o.type, f.db.find("System.Array")))
            return array_access(s, f, o, "set_element", 2);
          return get_field(s, f, o, luaL_checkstring(s, 2), 3);
        } catch (const std::exception &e) {
          lua_pushstring(s, e.what());
        }
        return lua_error(s);
      },
      1);
  lua_setfield(L, -2, "__newindex");
  lua_pushcfunction(L, [](lua_State *s) {
    lua_pushboolean(s, object(s, 1).address == object(s, 2).address);
    return 1;
  });
  lua_setfield(L, -2, "__eq");
  lua_pushcfunction(L, [](lua_State *s) {
    auto *p = static_cast<Object *>(luaL_checkudata(s, 1, object_mt));
    if (p->release) {
      std::lock_guard lock(releases_mutex);
      releases.emplace_back(p->release, p->address);
      p->release = 0;
    }
    return 0;
  });
  lua_setfield(L, -2, "__gc");
  lua_pop(L, 1);
  lua_getglobal(L, "sdk");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
  }
  lua_pushcfunction(L, [](lua_State *s) {
    lua_pushboolean(s, is_live_object(s, 1));
    return 1;
  });
  lua_setfield(L, -2, "is_managed_object");
  lua_pushlightuserdata(L, &f);
  lua_pushcclosure(
      L,
      [](lua_State *s) {
        auto &f = frame(s);
        auto ctx = f.persistent ? current_vm_context(f.main) : f.context;
        if (ctx)
          lua_pushlightuserdata(s, reinterpret_cast<void *>(ctx));
        else
          lua_pushnil(s);
        return 1;
      },
      1);
  lua_setfield(L, -2, "get_thread_context");
  lua_pushlightuserdata(L, &f);
  lua_pushcclosure(
      L,
      [](lua_State *s) {
        luaL_checktype(s, 1, LUA_TSTRING);
        try {
          auto &f = frame(s);
          push_obj(s, f, managed_string(s, f, 1), f.db.find("System.String"));
          return 1;
        } catch (const std::exception &e) {
          lua_pushstring(s, e.what());
        }
        return lua_error(s);
      },
      1);
  lua_setfield(L, -2, "create_managed_string");
  for (auto [key, func] :
       {std::pair<const char *, lua_CFunction>{"typeof", runtime_type},
        {"get_managed_singleton", singleton},
        {"get_primary_camera", primary_camera},
        {"create_managed_array", create_array}}) {
    lua_pushlightuserdata(L, &f);
    lua_pushcclosure(L, func, 1);
    lua_setfield(L, -2, key);
  }
  lua_pushlightuserdata(L, &f);
  lua_pushcclosure(
      L,
      [](lua_State *s) {
        auto &f = frame(s);
        const char *name = luaL_checkstring(s, 1);
        try {
          mod_lua::push_type_definition(s, f.db, f.db.find(name));
          return 1;
        } catch (const std::exception &e) {
          lua_pushstring(s, e.what());
        }
        return lua_error(s);
      },
      1);
  lua_setfield(L, -2, "find_type_definition");
  lua_pushlightuserdata(L, &f);
  lua_pushliteral(L, "call");
  lua_pushcclosure(L, invoke, 2);
  lua_setfield(L, -2, "call_object_func");
  lua_setglobal(L, "sdk");
}
} // namespace rise
