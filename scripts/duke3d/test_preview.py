"""Check the standalone converter and the actual mGBA video output."""
import argparse
from collections import deque
import ctypes as c
import hashlib
import itertools
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]

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
        subprocess.run([sys.executable,Path(__file__).with_name("bonus_checkpoint.py"),snapshot,name,prepared],check=True)
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
    buttons={"F":4,"D":5,"L":6,"R":7,"B":8}
    # Locate the native room directory after the palette and gameplay tiles.
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
                aim=10 if x<50 else 11 if x>150 else 0
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
                yield [8],4,None
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
        if stage//5!=next_stage//5:
            episode=next_stage//5
            yield [],176,("opening",10+episode)
            yield [],122,("opening",13+episode)
        yield [],1,next_stage
        yield [],132,None
        assert position()[:2]==(next_stage,0),(next_stage,position())


def render(core_path, rom, start=False, firing=False, walk=(), final_frame=0, stage_cards=None,
           campaign=None,tail=(),observed=None,episode=0,audio_samples=None,save=None,audio_start=120,audio_end=320,
           intro_frames=1000,video_probe=None,controller=None,pixel_probe=None):
    core = c.CDLL(str(core_path))
    frames = []
    capture=False
    pixel_format = 0
    keys = set()
    iwram=None
    memory={}

    def environment(command, data):
        nonlocal pixel_format,iwram
        if command==0x10024:
            mapping=c.cast(data,c.POINTER(MemoryMap))[0]
            memory.update((d.start,d.ptr+d.offset) for d in mapping.descriptors[:mapping.count] if d.ptr)
            iwram=memory.get(0x03000000)
            return True
        if command == 10:
            pixel_format = c.cast(data, c.POINTER(c.c_uint))[0]
            return pixel_format in (0, 1, 2)
        if command == 3:
            c.cast(data, c.POINTER(c.c_bool))[0] = True
            return True
        return False

    def video(ptr, width, height, pitch):
        if ptr and video_probe and frame>=0:
            video_probe(frame,memory)
        if ptr and pixel_probe and frame>=0:
            pixel_probe(frame,ptr,pitch,4 if pixel_format==1 else 2)
        if ptr and capture:
            step = 4 if pixel_format == 1 else 2
            frames[:] = [b"".join(c.string_at(ptr + y*pitch, width*step)
                                  for y in range(height))]

    audible=set()
    frame=-1
    def audio_batch(ptr,n):
        if ptr and audio_samples is not None and audio_start<=frame<audio_end:
            audio_samples.setdefault(frame,bytearray()).extend(c.string_at(ptr,4*n))
        if firing and frame>=119 and ptr:
            pcm=c.cast(ptr,c.POINTER(c.c_int16))
            if any(abs(pcm[i])>256 for i in range(0,2*n,16)):
                audible.add(frame)
        return n

    callbacks = []
    for name, signature, function in [
        ("environment", (c.c_bool, c.c_uint, c.c_void_p), environment),
        ("video_refresh", (None, c.c_void_p, c.c_uint, c.c_uint, c.c_size_t), video),
        ("audio_sample", (None, c.c_int16, c.c_int16), lambda *_: None),
        ("audio_sample_batch", (c.c_size_t, c.c_void_p, c.c_size_t), audio_batch),
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
            if save is not None:
                core.retro_get_memory_data.restype=c.c_void_p
                size=core.retro_get_memory_size(0)
                ram=core.retro_get_memory_data(0)
                assert ram and size>=256, "SRAM unavailable"
                if save:
                    assert len(save)==size
                    c.memmove(ram,bytes(save),size)
            if start:
                # Navigate the real opening before the existing timed gameplay tape.
                for _ in range(intro_frames): core.retro_run()
                for button in [3,8]+[5]*episode+[8,8,8,8]:
                    keys.add(button)
                    for _ in range(4): core.retro_run()
                    keys.clear()
                    for _ in range(4): core.retro_run()
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
                if controller is not None:
                    next_keys=controller(frame,memory)
                    if next_keys is None:
                        done=capture=True
                        keys.clear()
                    else:
                        keys=set(next_keys)
                if firing and frame == 120:
                    keys.add(0)
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
            if save is not None:
                save[:]=c.string_at(ram,size)
            if campaign: assert done,"campaign replay timed out"
            if firing:
                intervals = [b-a for a,b in zip(shot_frames, shot_frames[1:])]
                assert len(audible)>20, "silent mGBA audio output"
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


def check_scenery_sprite_updates(core,rom):
    """Step towards the street scenery while the first enemies approach."""
    samples=[]

    def probe(frame,memory):
        if 84<=frame<=96:
            oam=c.string_at(memory[0x07000000]+48*8,40*8)
            vram=memory[0x06000000]
            scenery=c.string_at(vram+0x8c00,17*30*32)
            first_piece=c.string_at(vram+0x10000+384*32,512)
            samples.append((frame,oam,scenery,first_piece))

    render(core,rom,start=True,walk=[(85,87,4)],final_frame=96,video_probe=probe)
    assert samples and samples[0][0]==84
    _,old_oam,old_scenery,old_piece=samples[0]
    assert struct.unpack_from("<H",old_oam)[0]!=0x200 and any(old_scenery) and any(old_piece)
    for frame,oam,scenery,piece in samples[1:]:
        if oam!=old_oam:
            assert scenery!=old_scenery and piece!=old_piece, frame
            return
        assert scenery==old_scenery and piece==old_piece, ("mixed scenery and sprite frame",frame)
    raise AssertionError("street move did not publish new sprites")


def press_steps(walk, frame, commands):
    # Long neutral gaps allow the same route to cross rooms with different
    # movement periods, and each press remains a single game action.
    buttons={"F":4,"L":6,"R":7,"B":8,"D":5}
    for command in commands.split():
        for _ in range(int(command[1:] or 1)):
            walk.append((frame,frame+4,buttons[command[0]]))
            frame+=20
    return frame


def campaign_offset(rom):
    palette=[(0,0,0),(27,31,17),(13,17,9),(1,9,5),(0,0,0),(22,22,21),(5,5,5)]
    raw=b"".join(struct.pack("<H",r|(g<<5)|(b<<10)) for r,g,b in palette)+bytes(18)
    return rom.index(raw)+32+19200


def card_tiles(rom,card):
    campaign=campaign_offset(rom)
    def word(pos):
        return struct.unpack_from("<I",rom,pos)[0]
    levels=word(campaign)
    cards=campaign+word(campaign+4*(levels+1))
    bank=campaign+word(campaign+4*(levels+2))
    records=bank+word(bank)
    if isinstance(card,tuple):
        opening=campaign+word(campaign+4*(levels+7))
        cards=opening+4+12*word(opening)
        card=card[1]
    layout=records+word(cards+4*card)
    tiles=bytearray()
    for row in struct.unpack_from("<20I",rom,layout):
        for strip in struct.unpack_from("<10I",rom,records+row):
            for tile in struct.unpack_from("<3I",rom,records+strip):
                if tile&1:
                    tiles.extend(rom[records+tile-1:records+tile+31])
                else:
                    for line in struct.unpack_from("<8H",rom,records+tile):
                        start=bank+4+4*line
                        tiles.extend(rom[start:start+4])
    return tiles


def check_main_menu(core,rom):
    def navigate(buttons, **kwargs):
        tape=[(1000+12*i,1004+12*i,key) for i,key in enumerate(buttons)]
        return render(core,rom,walk=tape,**kwargs)

    # Hash the complete LCD, including all unselected entries, against Tigerbyte.
    hashes=["a6971ad23160a85abf52c1470abbb0a08091f87c4303f0386a2610704fc378cd",
            "3ee744809ab832e159ff18c7a1ac67eb9dbf7bb17dcb88b589d3786db31fd004",
            "0a2a3de0cdf2a2f11f617849e272ca6f8057e70a3bfb2e8524f0e965ff450152",
            "33ada3bf9267c154036c4ecc31e7df11e494f59c100cbf8115bf84e517e1725e"]
    def lcd_hash(pixels):
        lcd=bytes({27:252,13:126,1:63,0:0}[pixels[y*240+x][0]]
                  for y in range(160) for x in range(20,220))
        return hashlib.sha256(lcd).hexdigest()
    for buttons,digest in zip(([3],[3,5],[3,5,5],[3,5,8]),hashes):
        assert lcd_hash(navigate(buttons))==digest,buttons
    # Uninterrupted opening matches the cartridge.
    for frame,digest in [(984,"c708e445ef8c0eed4ba25236ee448521021e2dace76a689970b7996a2a04aa44"),
                         (1306,"08500a434dbdbc35c34401de6363c84a1042de60e49c38494b254b5977b02061"),
                         (1450,"60c335463e79569c3314cca6ff4cf50cd03bf475efc9104381b26119838e151a"),
                         (1533,"ef9b967bd890b39682d96ff3b8554900fe66133d9eb68831903034b4c957f659"),
                         (1650,hashes[0])]:
        assert lcd_hash(render(core,rom,final_frame=frame))==digest,frame
    copyright_screen=render(core,rom,final_frame=600)
    for key in (8,3):
        logo=[(64,84,key)]
        assert render(core,rom,walk=logo,final_frame=78)==copyright_screen
        skip=logo+[(100,140,key)]
        assert lcd_hash(render(core,rom,walk=skip,final_frame=118))=="c708e445ef8c0eed4ba25236ee448521021e2dace76a689970b7996a2a04aa44"
        assert lcd_hash(render(core,rom,walk=skip+[(150,170,key)],final_frame=167))==hashes[0]
    assert lcd_hash(navigate([8]))==hashes[0]
    quit_tape=[(1000+12*i,1004+12*i,key) for i,key in enumerate([3,5,5,8])]
    assert lcd_hash(render(core,rom,walk=quit_tape+[(2040,2044,3)]))==hashes[0]

    save=bytearray()
    render(core,rom,start=True,episode=1,save=save)
    assert save[0]==0xd3 and save[1]==5
    checkpoint=bytes(save[:18])
    navigate([3,5,8,5,8,8],save=save)
    assert save[18]==0xad and save[:18]==checkpoint
    navigate([3,5,8,5,8,8],save=save)
    assert save[18]==0 and save[:18]==checkpoint
    buttons=[3,5,8,8]
    cursor=0
    for symbol in save[13:17]:
        if symbol//10!=cursor//10: buttons.append(5 if symbol>=10 else 4)
        dx=symbol%10-cursor%10
        buttons.extend([7 if dx>0 else 6]*abs(dx))
        buttons.append(8)
        cursor=symbol
    state={}
    navigate(buttons+[3,8],save=save,observed=state)
    assert state["position"][:2]==(5,0) and save[:18]==checkpoint,state
    print("mGBA verified main menu, quit, sound preference and password after reboot.")


def check_audio_mix(core,rom):
    # Silence only the selected source data in three test copies. The four
    # replays send identical inputs and execute the same native sound driver.
    campaign=campaign_offset(rom)
    def word(p):
        return struct.unpack_from("<I",rom,p)[0]
    audio=campaign+word(campaign+4*(word(campaign)+8))
    def replay(muted):
        image=bytearray(rom)
        for id in muted:
            p=audio+word(audio+4*id)
            if id<27:
                length=word(p)
                image[p+4:p+4+length]=bytes(length)
            else:
                while image[p+3]:
                    image[p+2]=0
                    p+=4
        samples={}
        render(core,bytes(image),start=True,walk=[(85,97,4),(120,220,0)],
               final_frame=220,audio_samples=samples)
        return samples
    mix=replay([])
    without_music=replay([29,30])
    without_weapon=replay([10])
    without_monsters=replay([25,26])
    def audible_difference(first,second):
        a=memoryview(first).cast("h")
        b=memoryview(second).cast("h")
        assert len(a)==len(b)
        return sum(abs(a[i]-b[i]) for i in range(0,len(a),16))>128*(len(a)//16)
    overlap=[frame for frame in mix if all(audible_difference(mix[frame],probe[frame])
             for probe in (without_music,without_weapon,without_monsters))]
    assert len(overlap)>=5,("missing simultaneous music, weapon or monster audio",overlap)
    # Compare the end of the opening line with and without a held trigger.
    line=audio+word(audio+4*17)
    quiet=bytearray(rom)
    quiet[line+4:line+4+word(line)]=bytes(word(line))
    endings=[]
    for inputs in ([],[(78,355,0)]):
        recordings=[]
        for image in (rom,bytes(quiet)):
            frames={}
            render(core,image,start=True,walk=inputs,final_frame=360,audio_samples=frames,
                   audio_start=280,audio_end=361)
            recordings.append(frames)
        a,b=recordings
        voiced=[f for f in a if audible_difference(a[f],b[f])]
        assert voiced,"missing end of opening speech"
        endings.append(voiced[-1])
    assert abs(endings[0]-endings[1])<=1,endings
    print("mGBA mixed music, gunfire and monster sounds on",len(overlap),"combat frames.")


def check_combat_speech(core,rom,route):
    bank=campaign_offset(rom)
    def word(p):
        return struct.unpack_from("<I",rom,p)[0]
    bank+=word(bank+4*(word(bank)+8))
    def replay(silent,intro_frames,save=None):
        image=bytearray(rom)
        if silent is not None:
            sample=bank+word(bank+4*silent)
            length=word(sample)
            image[sample+4:sample+4+length]=bytes(length)
        frames={}
        render(core,bytes(image),start=True,campaign=route,audio_start=0,intro_frames=intro_frames,save=save,
               audio_end=250000,audio_samples=frames)
        return [hashlib.sha256(pcm).digest() for pcm in frames.values()]
    heard=set()
    first_taunts=set()
    kills=0
    lines=0
    for delay in range(1000,1320,17):
        save=bytearray()
        baseline=replay(None,delay,save)
        kills+=struct.unpack_from("<H",save,11)[0]
        starts=[]
        for sample in (18,24,20,4):
            probe=replay(sample,delay)
            diff=[i for i,(normal,muted) in enumerate(zip(baseline,probe)) if normal!=muted]
            if sample==4:
                assert len(diff)>=5,"inaudible results farewell"
            elif len(diff)>=5:
                heard.add(sample)
                starts.append((diff[0],sample))
                lines+=1+sum(b-a>30 for a,b in zip(diff,diff[1:]))
        if starts:
            first_taunts.add(min(starts)[1])
        if len(heard)==3 and len(first_taunts)>1 and kills>=40:
            break
    assert len(heard)==3 and len(first_taunts)>1,(heard,first_taunts)
    assert 0<lines<kills/2,(lines,kills)
    print(f"mGBA heard all three combat taunts, {lines} lines across {kills} kills.")


def check_episode_openings(core,rom,cart):
    campaign=campaign_offset(rom)
    def word(p):
        return struct.unpack_from("<I",rom,p)[0]
    audio=campaign+word(campaign+4*(word(campaign)+8))
    def entry(id):
        return audio+word(audio+4*id)
    def listen(image,episode,last_frame):
        pcm={}
        render(core,bytes(image),start=True,episode=episode,final_frame=last_frame,
               audio_samples=pcm,audio_start=0)
        return pcm
    def audible_frames(pcm,muted):
        return [frame for frame in pcm if
                max(abs(a-b) for a,b in zip(memoryview(pcm[frame]).cast("h"),
                                           memoryview(muted[frame]).cast("h")))>128]

    no_speech=bytearray(rom)
    speech=entry(17)
    no_speech[speech+4:speech+4+word(speech)]=bytes(word(speech))
    colors=[(27,31,17),(13,17,9),(1,9,5),(0,0,0)]
    for episode in range(3):
        assert (listen(rom,episode,115)!=listen(no_speech,episode,115))==(episode==0),episode
        buttons=[3,8]+[5]*episode+[8,8,8]
        walk=[(1000+8*i,1004+8*i,button) for i,button in enumerate(buttons)]
        pixels=render(core,rom,walk=walk,final_frame=1000+8*len(buttons))
        for y in range(160):
            for x in range(200):
                packed=cart[(0x27-episode)*16384+64*x+y//4]
                assert pixels[240*y+20+x]==colors[(packed>>(6-2*(y%4)))&3],(episode,x,y)

    first,second=entry(31),entry(32)
    score_bank=0x2b*8192
    second_source=range(score_bank+0x04b9,score_bank+0x0654,3)
    assert entry(33)==second+4*(len(second_source)+1)
    assert [rom[second+4*i+3] for i in range(len(second_source))]==[cart[p+2] for p in second_source]
    assert word(entry(33)-4)==0, "episode 2's second voice must end before episode 3"

    # A long second voice may never play past the first voice's repeat.
    score=bytearray(rom)
    struct.pack_into("<HBBI",score,first,1750,31,32,0)
    struct.pack_into("<HBBHBBHBBI",score,second,1580,31,8,1620,31,28,1710,31,16,0)
    alternate=bytearray(score)
    struct.pack_into("<H",alternate,second+8,1810)
    pcm=listen(score,1,120)
    assert pcm==listen(alternate,1,120), "second voice played beyond the repeat point"

    # Begin a long note after audio capture starts. Its decay should last
    # longer than the former 42-step hardware counter and end before the note.
    struct.pack_into("<HBBHBBI",score,first,0,0,32,1750,31,64,0)
    score[second+2]=score[second+6]=score[second+10]=0
    quiet=bytearray(score)
    quiet[first+6]=0
    audible=audible_frames(listen(score,1,75),listen(quiet,1,75))
    assert 11<=len(audible)<=16 and audible[-1]<75,audible

    # The pulse repeats every 32 units, or 800 ms (about 48 display frames).
    # Give the second voice an early end, then listen for it at each restart.
    struct.pack_into("<HBBI",score,first,0,0,32,0)
    struct.pack_into("<HBBI",score,second,1580,31,2,0)
    quiet=bytearray(score)
    quiet[second+2]=0
    audible=audible_frames(listen(score,1,230),listen(quiet,1,230))
    pulses=[frame for i,frame in enumerate(audible) if i==0 or frame>audible[i-1]+1]
    assert len(pulses)>=4 and all(47<=b-a<=49 for a,b in zip(pulses[1:],pulses[2:])),pulses
    print("mGBA verified episode art, the opening line, score bounds, 25 ms music units, shared repeats and note decay.")


def check_stage_card(pixels,rom,entering_stage):
    palette=[(0,0,0),(27,31,17),(13,17,9),(1,9,5),(0,0,0),(22,22,21),(5,5,5)]
    title=card_tiles(rom,entering_stage if isinstance(entering_stage,tuple) else entering_stage*3+2)
    for y in range(160):
        for x in range(240):
            pos=32*(30*(y//8)+x//8)+4*(y%8)+(x%8)//2
            color=(title[pos]>>(4*(x%2)))&15
            assert pixels[240*y+x]==palette[color], (entering_stage,x,y)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cartridge", type=Path, help="Duke USA ZIP")
    parser.add_argument("--core", default=os.environ.get("MGBA_LIBRETRO"))
    parser.add_argument("--reference", type=Path, help="reference.c executable")
    parser.add_argument("--bios-dir", type=Path, help="directory containing the two game.com BIOS ZIPs")
    args = parser.parse_args()
    core = args.core or next((Path(os.environ["LOCALAPPDATA"]) / "Programs/mGBA")
                             .glob("libretro-*/mgba_libretro.dll"))
    with zipfile.ZipFile(args.cartridge) as archive:
        data = archive.read(next(i for i in archive.infolist() if i.file_size == 2097152))
    reference_frames = {}
    # Number of weapon switches after granting the arsenal, SM8500 art
    # descriptor, duration of held fire in the reference, GBA shot age.
    weapon_samples=[(1,0x86e5,18,17),(1,0x86db,44,30),
                    (2,0x8712,23,17),(3,0x868b,22,16)]
    reference_weapons={}
    with tempfile.TemporaryDirectory(dir=ROOT / "build/duke3d") as tmp:
        work = Path(tmp)
        converter_dir = work / "converter"
        converter_dir.mkdir()
        exe = Path(shutil.copy2(ROOT / "dist/duke3d/gamecom2gba.exe", converter_dir))
        outputs = [converter_dir / f"duke3d-{count}-maps.gba" for count in (12, 15)]
        raw = work / "renamed.bin"
        raw.write_bytes(data)
        result = subprocess.run([exe, args.cartridge.resolve()], cwd=work, check=True,
                                capture_output=True, text=True)
        assert result.stdout.splitlines() == [str(path) for path in outputs]
        expected, extended = (path.read_bytes() for path in outputs)
        flags = campaign_offset(expected) + 4 * (15 + 5)
        assert expected[flags:flags+4] == bytes(4)
        assert extended == expected[:flags] + b"\x07" + expected[flags+1:]
        subprocess.run([exe, raw.name], cwd=work, check=True)
        assert [p.read_bytes() for p in outputs] == [expected, extended]
        # Compare every card with the lossless, pre-optimization graphics.
        cards=hashlib.sha256()
        for card in range(46):
            tiles=card_tiles(expected,card)
            # Hash the LCD only, so refinements to the bezel are independent.
            for y in range(160):
                for tx in range(2,28):
                    off=32*(30*(y//8)+tx)+4*(y%8)
                    cards.update(tiles[off+(2 if tx==2 else 0):off+(2 if tx==27 else 4)])
        assert cards.hexdigest()=="ea52b33d2cc22130e1ff09da5608cf2625a9eaad1a331c318f14522ebe35d113"
        raw.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
        assert subprocess.run([exe, raw], cwd=work).returncode == 1
        assert [p.read_bytes() for p in outputs] == [expected, extended]
        unrelated = work / "other.zip"
        with zipfile.ZipFile(unrelated, "w") as archive:
            archive.writestr("Duke Nukem 3D (USA).tgc", b"unrecognized cartridge")
        assert subprocess.run([exe, unrelated], cwd=work).returncode == 1
        assert [p.read_bytes() for p in outputs] == [expected, extended]
        unrelated.unlink()
        assert set(work.iterdir()) == {converter_dir, raw}
        assert set(converter_dir.iterdir()) == {exe, *outputs}
        if args.reference:
            raw.write_bytes(data)
            bios = []
            for role, sha in [("Internal", "03368237e8fed4a8724f3b4a1596cf4b17c96d33"),
                              ("External", "97f782e72d738f4d7b861363266bf46b438d9b50")]:
                name = "[BIOS] Tiger Game.com (USA) (" + role + ").zip"
                bios_dir = args.bios_dir or args.cartridge.parent
                if args.bios_dir is None and not (bios_dir / name).is_file():
                    bios_dir = bios_dir.parent
                with zipfile.ZipFile(bios_dir / name) as archive:
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
            record=data[0x21*8192+0xf18:0x21*8192+0xf30]
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
            for extra, events in [
                (["100 ff","18 7f","220 ff"],
                 ["motion 5803 0 1 34 108 2 1","attack frame=5863","attack frame=5963"]),
                (["20 ff","18 fe","150 ff"],
                 ["walk 5719 1 2","attack frame=5732","attack frame=5833"])]:
                inputs=rows[:-1]+[s+" ff ff -1 0" for s in extra]
                run=subprocess.run(original,cwd=work,check=True,input="\n".join(lobby_tape+inputs).encode(),
                                   capture_output=True,env=dict(os.environ,GAMECOM_TRACE_COMBAT="5675",
                                       GAMECOM_TRACE_ACTOR_MOTION="5675",GAMECOM_TRACE_WALK="5675"))
                for event in events:
                    assert event.encode() in run.stderr,event
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
    pixels = render(core, expected,final_frame=600)
    assert len(pixels) == 240*160
    colors = [(27,31,17), (13,17,9), (1,9,5), (0,0,0)]
    for y in range(160):
        for x in range(200):
            packed = data[0x50*16384 + 64*(x+56) + (y+96)//4]
            assert pixels[y*240+x+20] == colors[(packed >> (6-2*(y%4))) & 3], (x,y)
    for x0 in (0, 220):
        side = {pixels[y*240+x] for y in range(160) for x in range(x0, x0+20)}
        assert side == {(22,22,21), (5,5,5)}
    left_ink=[x for x in range(19) if any(pixels[y*240+x]==(5,5,5) for y in range(160))]
    assert (min(left_ink),max(left_ink))==(2,15)
    check_main_menu(core,expected)
    check_audio_mix(core,expected)
    check_episode_openings(core,expected,data)
    check_scenery_sprite_updates(core,expected)
    level = render(core, expected, start=True)
    for episode in (1,2):
        state={}
        render(core,expected,start=True,episode=episode,observed=state)
        assert state["position"][:2]==(5*episode,0),state
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
    # The exit press is held for four frames with a sixteen-frame release in
    # drive(). Compare each of the visible emblem changes with the original LCD.
    whole="e8da89a17b35e3d94392a3add841a079c566931975b43392ba687b89ec8eb872"
    split="63213b83131f599d1cdb732f4322f76cb17e13154c3cfbf952cc133eaf75863f"
    for wait,digest in [(62,whole),(84,split),(112,whole)]:
        frame,_,_=visit(finish,[([],wait)])
        lcd=bytes({27:252,13:126,1:63,0:0}[frame[y*240+x][0]]
                  for y in range(160) for x in range(20,220))
        assert hashlib.sha256(lcd).hexdigest()==digest,(wait,"exit animation")
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
    check_combat_speech(core,expected,[first,routes[0]])
    normal=[first,*routes]
    commands_by_stage=dict(zip((0,1,2,3,5,6,7,8,10,11,12,13),
                               (commands for _,commands in normal)))
    commands_by_stage.update({4:BONUS_ROUTES[5],9:BONUS_ROUTES[10],14:BONUS_ROUTES[0]})
    full_order=[0,1,2,4,3,5,6,7,8,9,10,11,12,13,14,0]
    extended_route=[(dest,commands_by_stage[src]) for src,dest in zip(full_order,full_order[1:])]
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
    before=render(core,expected,start=True,final_frame=84)
    dialog=render(core,expected,start=True,walk=paused,final_frame=110)
    # Tigerbyte's 168x64 system dialog in the original LCD shades.
    shades={27:252,13:126,1:63,0:0}
    panel=bytes(shades[dialog[240*y+x][0]] for y in range(48,112) for x in range(36,204))
    assert hashlib.sha256(panel).hexdigest()=="5748cefdce0bf8f0c5135f9e78b86a64dfcdc54a6547dcea4e90482ec4643e74"
    assert all(before[i]==pixel for i,pixel in enumerate(dialog)
               if not (36<=i%240<204 and 48<=i//240<112))
    assert render(core,expected,start=True,walk=paused+[(155,157,3)],final_frame=158)==before
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
    # Exercise the PCM routing through real weapon switches and B. Only the
    # sample selected by the cartridge for that weapon should affect output.
    audio_bank=campaign_offset(expected)
    level_count=struct.unpack_from("<I",expected,audio_bank)[0]
    audio_bank+=struct.unpack_from("<I",expected,audio_bank+4*(level_count+8))[0]
    for switches,effect in enumerate((10,12,11,3)):
        muted=bytearray(expected)
        sample=audio_bank+struct.unpack_from("<I",expected,audio_bank+4*effect)[0]
        length=struct.unpack_from("<I",expected,sample)[0]
        muted[sample+4:sample+4+length]=bytes(length)
        route=armed+[(171+12*i,173+12*i,8) for i in range(switches)]+[(220,221,0)]
        recordings=[]
        for image in (expected,bytes(muted)):
            pcm={}
            render(core,image,start=True,walk=route,final_frame=240,audio_samples=pcm)
            recordings.append(pcm)
        a,b=recordings
        assert all(a[f]==b[f] for f in range(200,220)),effect
        assert any(a[f]!=b[f] for f in range(221,240)),effect
    for count,descriptor,_,age in weapon_samples:
        switches=[(171+12*i,173+12*i,8) for i in range(count)]
        frame=render(core,expected,start=True,walk=armed+switches+[(220,221,0)],final_frame=220+age)
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
    empty,_,empty_state=visit(enter_corridor,[([0],7000),([],100)])
    assert empty_state["weapon"][0]==0 and empty_state["weapon"][5]==0
    check_counter(empty,169,None)
    west=detour+" L11 B F"
    step=[([4],4),([],16)]
    floor,_,_=visit("R6 F8 B F L6 F11 B F")
    picked,at,arrival=visit(west,step)
    assert at==(2,1,1),at
    # Compare the floor shotgun with its distance-two cartridge sprite.
    # The held pistol obscures its middle, so compare both exposed ends.
    def word(bank,address):
        return struct.unpack_from(">H",data,bank*8192+(address&8191))[0]
    art=word(0x22,word(0x22,0x7987+2*(9-1))+2)
    width,height,sx,sy,page=data[0x26*8192+(art&8191):][:5]
    bottom=data[0x22*8192+(0x6a3d&8191)]
    matches=visible=0
    for y in range(height):
        for x in range(width):
            if width//2-14<=x<width//2+14: continue
            raw=data[page*16384+64*(sx+x)+(sy+y)//4]
            shade=(raw>>(6-2*((sy+y)%4)))&3
            if shade:
                visible+=1
                matches+=floor[(bottom-height+y)*240+120-width//2+x]==colors[shade]
    assert visible>300 and matches>visible*0.95,(matches,visible)
    # SHOT GUN, centered in the top row, is the cartridge pickup notice.
    assert any(picked[y*240+88:y*240+152]!=floor[y*240+88:y*240+152] for y in range(8))
    _,_,injured=visit(west,step+[([],180)])
    assert injured["inventory"][2]<arrival["inventory"][2], "corridor attacker missed"
    # Measure from actual contact or injury, independent of the route's duration.
    for wounded in (False,True):
        onset=[]
        def watch_corridor(frame,memory):
            ram=memory[0x03000000]
            _,room,_,depth=struct.unpack("<4I",c.string_at(ram,16))
            if room==2 and not onset:
                action=c.c_uint8.from_address(ram+POSITION_SIZE+5).value
                if (action==1 if wounded else depth==2):
                    onset.append(frame)
        state={}
        extra=[([],100),([0],4)] if wounded else [([4],4)]
        render(core,expected,start=True,campaign=[(None,west)],tail=step+extra+[([],260)],
               observed=state,video_probe=watch_corridor)
        assert onset,(wounded,state)
        hits=[f for f,_ in state["hits"] if f>onset[0]]
        assert len(hits)>=2 and abs(hits[0]-onset[0]-(60 if wounded else 13))<=3,(onset,hits)
        assert 99<=hits[1]-hits[0]<=102,hits
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
