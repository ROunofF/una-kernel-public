#!/usr/bin/env python3
"""Verify every kernel source file is registered in the CubeIDE project.

WHY THIS EXISTS
---------------
The kernel sources live out-of-tree (``Software/Libs/``) relative to the CubeIDE
project (``Software/App/UnaWatch-CubeIDE/``), so ``.project`` links every file
INDIVIDUALLY: the directories are ``<type>2</type>`` entries with
``locationURI virtual:/virtual`` (virtual folders with no filesystem behind
them), and each file is its own ``<type>1</type>`` link. CDT therefore cannot
discover a new source file on its own.

The consequence is a silent failure that looks exactly like success: a new
``.c``/``.cpp`` with no ``<link>`` is **never compiled**, while the build still
reports 0 errors and emits a valid ``.elf``/``.bin``/``.hex``. It only surfaces
much later as "my new code never runs". This script turns that into a build
failure.

WHAT IT CHECKS
--------------
* every ``Software/Libs/**/*.{c,cpp}`` has a matching ``<link>`` in ``.project``;
* a link whose spelling differs from the file on disk only by CASE is reported
  as a casing mismatch, not as a missing link -- the fix is different (correct
  the link, or rename the file), and a case-only mismatch compiles on Windows
  while breaking a case-sensitive Linux/Docker/CI build;
* no ``<link>`` points at a source file that no longer exists (a stale link,
  which fails loudly rather than silently, but is the same class of drift).

WHAT IT DOES *NOT* CHECK -- read this before trusting a green result
--------------------------------------------------------------------
* **Headers.** They do not affect compilation, since include paths resolve them.
  Convention is to link them anyway for IDE indexing; this script will not fail
  you for forgetting.
* **``.cproject`` exclusions.** "Linked" is necessary but NOT sufficient for
  "compiled": the ``sourceEntries`` entry for ``Libs`` carries an ``excluding=``
  list (currently several ``Source/Test/*.cpp`` files and the whole
  ``Components/UserAppLayer`` tree). A new source added under an excluded path
  would satisfy this check and still never compile. If you add code somewhere
  unusual, confirm the ``.o`` exists rather than trusting a green run here.
* **Sources outside ``Software/Libs``**, e.g. the two ``SDK/Libs/Source/JSON/*``
  links. Files added beside those have the same failure mode, unchecked.

Whatever this reports, the ground truth for "did my file get compiled" is the
build log plus the ``.o`` on disk. Never trust the build's exit code alone.

USAGE
-----
    python Software/Utilities/Scripts/check_project_links.py

Exits 0 when clean, 1 with a report otherwise.
"""

from __future__ import annotations

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# Repo-relative locations.
PROJECT_FILE = Path("Software/App/UnaWatch-CubeIDE/.project")
SOURCE_ROOT = Path("Software/Libs")
# .project names sources relative to a "Libs" virtual folder, e.g.
# "Libs/Source/Components/Crc/Crc32.cpp".
LINK_PREFIX = "Libs"
# Extensions that produce an object file, i.e. whose absence is a silent gap.
SOURCE_SUFFIXES = (".c", ".cpp")

# Files that intentionally have no link. Each entry needs a reason: if you are
# adding one, say why the file is not part of the firmware build. Note that a
# file merely *excluded* in .cproject may still be linked, so exclusion alone is
# not a reason to appear here.
#
# Most files that were unlinked when this check was written were resolved rather
# than excused: Crc8.{hpp,cpp}, Crc16.{h,c} and FIFOSafe.{h,c} were deleted as dead
# code, and TestAirohaGPS.cpp was linked after fixing a header-casing mismatch that
# had kept it from ever compiling. PixArt's unlinked factory test sources were
# deleted when the factory test moved into the PixArt PPG library.
ALLOWLIST: dict[str, str] = {}


def repo_root() -> Path:
    """Locate the repo root from this script's own position."""
    # .../Software/Utilities/Scripts/check_project_links.py -> up three.
    return Path(__file__).resolve().parents[3]


def linked_sources(project_file: Path) -> set[str]:
    """Every source path named by a <link> in .project."""
    tree = ET.parse(project_file)
    names = set()
    for link in tree.getroot().iter("link"):
        name = link.findtext("name")
        if name is None:
            continue
        name = name.strip()
        # Case-insensitive suffix test: a link spelled ".CPP" still names a
        # source, and we would rather report it than silently skip it.
        if name.lower().endswith(SOURCE_SUFFIXES):
            names.add(name)
    return names


def on_disk_sources(root: Path) -> set[str]:
    """Every source under Software/Libs, named as .project would name it."""
    found = set()
    for suffix in SOURCE_SUFFIXES:
        for path in root.rglob(f"*{suffix}"):
            found.add(f"{LINK_PREFIX}/{path.relative_to(root).as_posix()}")
    return found


def main() -> int:
    root = repo_root()
    project_file = root / PROJECT_FILE
    source_root = root / SOURCE_ROOT

    if not project_file.is_file():
        print(f"ERROR: {PROJECT_FILE} not found (looked under {root})")
        return 1
    if not source_root.is_dir():
        print(f"ERROR: {SOURCE_ROOT} not found (looked under {root})")
        return 1

    linked = linked_sources(project_file)
    on_disk = on_disk_sources(source_root)

    # Split "no link at all" from "linked under a different casing", because the
    # remedies differ and a case-only mismatch is the subtler bug: it resolves on
    # a case-insensitive filesystem and breaks a case-sensitive one.
    linked_by_lower = {name.lower(): name for name in linked}
    casing: list[tuple[str, str]] = []
    missing: list[str] = []
    for path in sorted(on_disk - linked - set(ALLOWLIST)):
        other = linked_by_lower.get(path.lower())
        if other is None:
            missing.append(path)
        else:
            casing.append((path, other))

    # A link whose file is gone. Restricted to our prefix so links to sources
    # elsewhere are left alone, and excluding the casing cases already reported.
    mismatched_links = {link for _, link in casing}
    stale = sorted(
        name for name in linked
        if name.startswith(f"{LINK_PREFIX}/")
        and name not in on_disk
        and name not in mismatched_links
    )
    # Allowlist entries that have since been linked or deleted are just noise.
    obsolete = sorted(
        path for path in ALLOWLIST
        if path in linked or path not in on_disk
    )

    # Output stays pure ASCII: this runs on a cp1252 Windows console as well as
    # UTF-8 CI, and a mojibake dash in a build failure is a distraction.
    if missing:
        print("ERROR: source files missing a <link> in "
              f"{PROJECT_FILE.as_posix()}:\n")
        for path in missing:
            print(f"  {path}")
        print("\nThese will be SILENTLY NOT COMPILED - the build reports 0 errors")
        print("and still produces a valid .elf. Add a link beside a sibling in the")
        print("same directory, matching the existing form exactly:\n")
        example = missing[0]
        print("    <link>")
        print(f"        <name>{example}</name>")
        print("        <type>1</type>")
        print(f"        <locationURI>PARENT-2-PROJECT_LOC/{example}</locationURI>")
        print("    </link>\n")
        print("Link the matching header too (convention, not enforced here). If the")
        print("file genuinely does not belong in the build, add it to ALLOWLIST in")
        print("this script with a reason.")

    if casing:
        print("\nERROR: <link> entries whose spelling differs from the file on")
        print("disk only by CASE:\n")
        for path, link in casing:
            print(f"  on disk: {path}")
            print(f"  linked : {link}\n")
        print("This compiles on case-insensitive Windows and BREAKS the")
        print("case-sensitive Linux/Docker/CI build. Fix one to match the other -")
        print("do not add a second link.")

    if stale:
        print("\nERROR: <link> entries pointing at files that no longer exist:\n")
        for path in stale:
            print(f"  {path}")
        print("\nRemove these links, or restore the files.")

    if obsolete:
        print("\nNOTE: ALLOWLIST entries that are now linked or deleted - please")
        print("remove them from this script to keep the list honest:\n")
        for path in obsolete:
            print(f"  {path}")

    if missing or casing or stale:
        return 1

    allowed = len(ALLOWLIST) - len(obsolete)
    print(f"OK: {len(on_disk)} source files under {SOURCE_ROOT.as_posix()}, "
          f"{len(on_disk) - allowed} linked, {allowed} allowlisted.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
