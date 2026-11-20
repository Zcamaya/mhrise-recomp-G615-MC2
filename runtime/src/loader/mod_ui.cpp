#include "loader/mod_ui.h"
#include "loader/mod_font.h"
#include "loader/lua_values.h"
#include "loader/application_lua.h"
#include <algorithm>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <atomic>
#include <backends/imgui_impl_opengl3.h>
#include <chrono>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>
#include <jni.h>
#include <lua.hpp>
#include <memory>
#include <vector>

namespace {
thread_local bool drawing = false;
enum class Scope { Tree, Tooltip, Window, Id, Style };
thread_local std::vector<Scope> scopes;
void close_scope() {
  switch (scopes.back()) {
  case Scope::Tree: ImGui::TreePop(); break;
  case Scope::Tooltip: ImGui::EndTooltip(); break;
  case Scope::Window: ImGui::End(); break;
  case Scope::Id: ImGui::PopID(); break;
  case Scope::Style: ImGui::PopStyleVar(); break;
  }
  scopes.pop_back();
}
ImGuiContext *ui = nullptr;
std::atomic<bool> visible = false;
std::atomic<bool> needs_render = false;
bool had_geometry = false;
const char *visible_text(const char *text) {
  if (auto *glyphs = static_cast<ModFontGlyphs *>(ImGui::GetIO().UserData))
    glyphs->observe(text);
  return text;
}
enum Widget {
  Text,
  TextColored,
  SameLine,
  Spacing,
  Separator,
  Tree,
  Pop,
  Button,
  Check,
  DragInt,
  SliderInt,
  DragFloat,
  Slider,
  Input,
  NewLine,
  Hovered,
  Tooltip,
  EndTooltip,
  BeginWindow,
  EndWindow,
  NextPos,
  NextSize,
  DisplaySize,
  WindowPos,
  MouseDown,
  PushId,
  PopId,
  PushStyle,
  PopStyle,
  Indent,
  Unindent,
  Combo
};
ImVec2 vec2(lua_State *L, int index) {
  float v[2];
  if (mod_lua::read_components(L, index, false, v, 2))
    return {v[0], v[1]};
  luaL_checktype(L, index, LUA_TTABLE);
  for (int axis = 0; axis < 2; ++axis) {
    lua_rawgeti(L, index, axis + 1);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_getfield(L, index, axis ? "y" : "x");
    }
    v[axis] = luaL_checknumber(L, -1);
    lua_pop(L, 1);
  }
  return {v[0], v[1]};
}
int widget(lua_State *L) {
  if (!drawing)
    return luaL_error(L, "imgui requires an active on_frame or on_draw_ui callback");
  auto kind = static_cast<Widget>(lua_tointeger(L, lua_upvalueindex(1)));
  switch (kind) {
  case Indent:
    ImGui::Indent(luaL_optnumber(L, 1, 0));
    return 0;
  case Unindent:
    ImGui::Unindent(luaL_optnumber(L, 1, 0));
    return 0;
  case PushId:
    if (lua_isinteger(L, 1)) ImGui::PushID(static_cast<int>(lua_tointeger(L, 1)));
    else ImGui::PushID(luaL_checkstring(L, 1));
    scopes.push_back(Scope::Id);
    return 0;
  case PopId:
    if (scopes.empty() || scopes.back() != Scope::Id)
      return luaL_error(L, "pop_id without matching push_id");
    close_scope();
    return 0;
  case PushStyle: {
    int var = luaL_checkinteger(L, 1);
    if (var < 0 || var >= ImGuiStyleVar_COUNT)
      return luaL_argerror(L, 1, "unknown style variable");
    if (ImGui::GetStyleVarInfo(var)->Count == 2) ImGui::PushStyleVar(var, vec2(L, 2));
    else ImGui::PushStyleVar(var, float(luaL_checknumber(L, 2)));
    scopes.push_back(Scope::Style);
    return 0;
  }
  case PopStyle: {
    int count = luaL_optinteger(L, 1, 1);
    if (count < 0 || count > std::count(scopes.begin(), scopes.end(), Scope::Style))
      return luaL_error(L, "pop_style_var exceeds the number pushed");
    ImGui::PopStyleVar(count);
    while (count--) {
      auto it = std::find(scopes.rbegin(), scopes.rend(), Scope::Style);
      scopes.erase(std::next(it).base());
    }
    return 0;
  }
  case MouseDown: {
    int button = luaL_checkinteger(L, 1);
    if (button < 0 || button >= ImGuiMouseButton_COUNT)
      return luaL_argerror(L, 1, "invalid mouse button");
    lua_pushboolean(L, ImGui::IsMouseDown(button));
    return 1;
  }
  case WindowPos:
  case DisplaySize: {
    auto v = kind == DisplaySize ? ImGui::GetIO().DisplaySize : ImGui::GetWindowPos();
    float xy[] = {v.x, v.y};
    mod_lua::push_components(L, false, xy, 2);
    return 1;
  }
  case NextPos:
  case NextSize: {
    auto value = vec2(L, 1);
    int cond = luaL_optinteger(L, 2, 0);
    if (cond != 0 && cond != ImGuiCond_Always && cond != ImGuiCond_Once &&
        cond != ImGuiCond_FirstUseEver && cond != ImGuiCond_Appearing)
      return luaL_argerror(L, 2, "invalid window condition");
    if (kind == NextPos)
      ImGui::SetNextWindowPos(value, cond, lua_isnoneornil(L, 3) ? ImVec2{} : vec2(L, 3));
    else
      ImGui::SetNextWindowSize(value, cond);
    return 0;
  }
  case NewLine:
    ImGui::NewLine();
    return 0;
  case Hovered:
    lua_pushboolean(L, ImGui::IsItemHovered(luaL_optinteger(L, 1, 0)));
    return 1;
  case Tooltip:
    ImGui::BeginTooltip();
    scopes.push_back(Scope::Tooltip);
    return 0;
  case EndTooltip:
    if (scopes.empty() || scopes.back() != Scope::Tooltip)
      return luaL_error(L, "end_tooltip without begin_tooltip");
    close_scope();
    return 0;
  case EndWindow:
    if (scopes.empty() || scopes.back() != Scope::Window)
      return luaL_error(L, "end_window without matching begin_window");
    close_scope();
    return 0;
  case SameLine:
    ImGui::SameLine();
    return 0;
  case Spacing:
    ImGui::Spacing();
    return 0;
  case Separator:
    ImGui::Separator();
    return 0;
  case Pop:
    if (scopes.empty() || scopes.back() != Scope::Tree)
      return luaL_error(L, "tree_pop without an open node");
    close_scope();
    return 0;
  default:
    break;
  }
  const char *label = visible_text(luaL_checkstring(L, 1));
  switch (kind) {
  case BeginWindow: {
    if (!*label)
      return luaL_error(L, "begin_window requires a nonempty name");
    bool closable = lua_isboolean(L, 2);
    bool open = !closable || lua_toboolean(L, 2);
    if (open) {
      ImGui::Begin(label, closable ? &open : nullptr, luaL_optinteger(L, 3, 0));
      // REFramework returns the open state, including for collapsed windows.
      scopes.push_back(Scope::Window);
    }
    lua_pushboolean(L, open);
    return 1;
  }
  case Combo: {
    luaL_checktype(L, 3, LUA_TTABLE);
    lua_settop(L, 3);
    lua_pushvalue(L, 2);
    lua_rawget(L, 3);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_pushnil(L);
      if (lua_next(L, 3)) {
        lua_pushvalue(L, -2);
        lua_replace(L, 2);
        lua_remove(L, -2);
      } else
        lua_pushnil(L);
    }
    auto preview = lua_tostring(L, -1);
    bool changed = false;
    if (ImGui::BeginCombo(label, visible_text(preview ? preview : ""))) {
      lua_pushnil(L);
      while (lua_next(L, 3)) {
        if (lua_type(L, -1) == LUA_TSTRING &&
            ImGui::Selectable(visible_text(lua_tostring(L, -1)), lua_rawequal(L, -2, 2))) {
          lua_pushvalue(L, -2);
          lua_replace(L, 2);
          changed = true;
        }
        lua_pop(L, 1);
      }
      ImGui::EndCombo();
    }
    lua_pushboolean(L, changed);
    lua_pushvalue(L, 2);
    return 2;
  }
  case Text:
    ImGui::TextUnformatted(label);
    return 0;
  case TextColored:
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(luaL_checkinteger(L, 2)),
                       "%s", label);
    return 0;
  case Button:
    lua_pushboolean(L, ImGui::Button(label));
    return 1;
  case Tree: {
    bool open = ImGui::TreeNode(label);
    if (open)
      scopes.push_back(Scope::Tree);
    lua_pushboolean(L, open);
    return 1;
  }
  case Check: {
    bool v = lua_toboolean(L, 2);
    bool changed = ImGui::Checkbox(label, &v);
    lua_pushboolean(L, changed);
    lua_pushboolean(L, v);
    return 2;
  }
  case DragInt: {
    int v = luaL_checkinteger(L, 2);
    bool changed =
        ImGui::DragInt(label, &v, luaL_optnumber(L, 3, 1),
                       luaL_optinteger(L, 4, 0), luaL_optinteger(L, 5, 0));
    lua_pushboolean(L, changed);
    lua_pushinteger(L, v);
    return 2;
  }
  case SliderInt: {
    int v = luaL_checkinteger(L, 2);
    bool changed = ImGui::SliderInt(label, &v, luaL_checkinteger(L, 3),
                                    luaL_checkinteger(L, 4), luaL_optstring(L, 5, "%d"));
    lua_pushboolean(L, changed);
    lua_pushinteger(L, v);
    return 2;
  }
  case DragFloat:
  case Slider: {
    float v = luaL_checknumber(L, 2);
    bool changed = kind == Slider
                       ? ImGui::SliderFloat(label, &v, luaL_checknumber(L, 3),
                                            luaL_checknumber(L, 4))
                       : ImGui::DragFloat(label, &v, luaL_optnumber(L, 3, 1),
                                          luaL_optnumber(L, 4, 0),
                                          luaL_optnumber(L, 5, 0));
    lua_pushboolean(L, changed);
    lua_pushnumber(L, v);
    return 2;
  }
  case Input: {
    size_t n;
    const char *value = visible_text(luaL_optlstring(L, 2, "", &n));
    struct Edit {
      std::vector<char> bytes;
      int from{}, to{};
    } edit;
    edit.bytes.resize(n + 1);
    memcpy(edit.bytes.data(), value, n);
    auto callback = [](ImGuiInputTextCallbackData *data) {
      auto &edit = *static_cast<Edit *>(data->UserData);
      if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        edit.bytes.resize(data->BufTextLen + 1);
        data->Buf = edit.bytes.data();
      }
      edit.from = data->SelectionStart;
      edit.to = data->SelectionEnd;
      return 0;
    };
    int flags = luaL_optinteger(L, 3, 0) | ImGuiInputTextFlags_CallbackResize |
                ImGuiInputTextFlags_CallbackAlways;
    bool changed = ImGui::InputText(label, edit.bytes.data(), edit.bytes.size(),
                                    flags, callback, &edit);
    lua_pushboolean(L, changed);
    lua_pushstring(L, edit.bytes.data());
    lua_pushinteger(L, edit.from);
    lua_pushinteger(L, edit.to);
    return 4;
  }
  default:
    return luaL_error(L, "Invalid ImGui widget");
  }
}
std::unique_ptr<ModFontGlyphs> glyphs;
GLint max_texture_size = 0;
std::chrono::steady_clock::time_point last;

enum class Primitive { Text, Line, FilledRect, OutlineRect, FilledCircle, OutlineCircle, FilledQuad, OutlineQuad };
int primitive(lua_State *L) {
  if (!drawing) return luaL_error(L, "draw requires an active frame");
  auto kind = static_cast<Primitive>(lua_tointeger(L, lua_upvalueindex(1)));
  auto *list = ImGui::GetBackgroundDrawList();
  auto point = [&](int i) {
    return ImVec2{float(luaL_checknumber(L, i)), float(luaL_checknumber(L, i + 1))};
  };
  if (kind == Primitive::Text) {
    list->AddText(point(2), luaL_checkinteger(L, 4), visible_text(luaL_checkstring(L, 1)));
    return 0;
  }
  auto a = point(1);
  if (kind == Primitive::FilledCircle || kind == Primitive::OutlineCircle) {
    float radius = luaL_checknumber(L, 3);
    ImU32 color = luaL_checkinteger(L, 4);
    int segments = luaL_optinteger(L, 5, 32);
    if (kind == Primitive::FilledCircle) list->AddCircleFilled(a, radius, color, segments);
    else list->AddCircle(a, radius, color, segments);
    return 0;
  }
  auto b = point(3);
  if (kind == Primitive::FilledQuad || kind == Primitive::OutlineQuad) {
    auto c = point(5), d = point(7);
    ImU32 color = luaL_checkinteger(L, 9);
    if (kind == Primitive::FilledQuad) list->AddQuadFilled(a, b, c, d, color);
    else list->AddQuad(a, b, c, d, color);
    return 0;
  }
  ImU32 color = luaL_checkinteger(L, 5);
  if (kind == Primitive::Line) list->AddLine(a, b, color);
  else if (kind == Primitive::FilledRect) list->AddRectFilled(a, {a.x+b.x, a.y+b.y}, color);
  else list->AddRect(a, {a.x+b.x, a.y+b.y}, color);
  return 0;
}
} // namespace
int mod_lua::call_ui_callback(lua_State *L) {
  auto base = scopes.size();
  int status = lua_pcall(L, 0, 0, 0);
  while (scopes.size() > base)
    close_scope();
  return status;
}
bool mod_lua::ui_visible() { return visible.load(); }
void mod_lua::service_ui_frame() {
  // BeginRendering owns script execution. The GLES thread consumes only completed
  // draw data, under the same mutex, and may skip superseded game frames.
  if (ui) ImGui::SetCurrentContext(ui);
  if (!ui || !ImGui::GetIO().Fonts->IsBuilt() ||
      ImGui::GetIO().DisplaySize.x <= 0 || ImGui::GetIO().DisplaySize.y <= 0) {
    draw_frame();
    return;
  }
  auto now = std::chrono::steady_clock::now();
  auto &io = ImGui::GetIO();
  io.DeltaTime = std::max(0.0001f, std::chrono::duration<float>(now - last).count());
  last = now;
  ImGui::NewFrame();
  drawing = true;
  if (visible) {
    ImGui::SetNextWindowSize({io.DisplaySize.x * .82f, io.DisplaySize.y * .8f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({io.DisplaySize.x * .5f, io.DisplaySize.y * .5f}, ImGuiCond_FirstUseEver, {.5f,.5f});
    bool open = true;
    if (ImGui::Begin("MOD UI", &open, ImGuiWindowFlags_NoBringToFrontOnFocus))
      draw_ui();
    ImGui::End();
    if (!open) visible = false;
  }
  draw_frame();
  drawing = false;
  ImGui::Render();
  bool geometry = ImGui::GetDrawData()->TotalVtxCount > 0;
  if (geometry || had_geometry) needs_render = true;
  had_geometry = geometry;
}
void mod_lua::open_imgui(lua_State *L) {
  lua_newtable(L);
  for (auto [name, kind] : {std::pair{"text", Text},
                            {"text_colored", TextColored},
                            {"same_line", SameLine},
                            {"spacing", Spacing},
                            {"separator", Separator},
                            {"tree_node", Tree},
                            {"tree_pop", Pop},
                            {"button", Button},
                            {"checkbox", Check},
                            {"drag_int", DragInt},
                            {"slider_int", SliderInt},
                            {"drag_float", DragFloat},
                            {"slider_float", Slider},
                            {"input_text", Input},
                            {"new_line", NewLine},
                            {"is_item_hovered", Hovered},
                            {"begin_tooltip", Tooltip},
                            {"end_tooltip", EndTooltip},
                            {"begin_window", BeginWindow},
                            {"end_window", EndWindow},
                            {"set_next_window_pos", NextPos},
                            {"set_next_window_size", NextSize},
                            {"get_display_size", DisplaySize},
                            {"get_window_pos", WindowPos},
                            {"is_mouse_down", MouseDown},
                            {"push_id", PushId},
                            {"pop_id", PopId},
                            {"push_style_var", PushStyle},
                            {"pop_style_var", PopStyle},
                            {"indent", Indent},
                            {"unindent", Unindent},
                            {"combo", Combo}}) {
    lua_pushinteger(L, kind);
    lua_pushcclosure(L, widget, 1);
    lua_setfield(L, -2, name);
  }
  lua_setglobal(L, "imgui");
  lua_newtable(L);
  for (auto [name, kind] : {std::pair{"text", Primitive::Text}, {"line", Primitive::Line},
                            {"filled_rect", Primitive::FilledRect}, {"outline_rect", Primitive::OutlineRect},
                            {"filled_circle", Primitive::FilledCircle}, {"outline_circle", Primitive::OutlineCircle},
                            {"filled_quad", Primitive::FilledQuad}, {"outline_quad", Primitive::OutlineQuad}}) {
    lua_pushinteger(L, static_cast<int>(kind));
    lua_pushcclosure(L, primitive, 1);
    lua_setfield(L, -2, name);
  }
  lua_setglobal(L, "draw");
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_iamwilmar_mhriseport_ModUi_nativeCreate(JNIEnv *, jclass,
                                                       jfloat density) {
  std::lock_guard lock(mod_lua::lua_mutex());
  ui = ImGui::CreateContext();
  ImGui::SetCurrentContext(ui);
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  ImGui::GetStyle().ScaleAllSizes(density);
  const char *font = "/system/fonts/NotoSansCJK-Regular.ttc";
  if (auto f = fopen(font, "rb")) {
    fclose(f);
    glyphs = std::make_unique<ModFontGlyphs>(*io.Fonts);
    if (io.Fonts->AddFontFromFileTTF(font, 17 * density, nullptr, glyphs->ranges()))
      io.UserData = glyphs.get();
    else
      glyphs.reset();
  } else {
    io.Fonts->AddFontDefault();
    io.FontGlobalScale = density;
  }
  if (!io.Fonts->Build()) {
    __android_log_write(ANDROID_LOG_ERROR, "RiseLua",
                        "System font atlas failed to build");
    io.Fonts->Clear();
    io.UserData = nullptr;
    glyphs.reset();
    io.Fonts->AddFontDefault();
    io.FontGlobalScale = density;
    if (!io.Fonts->Build()) {
      ImGui::DestroyContext(ui);
      ui = nullptr;
      return;
    }
  }
  ImGui_ImplOpenGL3_Init("#version 300 es");
  ImGui_ImplOpenGL3_NewFrame();
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  last = std::chrono::steady_clock::now();
  needs_render = true;
  had_geometry = false;
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_iamwilmar_mhriseport_ModUi_nativeSize(JNIEnv *, jclass, jint w, jint h) {
  std::lock_guard lock(mod_lua::lua_mutex());
  if (ui) ImGui::GetIO().DisplaySize = ImVec2(w, h);
  needs_render = true;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_iamwilmar_mhriseport_ModUi_nativePending(JNIEnv *, jclass) {
  return needs_render.load();
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_iamwilmar_mhriseport_ModUi_nativeMenu(JNIEnv *, jclass, jboolean open) {
  visible = open;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_iamwilmar_mhriseport_ModUi_nativeMenuOpen(JNIEnv *, jclass) {
  return visible.load();
}
extern "C" JNIEXPORT jint JNICALL
Java_io_github_iamwilmar_mhriseport_ModUi_nativeFrame(JNIEnv *, jclass, jint w,
                                                      jint h) {
  std::lock_guard lock(mod_lua::lua_mutex());
  needs_render = false;
  glViewport(0, 0, w, h);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  if (!ui || w <= 0 || h <= 0)
    return 0;
  ImGui::SetCurrentContext(ui);
  auto &io = ImGui::GetIO();
  if (auto *data = ImGui::GetDrawData())
    ImGui_ImplOpenGL3_RenderDrawData(data);
  if (glyphs) {
    if (!io.Fonts->IsBuilt()) return 0;
    auto update = glyphs->update(*io.Fonts, max_texture_size);
    if (update != ModFontGlyphs::Update::Unchanged) {
      if (update != ModFontGlyphs::Update::Built)
        __android_log_write(ANDROID_LOG_ERROR, "RiseLua", "Could not expand MOD font atlas");
      if (update == ModFontGlyphs::Update::Failed) return 0;
      ImGui_ImplOpenGL3_DestroyFontsTexture();
      ImGui_ImplOpenGL3_CreateFontsTexture();
    }
  }
  return (visible ? 1 : 0) | (io.WantTextInput ? 2 : 0);
}
extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_iamwilmar_mhriseport_ModUi_nativeWindowAt(JNIEnv *, jclass, jfloat x, jfloat y) {
  std::lock_guard lock(mod_lua::lua_mutex());
  if (!ui || ui->FrameCount == 0) return false;
  ImGui::SetCurrentContext(ui);
  ImGuiWindow *window = nullptr;
  // Hit-test the new contact, independently of any window another finger is moving.
  ImGui::FindHoveredWindowEx({x, y}, true, &window, nullptr);
  return window && window->Active && !window->IsFallbackWindow;
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_iamwilmar_mhriseport_ModUi_nativeTouch(JNIEnv *, jclass,
                                                      jfloat x, jfloat y,
                                                      jboolean down) {
  std::lock_guard lock(mod_lua::lua_mutex());
  if (!ui)
    return;
  ImGui::SetCurrentContext(ui);
  auto &io = ImGui::GetIO();
  io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
  io.AddMousePosEvent(x, y);
  io.AddMouseButtonEvent(0, down);
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_iamwilmar_mhriseport_ModUi_nativeChar(JNIEnv *, jclass,
                                                     jint cp) {
  std::lock_guard lock(mod_lua::lua_mutex());
  if (!ui)
    return;
  ImGui::SetCurrentContext(ui);
  auto &io = ImGui::GetIO();
  if (cp == 8) {
    io.AddKeyEvent(ImGuiKey_Backspace, true);
    io.AddKeyEvent(ImGuiKey_Backspace, false);
  } else if (cp > 0)
    io.AddInputCharacter(cp);
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_iamwilmar_mhriseport_ModUi_nativeClose(JNIEnv *, jclass) {
  std::lock_guard lock(mod_lua::lua_mutex());
  if (!ui)
    return;
  ImGui::SetCurrentContext(ui);
  ImGui_ImplOpenGL3_Shutdown();
  ImGui::DestroyContext(ui);
  glyphs.reset();
  ui = nullptr;
  needs_render = false;
}
