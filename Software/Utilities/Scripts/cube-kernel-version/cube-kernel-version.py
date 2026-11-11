#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path
from typing import Tuple


def run_git(args: list[str], cwd: Path) -> Tuple[int, str]:
    try:
        out = subprocess.check_output(
            ["git", *args],
            cwd=str(cwd),
            stderr=subprocess.STDOUT,
            text=True,
        )
        return 0, out.strip()
    except subprocess.CalledProcessError as e:
        return e.returncode or 1, (e.output or "").strip()
    except FileNotFoundError:
        return 127, "git not found in PATH"


def compute_repo_root(start_dir: Path) -> Path | None:
    rc, out = run_git(["rev-parse", "--show-toplevel"], start_dir)
    if rc != 0 or not out:
        return None
    return Path(out)


def compute_git_branch(repo_root: Path) -> str:
    rc, out = run_git(["symbolic-ref", "--short", "-q", "HEAD"], repo_root)
    if rc == 0 and out:
        return out
    return "detached"


def compute_build_version(repo_root: Path) -> str:
    # Example: v0.1.8-rc1-7-g4a72370-dirty  ->  0.1.8-rc1-7-4a72370-dirty
    rc, desc = run_git(["describe", "--tags", "--dirty", "--always"], repo_root)
    if rc != 0 or not desc:
        return "0.0.0-dev"

    v = desc
    if v.startswith("v"):
        v = v[1:]
    v = re.sub(r"-g([0-9a-fA-F]+)", r"-\1", v, count=1)
    return v


def sed_like_patch_prefs(prefs_path: Path, key: str, value: str) -> bool:
    """
    Exactly mimics:
      sed -i 's#/<key>/value=.*#/<key>/value=<value>#g'
    BUT independent of the prefix before '/<key>/value='.
    """
    needle = f"/{key}/value="
    text = prefs_path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines(keepends=True)

    changed = False
    for i, line in enumerate(lines):
        if needle in line:
            prefix = line.split(needle, 1)[0]
            newline = "\r\n" if line.endswith("\r\n") else "\n"
            lines[i] = f"{prefix}{needle}{value}{newline}"
            changed = True

    if changed:
        prefs_path.write_text("".join(lines), encoding="utf-8")
    return changed


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--proj-dir", required=True, help="CubeIDE project directory (e.g. ${ProjDirPath})")
    args = ap.parse_args()

    proj_dir = Path(args.proj_dir).resolve()
    prefs = proj_dir / ".settings" / "org.eclipse.cdt.core.prefs"

    if not prefs.exists():
        print(f"Prefs file not found: {prefs} (skipping)")
        return 0

    repo_root = compute_repo_root(proj_dir)
    if repo_root is None:
        print(f"No git repo detected from: {proj_dir} (skipping)")
        return 0

    build_version = compute_build_version(repo_root)
    git_branch = compute_git_branch(repo_root)

    ok1 = sed_like_patch_prefs(prefs, "__BUILD_VERSION__", build_version)
    if ok1:
        print(f"Overridden __BUILD_VERSION__ to '{build_version}' in {prefs}")
    else:
        print(f"Pattern __BUILD_VERSION__ not found in {prefs} (add it in CubeIDE Build Variables once)")

    ok2 = sed_like_patch_prefs(prefs, "__GIT_BRANCH__", git_branch)
    if ok2:
        print(f"Overridden __GIT_BRANCH__ to '{git_branch}' in {prefs}")
    else:
        print(f"Pattern __GIT_BRANCH__ not found in {prefs} (add it in CubeIDE Build Variables once)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
