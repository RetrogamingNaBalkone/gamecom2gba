"""Import the startup MMU windows and the scene-data overlay into Ghidra."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
from uuid import uuid4
import zipfile


SIZE = 2 * 1024 * 1024
SHA256 = "80d449b89d02ba07ec386c54724e02033eb5373cb6621d5762f7184029ff2cac"
HERE = Path(__file__).resolve().parent


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
    raise ValueError("Unsupported Resident Evil 2 cartridge")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cartridge", type=Path)
    parser.add_argument("--ghidra", type=Path, default=os.environ.get("GHIDRA_HOME"))
    args = parser.parse_args()
    image = cartridge(args.cartridge)
    build = HERE.parents[1] / "build" / "resident-evil-2" / "ghidra"
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
        binary = stage / "RE2Startup.bin"
        overlay = stage / "scene-bank.bin"
        binary.write_bytes(b"".join(image[p*8192:(p+1)*8192] for p in (0x20, 0x21, 0x25)))
        overlay.write_bytes(b"".join(image[p*8192:(p+1)*8192] for p in (0x30,0x22,0x24,0x28,0x33,0x23,0x29,0x34,0x36)))
        command = [str(headless), str(build), "RE2Startup"]
        if (build / "RE2Startup.gpr").exists():
            command += ["-process", binary.name]
        else:
            command += ["-import", str(binary), "-processor", "SM8500:BE:16:cart",
                        "-loader", "BinaryLoader", "-loader-baseAddr", "4000"]
        command += ["-noanalysis", "-scriptPath", str(HERE / "ghidra"),
                    "-postScript", "RE2Startup.java", str(overlay)]
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        sys.stdout.buffer.write(result.stdout)
        result.check_returncode()
        if b"RE2 cartridge analysis verified" not in result.stdout:
            raise RuntimeError("Ghidra analysis script did not complete")
    finally:
        shutil.rmtree(stage)


if __name__ == "__main__":
    main()
