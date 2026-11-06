#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check complete staged payloads, directory overrides and file-only removal."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


def run_make(target, stage, variables):
    result = subprocess.run(
        ["make", "--no-print-directory", "-C", str(ROOT), target,
         f"DESTDIR={stage}", *(f"{key}={value}" for key, value in variables.items())],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        env={**os.environ, "DISPLAY": "", "DBUS_SESSION_BUS_ADDRESS": "unix:path=/nonexistent"},
    )
    if result.returncode:
        raise RuntimeError(result.stdout)


for prefix, native_libdir, overrides in [
    ("/usr/local", "/usr/lib", {}),
    ("/opt/lunatic", "/usr/lib64", {}),
    ("/usr", "/usr/lib/x86_64-linux-gnu", {}),
    ("/opt/lunatic", "/usr/lib", {"BINDIR": "/opt/lunatic/custom-bin", "DATADIR": "/opt/lunatic/custom-share",
                                 "LIBDIR": "/opt/lunatic/custom-lib", "GTK_MODULEDIR": "/opt/gtk/modules"}),
]:
    # Xfce rebases its library suffix onto the descriptor's prefix. Custom
    # overrides test staging only: runtime needs matching search paths.
    variables = {"PREFIX": prefix, "XFCE_PREFIX": "/usr", "XFCE_LIBDIR": native_libdir,
                 "GTK_MODULEDIR": native_libdir + "/gtk-3.0/modules", **overrides}
    libdir = variables.get("LIBDIR", str(Path(prefix) / Path(native_libdir).relative_to("/usr")))
    bindir = variables.get("BINDIR", prefix + "/bin")
    datadir = variables.get("DATADIR", prefix + "/share")
    launcher = {
        bindir + "/lunatic-launcher": "launcher/lunatic-launcher",
        datadir + "/applications/lunatic-launcher.desktop": "launcher/data/lunatic-launcher.desktop",
        datadir + "/icons/hicolor/scalable/apps/lunatic-launcher.svg": "launcher/data/launcher_mark.svg",
    }
    panel = {
        libdir + "/xfce4/panel/plugins/liblunatic-global-menu.so": "panel/build/liblunatic-global-menu.so",
        variables["GTK_MODULEDIR"] + "/liblunatic-panel-gtk-module.so": "panel/build/liblunatic-panel-gtk-module.so",
        datadir + "/xfce4/panel/plugins/lunatic-global-menu.desktop": "panel/xfce-plugin/lunatic-global-menu.desktop",
    }
    for target, payload in [("install", {**launcher, **panel}), ("install-panel", panel),
                            ("install-launcher", launcher)]:
        with tempfile.TemporaryDirectory(prefix="lunatic install ") as directory:
            stage = Path(directory)
            marker = stage / "unrelated-file"
            marker.write_text("keep")
            run_make(target, stage, variables)
            files = {"/" + str(path.relative_to(stage)) for path in stage.rglob("*")
                     if path.is_file() and path != marker}
            assert files == set(payload), (target, files, payload)
            for installed, source in payload.items():
                path = stage / installed.lstrip("/")
                assert digest(path) == digest(ROOT / source), path
            remove = target.replace("install", "uninstall", 1)
            run_make(remove, stage, variables)
            run_make(remove, stage, variables)
            assert {path for path in stage.rglob("*") if path.is_file()} == {marker}
            assert marker.read_text() == "keep"
    print(f"PASS: full/component payloads and uninstall, prefix={prefix}, libdir={libdir}")
