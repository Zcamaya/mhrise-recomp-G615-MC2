"""Prepare the pinned Java/JNI archive reader for the two non-Gradle APK builders."""
import hashlib
from pathlib import Path
import sys
import tempfile
from urllib.request import urlopen
from zipfile import ZipFile

VERSION = "1.1.6"
SHA256 = "06d97a37872a9574ceb2bf94a3ad8dc38fcc89a60f2424067b20eb5efaa62189"

def prepare(staging):
    cache = Path(__file__).resolve().parents[1] / "build/mod-dependencies"
    cache.mkdir(parents=True, exist_ok=True)
    aar = cache / f"libarchive-{VERSION}.aar"
    if not aar.is_file() or hashlib.sha256(aar.read_bytes()).hexdigest() != SHA256:
        url = f"https://repo.maven.apache.org/maven2/me/zhanghai/android/libarchive/library/{VERSION}/library-{VERSION}.aar"
        with tempfile.TemporaryDirectory(dir=cache) as tmp:
            download = Path(tmp) / "archive.aar"
            with urlopen(url, timeout=60) as response:
                download.write_bytes(response.read())
            if hashlib.sha256(download.read_bytes()).hexdigest() != SHA256:
                raise ValueError("Unexpected libarchive Android artifact checksum")
            download.replace(aar)
    with ZipFile(aar) as source:
        (staging / "mod-archive.jar").write_bytes(source.read("classes.jar"))
        native = staging / "lib/arm64-v8a/libarchive-jni.so"
        native.parent.mkdir(parents=True, exist_ok=True)
        native.write_bytes(source.read("jni/arm64-v8a/libarchive-jni.so"))

if __name__ == "__main__":
    prepare(Path(sys.argv[1]))
