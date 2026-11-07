#!/usr/bin/env python3
"""Validate generated XPM frames and the original button-artwork links."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1] / "Ambiance/xfwm4"


def read(path):
    lines = re.findall(r'^"(.*)"[,;]?$', path.read_text(), re.M)
    width, height, count, cpp = map(int, lines[0].split())
    assert len(lines) == 1 + count + height, path
    palette = {}
    for line in lines[1:1 + count]:
        symbol, value = line[:cpp], line[cpp:].strip()
        assert re.fullmatch(r"c (None|#[0-9a-fA-F]{6})", value), (path, value)
        palette[symbol] = value[2:]
    rows = lines[1 + count:]
    assert all(len(row) == width * cpp for row in rows), path
    return [[palette[row[i:i + cpp]] for i in range(0, len(row), cpp)] for row in rows]


def check():
    images = {path.stem: read(path) for path in ROOT.glob("*.xpm")}
    assert len(images) == 60
    for state in ["active", "inactive"]:
        titles = [images[f"title-{part}-{state}"] for part in range(1, 6)]
        assert all(title == titles[0] for title in titles)
        assert len(titles[0]) == 28
        assert all(row[0] != "None" and all(c == row[0] for c in row) for row in titles[0])
        for side in ["left", "right"]:
            rows = images[f"{side}-{state}"]
            assert len(rows[0]) == 3 and all(row == rows[0] for row in rows)
            assert all(color != "None" for row in rows for color in row)
            corner = images[f"top-{side}-{state}"]
            assert len(corner) == 28 and len(corner[0]) == 6
            assert corner[-1] == [titles[0][-1][0]] * 6
    links = list(ROOT.glob("*.png"))
    assert len(links) == 20
    for path in links:
        assert path.is_symlink() and path.exists(), path
    print("Validated 60 XPMs and 20 original artwork links; 28px title bars and 3px borders.")


if __name__ == "__main__":
    check()
