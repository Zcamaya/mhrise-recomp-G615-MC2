#include "loader/mod_ui.h"
#include "loader/mod_font.h"
#include "loader/application_lua.h"
#include <GLES3/gl3.h>
#include <android/log.h>
#include <atomic>
#include <backends/imgui_impl_opengl3.h>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>
#include <jni.h>
#include <lua.hpp>
#include <memory>
#include <vector>

namespace {
thread_local bool drawing = false;
enum class Scope { Tree, Tooltip, Window };
thread_local std::vector<Scope> scopes;
void close_scope() {
  switch (scopes.back()) {
  case Scope::Tree: ImGui::TreePop(); break;
  case Scope::Tooltip: ImGui::EndTooltip(); break;
  case Scope::Window: ImGui::End(); break;
  }
  scopes.pop_back();
}
ImGuiContext *requested = nullptr;
std::atomic<bool> visible = false;
std::condition_variable_any completed;
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
  Combo
};
int widget(lua_State *L) {
  if (!drawing)
    return luaL_error(L, "imgui widget outside on_draw_ui");
  auto kind = static_cast<Widget>(lua_tointeger(L, lua_upvalueindex(1)));
  switch (kind) {
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
      if (open)
        scopes.push_back(Scope::Window);
      else
        ImGui::End();
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
thread_local ImGuiContext *ui = nullptr;
thread_local std::unique_ptr<ModFontGlyphs> glyphs;
thread_local GLint max_texture_size = 0;
thread_local std::chrono::steady_clock::time_point last;
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
  if (!requested)
    return;
  ImGui::SetCurrentContext(requested);
  drawing = true;
  draw_ui();
  drawing = false;
  requested = nullptr;
  completed.notify_one();
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
                            {"combo", Combo}}) {
    lua_pushinteger(L, kind);
    lua_pushcclosure(L, widget, 1);
    lua_setfield(L, -2, name);
  }
  lua_setglobal(L, "imgui");
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
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  last = std::chrono::steady_clock::now();
  visible = true;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_iamwilmar_mhriseport_ModUi_nativeFrame(JNIEnv *, jclass, jint w,
                                                      jint h) {
  std::unique_lock lock(mod_lua::lua_mutex());
  if (!ui || w <= 0 || h <= 0)
    return false;
  ImGui::SetCurrentContext(ui);
  auto &io = ImGui::GetIO();
  if (glyphs) {
    if (!io.Fonts->IsBuilt()) return false;
    auto update = glyphs->update(*io.Fonts, max_texture_size);
    if (update != ModFontGlyphs::Update::Unchanged) {
      if (update != ModFontGlyphs::Update::Built)
        __android_log_write(ANDROID_LOG_ERROR, "RiseLua", "Could not expand MOD font atlas");
      if (update == ModFontGlyphs::Update::Failed) return false;
      ImGui_ImplOpenGL3_DestroyFontsTexture();
      ImGui_ImplOpenGL3_CreateFontsTexture();
    }
  }
  io.DisplaySize = ImVec2(w, h);
  auto now = std::chrono::steady_clock::now();
  io.DeltaTime =
      std::max(0.0001f, std::chrono::duration<float>(now - last).count());
  last = now;
  glViewport(0, 0, w, h);
  glClearColor(.08f, .08f, .08f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_NewFrame();
  ImGui::NewFrame();
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("MOD UI", nullptr,
               ImGuiWindowFlags_NoDecoration |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoSavedSettings);
  requested = ui;
  // Let Lua read the live game VM on UpdateScene. GLES stays on its owning
  // thread.
  if (!completed.wait_for(lock, std::chrono::milliseconds(100),
                          [] { return !requested; })) {
    requested = nullptr;
    ImGui::TextUnformatted("Waiting for game update");
  }
  ImGui::End();
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  return io.WantTextInput;
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_iamwilmar_mhriseport_ModUi_nativeTouch(JNIEnv *, jclass,
                                                      jfloat x, jfloat y,
                                                      jboolean down) {
  if (!ui)
    return;
  ImGui::SetCurrentContext(ui);
  auto &io = ImGui::GetIO();
  io.AddMousePosEvent(x, y);
  io.AddMouseButtonEvent(0, down);
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_iamwilmar_mhriseport_ModUi_nativeChar(JNIEnv *, jclass,
                                                     jint cp) {
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
  visible = false;
}
