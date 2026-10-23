#!/usr/bin/env python3
"""Offline byte-count comparison only: no BLE timing or power measurement.

Usage: python3 tools/analyze_ble_photo_transport.py /tmp/photo-v2-fixtures
RLE model: (one-byte count 1..255, one packed byte), no wire headers.
The high-entropy inputs are deterministic valid-palette surrogates, not photos.
"""
import itertools
from pathlib import Path
import random
import sys


def rle_size(data):
    return sum(2 * ((sum(1 for _ in run) + 254) // 255)
               for _, run in itertools.groupby(data))


def main():
    assert rle_size(bytes(256)) == 4
    assert rle_size(bytes([0, 1, 0, 1])) == 8
    root = Path(sys.argv[1])
    print("Offline model; packet/byte counts only. RLE includes no protocol metadata.")
    print("board,sample,raw_bytes,raw_packets,rle_bytes,rle/raw,old_fixed_delay_s")
    for name, codes in [("e1004", [0, 1, 2, 3, 5, 6]), ("e1003", list(range(16))),
                        ("e1001", None), ("sticky", None)]:
        frame = (root / f"{name}.bin").read_bytes()
        rng = random.Random(49)
        noisy = bytes((rng.choice(codes) << 4) | rng.choice(codes) for _ in frame) if codes else rng.randbytes(len(frame))
        for sample, data in [("flat", bytes(len(frame))), ("bars", frame), ("high_entropy_surrogate", noisy)]:
            encoded = rle_size(data)
            chunks = (len(data) + 383) // 384
            print(f"{name},{sample},{len(data)},{chunks},{encoded},{encoded/len(data):.4f},{chunks * .015:.3f}")
    print("Window 4 preserves these raw packet/ACK counts; it overlaps up to four application ACK waits.")
    print("ATT writes still use responses. No fourfold speedup or battery saving is inferred.")


if __name__ == "__main__":
    main()
