"""Allowlisted Windows release staging. No build, game launch, deletion or Git writes.

Requires a fresh output path. Creates a companion snapshot from CURRENT tracked
and addable source (not HEAD), retaining uncommitted changes and their hashes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
NVIDIA = ("sl.interposer.dll sl.common.dll sl.dlss.dll nvngx_dlss.dll "
          "sl.dlss_d.dll nvngx_dlssd.dll sl.dlss_g.dll nvngx_dlssg.dll "
          "sl.reflex.dll sl.pcl.dll NvLowLatencyVk.dll").split()
NVIDIA_NOTICES = ("streamline-license.txt streamline-3rd-party-licenses.md "
                  "nvngx_dlss.license.txt reflex.license.txt dlssnr-3rd-party-notice.txt").split()
MINGW = "libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll".split()
MSVC = "msvcp140.dll vcruntime140.dll vcruntime140_1.dll".split()
SYSTEM = set(("advapi32 avrt bcrypt cfgmgr32 comctl32 comdlg32 crypt32 d3d11 "
              "d3d12 dbghelp dxgi dwmapi gdi32 imm32 kernel32 ntdll ole32 oleaut32 "
              "opengl32 powrprof psapi rpcrt4 secur32 setupapi shell32 shlwapi "
              "user32 userenv uxtheme version winmm wintrust ws2_32 msvcrt "
              "winhttp wtsapi32 hid normaliz shcore iphlpapi ucrtbase").split())


def command(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True, encoding="utf-8", errors="replace")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy(source, dest):
    if not source.is_file() or source.is_symlink():
        raise RuntimeError(f"Missing/nonregular release input: {source}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, dest)


def imports(folder, objdump):
    report = {}
    for binary in sorted(folder.rglob("*")):
        if binary.suffix.lower() not in {".dll", ".exe"}:
            continue
        data = command(str(objdump), "-p", str(binary))
        if "pei-x86-64" not in data:
            raise RuntimeError(f"Not a Windows x64 binary: {binary}")
        dependencies = re.findall(r"DLL Name:\s*(\S+)", data)
        report[binary.relative_to(folder).as_posix()] = dependencies
        # Modules may use app-root DLLs, their own directory, or Windows APIs.
        # Never accept an arbitrary DLL merely because it exists on this PC.
        for dep in dependencies:
            name = dep.lower()
            local = (folder / dep).is_file() or (binary.parent / dep).is_file()
            if not local:
                local = any(p.name.lower() == name for p in folder.iterdir() if p.is_file())
            if not local and not name.startswith(("api-ms-win-", "ext-ms-win-")) and name.removesuffix(".dll") not in SYSTEM:
                raise RuntimeError(f"Missing dependency: {binary.name} -> {dep}")
    return report


def leading_comments(path):
    text = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"/\*.*?\*/", text[:16384], re.S)
    if not match:
        raise RuntimeError(f"No license header: {path}")
    return match[0].strip() + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mingw", type=Path, default=Path("C:/msys64/ucrt64"))
    parser.add_argument("--msvc-crt", type=Path, required=True,
                        help="Licensed Visual Studio Redist/MSVC/<version>/x64/Microsoft.VC143.CRT")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    build, out = args.build.resolve(), args.output.resolve()
    if args.verify_only:
        report = imports(out, args.mingw / "bin/objdump.exe")
        expected = {"SHA256SUMS.txt"}
        for line in (out / "SHA256SUMS.txt").read_text().splitlines():
            digest, name = line.split("  ", 1)
            expected.add(name)
            if not (out / name).resolve().is_relative_to(out):
                raise RuntimeError(f"Invalid manifest path: {name}")
            if sha(out / name) != digest:
                raise RuntimeError(f"Checksum mismatch: {name}")
        actual = {p.relative_to(out).as_posix() for p in out.rglob("*") if p.is_file()}
        if actual != expected:
            raise RuntimeError(f"Unexpected/missing package files: {sorted(actual ^ expected)}")
        print(f"PASS: checksums and dependency closure for {len(report)} Windows x64 binaries")
        return
    if not out.is_relative_to(ROOT) or out.parent == ROOT or out.exists():
        raise RuntimeError("Choose a NEW nested output folder inside this repository (normally dist/).")
    source_zip = out.parent / "VQ3Evolution-1.0-source.zip"
    if source_zip.exists():
        raise RuntimeError(f"Refusing to replace existing source archive: {source_zip}")
    if not re.search(r"^VERSION=1\.0$", (ROOT / "Makefile").read_text(), re.M):
        raise RuntimeError("Expected 1.0 source version")
    # Plan the payload explicitly; never copy the build directory recursively.
    files = {}
    def add(source, name):
        if name in files:
            raise RuntimeError(f"Duplicate destination: {name}")
        files[name] = source
    for name in ["vQ3Evolution.exe", "vQ3EvoDed.exe", "SDL264.dll", "nvngx.dll",
                 "renderer_vulkan_x86_64.dll", "renderer_opengl1_x86_64.dll",
                 "renderer_opengl2_x86_64.dll", *NVIDIA, *NVIDIA_NOTICES]:
        add(build / name, name)
    for game in ["baseq3", "missionpack"]:
        for module in ["cgame", "qagame", "ui"]:
            for name in [f"{game}/{module}x86_64.dll", f"{game}/vm/{module}.qvm"]:
                add(build / name, name)
    for name in MINGW:
        add(args.mingw / "bin" / name, name)
    for name in MSVC:
        add(args.msvc_crt / name, name)
    for package in sorted((ROOT / "postfx").iterdir()):
        if package.suffix not in {".effect", ".comp", ".vert", ".frag", ".glsl", ".png"}:
            raise RuntimeError(f"Unexpected postfx source: {package.name}")
        add(package, "postfx/" + package.name)
        if package.suffix in {".comp", ".vert", ".frag"}:
            name = package.name + ".spv"
            add(build / "postfx" / name, "postfx/" + name)
    for source, name in [("misc/release/README.txt", "README.txt"),
                         ("COPYING.txt", "COPYING.txt"),
                         ("docs/RELEASE_1.0.md", "RELEASE-NOTES.md"),
                         ("docs/THIRD_PARTY.md", "THIRD-PARTY.md"),
                         ("code/jpeg-8c/README", "licenses/JPEG.txt")]:
        add(ROOT / source, name)
    for license_file in (ROOT / "docs/licenses").iterdir():
        add(license_file, "licenses/" + license_file.name)
    for component in ["gcc-libs", "libwinpthread"]:
        for license_file in (args.mingw / "share/licenses" / component).iterdir():
            if license_file.is_file():
                add(license_file, "licenses/" + component + "/" + license_file.name)
    for name, source in files.items():
        if not source.is_file() or source.is_symlink():
            raise RuntimeError(f"Missing input {name}: {source}")
    candidates = command("git", "ls-files", "--cached", "--others", "--exclude-standard", "-z").split("\0")
    sources = []
    for name in sorted(set(candidates) - {""}):
        source = ROOT / name
        if not source.exists():
            continue  # Pending source deletions stay deleted in the snapshot.
        if source.is_symlink() or not source.resolve().is_relative_to(ROOT):
            raise RuntimeError(f"Unsupported source path: {name}")
        if source.suffix.lower() in {".pk3", ".qvm", ".exe", ".dmp", ".log"}:
            raise RuntimeError(f"Unexpected source payload: {name}")
        if source.suffix.lower() in {".dll", ".a", ".lib"} and not name.startswith("code/libs/"):
            raise RuntimeError(f"Unexpected source binary dependency: {name}")
        sources.append((name, source))
    out.mkdir(parents=True)
    for name, source in files.items():
        copy(source, out / name)
    for label, source in [("SDL2", "code/SDL2/include/SDL.h"),
                          ("zlib", "code/zlib/zlib.h"),
                          ("minizip", "code/qcommon/unzip.h"),
                          ("SILK", "code/opus-1.2.1/silk/NSQ.c"),
                          ("Khronos", "code/renderer_vulkan/vulkan/vulkan_core.h")]:
        path = ROOT / source
        if label == "Khronos" and not path.exists():
            # Locate the checked-in loader/header layout, not the system SDK.
            path = next((ROOT / "code").rglob("vulkan_core.h"))
        (out / "licenses" / (label + ".txt")).write_text(leading_comments(path), encoding="utf-8")
    # Runtime copies of Markdown refer back to the source repository, not absent files.
    for name, original in [("RELEASE-NOTES.md", ROOT / "docs/RELEASE_1.0.md"),
                           ("THIRD-PARTY.md", ROOT / "docs/THIRD_PARTY.md")]:
        text = original.read_text(encoding="utf-8")
        def link(match):
            target = match[2]
            if "://" in target or target.startswith("#"):
                return match[0]
            path, _, anchor = target.partition("#")
            relative = (original.parent / path).resolve().relative_to(ROOT).as_posix()
            return f"[{match[1]}](https://github.com/Sepultaris/vQ3Evolution/blob/main/{relative}" + ("#" + anchor if anchor else "") + ")"
        (out / name).write_text(re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, text), encoding="utf-8")
    report = imports(out, args.mingw / "bin/objdump.exe")
    source_hashes = {name: sha(path) for name, path in sources}
    with zipfile.ZipFile(source_zip, "x", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, path in sources:
            archive.write(path, "VQ3Evolution-1.0-source/" + name)
        archive.writestr("VQ3Evolution-1.0-source/SOURCE-SHA256.json", json.dumps(source_hashes, indent=2) + "\n")
    manifest = {
        "version": "1.0", "architecture": "win64", "source_base_commit": command("git", "rev-parse", "HEAD").strip(),
        "source_is_working_tree": True, "source_archive": source_zip.name,
        "source_sha256": sha(source_zip), "source_files": len(sources),
        "build_command": "make PLATFORM=mingw64 ARCH=x86_64 BUILD_DIR=build-release-1.0 STREAMLINE_DIR=build-widescreen/deps/streamline-sdk-v2.12.0 USE_NVIDIA_DLSS=1 BUILD_SERVER=1 VERSION=1.0 -j6 release",
        "compiler": command(str(args.mingw / "bin/gcc.exe"), "--version").splitlines()[0],
        "nvidia_sdk": "2.12.0 production bin/x64", "dependencies": report,
        "excluded": ["Quake III/mod content", "personal settings", "test fixtures/captures", "SDKs", "nvngx_dlssnr.dll", "vq3e_nrd.dll", "optional OpenAL"]
    }
    (out / "BUILD-MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    payload = sorted(p for p in out.rglob("*") if p.is_file())
    (out / "SHA256SUMS.txt").write_text("".join(f"{sha(p)}  {p.relative_to(out).as_posix()}\n" for p in payload), encoding="utf-8")
    print(f"PASS: staged {len(payload)+1} files; {sum(p.stat().st_size for p in payload)/1048576:.1f} MiB; {len(report)} PE binaries")
    print(f"Release: {out}\nMatching source ({len(sources)} files): {source_zip}")


if __name__ == "__main__":
    main()
