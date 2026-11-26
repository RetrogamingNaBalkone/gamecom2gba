"""Exercise the inventory and first checkpoint in the mGBA libretro core using buttons."""
import argparse
import ctypes
import hashlib
import os
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/"scripts/duke3d"))
from test_preview import render,unpack_colors
sys.path.insert(0,str(ROOT/"scripts/resident-evil-2"))
from test_port import PALETTE

A,B,SELECT,UP,DOWN,LEFT,RIGHT,R=8,0,2,4,5,6,7,11  # libretro ids


def lcd(pixels,x,y,w,h):
    shades=bytes(PALETTE[pixels[row*240+col+20]] for row in range(y,y+h) for col in range(x,x+w))
    return hashlib.sha256(shades).hexdigest()


def menus(core,rom,bios_font=False):
    # The reference hashes are crops from Tigerbyte's raw 200x160 shade arrays.
    checks=[
        ("map", [UP,LEFT,A], (0,21,200,139), "9bd7c123f917f52fc0d4134851dc890272e0fa90ecdd03a14cae9b671d389955"),
        ("file list", [UP,LEFT,LEFT,A], (0,0,200,160), "806a5630dbeaad4e721b70664f126e4797260da565baf947ba79696be589907c"),
        ("handgun check", [A,DOWN,A], (2,101,130,44), "5904b8977b7db3f83775ba7a2a71b6231f4ad269cf9494226ea55ec764040b14"),
        ("equipped knife", [RIGHT,A,A,A,A], (106,15,45,27), "a870b409490d7350523fc9b0fac518b0b3307554960117ad6a215f55b4b89eb3"),
        ("ammunition", [DOWN,A], (31,51,74,40), "d9d7a54224a7482ed3ceda99ea327421bee4c697faa5c3040604171eeec6e8a2"),
    ]
    if bios_font:
        document_hashes = (
            "eb14f06513db9d11488475af9f985851a94034f0fa2b9e411564384a7d584e82",
            "db3504d813fedfd41e7038958e7ceb2734dcaad7119449c68813c50431b8686b",
            "0fc4a65713644f57ae9fa2e9cdb56fd08d55a5212ad83b929d3be8846a0ef337",
            "1a866ca4009d09de479ffdcae296f4e2d1a9e68ee93a63b6534cc99531b93f54",
            "09badf3da267bb4fac16d0e603425001ff4e8678181dff5528f636f2cb6edd22",
            "44d2c4c17fc66ac809dc8f23aa3d55400c17e4b653284922a18fb695304b185a",
            "4da01636cdba73458577328fbbd92a548b814778fa60c444905ca7de3fc97996",
            "22a8b6d09c51d1c2a77b3a2043657fd87f1a87e12d67e1b18dcf3dd02b5cce73",
            "ecf9f4006b40b22e66a32083c999668a15a054b236cc5a75dd3b1b5c967b7fe1",
            "a0b1a144c3122dab3ce2929ca42e615d48b119865949ee0b4fc3baa3c4cca1b6",
            "98765799958eb729297c29736901db95074f84d7ebee0ee8469df10b56aa744d",
            "d2cfbf3a73b5998bb48cf9c5bde7ad60fc5424d3f5f75579895beba29b6200f4",
        )
        for i,digest in enumerate(document_hashes):
            checks.append((f"file {i+1} first page",[UP,LEFT,LEFT,A]+[DOWN]*i+[A],(0,0,200,160),digest))
        pause=render(core,rom,walk=[(900,906,A),(940,946,A),(1000,1006,3)],final_frame=1040)
        assert lcd(pause,8,39,177,85)=="72f6a8b9a832a79bd9a45365f047b4b9ab27f4b0aba9709f19e89e9b4d2c0233"
        print("Original LCD matched: pause panel")
    for name,buttons,crop,expected in checks:
        tape=[(900,906,A),(940,946,A),(1000,1006,SELECT)]
        f=1040
        for button in buttons:
            tape.append((f,f+5,button));f+=20
        got=lcd(render(core,rom,walk=tape,final_frame=f),*crop)
        assert got==expected,(name,got,expected)
        print("Original LCD matched:",name)


# Plane and facing from the normal police-save.txt cartridge route. For doors
# the interval is a valid interaction range. Movement edges just need a heading.
# Rows are source area/camera, facing, depth, and optional door X interval.
WAYPOINTS={
    (0,0):(6,0), (0,1):(6,0), (0,2):(4,0), (0,3):(2,2,89,95),
    (0,4):(4,0), (0,5):(0,2,101,137), (0,6):(4,0), (0,7):(0,2,110,137),
    (0,9):(4,0), (0,10):(0,2,56,89), (0,12):(4,0), (0,13):(0,2,2,197),
    (0,14):(6,1), (0,15):(0,2), (0,16):(2,2,146,155), (0,17):(2,1),
    (0,18):(0,2,122,137), (0,19):(2,2), (0,20):(0,2,125,158), (0,21):(4,0),
    (0,22):(2,2,101,110), (0,23):(2,2), (0,24):(2,2), (0,25):(2,2),
    (0,26):(0,2,110,158), (0,27):(2,0), (0,30):(2,0), (0,31):(0,2,131,167),
    (1,1):(2,0), (1,2):(6,2), (1,5):(6,1)
}


class Progress:
    def __init__(self,signature):
        self.address=None
        self.signature=signature

    def read(self,memory):
        address=memory.get(0x02000000)
        if address is None: return None
        if self.address is None:
            image=ctypes.string_at(address,262144)
            pos=image.find(self.signature)
            if pos<0:return None
            if image.find(self.signature,pos+1)>=0:return None
            self.address=address+pos-0x262
        return ctypes.string_at(self.address,0x29c)


def first_checkpoint(core,rom):
    start=2220
    tape={}
    def add(frame,duration,*keys):
        for n in range(frame,frame+duration):
            tape.setdefault(n,set()).update(keys)
    add(2092,8,A);add(start,3,A)
    turn=start+25
    add(turn,20,RIGHT)
    aim=turn+20
    add(aim,630,R)
    for i in range(12):add(aim+30+50*i,25,A)
    add(aim+630,260,UP,B)
    state=Progress(bytes.fromhex("01 03 07 00 00 00 01"))
    rooms=[]
    last_room=None;since=0;armed=True;last_action=-100
    checkpoint=None
    def control(frame,memory):
        nonlocal last_room,since,armed,last_action,checkpoint
        if frame<=start:return tape.get(frame,())
        data=state.read(memory)
        if data is None:return tape.get(frame,())
        room=tuple(data[0x208:0x20a]);facing,lane=divmod(data[0x233],16)
        x=data[0x234];action=data[0x236]
        if room!=last_room:
            rooms.append(room);last_room=room;since=frame
        assert frame-since<3000 and data[0x238],(frame,room,x,lane,facing,action)
        if data[0xcc]==29 and data[0x298]==3:
            if checkpoint is None: checkpoint=(frame,data)
            if frame-checkpoint[0]>=20:return None
            return ()
        if frame<aim+830:return tape.get(frame,())
        if data[0x270]:return (A,) if frame%12<3 else ()
        if data[0x20b]&2:return ()
        if 0x6e<=action<=0x75:
            if frame%10==1:
                armed=False
                return (R,)
            return ()
        if action in (0x76,0x77):return ()
        def turn_to(target):
            return (RIGHT if (target-facing)%8<=4 else LEFT,)
        def walk(target,run=False):
            if target!=facing:return turn_to(target)
            return (UP,B) if run else (UP,)
        def interact():
            nonlocal armed,last_action
            if armed:
                if frame-last_action<12:return ()
                armed=False;last_action=frame
                return (R,)
            return (A,) if frame%24<8 else ()
        if room==(1,4):
            if not data[0x298]:
                if lane and 140<x<177:return walk(4)
                if x<150:return walk(2)
                if x>171:return walk(6)
                if lane:return walk(4)
                return interact()
            if lane<2:return walk(0)
            if x<130:return walk(2)
            if x>=145:return walk(6)
            return turn_to(2) if facing!=2 else interact()
        assert room in WAYPOINTS,room
        d,depth,*window=WAYPOINTS[room]
        if room==(0,5) and data[0x290]&2 and lane==0:
            if x<149:return walk(2,True)
            if x>175:return walk(6)
            return interact()
        if room==(0,13) and lane==0 and x<76:return walk(2,True)
        if depth!=lane:return walk(0 if depth>lane else 4)
        if window:
            lo,hi=window
            if x<lo:return walk(2,lo-x>20)
            if x>hi:return walk(6,x-hi>20)
            return turn_to(d) if facing!=d else interact()
        return walk(d,True)
    save=bytearray()
    image=render(core,rom,final_frame=45000,controller=control,save=save)
    assert checkpoint and rooms[-1]==(1,4) and len(rooms)>=32,rooms
    data=checkpoint[1]
    assert data[0x298]==3 and 11 in data[0x262:0x268] and data[0x296]>=40, data[0x290:0x29c].hex()
    assert save[:4]==bytes([0xa5,ord('R'),ord('2'),1])
    # Same TIME legend as the original typewriter. The six dynamic digits are
    # the session's actual elapsed time; independent routes can take different times.
    assert lcd(image,32,140,44,14)=="fe8798103fda7f9539e4db3fa5923af1fbc11dbc18326c8fba155f0287a26c13"
    print("Police typewriter reached at frame",checkpoint[0],"with TIME",
          "%02d:%02d:%02d"%tuple(data[0x284:0x287]))
    return save,data


def reload_checkpoint(core,rom,save,previous):
    progress=Progress(previous[0x262:0x269])
    loaded=[]
    def observe(frame,memory):
        if frame==2290:
            data=progress.read(memory)
            assert data
            loaded.append(data)
    render(core,rom,walk=[(2092,2100,A),(2170,2178,DOWN),(2220,2228,A)],
           final_frame=2290,save=save,video_probe=observe)
    assert loaded
    data=loaded[0]
    assert data[0x238]==100  # The original LOAD routine sets full health.
    for lo,hi in [(0x208,0x20a),(0x262,0x269),(0x290,0x29c)]:
        assert data[lo:hi]==previous[lo:hi],(hex(lo),data[lo:hi],previous[lo:hi])
    print("Load Game restored camera, carried items, pickup flags and ammunition, and set health to 100.")


def campaign_replay(core,rom,save,previous,tape=None):
    """Replay the post-load button tape and observe campaign state without modifying RAM."""
    buttons={"A":A,"B":B,"SELECT":SELECT,"UP":UP,"DOWN":DOWN,"LEFT":LEFT,"RIGHT":RIGHT,"R":R}
    tape=tape or Path(__file__).with_name("campaign-gba.txt")
    spans=[]
    stop=0
    for line in tape.read_text(encoding="ascii").splitlines():
        fields=line.partition("#")[0].split()
        if fields:
            stop+=int(fields[0])
            spans.append((stop,tuple(buttons[k] for k in fields[1:])))
    index=0
    progress=Progress(previous[0x262:0x269])
    areas=set()
    items=set()
    final=None
    crocodile_frame=None
    boss_entry=None
    title_frame=None
    end_picture_frame=None
    def controls(frame,memory):
        nonlocal index,final,title_frame,end_picture_frame,crocodile_frame,boss_entry
        if frame>2250:
            state=progress.read(memory)
            if state:
                if crocodile_frame is None and state[0x208:0x20a]==bytes([6,25]) and state[0xc2]&64:
                    crocodile_frame=frame
                if boss_entry is None and state[0x208:0x20a]==bytes([4,107]) and state[0x217]==12:
                    boss_entry=(frame,state[0x21d],state[0x238])
                areas.add(state[0x208])
                items.update(state[0x262:0x268])
                if final and state[0xc2]==0 and state[0x208:0x20a]==bytes(2):
                    title_frame=frame
                    return None
                if final is None and state[0xcc]==0x28 and state[0xcd]>=30 and state[0xc2]&4:
                    final=frame,state
                if final and end_picture_frame is None and state[0xcd]==33:
                    end_picture_frame=frame+2
        while index<len(spans) and frame>=spans[index][0]:
            index+=1
        return spans[index][1] if index<len(spans) else ()
    ending_image=[]
    def capture_ending(frame,ptr,pitch,step):
        if frame==end_picture_frame:
            raw=b"".join(ctypes.string_at(ptr+y*pitch+20*step,200*step) for y in range(160))
            ending_image.append((raw,step))
    render(core,rom,save=save,controller=controls,pixel_probe=capture_ending,final_frame=stop+8000)
    assert set(range(1,7))<=areas,sorted(areas)
    assert {0x11,0x14,0x15,0x16,0x17,0x18,0x1a,0x1b,0x1c}<=items,sorted(items)
    assert crocodile_frame,"The crocodile encounter did not finish"
    assert boss_entry and boss_entry[1]==125,"Final boss did not spawn with full health"
    print("Defeated the crocodile on frame",crocodile_frame,"and reached the final boss on frame",boss_entry[0])
    assert final,"The gameplay tape did not reach the ending"
    frame,state=final
    raw,step=ending_image[0]
    hashes=[]
    for pixel_format in ((1,) if step==4 else (0,2)):
        colors=unpack_colors(raw,pixel_format)
        if set(colors)<=PALETTE.keys():
            hashes.append(hashlib.sha256(bytes(PALETTE[c] for c in colors)).hexdigest())
    assert "84ac973156d3f3169e420eaae99511607d3a879292acecf68d8adda3600b1b3c" in hashes,hashes
    print("Original LCD matched: THE END")
    assert title_frame,"The ending did not return to the title"
    assert save[128:132]==bytes([0x2a])+state[0x284:0x287],"SRAM clear time differs from TIME"
    print("Ending returned to the title at frame",title_frame,"and recorded the clear time in SRAM.")
    print("Final cinematic reached on frame",frame,"with TIME","%02d:%02d:%02d"%tuple(state[0x284:0x287]))
    print("GBA SHA-256",hashlib.sha256(rom).hexdigest())

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--rom",type=Path,default=ROOT/"dist/resident-evil-2.gba")
    p.add_argument("--core",type=Path,default=os.environ.get("MGBA_LIBRETRO"))
    p.add_argument("--bios-font",action="store_true",help="verify the external BIOS text against Tigerbyte")
    p.add_argument("--campaign",action="store_true",help="play the post-load campaign tape through the finale")
    args=p.parse_args()
    core=args.core or next((Path(os.environ["LOCALAPPDATA"])/"Programs/mGBA").glob("libretro-*/mgba_libretro.dll"))
    rom=args.rom.read_bytes()
    menus(core,rom,args.bios_font)
    save,state=first_checkpoint(core,rom)
    reload_checkpoint(core,rom,save,state)
    if args.campaign:
        campaign_replay(core,rom,save,state)


if __name__=="__main__":
    main()
