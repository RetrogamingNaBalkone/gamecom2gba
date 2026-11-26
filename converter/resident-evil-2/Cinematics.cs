using System;
using System.Collections.Generic;
using System.IO;

internal static class Cinematics
{
    const int SceneStart = 0x60000;
    const int GraphicStart = 0x4a000;

    static int Word(byte[] rom, int offset)
    {
        return rom[offset] * 256 + rom[offset + 1];
    }

    static int SceneAddress(int address)
    {
        if (address < 0x8000 || address >= 0xa000)
            throw new InvalidDataException("Invalid RE2 scene pointer");
        return SceneStart + address - 0x8000;
    }

    sealed class Catalog
    {
        readonly byte[] rom;
        readonly MemoryStream[] sections = new MemoryStream[6];
        readonly BinaryWriter[] write = new BinaryWriter[6];
        readonly Dictionary<int, ushort> lists = new Dictionary<int, ushort>();
        readonly Dictionary<string, ushort> pieces = new Dictionary<string, ushort>();

        internal Catalog(byte[] rom)
        {
            this.rom = rom;
            for (int i = 0; i < sections.Length; ++i) {
                sections[i] = new MemoryStream();
                write[i] = new BinaryWriter(sections[i]);
            }
        }

        ushort Piece(int offset)
        {
            int ptr = Word(rom, offset + 2);
            if (ptr < 0x8000 || ptr > 0xbffb)
                throw new InvalidDataException("Invalid RE2 graphics descriptor");
            int addr = ptr >= 0xa000 ? SceneAddress(ptr - 0x2000) : GraphicStart + ptr - 0x8000;
            int width = rom[addr], height = rom[addr+1];
            int sx = rom[addr+2], sy = rom[addr+3], page = rom[addr+4];
            int destX = rom[offset+4], destY = rom[offset+5];
            int flags = rom[offset+6], palette = rom[offset+7];
            if (rom[offset+1] != 0) {
                sx += width - rom[offset+1];
                width = rom[offset+1];
            }
            if (rom[offset] != 0) {
                sy += height - rom[offset];
                height = rom[offset];
            }
            int visibleWidth = Math.Min(width, 200 - destX);
            int visibleHeight = Math.Min(height, 160 - destY);
            if (visibleWidth <= 0 || visibleHeight <= 0 || (flags & ~0xc1) != 0)
                throw new InvalidDataException("Unsupported RE2 cinematic placement");
            byte[,] pixels = new byte[(visibleHeight+7)&~7, (visibleWidth+7)&~7];
            for (int y = 0; y < visibleHeight; ++y) {
                int v = sy + ((flags & 0x80) != 0 ? visibleHeight - 1 - y : y);
                for (int x = 0; x < visibleWidth; ++x) {
                    int u = sx + ((flags & 0x40) != 0 ? width - 1 - x : x);
                    int raw = Tiles.Pixel(rom, page, u & 255, v & 255) - 1;
                    if (raw != 0 || (flags & 1) == 0)
                        pixels[y,x] = (byte)(1 + ((palette >> (raw * 2)) & 3));
                }
            }
            byte[] tileData;
            using (var tiles = new MemoryStream()) {
                using (var writer = new BinaryWriter(tiles)) Tiles.Write(writer, pixels);
                tileData = tiles.ToArray();
            }
            string key = visibleWidth + "/" + visibleHeight + "/" + Convert.ToBase64String(tileData);
            ushort index;
            if (!pieces.TryGetValue(key, out index)) {
                index = checked((ushort)pieces.Count);
                pieces.Add(key, index);
                write[4].Write(checked((uint)sections[5].Position));
                write[4].Write((ushort)visibleWidth);
                write[4].Write((ushort)visibleHeight);
                write[5].Write(tileData);
            }
            return index;
        }

        ushort PlacementList(int address)
        {
            ushort index;
            if (lists.TryGetValue(address, out index)) return index;
            index = checked((ushort)lists.Count);
            lists.Add(address, index);
            int first = checked((int)sections[3].Position / 4);
            for (int offset = address; rom[offset] != 255; offset += 8) {
                write[3].Write(Piece(offset));
                write[3].Write(rom[offset+4]);
                write[3].Write(rom[offset+5]);
            }
            write[2].Write(checked((ushort)first));
            write[2].Write(checked((ushort)(sections[3].Position / 4 - first)));
            return index;
        }

        internal byte[] Encode()
        {
            write[0].Write(0);
            for (int id = 1; id <= 40; ++id) {
                int scene = SceneAddress(Word(rom, SceneAddress(0x8b04 + id*2)));
                int count = rom[scene] + 1;
                write[0].Write(checked((ushort)(sections[1].Position / 2)));
                write[0].Write(checked((byte)count));
                write[0].Write(rom[scene+1]);
                for (int i = 0; i < count; ++i) {
                    int ptr = Word(rom, scene + 2 + 2*i);
                    write[1].Write(ptr >= 0xff00 ? (ushort)ptr : PlacementList(SceneAddress(ptr)));
                }
            }
            using (var output = new MemoryStream())
            using (var header = new BinaryWriter(output)) {
                header.Write(new byte[] { 67, 73, 78, 49 });
                int offset = 28;
                foreach (var section in sections) {
                    header.Write(offset);
                    offset = checked(offset + (int)section.Length);
                }
                foreach (var section in sections) section.WriteTo(output);
                return output.ToArray();
            }
        }
    }

    internal static byte[] Build(byte[] rom)
    {
        return new Catalog(rom).Encode();
    }
}
