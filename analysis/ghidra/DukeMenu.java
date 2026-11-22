//@category GameCom
import ghidra.app.script.GhidraScript;

public class DukeMenu extends GhidraScript {
    @Override
    public void run() throws Exception {
        createLabel(toAddr(0x6221),"start_selected_episode",true);
        createLabel(toAddr(0x628a),"consume_system_record_flags",true);
        createLabel(toAddr(0x62b0),"unpack_bonus_episode_bits",true);
        createLabel(toAddr(0x6a05),"pause_input_loop",true);
        createLabel(toAddr(0x6b26),"item_code_buttons",true);
        createLabel(toAddr(0x6b2a),"weapon_code_buttons",true);
        createLabel(toAddr(0x6cb2),"give_weapons_and_ammunition",true);
        createLabel(toAddr(0x6cd7),"give_key_and_armor",true);
        println("Labeled menu, bonus episode and pause input handlers");
    }
}
