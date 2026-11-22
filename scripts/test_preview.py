"""Check the standalone converter and the actual mGBA video output."""
import argparse
import ctypes as c
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class GameInfo(c.Structure):
    _fields_ = [("path", c.c_char_p), ("data", c.c_void_p),
                ("size", c.c_size_t), ("meta", c.c_char_p)]


def render(core_path, rom, start=False, firing=False, walk=(), final_frame=0, stage_cards=None):
    core = c.CDLL(str(core_path))
    frames = []
    capture=False
    pixel_format = 0
    keys = set()

    def environment(command, data):
        nonlocal pixel_format
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
            previous_view = None
            last_input=max((end for _,end,_ in walk), default=0)
            events={}
            for pressed,released,button in walk:
                events.setdefault(pressed,[]).append((button,True))
                events.setdefault(released,[]).append((button,False))
            end_frame=max(360 if firing else 120,last_input+15,final_frame+1)
            for frame in range(end_frame):
                check_card=stage_cards is not None and frame in stage_cards
                capture=firing or held_walk or frame==end_frame-1 or check_card
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
                if check_card:
                    check_stage_card(unpack_colors(frames[0],pixel_format),rom,stage_cards[frame])
                if held_walk and frame >= 84:
                    view = frames[0]
                    if previous_view is not None and view != previous_view:
                        walk_frames.append(frame)
                    previous_view = view
                if firing and frame >= 119:
                    step = 4 if pixel_format == 1 else 2
                    # The muzzle rises above the idle sprite at y=83..95.
                    band = b"".join(frames[0][(y*240+106)*step:(y*240+134)*step]
                                    for y in range(83,96))
                    if frame == 119:
                        rest = band
                        flashing = False
                    elif (band != rest) != flashing:
                        flashing = band != rest
                        if flashing:
                            shot_frames.append(frame)
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


def check_stage_card(pixels,rom,entering_stage):
    palette=[(0,0,0),(27,31,17),(13,17,9),(1,9,5),(0,0,0),(22,22,21),(5,5,5)]
    raw=b"".join(struct.pack("<H",r|(g<<5)|(b<<10)) for r,g,b in palette)+bytes(18)
    campaign=rom.index(raw)+32+2*19200+6*2048
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
                         "room=0 lane=4 depth=3 key=0 medkit=1 items=100000")
            take_key=["80 f7","12 ff","145 fe","12 ff","130 fb","12 ff"]
            unlock=take_key+["128 f7","12 ff","60 fe","12 ff","16 fe ff fd","55 ff"]
            verify_items(lobby_tape,take_key,"room=1 lane=4 depth=9 key=1 medkit=0 items=010000")
            verify_items(lobby_tape,unlock,"room=1 lane=12 depth=11 key=0 medkit=0 items=010000")
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
    approach = [(85,164,7), (185,300,4)]
    closed = render(core, expected, start=True, walk=approach)
    blocked = render(core, expected, start=True, walk=approach+[(322,330,4)])
    opened = render(core, expected, start=True, walk=approach+[(319,321,0)])
    entered = render(core, expected, start=True, walk=approach+[(319,321,0),(322,330,4)])
    assert closed == blocked and opened != closed and entered != opened, "first door activation and entry"
    lobby=approach+[(319,321,0),(322,330,4)]
    # The locked exit is in lane 12. Go directly there, then repeat with a
    # detour through lane 4 to collect the key before approaching the lock.
    direct=lobby+[(351,418,7),(450,613,4)]
    detour=lobby+[(351,418,7),(450,582,4),(600,717,6),(730,847,7),(870,890,4)]
    def at_lock(route, uses):
        return render(core, expected, start=True, walk=route+[
            (915+12*i,918+12*i,0) for i in range(uses)]+[(960,968,4)])
    locked=at_lock(direct,0)
    assert at_lock(direct,2)==locked, "door opened without a key"
    assert at_lock(detour,1)==locked, "unlocking should consume the first use"
    assert at_lock(detour,2)!=locked, "key did not open the locked route"
    enter_corridor=detour+[(915,918,0),(927,930,0),(960,980,4)]
    corridor=render(core,expected,start=True,walk=enter_corridor)
    finish=enter_corridor+[(1010,1060,4),(1090,1094,0),(1110,1240,4),
                           (1270,1380,6),(1410,1414,0)]
    next_level=render(core,expected,start=True,walk=finish,final_frame=2000)
    # Three street enemies enter pistol range on the first step. Exhaust the
    # group, return to the starting cell, then read both intermission counters.
    fight=[(85,97,4),(110,375,8),(405,412,5)]
    after_fight=fight+[(a+400,b+400,key) for a,b,key in finish]
    score=render(core,expected,start=True,walk=after_fight,final_frame=2160)
    no_kills=render(core,expected,start=True,walk=finish,final_frame=1760)
    for y in (96,128):
        changed=False
        for row,bits in enumerate((7,1,7,1,7)):
            for col in range(3):
                pos=240*(y+row)+124+col
                if bits&(4>>col):
                    assert score[pos]==colors[0], ("frag tally",y,row,col)
                changed|=score[pos]!=no_kills[pos]
        assert changed, "frag count remained zero"
    # Ordinary campaign route. Each row ends at an exit; the value is the
    # index of the title expected next, including the original wraparound.
    routes=[
        (2,"L2 F6 B F57 B F L F18 R5 F14 B F75 B F9 L3 F B F L F6 B"),
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
    campaign_walk=finish.copy()
    cards={}
    resume=2010
    for next_stage,commands in routes:
        end=press_steps(campaign_walk,resume,commands)
        cards[end+470]=next_stage
        resume=end+590
    returned=render(core,expected,start=True,walk=campaign_walk,final_frame=resume,stage_cards=cards)
    assert returned[:150*240]==level[:150*240], "campaign wraparound spawn"
    print("mGBA crossed all 12 normal stage exits and returned to Hollywood Holocaust.")
    wall=[]
    frame=press_steps(wall,85,"R9 F4")
    at_wall=render(core,expected,start=True,walk=wall)
    frame=press_steps(wall,frame,"F")
    assert at_wall==render(core,expected,start=True,walk=wall), "street obstacle"
    press_steps(wall,frame,"L F")
    assert at_wall!=render(core,expected,start=True,walk=wall), "route around street obstacle"
    beside_medkit=[(85,104,7)]
    before=render(core,expected,start=True,walk=beside_medkit)
    round_trip=[(130,166,4),(190,226,5)]
    after=render(core,expected,start=True,walk=beside_medkit+round_trip)
    second_trip=[(start+120,end+120,button) for start,end,button in round_trip]
    assert before!=after, "medkit still visible after collection"
    assert after==render(core,expected,start=True,walk=beside_medkit+round_trip+second_trip)
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
