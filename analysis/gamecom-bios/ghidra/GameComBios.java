//@category GameCom
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.SourceType;

/** Seeds the interrupt vectors, reset entry and cartridge service dispatcher, then exports pseudo-C. */
public class GameComBios extends GhidraScript {
    private void seed(int a, String name) throws Exception {
        Address at = toAddr(a);
        if (name != null) createLabel(at, name, true, SourceType.USER_DEFINED);
        if (getInstructionAt(at) == null)
            new DisassembleCommand(new AddressSet(at), new AddressSet(currentProgram.getMemory()), true)
                .applyTo(currentProgram, monitor);
        if (getFunctionAt(at) == null) createFunction(at, name);
    }

    private int word(int a) throws Exception {
        return ((getByte(toAddr(a)) & 0xff) << 8) | (getByte(toAddr(a + 1)) & 0xff);
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String[] vectors = {"dma", "timer0", "unused04", "external", "uart", "unused0a", "unused0c", "lcdc",
                            "unused10", "timer1", "unused14", "clock", "unused18", "pio", "watchdog", "nmi"};
        for (int i = 0; i < 16; ++i) {
            int target = word(0x1000 + 2 * i);
            if (target >= 0x1000 && target < 0x4000) seed(target, "irq_" + vectors[i]);
        }
        seed(0x1020, "reset");
        seed(0x20f1, "cartridge_service");
        seed(0x2142, "irq_return_2142");
        seed(0x22fa, "kernel_22fa");
        // Follow every immediate call target until no new code appears.
        for (int pass = 0; pass < 10; ++pass) {
            int added = 0;
            for (Instruction insn : currentProgram.getListing().getInstructions(true)) {
                if (!insn.getMnemonicString().equals("CALL") && !insn.getMnemonicString().equals("CALS")) continue;
                for (Address to : insn.getFlows()) {
                    if (to.getOffset() < 0x1000 || to.getOffset() >= 0x4000) continue;
                    if (getFunctionAt(to) == null) { seed((int) to.getOffset(), null); added++; }
                }
            }
            if (added == 0) break;
        }
        Path out = Path.of(args[1]);
        Files.createDirectories(out);
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        int count = 0;
        try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(out.resolve("kernel.c")))) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                DecompileResults r = dec.decompileFunction(f, 60, monitor);
                w.println("// ==== " + f.getEntryPoint() + " " + f.getName());
                if (r != null && r.decompileCompleted()) w.println(r.getDecompiledFunction().getC());
                count++;
            }
        }
        try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(out.resolve("listing.txt")))) {
            for (Instruction insn : currentProgram.getListing().getInstructions(true)) {
                var sym = getSymbolAt(insn.getAddress());
                if (sym != null) w.println(sym.getName() + ":");
                w.printf("  %s  %s%n", insn.getAddress(), insn.toString());
            }
        }
        println("game.com BIOS analysis exported: " + count + " functions");
    }
}
