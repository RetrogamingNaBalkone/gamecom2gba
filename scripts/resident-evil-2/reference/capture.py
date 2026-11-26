"""Replay an original game.com input tape in Tigerbyte and record LCD/audio traces."""
import argparse
from contextlib import contextmanager
import csv
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
from uuid import uuid4
import wave
import zipfile
import zlib


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SHA256 = "80d449b89d02ba07ec386c54724e02033eb5373cb6621d5762f7184029ff2cac"
KEYS = {"UP": (0, 1), "DOWN": (0, 2), "LEFT": (0, 4), "RIGHT": (0, 8),
        "MENU": (0, 16), "PAUSE": (0, 32), "SOUND": (0, 64), "A": (0, 128),
        "B": (1, 1), "C": (1, 2), "D": (2, 2)}


@contextmanager
def staging_directory(parent):
    stage = parent / ("reference-" + uuid4().hex)
    stage.mkdir()
    try:
        yield stage
    finally:
        shutil.rmtree(stage)


def decode_tape(path, checkpoints_only=False):
    snapshots, rows = [], []
    frame = 0
    lines = [line.partition("#")[0].split() for line in path.read_text().splitlines()]
    commands = [fields for fields in lines if fields]
    for i, words in enumerate(commands):
        duration, label, *inputs = words
        ports, column, row = [255] * 3, -1, 0
        for key in inputs:
            if key.startswith("@"):
                column, row = map(int, key[1:].split(","))
                if not (0 <= column < 13 and 0 <= row < 10):
                    raise ValueError(key)
            else:
                port, mask = KEYS[key]
                ports[port] &= ~mask
        ticks = int(duration)
        if ticks <= 0:
            raise ValueError(duration)
        frame += ticks
        observe = not checkpoints_only or label != "input" or i == len(commands) - 1
        rows.append(f"{ticks if observe else -ticks} {ports[0]:x} {ports[1]:x} {ports[2]:x} {column} {row}")
        if observe:
            snapshots.append((frame, label))
    return "\n".join(rows).encode(), snapshots


def unpack(archive, algorithm, expected, destination):
    with zipfile.ZipFile(archive) as source:
        for member in source.infolist():
            if member.is_dir():
                continue
            data = source.read(member)
            if hashlib.new(algorithm, data).hexdigest() == expected:
                destination.write_bytes(data)
                return
    raise ValueError(f"Unrecognized contents in {archive}")


def png(shades):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))
    palette = bytes.fromhex("000000 0f4f2f 6f8f4f 8fcf8f dfff8f")
    scanlines = b"".join(b"\0" + shades[y:y+200] for y in range(0, len(shades), 200))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 200, 160, 8, 3, 0, 0, 0))
            + chunk(b"PLTE", palette) + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))



def verify_campaign(output):
    with (output / "trace.csv").open(newline="") as source:
        states = {int(row["frame"]): row for row in csv.DictReader(source)}
    with (output / "lcd.csv").open(newline="") as source:
        images = {row["label"]: row for row in csv.DictReader(source)}

    def ram(label):
        state = states[int(images[label]["frame"])]
        data = bytearray(0x29c)
        data[0xc0:0xe0] = bytes.fromhex(state["ram_c0_df"])
        data[0x208:0x241] = bytes.fromhex(state["ram_208_240"])
        data[0x241:] = bytes.fromhex(state["ram_241_29b"])
        return data

    assert ram("gator-defeated")[0xc2] & 0x40, "Crocodile defeat flag missing"
    entry = ram("final-encounter")
    assert entry[0x208:0x20a] == bytes([4, 107])
    assert entry[0x217] == 12 and entry[0x21d] == 125, "Final boss spawn differs"
    assert ram("final-enemy-defeated")[0xc2] & 1, "Final boss defeat flag missing"
    ending = ram("the-end")
    assert ending[0xcc:0xce] == bytes([40, 33])
    assert images["the-end"]["sha256_shades"] == "84ac973156d3f3169e420eaae99511607d3a879292acecf68d8adda3600b1b3c"
    assert ram("return-to-title")[0xcc] in (0, 1), "Title sequence did not restart"
    print("Original campaign verified through THE END. Clear time %02d:%02d:%02d" % tuple(ending[0x284:0x287]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tape", type=Path)
    parser.add_argument("output", type=Path, help="new directory for private reference captures")
    parser.add_argument("--originals", type=Path, default=ROOT / "originals")
    parser.add_argument("--tigerbyte", type=Path, default=os.environ.get("TIGERBYTE_HOME") or
                        Path(os.environ.get("LOCALAPPDATA", ".")) / "Programs/Tigerbyte")
    parser.add_argument("--zig", default=os.environ.get("ZIG_EXE") or "zig")
    parser.add_argument("--dma", action="store_true", help="capture DMA transfer starts and LCDC writes")
    parser.add_argument("--state", action="store_true", help="trace RAM at C0-DF and 0208-029B")
    parser.add_argument("--setup", help="diagnostic RAM setup frame:hex-address=hex-byte[:...]")
    parser.add_argument("--nvram-in", type=Path, help="start with an earlier 8192-byte original NVRAM")
    parser.add_argument("--nvram-out", action="store_true", help="retain final NVRAM as original.nv")
    parser.add_argument("--checkpoints-only", action="store_true",
                        help="record named checkpoints and the last input, skipping the WAV and per-frame traces")
    parser.add_argument("--verify-campaign", action="store_true",
                        help="check the complete cartridge campaign tape, including the ending image")
    args = parser.parse_args()
    if args.verify_campaign:
        args.checkpoints_only = args.state = True
    tape, snapshots = decode_tape(args.tape, args.checkpoints_only)
    parent = ROOT / "build/resident-evil-2"
    parent.mkdir(parents=True, exist_ok=True)
    with staging_directory(parent) as stage:
        source = args.tigerbyte / "src"
        executable = stage / "capture.exe"
        compiler = [args.zig, "cc", "-O2", f"-I{source}", str(HERE / "capture.c")]
        sources = sorted((source / "cpu").glob("*.c")) + sorted((source / "sys").glob("*.c"))
        if args.dma:
            original = source / "sys/gcbus.c"
            shim = stage / "gcbus.c"
            code = original.read_text(encoding="utf-8")
            anchor = "static void gcbus_dma(gcbus_t *b)\n{"
            if code.count(anchor) != 1:
                raise RuntimeError("Unsupported Tigerbyte DMA entry point")
            shim.write_text(code.replace(anchor, "extern void capture_dma_begin(void);\n" +
                            anchor + "\n   capture_dma_begin();"), encoding="utf-8")
            sources[sources.index(original)] = shim
            compiler.append(f"-I{source / 'sys'}")
        subprocess.run([*compiler, *sources, "-o", executable], check=True,
                       env=dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT / "build/zig-cache")))
        bios = []
        for role, digest in (("Internal", "03368237e8fed4a8724f3b4a1596cf4b17c96d33"),
                             ("External", "97f782e72d738f4d7b861363266bf46b438d9b50")):
            raw = stage / (role + ".bin")
            unpack(args.originals / f"[BIOS] Tiger Game.com (USA) ({role}).zip", "sha1", digest, raw)
            bios.append(raw)
        cart = stage / "cart.tgc"
        unpack(args.originals / "resident-evil-2/Resident Evil 2 (USA).zip", "sha256", SHA256, cart)
        pcm = stage / "audio.pcm"
        trace = stage / "trace.csv"
        dma = stage / "dma.csv"
        environment = os.environ.copy()
        if args.checkpoints_only:
            environment["RE2_CAPTURE_CHECKPOINTS"] = "1"
        if args.state:
            environment["RE2_CAPTURE_STATE"] = "1"
        if args.setup:
            environment["RE2_CAPTURE_SETUP"] = args.setup
        if args.nvram_in:
            environment["RE2_CAPTURE_NVRAM_IN"] = str(args.nvram_in.resolve())
        if args.nvram_out:
            environment["RE2_CAPTURE_NVRAM_OUT"] = str(stage / "original.nv")
        run = subprocess.run([executable, *bios, cart, pcm, trace, dma if args.dma else "-"],
                             input=tape, stdout=subprocess.PIPE, env=environment, check=True)
        if len(run.stdout) != 32000 * len(snapshots):
            raise RuntimeError("Incomplete LCD capture")
        args.output.mkdir(parents=True, exist_ok=False)
        if args.setup:
            (args.output / "diagnostic-setup.txt").write_text(args.setup+"\n")
        if args.nvram_out:
            shutil.copyfile(stage / "original.nv", args.output / "original.nv")
        shutil.copyfile(trace, args.output / "trace.csv")
        if args.dma:
            shutil.copyfile(dma, args.output / "dma.csv")
        if not args.checkpoints_only:
            with pcm.open("rb") as source, wave.open(str(args.output / "audio.wav"), "wb") as audio:
                audio.setparams((2, 2, 44100, 0, "NONE", ""))
                while block := source.read(1048576):
                    audio.writeframesraw(block)
        with (args.output / "lcd.csv").open("w", newline="") as index:
            writer = csv.writer(index)
            writer.writerow(("frame", "label", "sha256_shades"))
            for i, (frame, label) in enumerate(snapshots):
                shades = run.stdout[i*32000:(i+1)*32000]
                (args.output / f"{frame:06}.png").write_bytes(png(shades))
                writer.writerow((frame, label, hashlib.sha256(shades).hexdigest()))
        if args.verify_campaign:
            verify_campaign(args.output)


if __name__ == "__main__":
    main()
