// nn::oe: estado de la aplicación en el sistema. Lo que el juego usa como reloj tiene que avanzar:
// CRI Mana (los vídeos) mide la reproducción con GetProgramTotalActiveTime (main+0xa5154c), y con 0
// fijo el vídeo se queda en los primeros frames.
//
// La Flip es una consola en modo portátil que nunca cambia de estado: rendimiento normal, siempre en primer
// plano y sin mensajes del sistema (los eventos de aviso existen pero no se señalan nunca). Grabación, capturas
// y la imagen de copyright no tienen dónde ir.

#include <sys/system_properties.h>
#include <unistd.h>
#include <time.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "nnsdk/runtime.h"
#include "nnsdk/types.h"

namespace nn {
namespace os {
class SystemEvent;  // su primer miembro es el SystemEventType: misma dirección
}
namespace time {
struct PosixTime {
    int64_t value;
};
}  // namespace time
namespace settings {
enum Language : int;
struct LanguageCode {  // como en settings.cpp
    char string[8];
    static LanguageCode Make(Language language);
};
}  // namespace settings
}  // namespace nn

namespace nn::oe {

enum OperationMode : uint8_t { OperationMode_Handheld = 0, OperationMode_Console = 1 };
enum PerformanceMode : int { PerformanceMode_Normal = 0, PerformanceMode_Boost = 1 };
enum FocusState : uint8_t { FocusState_InFocus = 1, FocusState_OutOfFocus = 2, FocusState_Background = 3 };
enum CpuBoostMode : int {};
enum FocusHandlingMode : int {};
enum WindowOriginMode : int {};

struct DisplayVersion {
    char value[16];
};

namespace {

int64_t monotonic_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t{ts.tv_sec} * 1'000'000'000 + ts.tv_nsec;
}

const int64_t g_start_ns = monotonic_ns();

// El SystemEvent de los mensajes del sistema (en el sdk, un global que devuelve GetNotificationMessageEvent).
// os.cpp crea su evento al primer uso, sin señalar.
alignas(16) unsigned char g_message_event[0x40];

// Configuración de rendimiento por modo (la de serie del portátil: 1020 MHz de CPU, 384 de GPU).
int g_performance_config[2] = {0x00010000, 0x00020000};

// El idioma que pide el juego: el de Android traducido a los que trae MH Rise en su control.nacp (japonés,
// inglés de EE. UU., francés, alemán, italiano, español de España y de Latinoamérica, ruso, coreano y chino
// tradicional y simplificado). Se puede forzar con <datos>/idioma.txt (un código de Nintendo: "es", "es-419"…).
std::string desired_language() {
    const char* env = getenv("NIER_DATA");
    std::string path = std::string(env ? env : "/sdcard/NieR") + "/idioma.txt";
    if (FILE* f = fopen(path.c_str(), "r")) {
        char code[16] = {};
        bool ok = fscanf(f, "%15s", code) == 1;
        fclose(f);
        if (ok) return code;
    }
    char locale[PROP_VALUE_MAX] = {};
    if (__system_property_get("persist.sys.locale", locale) <= 0) __system_property_get("ro.product.locale", locale);
    std::string l = locale;  // "es-CO", "zh-Hant-TW", "en-US"…
    std::string lang = l.substr(0, l.find('-')), rest = l.size() > lang.size() ? l.substr(lang.size()) : "";
    if (lang == "ja" || lang == "fr" || lang == "de" || lang == "it" || lang == "ru" || lang == "ko") return lang;
    if (lang == "es") return rest.empty() || rest == "-ES" ? "es" : "es-419";
    if (lang == "zh")
        return rest.find("Hant") != std::string::npos || rest.find("TW") != std::string::npos ||
                       rest.find("HK") != std::string::npos || rest.find("MO") != std::string::npos
                   ? "zh-Hant"
                   : "zh-Hans";
    return "en-US";
}

}  // namespace

void Initialize() {}

// Tiempo activo desde que arrancó el programa (en la consola no cuenta el tiempo en suspensión).
TimeSpan GetProgramTotalActiveTime() { return TimeSpan{monotonic_ns() - g_start_ns}; }

settings::LanguageCode GetDesiredLanguage() {
    static const std::string code = [] {
        std::string c = desired_language();
        printf("[oe] idioma: %s\n", c.c_str());
        return c;
    }();
    settings::LanguageCode out{};
    strncpy(out.string, code.c_str(), sizeof out.string - 1);
    return out;
}

void GetDisplayVersion(DisplayVersion* out) {
    memset(out, 0, sizeof *out);
    strcpy(out->value, "16.0.1");
}

}  // namespace nn::oe

namespace nnsdk {
bool docked() {
    static const bool on = [] {
        const char* data = getenv("NIER_DATA");
        const bool d = access((std::string(data ? data : ".") + "/dock_mode").c_str(), F_OK) == 0;
        printf("[oe] mode: %s\n", d ? "docked (dock_mode)" : "handheld");
        return d;
    }();
    return on;
}
}  // namespace nnsdk

namespace nn::oe {

OperationMode GetOperationMode() { return nnsdk::docked() ? OperationMode_Console : OperationMode_Handheld; }
PerformanceMode GetPerformanceMode() { return nnsdk::docked() ? PerformanceMode_Boost : PerformanceMode_Normal; }
FocusState GetCurrentFocusState() { return FocusState_InFocus; }
bool IsCpuOverclockEnabled() { return false; }

// The handheld's 720p, or the TV's 1080p when docked. With <datos>/ultrawide (the phone's width / height, written by the
// settings menu; read at start-up) the screen is as wide as the phone's: the game sizes its picture from this, so a
// 20:9 phone gets 1600x720 instead of the 16:9 picture stretched.
void GetDefaultDisplayResolution(int* width, int* height) {
    *height = nnsdk::docked() ? 1080 : 720;
    *width = nnsdk::docked() ? 1920 : 1280;
    static const float wide = [] {
        const char* data = getenv("NIER_DATA");
        float r = 0.0f;
        if (FILE* f = fopen((std::string(data ? data : ".") + "/ultrawide").c_str(), "r")) {
            if (fscanf(f, "%f", &r) != 1) r = 0.0f;
            fclose(f);
        }
        if (r < 16.0f / 9.0f || r > 3.0f) r = 0.0f;  // (narrower than 16:9, or not a ratio: as the console)
        if (r > 0.0f) printf("[oe] ultrawide: screen %.3f:1\n", r);
        return r;
    }();
    if (wide > 0.0f) *width = (static_cast<int>(*height * wide) + 7) & ~7;
}
void GetDefaultDisplayResolutionChangeEvent(os::SystemEvent*) {}

os::SystemEventType* GetNotificationMessageEvent() { return reinterpret_cast<os::SystemEventType*>(g_message_event); }
bool TryPopNotificationMessage(uint32_t*) { return false; }

int GetPerformanceConfiguration(PerformanceMode mode) { return g_performance_config[mode == PerformanceMode_Boost]; }
void SetPerformanceConfiguration(PerformanceMode mode, int config) {
    g_performance_config[mode == PerformanceMode_Boost] = config;
}
void SetCpuBoostMode(CpuBoostMode) {}

void SetFocusHandlingMode(FocusHandlingMode) {}
void SetResumeNotificationEnabled(bool) {}
void SetOperationModeChangedNotificationEnabled(bool) {}
void SetPerformanceModeChangedNotificationEnabled(bool) {}
void EnterExitRequestHandlingSection() {}
void LeaveExitRequestHandlingSection() {}

void FinishStartupLogo() {}
void EnableRecording() {}
void DisableRecording() {}
void EnableScreenShot() {}
void DisableScreenShot() {}
void InitializeCopyrightFrameBuffer(void*, size_t) {}
void SetCopyrightImage(const void*, size_t, int, int, int, int, WindowOriginMode) {}
void SetCopyrightVisibility(bool) {}

// Aviso de mantenimiento de los servidores: no hay servidores.
void SendServerMaintenanceOverlayNotification(time::PosixTime, time::PosixTime) {}

}  // namespace nn::oe
