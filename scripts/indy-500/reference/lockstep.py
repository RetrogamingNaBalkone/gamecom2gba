"""Compare the shared runtime with Tigerbyte display period by display period.

    py scripts/indy-500/reference/lockstep.py [--frames N] [--tape TAPE]

Both start from the BIOS launch of the cartridge (gba/indy-500/entry_state.h). The
tape is replayed in Tigerbyte from power-on; the host run receives the inputs held
after the launch. The script reports the first display period whose RAM 0010-03FF
differs, with the differing addresses.
"""
import argparse
import csv
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from uuid import uuid4

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
import capture  # noqa: E402

LAUNCH = ["1200 input", "8 input @2,2"]
# Registers whose value depends on when the host samples them, not on the program.
IGNORED = set(range(0x00, 0x10)) | {0x14, 0x15, 0x32}


def entry_frame():
    text = (ROOT / "gba/indy-500/entry_state.h").read_text()
    return int(re.search(r"ENTRY_FRAME = (\d+)", text).group(1))


def host_inputs(tape, start):
    """Input rows for the host, beginning with the display period after `start`."""
    rows, frame = [], 0
    for line in tape:
        words = line.partition("#")[0].split()
        if not words:
            continue
        duration, _, *keys = words
        ports, touch = [255, 255, 255], False
        for key in keys:
            if key.startswith("@"):
                touch = True
                continue
            port, mask = capture.KEYS[key.upper()]
            ports[port] &= ~mask
        for _ in range(int(duration)):
            frame += 1
            if frame >= start:
                if touch:
                    raise ValueError("touch input after the launch is not supported")
                rows.append(ports)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--tape", type=Path, help="tape after the launch; default: no input")
    parser.add_argument("--zig", default=os.environ.get("ZIG_EXE") or "zig")
    parser.add_argument("--show", type=int, default=24, help="differing addresses to print")
    args = parser.parse_args()
    first = entry_frame()
    after = args.tape.read_text().splitlines() if args.tape else []
    total = first + args.frames
    tape = LAUNCH + after
    used = sum(int(l.split()[0]) for l in tape if l.split())
    if used < total:
        tape.append(f"{total - used} input")
    build = ROOT / "build/indy-500"
    stage = build / ("lockstep-" + uuid4().hex)
    stage.mkdir(parents=True)
    try:
        host = stage / "host.exe"
        sources = [ROOT / "scripts/indy-500/host/host.c", ROOT / "gba/sm8500.c", ROOT / "gba/gamecom.c",
                   ROOT / "gba/pcm_isr.c", ROOT / "gba/indy-500/launch.c"]
        subprocess.run([args.zig, "cc", "-O2", "-w", *map(str, sources), "-o", str(host)], check=True,
                       env=dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT / "build/zig-cache")))
        cart, bios = stage / "cart.bin", stage / "bios.bin"
        capture.unpack(ROOT / "originals/indy-500/Indy 500 (USA).zip", "sha256", capture.SHA256, cart, capture.SIZE)
        capture.unpack(ROOT / "originals/[BIOS] Tiger Game.com (USA) (External).zip", "sha1",
                       "97f782e72d738f4d7b861363266bf46b438d9b50", bios)
        rows = host_inputs(tape, first)[:args.frames]
        stdin = "".join(f"1 {a:x} {b:x} {c:x}\n" for a, b, c in rows)
        ram = stage / "host.ram"
        run = subprocess.run([str(host), str(cart), str(bios), str(args.frames), str(ram)],
                             input=stdin.encode(), stderr=subprocess.PIPE)
        sys.stderr.write(run.stderr.decode())
        reference = stage / "reference"
        capture.capture(tape, reference, trace="0000-03ff")
        with (reference / "trace.csv").open(newline="") as source:
            original = {int(r["frame"]): bytes.fromhex(r["ram"]) for r in csv.DictReader(source)}
        data = ram.read_bytes()
        frames = len(data) // 0x400
        for k in range(frames):
            mine = data[k * 0x400:(k + 1) * 0x400]
            theirs = original[first + k]
            diff = [a for a in range(0x400) if a not in IGNORED and mine[a] != theirs[a]]
            if diff:
                print(f"first difference after {k + 1} display periods (Tigerbyte frame {first + k}):")
                for a in diff[:args.show]:
                    print(f"  {a:04x}: host {mine[a]:02x}  Tigerbyte {theirs[a]:02x}")
                if len(diff) > args.show:
                    print(f"  ... {len(diff)} addresses differ")
                return 1
        print(f"RAM 0010-03FF matched Tigerbyte for {frames} display periods "
              f"(frames {first}-{first + frames - 1})")
        return 0
    finally:
        shutil.rmtree(stage)


if __name__ == "__main__":
    sys.exit(main())
