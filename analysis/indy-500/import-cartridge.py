"""Import the verified Indy 500 code banks into Ghidra and export a decompiled listing.

CPU 4000-5FFF holds bank 80 (file 0000), 6000-7FFF bank 81 (file 2000) or the
overlay bank 83 (file 6000), and 8000-9FFF bank 82 (file 4000). The private
project and the listings are written to build/indy-500/ghidra/.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
from uuid import uuid4
import zipfile

SIZE = 0x100000
SHA256 = "58cc3a3a4c33fec49bb9260ae83650a6d22b1004b4767a61307d197f6e423d5a"
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def cartridge(path):
    if path.suffix.lower() == ".zip":
        with zipfile.ZipFile(path) as archive:
            for entry in archive.infolist():
                if entry.file_size == SIZE:
                    image = archive.read(entry)
                    if hashlib.sha256(image).hexdigest() == SHA256:
                        return image
    elif path.stat().st_size == SIZE:
        image = path.read_bytes()
        if hashlib.sha256(image).hexdigest() == SHA256:
            return image
    raise ValueError("Unsupported Indy 500 cartridge")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cartridge", type=Path, nargs="?",
                        default=ROOT / "originals/indy-500/Indy 500 (USA).zip")
    parser.add_argument("--ghidra", type=Path, default=os.environ.get("GHIDRA_HOME"))
    parser.add_argument("--fresh", action="store_true", help="delete and recreate the project")
    parser.add_argument("--coverage", type=Path, action="append", default=[],
                        help="coverage.bin from scripts/indy-500/reference/capture.py (repeatable)")
    args = parser.parse_args()
    image = cartridge(args.cartridge)
    build = ROOT / "build/indy-500/ghidra"
    if args.fresh and build.exists():
        shutil.rmtree(build)
    build.mkdir(parents=True, exist_ok=True)
    home = args.ghidra or Path(os.environ["LOCALAPPDATA"]) / "Programs/Ghidra/ghidra_12.1.2_PUBLIC"
    headless = home / "support" / ("analyzeHeadless.bat" if os.name == "nt" else "analyzeHeadless")
    env = os.environ.copy()
    if not env.get("JAVA_HOME") and (home.parent / "java").exists():
        candidates = sorted((home.parent / "java").glob("jdk-*"))
        if candidates:
            env["JAVA_HOME"] = str(candidates[-1])
    stage = build / ("import-" + uuid4().hex)
    stage.mkdir()
    try:
        binary = stage / "Indy500.bin"
        binary.write_bytes(image[0x0000:0x2000] + image[0x2000:0x4000] + image[0x4000:0x6000])
        overlay = stage / "bank83.bin"
        overlay.write_bytes(image[0x6000:0x8000])
        executed = bytearray(SIZE)
        for path in args.coverage:
            data = path.read_bytes()
            if len(data) != SIZE:
                raise ValueError(f"{path} is not an Indy 500 coverage map")
            executed = bytearray(a | b for a, b in zip(executed, data))
        seeds = stage / "executed.txt"
        seeds.write_text("\n".join(f"{i:05x}" for i in range(0x8000) if executed[i]))
        command = [str(headless), str(build), "Indy500"]
        if (build / "Indy500.gpr").exists():
            command += ["-process", binary.name, "-noanalysis"]
        else:
            command += ["-import", str(binary), "-processor", "SM8500:BE:16:cart",
                        "-loader", "BinaryLoader", "-loader-baseAddr", "4000", "-noanalysis"]
        command += ["-scriptPath", str(HERE / "ghidra"), "-postScript", "Indy500.java",
                    str(overlay), str(build / "decompiled"), str(seeds)]
        result = subprocess.run(command, env=env, stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        sys.stdout.buffer.write(result.stdout[-4000:])
        result.check_returncode()
        if b"Indy 500 analysis exported" not in result.stdout:
            raise RuntimeError("Ghidra analysis script did not complete")
    finally:
        shutil.rmtree(stage)


if __name__ == "__main__":
    main()
