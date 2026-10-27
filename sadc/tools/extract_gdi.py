#!/usr/bin/env python3
"""Extract the ISO9660 filesystem from a Dreamcast GD-ROM .gdi dump.

Usage: extract_gdi.py <path/to/game.gdi> <outdir>

Only the high-density area (the last data track) holds the game filesystem.
Raw tracks are 2352-byte Mode 1 sectors; user data is bytes 16..2064.
"""
import os
import struct
import sys


class Track:
    def __init__(self, num, lba, ttype, sector_size, path, offset):
        self.num, self.lba, self.ttype = num, lba, ttype
        self.sector_size, self.path, self.offset = sector_size, path, offset
        self.fp = open(path, "rb")
        self.nsectors = os.path.getsize(path) // sector_size

    def read_sector(self, lba):
        rel = lba - self.lba
        if rel < 0 or rel >= self.nsectors:
            raise ValueError(f"LBA {lba} outside track {self.num}")
        self.fp.seek(rel * self.sector_size + (16 if self.sector_size == 2352 else 0))
        return self.fp.read(2048)


def parse_gdi(path):
    base = os.path.dirname(os.path.abspath(path))
    lines = [l.strip() for l in open(path) if l.strip()]
    tracks = []
    for line in lines[1:int(lines[0]) + 1]:
        p = line.split()
        tracks.append(Track(int(p[0]), int(p[1]), int(p[2]), int(p[3]),
                            os.path.join(base, p[4]), int(p[5])))
    return tracks


def main():
    gdi, outdir = sys.argv[1], sys.argv[2]
    tracks = parse_gdi(gdi)
    data = [t for t in tracks if t.ttype == 4]
    hd = data[-1]

    def read(lba, count=1):
        out = bytearray()
        for i in range(count):
            for t in data:
                if t.lba <= lba + i < t.lba + t.nsectors:
                    out += t.read_sector(lba + i)
                    break
            else:
                raise ValueError(f"LBA {lba + i} not in any data track")
        return bytes(out)

    # IP.BIN occupies the first 16 sectors of the HD area.
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "IP.BIN"), "wb") as f:
        f.write(read(hd.lba, 16))

    pvd = read(hd.lba + 16)
    assert pvd[1:6] == b"CD001", "no ISO9660 PVD"
    root = pvd[156:156 + 34]

    listing = []

    def walk(rec, path):
        extent = struct.unpack_from("<I", rec, 2)[0]
        size = struct.unpack_from("<I", rec, 10)[0]
        raw = read(extent, (size + 2047) // 2048)
        pos = 0
        while pos < size:
            ln = raw[pos]
            if ln == 0:
                pos = (pos // 2048 + 1) * 2048
                continue
            r = raw[pos:pos + ln]
            pos += ln
            nlen = r[32]
            name = r[33:33 + nlen]
            if name in (b"\x00", b"\x01"):
                continue
            name = name.decode("ascii").split(";")[0]
            flags = r[25]
            ext = struct.unpack_from("<I", r, 2)[0]
            sz = struct.unpack_from("<I", r, 10)[0]
            full = os.path.join(path, name)
            if flags & 2:
                os.makedirs(os.path.join(outdir, full), exist_ok=True)
                walk(r, full)
            else:
                with open(os.path.join(outdir, full), "wb") as f:
                    f.write(read(ext, (sz + 2047) // 2048)[:sz])
                listing.append((full.replace("\\", "/"), ext, sz))

    walk(root, "")
    with open(os.path.join(outdir, "filelist.txt"), "w") as f:
        for name, ext, sz in sorted(listing):
            f.write(f"{name}\t{ext}\t{sz}\n")
    print(f"extracted {len(listing)} files to {outdir}")


if __name__ == "__main__":
    main()
