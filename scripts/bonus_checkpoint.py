"""Prepare a separate game.com NVRAM image for Duke's Options / Password menu."""
import argparse
from pathlib import Path


STAGES = {"launching-facility": 0x15, "lunatic-fringe": 0x25, "freeway": 0x35}


def checkpoint(nvram, stage):
    marker = b"DUKE NUKEM \x01"
    start = nvram.find(marker)
    if (len(nvram) != 8192 or start < 0 or start + 43 > len(nvram)
            or nvram.count(marker) != 1 or stage not in STAGES.values()):
        raise ValueError("Expected an 8192-byte game.com NVRAM containing one Duke record.")
    result = bytearray(nvram)
    # 21:6D5D lists the 18 variables restored by the cartridge. Four 1s
    # correspond to the first password letter. Start with the normal pistol.
    saved = bytes([1, 1, 1, 1, stage, 0, 0x10, 0, 4, 8, 0, 0, 0, 0, 1, 0, 0, 0])
    result[start + 24:start + 43] = saved + bytes([sum(saved) & 255])
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("nvram", type=Path)
    parser.add_argument("stage", choices=STAGES)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        image = checkpoint(args.nvram.read_bytes(), STAGES[args.stage])
        with args.output.open("xb") as output:
            output.write(image)
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")
    print("Select Options / Password, enter AAAA, then press C.")
