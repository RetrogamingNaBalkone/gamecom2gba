"""Import the verified game.com internal ROM and external BIOS kernel page into Ghidra.

CPU 1000-1FFF holds the internal ROM and 2000-3FFF external BIOS page 2, as mapped
while cartridges run. Pages 0-31 of the external BIOS are added as overlays at 4000.
The project and decompiled listings go to build/gamecom-bios/ghidra/.
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

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
INTERNAL = ("03368237e8fed4a8724f3b4a1596cf4b17c96d33", 0x1000)
EXTERNAL = ("97f782e72d738f4d7b861363266bf46b438d9b50", 0x40000)


def unpack(archive, digest, size):
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            if entry.file_size == size:
                data = source.read(entry)
                if hashlib.sha1(data).hexdigest() == digest:
                    return data
    raise ValueError(f"Unsupported BIOS image in {archive}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--originals", type=Path, default=ROOT / "originals")
    parser.add_argument("--ghidra", type=Path, default=os.environ.get("GHIDRA_HOME"))
    parser.add_argument("--fresh", action="store_true")
    args = parser.parse_args()
    internal = unpack(args.originals / "[BIOS] Tiger Game.com (USA) (Internal).zip", *INTERNAL)
    external = unpack(args.originals / "[BIOS] Tiger Game.com (USA) (External).zip", *EXTERNAL)
    build = ROOT / "build/gamecom-bios/ghidra"
    if args.fresh and build.exists():
        shutil.rmtree(build)
    build.mkdir(parents=True, exist_ok=True)
    home = args.ghidra or Path(os.environ["LOCALAPPDATA"]) / "Programs/Ghidra/ghidra_12.1.2_PUBLIC"
    headless = home / "support" / ("analyzeHeadless.bat" if os.name == "nt" else "analyzeHeadless")
    env = os.environ.copy()
    if not env.get("JAVA_HOME") and (home.parent / "java").exists():
        env["JAVA_HOME"] = str(sorted((home.parent / "java").glob("jdk-*"))[-1])
    stage = build / ("import-" + uuid4().hex)
    stage.mkdir()
    try:
        binary = stage / "Kernel.bin"
        binary.write_bytes(internal + external[0x4000:0x6000])
        pages = stage / "pages.bin"
        pages.write_bytes(external)
        command = [str(headless), str(build), "GameComBios"]
        if (build / "GameComBios.gpr").exists():
            command += ["-process", binary.name, "-noanalysis"]
        else:
            command += ["-import", str(binary), "-processor", "SM8500:BE:16:cart",
                        "-loader", "BinaryLoader", "-loader-baseAddr", "1000", "-noanalysis"]
        command += ["-scriptPath", str(HERE / "ghidra"), "-postScript", "GameComBios.java",
                    str(pages), str(build / "decompiled")]
        result = subprocess.run(command, env=env, stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        sys.stdout.buffer.write(result.stdout[-3000:])
        result.check_returncode()
        if b"game.com BIOS analysis exported" not in result.stdout:
            raise RuntimeError("Ghidra analysis script did not complete")
    finally:
        shutil.rmtree(stage)


if __name__ == "__main__":
    main()
