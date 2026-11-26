// Label the Web Link serial command table in the startup MMU view.
//@category GameCom
import ghidra.app.script.GhidraScript;

public class WebLink extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] names = {
            "open_link", "close_link", "read_all_records", "read_one_record",
            "overwrite_record", "write_text_message"
        };
        int[] entries = {0x61f7, 0x61fc, 0x621d, 0x6243, 0x62b6, 0x6316};
        var memory = currentProgram.getMemory();
        createLabel(toAddr(0x6370), "command_handlers", true);
        createLabel(toAddr(0x637c), "command_status_strings", true);
        for (int i = 0; i < names.length; ++i) {
            var entry = toAddr(entries[i]);
            int target = memory.getShort(toAddr(0x6370 + 2 * i)) & 0xffff;
            int status = memory.getShort(toAddr(0x637c + 2 * i)) & 0xffff;
            if (target != entries[i] || status != 0x6388 + 21 * i)
                throw new IllegalStateException("Unexpected Web Link table entry: " + i);
            createLabel(entry, names[i], true);
            createLabel(toAddr(status), names[i] + "_status", true);
            disassemble(entry);
            println(String.format("%02x %04x %04x %s", 2 * i + 1, target, status, names[i]));
        }
        createLabel(toAddr(0x60c5), "find_game_record", true);
        createLabel(toAddr(0x6285), "send_matching_record", true);
        println("Web Link command table verified");
    }
}
