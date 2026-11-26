"""Open the verified Tiger Web Link startup banks in Ghidra."""

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
SIZE = 0x40000
SHA256 = "50cf190d108197e857aff4484a43c1de417b9f3c0ceafd0210846d46e2a31347"


def read_rom(path):
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
    raise ValueError("Unsupported Tiger Web Link cartridge")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cartridge", type=Path)
    parser.add_argument("--ghidra", type=Path, default=os.environ.get("GHIDRA_HOME"))
    args = parser.parse_args()
    rom = read_rom(args.cartridge)
    home = args.ghidra or Path(os.environ["LOCALAPPDATA"]) / "Programs/Ghidra/ghidra_12.1.2_PUBLIC"
    headless = home / "support" / ("analyzeHeadless.bat" if os.name == "nt" else "analyzeHeadless")
    projects = HERE.parents[1] / "build" / "tiger-web-link" / "ghidra"
    projects.mkdir(parents=True, exist_ok=True)
    stage = projects / ("import-" + uuid4().hex)
    stage.mkdir()
    try:
        binary = stage / "WebLink.bin"
        command = [str(headless), str(projects), "WebLink"]
        if (projects / "WebLink.gpr").exists():
            command += ["-process", binary.name]
        else:
            # MMU2..4 map cartridge banks 20h..22h at CPU 4000h..9fffh.
            binary.write_bytes(rom[:0x6000])
            command += ["-import", str(binary), "-processor", "SM8500:BE:16:cart",
                        "-loader", "BinaryLoader", "-loader-baseAddr", "4000"]
        command += ["-noanalysis", "-scriptPath", str(HERE / "ghidra"), "-postScript", "WebLink.java"]
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        sys.stdout.buffer.write(result.stdout)
        result.check_returncode()
        if b"Web Link command table verified" not in result.stdout:
            raise RuntimeError("Ghidra did not verify the Web Link command table")
    finally:
        shutil.rmtree(stage)


if __name__ == "__main__":
    main()
