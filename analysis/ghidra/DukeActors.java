//@category GameCom
import ghidra.app.script.GhidraScript;

public class DukeActors extends GhidraScript {
    private int pointer(int address) throws Exception {
        return getShort(toAddr(address)) & 0xffff;
    }
    @Override
    public void run() throws Exception {
        createLabel(toAddr(0x60d0),"advance_actors",true);
        createLabel(toAddr(0x6767),"draw_actors_at_distance",true);
        createLabel(toAddr(0x69fe),"apply_actor_damage",true);
        createLabel(toAddr(0x6eff),"spawn_level_actors",true);
        createLabel(toAddr(0x70be),"write_actor_record",true);
        createLabel(toAddr(0x70f6),"actor_action_tables",true);
        createLabel(toAddr(0x7a25),"project_actor_x",true);
        for (int type=0;type<4;type++) {
            int actions=pointer(0x70f6+2*type);
            createLabel(toAddr(actions),"actor_actions_"+type,true);
            for (int action=0;action<=10;action++) {
                int distances=pointer(actions+2*action);
                for (int d=0;d<4;d++) {
                    int phases=getByte(toAddr(distances+3*d)) & 0xff;
                    int frames=pointer(distances+3*d+1);
                    for (int i=0;i<=phases;i++) {
                        int art=pointer(frames+5*i+3);
                        if (art<0x8000 || art+4>=0xa000)
                            throw new IllegalStateException("Frame outside graphics bank: "+Integer.toHexString(art));
                    }
                }
            }
        }
        println("Validated action frames for all four enemy types");
    }
}
