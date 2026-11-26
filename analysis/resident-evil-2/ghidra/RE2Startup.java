//@category GameCom
import java.nio.file.Files;
import java.nio.file.Path;
import java.io.ByteArrayInputStream;
import java.util.HashSet;
import java.util.Set;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.SourceType;

public class RE2Startup extends GhidraScript {
    private MemoryBlock scenes;

    private Address scene(int cpuAddress) {
        if (cpuAddress < 0x8000 || cpuAddress >= 0xa000)
            throw new IllegalArgumentException("Outside scene window: " + cpuAddress);
        return scenes.getStart().add(cpuAddress - 0x8000);
    }

    private int word(Address addr) throws Exception {
        return getShort(addr) & 0xffff;
    }

    private void reference(Address from, Address to) {
        currentProgram.getReferenceManager().addMemoryReference(from, to, RefType.DATA,
            SourceType.USER_DEFINED, 0);
    }

    @Override
    public void run() throws Exception {
        var mem = currentProgram.getMemory();
        byte[] pages = Files.readAllBytes(Path.of(getScriptArgs()[0]));
        String[] names = {"MMU4_30", "MMU3_22", "MMU3_24", "MMU4_28", "MMU4_33",
                          "MMU3_23", "MMU4_29", "MMU4_34", "MMU4_36"};
        for (int i=0; i<names.length; ++i) {
            if (mem.getBlock(names[i]) == null) {
                int base = names[i].startsWith("MMU3") ? 0x6000 : 0x8000;
                var stream = new ByteArrayInputStream(pages,i*8192,8192);
                mem.createInitializedBlock(names[i],toAddr(base),stream,8192,monitor,true);
            }
        }
        scenes = mem.getBlock(names[0]);
        var world = mem.getBlock("MMU3_24").getStart();
        createLabel(world,"test_camera_edge",true);
        createLabel(world.add(0x276),"decode_transition",true);
        createLabel(world.add(0x778),"use_door",true);
        createLabel(world.add(0xcda),"use_typewriter",true);
        createLabel(world.add(0xd56),"typewriter_positions",true);
        createLabel(world.add(0xe79),"camera_perspectives",true);
        createLabel(world.add(0x16_19),"select_puzzle_prop",true);
        createLabel(world.add(0x16_77),"move_puzzle_prop",true);
        createLabel(world.add(0x17_83),"test_puzzle_prop",true);
        createLabel(mem.getBlock("MMU3_23").getStart().add(0xfac),"test_ground_item",true);
        createLabel(mem.getBlock("MMU4_29").getStart().add(0x1236),"ground_item_directory",true);
        createLabel(mem.getBlock("MMU4_34").getStart().add(0xcb6),"inventory_icon_directory",true);
        createLabel(mem.getBlock("MMU4_28").getStart(),"area_camera_directory",true);
        createLabel(mem.getBlock("MMU4_33").getStart(),"street_camera_zero_edges",true);
        var encounters = mem.getBlock("MMU4_36").getStart();
        createLabel(encounters,"encounter_directory",true);
        var pursuit = encounters.add(0x6f8);
        createLabel(pursuit,"pursuit_directory",true);
        reference(toAddr(0x4e24),pursuit);
        var sewer = encounters.add(word(pursuit.add(12)) - 0x8000);
        var link = encounters.add(word(sewer.add(2*0x19)) - 0x8000);
        reference(pursuit.add(12),sewer);
        reference(sewer.add(2*0x19),link);
        createLabel(link,"gas_cylinder_crocodile_entry",true);
        if (word(link)!=0x1861 || word(link.add(2))!=0xdcff)
            throw new IllegalStateException("Unexpected crocodile pursuit link");
        createLabel(mem.getBlock("MMU3_22").getStart().add(0x10b5),"set_ending_flag",true);
        createLabel(mem.getBlock("MMU3_22").getStart().add(0x0f81),"process_type_13_hit",true);
        createLabel(mem.getBlock("MMU3_22").getStart().add(0x1069),"process_final_boss_hit",true);
        createLabel(toAddr(0x4020), "cartridge_dispatch", true);
        createLabel(toAddr(0x4092), "map_startup_windows", true);
        createLabel(toAddr(0x4131), "main_update_loop", true);
        createLabel(toAddr(0x42f0), "call_bank21_service", true);
        createLabel(toAddr(0x42fc), "bank21_service_vectors", true);
        createLabel(toAddr(0x44a9), "write_checkpoint", true);
        createLabel(toAddr(0x44db), "read_checkpoint", true);
        createLabel(toAddr(0x4591), "switch_interaction", true);
        createLabel(toAddr(0x4d2f), "roll_cached_enemy_entry", true);
        createLabel(toAddr(0x4d79), "check_crocodile_entry_position", true);
        createLabel(toAddr(0x4e1c), "lookup_enemy_pursuit_link", true);
        createLabel(toAddr(0x4f79), "run_ending", true);
        createLabel(toAddr(0x6098), "draw_clipped_placement", true);
        createLabel(toAddr(0x6169), "read_graphic_descriptor", true);
        createLabel(toAddr(0x62a6), "advance_elapsed_clock", true);
        createLabel(toAddr(0x6330), "advance_scene_cursor", true);
        createLabel(toAddr(0x6365), "draw_scene_frame", true);
        createLabel(toAddr(0x63c1), "draw_fixed_placement_list", true);
        createLabel(toAddr(0x63ee), "fixed_placement_directory", true);
        createLabel(toAddr(0x52f7), "sample_rom_offsets", true);
        createLabel(toAddr(0x533f), "sample_timer_interrupt", true);
        createLabel(toAddr(0x534e), "write_dac_and_decode_next_nibble", true);
        createLabel(toAddr(0x547d), "start_sample_timer", true);
        createLabel(toAddr(0x5546), "tick_active_score_voices", true);
        createLabel(toAddr(0x5789), "initialize_score_voice_one", true);
        createLabel(toAddr(0x581f), "initialize_score_voice_two", true);
        createLabel(toAddr(0x5954), "disable_score_voices", true);
        createLabel(toAddr(0x469f), "unconfirmed_area_select", true);
        createLabel(toAddr(0x7122), "request_priority_sample", true);
        createLabel(toAddr(0x7152), "pause_sequence_poll", true);
        createLabel(toAddr(0x71eb), "pause_sequence_patterns", true);
        createLabel(scene(0x8b04), "scene_directory", true);
        reference(toAddr(0x633e), scene(0x8b04));
        reference(toAddr(0x6381), scene(0x8b04));
        for (int offset = 0; offset <= 0x2c; offset += 2) {
            var entry = toAddr(0x42fc + offset);
            if (getDataAt(entry) == null) createWord(entry);
            reference(entry, toAddr(word(entry)));
        }
        for (int addr : new int[]{0x4020, 0x4092, 0x4131, 0x6365}) {
            if (getInstructionAt(toAddr(addr)) == null && !disassemble(toAddr(addr)))
                throw new IllegalStateException("Cannot disassemble " + addr);
        }
        int slots = 0;
        int placements = 0;
        Set<Integer> lists = new HashSet<>();
        Set<Address> graphics = new HashSet<>();
        for (int id = 1; id <= 40; id++) {
            var row = scene(0x8b04 + 2*id);
            if (getDataAt(row) == null) createWord(row);
            var sequence = scene(word(row));
            reference(row, sequence);
            createLabel(sequence, String.format("scene_%02d", id), true);
            int last = getByte(sequence) & 0xff;
            for (int n = 0; n <= last; n++) {
                slots++;
                var slot = sequence.add(2 + n*2);
                int pointer = word(slot);
                if (pointer >= 0xff00) continue;
                reference(slot, scene(pointer));
                if (!lists.add(pointer)) continue;
                while ((getByte(scene(pointer)) & 0xff) != 0xff) {
                    var field = scene(pointer + 2);
                    int graphic = word(field);
                    var descriptor = graphic >= 0xa000 ? scene(graphic - 0x2000) : toAddr(graphic);
                    if ((graphic < 0x8000) || !mem.contains(descriptor.add(4)))
                        throw new IllegalStateException("Bad graphic " + graphic);
                    reference(field, descriptor);
                    graphics.add(descriptor);
                    placements++;
                    pointer += 8;
                }
            }
        }
        if (slots != 752 || lists.size() != 156 || placements != 257 || graphics.size() != 75)
            throw new IllegalStateException("Unexpected scene catalog");
        println("RE2 cartridge analysis verified: 40 sequences, 752 slots, 156 lists, " +
                "257 placements, 75 graphics");
    }
}
