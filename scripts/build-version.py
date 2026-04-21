#!/usr/bin/env python3
"""Print the effective build version for vpnhide artifacts.

  - HEAD on a tag vX.Y.Z        -> "X.Y.Z"          (release build)
  - N commits after tag vX.Y.Z  -> "X.Y.Z-N-gSHA"   (dev build)
  - working tree dirty          -> additional "-dirty" suffix
  - no git / no matching tag    -> falls back to VERSION file

Used by every packaging step (module.prop, APK versionName, CI
artifact names) so dev builds are unambiguously identifiable at a
glance. Called from `app/build.gradle.kts` on every Gradle build, so
stays on stdlib only — Gradle shouldn't need `uv` / external deps to
assemble the APK.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    # `git describe` fails (non-zero exit) both when we're outside a
    # git repo and when there's no matching `v*` tag yet — either case
    # means we should fall back to the VERSION file.
    result = subprocess.run(
        ["git", "describe", "--tags", "--match", "v*", "--dirty"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    if result.returncode == 0 and result.stdout.strip():
        raw = result.stdout.strip().removeprefix("v")
        print(raw)
        return 0

    version_file = REPO_ROOT / "VERSION"
    print(version_file.read_text(encoding="utf-8").strip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
