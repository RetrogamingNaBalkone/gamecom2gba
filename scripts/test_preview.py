"""Check the standalone converter and the actual mGBA video output."""
import argparse
import ctypes as c
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class GameInfo(c.Structure):
    _fields_ = [("path", c.c_char_p), ("data", c.c_void_p),
                ("size", c.c_size_t), ("meta", c.c_char_p)]


def render(core_path, rom, start=False, firing=False):
    core = c.CDLL(str(core_path))
    frames = []
    pixel_format = 0
    keys = set()

    def environment(command, data):
        nonlocal pixel_format
        if command == 10:
            pixel_format = c.cast(data, c.POINTER(c.c_uint))[0]
            return pixel_format in (0, 1, 2)
        if command == 3:
            c.cast(data, c.POINTER(c.c_bool))[0] = True
            return True
        return False

    def video(ptr, width, height, pitch):
        if ptr:
            step = 4 if pixel_format == 1 else 2
            frames[:] = [b"".join(c.string_at(ptr + y*pitch, width*step)
                                  for y in range(height))]

    callbacks = []
    for name, signature, function in [
        ("environment", (c.c_bool, c.c_uint, c.c_void_p), environment),
        ("video_refresh", (None, c.c_void_p, c.c_uint, c.c_uint, c.c_size_t), video),
        ("audio_sample", (None, c.c_int16, c.c_int16), lambda *_: None),
        ("audio_sample_batch", (c.c_size_t, c.c_void_p, c.c_size_t), lambda _, n: n),
        ("input_poll", (None,), lambda: None),
        ("input_state", (c.c_int16, c.c_uint, c.c_uint, c.c_uint, c.c_uint),
         lambda port, device, index, button: int(port == 0 and device == 1 and button in keys)),
    ]:
        cb = c.CFUNCTYPE(*signature)(function)
        callbacks.append(cb)
        getattr(core, "retro_set_" + name)(cb)
    core.retro_init()
    buffer = c.create_string_buffer(rom)
    info = GameInfo(b"preview.gba", c.cast(buffer, c.c_void_p), len(rom), None)
    try:
        core.retro_load_game.restype = c.c_bool
        assert core.retro_load_game(c.byref(info))
        try:
            shot_frames = []
            for frame in range(360 if firing else 120):
                if start and frame == 60:
                    keys.add(3)
                if frame == 62:
                    keys.clear()
                if firing and frame == 120:
                    keys.add(8)
                core.retro_run()
                if firing and frame >= 119:
                    step = 4 if pixel_format == 1 else 2
                    # The muzzle rises above the idle sprite at y=83..95.
                    band = b"".join(frames[0][(y*240+106)*step:(y*240+134)*step]
                                    for y in range(83,96))
                    if frame == 119:
                        rest = band
                        flashing = False
                    elif (band != rest) != flashing:
                        flashing = band != rest
                        if flashing:
                            shot_frames.append(frame)
            if firing:
                intervals = [b-a for a,b in zip(shot_frames, shot_frames[1:])]
                assert len(intervals) >= 9, shot_frames
                assert all(16 <= n <= 17 for n in intervals[:5]), intervals
                assert 77 <= intervals[5] <= 79, intervals
                print("mGBA shot intervals (display frames):", intervals)
        finally:
            core.retro_unload_game()
    finally:
        core.retro_deinit()
    pixels = []
    for value, in struct.iter_unpack("<I" if pixel_format == 1 else "<H", frames[0]):
        if pixel_format == 1:
            rgb = (value >> 19, value >> 11, value >> 3)
        else:
            rgb = (value >> (11 if pixel_format == 2 else 10),
                   value >> (6 if pixel_format == 2 else 5), value)
        pixels.append(tuple(channel & 31 for channel in rgb))
    return pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cartridge", type=Path, help="Duke USA ZIP")
    parser.add_argument("--core", default=os.environ.get("MGBA_LIBRETRO"))
    parser.add_argument("--reference", type=Path, help="reference.c executable; use adjacent BIOS ZIPs")
    args = parser.parse_args()
    core = args.core or next((Path(os.environ["LOCALAPPDATA"]) / "Programs/mGBA")
                             .glob("libretro-*/mgba_libretro.dll"))
    with zipfile.ZipFile(args.cartridge) as archive:
        data = archive.read(next(i for i in archive.infolist() if i.file_size == 2097152))
    exe = ROOT / "dist/gamecom-gba.exe"
    reference_frame = None
    with tempfile.TemporaryDirectory(dir=ROOT / "build") as tmp:
        work = Path(tmp)
        output = work / "output.gba"
        raw = work / "renamed.bin"
        raw.write_bytes(data)
        for source in (args.cartridge.resolve(), raw):
            subprocess.run([exe, source, output], cwd=work, check=True)
            if source != raw:
                expected = output.read_bytes()
            else:
                assert expected == output.read_bytes()
        raw.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
        assert subprocess.run([exe, raw, output], cwd=work).returncode == 1
        assert expected == output.read_bytes()
        unrelated = work / "other.zip"
        with zipfile.ZipFile(unrelated, "w") as archive:
            archive.writestr("Duke Nukem 3D (USA).tgc", b"unrecognized cartridge")
        assert subprocess.run([exe, unrelated, output], cwd=work).returncode == 1
        assert expected == output.read_bytes()
        unrelated.unlink()
        assert sorted(p.name for p in work.iterdir()) == ["output.gba", "renamed.bin"]
        if args.reference:
            raw.write_bytes(data)
            bios = []
            for role, sha in [("Internal", "03368237e8fed4a8724f3b4a1596cf4b17c96d33"),
                              ("External", "97f782e72d738f4d7b861363266bf46b438d9b50")]:
                name = "[BIOS] Tiger Game.com (USA) (" + role + ").zip"
                with zipfile.ZipFile(args.cartridge.parent / name) as archive:
                    blob = next(b for i in archive.infolist() for b in [archive.read(i)]
                                if hashlib.sha1(b).hexdigest() == sha)
                path = work / (role + ".bin")
                path.write_bytes(blob)
                bios.append(path)
            # Boot, select the cartridge, then A for New Game, episode and skill.
            waits = (1200, 2400, 120, 90, 424)
            actions = ("ff ff ff 2 2", "7f ff ff -1 0", "7f ff ff -1 0", "7f ff ff -1 0")
            tape = []
            for i, wait in enumerate(waits):
                tape.append(f"{wait} ff ff ff -1 0")
                if i < len(actions):
                    tape.append("8 " + actions[i])
            result = subprocess.run([args.reference, *bios, raw], cwd=work, check=True,
                                    input="\n".join(tape).encode(), stdout=subprocess.PIPE)
            reference_frame = result.stdout
    pixels = render(core, expected)
    assert len(pixels) == 240*160
    colors = [(27,31,17), (13,17,9), (1,9,5), (0,0,0)]
    for y in range(160):
        for x in range(200):
            packed = data[0x94000 + 64*x + y//4]
            assert pixels[y*240+x+20] == colors[(packed >> (6-2*(y%4))) & 3], (x,y)
    for x0 in (0, 220):
        side = {pixels[y*240+x] for y in range(160) for x in range(x0, x0+20)}
        assert side == {(22,22,21), (5,5,5)}
    level = render(core, expected, start=True)
    render(core, expected, start=True, firing=True)
    assert level != pixels
    for y in range(160):
        assert level[y*240:y*240+20] == pixels[y*240:y*240+20]
        assert level[y*240+220:y*240+240] == pixels[y*240+220:y*240+240]
    if reference_frame:
        magic, size, maximum, image = reference_frame.split(b"\n", 3)
        assert (magic, size, maximum, len(image)) == (b"P5", b"200 160", b"255", 32000)
        shades = {0:colors[3], 63:colors[2], 126:colors[1], 252:colors[0]}
        for y in range(150):
            for x in range(200):
                assert level[y*240+x+20] == shades[image[y*200+x]], (x,y)
    print("ZIP/raw identity, rejection, output preservation and both mGBA screens passed.")


if __name__ == "__main__":
    main()
