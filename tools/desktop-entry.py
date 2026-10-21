#!/usr/bin/env python3
"""Create and install the GNOME / FreeDesktop desktop entry and launcher icon."""
import os
import shutil
import subprocess
from pathlib import Path

PROJECT_ROOT = Path("@PROJECT_ROOT@")
HOST_HOME = Path("@HOST_HOME@")


def remove_legacy_entry(path: Path) -> None:
    """Remove a previous launcher entry only when this project created it."""
    try:
        if path.exists() and "desktop-run.sh" in path.read_text(errors="ignore"):
            path.unlink()
    except OSError:
        pass


def main():
    icons_base = HOST_HOME / ".local/share/icons"
    apps_base = HOST_HOME / ".local/share/applications"

    scalable_dir = icons_base / "hicolor/scalable/apps"
    png_dir = icons_base / "hicolor/128x128/apps"
    scalable_dir.mkdir(parents=True, exist_ok=True)
    png_dir.mkdir(parents=True, exist_ok=True)
    apps_base.mkdir(parents=True, exist_ok=True)

    source_svg = PROJECT_ROOT / "darling-lol.svg"
    source_png = PROJECT_ROOT / "darling-lol.png"
    if not source_svg.is_file() or not source_png.is_file():
        raise SystemExit("launcher icon is missing: expected darling-lol.svg and darling-lol.png")

    # Install the project's original icon under a neutral name.  Do not extract
    # or install an icon from the game/client installation.
    svg_target = scalable_dir / "darling-launcher.svg"
    png_target = png_dir / "darling-launcher.png"
    shutil.copyfile(source_svg, svg_target)
    shutil.copyfile(source_png, png_target)
    shutil.copyfile(source_svg, icons_base / "darling-launcher.svg")
    shutil.copyfile(source_png, icons_base / "darling-launcher.png")

    # Remove artifacts created by older versions of this helper.
    remove_legacy_entry(apps_base / "league-of-legends.desktop")
    for legacy_icon in (
        icons_base / "league-of-legends.svg",
        icons_base / "league-of-legends.png",
        scalable_dir / "league-of-legends.svg",
        icons_base / "hicolor/512x512/apps/league-of-legends.png",
    ):
        try:
            if legacy_icon.exists():
                legacy_icon.unlink()
        except OSError:
            pass

    # Create the desktop entry.  Keep StartupWMClass: it is the client's real
    # window class and is required for the compositor to match the window.
    run_script = PROJECT_ROOT / "scripts/desktop-run.sh"
    exec_path = str(run_script).replace("%", "%%")
    exec_cmd = f'"{exec_path}"' if " " in exec_path else exec_path
    desktop_content = f"""[Desktop Entry]
Version=1.0
Type=Application
Name=Darling Launcher
GenericName=Game
Comment=Unofficial launcher for the macOS game client via Darling
Exec={exec_cmd}
Path={PROJECT_ROOT}
Icon=darling-launcher
Terminal=false
Categories=Game;ActionGame;StrategyGame;
Keywords=darling;launcher;compatibility;
StartupWMClass=Riot Client
StartupNotify=false
"""

    desktop_file = apps_base / "darling-launcher.desktop"
    desktop_file.write_text(desktop_content)
    os.chmod(desktop_file, 0o755)
    print(f"Created application launcher: {desktop_file}")

    target_desktop_dirs = []
    try:
        res = subprocess.run(["xdg-user-dir", "DESKTOP"], capture_output=True, text=True, check=False)
        if res.returncode == 0 and res.stdout.strip():
            d = Path(res.stdout.strip())
            if d.is_dir() and d not in target_desktop_dirs:
                target_desktop_dirs.append(d)
    except Exception:
        pass
    for cand in [HOST_HOME / "Desktop"]:
        if cand.is_dir() and cand not in target_desktop_dirs:
            target_desktop_dirs.append(cand)

    for ddir in target_desktop_dirs:
        remove_legacy_entry(ddir / "league-of-legends.desktop")
        desktop_shortcut = ddir / "darling-launcher.desktop"
        desktop_shortcut.write_text(desktop_content)
        os.chmod(desktop_shortcut, 0o755)
        gio = shutil.which("gio")
        if gio:
            subprocess.run([gio, "set", str(desktop_shortcut), "metadata::trusted", "true"], check=False)
        print(f"Created desktop shortcut: {desktop_shortcut}")

    # Refresh icon and desktop databases
    update_desktop = shutil.which("update-desktop-database")
    if update_desktop:
        subprocess.run([update_desktop, str(apps_base)], check=False)

    update_icons = shutil.which("gtk-update-icon-cache")
    if update_icons:
        subprocess.run([update_icons, "-f", "-t", str(icons_base / "hicolor")], check=False)

    print("GNOME application launcher and icon installed successfully.")


if __name__ == "__main__":
    main()
