"""Replay an input tape on the original Indy 500 cartridge in Tigerbyte.

Each tape line: DURATION LABEL [inputs...]. Inputs are game.com keys (UP DOWN LEFT RIGHT
MENU PAUSE SOUND A B C D) or a touch cell @column,row on the 13x10 BIOS grid.
A line labelled `input` advances without a snapshot. Every other label writes
FRAME-LABEL.png and FRAME-LABEL.ram (RAM 0000-0FFF followed by E000-FFFF).
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
from uuid import uuid4
import zipfile
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SIZE = 0x100000
SHA256 = "58cc3a3a4c33fec49bb9260ae83650a6d22b1004b4767a61307d197f6e423d5a"
KEYS = {"UP": (0, 1), "DOWN": (0, 2), "LEFT": (0, 4), "RIGHT": (0, 8),
        "MENU": (0, 16), "PAUSE": (0, 32), "SOUND": (0, 64), "A": (0, 128),
        "B": (1, 1), "C": (1, 2), "D": (2, 2)}
SNAPSHOT = 32000 + 4096 + 8192


def decode_tape(lines):
    rows, snapshots, frame = [], [], 0
    for line in lines:
        words = line.partition("#")[0].split()
        if not words:
            continue
        duration, label, *inputs = words
        ports, column, row = [255, 255, 255], -1, 0
        for key in inputs:
            if key.startswith("@"):
                column, row = map(int, key[1:].split(","))
                if not (0 <= column < 13 and 0 <= row < 10):
                    raise ValueError(key)
            else:
                port, mask = KEYS[key.upper()]
                ports[port] &= ~mask
        ticks = int(duration)
        if ticks <= 0:
            raise ValueError(duration)
        frame += ticks
        observe = label != "input"
        rows.append(f"{ticks if observe else -ticks} {ports[0]:x} {ports[1]:x} {ports[2]:x} {column} {row}")
        if observe:
            snapshots.append((frame, label))
    return "\n".join(rows).encode() + b"\n", snapshots


def unpack(archive, algorithm, expected, destination, size=None):
    with zipfile.ZipFile(archive) as source:
        for member in source.infolist():
            if member.is_dir() or (size and member.file_size != size):
                continue
            data = source.read(member)
            if hashlib.new(algorithm, data).hexdigest() == expected:
                destination.write_bytes(data)
                return
    raise ValueError(f"Unrecognized contents in {archive}")


def png(shades, path):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))
    palette = bytes.fromhex("000000 0f4f2f 6f8f4f 8fcf8f dfff8f".replace(" ", ""))
    scan = b"".join(b"\0" + shades[y:y + 200] for y in range(0, len(shades), 200))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 200, 160, 8, 3, 0, 0, 0))
                     + chunk(b"PLTE", palette) + chunk(b"IDAT", zlib.compress(scan)) + chunk(b"IEND", b""))


def build(stage, tigerbyte, zig, dma, coverage=False):
    source = tigerbyte / "src"
    executable = stage / "capture.exe"
    sources = sorted((source / "cpu").glob("*.c")) + sorted((source / "sys").glob("*.c"))
    compiler = [zig, "cc", "-O2", f"-I{source}", str(HERE / "capture.c")]
    if dma:
        original = source / "sys/gcbus.c"
        code = original.read_text(encoding="utf-8")
        anchor = "static void gcbus_dma(gcbus_t *b)\n{"
        if code.count(anchor) != 1:
            raise RuntimeError("Unsupported Tigerbyte DMA entry point")
        shim = stage / "gcbus.c"
        shim.write_text(code.replace(anchor, "extern void capture_dma_begin(void);\n" + anchor +
                                     "\n   capture_dma_begin();"), encoding="utf-8")
        sources[sources.index(original)] = shim
        compiler.append(f"-I{source / 'sys'}")
    if coverage:
        original = source / "cpu/sm8521.c"
        code = original.read_text(encoding="utf-8")
        anchor = "   uint16_t at = c->pc;\n"
        if code.count(anchor) != 1:
            raise RuntimeError("Unsupported Tigerbyte CPU step")
        shim = stage / "sm8521.c"
        shim.write_text(code.replace(anchor, anchor + "   capture_pc(at);\n").replace(
            "int sm8521_step(sm8521_t *c)", "extern void capture_pc(uint16_t);\nint sm8521_step(sm8521_t *c)"),
            encoding="utf-8")
        sources[sources.index(original)] = shim
        compiler += [f"-I{source / 'cpu'}", "-DINDY_COVERAGE"]
    subprocess.run([*compiler, *sources, "-o", executable], check=True,
                   env=dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT / "build/zig-cache")))
    return executable


def capture(tape_lines, output, originals=ROOT / "originals", tigerbyte=None, zig="zig",
            dma=False, trace=None, audio=False, coverage=False, watch=None):
    tigerbyte = tigerbyte or Path(os.environ.get("TIGERBYTE_HOME") or
                                  Path(os.environ.get("LOCALAPPDATA", ".")) / "Programs/Tigerbyte")
    tape, snapshots = decode_tape(tape_lines)
    parent = ROOT / "build/indy-500"
    parent.mkdir(parents=True, exist_ok=True)
    stage = parent / ("reference-" + uuid4().hex)
    stage.mkdir()
    try:
        executable = build(stage, tigerbyte, zig, dma, coverage)
        bios = []
        for role, digest in (("Internal", "03368237e8fed4a8724f3b4a1596cf4b17c96d33"),
                             ("External", "97f782e72d738f4d7b861363266bf46b438d9b50")):
            raw = stage / (role + ".bin")
            unpack(originals / f"[BIOS] Tiger Game.com (USA) ({role}).zip", "sha1", digest, raw)
            bios.append(raw)
        cart = stage / "cart.tgc"
        unpack(originals / "indy-500/Indy 500 (USA).zip", "sha256", SHA256, cart, SIZE)
        env = os.environ.copy()
        if trace:
            env["INDY_TRACE"] = trace
        if watch:
            env["INDY_WATCH"] = f"{watch}:{stage / 'watch.csv'}"
        if coverage:
            env["INDY_COVERAGE"] = str(stage / "coverage.bin")
            env["INDY_ENTRY_STATE"] = str(stage / "entry-state.bin")
        run = subprocess.run([executable, *bios, cart,
                              stage / "trace.csv" if trace else "-",
                              stage / "dma.csv" if dma else "-",
                              stage / "audio.pcm" if audio else "-"],
                             input=tape, stdout=subprocess.PIPE, env=env, check=True)
        if len(run.stdout) != SNAPSHOT * len(snapshots):
            raise RuntimeError("Incomplete capture")
        output.mkdir(parents=True, exist_ok=False)
        for name in ("trace.csv", "dma.csv", "audio.pcm", "coverage.bin", "watch.csv", "entry-state.bin", "entry-state.bin.phase"):
            if (stage / name).exists():
                shutil.copyfile(stage / name, output / name)
        for i, (frame, label) in enumerate(snapshots):
            block = run.stdout[i * SNAPSHOT:(i + 1) * SNAPSHOT]
            png(block[:32000], output / f"{frame:06}-{label}.png")
            (output / f"{frame:06}-{label}.ram").write_bytes(block[32000:])
        return snapshots
    finally:
        shutil.rmtree(stage)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tape", type=Path)
    parser.add_argument("output", type=Path, help="new directory for private captures")
    parser.add_argument("--zig", default=os.environ.get("ZIG_EXE") or "zig")
    parser.add_argument("--dma", action="store_true")
    parser.add_argument("--trace", help="RAM range traced every frame, e.g. 0200-02ff")
    parser.add_argument("--audio", action="store_true")
    parser.add_argument("--watch", help="log CPU writes to a RAM range, e.g. 00cd-00ce")
    parser.add_argument("--coverage", action="store_true",
                        help="write coverage.bin: one byte per cartridge offset, 1 = instruction start executed")
    args = parser.parse_args()
    capture(args.tape.read_text().splitlines(), args.output, zig=args.zig, dma=args.dma,
            trace=args.trace, audio=args.audio, coverage=args.coverage, watch=args.watch)


if __name__ == "__main__":
    main()
