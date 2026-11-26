"""Compare puzzle interactions with the bank 24 SM8500 routine on identical state."""
import ctypes
import hashlib
import os
from pathlib import Path
import random
import subprocess
import zipfile
from capture import ROOT, SHA256, staging_directory

def main():
    src=Path(os.environ.get("TIGERBYTE_HOME",Path(os.environ["LOCALAPPDATA"])/"Programs/Tigerbyte"))/"src"
    gba=ROOT/"gba/resident-evil-2"
    with zipfile.ZipFile(next((ROOT/"originals/resident-evil-2").glob("*.zip"))) as z:
        rom=z.read(next(n for n in z.namelist() if n.lower().endswith(".tgc")))
    assert hashlib.sha256(rom).hexdigest()==SHA256
    with staging_directory(ROOT/"build/resident-evil-2") as stage:
        dll=stage/"puzzle.dll"
        subprocess.run(["zig","cc","-O2","-shared","-Wl,--export-all-symbols",
                        "-I",src,"-I",gba,Path(__file__).with_name("puzzle_compare.c"),
                        gba/"puzzle.c",gba/"progress.c",src/"cpu/sm8521.c",
                        src/"cpu/sm8521_disasm.c","-o",dll],check=True,
                       env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/"build/zig-cache")))
        lib=ctypes.CDLL(str(dll))
        try:
            lib.load_banks(rom)
            cases=0
            rng=random.Random(0x694d)
            for area,cam,low,high,face in (
                (1,1,105,145,1),(1,32,0,90,0x61),(1,70,145,165,2),
                (1,71,15,40,2),(1,71,95,115,2),(4,27,60,80,1),
                (4,27,85,100,1),(4,40,140,180,1),(5,53,153,173,2),
                (5,54,65,85,2),(5,73,92,108,2),(5,76,92,108,2)):
                for x in {max(0,low-1),low,high-1,high}:
                    for f in {face,face^0x40}:
                        for _ in range(100):
                            state=bytearray(0x29c)
                            state[0x208:0x20a]=bytes((area,cam))
                            state[0x233:0x237]=bytes((f,x,123,4))
                            state[0xd3:0xd5]=rng.randbytes(2)
                            state[0x262:0x268]=rng.choices([0,1,3,0x12,0x13,0x14,0x1b,0x1c],k=6)
                            state[0x268]=0xd
                            mismatch=lib.compare_puzzle(bytes(state))
                            assert mismatch==0,(hex(mismatch),area,cam,x,f,state[0xd3:0xd5])
                            cases+=1
            print(cases,"puzzle states matched the cartridge routine")
        finally:
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)

if __name__=="__main__":
    main()
