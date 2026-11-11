#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
from pathlib import Path
from typing import Optional


def read_pref_value(prefs: Path, key: str) -> Optional[str]:
    needle = f"/{key}/value="
    try:
        for line in prefs.read_text(encoding="utf-8", errors="replace").splitlines():
            if needle in line:
                return line.split(needle, 1)[1].strip()
    except FileNotFoundError:
        return None
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--proj-dir", required=True)
    ap.add_argument("--post-build", required=True)  # path to post_build.py
    ap.add_argument("-n", "--name", required=True)
    ap.add_argument("-d", "--dir", required=True)
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("-m", "--mode", required=True)
    ap.add_argument("-v", "--version-fallback", default="dummy")  # optional fallback
    args = ap.parse_args()

    proj_dir = Path(args.proj_dir).resolve()
    prefs = proj_dir / ".settings" / "org.eclipse.cdt.core.prefs"

    version = read_pref_value(prefs, "__BUILD_VERSION__") or args.version_fallback

    cmd = [
        "python",
        str(Path(args.post_build).resolve()),
        "-n", args.name,
        "-v", version,
        "-d", args.dir,
        "-o", args.out,
        "-m", args.mode,
    ]

    print("Post-build version:", version)
    return subprocess.call(cmd)


if __name__ == "__main__":
    raise SystemExit(main())
