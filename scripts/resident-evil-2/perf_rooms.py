"""Compare both DMA layouts on the most fragmented camera of a converted ROM."""
import argparse
import ctypes
import os
from pathlib import Path
import statistics
import struct

from perf import diagnostic_rom, frame_runner


def word(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def fixture(rom):
    for count in range(10, 33):
        header = rom.find(struct.pack("<II", count*4, count*4 + 32 + 19200))
        if header < 0:
            continue
        offsets = struct.unpack_from(f"<{count}I", rom, header)
        if list(offsets) == sorted(offsets) and offsets[-1] < len(rom)-header:
            break
    else:
        raise ValueError("RE2 asset directory was not found")
    block = rom[header + word(rom, header + 24):header + word(rom, header + 28)]
    atlas = word(block, 32)
    planes = {}
    max_id = 0

    def decode(offset):
        nonlocal max_id
        if not offset:
            return b"", b"", 0
        if offset in planes:
            return planes[offset]
        count = word(block, offset)
        pos = offset + 1004
        tiles = bytearray()
        while len(tiles) < 32 * count:
            entry = word(block, pos)
            pos += 4
            first, size = entry & 0xffffff, 1 + (entry >> 24)
            tiles.extend(block[atlas + 32 * first:atlas + 32 * (first + size)])
            max_id = max(max_id, first + size)
        assert len(tiles) == count * 32
        planes[offset] = block[offset:pos], tiles, (pos - offset - 1004) // 4
        return planes[offset]

    cameras = []
    for area in range(7):
        directory = word(block, 4 + 4 * area)
        for camera in range(word(block, directory)):
            bg, fg = struct.unpack_from("<II", block, directory + 4 + 12 * camera)
            score = decode(bg)[2] + decode(fg)[2]
            cameras.append((score, area, camera, bg, fg))
    runs, area, camera, bg, fg = max(cameras)
    before = sum(len(p[1]) for p in planes.values())
    after = 32 * max_id + 4 * sum(p[2] for p in planes.values())
    print(f"{len(planes)} distinct planes, {max_id} tiles; tile storage "
          f"{before:,} to {after:,} bytes. Area {area}, camera {camera}, {runs} transfers.")
    offsets, result = [], bytearray(20)
    for payload in (block[atlas:atlas + max_id * 32], decode(bg)[0], decode(fg)[0],
                    decode(bg)[1], decode(fg)[1]):
        offsets.append(len(result) if payload else 0)
        result.extend(payload)
    struct.pack_into("<5I", result, 0, *offsets)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--core", type=Path, default=os.environ.get("MGBA_LIBRETRO"))
    args = parser.parse_args()
    core = args.core or next((Path(os.environ["LOCALAPPDATA"]) / "Programs/mGBA")
                             .glob("libretro-*/mgba_libretro.dll"))
    image = diagnostic_rom(Path(__file__).with_suffix(".c").resolve()) + fixture(args.rom.read_bytes())
    samples = []
    def observe(frame, regions):
        if 0x03000000 in regions:
            record = struct.unpack("<3I", ctypes.string_at(regions[0x03000000] + 0x7000, 12))
            if 3 <= record[0] <= 256:
                samples.append(record)
    frame_runner().render(core, image, final_frame=180, video_probe=observe)
    assert len(samples) >= 150
    assert all(curr[0] == prev[0]+1 for prev,curr in zip(samples,samples[1:]))
    for parity, layout in enumerate(("contiguous", "dictionary")):
        copy = [ticks for frame,ticks,_ in samples if frame % 2 == parity]
        print(f"{layout}: median {statistics.median(copy) * 64:,.0f} cycles preparing both planes")
    print(f"{len(samples)} consecutive presentations, VBlank interval "
          f"{min(s[2] for s in samples)}..{max(s[2] for s in samples)} ticks of 64 cycles")


if __name__ == "__main__":
    main()
