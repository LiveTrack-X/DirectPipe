"""Verify a locally staged Windows candidate archive without executing it."""
from pathlib import Path
import hashlib
import json
import re
import struct
import sys
import zipfile

base = Path(sys.argv[1])
root = Path(__file__).resolve().parents[1]
version = re.search(r"project\(DirectPipe VERSION ([0-9.]+)", (root / "CMakeLists.txt").read_text()).group(1)
stage = base / "windows-stage"
archive = base / f"DirectPipe-v{version}-Windows.zip"
identities = json.loads((base / "windows-identity.json").read_text(encoding="utf-8-sig"))
with zipfile.ZipFile(archive) as zipped:
    members = [m for m in zipped.infolist() if not m.is_dir()]
    staged = {p.relative_to(stage).as_posix(): p for p in stage.rglob("*") if p.is_file()}
    required = {
        "DirectPipe.exe", "DirectPipe Receiver.dll",
        "DirectPipe Receiver.vst3/Contents/x86_64-win/DirectPipe Receiver.vst3",
        "DirectPipe Receiver.vst3/Contents/Resources/moduleinfo.json",
    }
    assert len(members) == len(required) and {m.filename for m in members} == set(staged) == required
    digests = {}
    for member in members:
        data = zipped.read(member)
        digest = hashlib.sha256(data).hexdigest()
        assert digest == hashlib.sha256(staged[member.filename].read_bytes()).hexdigest()
        digests[member.filename] = digest
    assert len(identities) == 3
    for identity in identities:
        name = identity["path"].replace("\\", "/")
        data = zipped.read(name)
        assert data[:2] == b"MZ"
        pe_offset = struct.unpack_from("<I", data, 60)[0]
        assert data[pe_offset:pe_offset + 4] == b"PE\0\0"
        assert struct.unpack_from("<H", data, pe_offset + 4)[0] == 0x8664
        assert identity["sha256"].lower() == digests[name]
        assert identity["file_version"] == identity["product_version"] == version
    # JUCE's SDK helper emits trailing commas; inspect a normalized copy only.
    text = zipped.read("DirectPipe Receiver.vst3/Contents/Resources/moduleinfo.json").decode("utf-8")
    metadata = json.loads(re.sub(r",(\s*[}\]])", r"\1", text))
    assert metadata["Version"] == version
result = {"zip_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
          "file_count": len(members), "pe_amd64": len(identities),
          "payload_matches_staging": True, "payload_sha256": digests,
          "public_release": False}
(base / "windows-package-verification.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print(f"PASS: {archive.name}, 3 AMD64/versioned binaries, {len(members)} exact payload files")
print("SHA256: " + result["zip_sha256"])
