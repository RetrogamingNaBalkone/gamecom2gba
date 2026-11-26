"""Compare the production scene renderer and its hold times with the US cartridge."""
import ctypes
import os
from pathlib import Path
import struct
import subprocess
import sys
from capture import HERE, ROOT, SHA256, decode_tape, staging_directory, unpack
sys.path.insert(0,str(HERE.parent))
from perf import diagnostic_rom, frame_runner


def verify_audio_clock(bank, timelines):
    core=os.environ.get("MGBA_LIBRETRO") or next(
        (Path(os.environ["LOCALAPPDATA"])/"Programs/mGBA").glob("libretro-*/mgba_libretro.dll"))
    image=diagnostic_rom(HERE/"audio_clock.c",driver="audio.c")+bank
    observed=set()
    worst=0
    def inspect(frame,memory):
        nonlocal worst
        if 0x03000000 not in memory:
            return
        address=memory[0x03000000]+0x7000
        sound,age,count=struct.unpack("<III",ctypes.string_at(address,12))
        if not 1<=sound<=len(timelines):
            return
        timeline=timelines[sound-1]
        if 0<count<len(timeline):
            elapsed=age*(280896/16777216)
            expected=(timeline[count]-timeline[0])/4915200
            worst=max(worst,abs(elapsed-expected))
            if count>len(timeline)*0.85:
                observed.add(sound)
    total=sum((t[-1]-t[0])/4915200 for t in timelines)
    frame_runner().render(core,image,final_frame=int(total*60)+90,video_probe=inspect)
    assert observed==set(range(1,25)),observed
    assert worst<=0.004,("maximum FIFO timing error",worst)
    print(f"All 24 GBA FIFO sample clocks matched the cartridge within {worst*1000:.2f} ms.")


def replay_to(lib,tape,limit):
    frames=0
    for line in tape.decode().splitlines():
        duration,a,b,c,column,row=line.split()
        step=min(int(duration),limit-frames)
        lib.advance(step,int(a,16),int(b,16),int(c,16),int(column),int(row))
        frames+=step
        if frames==limit: return
    raise ValueError("Input tape ends before checkpoint")


def main():
    rom=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/"dist/resident-evil-2.gba"
    data=rom.read_bytes()
    start=data.index(b"CIN1")
    assert data.find(b"CIN1",start+1)==-1
    cinema=data[start:]
    src=Path(os.environ.get("TIGERBYTE_HOME",Path(os.environ["LOCALAPPDATA"])/"Programs/Tigerbyte"))/"src"
    gba=ROOT/"gba/resident-evil-2"
    with staging_directory(ROOT/"build/resident-evil-2") as stage:
        dll=stage/"scenes.dll"
        sources=sorted((src/"cpu").glob("*.c"))+sorted((src/"sys").glob("*.c"))
        subprocess.run(["zig","cc","-O2","-shared","-Wl,--export-all-symbols","-I",src,
                        "-I",gba,HERE/"scene_compare.c",gba/"cinema.c",*sources,"-o",dll],
                       check=True,env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/"build/zig-cache")))
        images=[]
        for role,digest in (("Internal","03368237e8fed4a8724f3b4a1596cf4b17c96d33"),
                            ("External","97f782e72d738f4d7b861363266bf46b438d9b50")):
            path=stage/(role+".bin")
            unpack(ROOT/"originals"/f"[BIOS] Tiger Game.com (USA) ({role}).zip","sha1",digest,path)
            images.append(path)
        cart=stage/"cartridge.tgc"
        unpack(ROOT/"originals/resident-evil-2/Resident Evil 2 (USA).zip","sha256",SHA256,cart)
        lib=ctypes.CDLL(str(dll))
        try:
            tape,_=decode_tape(HERE/"start.txt")
            paths=list(map(os.fsencode,[*images,cart]))
            assert lib.boot(*paths)
            lib.re2_cinema_init(cinema)
            replay_to(lib,tape,1515)
            result=lib.compare_openings()
            assert result==0,("opening frame",result)
            print("Both complete opening sequences matched original pictures and cue times.")
            assert lib.boot(*paths)
            replay_to(lib,tape,3789)
            lib.save_checkpoint()
            for scene in range(3,41):
                result=lib.compare(scene)
                assert result==0,(scene,result)
            print("38 scene timelines and PCM cues matched. Full-screen cinematic layers matched original displayed pixels.")
            directory=data.index(struct.pack("<II",44,44+32+19200))
            bank=directory+struct.unpack_from("<I",data,directory+32)[0]
            timelines=[]
            for i in range(24):
                record=bank+struct.unpack_from("<I",data,bank+4*i)[0]
                length=struct.unpack_from("<I",data,record)[0]
                result=lib.compare_sample(i+1,data[record+4:record+4+length],length)
                assert result==0,("PCM",i+1,length,result)
                timelines.append([lib.sample_cycle_at(j) for j in range(length)])
            print("All 24 converted PCM samples matched the original DAC output and sample counts.")
            bank_end=directory+struct.unpack_from("<I",data,directory+36)[0]
            verify_audio_clock(data[bank:bank_end],timelines)
        finally:
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)


if __name__=="__main__":
    main()
