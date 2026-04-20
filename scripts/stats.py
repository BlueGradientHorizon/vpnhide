#!/usr/bin/env -S uv run --script
#
# /// script
# requires-python = ">=3.12"
# dependencies = [
#   "httpx",
#   "rich",
# ]
# ///

import httpx
from rich.console import Console
from rich.table import Table

console = Console()
releases = httpx.get("https://api.github.com/repos/okhsunrog/vpnhide/releases").json()

grand_total = 0
for release in releases:
    assets = release["assets"]
    total = sum(a["download_count"] for a in assets)
    grand_total += total

    table = Table(title=f"{release['tag_name']}  ({total} downloads)", title_style="bold", show_header=False, box=None, padding=(0, 1))
    table.add_column("Asset", style="cyan")
    table.add_column("Count", justify="right", style="yellow")
    table.add_column("Bar", style="green")

    for a in assets:
        bar = "█" * max(1, a["download_count"] // 2)
        table.add_row(a["name"], str(a["download_count"]), bar)

    console.print()
    console.print(table)

console.print(f"\n[bold green]Total: {grand_total} downloads[/bold green]")
