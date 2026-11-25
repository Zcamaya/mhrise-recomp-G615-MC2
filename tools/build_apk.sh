#!/usr/bin/env bash
# Compiles the APK (start screen in Java + NativeActivity with libnier.so and libnnsdk.so) into build/mhriseport.apk.
# Needs: Android SDK with NDK 28.2.13676358, build-tools 35.0.0 and platform android-33; a JDK (11+); cmake; python3.
# Run tools/setup.sh once first (third-party sources).
#
# Signing: by default the Android debug keystore (~/.android/debug.keystore, created if missing). For your own release
# key: KEYSTORE=<file> KS_PASS=<password> [KEY_ALIAS=<alias>] tools/build_apk.sh
# RELEASE=1: the build for players (the release workflow): -O3 without debug info, unused code and duplicate functions
# dropped, symbols stripped, and an APK that isn't debuggable (ART runs a debuggable app's Java slower). Without it,
# RelWithDebInfo and a debuggable APK, for testing.
# PACKAGE=<name>: the same app under another package name (with LABEL=<name on the home screen>). By default this source
# uses io.github.iamwilmar.mhriseport.camon50ultra to install alongside the official app. The Java classes keep their
# own package; only what Android sees changes, and each package gets its own settings and drivers.
# Compila la APK. Firma con la clave de depuración de Android salvo que se pase KEYSTORE.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}}"
NDK="${NDK_DIR:-$SDK/ndk/28.2.13676358}"  # this exact NDK (ANDROID_NDK_HOME often points to another)
BT="$SDK/build-tools/35.0.0"
JAR="$SDK/platforms/android-33/android.jar"
D8="$BT/d8"
APKSIGNER="$BT/apksigner"
if [ ! -e "$D8" ] && [ -f "$BT/d8.bat" ]; then D8="$BT/d8.bat"; fi
if [ ! -e "$APKSIGNER" ] && [ -f "$BT/apksigner.bat" ]; then APKSIGNER="$BT/apksigner.bat"; fi
RELEASE="${RELEASE:-0}"
if [ "$RELEASE" = 1 ]; then
  BUILD_TYPE=Release BUILD="$ROOT/build/android-release" DEBUG_FLAG=""
else
  BUILD_TYPE=RelWithDebInfo BUILD="$ROOT/build/android" DEBUG_FLAG="--debug-mode"
fi
STAGE="$ROOT/build/apk"
APK="${APK_OUT:-$ROOT/build/mhriseport.apk}"
PACKAGE="${PACKAGE:-}"
if [ -z "$PACKAGE" ]; then
  PACKAGE=io.github.iamwilmar.mhriseport.camon50ultra
  LABEL_DEFAULT="MH Rise port (Camon)"
else
  LABEL_DEFAULT="MH Rise port ($PACKAGE)"
fi
LABEL="${LABEL:-$LABEL_DEFAULT}"
# LUA_LAB=1: with the Lua runtime for mods (Lua, Dear ImGui, FreeType and JSON fetched by CMake; the releases have it)
LUA_LAB="${LUA_LAB:-0}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2> /dev/null || echo 4)}"

for need in "$NDK/build/cmake/android.toolchain.cmake" "$BT/aapt2" "$JAR"; do
  [ -e "$need" ] || { echo "missing: $need (see README: Building)" >&2; exit 1; }
done
[ -d "$ROOT/third_party/sirit" ] || { echo "run tools/setup.sh first" >&2; exit 1; }

mkdir -p "$BUILD"
cmake -S "$ROOT/runtime" -B "$BUILD" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DMHRISE_LUA_DEBUG="$LUA_LAB" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_NDK="$NDK" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DANDROID_STL=c++_static > /dev/null
if ! cmake --build "$BUILD" -j"$JOBS" > "$BUILD/build.log" 2>&1; then
  grep -E "error" "$BUILD/build.log" >&2 || tail -40 "$BUILD/build.log" >&2
  echo "build failed (full log: $BUILD/build.log)" >&2
  exit 1
fi

rm -rf "$STAGE"
mkdir -p "$STAGE/lib/arm64-v8a" "$STAGE/classes"
# Start screen (SetupActivity: find the user's game files, then start): javac + d8 into classes.dex.
python3 "$ROOT/tools/mod_archive_dependency.py" "$STAGE"
JAVA_SOURCES=()
while IFS= read -r -d '' source; do JAVA_SOURCES+=("$source"); done < <(find "$ROOT/android/java" -name '*.java' -print0)
if [[ "$OSTYPE" == msys* || "$OSTYPE" == cygwin* ]]; then
  JAVAC_CLASSPATH="$(cygpath -w "$JAR");$(cygpath -w "$STAGE/mod-archive.jar")"
else
  JAVAC_CLASSPATH="$JAR:$STAGE/mod-archive.jar"
fi
javac -Xlint:-options --release 11 -encoding UTF-8 -classpath "$JAVAC_CLASSPATH" -d "$STAGE/classes" "${JAVA_SOURCES[@]}"
jar cf "$STAGE/classes.jar" -C "$STAGE/classes" .
"$D8" --release --min-api 30 --lib "$JAR" --output "$STAGE" "$STAGE/mod-archive.jar" "$STAGE/classes.jar"
rm "$STAGE/classes.jar"
cp "$BUILD/libnier.so" "$BUILD/libnnsdk.so" "$STAGE/lib/arm64-v8a/"
# libadrenotools' hooks (a driver of the user's own, runtime/src/nnsdk/nvn/driver.h): they must be in nativeLibraryDir.
HOOK_LIBS=()
while IFS= read -r -d '' hook; do HOOK_LIBS+=("$hook"); done < <(find "$BUILD/adrenotools" -name 'lib*hook*.so' -print0)
cp "${HOOK_LIBS[@]}" "$STAGE/lib/arm64-v8a/"
NOTICE_ARGS=()
if [ "$LUA_LAB" = 1 ]; then NOTICE_ARGS+=("$BUILD/_deps"); fi
python3 "$ROOT/tools/apk_notices.py" "$STAGE/assets/licenses" "${NOTICE_ARGS[@]}"
# How the app knows it has the Lua runtime (GameActivity.luaBuild)
if [ "$LUA_LAB" = 1 ]; then echo 1 > "$STAGE/assets/lua_build"; fi
# The Citrus curves for Steam saves (SaveData.java)
cp "$ROOT/android/assets/citrus_curves.bin" "$STAGE/assets/"
"$BT/aapt2" compile --dir "$ROOT/android/res" -o "$STAGE/res.zip"
MANIFEST="$ROOT/android/AndroidManifest.xml" RENAME=()
if [ -n "$PACKAGE" ]; then
  MANIFEST="$STAGE/AndroidManifest.xml"
  sed "s|android:label=\"MH Rise port\"|android:label=\"$LABEL\"|" \
    "$ROOT/android/AndroidManifest.xml" > "$MANIFEST"
  RENAME=(--rename-manifest-package "$PACKAGE")
fi
"$BT/aapt2" link --manifest "$MANIFEST" -I "$JAR" -R "$STAGE/res.zip" --auto-add-overlay \
  "${RENAME[@]}" $DEBUG_FLAG -o "$STAGE/base.apk"
(cd "$STAGE" && jar uf base.apk lib classes.dex assets)
"$BT/zipalign" -f -p 4 "$STAGE/base.apk" "$STAGE/aligned.apk"

if [ -n "${KEYSTORE:-}" ]; then
  "$APKSIGNER" sign --ks "$KEYSTORE" --ks-pass env:KS_PASS ${KEY_ALIAS:+--ks-key-alias "$KEY_ALIAS"} \
    --out "$APK" "$STAGE/aligned.apk"
else
  DEBUG_KS="$HOME/.android/debug.keystore"
  if [ ! -f "$DEBUG_KS" ]; then
    mkdir -p "$(dirname "$DEBUG_KS")"
    keytool -genkeypair -keystore "$DEBUG_KS" -storepass android -keypass android -alias androiddebugkey \
      -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" > /dev/null
  fi
  "$APKSIGNER" sign --ks "$DEBUG_KS" --ks-pass pass:android --key-pass pass:android --out "$APK" "$STAGE/aligned.apk"
fi
echo "$APK"
