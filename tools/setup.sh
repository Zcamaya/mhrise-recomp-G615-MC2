#!/usr/bin/env bash
# Fetches third_party/ (not in git): the sources used by Eden's shader recompiler, all pinned to known versions, with
# this project's patches on top (tools/patches/eden-*.patch).
# Prepara third_party/ (no va en git): las fuentes que usa el recompilador de shaders de Eden, fijadas a versiones
# conocidas, con los parches propios encima.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP="$ROOT/third_party"
mkdir -p "$TP"

clone() {  # <folder> <repo> <tag>: shallow clone of a fixed version
  [ -d "$TP/$1" ] || git clone -q --depth 1 --branch "$3" "$2" "$TP/$1"
}

# Eden (GPL-3.0-or-later): only src/shader_recompiler and the headers in src/common, at the commit this port was built
# and tested with.
EDEN_COMMIT=060026dc2d1daa749cf0b39378f132f506cbfd93
# The patches are applied when Eden is fetched, so a checkout made before a patch (or this script's own edit below)
# changed would keep building without it: a stamp of what was applied, and a fresh fetch when it no longer matches.
EDEN_STAMP="$EDEN_COMMIT $(cat "$ROOT"/tools/patches/eden-*.patch "$0" | sha256sum | cut -c1-16)"
if [ -d "$TP/eden" ] && [ "$(cat "$TP/eden/.mhrise-stamp" 2>/dev/null)" != "$EDEN_STAMP" ]; then
  echo "third_party/eden was set up with older patches: fetching it again" >&2
  rm -rf "$TP/eden"
fi
if [ ! -d "$TP/eden" ]; then
  git init -q "$TP/eden"
  git -C "$TP/eden" remote add origin https://git.eden-emu.dev/eden-emu/eden.git
  git -C "$TP/eden" sparse-checkout set src/shader_recompiler src/common
  git -C "$TP/eden" fetch -q --depth 1 --filter=blob:none origin "$EDEN_COMMIT"
  git -C "$TP/eden" checkout -q FETCH_HEAD
  for patch in "$ROOT"/tools/patches/eden-*.patch; do git -C "$TP/eden" apply "$patch"; done
  # The same Mali drivers (has_broken_pack_half) abort on a plain f32 to f16 conversion in a vertex stage too ("Cannot
  # select: zero_extend (fp_round ...)" in __start__position_shader, a Mali-G57): F2F.F16.F32 goes through the integer
  # packing as well, its low half taken back as an f16. A sed rather than a patch, as it's one line in the function;
  # setup stops if that line isn't there any more.
  convert="$TP/eden/src/shader_recompiler/backend/spirv/emit_spirv_convert.cpp"
  sed -i '/^Id EmitConvertF16F32(EmitContext& ctx, Id value) {$/,/^}$/ s/^\( *\)return ctx\.OpFConvert(ctx\.F16\[1\], value);$/\1if (ctx.profile.has_broken_pack_half \&\& ctx.stage != Stage::Fragment \&\& ctx.stage != Stage::Compute) {\n\1    const Id bits{EmitPackHalf2x16(ctx, ctx.OpCompositeConstruct(ctx.F32[2], value, ctx.Const(0.0f)))};\n\1    return ctx.OpCompositeExtract(ctx.F16[1], ctx.OpBitcast(ctx.F16[2], bits), 0U);\n\1}\n&/' "$convert"
  grep -q has_broken_pack_half "$convert" || { echo "Eden's EmitConvertF16F32 changed: tools/setup.sh can't apply the Mali fix" >&2; exit 1; }
  echo "$EDEN_STAMP" > "$TP/eden/.mhrise-stamp"
fi
# Same versions as Eden's cpmfile.json.
clone fmt https://github.com/fmtlib/fmt.git 12.1.0
clone sirit https://github.com/eden-emulator/sirit.git v1.0.5
clone SPIRV-Headers https://github.com/KhronosGroup/SPIRV-Headers.git vulkan-sdk-1.4.341.0
# sirit expects SPIRV-Headers as a submodule in externals/.
rmdir "$TP/sirit/externals/SPIRV-Headers" 2> /dev/null || true
[ -e "$TP/sirit/externals/SPIRV-Headers" ] || ln -s ../../SPIRV-Headers "$TP/sirit/externals/SPIRV-Headers"

# Official Boost 1.92.0: only each module's headers (everything used is header-only).
if [ ! -d "$TP/boost" ]; then
  tmp="$TP/boost-dl"
  mkdir -p "$tmp"
  # GitHub sometimes answers 502 for a moment: retry instead of failing the build.
  curl -fsSL --retry 5 --retry-delay 10 --retry-all-errors -o "$tmp/boost.tar.xz" \
    https://github.com/boostorg/boost/releases/download/boost-1.92.0/boost-1.92.0-cmake.tar.xz
  if tar --version 2> /dev/null | grep -q GNU; then
    tar xf "$tmp/boost.tar.xz" -C "$tmp" --wildcards --wildcards-match-slash '*/libs/*/include/*'
  else
    tar xf "$tmp/boost.tar.xz" -C "$tmp" --include='*/libs/*/include/*'
  fi
  mv "$tmp/boost-1.92.0" "$TP/boost"
  rm -rf "$tmp"
fi
echo "third_party ready"
