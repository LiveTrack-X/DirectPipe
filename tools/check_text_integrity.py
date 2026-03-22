#!/usr/bin/env python3
"""
Fail the build if tracked text files are not valid UTF-8 or contain U+FFFD.

This protects against accidental mojibake/encoding corruption in docs and source.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path, PurePosixPath

TEXT_EXTENSIONS = {
    ".md",
    ".txt",
    ".json",
    ".yml",
    ".yaml",
    ".xml",
    ".cmake",
    ".c",
    ".cc",
    ".cpp",
    ".h",
    ".hh",
    ".hpp",
    ".js",
    ".mjs",
    ".cjs",
    ".ts",
    ".tsx",
    ".html",
    ".css",
    ".ps1",
    ".sh",
    ".bat",
    ".toml",
    ".ini",
    ".rc",
    ".csv",
}

TEXT_FILENAMES = {
    "CMakeLists.txt",
    ".gitignore",
    ".gitattributes",
    ".editorconfig",
    "LICENSE",
}

SKIP_PREFIXES = (
    ".git/",
    "build/",
    "build-local/",
    "com.directpipe.directpipe.sdPlugin/node_modules/",
)


def git_ls_files(repo_root: Path) -> list[str]:
    raw = subprocess.check_output(
        ["git", "-C", str(repo_root), "ls-files", "-z"], stderr=subprocess.PIPE
    )
    return [p for p in raw.decode("utf-8").split("\0") if p]


def is_target_text_file(rel_path: str) -> bool:
    if any(rel_path.startswith(prefix) for prefix in SKIP_PREFIXES):
        return False

    posix = PurePosixPath(rel_path)
    if posix.name in TEXT_FILENAMES:
        return True

    return posix.suffix.lower() in TEXT_EXTENSIONS


def find_replacement_lines(text: str) -> list[int]:
    bad_lines: list[int] = []
    for i, line in enumerate(text.splitlines(), start=1):
        if "\ufffd" in line:
            bad_lines.append(i)
    return bad_lines


def main() -> int:
    parser = argparse.ArgumentParser(description="Check UTF-8 text integrity.")
    parser.add_argument(
        "--repo-root",
        default=".",
        help="Repository root directory (default: current directory)",
    )
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    tracked = git_ls_files(repo_root)

    decode_errors: list[tuple[str, str]] = []
    replacement_errors: list[tuple[str, list[int]]] = []

    for rel in tracked:
        if not is_target_text_file(rel):
            continue

        path = repo_root / rel
        data = path.read_bytes()

        if b"\x00" in data:
            decode_errors.append((rel, "contains NUL byte"))
            continue

        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError as exc:
            decode_errors.append((rel, str(exc)))
            continue

        bad_lines = find_replacement_lines(text)
        if bad_lines:
            replacement_errors.append((rel, bad_lines[:10]))

    if not decode_errors and not replacement_errors:
        print("OK: text integrity check passed (UTF-8 + no U+FFFD).")
        return 0

    print("ERROR: text integrity check failed.")
    if decode_errors:
        print("\n[UTF-8 decode errors]")
        for rel, reason in decode_errors:
            print(f" - {rel}: {reason}")

    if replacement_errors:
        print("\n[Replacement character U+FFFD found]")
        for rel, lines in replacement_errors:
            line_list = ", ".join(str(n) for n in lines)
            print(f" - {rel}: lines {line_list}")

    print(
        "\nTip: edit/save files as UTF-8 and avoid shell rewrites without explicit UTF-8 encoding."
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
