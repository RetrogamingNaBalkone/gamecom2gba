"""Find a reproducible sequence of normal GBA button presses for the final fight."""
import ctypes
from test_gameplay import A, B, DOWN, LEFT, R, RIGHT, SELECT, UP, render


def save_state(engine):
    engine.retro_serialize_size.restype=ctypes.c_size_t
    snapshot=ctypes.create_string_buffer(engine.retro_serialize_size())
    if not engine.retro_serialize(snapshot,len(snapshot)):
        raise RuntimeError("mGBA did not serialize its current state")
    return snapshot.raw


def restore(engine,snapshot):
    if not engine.retro_unserialize(snapshot,len(snapshot)):
        raise RuntimeError("mGBA rejected its own saved state")


def choose_input(tactic,frame,pressed,ram):
    direction,plane=divmod(ram[0x233],16)
    action=ram[0x236]
    ready=0x18<=action<=0x1f
    def turn(target):
        return (RIGHT if (target-direction)%8<=4 else LEFT,)
    name,*argument=tactic
    if name in ("turn","dash"):
        if direction!=argument[0]:
            return turn(argument[0])
        return (UP,B) if name=="dash" else ()
    if name=="line":
        dx=ram[0x219]-ram[0x234]
        if plane==1: target=2 if dx>=0 else 6
        elif abs(dx)<=14: target=0 if plane==0 else 4
        elif plane==0: target=1 if dx>=0 else 7
        else: target=3 if dx>=0 else 5
        if argument and plane==1 and frame<30:
            target=0 if argument[0]==1 else 4
            return turn(target) if direction!=target else (UP,)
        if direction!=target and (action<8 or ready):
            return turn(target)
        if action<8 and frame%20==5:
            return (R,)
        return (A,) if ready and not pressed else ()
    if name=="back":
        return (DOWN,B)
    if name=="rearm" and not frame:
        return (R,)
    if name in ("keep","auto","rearm"):
        if name=="auto" and action<8 and frame==6:
            return (R,)
        return (A,) if ready and not pressed else ()
    return ()


def find_winning_inputs(core,rom,snapshot,progress_offset):
    """Explore input alternatives from a state reached in ordinary play."""
    engine=ctypes.CDLL(str(core))
    result=[]
    held=()
    input_type=ctypes.CFUNCTYPE(ctypes.c_int16,ctypes.c_uint,ctypes.c_uint,ctypes.c_uint,ctypes.c_uint)
    @input_type
    def read_pad(port,device,index,button):
        return int(port==0 and device==1 and button in held)

    def search(frame,memory):
        nonlocal held
        restore(engine,snapshot)
        address=memory[0x02000000]+progress_offset
        def state():
            return ctypes.string_at(address,0x29c)
        engine.retro_set_input_state(read_pad)
        commands=[]
        for key in (SELECT,A,A,A,SELECT):
            for held in [()]*8+[(key,)]*4:
                engine.retro_run()
                commands.append(held)
        assert state()[0x268]==1,"Handgun equip sequence failed"
        front=[(save_state(engine),commands)]
        best_hp=126
        for generation in range(400):
            candidates={}
            for saved,path in front:
                restore(engine,saved)
                ram=state()
                face=2 if ram[0x219]>ram[0x234] else 6
                tactics=[("line",),("line",1),("line",2),("auto",),("keep",),("rearm",),
                         ("turn",face),("turn",0),("turn",4),("dash",2),("dash",6),
                         ("dash",0),("dash",4),("back",),("wait",)]
                for tactic in tactics:
                    restore(engine,saved)
                    trial=[]
                    pressed=False
                    for tick in range(60):
                        ram=state()
                        if ram[0x208:0x20a]!=bytes([4,107]) or not ram[0x238]:
                            break
                        held=choose_input(tactic,tick,pressed,ram)
                        pressed=A in held
                        engine.retro_run()
                        trial.append(held)
                    else:
                        ram=state()
                        if ram[0xc2]&1:
                            result.extend(path+trial)
                            return None
                        if ram[0x208:0x20a]!=bytes([4,107]) or ram[0x217]!=12 or not ram[0x238]:
                            continue
                        hp,boss_hp=ram[0x238],ram[0x21d]
                        x,enemy_x=ram[0x234],ram[0x219]
                        action=ram[0x236]
                        ready=0x18<=action<0x20 or 0x48<=action<0x50
                        score=(125-boss_hp)*10+hp*1.15+(ram[0x295]+ram[0x296])*.8
                        score+=min(abs(enemy_x-x),90)*.05+4*ready
                        pose=(boss_hp,hp,ram[0x233],x//8,enemy_x//8,action//8,ram[0x21b]//3)
                        if pose not in candidates or score>candidates[pose][0]:
                            candidates[pose]=(score,save_state(engine),path+trial)
            if not candidates:
                raise RuntimeError("No surviving gameplay branch in the final encounter")
            best=sorted(candidates.values(),key=lambda node:-node[0])[:10]
            front=[(saved,path) for _,saved,path in best]
            restore(engine,front[0][0])
            hp=state()[0x21d]
            if hp<best_hp:
                print(f"Final encounter, enemy HP {hp}",flush=True)
                best_hp=hp
        raise RuntimeError("No finishing tape found within 400 search steps")

    render(core,rom,controller=search,final_frame=1)
    return result
