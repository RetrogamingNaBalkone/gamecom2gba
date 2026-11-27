"""Read symbol addresses from the debug ELF that scripts/build.ps1 keeps in build/indy-500."""
import struct
from pathlib import Path

ELF = Path(__file__).resolve().parents[2] / "build/indy-500/runtime.elf"


def symbols(path=ELF):
    d = path.read_bytes()
    shoff = struct.unpack_from("<I", d, 0x20)[0]
    size, count = struct.unpack_from("<HH", d, 0x2e)
    sections = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * size) for i in range(count)]
    result = {}
    for s in sections:
        if s[1] != 2:
            continue
        strtab = sections[s[6]][4]
        for o in range(s[4], s[4] + s[5], 16):
            name, value, sz, info, other, shndx = struct.unpack_from("<IIIBBH", d, o)
            end = d.index(b"\0", strtab + name)
            result[d[strtab + name:end].decode()] = value
    return result
