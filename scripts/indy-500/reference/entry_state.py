"""Write gba/indy-500/entry_state.h from a Tigerbyte capture of the cartridge launch.

    py scripts/indy-500/reference/capture.py TAPE OUT --coverage   (with INDY_ENTRY_FRAME=1500)
    py scripts/indy-500/reference/entry_state.py OUT/entry-state.bin

The snapshot is taken when the BIOS calls 80:4020 with request 0 after the cartridge
icon was touched. The .phase file next to it holds the emulator timing state.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def main():
    path = Path(sys.argv[1])
    d = path.read_bytes()
    ram, ext, reg, tail = d[:4096], d[4096:12288], d[12288:12544], d[-8:]
    phase = {}
    for line in Path(str(path) + ".phase").read_text().splitlines():
        key, *values = line.split()
        phase[key] = [int(v) for v in values]
    out = ["/* State the US game.com BIOS establishes when it starts the cartridge (request 0",
           "   to 80:4020 after the cartridge icon is touched). Captured in Tigerbyte and written",
           "   by scripts/indy-500/reference/entry_state.py; see docs/indy-500/cartridge.md. */",
           "#include <stdint.h>",
           "typedef struct { uint16_t address; uint8_t value; } entry_byte_t;",
           "static const entry_byte_t entry_ram[] = {"]
    skip = {0x19, 0x1c, 0x1d, 0x1e, 0x1f}
    items = [(a, ram[a]) for a in range(0x10, 0x1000) if ram[a] and a not in skip]
    items += [(0xe000 + a, ext[a]) for a in range(0x2000) if ext[a]]
    line = ""
    for a, v in items:
        line += "{0x%04x,0x%02x}," % (a, v)
        if len(line) > 100:
            out.append("    " + line)
            line = ""
    if line:
        out.append("    " + line)
    out.append("};")
    out.append("static const entry_byte_t entry_registers[] = {")
    out.append("    " + "".join("{0x%02x,0x%02x}," % (i, v) for i, v in enumerate(reg) if v))
    out.append("};")
    t0, t1 = phase["timer0"], phase["timer1"]
    pending = (phase["irq"][0] >> 1) & 0xff     # Tigerbyte numbers lines from ILL = 0
    out.append("enum { ENTRY_PC = 0x%02x%02x, ENTRY_SP = 0x%02x%02x, ENTRY_PS0 = 0x%02x, ENTRY_PS1 = 0x%02x,"
               % (tail[0], tail[1], tail[2], tail[3], tail[4], tail[5]))
    out.append("       ENTRY_SYS = 0x%02x, ENTRY_FRAME = %d, ENTRY_CYCLE = %d, ENTRY_CLOCK = %d,"
               % (ram[0x19], phase["frame"][0], phase["cycle"][0], phase["clock"][0]))
    out.append("       ENTRY_TIMER0_RELOAD = %d, ENTRY_TIMER0_COUNT = %d, ENTRY_TIMER1_RELOAD = %d, ENTRY_TIMER1_COUNT = %d,"
               % (t0[1], t0[3], t1[1], t1[3]))
    out.append("       ENTRY_DMA_BUSY = %d, ENTRY_PENDING = 0x%02x };" % (phase["dma"][0], pending))
    target = ROOT / "gba/indy-500/entry_state.h"
    target.write_text("\n".join(out) + "\n")
    print(target, len(items), "bytes")


if __name__ == "__main__":
    main()
