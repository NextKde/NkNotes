#!/usr/bin/env python3
"""Disposable CI VM only: exercise the installed app without personal data."""
from pathlib import Path
import sqlite3
import subprocess
import time

app = "org.nextks.notes"
profile = Path.home() / ".var/app" / app
evidence = Path("tmp/evidence")
evidence.mkdir(parents=True, exist_ok=True)


def wait_for(probe, description, seconds=45):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        result = probe()
        if result:
            return result
        time.sleep(0.5)
    raise RuntimeError("Timed out: " + description)


def database():
    return next(profile.glob("data/**/Library/notes.sqlite3"), None)


def note_count():
    db = database()
    if not db:
        return 0
    with sqlite3.connect(f"file:{db}?mode=ro", uri=True) as connection:
        return connection.execute("SELECT count(*) FROM notes WHERE deleted='' ").fetchone()[0]


def launch(label, create=False):
    with (evidence / f"{label}.log").open("w") as log:
        process = subprocess.Popen([
            "flatpak", "run", "--unshare=network", "--socket=x11", "--nosocket=wayland",
            "--env=QT_QPA_PLATFORM=xcb", "--env=QT_QUICK_BACKEND=software",
            "--env=QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu --disable-dev-shm-usage",
            app], stdout=log, stderr=subprocess.STDOUT)
        try:
            wait_for(database, "private library initialization")
            def window():
                found = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "笔记"],
                                       capture_output=True, text=True)
                return found.stdout.splitlines()[0] if found.returncode == 0 and found.stdout else None
            window_id = wait_for(window, "application window")
            if create:
                before = note_count()
                subprocess.run(["xdotool", "windowfocus", "--sync", window_id], check=True)
                subprocess.run(["xdotool", "key", "--clearmodifiers", "ctrl+n"], check=True)
                wait_for(lambda: note_count() > before, "create and persist a note")
            time.sleep(8)
            if process.poll() is not None:
                raise RuntimeError("Application exited during offline launch")
            subprocess.run(["import", "-window", "root", str(evidence / f"{label}.png")], check=True)
        finally:
            subprocess.run(["flatpak", "kill", app], check=False)
            process.wait(timeout=15)
    text = (evidence / f"{label}.log").read_text()
    for forbidden in ("failed to load component", "is not installed", "Cannot load library", "ReferenceError:", "TypeError:"):
        if forbidden in text:
            raise RuntimeError(f"Runtime failure in {label}: {forbidden}")


launch("first-offline-launch", create=True)
count = note_count()
bundle = next(Path("dist").glob("org.nextks.notes-*.flatpak"))
subprocess.run(["flatpak", "uninstall", "--user", "--noninteractive", "--assumeyes", app], check=True)
assert database() and note_count() == count
subprocess.run(["flatpak", "install", "--user", "--noninteractive", "--assumeyes", "--no-deps", str(bundle)], check=True)
launch("reinstall-offline-launch")
assert note_count() == count
subprocess.run(["flatpak", "uninstall", "--user", "--noninteractive", "--assumeyes", app], check=True)
assert database() and note_count() == count
subprocess.run(["flatpak", "info", "--user", "org.kde.Platform//6.11"], check=True)
(evidence / "acceptance.txt").write_text("PASS: install, offline launch, note creation, reinstall, offline relaunch, uninstall preserves notes and shared runtime.\n")
