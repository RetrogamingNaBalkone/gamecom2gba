"""Audit score activity and sample requests for a normal-input Tigerbyte tape."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "scripts/resident-evil-2/reference"))
from capture import decode_tape, staging_directory, unpack, SHA256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tape", type=Path, nargs="+")
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--route", action="store_true",
                        help="log camera changes, persistent campaign flags and the ending instead of individual samples")
    output.add_argument("--calls", action="store_true",
                        help="log every sound call with its caller and current priority, including rejected calls")
    args = parser.parse_args()
    tiger = Path(os.environ.get("TIGERBYTE_HOME") or
                 Path(os.environ["LOCALAPPDATA"]) / "Programs/Tigerbyte")
    src = tiger / "src"
    parent = ROOT / "build/resident-evil-2"
    parent.mkdir(parents=True, exist_ok=True)
    with staging_directory(parent) as stage:
        binary = stage / "sound-trace.exe"
        inputs = sorted((src / "sys").glob("*.c")) + sorted((src / "cpu").glob("*.c"))
        subprocess.run([os.environ.get("ZIG_EXE","zig"),"cc","-O2","-I",src,
                        HERE / "trace-sound.c",*inputs,"-o",binary], check=True,
                       env=dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT / "build/zig-cache")))
        roms = []
        for role,digest in (("Internal","03368237e8fed4a8724f3b4a1596cf4b17c96d33"),
                            ("External","97f782e72d738f4d7b861363266bf46b438d9b50")):
            path = stage / (role + ".bin")
            unpack(ROOT / "originals" / f"[BIOS] Tiger Game.com (USA) ({role}).zip",
                   "sha1", digest, path)
            roms.append(path)
        cart = stage / "verified.tgc"
        unpack(ROOT / "originals/resident-evil-2/Resident Evil 2 (USA).zip", "sha256", SHA256, cart)
        for tape in args.tape:
            print(tape, flush=True)
            command=[binary,*roms,cart]
            if args.route:
                command.append("--route")
            if args.calls:
                command.append("--calls")
            subprocess.run(command, input=decode_tape(tape)[0], check=True)


if __name__ == "__main__":
    main()
