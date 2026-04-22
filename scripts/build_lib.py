"""Shared helpers for build scripts.

Used by kmod/build-zip.py, portshide/build-zip.py, and zygisk/build-zip.py.
"""

from __future__ import annotations

import os
import zipfile
from pathlib import Path


def get_python_exe() -> str:
    """Return 'python' on Windows, 'python3' elsewhere."""
    return "python" if os.name == "nt" else "python3"


def make_zip(source_dir: Path, output_zip: Path) -> None:
    """Create a zip archive from source_dir contents."""
    with zipfile.ZipFile(output_zip, "w", zipfile.ZIP_DEFLATED) as zf:
        for file_path in source_dir.rglob("*"):
            if file_path.is_file():
                arcname = file_path.relative_to(source_dir)
                zf.write(file_path, arcname)


def get_build_version(repo_root: Path | None = None) -> str:
    """Get the effective build version for vpnhide artifacts.

    - HEAD on a tag vX.Y.Z        -> "X.Y.Z"          (release build)
    - N commits after tag vX.Y.Z  -> "X.Y.Z-N-gSHA"   (dev build)
    - working tree dirty          -> additional "-dirty" suffix
    - no git / no matching tag    -> falls back to VERSION file
    """
    import subprocess

    if repo_root is None:
        repo_root = Path(__file__).resolve().parent.parent

    result = subprocess.run(
        ["git", "describe", "--tags", "--match", "v*", "--dirty"],
        cwd=repo_root,
        capture_output=True,
        text=True,
    )
    if result.returncode == 0 and result.stdout.strip():
        return result.stdout.strip().removeprefix("v")

    version_file = repo_root / "VERSION"
    return version_file.read_text(encoding="utf-8").strip()
