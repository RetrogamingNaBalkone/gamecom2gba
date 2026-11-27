"""Run a GBA image in the mGBA libretro core with a key tape and save PNG snapshots.

Tape lines: DURATION LABEL [keys...] with GBA keys A B L R START SELECT UP DOWN LEFT RIGHT.
A line labelled `input` advances without a snapshot.
"""
import argparse
import ctypes as c
import os
from pathlib import Path
import struct
import zlib

KEYS = {"B": 0, "Y": 1, "SELECT": 2, "START": 3, "UP": 4, "DOWN": 5, "LEFT": 6, "RIGHT": 7,
        "A": 8, "X": 9, "L": 10, "R": 11}


class GameInfo(c.Structure):
    _fields_ = [("path", c.c_char_p), ("data", c.c_void_p), ("size", c.c_size_t), ("meta", c.c_char_p)]


class MemoryDescriptor(c.Structure):
    _fields_ = [("flags", c.c_uint64), ("ptr", c.c_void_p), ("offset", c.c_size_t), ("start", c.c_size_t),
                ("select", c.c_size_t), ("disconnect", c.c_size_t), ("len", c.c_size_t), ("addrspace", c.c_char_p)]


class MemoryMap(c.Structure):
    _fields_ = [("descriptors", c.POINTER(MemoryDescriptor)), ("count", c.c_uint)]


def png(path, width, height, rgb):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def run(rom, tape, output=None, core_path=None, probe=None):
    core_path = core_path or os.environ.get("MGBA_LIBRETRO") or next(
        (Path(os.environ["LOCALAPPDATA"]) / "Programs/mGBA").glob("libretro-*/mgba_libretro.dll"))
    core = c.CDLL(str(core_path))
    state = {"format": 0, "keys": set(), "frame": None, "memory": {}}

    def environment(command, data):
        if command == 0x10024:
            m = c.cast(data, c.POINTER(MemoryMap))[0]
            state["memory"].update((d.start, d.ptr + d.offset) for d in m.descriptors[:m.count] if d.ptr)
            return True
        if command == 10:
            state["format"] = c.cast(data, c.POINTER(c.c_uint))[0]
            return state["format"] in (0, 1, 2)
        if command == 3:
            c.cast(data, c.POINTER(c.c_bool))[0] = True
            return True
        return False

    def video(ptr, width, height, pitch):
        if ptr:
            step = 4 if state["format"] == 1 else 2
            state["frame"] = (width, height, b"".join(c.string_at(ptr + y * pitch, width * step) for y in range(height)))

    callbacks = []
    for name, signature, function in [
        ("environment", (c.c_bool, c.c_uint, c.c_void_p), environment),
        ("video_refresh", (None, c.c_void_p, c.c_uint, c.c_uint, c.c_size_t), video),
        ("audio_sample", (None, c.c_int16, c.c_int16), lambda *_: None),
        ("audio_sample_batch", (c.c_size_t, c.c_void_p, c.c_size_t), lambda p, n: n),
        ("input_poll", (None,), lambda: None),
        ("input_state", (c.c_int16, c.c_uint, c.c_uint, c.c_uint, c.c_uint),
         lambda port, device, index, button: int(port == 0 and device == 1 and button in state["keys"])),
    ]:
        cb = c.CFUNCTYPE(*signature)(function)
        callbacks.append(cb)
        getattr(core, "retro_set_" + name)(cb)
    core.retro_init()
    buffer = c.create_string_buffer(rom)
    info = GameInfo(b"indy.gba", c.cast(buffer, c.c_void_p), len(rom), None)
    core.retro_load_game.restype = c.c_bool
    assert core.retro_load_game(c.byref(info))
    shots = []
    frame = 0
    try:
        for line in tape:
            words = line.partition("#")[0].split()
            if not words:
                continue
            duration, label, *keys = words
            state["keys"] = {KEYS[k.upper()] for k in keys}
            for _ in range(int(duration)):
                core.retro_run()
                frame += 1
                if probe:
                    probe(frame, state["memory"])
            if label != "input" and output:
                width, height, raw = state["frame"]
                if state["format"] == 1:
                    rgb = b"".join(bytes((px[2], px[1], px[0])) for px in struct.iter_unpack("4B", raw))
                else:
                    rgb = bytearray()
                    for (v,) in struct.iter_unpack("<H", raw):
                        rgb += bytes(((v >> 11 & 31) << 3, (v >> 5 & 63) << 2, (v & 31) << 3))
                path = output / f"{frame:06}-{label}.png"
                png(path, width, height, bytes(rgb))
                shots.append(path)
    finally:
        core.retro_unload_game()
        core.retro_deinit()
    return shots


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("tape", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    run(args.rom.read_bytes(), args.tape.read_text().splitlines(), args.output)


if __name__ == "__main__":
    main()
