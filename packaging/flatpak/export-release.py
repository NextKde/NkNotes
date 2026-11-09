#!/usr/bin/env python3
"""Export a catalog-compatible plan from the final bundle's exact bytes."""
import argparse
import base64
import configparser
import hashlib
import json
from pathlib import Path
import re
import subprocess

import gi
gi.require_version("Flatpak", "1.0")
from gi.repository import Flatpak, Gio


def output(*args):
    return subprocess.check_output(args)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    args = argparse.ArgumentParser()
    args.add_argument("--repo", required=True)
    args.add_argument("--output", required=True)
    args.add_argument("--revision", required=True)
    options = args.parse_args()
    if not re.fullmatch(r"[0-9a-f]{40}", options.revision):
        raise SystemExit("Expected an immutable Git revision")
    version = Path("VERSION").read_text().strip()
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise SystemExit("Invalid release version")
    app_id, arch = "org.nextks.notes", "x86_64"
    ref = f"app/{app_id}/{arch}/stable"
    target = Path(options.output)
    target.mkdir(parents=True, exist_ok=True)
    bundle = target / f"{app_id}-{version}-{arch}.flatpak"
    subprocess.run(["flatpak", "build-bundle", "--arch=" + arch, options.repo,
                    str(bundle), app_id, "stable"], check=True)
    subprocess.run(["flatpak", "build-bundle", "--runtime", "--arch=" + arch,
                    options.repo, str(target / f"{app_id}.Sources-{version}-{arch}.flatpak"),
                    app_id + ".Sources", "stable"], check=True)
    exported = Flatpak.BundleRef.new(Gio.File.new_for_path(str(bundle.resolve())))
    metadata = bytes(exported.get_metadata().get_data())
    original = output("ostree", "--repo=" + options.repo, "cat", ref, "/metadata")
    if metadata != original or not metadata or len(metadata) > 256 * 1024:
        raise SystemExit("Bundle metadata differs from the exported repository")
    commit = output("ostree", "--repo=" + options.repo, "rev-parse", ref).decode().strip()
    if exported.format_ref() != ref or exported.get_commit() != commit:
        raise SystemExit("Bundle identity differs from the exported repository")
    if exported.get_origin() or exported.get_runtime_repo_url():
        raise SystemExit("Bundle must not embed online repository URLs")
    config = configparser.ConfigParser(interpolation=None)
    config.read_string(metadata.decode())
    if config["Application"]["runtime"] != "org.kde.Platform/x86_64/6.11":
        raise SystemExit("Unexpected runtime")
    required = [s for s in config.sections() if s.startswith("Extension ")
                and not config.getboolean(s, "autodelete", fallback=False)]
    if required:
        raise SystemExit(f"Unexpected required app extensions: {required}")
    installed_size = exported.get_installed_size()
    if not 0 < installed_size <= 2 * 1024**3:
        raise SystemExit("Unexpected installed size")
    release = {"schemaVersion": 2, "appId": app_id, "version": version,
               "revision": options.revision, "bundles": {arch: {
                   "asset": bundle.name, "ref": ref, "commit": commit,
                   "sha256": digest(bundle), "size": bundle.stat().st_size,
                   "metadata": base64.b64encode(metadata).decode(),
                   "metadataSha256": hashlib.sha256(metadata).hexdigest(),
                   "installedSize": installed_size,
                   "runtime": {"ref": "runtime/org.kde.Platform/x86_64/6.11"},
                   "requiredExtensions": []}}}
    (target / f"{app_id}.flatpak-release.json").write_text(json.dumps(release, indent=2) + "\n")
    (target / f"{app_id}.flatpak-metadata.ini").write_bytes(metadata)


if __name__ == "__main__":
    main()
