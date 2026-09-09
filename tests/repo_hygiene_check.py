"""Read-only pre-commit checks for the working tree, including untracked files.

Does not stage, commit, fetch dependencies, run the game, or scan Git history.
The credential check detects common high-confidence token/key forms only;
it is not a complete security audit. Values are never printed.
"""
import argparse
from collections import Counter
from pathlib import Path
import re
import subprocess
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]


def git(*args, input_data=None):
    result = subprocess.run(["git", "-C", str(ROOT), *args], input=input_data,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            timeout=60, check=False)
    if result.returncode not in (0, 1):
        raise RuntimeError(result.stderr.decode(errors="replace"))
    return result.stdout


def names(data):
    return {item.decode("utf-8") for item in data.split(b"\0") if item}


def anchors(path):
    result, counts = set(), Counter()
    fenced = False
    for line in path.read_text(encoding="utf-8").splitlines():
        if re.match(r"^\s*(```|~~~)", line):
            fenced = not fenced
        if fenced:
            continue
        heading = re.match(r"^#{1,6}\s+(.+?)\s*#*\s*$", line)
        if heading:
            slug = re.sub(r"[^\w\s-]", "", heading[1].lower()).replace(" ", "-")
            count = counts[slug]
            counts[slug] += 1
            result.add(slug if not count else f"{slug}-{count}")
        result.update(re.findall(r'\bid=["\']([^"\']+)["\']', line))
    return result


def check_docs(errors):
    docs = [ROOT / name for name in ("README.md", "CONTRIBUTING.md", "SECURITY.md",
                                    "code/renderer_vulkan/README.md")]
    docs += sorted((ROOT / "docs").rglob("*.md"))
    checked = 0
    for doc in docs:
        text = doc.read_text(encoding="utf-8")
        text = re.sub(r"(?ms)^\s*```.*?^\s*```\s*$", "", text)
        for match in re.finditer(r"\[[^\]\n]+\]\(([^)\n]+)\)", text):
            target = match[1].strip().strip("<>")
            url = urlsplit(target)
            if url.scheme or url.netloc:
                continue  # No network requests or external-link claims.
            checked += 1
            resolved = (doc.parent / unquote(url.path)).resolve() if url.path else doc
            label = f"{doc.relative_to(ROOT)}: {target}"
            if not resolved.is_relative_to(ROOT) or not resolved.exists():
                errors.append("Missing/outside-repo documentation target: " + label)
            elif url.fragment and resolved.suffix == ".md":
                if unquote(url.fragment) not in anchors(resolved):
                    errors.append("Missing documentation heading: " + label)
    print(f"Checked {len(docs)} project Markdown documents / {checked} local links.")


def check_ignore(errors):
    rejected = ["build/test.o", "build-widescreen/deps/sdk.dll", "build-old/release.zip",
                "qconsole.log", "vq3e-rendering.cfg", "baseq3/pak0.pk3",
                "missionpack/pak3.pk3", "screenshots/test.png", "capture.nsys-rep",
                "capture.ngfx-gputrace", "capture.dmp", "capture.rdc", "renderer.dll",
                "old-game.exe", "test.obj", "model.safetensors", "model.pth",
                "model.onnx", ".env", ".env.local", "tests/__pycache__/test.pyc"]
    retained = ["tests/pt_rr_workgroup.cfg", "tests/repo_hygiene_check.py",
                "docs/STATUS.md", ".env.example", "code/renderer_vulkan/pt_blue_noise.h",
                "code/renderer_vulkan/shaders/pt_rr_trace.comp",
                "code/renderer_vulkan/shaders/Compiled/pt_rr_trace_comp.c",
                "code/renderer_vulkan/shaders/Compiled/pt_rr_trace.cspv"]
    ignored = set(git("check-ignore", "--no-index", "--stdin",
                      input_data=("\n".join(rejected + retained) + "\n").encode()).decode().splitlines())
    for name in rejected:
        if name not in ignored:
            errors.append("Expected ignored path is addable: " + name)
    for name in retained:
        if name in ignored:
            errors.append("Required source/example is ignored: " + name)
    print(f"Checked {len(rejected) + len(retained)} ignore-policy cases.")


def check_documented_defaults(errors):
    source = (ROOT / "code/renderer_vulkan/tr_cvar.c").read_text(encoding="utf-8")
    defaults = dict(re.findall(r'Cvar_Get\s*\(\s*"([^"]+)"\s*,\s*"([^"]+)"', source))
    checked = 0
    for name in ("PATH_TRACING.md", "RAY_RECONSTRUCTION.md", "RTX.md"):
        text = (ROOT / "docs" / name).read_text(encoding="utf-8")
        for cvar, value in re.findall(r'^\| `(r_\w+)` \| ([0-9.]+) \|', text, re.M):
            checked += 1
            if cvar not in defaults or float(defaults[cvar]) != float(value):
                errors.append(f"Documented default disagrees with source: {name}: {cvar}")
    print(f"Checked {checked} documented renderer defaults against registration.")


def check_candidates(errors):
    tracked = names(git("ls-files", "-z"))
    candidates = tracked | names(git("ls-files", "--others", "--exclude-standard", "-z"))
    ignored_tracked = names(git("ls-files", "--cached", "--ignored", "--exclude-standard", "-z"))
    for name in ignored_tracked:
        if not name.startswith("code/libs/"):
            errors.append("Generated/local file is already tracked: " + name)
    patterns = {
        "private-key block": rb"-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----",
        "GitHub token": rb"\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})\b",
        "API key": rb"\bsk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{40,}\b",
        "AWS access key": rb"\bAKIA[A-Z0-9]{16}\b",
    }
    count, total, largest = 0, 0, (0, "")
    for name in sorted(candidates):
        path = ROOT / name
        if not path.is_file():
            continue  # Pending deletions are not candidate payloads.
        size = path.stat().st_size
        count += 1
        total += size
        largest = max(largest, (size, name))
        if size >= 100 * 1024 * 1024:
            errors.append("File exceeds 100 MiB source-repository policy: " + name)
        if name not in tracked and path.suffix.lower() in {".dll", ".exe", ".pk3", ".pdb", ".dmp"}:
            errors.append("Unexpected binary/game payload: " + name)
        data = path.read_bytes()
        if b"\0" in data[:8192]:
            continue
        for kind, pattern in patterns.items():
            found = re.search(pattern, data)
            if found:
                line = data.count(b"\n", 0, found.start()) + 1
                errors.append(f"Possible {kind}: {name}:{line} (value withheld)")
    print(f"Checked {count} candidate files ({total / 1048576:.1f} MiB); "
          f"largest {largest[1]} ({largest[0] / 1048576:.2f} MiB).")


def check_shaders(errors):
    compiled = ROOT / "code/renderer_vulkan/shaders/Compiled"
    count = 0
    for suffix, stage in (("cspv", "comp"), ("vspv", "vert"), ("fspv", "frag")):
        for binary in sorted(compiled.glob("*." + suffix)):
            array = binary.with_name(binary.stem + "_" + stage + ".c")
            if not array.is_file():
                errors.append("Missing embedded shader array: " + str(array.relative_to(ROOT)))
                continue
            data = array.read_bytes()
            payload = bytes(int(match[1], 16) for match in
                            re.finditer(rb"\b0x([0-9a-fA-F]{2})\b", data))
            if payload != binary.read_bytes():
                errors.append("Shader bytecode/array mismatch: " + str(binary.relative_to(ROOT)))
            count += 1
    if not count:
        errors.append("No embedded shader payloads found")
    print(f"Checked {count} embedded shader bytecode/array pairs (not GLSL regeneration).")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shader-payloads", action="store_true")
    args = parser.parse_args()
    errors = []
    check_docs(errors)
    check_ignore(errors)
    check_documented_defaults(errors)
    check_candidates(errors)
    if args.shader_payloads:
        check_shaders(errors)
    if errors:
        for error in errors:
            print("FAIL:", error)
        return 1
    print("PASS: repository hygiene; no files staged or changed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
