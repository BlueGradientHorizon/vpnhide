#!/usr/bin/env python3
"""Build and package the KernelSU/Magisk kernel module zip.

Assembles a module staging directory so the committed module.prop stays at
its release version while the zip carries the actual build version (git describe).

Usage:
    python3 build-zip.py                      # Build using Makefile with env vars
    python3 build-zip.py --kdir /path/to/kdir  # CI: override kernel source dir
    python3 build-zip.py --kmi android14-5.15  # CI: set gkiVariant
    python3 build-zip.py --out vpnhide-kmod-android14-5.15.zip  # custom output name
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
from build_lib import get_build_version, make_zip  # type: ignore[import-not-found]


# Module file names
KMOD_C = "vpnhide_kmod.c"
KMOD_KO = "vpnhide_kmod.ko"
KMOD_ZIP = "vpnhide-kmod.zip"


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and package the kernel module zip.")
    parser.add_argument(
        "--kdir",
        type=str,
        help="Kernel source directory (for CI, overrides KERNEL_SRC env var)",
    )
    parser.add_argument(
        "--kmi",
        type=str,
        help="Kernel module interface variant (e.g., android14-5.15) for module.prop",
    )
    parser.add_argument(
        "--out",
        type=str,
        help=f"Output zip filename (default: {KMOD_ZIP})",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    os.chdir(script_dir)

    # Set up environment for make (CI can override via --kdir)
    if args.kdir:
        os.environ["KERNEL_SRC"] = args.kdir

    # Set CLANG_DIR from the environment (required by Makefile)
    # In CI, clang is at /opt/ddk/clang/clang-r*/bin
    clang_base = Path("/opt/ddk/clang")
    if clang_base.exists():
        clang_dirs = sorted(d for d in clang_base.iterdir() if d.is_dir() and d.name.startswith("clang-"))
        if clang_dirs:
            os.environ["CLANG_DIR"] = str(clang_dirs[-1])

    # Build the kernel module (env vars loaded by direnv from .env)
    kmod_c = script_dir / KMOD_C
    kmod_ko = script_dir / KMOD_KO

    if not kmod_ko.exists() or kmod_c.stat().st_mtime > kmod_ko.stat().st_mtime:
        print("Building kernel module...")
        subprocess.run(["make", "strip"], check=True)

    # Assemble the module staging directory so the committed module.prop
    # stays at its release version while the zip carries the actual build
    # version (git describe).
    staging = script_dir / "module-staging"
    if staging.exists():
        shutil.rmtree(staging)
    shutil.copytree(script_dir / "module", staging)
    shutil.copy(script_dir / KMOD_KO, staging / KMOD_KO)

    # Get build version
    build_version = get_build_version(script_dir.parent)

    # Stamp version into module.prop
    module_prop = staging / "module.prop"
    content = module_prop.read_text(encoding="utf-8")
    content = re.sub(r"^version=.*", f"version=v{build_version}", content, flags=re.MULTILINE)
    # Add gkiVariant and updateJson if --kmi was provided
    if args.kmi:
        content = re.sub(r"^gkiVariant=.*", f"gkiVariant={args.kmi}", content, flags=re.MULTILINE)
        if not re.search(r"^gkiVariant=", content, flags=re.MULTILINE):
            content = content.rstrip() + f"\ngkiVariant={args.kmi}\n"
        update_json_url = f"https://raw.githubusercontent.com/okhsunrog/vpnhide/main/update-json/update-kmod-{args.kmi}.json"
        content = re.sub(r"^updateJson=.*", f"updateJson={update_json_url}", content, flags=re.MULTILINE)
        if not re.search(r"^updateJson=", content, flags=re.MULTILINE):
            content = content.rstrip() + f"\nupdateJson={update_json_url}\n"
    module_prop.write_text(content, encoding="utf-8")
    print(f"Stamped module.prop version=v{build_version}" + (f" gkiVariant={args.kmi}" if args.kmi else ""))

    # Create zip in parent directory (workspace root for CI)
    out_zip = script_dir.parent / (args.out if args.out else KMOD_ZIP)
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
