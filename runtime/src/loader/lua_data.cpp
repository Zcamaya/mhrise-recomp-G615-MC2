#include "loader/lua_data.h"
#include <fstream>
#include <lua.hpp>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <unistd.h>

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
enum class Op { LoadString, DumpString, LoadFile, DumpFile, Read, Write, Glob };
std::string string_at(lua_State *L, int arg) {
  size_t len = 0;
  auto *s = luaL_checklstring(L, arg, &len);
  return {s, len};
}
Json encode(lua_State *L, int index, std::set<const void *> &visiting) {
  switch (lua_type(L, index)) {
  case LUA_TBOOLEAN:
    return bool(lua_toboolean(L, index));
  case LUA_TNUMBER:
    if (lua_isinteger(L, index))
      return lua_tointeger(L, index);
    return lua_tonumber(L, index);
  case LUA_TSTRING:
    return string_at(L, index);
  case LUA_TTABLE:
    break;
  default:
    return nullptr;
  }
  index = lua_absindex(L, index);
  auto identity = lua_topointer(L, index);
  if (!visiting.insert(identity).second)
    throw std::runtime_error("Cyclic JSON table");
  Json object, arr;
  size_t count = 0, length = lua_rawlen(L, index);
  bool array = true;
  lua_pushnil(L);
  while (lua_next(L, index)) {
    auto value = encode(L, -1, visiting);
    if (lua_isinteger(L, -2)) {
      auto n = lua_tointeger(L, -2);
      if (n > 0 && size_t(n) <= length)
        arr[size_t(n) - 1] = value;
      else
        array = false;
      object[std::to_string(n)] = std::move(value);
    } else if (lua_type(L, -2) == LUA_TSTRING) {
      array = false;
      object[string_at(L, -2)] = std::move(value);
    } else
      throw std::runtime_error("Invalid JSON object key");
    ++count;
    lua_pop(L, 1);
  }
  visiting.erase(identity);
  return array && count == length ? arr : object;
}
void decode(lua_State *L, const Json &j) {
  if (j.is_null())
    lua_pushnil(L);
  else if (j.is_boolean())
    lua_pushboolean(L, j.get<bool>());
  else if (j.is_number_integer())
    lua_pushinteger(L, j.get<lua_Integer>());
  else if (j.is_number_float())
    lua_pushnumber(L, j.get<double>());
  else if (j.is_string()) {
    auto &s = j.get_ref<const std::string &>();
    lua_pushlstring(L, s.data(), s.size());
  } else if (j.is_array()) {
    lua_createtable(L, j.size(), 0);
    for (size_t i = 0; i < j.size(); ++i) {
      decode(L, j[i]);
      lua_rawseti(L, -2, i + 1);
    }
  } else {
    lua_createtable(L, 0, j.size());
    for (auto &[k, v] : j.items()) {
      lua_pushlstring(L, k.data(), k.size());
      decode(L, v);
      lua_rawset(L, -3);
    }
  }
}
fs::path path_under(const fs::path &root, std::string name) {
  std::replace(name.begin(), name.end(), '\\', '/');
  fs::path rel(name);
  if (name.find('\0') != name.npos || rel.is_absolute() ||
      name.find(':') != name.npos)
    throw std::runtime_error("Expected a relative data path");
  auto base = fs::weakly_canonical(root);
  auto path = fs::weakly_canonical(base / rel);
  auto r = path.lexically_relative(base);
  if (r.empty() || r.is_absolute() || *r.begin() == "..")
    throw std::runtime_error("Path outside mod data");
  return path;
}
void write_file(const fs::path &path, const std::string &contents) {
  fs::create_directories(path.parent_path());
  auto pattern = path.string() + ".XXXXXX";
  auto fd = mkstemp(pattern.data());
  if (fd < 0)
    throw std::runtime_error("Cannot create temporary mod data file");
  bool ok = true;
  size_t offset = 0;
  while (offset < contents.size()) {
    ssize_t n = ::write(fd, contents.data() + offset, contents.size() - offset);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      ok = false;
      break;
    }
    offset += n;
  }
  if (close(fd) != 0)
    ok = false;
  if (!ok || rename(pattern.c_str(), path.c_str()) != 0) {
    unlink(pattern.c_str());
    throw std::runtime_error("Cannot store mod data");
  }
}
int process(lua_State *L) {
  auto op = static_cast<Op>(lua_tointeger(L, lua_upvalueindex(1)));
  int top = lua_gettop(L);
  try {
    fs::path root = lua_tostring(L, lua_upvalueindex(2));
    if (op == Op::LoadString) {
      decode(L, Json::parse(string_at(L, 1)));
      return 1;
    }
    if (op == Op::DumpString || op == Op::DumpFile) {
      bool file = op == Op::DumpFile;
      std::set<const void *> visiting;
      int indent = luaL_optinteger(L, file ? 3 : 2, file ? 4 : -1);
      auto contents = encode(L, file ? 2 : 1, visiting).dump(indent);
      if (file) {
        write_file(path_under(root / "data", string_at(L, 1)), contents);
        lua_pushboolean(L, 1);
      } else
        lua_pushlstring(L, contents.data(), contents.size());
      return 1;
    }
    if (op == Op::Glob) {
      std::string modifier = luaL_optstring(L, 2, "");
      if (!modifier.empty() && modifier != "$autorun")
        throw std::runtime_error("Unknown mod search root");
      auto dir = root / (modifier == "$autorun" ? "autorun" : "data");
      fs::create_directories(dir);
      std::regex pattern(string_at(L, 1));
      std::vector<std::string> found;
      for (auto &entry : fs::recursive_directory_iterator(dir))
        if (entry.is_regular_file()) {
          auto name = entry.path().lexically_relative(dir).generic_string();
          if (std::regex_match(name, pattern))
            found.push_back(std::move(name));
        }
      std::sort(found.begin(), found.end());
      lua_createtable(L, found.size(), 0);
      for (size_t i = 0; i < found.size(); ++i) {
        lua_pushlstring(L, found[i].data(), found[i].size());
        lua_rawseti(L, -2, i + 1);
      }
      return 1;
    }
    auto path = path_under(root / "data", string_at(L, 1));
    if (op == Op::Write) {
      write_file(path, string_at(L, 2));
      return 0;
    }
    std::ifstream in(path, std::ios::binary);
    if (op == Op::LoadFile) {
      if (!in)
        lua_pushnil(L);
      else
        decode(L, Json::parse(in));
      return 1;
    }
    if (!in && fs::exists(path))
      throw std::runtime_error("Cannot read mod data");
    std::string data{std::istreambuf_iterator<char>(in), {}};
    lua_pushlstring(L, data.data(), data.size());
    return 1;
  } catch (const std::exception &e) {
    lua_settop(L, top);
    if (op == Op::LoadString || op == Op::LoadFile) {
      lua_pushnil(L);
      return 1;
    }
    if (op == Op::DumpString) {
      lua_pushliteral(L, "");
      return 1;
    }
    if (op == Op::DumpFile) {
      lua_pushboolean(L, 0);
      return 1;
    }
    lua_pushstring(L, e.what());
  }
  return lua_error(L);
}
} // namespace
void mod_lua::open_data(lua_State *L, const std::filesystem::path &root) {
  auto add = [&](const char *name, Op op) {
    lua_pushinteger(L, static_cast<int>(op));
    lua_pushstring(L, root.c_str());
    lua_pushcclosure(L, process, 2);
    lua_setfield(L, -2, name);
  };
  lua_newtable(L);
  add("load_string", Op::LoadString);
  add("dump_string", Op::DumpString);
  add("load_file", Op::LoadFile);
  add("dump_file", Op::DumpFile);
  lua_setglobal(L, "json");
  lua_newtable(L);
  add("glob", Op::Glob);
  add("read", Op::Read);
  add("write", Op::Write);
  lua_setglobal(L, "fs");
}
