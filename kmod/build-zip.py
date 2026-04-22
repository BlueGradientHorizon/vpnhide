#!/usr/bin/env python3
"""Build and package the KernelSU/Magisk kernel module zip.

Assembles a module staging directory so the committed module.prop stays at
its release version while the zip carries the actual build version (git describe).
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
from build_lib import get_build_version, make_zip  # type: ignore[import-not-found]


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    os.chdir(script_dir)

    # Build the kernel module (env vars loaded by direnv from .env)
    kmod_c = script_dir / "vpnhide_kmod.c"
    kmod_ko = script_dir / "vpnhide_kmod.ko"

    if not kmod_ko.exists() or kmod_c.stat().st_mtime > kmod_ko.stat().st_mtime:
        print("Building kernel module...")
        subprocess.run(["make"], check=True)

    # Assemble the module staging directory so the committed module.prop
    # stays at its release version while the zip carries the actual build
    # version (git describe).
    staging = script_dir / "module-staging"
    if staging.exists():
        shutil.rmtree(staging)
    shutil.copytree(script_dir / "module", staging)
    shutil.copy(script_dir / "vpnhide_kmod.ko", staging / "vpnhide_kmod.ko")

    # Get build version
    build_version = get_build_version(script_dir.parent)

    # Stamp version into module.prop
    module_prop = staging / "module.prop"
    content = module_prop.read_text(encoding="utf-8")
    content = re.sub(r"^version=.*", f"version=v{build_version}", content, flags=re.MULTILINE)
    module_prop.write_text(content, encoding="utf-8")
    print(f"Stamped module.prop version=v{build_version}")

    # Create zip
    out_zip = script_dir / "vpnhide-kmod.zip"
    if out_zip.exists():
        out_zip.unlink()

    make_zip(staging, out_zip)
    shutil.rmtree(staging)

    print()
    print(f"Built: {out_zip.name}")
    size_kb = out_zip.stat().st_size / 1024
    print(f"  {out_zip} ({size_kb:.1f} KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
