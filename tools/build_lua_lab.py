"""Build the side-by-side Windows research APK with the v1.0.85 release renderer."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
from zipfile import ZipFile, ZIP_DEFLATED
from mod_archive_dependency import prepare as prepare_mod_archives


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for opt in ("sdk", "ndk", "jdk", "release-apk"):
        p.add_argument("--" + opt, type=Path, required=True)
    p.add_argument("--keystore", type=Path, default=Path.home()/".android/debug.keystore")
    args = p.parse_args()
    if hashlib.sha256(args.release_apk.read_bytes()).hexdigest() != "7cd885bf01c1d2be814ec59b88020c947f959dd7087dbe1911bd32dba149956c":
        p.error("Expected the original mhriseport-v1.0.85.apk")
    root = Path(__file__).resolve().parents[1]
    build = root / "build/tdb-android"
    cmake = args.sdk / "cmake/3.22.1/bin"
    tools = args.sdk / "build-tools/36.0.0"
    platform = args.sdk / "platforms/android-34/android.jar"
    jdk = args.jdk / "bin"
    os.environ["JAVA_HOME"] = str(args.jdk)
    run(cmake/"cmake.exe", "-S", root/"runtime", "-B", build, "-G", "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={cmake/'ninja.exe'}",
        f"-DCMAKE_TOOLCHAIN_FILE={args.ndk/'build/cmake/android.toolchain.cmake'}",
        "-DCMAKE_CXX_FLAGS=--driver-mode=g++", "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-30",
        "-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release", "-DMHRISE_LUA_DEBUG=ON", "-DMHRISE_LOADER_ONLY=ON")
    run(cmake/"cmake.exe", "--build", build, "--target", "nier", "nier_loader", "--parallel", "12")
    target = root / "build/mhrise-lua-lab.apk"
    with tempfile.TemporaryDirectory(dir=build) as tmp:
        staging = Path(tmp)
        classes = staging / "classes"
        classes.mkdir()
        prepare_mod_archives(staging)
        archive_jar = staging / "mod-archive.jar"
        run(jdk/"javac.exe", "--release", "11", "-encoding", "UTF-8", "-cp", os.pathsep.join(map(str,(platform,archive_jar))), "-d", classes,
            *sorted((root/"android/java").rglob("*.java")))
        jar = staging/"classes.jar"
        run(jdk/"jar.exe", "cf", jar, "-C", classes, ".")
        run(jdk/"java.exe", "-cp", tools/"lib/d8.jar", "com.android.tools.r8.D8", "--min-api", "30",
            "--lib", platform, "--output", staging, jar, archive_jar)
        manifest = (root/"android/AndroidManifest.xml").read_text(encoding="utf-8")
        (staging/"AndroidManifest.xml").write_text(manifest.replace('android:label="MH Rise port"',
            'android:label="MH Rise Lua Lab"'), encoding="utf-8")
        base = staging/"base.apk"
        run(tools/"aapt2.exe", "compile", "--dir", root/"android/res", "-o", staging/"res.zip")
        run(tools/"aapt2.exe", "link", "--manifest", staging/"AndroidManifest.xml", "-I", platform,
            "-R", staging/"res.zip", "--auto-add-overlay", "--debug-mode", "--rename-manifest-package",
            "io.github.iamwilmar.mhriseport.lab", "-o", base)
        loader = "lib/arm64-v8a/libnier.so"
        with ZipFile(args.release_apk) as original, ZipFile(base, "a", ZIP_DEFLATED) as apk:
            for name in original.namelist():
                if name.startswith(("assets/", "lib/")) and name != loader:
                    apk.writestr(name, original.read(name))
            apk.write(build/"libnier.so", loader)
            apk.write(staging/"lib/arm64-v8a/libarchive-jni.so", "lib/arm64-v8a/libarchive-jni.so")
            apk.write(root/"android/assets/licenses/mod-archives.txt", "assets/licenses/mod-archives.txt")
            apk.write(staging/"classes.dex", "classes.dex")
            apk.write(build/"_deps/rise_imgui-src/LICENSE.txt","assets/licenses/imgui.txt")
            apk.write(build/"_deps/freetype-src/docs/FTL.TXT","assets/licenses/freetype.txt")
            apk.write(build/"_deps/rise_json-src/LICENSE.MIT","assets/licenses/json.txt")
            lua_header = (build/"_deps/lua54-src/src/lua.h").read_text()
            apk.writestr("assets/licenses/lua.txt", lua_header[lua_header.rindex("/*"):].split("*/")[0] + "*/\n")
        aligned = staging/"aligned.apk"
        run(tools/"zipalign.exe", "-f", "-P", "16", "4", base, aligned)
        signer = [jdk/"java.exe", "-jar", tools/"lib/apksigner.jar"]
        signed = staging / target.name
        run(*signer, "sign", "--ks", args.keystore, "--ks-pass", "pass:android", "--out", signed, aligned)
        run(*signer, "verify", signed)
        signed.replace(target)
        signature = signed.with_suffix(".apk.idsig")
        if signature.exists():
            signature.replace(target.with_suffix(".apk.idsig"))
    print(target)


if __name__ == "__main__":
    main()
