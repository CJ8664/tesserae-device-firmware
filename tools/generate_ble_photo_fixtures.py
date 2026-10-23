#!/usr/bin/env python3
"""Reproduce Photo v2 descriptors and pixel goldens, without device access."""
import hashlib
import json
from pathlib import Path
import sys

def descriptor(kind, width, height, fmt, bpp):
    stride = (width * bpp + 7) // 8
    size = stride * height
    canonical = f"2|{kind}|{width}|{height}|{width}|{height}|0|{fmt}|{stride}|{size}|384|top_down|manual"
    return dict(event="photo_capabilities", schema=2, kind=kind,
                width=width, height=height, visible_width=width, visible_height=height,
                x_offset=0, format=fmt, stride=stride, bytes=size, chunk_bytes=384,
                digest=hashlib.sha256(canonical.encode()).hexdigest(),
                row_order="top_down", lifecycle="manual", max_window=4,
                write_without_response=True)

def main():
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    cases = [
        ("e1004", "seeed_reterminal_e1004", 1200, 1600, "spectra6-msb", 4,
         bytes([0x00])*100 + bytes([0x11])*100 + bytes([0x22])*100 +
         bytes([0x33])*100 + bytes([0x55])*100 + bytes([0x66])*100),
        ("e1001", "seeed_reterminal_e1001", 800, 480, "mono1-msb", 1,
         bytes([0x00])*50 + bytes([0xff])*50),
        ("sticky", "seeed_reterminal_sticky", 480, 800, "gray2-msb", 2,
         bytes([0x00])*30 + bytes([0x55])*30 + bytes([0xaa])*30 + bytes([0xff])*30),
        ("e1003", "seeed_reterminal_e1003", 1872, 1404, "gray4-msb", 4,
         bytes(((2*x)//117 << 4) | ((2*x+1)//117) for x in range(936))),
    ]
    for name, kind, width, height, fmt, bpp, row in cases:
        cap = descriptor(kind, width, height, fmt, bpp)
        assert len(json.dumps(cap, separators=(",", ":")).encode()) <= 512
        frame = row * height
        assert len(frame) == cap["bytes"]
        (destination/f"{name}.capabilities.json").write_text(json.dumps(cap, indent=2)+"\n")
        (destination/f"{name}.bin").write_bytes(frame)
        (destination/f"{name}.sha256").write_text(hashlib.sha256(frame).hexdigest()+"\n")
        print(name, cap["digest"], hashlib.sha256(frame).hexdigest())

if __name__ == "__main__":
    main()
