#!/usr/bin/env python3
"""Compare the production raster output with the retained GL reference backend."""
import ctypes
from pathlib import Path

cairo = ctypes.CDLL('libcairo.so.2')
cairo.cairo_image_surface_create_from_png.argtypes = [ctypes.c_char_p]
cairo.cairo_image_surface_create_from_png.restype = ctypes.c_void_p
cairo.cairo_surface_status.argtypes = [ctypes.c_void_p]
cairo.cairo_image_surface_get_width.argtypes = [ctypes.c_void_p]
cairo.cairo_image_surface_get_height.argtypes = [ctypes.c_void_p]
cairo.cairo_image_surface_get_stride.argtypes = [ctypes.c_void_p]
cairo.cairo_image_surface_get_data.argtypes = [ctypes.c_void_p]
cairo.cairo_image_surface_get_data.restype = ctypes.c_void_p
cairo.cairo_surface_destroy.argtypes = [ctypes.c_void_p]


def load(path):
    surface = cairo.cairo_image_surface_create_from_png(bytes(path))
    assert cairo.cairo_surface_status(surface) == 0, path
    width = cairo.cairo_image_surface_get_width(surface)
    height = cairo.cairo_image_surface_get_height(surface)
    stride = cairo.cairo_image_surface_get_stride(surface)
    data = ctypes.string_at(cairo.cairo_image_surface_get_data(surface), height * stride)
    cairo.cairo_surface_destroy(surface)
    return width, height, stride, data


def main():
    paths = sorted(Path('build/cpu').glob('*.png'))
    assert len(paths) >= 14, 'Run make check-renderer-parity first'
    for path in paths:
        cpu = load(path)
        gpu = load(Path('build/gl') / path.name)
        assert cpu[:3] == gpu[:3], path
        deltas = [abs(a - b) for a, b in zip(cpu[3], gpu[3])]
        mean = sum(deltas) / len(deltas)
        outliers = sum(d > 4 for d in deltas) / len(deltas)
        print(f'{path.name}: mean channel error {mean:.3f}/255; '
              f'{outliers * 100:.3f}% of channels differ by >4; maximum {max(deltas)}')
        # Integer bilinear filtering/blending can round a few byte levels apart.
        # Every pixel must stay within the tolerance, including polygon edges.
        assert mean < 0.2 and max(deltas) <= 4, path


if __name__ == '__main__':
    main()
