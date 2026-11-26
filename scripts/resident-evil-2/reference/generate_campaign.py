"""Generate and verify a GBA button tape from New Game through the normal ending."""
import argparse
import ctypes
import os
import struct
from collections import deque
from itertools import groupby
from pathlib import Path

from test_gameplay import A, B, DOWN, LEFT, R, RIGHT, SELECT, UP, ROOT, Progress, first_checkpoint, campaign_replay, render
from campaign_fight import find_winning_inputs,save_state
from capture import staging_directory

HERE = Path(__file__).resolve().parent
BUTTONS = {A:"A", B:"B", UP:"UP", DOWN:"DOWN", LEFT:"LEFT", RIGHT:"RIGHT", R:"R", SELECT:"SELECT"}
# Targets follow the first typewriter save. The keys and sockets on each side
# of the police station connect the two wings before the laboratory opens.
OBJECTIVES = [(1,1),(1,15),(1,20),(5,19),(5,20),(5,3),(5,4),(1,32),(5,53),(5,54),
              (1,8),(1,62),(2,0),(2,10),(3,27),(3,39),(4,0),(4,31),(4,27),(4,95),(4,107)]


def load_waypoints():
    result = {}
    for line in (HERE/"campaign-waypoints.txt").read_text().splitlines():
        fields = line.partition("#")[0].split()
        if fields:
            objective = int(fields[0])
            area, camera = (int(value,16) for value in fields[1].split(":"))
            result[objective,(area,camera)] = [tuple(map(int,w.split(","))) for w in fields[2:]]
    return result


def ammunition_locations(rom):
    catalog = rom.index(b"CAM1")
    directory, records = struct.unpack_from("<II",rom,catalog+48)
    starts = (0,33,109,150,193,305,383,420)
    locations = {}
    for area in range(7):
        for view in range(starts[area],starts[area+1]):
            entry = catalog+directory+2*view
            first, count = rom[entry:entry+2]
            for index in range(first,first+count):
                record = catalog+records+8*index
                address,mask,item,_,x,_,width,_ = rom[record:record+8]
                if item==7:
                    locations[area,view-starts[area]] = (0x290+address,mask,x,width)
    return locations


class Pilot:
    def __init__(self,rom,previous):
        self.progress = Progress(previous[0x262:0x269])
        self.windows = load_waypoints()
        self.ammunition = ammunition_locations(rom)
        self.objective = 0
        self.previous_room = None
        self.entered = 0
        self.menu = deque()
        self.inputs = []
        self.visited_crocodile = False
        self.crocodile_done = False
        self.aiming = False

    def queue(self,keys,delay):
        for key in keys:
            self.menu.extend([()]*delay+[(key,)]*4)

    def advance(self):
        self.objective += 1
        self.entered = self.frame

    def turn(self,direction):
        return (RIGHT if (direction-self.facing)%8<=4 else LEFT,)

    def walk(self,direction,run=False):
        if direction!=self.facing:
            return self.turn(direction)
        return (UP,B) if run else (UP,)

    def interact(self,direction,plane,left,right):
        if self.plane!=plane:
            return self.walk(0 if plane>self.plane else 4)
        if self.x<left:
            return self.walk(2,left-self.x>20)
        if self.x>right:
            return self.walk(6,self.x-right>20)
        if self.facing!=direction:
            return self.turn(direction)
        return (A,) if self.frame%24<6 else ()

    def approach_item(self,direction,left,right):
        # The hotspot tests the exact floor Y. Let the step between planes
        # finish if the animation has already changed the reported plane.
        if self.state[0x235]!=155:
            if self.plane==0 and self.action not in (0x6a,0x6b):
                return self.walk(0)
            return self.walk(0 if self.action==0x6a else 4)
        return self.interact(direction,0,left,right)

    def push(self,direction,left,right,tolerance):
        if self.plane!=2 and (self.x<left-tolerance or self.x>right+tolerance):
            return self.walk(2 if self.x<left else 6,tolerance!=25)
        return self.interact(direction,2,left,right)

    def operate(self):
        stage = self.objective
        ram = self.state
        bag = ram[0x262:0x268]
        box = ram[0x20e]
        if stage==0 and 0x17 not in bag:
            return self.interact(0,1,110,135)
        if stage==1:
            return self.interact(0,2,123,140)
        if stage in (3,4,5,6) and not ram[0x20f]&(1<<(stage-3)):
            if stage in (3,5):
                return self.push(2,box-11,box-3,25)
            return self.push(6,box+(29 if stage==4 else 23),box+(38 if stage==4 else 31),25)
        if stage==7 and 0x14 not in bag:
            if ram[0x268]!=13:
                self.queue((SELECT,RIGHT,UP,A,A,A,A,SELECT),10)
                return ()
            return self.interact(6,1,15,85)
        if (stage==8 and not ram[0xd3]&32) or (stage==9 and 0x15 not in bag):
            left,right = (155,168) if stage==8 else (68,80)
            if self.x<left-20 or self.x>right+20:
                return self.walk(4) if self.plane==2 else self.walk(2 if self.x<left else 6,True)
            return self.interact(0,2,left,right)
        if stage==10 and 0x16 not in bag:
            if self.plane and self.x<140:
                return self.walk(2,True)
            return self.approach_item(2,160,185)
        if stage==13 and not ram[0x20f]&1:
            return self.push(6,box+98,box+108,20)
        if stage==14:
            if ram[0x235]>=100:
                if self.plane==2 and self.action not in (0x6a,0x6b):
                    return self.walk(4)
                return self.walk(4 if self.action==0x6b else 0)
            return self.interact(0,2,42,63)
        if stage in (15,17,19) and {15:0x1a,17:0x1b,19:0x11}[stage] not in bag:
            if self.plane and self.x>70:
                return self.walk(6,True)
            return self.approach_item(6,24,47)
        if stage==18 and not ram[0x20b]&4:
            return self.interact(0,1,87,97)
        self.advance()
        return None

    def navigate(self):
        windows = self.windows[self.objective,self.room]
        def distance(window):
            _,_,plane,left,right=window
            return abs(plane-self.plane)*35+max(0,left-self.x,self.x-right)
        door,direction,plane,left,right = min(windows,key=distance)
        x,lane = self.x,self.plane
        age = self.frame-self.entered
        # Walk around furniture on the appropriate plane, then approach the door.
        if self.room==(4,81) and self.objective==20:
            return self.walk(4) if lane else self.walk(6,True)
        if not door and direction in (2,6) and age>420 and (x>right+18 or x<left-18):
            return self.walk(0 if lane==0 else 4) if lane!=1 else self.walk(direction,True)
        if self.room==(3,34) and x>75:
            return self.walk(0) if lane==0 else self.walk(6,True)
        if self.room==(4,81) and self.objective==19 and x<115:
            return self.walk(4) if lane==1 else self.walk(2,True)
        if self.room==(4,25) and x>42 and plane==2 and direction==0:
            return self.walk(4) if lane==2 else self.walk(6,True)
        if self.room==(6,34) and lane==0 and x>=19:
            return self.walk(6,True)
        if self.room==(2,0) and x<146:
            return self.walk(4) if lane==2 else self.walk(2,True)
        if self.room==(5,7) and self.objective>4 and x<105 and lane==0:
            return self.walk(2,True)
        if self.room==(1,31) and self.objective>7 and x>75:
            return self.walk(4) if lane==2 else self.walk(6,True)
        if door and plane in (0,2) and age>360 and (x<left-6 or x>right+6):
            if lane==plane:
                return self.walk(4 if plane==2 else 0)
            return self.walk(2 if x<(left+right)//2 else 6,True)
        if self.room==(5,22) and x<120:
            return self.walk(4) if lane==2 else self.walk(2,True)
        if door:
            return self.interact(direction,plane,left,right)
        if plane!=lane:
            return self.walk(0 if plane>lane else 4)
        if direction in (0,4) and (x<left or x>right):
            return self.walk(2 if x<left else 6,True)
        return self.walk(direction,True)

    def crocodile(self):
        ram=self.state
        if self.room==(6,24):
            self.visited_crocodile=True
            return self.walk(6,True)
        if self.room!=(6,25):
            return None
        if ram[0xc2]&64:
            if self.aiming:
                self.aiming=False
                return (R,)
            if ram[0x268]!=13:
                if self.action<8:
                    self.queue((SELECT,RIGHT,UP,A,A,A,A,SELECT),10)
                return ()
            self.crocodile_done=True
            print(f"Crocodile defeated at {self.frame}, HP {ram[0x238]}",flush=True)
            return ()
        if ram[0x268]!=1:
            self.queue((SELECT,A,A,A,SELECT),8)
            return ()
        if not ram[0xd6]&16:
            if self.x<30 or self.x>=36:
                return self.walk(2 if self.x<30 else 6)
            return (A,) if self.frame%16<6 else ()
        if not self.visited_crocodile:
            return self.walk(2,True)
        if not ram[0xc4]&8:
            if self.plane:
                return self.walk(4)
            if self.x<68 or self.x>78:
                return self.walk(2 if self.x<68 else 6)
            return ()
        if self.plane!=1 or ram[0x235]!=123:
            return self.walk(0 if self.plane<=1 else 4)
        if self.facing!=2:
            return self.turn(2)
        if not self.aiming:
            self.aiming=True
            return (R,)
        return (A,) if self.frame%12<6 else ()
    def buttons(self,frame,memory):
        if frame<2280:
            if 2092<=frame<2100 or 2220<=frame<2228:
                return (A,)
            return (DOWN,) if 2170<=frame<2178 else ()
        ram=self.progress.read(memory)
        if not ram:
            raise RuntimeError(f"Load Game state unavailable at frame {frame}")
        self.state=ram
        self.frame=frame
        self.room=tuple(ram[0x208:0x20a])
        self.facing,self.plane=divmod(ram[0x233],16)
        self.x,self.action=ram[0x234],ram[0x236]
        if self.room!=self.previous_room:
            self.previous_room=self.room
            self.entered=frame
        if frame-self.entered>(4000 if self.room==(6,25) else 1600) or not ram[0x238]:
            raise RuntimeError(f"Stopped at {self.room}, frame {frame}, x={self.x}, plane={self.plane}, y={ram[0x235]}, facing={self.facing}, pose={self.action}, equipped={ram[0x268]}, HP={ram[0x238]}")
        if self.menu:
            return self.menu.popleft()
        if ram[0x20b]&2:
            return (A,) if frame%24<5 else ()
        if 0x6e<=self.action<=0x75:
            return (R,) if frame%10==1 else ()
        if ram[0x270]&128:
            if self.objective==1 and self.room==(1,15):
                self.queue((RIGHT,A,A,DOWN,A,DOWN,DOWN,DOWN,A,SELECT,SELECT,RIGHT,A,A,A,A,SELECT),8)
                self.advance()
            if self.objective==14 and self.room==(3,27):
                self.queue((DOWN,A,DOWN,DOWN,DOWN,DOWN,A,A,DOWN,A,SELECT),9)
                self.advance()
            return ()
        if ram[0x270]:
            return (A,) if frame%24<6 else ()
        if self.objective==16 and not self.crocodile_done:
            keys=self.crocodile()
            if keys is not None:
                return keys
        bag=ram[0x262:0x268]
        if ((self.objective==4 and 0x18 in bag) or (self.objective==18 and ram[0x20b]&4)
                or (self.objective==6 and 0x1c in bag)):
            self.advance()
        if self.room in self.ammunition and self.room[0]==4 and ram[0x296]<220:
            address,mask,x,width=self.ammunition[self.room]
            if ram[address]&mask:
                return self.approach_item(2,x,x+width)
        if self.room==(3,38) and self.objective==15 and not ram[0x20f]&1:
            prop=ram[0x20e]
            return self.push(2,prop-10,prop-3,15)
        if self.room==OBJECTIVES[self.objective]:
            keys=self.operate()
            if keys is not None:
                return keys
        return self.navigate()


def generate(core,rom,output):
    player_save,previous=first_checkpoint(core,rom)
    pilot=Pilot(rom,previous)
    boss=[]
    def control(frame,memory):
        if pilot.progress.address and pilot.objective==20:
            ram=ctypes.string_at(pilot.progress.address,0x29c)
            if ram[0x208:0x20a]==bytes([4,107]):
                engine=ctypes.CDLL(str(core))
                boss.append((save_state(engine),pilot.progress.address-memory[0x02000000]))
                print(f"Reached final encounter at {frame}, HP {ram[0x238]}",flush=True)
                return None
        keys=pilot.buttons(frame,memory)
        pilot.inputs.append(keys)
        return keys
    render(core,rom,save=bytearray(player_save),controller=control,final_frame=150000)
    if not boss:
        raise RuntimeError("The pilot did not reach the final encounter")
    pilot.inputs.extend(find_winning_inputs(core,rom,*boss[0]))
    parent=ROOT/"build/resident-evil-2"
    parent.mkdir(parents=True,exist_ok=True)
    with staging_directory(parent) as stage:
        tape=stage/"campaign-gba.txt"
        with tape.open("w",encoding="ascii") as stream:
            print("# mGBA display periods followed by held GBA controls. Begins at cold boot with the police typewriter SRAM.",file=stream)
            for keys,group in groupby(pilot.inputs):
                print(sum(1 for _ in group),*(BUTTONS[key] for key in keys),file=stream)
        campaign_replay(core,rom,player_save,previous,tape)
        output.parent.mkdir(parents=True,exist_ok=True)
        os.replace(tape,output)
    print(output)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core",type=Path,default=os.environ.get("MGBA_LIBRETRO"))
    parser.add_argument("--rom",type=Path,default=ROOT/"dist/resident-evil-2.gba")
    parser.add_argument("--output",type=Path,default=ROOT/"build/resident-evil-2/campaign-gba.txt")
    args=parser.parse_args()
    core=args.core or next((Path(os.environ["LOCALAPPDATA"])/"Programs/mGBA").glob("libretro-*/mgba_libretro.dll"))
    generate(core,args.rom.read_bytes(),args.output)


if __name__=="__main__":
    main()