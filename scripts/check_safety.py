#!/usr/bin/env python3
"""Safety check for Model:Cycles repos.

Stops three kinds of accident before they reach GitHub:
  1. Elektron firmware (official OS files, or anything built from them)
  2. Passwords / API keys / private keys
  3. Huge files that should not live in git

Usage:
  python3 check_safety.py             check the files staged for commit (default)
  python3 check_safety.py --all       check every file git knows about (tracked + new, not ignored)
  python3 check_safety.py PATH ...    check specific files or folders

Exit code 0 = fine, 1 = problem found (the commit is blocked when used as a hook).
Needs only Python 3 and git. No other packages.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys

# Elektron's MIDI manufacturer ID is 00 20 3C. Every Elektron sysex file
# (including OS updates) starts with F0 00 20 3C.
ELEKTRON_SYSEX_PREFIX = bytes([0xF0, 0x00, 0x20, 0x3C])

# Whole-file SHA-256 of official files we know about (from Modded-Cycles BUILD.md).
KNOWN_FIRMWARE_SHA256 = {
    "44fe586269631a0ca7da25a3383fc6733c314809505fc3cc52f1e0ed9800640c":
        "the official model-cycles_OS1.13.syx from elektron.se",
}

BLOCKED_EXTENSIONS = {".syx"}
BLOCKED_NAMES = {".env", "auth.json", "id_rsa", "id_ed25519", "credentials.json"}
BLOCKED_SUFFIXES = (".pem", ".p12", ".pfx")
MAX_BYTES = 5 * 1024 * 1024          # 5 MB
MAX_SCAN_BYTES = 2 * 1024 * 1024     # only text-scan files up to 2 MB

SECRET_PATTERNS = [
    ("an Anthropic API key", re.compile(rb"sk-ant-[A-Za-z0-9_\-]{20,}")),
    ("an API key (sk-...)", re.compile(rb"\bsk-[A-Za-z0-9]{24,}\b")),
    ("a GitHub token", re.compile(rb"\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{30,}\b")),
    ("a GitHub fine-grained token", re.compile(rb"github_pat_[A-Za-z0-9_]{30,}")),
    ("an AWS access key", re.compile(rb"\bAKIA[0-9A-Z]{16}\b")),
    ("a private key block", re.compile(rb"-----BEGIN [A-Z ]*PRIVATE KEY-----")),
]


def run_git(args, cwd=None):
    return subprocess.run(["git"] + args, cwd=cwd, capture_output=True, check=True).stdout


def repo_root():
    try:
        return run_git(["rev-parse", "--show-toplevel"]).decode().strip()
    except Exception:
        return None


def list_staged(root):
    out = run_git(["diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z"], cwd=root)
    return [p for p in out.decode("utf-8", "replace").split("\0") if p]


def list_all(root):
    out = run_git(["ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=root)
    return [p for p in out.decode("utf-8", "replace").split("\0") if p]


def expand_paths(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            for dirpath, dirnames, filenames in os.walk(p):
                dirnames[:] = [d for d in dirnames if d != ".git"]
                for f in filenames:
                    files.append(os.path.join(dirpath, f))
        elif os.path.exists(p):
            files.append(p)
        else:
            print(f"warning: {p} does not exist, skipped")
    return files


def read_bytes(path, root, staged):
    if staged:
        return run_git(["show", f":{path}"], cwd=root)
    full = path if root is None else os.path.join(root, path)
    with open(full, "rb") as fh:
        return fh.read()


def check_file(path, data):
    """Return a list of plain-English problems for one file."""
    problems = []
    name = os.path.basename(path)
    lower = name.lower()
    ext = os.path.splitext(lower)[1]

    if ext in BLOCKED_EXTENSIONS:
        problems.append("is a .syx file. Never publish these: they contain Elektron's code.")
    if data.startswith(ELEKTRON_SYSEX_PREFIX):
        problems.append("starts with Elektron's sysex header (F0 00 20 3C): this is Elektron firmware or "
                        "something built from it, whatever the file is called.")
    if lower in BLOCKED_NAMES or lower.endswith(BLOCKED_SUFFIXES):
        problems.append("looks like a secrets / credentials file.")
    if len(data) > MAX_BYTES:
        problems.append(f"is {len(data) / 1e6:.1f} MB. Files over {MAX_BYTES // (1024 * 1024)} MB "
                        "should not go in git. Ask your AI where this should live instead.")

    digest = hashlib.sha256(data).hexdigest()
    if digest in KNOWN_FIRMWARE_SHA256:
        problems.append(f"is byte-for-byte {KNOWN_FIRMWARE_SHA256[digest]}.")

    if len(data) <= MAX_SCAN_BYTES and b"\0" not in data[:8192]:
        for label, pattern in SECRET_PATTERNS:
            if pattern.search(data):
                problems.append(f"contains what looks like {label}.")
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*", help="files or folders to check (default: files staged in git)")
    ap.add_argument("--all", action="store_true", help="check every non-ignored file in the repo")
    ap.add_argument("--staged", action="store_true", help="check files staged for commit (default)")
    args = ap.parse_args()

    root = repo_root()
    staged = False
    if args.paths:
        files = expand_paths(args.paths)
        root_for_read = None
    else:
        if root is None:
            print("Not inside a git repository. Give me paths to check.")
            return 1
        if args.all:
            files = list_all(root)
        else:
            files = list_staged(root)
            staged = True
        root_for_read = root

    bad = 0
    for path in files:
        try:
            data = read_bytes(path, root_for_read, staged)
        except Exception as exc:  # unreadable file: say so, do not crash
            print(f"warning: could not read {path}: {exc}")
            continue
        for problem in check_file(path, data):
            bad += 1
            print(f"BLOCKED  {path}\n         {problem}")

    if bad:
        print(f"\n{bad} problem(s) found. Nothing was changed. Fix or remove the file(s), then try again.")
        return 1
    print(f"Safety check passed ({len(files)} file(s) checked).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
