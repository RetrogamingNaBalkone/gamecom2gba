"""Count LCD changes while Leon runs across the second street camera."""
import argparse
import csv
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import zlib

from perf import ROOT, frame_runner, staging_directory


def report(name, samples,camera):
    assert len(samples)>60
    changes=sum(a!=b for a,b in zip(samples,samples[1:]))
    hz=59.7275 if name=="mGBA" else 59.732155
    print(f"{name}: {len(samples)} display periods in camera {camera}, "
          f"{changes} visible changes, {changes*hz/(len(samples)-1):.2f} changes/s")


def checkpoint_payload(path):
    nv=bytearray(path.read_bytes())
    if len(nv)!=8192:
        raise ValueError("Expected an 8192-byte game.com NVRAM image")
    second=nv.find(bytes.fromhex("1b 3b 01"))
    if second<32 or b"RE2" not in nv[max(0,second-100):second-32]:
        raise ValueError("The NVRAM lacks Resident Evil 2 typewriter records")
    first=second-32
    # Controlled entry to room 7 from room 6. All combat and camera
    # transitions after LOAD are driven by holding Forward and Run.
    nv[first:first+2]=bytes((0,6))
    payload=bytearray(nv[first:first+31]+nv[second+3:second+24])
    payload[41:47]=bytes([255])*6
    nv[second+3:second+24]=payload[31:]
    save=bytearray(131072)
    save[:5]=bytes((165,82,50,1,0))
    save[5:57]=payload
    checksum=2166136261
    for byte in save[1:57]:
        checksum=((checksum^byte)*16777619)&0xffffffff
    struct.pack_into("<I",save,57,checksum)
    return nv,save


def original(checkpoint=None):
    with staging_directory(ROOT/"build/resident-evil-2") as work:
        prefix=(Path(__file__).parent/"reference/start.txt").read_text().split("240 after-selection")[0]
        lines=[prefix,"45 ready","14 turn RIGHT","30 draw C"]
        for i in range(12):
            lines.extend((f"25 fire{i} C A", f"25 wait{i} C"))
        lines.extend(f"1 step{i} UP B" for i in range(350))
        extra=[]
        camera_id="0001"
        if checkpoint:
            nv,_=checkpoint_payload(checkpoint)
            nvpath=work/"checkpoint.nv"
            nvpath.write_bytes(nv)
            extra=["--nvram-in",nvpath]
            lines=[(Path(__file__).parent/"reference/load.txt").read_text()]
            lines.extend(f"1 step{i} UP B" for i in range(240))
            camera_id="0007"
        tape=work/"motion.txt"
        tape.write_text("\n".join(lines)+"\n")
        target=work/"reference"
        subprocess.run([sys.executable,Path(__file__).parent/"reference/capture.py",
                        tape,target,"--state",*extra],check=True)
        with (target/"trace.csv").open() as f:
            camera={int(r["frame"]) for r in csv.DictReader(f) if r["ram_208_240"][:4]==camera_id}
        with (target/"lcd.csv").open() as f:
            hashes=[r["sha256_shades"] for r in csv.DictReader(f) if int(r["frame"]) in camera]
        report("Tigerbyte",hashes,7 if checkpoint else 1)


def unblended(image):
    """Build the same runtime, changing only Leon's displayed OBJ coordinate."""
    header=next((i for n in range(10,33) if
                 (i:=image.find(struct.pack("<II",n*4,n*4+19232)))>=0),None)
    if header is None:raise ValueError("Missing asset directory")
    sources=ROOT/"gba/resident-evil-2"
    with staging_directory(ROOT/"build/resident-evil-2") as stage:
        env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/"build/zig-cache"))
        def run(*args):subprocess.run(args,check=True,cwd=stage,env=env)
        objects=[]
        for source in [sources/"start.S",*sorted(sources.glob("*.c"))]:
            obj=source.stem+".o"
            objects.append(obj)
            if source.name=="street.c":
                code=source.read_text()
                anchor="int x=a==&player?walk_display.x:a->x;"
                assert code.count(anchor)==1
                source=stage/"street.c"
                source.write_text(code.replace(anchor,"int x=a->x;"))
            target="arm" if source.suffix==".S" else "thumb"
            run("zig","cc","-target",target+"-freestanding-eabi","-mcpu=arm7tdmi","-Oz",
                "-ffreestanding","-fno-unwind-tables","-fno-exceptions","-I",sources,
                "-c",source,"-o",obj)
        run("zig","ld.lld","--oformat=binary","-T",sources/"rom.ld",*objects,"-o","game.gba")
        fixer=os.environ.get("GBAFIX") or shutil.which("gbafix") or (
            Path(os.environ["LOCALAPPDATA"])/"Programs/gba-tools/gbafix.exe")
        run(fixer,"game.gba","-tRE2-GAMECOM","-cGRE2","-m00")
        return (stage/"game.gba").read_bytes()+image[header:]


def native(rom,core,checkpoint=None):
    turn=2245
    ready=turn+20
    run=ready+630
    tape=[(2092,2100,8),(2197,2200,8),(turn,ready,7),(ready,run,11),
          (run,run+350,4),(run,run+350,0)]
    for i in range(12):
        tape.append((ready+30+50*i,ready+55+50*i,8))
    save=None
    camera=1
    end=run+350
    if checkpoint:
        _,save=checkpoint_payload(checkpoint)
        tape=[(2092,2100,8),(2170,2178,5),(2197,2205,8),(2295,2535,4),(2295,2535,0)]
        camera=7
        end=2540
    state=None
    room=None
    samples=[]
    def observe(frame,memory):
        nonlocal state,room
        if frame<2220:return
        if state is None:
            base=memory[0x02000000]
            marker=bytes(save[19:26]) if save else bytes.fromhex("01 03 07 00 00 00 01")
            offset=ctypes.string_at(base,262144).find(marker)
            if offset<0:return
            state=base+offset-0x262
        room=ctypes.string_at(state+0x208,2)
    def pixels(frame,ptr,pitch,step):
        if room==bytes((0,camera)):
            crc=0
            for y in range(160):
                crc=zlib.crc32(ctypes.string_at(ptr+pitch*y+20*step,200*step),crc)
            samples.append(crc)
    frame_runner().render(core,rom,walk=tape,final_frame=end,
                          video_probe=observe,pixel_probe=pixels,save=save)
    report("mGBA",samples,camera)


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom",type=Path)
    parser.add_argument("--original",action="store_true")
    parser.add_argument("--checkpoint",type=Path,help="compare the two-enemy room using an original typewriter NVRAM")
    parser.add_argument("--unblended",action="store_true",help="also compare identical game logic with unsmoothed OBJ coordinates")
    parser.add_argument("--core",type=Path,default=os.environ.get("MGBA_LIBRETRO"))
    args=parser.parse_args()
    core=args.core or next((Path(os.environ["LOCALAPPDATA"])/"Programs/mGBA").glob("libretro-*/mgba_libretro.dll"))
    if args.original:original(args.checkpoint)
    image=args.rom.read_bytes()
    if args.unblended:
        print("Control using logical OBJ positions")
        native(unblended(image),core,args.checkpoint)
    native(image,core,args.checkpoint)
