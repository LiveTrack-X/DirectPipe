"""Check current release metadata, local document links and stale RNNoise claims.

This is a consistency guard, not a replacement for semantic source/doc review.
Published release records and historical plans are inventoried, not rewritten.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
version = re.search(r"project\(DirectPipe VERSION ([0-9.]+)", (root / "CMakeLists.txt").read_text()).group(1)
errors = []
for name in ("host/CMakeLists.txt", "plugins/receiver/CMakeLists.txt"):
    if f'VERSION "{version}"' not in (root / name).read_text(encoding="utf-8"):
        errors.append(f"Version mismatch: {name}")
plugin = root / "com.directpipe.directpipe.sdPlugin"
manifest = json.loads((plugin / "manifest.json").read_text(encoding="utf-8"))
package = json.loads((plugin / "package.json").read_text(encoding="utf-8"))
lock = json.loads((plugin / "package-lock.json").read_text(encoding="utf-8"))
if (manifest["Version"], package["version"], lock["version"], lock["packages"][""]["version"]) != (version + ".0", version, version, version):
    errors.append("Stream Deck manifest/package/lock versions disagree")
if (root / "dist/release_body.md").read_bytes() != (root / f"dist/release_body_v{version}.md").read_bytes():
    errors.append("Current/versioned release bodies disagree")
for name, expression in (
        ("README.md", r"badge/version-v([0-9.]+)-"),
        ("CHANGELOG.md", r"^## \[([0-9.]+)\]"),
        ("docs/ReleaseNote.md", r"^## DirectPipe v([0-9.]+)"),
        ("docs/ARCHITECTURE.md", r"([0-9]+\.[0-9]+\.[0-9]+)"),
        ("docs/BUILDING.md", r"([0-9]+\.[0-9]+\.[0-9]+)"),
        ("docs/PRODUCT_SPEC.md", r"([0-9]+\.[0-9]+\.[0-9]+)"),
        ("docs/USER_GUIDE.md", r"Guide Version ([0-9.]+)"),
        ("dist/release_body.md", r"^## DirectPipe v([0-9.]+)")):
    match = re.search(expression, (root / name).read_text(encoding="utf-8"), re.M)
    if not match or match.group(1) != version:
        errors.append(f"Current document version mismatch: {name}")
guide = (root / "docs/STREAMDECK_GUIDE.md").read_text(encoding="utf-8")
if not re.search(re.escape(version + ".0"), guide):
    errors.append("Stream Deck guide version mismatch")

paths = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard"], cwd=root, text=True).splitlines()
historical_names = {"MULTI_RECEIVER_4_4_0.md", "MONO_VST_ROUTING.md", "RECEIVER_RELIABILITY_UPDATE.md"}
inventory = []
for rel in sorted(set(paths)):
    path = root / rel
    if not path.is_file() or path.suffix != ".md":
        continue
    historical = ((rel.startswith("dist/") and rel not in {"dist/release_body.md", f"dist/release_body_v{version}.md", "dist/RELEASE_NOTES.md"})
                  or "/superpowers/" in rel or path.name in historical_names
                  or (path.name.startswith(("RELEASE_", "DOCUMENTATION_AUDIT_")) and version.replace(".", "_") not in path.name))
    content = path.read_text(encoding="utf-8")
    inventory.append({"path": rel, "role": "historical" if historical else "current",
                      "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "lines": len(content.splitlines())})
    if historical:
        continue
    # Fenced examples are examples, not local navigational links.
    prose = re.sub(r"```.*?```", "", content, flags=re.S)
    for target in re.findall(r"\]\(([^)]+)\)", prose):
        target = target.strip().split(' "')[0].strip("<>")
        if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target) or target.startswith(("#", "/", "//")):
            continue
        target = target.split("#")[0]
        if not target or any(mark in target for mark in ("{", "}", "*", "$", "<", ">")):
            continue
        if not (path.parent / target).exists():
            errors.append(f"Missing local link: {rel}: {target}")

obsolete = ("Noise Removal requires 48", "Noise Removal은 48kHz 전용", "RNN state starts uninitialized", "ONLY safe approach")
source_inventory = []
for base in ("host/Source", "core", "plugins/receiver"):
    for path in sorted((root / base).rglob("*")):
        if path.suffix not in {".h", ".cpp"} or not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        source_inventory.append({"path": path.relative_to(root).as_posix(), "lines": len(text.splitlines()),
                                 "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        for phrase in obsolete:
            if phrase in text:
                errors.append(f"Obsolete source claim: {path.relative_to(root)}: {phrase}")
# Current meter prose must not revive the post-Mono input-bar interpretation.
for name in ("docs/USER_GUIDE.md", "docs/PRODUCT_SPEC.md", "docs/NOISE_LR_METERS.md"):
    content = (root / name).read_text(encoding="utf-8")
    for phrase in ("post mapping/gain/mute", "after channel mapping", "Mono duplication displays equal channels"):
        if phrase in content:
            errors.append(f"Obsolete input-meter semantics: {name}: {phrase}")
for name in ("README.md", "docs/USER_GUIDE.md", "docs/PRODUCT_SPEC.md", "docs/AUTO_DESIGN.md", "docs/ARCHITECTURE.md", "docs/BUILDING.md"):
    if "DPWIM" not in (root / name).read_text(encoding="utf-8"):
        errors.append(f"Missing optional DPWIM setup coverage: {name}")
for name in ("README.md", "docs/USER_GUIDE.md", "docs/PRODUCT_SPEC.md"):
    current = (root / name).read_text(encoding="utf-8")
    if "Install DPWIM" not in current:
        errors.append(f"Missing Windows optional-installer description: {name}")
for name in ("host/Source/Audio/VSTChain.h", "host/Source/UI/PresetManager.cpp", "host/CMakeLists.txt"):
    if any(word in (root / name).read_text(encoding="utf-8") for word in ("BuiltinDPWIM", "builtin_dpwim", "directpipe_add_dpwim")):
        errors.append(f"Deferred embedded DPWIM leaked into final release: {name}")
report = {"version": version, "documents": inventory, "source_files": source_inventory, "errors": errors,
          "limit": "Inventory/link/pattern checks supplement manual semantic review; no full correctness proof."}
if len(sys.argv) > 1:
    output = root / sys.argv[1]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
print(f"Version {version}; {len(inventory)} documents ({sum(r['role']=='current' for r in inventory)} current); {len(source_inventory)} source files")
for error in errors:
    print(error)
raise SystemExit(bool(errors))
