"""Exercise gamecom2gba and compare the native intro to original Tigerbyte LCD hashes."""
import argparse
import ctypes
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import zipfile
from reference.capture import staging_directory

ROOT = Path(__file__).resolve().parents[2]
PALETTE = {tuple((rgb >> shift) & 31 for shift in (19, 11, 3)): shade
           for shade, rgb in enumerate((0, 0x0f4f2f, 0x6f8f4f, 0x8fcf8f, 0xdfff8f))}
# Original capture frame minus the first cartridge picture at 1516.
SHOTS = {
    52: "a9c819fc8e8a568c1787e0106f3f1476dcc0cefdaa7f89815dc0badaf5e3a4aa",
    412: "4b8414a40762fdca1dc8923a15bab25b95a8ea3454fb4c07fe43ff5f90ba29e2",
    892: "6c88e93105811a49d12cd903bde2f3d135ae9407e1bc7bb84b5168268a545693",
    2092: "3020f176ef07940cb848ab8d7ede216c483042c9d74b87d59fbece6bbc8ac3b5",
}
BEZEL_SHA256="44fee515c9f7c25d060dbbb957d6412ca93d20c98eb1cce454d6007bcfabca4f"

def lcd_hash(pixels):
    screen=bytes(PALETTE[pixels[y*240+x+20]] for y in range(160) for x in range(200))
    return hashlib.sha256(screen).hexdigest()

def check_scenes(mgba,core,rom):
    # Use the same isolated event and street backdrop as the original.
    for scene,trigger,snapshot,expected in (
        (25,2242,2343,"a911716f7c440020b86d0f1302916447b5d9a461f8023724d0640750d89375d2"),
        (26,2242,2250,"b14a1ff802ed66190c0373b5734da37aca8f0e9380917cec5a5decbe7c5e4acb"),
        (40,2250,2614,"ca7dfd426ef85fc777f50cf3a2e9d2131734a9897607accd514dd39d1d75b44a"),
        (40,2250,3094,"84ac973156d3f3169e420eaae99511607d3a879292acecf68d8adda3600b1b3c")):
        def trigger_event(frame,memory):
            if frame!=trigger:
                return
            base=memory[0x02000000]
            bag=ctypes.string_at(base,262144).index(bytes.fromhex("01 03 07 00 00 00 01"))
            state=(ctypes.c_uint8*0x29c).from_address(base+bag-0x262)
            if scene==40:
                state[0xc2]=1
                state[0xd7:0xdb]=[17,28,0,0]
            else:
                state[0xcc]=scene
                state[0x20b]|=2
        pixels=mgba.render(core,rom,walk=[(2092,2100,8),(2197,2202,8)],
                           final_frame=snapshot,video_probe=trigger_event)
        assert lcd_hash(pixels)==expected,(scene,snapshot,lcd_hash(pixels))
    print("Death and crane overlays, ending time and final picture matched the original diagnostics")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "dist/gamecom2gba.exe")
    parser.add_argument("--cart", type=Path, default=ROOT / "originals/resident-evil-2/Resident Evil 2 (USA).zip")
    parser.add_argument("--font-bios", type=Path, help="also validate the original system-font conversion")
    parser.add_argument("--core", type=Path, default=os.environ.get("MGBA_LIBRETRO"))
    args = parser.parse_args()
    core = args.core or next((Path(os.environ["LOCALAPPDATA"]) / "Programs/mGBA").glob("libretro-*/mgba_libretro.dll"))
    spec = importlib.util.spec_from_file_location("mgba", ROOT / "scripts/duke3d/test_preview.py")
    mgba = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mgba)
    parent=ROOT / "build/resident-evil-2"
    parent.mkdir(parents=True,exist_ok=True)
    with staging_directory(parent) as tmp:
        exe = Path(shutil.copy2(args.exe,tmp))
        subprocess.run([exe,args.cart.resolve()],check=True)
        path = tmp / "resident-evil-2.gba"
        rom = path.read_bytes()
        raw = tmp / "input.bin"
        with zipfile.ZipFile(args.cart) as z:
            raw.write_bytes(z.read(next(n for n in z.namelist() if n.lower().endswith(".tgc"))))
        subprocess.run([exe,raw],check=True)
        assert path.read_bytes() == rom
        with raw.open("r+b") as f:
            first = f.read(1)[0]
            f.seek(0)
            f.write(bytes([first ^ 1]))
        assert subprocess.run([exe,raw]).returncode and path.read_bytes() == rom
        for frame, expected in SHOTS.items():
            pixels = mgba.render(core,rom,final_frame=frame)
            border=bytes(c for y in range(160) for x in (*range(20),*range(220,240))
                         for c in pixels[y*240+x])
            assert hashlib.sha256(border).hexdigest()==BEZEL_SHA256
            result = lcd_hash(pixels)
            assert result == expected, (frame,result,expected)
            print("Original LCD matched on frame",frame)
        check_scenes(mgba,core,rom)
        print("ZIP and raw inputs produced identical",len(rom),"byte images")
        if args.font_bios:
            subprocess.run([exe,args.cart.resolve(),args.font_bios.resolve()],check=True)
            with_font=path.read_bytes()
            assert with_font!=rom
            damaged=tmp/"unrecognized-bios.bin"
            damaged.write_bytes(bytes(262144))
            assert subprocess.run([exe,args.cart.resolve(),damaged]).returncode
            assert path.read_bytes()==with_font
            print("Verified optional BIOS handling and preserved the output after rejection")

if __name__ == "__main__":
    main()

