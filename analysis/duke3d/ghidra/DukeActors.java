//@category GameCom
import ghidra.app.script.GhidraScript;

public class DukeActors extends GhidraScript {
    private int pointer(int address) throws Exception {
        return getShort(toAddr(address)) & 0xffff;
    }
    @Override
    public void run() throws Exception {
        createLabel(toAddr(0x60d0),"advance_actors",true);
        createLabel(toAddr(0x61ef),"choose_action_by_distance",true);
        createLabel(toAddr(0x63ae),"prepare_forward_step",true);
        createLabel(toAddr(0x6556),"strafe_right",true);
        createLabel(toAddr(0x65d6),"strafe_left",true);
        createLabel(toAddr(0x6879),"actor_scenery_collision",true);
        createLabel(toAddr(0x68f0),"actor_actor_collision",true);
        createLabel(toAddr(0x6d2f),"leave_ammo_or_shotgun",true);
        createLabel(toAddr(0x6767),"draw_actors_at_distance",true);
        createLabel(toAddr(0x69fe),"apply_actor_damage",true);
        createLabel(toAddr(0x649d),"strike_player",true);
        createLabel(toAddr(0x6b3e),"split_damage_between_armor_and_health",true);
        createLabel(toAddr(0x6b94),"subtract_decimal_counter",true);
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
