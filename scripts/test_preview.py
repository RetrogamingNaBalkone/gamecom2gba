"""Check the standalone converter and the actual mGBA video output."""
import argparse
from collections import deque
import ctypes as c
import hashlib
import itertools
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]

BONUS_ROUTES={
    5:"L2 F9 B F15 B F L4 F14 L B F R5 F8 B F39 B F7 B F7 B F R4 F6 R B F20 B F L10 F14 B2 F R4 F5 B",
    10:"F7 B F L2 F6 B F19 B F10 B F L11 F8 B F5 B F L3 F4 L B",
    0:"F16 B F R7 F9 B F22 B F10 L3 B",
}


def verify_bonus_passwords(original,tape,snapshot,work):
    inputs=tape[:3].copy()
    for ports,delay in ([("fd ff ff",80),("7f ff ff",80),("7f ff ff",80)]
                        +[("7f ff ff",35)]*4+[("ff fd ff",750)]):
        inputs.extend([f"8 {ports} -1 0",f"{delay} ff ff ff -1 0"])
    for name,stage,next_id,route_id,actors in [
        ("launching-facility",0x15,0x21,5,7),("lunatic-fringe",0x25,0x31,10,1),
        ("freeway",0x35,0x11,0,1)]:
        prepared=work/(name+".nv")
        subprocess.run([sys.executable,ROOT/"scripts/bonus_checkpoint.py",snapshot,name,prepared],check=True)
        route=BONUS_ROUTES[route_id]
        if stage==0x15:
            # Detour for the first room's medkit, then armor in room 3.
            route=("L2 F8 R3 L3 F1 W60 B F15 B F F8 R4 F2 L8 F4 L W60 B F "
                   "R5 F8 B F39 B F7 B F7 B F R4 F6 R B F20 B F L10 F14 B2 F R4 F5 B")
        result=subprocess.run(original,input="\n".join(inputs).encode(),check=True,capture_output=True,
                              env=dict(os.environ,GAMECOM_LOAD_NVRAM=str(prepared),
                                       GAMECOM_LOAD_NVRAM_FRAME="3608",GAMECOM_ROUTE="W1 "+route+" W640"))
        rows=[row for row in result.stderr.decode().splitlines() if row.startswith("route ")]
        assert f"stage={stage:02x} room=0" in rows[0] and f"actors={actors} " in rows[0],rows[0]
        assert f"stage={next_id:02x} room=0" in rows[-1], rows[-3:]
        frags=int(rows[-2].split("frags=")[1])
        assert frags=={0x15:48,0x25:17,0x35:11}[stage],rows[-2]
        print(f"game.com {name}: password accepted, {frags} frags, exit to {next_id:02x}.")


class GameInfo(c.Structure):
    _fields_ = [("path", c.c_char_p), ("data", c.c_void_p),
                ("size", c.c_size_t), ("meta", c.c_char_p)]


class MemoryDescriptor(c.Structure):
    _fields_=[("flags",c.c_uint64),("ptr",c.c_void_p)]+[
        (name,c.c_size_t) for name in ("offset","start","select","disconnect","length")
    ]+[("space",c.c_char_p)]


class MemoryMap(c.Structure):
    _fields_=[("descriptors",c.POINTER(MemoryDescriptor)),("count",c.c_uint)]


# Native state exposed through mGBA's standard read-only memory map.
POSITION_SIZE=16
ENEMY_SIZE=24
ENEMY_COUNT=POSITION_SIZE+18*ENEMY_SIZE
WEAPON=472
INVENTORY=496


def fight_campaign(ram,plan,rom):
    """Choose buttons from the live player's position and projected actors."""
    buttons={"F":4,"D":5,"L":6,"R":7,"B":0}
    # Locate the native room directory after the two screen assets.
    campaign=campaign_offset(rom)
    def word(offset):
        return struct.unpack_from("<I",rom,offset)[0]
    def position():
        return struct.unpack("<4I",c.string_at(ram,16))
    def target(lane,depth):
        left=0 if lane==1 else lane*50-72
        count=c.c_uint32.from_address(ram+ENEMY_COUNT).value
        assert count<=18,count
        nearest=5
        aim=None
        for i in range(count):
            x,z,_,hp=struct.unpack("<HBBB",c.string_at(ram+POSITION_SIZE+ENEMY_SIZE*i,5))
            x-=left
            if hp and 0<=x<200 and 0<z-depth<nearest:
                nearest=z-depth
                aim=10 if x<50 else 11 if x>150 else 8
        return aim,nearest
    vectors={"F":(0,1),"D":(0,-1),"L":(-1,0),"R":(1,0)}
    def drive(key,header):
        for _ in range(300):
            old=position()
            _,_,lane,depth=old
            ammo,_,delay,_,_,kind=struct.unpack("<6I",c.string_at(ram+WEAPON,24))
            _,_,health,shells,rockets,weapons=struct.unpack("<6I",c.string_at(ram+INVENTORY,24))
            assert health,("died",old,key)
            best=3 if rockets and weapons&4 else 2 if shells and weapons&2 else 1 if ammo else 0
            if kind!=best and depth+1<rom[header+1]:
                yield [],max(3,delay//274+3),None
                yield [0],4,None
                yield [],4,None
                continue
            aim,nearest=target(lane,depth)
            if aim is not None and key!="B" and (kind or nearest==1):
                yield [aim],10,None
                continue
            yield [buttons[key]],4,None
            yield [],16,None
            return
        raise AssertionError(("combat stalled",position(),key))

    def detour(header,goal,failed):
        width,length=rom[header:header+2]
        actors=header+36+4*rom[header+32]
        views=actors+4+4*word(actors)
        def passable(x,z):
            return (1<=x<=width and 0<=z<length and
                    not word(views+4*(z*width+x-1))&0x80000000)
        if not passable(*goal): return
        for _ in range(50):
            current=position()[2:]
            if current==goal: return
            queue=deque([(current,None)])
            seen={current}
            while queue:
                (x,z),first=queue.popleft()
                if (x,z)==goal: break
                for button,(dx,dz) in vectors.items():
                    dest=x+dx,z+dz
                    if dest in seen or ((x,z),button) in failed or not passable(*dest): continue
                    seen.add(dest)
                    queue.append((dest,first or button))
            else: raise AssertionError(("no path",position(),goal))
            yield from drive(first,header)
            if current==position()[2:]: failed.add((current,first))
        raise AssertionError(("detour stalled",position(),goal))

    for next_stage,commands in plan:
        for command in commands.split():
            key=command[0]
            for _ in range(int(command[1:] or 1)):
                old=position()
                stage,room,lane,depth=old
                directory=campaign+word(campaign+4*(stage+1))
                header=directory+word(directory+4*(room+1))
                yield from drive(key,header)
                if old==position() and key!="B":
                    dx,dz=vectors[key]
                    yield from detour(header,(lane+dx,depth+dz),{((lane,depth),key)})
        if next_stage is None: continue
        yield [],445,None
        yield [],1,next_stage
        yield [],132,None
        assert position()[:2]==(next_stage,0),(next_stage,position())


def render(core_path, rom, start=False, firing=False, walk=(), final_frame=0, stage_cards=None,
           campaign=None,tail=(),observed=None):
    core = c.CDLL(str(core_path))
    frames = []
    capture=False
    pixel_format = 0
    keys = set()
    iwram=None

    def environment(command, data):
        nonlocal pixel_format,iwram
        if command==0x10024:
            mapping=c.cast(data,c.POINTER(MemoryMap))[0]
            iwram=next((d.ptr+d.offset for d in mapping.descriptors[:mapping.count]
                        if d.start==0x03000000),None)
            return True
        if command == 10:
            pixel_format = c.cast(data, c.POINTER(c.c_uint))[0]
            return pixel_format in (0, 1, 2)
        if command == 3:
            c.cast(data, c.POINTER(c.c_bool))[0] = True
            return True
        return False

    def video(ptr, width, height, pitch):
        if ptr and capture:
            step = 4 if pixel_format == 1 else 2
            frames[:] = [b"".join(c.string_at(ptr + y*pitch, width*step)
                                  for y in range(height))]

    callbacks = []
    for name, signature, function in [
        ("environment", (c.c_bool, c.c_uint, c.c_void_p), environment),
        ("video_refresh", (None, c.c_void_p, c.c_uint, c.c_uint, c.c_size_t), video),
        ("audio_sample", (None, c.c_int16, c.c_int16), lambda *_: None),
        ("audio_sample_batch", (c.c_size_t, c.c_void_p, c.c_size_t), lambda _, n: n),
        ("input_poll", (None,), lambda: None),
        ("input_state", (c.c_int16, c.c_uint, c.c_uint, c.c_uint, c.c_uint),
         lambda port, device, index, button: int(port == 0 and device == 1 and button in keys)),
    ]:
        cb = c.CFUNCTYPE(*signature)(function)
        callbacks.append(cb)
        getattr(core, "retro_set_" + name)(cb)
    core.retro_init()
    buffer = c.create_string_buffer(rom)
    info = GameInfo(b"preview.gba", c.cast(buffer, c.c_void_p), len(rom), None)
    try:
        core.retro_load_game.restype = c.c_bool
        assert core.retro_load_game(c.byref(info))
        try:
            shot_frames = []
            held_walk = walk == [(85,220,7)]
            walk_frames = []
            previous_lane = None
            last_ammo=48
            previous_health=100
            last_input=max((end for _,end,_ in walk), default=0)
            events={}
            for pressed,released,button in walk:
                events.setdefault(pressed,[]).append((button,True))
                events.setdefault(released,[]).append((button,False))
            end_frame=final_frame+1 if final_frame else max(360 if firing else 120,last_input+15)
            replay=None
            next_input=max(85,last_input+10)
            for frame in range(250000 if campaign else end_frame):
                check_card=stage_cards is not None and frame in stage_cards
                card=stage_cards[frame] if check_card else None
                done=False
                if campaign and frame==next_input:
                    if replay is None:
                        assert iwram,"mGBA did not expose IWRAM"
                        replay=itertools.chain(fight_campaign(iwram,campaign,rom),
                                               ((buttons,n,None) for buttons,n in tail))
                    action=next(replay,None)
                    if action is None: done=True
                    else:
                        pressed,delay,card=action
                        keys=set(pressed)
                        next_input+=delay
                        check_card=card is not None
                capture=frame==end_frame-1 or check_card or done
                if start and frame == 60:
                    keys.add(3)
                if frame == 62:
                    keys.clear()
                for button,down in events.get(frame,()):
                    if down:
                        keys.add(button)
                    else:
                        keys.discard(button)
                if firing and frame == 120:
                    keys.add(8)
                core.retro_run()
                if observed is not None and iwram and frame>=70:
                    health=c.c_uint32.from_address(iwram+INVENTORY+8).value
                    if health!=previous_health:
                        observed.setdefault("hits",[]).append((frame,health))
                        previous_health=health
                if check_card:
                    check_stage_card(unpack_colors(frames[0],pixel_format),rom,card)
                if done: break
                if held_walk and frame>=84:
                    lane=c.c_uint32.from_address(iwram+8).value
                    if previous_lane is not None and lane!=previous_lane: walk_frames.append(frame)
                    previous_lane=lane
                if firing and frame>=119:
                    remaining=c.c_uint32.from_address(iwram+WEAPON).value
                    if remaining<last_ammo: shot_frames.append(frame)
                    last_ammo=remaining
            if observed is not None:
                observed["position"]=struct.unpack("<4I",c.string_at(iwram,16))
                observed["inventory"]=struct.unpack("<6I",c.string_at(iwram+INVENTORY,24))
                observed["weapon"]=struct.unpack("<6I",c.string_at(iwram+WEAPON,24))
            if campaign: assert done,"campaign replay timed out"
            if firing:
                intervals = [b-a for a,b in zip(shot_frames, shot_frames[1:])]
                assert len(intervals) >= 9, shot_frames
                assert all(16 <= n <= 17 for n in intervals[:5]), intervals
                assert 77 <= intervals[5] <= 79, intervals
                print("mGBA shot intervals (display frames):", intervals)
            if held_walk:
                assert len(walk_frames) == 9, walk_frames
                intervals = [b-a for a,b in zip(walk_frames,walk_frames[1:])]
                assert all(14 <= step <= 15 for step in intervals), intervals
                print("mGBA lateral step intervals (display frames):", intervals)
        finally:
            core.retro_unload_game()
    finally:
        core.retro_deinit()
    return unpack_colors(frames[0],pixel_format)


def unpack_colors(frame,pixel_format):
    pixels = []
    for value, in struct.iter_unpack("<I" if pixel_format == 1 else "<H", frame):
        if pixel_format == 1:
            rgb = (value >> 19, value >> 11, value >> 3)
        else:
            rgb = (value >> (11 if pixel_format == 2 else 10),
                   value >> (6 if pixel_format == 2 else 5), value)
        pixels.append(tuple(channel & 31 for channel in rgb))
    return pixels


def press_steps(walk, frame, commands):
    # Long neutral gaps allow the same route to cross rooms with different
    # movement periods, and each press remains a single game action.
    buttons={"F":4,"L":6,"R":7,"B":0,"D":5}
    for command in commands.split():
        for _ in range(int(command[1:] or 1)):
            walk.append((frame,frame+4,buttons[command[0]]))
            frame+=20
    return frame


def campaign_offset(rom):
    palette=[(0,0,0),(27,31,17),(13,17,9),(1,9,5),(0,0,0),(22,22,21),(5,5,5)]
    raw=b"".join(struct.pack("<H",r|(g<<5)|(b<<10)) for r,g,b in palette)+bytes(18)
    return rom.index(raw)+32+2*19200


def check_stage_card(pixels,rom,entering_stage):
    palette=[(0,0,0),(27,31,17),(13,17,9),(1,9,5),(0,0,0),(22,22,21),(5,5,5)]
    campaign=campaign_offset(rom)
    levels,=struct.unpack_from("<I",rom,campaign)
    cards,=struct.unpack_from("<I",rom,campaign+4*(levels+1))
    title=campaign+cards+(entering_stage*3+2)*19200
    for y in range(160):
        for x in range(240):
            pos=title+32*(30*(y//8)+x//8)+4*(y%8)+(x%8)//2
            color=(rom[pos]>>(4*(x%2)))&15
            assert pixels[240*y+x]==palette[color], (entering_stage,x,y)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cartridge", type=Path, help="Duke USA ZIP")
    parser.add_argument("--core", default=os.environ.get("MGBA_LIBRETRO"))
    parser.add_argument("--reference", type=Path, help="reference.c executable; use adjacent BIOS ZIPs")
    args = parser.parse_args()
    core = args.core or next((Path(os.environ["LOCALAPPDATA"]) / "Programs/mGBA")
                             .glob("libretro-*/mgba_libretro.dll"))
    with zipfile.ZipFile(args.cartridge) as archive:
        data = archive.read(next(i for i in archive.infolist() if i.file_size == 2097152))
    exe = ROOT / "dist/gamecom-gba.exe"
    reference_frames = {}
    # Number of weapon switches after granting the arsenal, SM8500 art
    # descriptor, duration of held fire in the reference, GBA shot age.
    weapon_samples=[(1,0x86e5,18,17),(1,0x86db,44,30),
                    (2,0x8712,23,17),(3,0x868b,22,16)]
    reference_weapons={}
    with tempfile.TemporaryDirectory(dir=ROOT / "build") as tmp:
        work = Path(tmp)
        output = work / "output.gba"
        raw = work / "renamed.bin"
        raw.write_bytes(data)
        for source in (args.cartridge.resolve(), raw):
            subprocess.run([exe, source, output], cwd=work, check=True)
            if source != raw:
                expected = output.read_bytes()
            else:
                assert expected == output.read_bytes()
        raw.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
        assert subprocess.run([exe, raw, output], cwd=work).returncode == 1
        assert expected == output.read_bytes()
        unrelated = work / "other.zip"
        with zipfile.ZipFile(unrelated, "w") as archive:
            archive.writestr("Duke Nukem 3D (USA).tgc", b"unrecognized cartridge")
        assert subprocess.run([exe, unrelated, output], cwd=work).returncode == 1
        assert expected == output.read_bytes()
        unrelated.unlink()
        assert sorted(p.name for p in work.iterdir()) == ["output.gba", "renamed.bin"]
        # First 24 bytes come from the cartridge's system record initializer.
        record=bytearray(data[0x21*8192+0xf18:0x21*8192+0xf30])+bytearray([255])*19
        bonus_file=work/"duke-record.bin"
        record[0x15:0x18]=bytes((250,7,1))
        bonus_file.write_bytes(record)
        convert_bonus=[exe,args.cartridge.resolve(),output,"--bonus-record",bonus_file]
        subprocess.run(convert_bonus,cwd=work,check=True)
        extended=output.read_bytes()
        nvram=bytearray(8192)
        nvram[0x1c63:0x1c63+43]=record
        bonus_file.write_bytes(nvram)
        subprocess.run(convert_bonus,cwd=work,check=True)
        assert extended==output.read_bytes() and extended!=expected
        record[0x17]^=1
        bonus_file.write_bytes(record)
        assert subprocess.run(convert_bonus,cwd=work).returncode==1 and output.read_bytes()==extended
        if args.reference:
            raw.write_bytes(data)
            bios = []
            for role, sha in [("Internal", "03368237e8fed4a8724f3b4a1596cf4b17c96d33"),
                              ("External", "97f782e72d738f4d7b861363266bf46b438d9b50")]:
                name = "[BIOS] Tiger Game.com (USA) (" + role + ").zip"
                with zipfile.ZipFile(args.cartridge.parent / name) as archive:
                    blob = next(b for i in archive.infolist() for b in [archive.read(i)]
                                if hashlib.sha1(b).hexdigest() == sha)
                path = work / (role + ".bin")
                path.write_bytes(blob)
                bios.append(path)
            # Boot, select the cartridge, then A for New Game, episode and skill.
            waits = (1200, 2400, 120, 90, 424)
            actions = ("ff ff ff 2 2", "7f ff ff -1 0", "7f ff ff -1 0", "7f ff ff -1 0")
            tape = []
            for i, wait in enumerate(waits):
                tape.append(f"{wait} ff ff ff -1 0")
                if i < len(actions):
                    tape.append("8 " + actions[i])
            snapshot=work/"title.nv"
            original=[args.reference,*bios,raw]
            subprocess.run(original,input="\n".join(tape[:3]).encode(),capture_output=True,check=True,
                           env=dict(os.environ,GAMECOM_SAVE_NVRAM=str(snapshot)))
            system=bytearray(snapshot.read_bytes())
            start=system.index(record[:12])
            assert system[start:start+21]==record[:21] and system[start+21:start+24]==bytes(3)
            verify_bonus_passwords(original,tape,snapshot,work)
            for flags,register in [(4,0x81),(2,0x41),(1,0x09),(7,0xc9)]:
                system[start+21:start+24]=bytes((250,flags,(250+flags)&255))
                snapshot.write_bytes(system)
                result=subprocess.run(original,input="\n".join(tape).encode(),capture_output=True,check=True,
                                      env=dict(os.environ,GAMECOM_LOAD_NVRAM=str(snapshot),
                                               GAMECOM_LOAD_NVRAM_FRAME="3608",GAMECOM_TRACE_STATE="1"))
                state=next(row for row in result.stderr.splitlines() if row.startswith(b"d0:"))
                assert int(state.split()[12],16)==register, state
            subprocess.run([exe,raw,output,"--bonus-record",snapshot],check=True,cwd=work)
            assert output.read_bytes()==extended
            system[start+21:start+24]=bytes((0,32,32))
            snapshot.write_bytes(system)
            for switches,descriptor,duration,_ in weapon_samples:
                inputs=["25 ff ff ff -1 0"]+["12 ff ff fd -1 0","22 ff ff ff -1 0"]*switches
                inputs.append(f"{duration} 7f ff ff -1 0")
                result=subprocess.run(original,input="\n".join(tape+inputs).encode(),capture_output=True,
                                      check=True,env=dict(os.environ,GAMECOM_LOAD_NVRAM=str(snapshot),
                                                          GAMECOM_LOAD_NVRAM_FRAME="3608"))
                reference_weapons[descriptor]=result.stdout.split(b"255\n",1)[1]
            for key, port in [(None,None),(4,"fe"),(6,"fb"),(7,"f7"),("held","f7"),
                              ("door",None),("open",None),("entered",None)]:
                duration = 135 if key == "held" else 12
                moves = [] if port is None else ["7 ff ff ff -1 0",f"{duration} {port} ff ff -1 0","7 ff ff ff -1 0"]
                if key in ("door","open","entered"):
                    moves=["7 ff ff ff -1 0","87 f7 ff ff -1 0","12 ff ff ff -1 0",
                           "140 fe ff ff -1 0","12 ff ff ff -1 0"]
                    if key!="door":
                        moves+=["16 fe ff fd -1 0","80 ff ff ff -1 0"]
                    if key=="entered":
                        moves+=["18 fe ff ff -1 0","12 ff ff ff -1 0"]
                        lobby_tape=tape+moves
                result = subprocess.run([args.reference, *bios, raw], cwd=work, check=True,
                                        input="\n".join(tape+moves).encode(), stdout=subprocess.PIPE)
                reference_frames[key] = result.stdout
            shot = ["7 ff", "12 fe", "7 ff", "18 7f", "2 ff"]
            for steps, state in [(shot[:3], "28 00 07"), (shot, "21 14 03")]:
                rows = [step + " ff ff -1 0" for step in steps]
                result = subprocess.run([args.reference, *bios, raw], cwd=work, check=True,
                                        input="\n".join(tape + rows).encode(), capture_output=True,
                                        env=dict(os.environ, GAMECOM_TRACE_ACTORS="1"))
                assert ("actor 0 00 02 05 " + state).encode() in result.stderr, result.stderr[-2000:]
            def verify_items(start, steps, state):
                rows=[" ".join((step.split()+["ff","ff"])[:4])+ " -1 0" for step in steps]
                run=subprocess.run([args.reference,*bios,raw],cwd=work,check=True,
                                   input="\n".join(start+rows).encode(),capture_output=True,
                                   env=dict(os.environ,GAMECOM_TRACE_ITEMS="1"))
                assert state.encode() in run.stderr, run.stderr.decode()
                return run.stdout
            verify_items(tape,["38 f7","12 ff","50 fe","12 ff"],
                         "room=0 lane=4 depth=3 key=0 armor=1 items=100000")
            for scenario, steps, expected_hits in [
                ("stand still", ["1400 ff"], [
                    (4667,1,91,0),(4768,1,82,0),(4868,1,73,0),(4969,1,64,0),
                    (5070,1,55,0),(5171,1,46,0),(5271,1,37,0),(5372,0,27,0),
                    (5372,1,18,0),(5473,0,8,0),(5473,1,0,0)]),
                ("one step forward", ["7 ff","12 fe","1200 ff"], [
                    (4731,1,91,0),(4781,0,81,0),(4861,1,72,0),(4911,0,62,0),
                    (4991,1,53,0),(5041,0,43,0),(5121,1,34,0),(5171,0,24,0),
                    (5251,1,15,0),(5301,0,5,0),(5380,1,0,0)]),
                ("collect armor", ["38 f7","12 ff","50 fe","190 ff"], [
                    (4455,1,95,96),(4555,1,90,92)])]:
                rows=[step+" ff ff -1 0" for step in steps]
                trace=dict(os.environ,GAMECOM_TRACE_COMBAT="4266")
                if scenario=="one step forward":
                    trace["GAMECOM_TRACE_ACTOR_MOTION"]="4266"
                run=subprocess.run([args.reference,*bios,raw],cwd=work,check=True,
                                   input="\n".join(tape+rows).encode(),capture_output=True,
                                   env=trace)
                hits=[]
                for line in run.stderr.decode().splitlines():
                    if line.startswith("attack "):
                        fields=dict(pair.split("=") for pair in line.split()[1:])
                        hits.append(tuple(int(fields[key]) for key in ("frame","type","health","armor")))
                assert hits==expected_hits, (scenario,hits)
                if "GAMECOM_TRACE_ACTOR_MOTION" in trace:
                    for snapshot in ["motion 4345 2 3 2 138 4 0", "motion 4448 2 7 34 98 4 0",
                                     "motion 4579 2 10 32 178 4 0", "motion 4731 2 2 22 178 2 1"]:
                        assert snapshot.encode() in run.stderr, snapshot
            take_key=["80 f7","12 ff","145 fe","12 ff","130 fb","12 ff"]
            unlock=take_key+["128 f7","12 ff","60 fe","12 ff","16 fe ff fd","55 ff"]
            verify_items(lobby_tape,take_key,"room=1 lane=4 depth=9 key=1 armor=0 items=010000")
            verify_items(lobby_tape,unlock,"room=1 lane=12 depth=11 key=0 armor=0 items=010000")
            west=unlock+["235 fb","12 ff","19 fe ff fd","85 ff","18 fe","180 ff"]
            rows=[" ".join((step.split()+["ff","ff"])[:4])+" -1 0" for step in west]
            run=subprocess.run([args.reference,*bios,raw],cwd=work,check=True,
                               input="\n".join(lobby_tape+rows).encode(),capture_output=True,
                               env=dict(os.environ,GAMECOM_TRACE_COMBAT="5675",
                                        GAMECOM_TRACE_ACTOR_MOTION="5675"))
            for event in ["motion 5675 0 7 0 108 3 0","motion 5728 0 3 0 108 2 0",
                          "attack frame=5749 actor=0224 type=0 x=108 depth=2 health=90",
                          "attack frame=5850 actor=0224 type=0 x=108 depth=2 health=80"]:
                assert event.encode() in run.stderr, event
            armed_west=west[:-1]+["40 ff","14 ff ff fd","40 ff"]
            verify_items(lobby_tape,armed_west,"weapon=2 bullets=48 shells=6 rockets=0")
            verify_items(lobby_tape,armed_west+["22 7f","10 ff"],"weapon=2 bullets=48 shells=5 rockets=0")
            reference_frames["corridor"]=verify_items(
                lobby_tape,unlock+["29 fe ff fd","130 ff","25 fe","132 ff"],
                "room=5 lane=1 depth=1 key=0")
            through_corridor=unlock+["29 fe ff fd","130 ff","25 fe","12 ff","120 fe",
                                      "12 ff","30 fe ff fd","120 ff","24 fe","12 ff"]
            finish=through_corridor+["130 fe","12 ff","130 fb","12 ff","30 fe ff fd"]
            reference_frames["next_level"]=verify_items(
                lobby_tape,finish+["540 ff"],"room=0 lane=4 depth=0 key=0")
    pixels = render(core, expected)
    assert len(pixels) == 240*160
    colors = [(27,31,17), (13,17,9), (1,9,5), (0,0,0)]
    for y in range(160):
        for x in range(200):
            packed = data[0x94000 + 64*x + y//4]
            assert pixels[y*240+x+20] == colors[(packed >> (6-2*(y%4))) & 3], (x,y)
    for x0 in (0, 220):
        side = {pixels[y*240+x] for y in range(160) for x in range(x0, x0+20)}
        assert side == {(22,22,21), (5,5,5)}
    level = render(core, expected, start=True)
    # From lane 2 to lane 8, then to the front wall. A closed door blocks Up.
    def visit(commands,tail=(),walk=()):
        state={}
        screen=render(core,expected,start=True,walk=walk,campaign=[(None,commands)],tail=tail,observed=state)
        return screen,state["position"][1:],state
    approach="R6 F8"
    closed,at,_=visit(approach)
    blocked,still,_=visit(approach,[([4],4),([],16)])
    opened,_,_=visit(approach+" B")
    lobby=approach+" B F"
    entered,inside,_=visit(lobby)
    assert at==still==(0,8,8) and inside==(1,7,0) and closed!=opened
    direct=lobby+" R5 F11"
    detour=lobby+" R5 F9 L8 R8 F2"
    assert visit(direct+" B2 F")[1]==(1,12,11), "door opened without a key"
    assert visit(detour+" B F")[1]==(1,12,11), "first use must consume the key"
    enter_corridor=detour+" B2 F2"
    corridor,inside,_=visit(enter_corridor)
    assert inside==(5,1,1)
    finish=detour+" B2 F6 B F9 L8 B"
    next_level,spawn,_=visit(finish,[([],570)])
    assert spawn==(0,4,0)
    score,_,_=visit(finish,[([],320)])
    # Total and level frags match on the first score card and exceed zero.
    ten_glyphs=["75557","26227","71747","71717","55711","74717","74757","71111","75757","75717"]
    def tally(y):
        value=0
        for x in range(84,125,8):
            bits="".join(str(sum((score[240*(y+dy)+x+dx]==colors[0])<<(2-dx)
                                 for dx in range(3))) for dy in range(5))
            if bits!="00000": value=10*value+ten_glyphs.index(bits)
        return value
    assert tally(96)==tally(128)>0
    # Ordinary campaign route. Each row ends at an exit; the value is the
    # index of the title expected next, including the original wraparound.
    routes=[
        (2,"L2 F6 B F57 B F L F18 R5 F14 B F75 B F10 L3 B F L F6 B"),
        (3,"F42 B F R6 F12 R F2 B F30 B F R F21 R F B F34 B F17 B"),
        (5,"F61 B F R6 F24 B F27 B F63 B F28 B"),
        (6,"F19 B F16 B F10 B F R5 F6 B F7 B F L6 F7 B F15 B F21 B F27 B F R F6 B F L6 F5 B F8 B F R6 F5 B2 F7 B"),
        (7,"F6 B F6 B F R F18 B F L7 F5 L F L F B F L2 F8 L B F20 B F7 R F2 R3 B2 F19 B F8 B"),
        (8,"F6 B F R F5 B F10 B F22 B F R2 F6 B F R F8 B2 F12 B F14 B F10 B F14 B F18 B F25 B F21 B F11 B"),
        (10,"F5 B F17 B F16 B F L4 F7 B F19 B F13 B F14 B F R8 F4 B F10 B2 F L6 F7 B F22 B"),
        (11,"F6 B F L4 F10 L3 F B F R3 F7 B"),
        (12,"L F5 B F13 B F R4 F6 B F R5 F7 B F29 B F L5 F6 B2 F L F19 B F15 B"),
        (13,"F10 B F7 B F R6 F9 B2 F22 B"),
        (0,"R4 F6 B F7 B F R3 F7 B F6 B F L9 F6 B F14 B F R F13 L8 F6 B F12 B F L F6 B"),
    ]
    first=(1,"R6 F8 B F R5 F9 L8 R8 F2 B2 F6 B F9 L8 B")
    normal=[first,*routes]
    extended_route=[first]
    for next_stage,commands in routes:
        if next_stage in BONUS_ROUTES:
            extended_route.append(((next_stage-1)%15,commands))
            commands=BONUS_ROUTES[next_stage]
        extended_route.append((next_stage,commands))
    for image,plan in [(expected,normal),(extended,extended_route)]:
        returned=render(core,image,start=True,campaign=plan)
        assert returned[:20*240]==level[:20*240], "campaign wraparound spawn"
        print(f"mGBA crossed all {len(plan)} stage exits while fighting and returned to Hollywood Holocaust.")
    wall=[]
    frame=press_steps(wall,85,"R9 F4")
    at_wall=render(core,expected,start=True,walk=wall)
    frame=press_steps(wall,frame,"F")
    assert at_wall==render(core,expected,start=True,walk=wall), "street obstacle"
    press_steps(wall,frame,"L F")
    assert at_wall!=render(core,expected,start=True,walk=wall), "route around street obstacle"
    beside_armor=[(85,104,7)]
    before=render(core,expected,start=True,walk=beside_armor)
    round_trip=[(130,166,4),(190,226,5)]
    after=render(core,expected,start=True,walk=beside_armor+round_trip)
    second_trip=[(start+120,end+120,button) for start,end,button in round_trip]
    assert before[:136*240]!=after[:136*240], "armor still visible after collection"
    def hud_glyph(frame,x):
        return [sum((frame[240*y+x+i]==colors[0])<<(2-i) for i in range(3))
                for y in range(151,156)]
    assert hud_glyph(before,61)==[0]*5 and hud_glyph(after,61)==[2,6,2,2,7]
    assert hud_glyph(after,69)==hud_glyph(after,77)==[7,5,5,5,7]
    assert all(hud_glyph(before,33+dx)==hud_glyph(after,61+dx) for dx in (0,8,16))
    def check_counter(image,x,number,width=3):
        glyphs=["75557","26227","71747","71717","55711","74717",
                "74757","71111","75757","75717"]
        text=" "*width if number is None else f"{number:{width}d}"
        for i,char in enumerate(text):
            wanted="00000" if char==" " else glyphs[int(char)]
            assert hud_glyph(image,x+8*i)==list(map(int,wanted)), (x,number,i)
    paused=[(85,87,3)]
    item_code=paused+[(95,97,8),(105,107,10),(115,117,0),(125,127,10),(135,155,2)]
    check_counter(render(core,expected,start=True,walk=item_code),61,0)
    granted=render(core,expected,start=True,walk=item_code+[(161,163,2)])
    check_counter(granted,61,100)
    check_counter(granted,200,1,1)
    key_code=item_code+[(161,163,2),(173,175,3)]
    assert visit(direct+" B2 F",walk=key_code)[1][0]==5, "pause code key did not open the exit"
    weapon_code=paused+[(95,97,11),(105,107,8),(115,117,0),(125,127,8),(135,137,0),(145,147,0)]
    check_counter(render(core,expected,start=True,walk=weapon_code),169,200)
    armed=weapon_code+[(159,161,3)]
    for count,descriptor,_,age in weapon_samples:
        switches=[(171+12*i,173+12*i,0) for i in range(count)]
        frame=render(core,expected,start=True,walk=armed+switches+[(220,221,8)],final_frame=220+age)
        check_counter(frame,169,None if count==3 else 49,2)
        p=0x26*8192+(descriptor&8191)
        width,height,sx,sy,page=data[p:p+5]
        for y in range(height):
            for x in range(width):
                pos=page*16384+64*((sx+x)&255)+((sy+y)&255)//4
                shift=6-2*((sy+y)%4)
                ink=(data[pos]>>shift)&3
                mask=(data[pos+16384]>>shift)&3
                if not (ink or mask): continue
                px,py=(200-width)//2+x,136-height+y
                shade=0 if mask else ink
                assert frame[py*240+px+20]==colors[shade], (hex(descriptor),px,py)
                if descriptor in reference_weapons:
                    assert reference_weapons[descriptor][py*200+px]==(252,126,63,0)[shade]
    # With only the starting pistol, exhausting all 48 rounds selects the foot.
    empty,_,empty_state=visit(enter_corridor,[([8],7000),([],100)])
    assert empty_state["weapon"][0]==0 and empty_state["weapon"][5]==0
    check_counter(empty,169,None)
    west=detour+" L11 B F"
    step=[([4],4),([],16)]
    _,at,arrival=visit(west,step)
    assert at==(2,1,1),at
    _,_,injured=visit(west,step+[([],180)])
    assert injured["inventory"][2]<arrival["inventory"][2], "corridor attacker missed"
    # At the west entrance there are six shells even if the player already
    # acquired another shotgun or shells from defeated enemies.
    assert arrival["inventory"][3]>=6 and arrival["inventory"][5]&2
    contact=[(85,97,4)]
    fight={}
    fatal=render(core,expected,start=True,walk=contact,final_frame=1210,observed=fight)
    hits=fight["hits"]
    assert [hp for _,hp in hits]==[91,81,72,62,53,43,34,24,15,5,0], hits
    assert abs(hits[0][0]-60-464)<=12, hits
    for (a,_),(b,_) in zip(hits,hits[2:]):
        assert abs(b-a-130)<=2, hits
    assert abs(hits[1][0]-hits[0][0]-50)<=5, hits
    check_counter(fatal,33,0)
    retry=render(core,expected,start=True,walk=contact,final_frame=1580)
    assert retry!=fatal and retry!=level
    restarted={}
    renewed=render(core,expected,start=True,walk=contact+[(1590,1592,8)],final_frame=1620,observed=restarted)
    assert restarted["position"]==(0,0,2,0) and restarted["weapon"][:2]==(48,6)
    check_counter(renewed,33,100)
    revisited=render(core,expected,start=True,walk=beside_armor+round_trip+second_trip)
    # The collected armor cannot refill on the second trip. Incoming hits
    # may consume some of it as the enemies move around the street.
    used={}
    render(core,expected,start=True,walk=beside_armor+round_trip+second_trip,observed=used)
    check_counter(revisited,61,used["inventory"][1])
    assert used["inventory"][1]<100 and used["hits"]
    armored={}
    render(core,expected,start=True,walk=beside_armor+round_trip[:1],final_frame=280,observed=armored)
    assert armored["inventory"][1:3]==(96,95),armored
    still={}
    render(core,expected,start=True,final_frame=1320,observed=still)
    hits=still["hits"]
    # Last two pairs land together in the cartridge and in mGBA.
    assert [health for _,health in hits]==[91,82,73,64,55,46,37,18,0],hits
    assert abs(hits[0][0]-60-401)<=12,hits
    assert all(99<=b[0]-a[0]<=102 for a,b in zip(hits,hits[1:])),hits
    print("mGBA street pursuit, first three hits:",hits[:3])
    render(core, expected, start=True, firing=True)
    assert level != pixels
    for y in range(160):
        assert level[y*240:y*240+20] == pixels[y*240:y*240+20]
        assert level[y*240+220:y*240+240] == pixels[y*240+220:y*240+240]
    for button, reference_frame in reference_frames.items():
        current = level
        if button=="door":
            current=closed
        elif button=="open":
            current=opened
        elif button=="entered":
            current=entered
        elif button=="corridor":
            current=corridor
        elif button=="next_level":
            current=next_level
        elif button==4:
            # Capture immediately after the step and before the next AI
            # decision, the same animation pose as the cartridge snapshot.
            current=render(core,expected,start=True,walk=[(72,80,4)],final_frame=84)
        elif button is not None:
            motion = [(85,220,7)] if button == "held" else [(85,97,button)]
            current = render(core, expected, start=True, walk=motion)
        magic, size, maximum, image = reference_frame.split(b"\n", 3)
        assert (magic, size, maximum, len(image)) == (b"P5", b"200 160", b"255", 32000)
        shades = {0:colors[3], 63:colors[2], 126:colors[1], 252:colors[0]}
        for y in range(150):
            for x in range(200):
                # On entering room 1 the original overlays page 4a at the left edge.
                if button == "entered" and x < 31 and 22 <= y < 80:
                    continue
                assert current[y*240+x+20] == shades[image[y*200+x]], (button,x,y)
    print("ZIP/raw identity, rejection, output preservation and both mGBA screens passed.")


if __name__ == "__main__":
    main()
