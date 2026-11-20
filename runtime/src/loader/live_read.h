#pragma once
#include "loader/tdb71.h"
struct lua_State;
namespace rise {
struct ReadFrame {
  const Module &main;
  Tdb &db;
  uintptr_t context;
  uintptr_t manager;
  uintptr_t encoded_base;
  bool persistent = false;
};
void open_live_read(lua_State *L, ReadFrame &frame);
void validate_reflection_hook(lua_State *L, uint32_t method);
void push_live_object(lua_State *, uintptr_t, uint32_t);
bool is_live_object(lua_State *, int);
int invoke_descriptor(lua_State *);
int read_descriptor_field(lua_State *);
int descriptor_runtime_type(lua_State *);
uintptr_t current_vm_context(const Module &);
void drain_managed_releases();
} // namespace rise
