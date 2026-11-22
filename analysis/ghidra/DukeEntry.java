// Analyze the cartridge view after the startup dispatch maps banks 21 and 26.
//@category GameCom
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;

public class DukeEntry extends GhidraScript {
    @Override
    public void run() throws Exception {
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
