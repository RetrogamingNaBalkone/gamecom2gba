// Analyze the cartridge view after the startup dispatch maps banks 21 and 26.
//@category GameCom
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;

public class DukeEntry extends GhidraScript {
    @Override
    public void run() throws Exception {
        // The room-selection code and title renderer index the same 3x5 stages.
        // The data labels live only in the user's local analysis project.
        createLabel(toAddr(0x7597), "stage_title_table", true);
        for (int stage=0; stage<15; stage++) {
            var slot=toAddr(0x7597+2*stage);
            int address=currentProgram.getMemory().getShort(slot)&0xffff;
            var title=toAddr(address);
            int first=getByte(title)&0xff;
            int second=getByte(title.add(first+1))&0xff;
            if (address<0x75b5 || address+2+first+second>0x7676 || first==0 || second==0)
                throw new IllegalStateException("Invalid stage title at " + slot);
            createLabel(title,"title_episode_"+(1+stage/5)+"_map_"+(1+stage%5),true);
        }
        createLabel(toAddr(0x6a18), "advance_campaign", true);
        createLabel(toAddr(0x67cb), "junction_left", true);
        createLabel(toAddr(0x681c), "junction_right", true);
        DecompInterface decompiler = new DecompInterface();
        try {
            if (!decompiler.openProgram(currentProgram))
                throw new IllegalStateException(decompiler.getLastMessage());
            int[] entries = {0x65b9, 0x65c6, 0x65cd, 0x4020};
            String[] names = {"read_rect", "set_rect_registers", "read_scene_piece", "cartridge_dispatch"};
            for (int i = 0; i < entries.length; i++) {
                var entry = toAddr(entries[i]);
                if (getInstructionAt(entry) == null && !disassemble(entry))
                    throw new IllegalStateException("Cannot decode " + entry);
                Function fn = getFunctionAt(entry);
                if (fn == null) fn = createFunction(entry, names[i]);
                if (fn == null) throw new IllegalStateException("Cannot define " + entry);
                DecompileResults result = decompiler.decompileFunction(fn, 30, monitor);
                if (!result.decompileCompleted()) throw new IllegalStateException(result.getErrorMessage());
                println(result.getDecompiledFunction().getC());
            }
        } finally {
            decompiler.dispose();
        }
    }
}
