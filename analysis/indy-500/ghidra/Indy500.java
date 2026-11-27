//@category GameCom
import java.io.ByteArrayInputStream;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.SourceType;

/** Imports the Indy 500 overlay bank, seeds code entry points and exports pseudo-C. */
public class Indy500 extends GhidraScript {
    private MemoryBlock overlay;
    private boolean[] executed81, executed83;

    /** Resolves a 6000-7FFF flow to bank 81 or the bank 83 overlay. */
    private Address resolve(Instruction insn, Address to) {
        long t = to.getOffset();
        if (t < 0x6000 || t >= 0x8000) return to;
        int i = (int) t - 0x6000;
        if (overlay.contains(insn.getAddress())) return executed81[i] && !executed83[i] ? to : bank83((int) t);
        if (insn.getAddress().getOffset() >= 0x6000 && insn.getAddress().getOffset() < 0x8000)
            return executed83[i] && !executed81[i] ? bank83((int) t) : to;
        return executed83[i] && !executed81[i] ? bank83((int) t) : to;
    }

    private AddressSet allowed() {
        AddressSet set = new AddressSet(currentProgram.getMemory());
        set.delete(base(0x6000), base(0x7fff));
        for (int i = 0; i < 0x2000; ++i) if (executed81[i]) set.add(base(0x6000 + i));
        set.delete(base(0x8000), base(0x9fff));
        return set;
    }

    private Address base(int a) { return toAddr(a); }

    private Address bank83(int a) {
        return overlay.getStart().add(a - 0x6000);
    }

    private void seed(Address a, String name) throws Exception {
        if (name != null) createLabel(a, name, true, SourceType.USER_DEFINED);
        if (getInstructionAt(a) == null) {
            DisassembleCommand cmd = new DisassembleCommand(new AddressSet(a), allowed(), true);
            cmd.applyTo(currentProgram, monitor);
        }
        if (getFunctionAt(a) == null) createFunction(a, name);
    }

    private void label(int a, String name) throws Exception {
        createLabel(base(a), name, true, SourceType.USER_DEFINED);
    }

    /** Disassembles every immediate CALL target found in already decoded code. */
    private int followCalls() throws Exception {
        int added = 0;
        AddressSet allowedSet = allowed();
        List<Address> targets = new ArrayList<>();
        for (Instruction insn : currentProgram.getListing().getInstructions(true)) {
            String m = insn.getMnemonicString();
            if (!m.equals("CALL") && !m.equals("JMP") && !m.equals("BR")) continue;
            for (Address to : insn.getFlows()) {
                if (to.getOffset() < 0x4000 || to.getOffset() >= 0xa000) continue;
                // A flow from the overlay into 6000-7FFF stays in the overlay.
                Address dest = resolve(insn, to);
                if (!dest.equals(to)) {
                    insn.addOperandReference(0, dest, m.equals("CALL") ? RefType.CALL_OVERRIDE_UNCONDITIONAL
                        : RefType.JUMP_OVERRIDE_UNCONDITIONAL, SourceType.USER_DEFINED);
                }
                if (!allowedSet.contains(dest)) continue;
                if (getInstructionAt(dest) == null) targets.add(dest);
                if (m.equals("CALL") && getFunctionAt(dest) == null) targets.add(dest);
            }
        }
        for (Address t : targets) {
            if (getInstructionAt(t) == null) {
                new DisassembleCommand(t, allowedSet, true).applyTo(currentProgram, monitor);
                added++;
            }
        }
        for (Instruction insn : currentProgram.getListing().getInstructions(true)) {
            if (!insn.getMnemonicString().equals("CALL")) continue;
            for (Address to : insn.getFlows()) {
                Address dest = resolve(insn, to);
                if (getInstructionAt(dest) != null && getFunctionAt(dest) == null) {
                    createFunction(dest, null);
                    added++;
                }
            }
        }
        return added;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        var mem = currentProgram.getMemory();
        overlay = mem.getBlock("BANK83");
        if (overlay == null) {
            byte[] page = Files.readAllBytes(Path.of(args[0]));
            overlay = mem.createInitializedBlock("BANK83", toAddr(0x6000),
                new ByteArrayInputStream(page), page.length, monitor, true);
        }
        String[][] io = {
            {"10","IE0"},{"11","IE1"},{"12","IR0"},{"13","IR1"},{"14","P0"},{"15","P1"},{"16","P2"},{"17","P3"},
            {"19","SYS"},{"1a","CKC"},{"1c","SPH"},{"1d","SPL"},{"1e","PS0"},{"1f","PS1"},
            {"24","MMU0"},{"25","MMU1"},{"26","MMU2"},{"27","MMU3"},{"28","MMU4"},
            {"30","LCDC"},{"31","LCH"},{"32","LCV"},{"34","DMC"},{"35","DMX1"},{"36","DMY1"},{"37","DMDX"},
            {"38","DMDY"},{"39","DMX2"},{"3a","DMY2"},{"3b","DMPL"},{"3c","DMBR"},{"3d","DMVP"},
            {"40","SGC"},{"42","SG0L"},{"46","SG1L"},{"48","SG0TH"},{"49","SG0TL"},{"4a","SG1TH"},{"4b","SG1TL"},
            {"4c","SG2L"},{"4e","SG2TH"},{"4f","SG2TL"},{"50","SGDA"},
            {"58","TM0C"},{"59","TM0D"},{"5a","TM1C"},{"5b","TM1D"},{"5c","CLKT"},{"5e","WDT"},{"5f","WDTC"},
        };
        for (String[] r : io) label(Integer.parseInt(r[0], 16), r[1]);

        // Coverage: cartridge offsets 0000-1FFF (bank 80 at 4000), 2000-3FFF (bank 81
        // at 6000) and 6000-7FFF (bank 83 overlay at 6000) where Tigerbyte executed code.
        executed81 = new boolean[0x2000];
        executed83 = new boolean[0x2000];
        List<Address> starts = new ArrayList<>();
        for (String line : Files.readAllLines(Path.of(args[2]))) {
            if (line.isBlank()) continue;
            int off = Integer.parseInt(line.trim(), 16);
            if (off < 0x2000) starts.add(base(0x4000 + off));
            else if (off < 0x4000) { executed81[off - 0x2000] = true; starts.add(base(0x6000 + off - 0x2000)); }
            else if (off >= 0x6000 && off < 0x8000) { executed83[off - 0x6000] = true; starts.add(bank83(off)); }
        }
        println("coverage seeds: " + starts.size());
        AddressSet seeds = new AddressSet();
        for (Address a : starts) if (getInstructionAt(a) == null) seeds.add(a);
        DisassembleCommand all = new DisassembleCommand(seeds, allowed(), true);
        all.applyTo(currentProgram, monitor);
        seed(base(0x4020), "cartridge_entry");
        seed(bank83(0x7938), "car_unlock_code");
        seed(bank83(0x7904), "check_unlock_touches");
        seed(bank83(0x68f1), "draw_super_secret_area");
        for (int pass = 0; pass < 12; ++pass) {
            int n = followCalls();
            println("pass " + pass + ": " + n + " new entries");
            if (n == 0) break;
        }

        Path out = Path.of(args[1]);
        Files.createDirectories(out);
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        int count = 0;
        try (PrintWriter base = new PrintWriter(Files.newBufferedWriter(out.resolve("base.c")));
             PrintWriter bank = new PrintWriter(Files.newBufferedWriter(out.resolve("bank83.c")))) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                PrintWriter w = overlay.contains(f.getEntryPoint()) ? bank : base;
                DecompileResults r = dec.decompileFunction(f, 60, monitor);
                w.println("// ==== " + f.getEntryPoint() + " " + f.getName());
                if (r != null && r.decompileCompleted()) w.println(r.getDecompiledFunction().getC());
                else w.println("// decompilation failed: " + (r == null ? "" : r.getErrorMessage()));
                count++;
            }
        }
        try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(out.resolve("listing.txt")))) {
            for (Instruction insn : currentProgram.getListing().getInstructions(true)) {
                Function f = getFunctionContaining(insn.getAddress());
                var sym = getSymbolAt(insn.getAddress());
                if (sym != null) w.println(sym.getName() + ":");
                w.printf("  %s  %-28s%n", insn.getAddress(), insn.toString());
            }
        }
        println("Indy 500 analysis exported: " + count + " functions");
    }
}
