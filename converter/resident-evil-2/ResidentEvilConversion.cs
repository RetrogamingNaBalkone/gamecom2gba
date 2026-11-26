using System;
using System.IO;
using System.Reflection;

internal static class ResidentEvilConversion
{
    // CPU bank 21:63EE contains the four title/menu display lists.
    static void WriteTitle(byte[] cart, BinaryWriter output, int index)
    {
        int bank = 0x42000 - 0x6000;
        int list = bank + ReadWord(cart, bank + 0x63ee + index * 2);
        byte[,] scene = new byte[160,200];
        for (int y = 0; y < 160; ++y)
            for (int x = 0; x < 200; ++x) scene[y,x] = 4;
        while (cart[list] != 255)
        {
            int address = ReadWord(cart, list + 2);
            int part = address < 0xa000 ? 0x4a000 + address - 0x8000 : 0x60000 + address - 0xa000;
            int width = cart[part], height = cart[part + 1];
            int left = cart[list + 4], top = cart[list + 5];
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    int shade = Tiles.Pixel(cart, cart[part + 4], cart[part + 2] + x, cart[part + 3] + y) - 1;
                    if (cart[list + 6] == 0 || shade != 0)
                        scene[top + y,left + x] = (byte)(1 + ((cart[list + 7] >> (2 * shade)) & 3));
                }
            list += 8;
        }
        var tiles = new TileAtlas();
        var map = new ushort[500];
        for (int i = 0; i < map.Length; ++i)
        {
            byte[] tile = new byte[32];
            for (int row = 0; row < 8; ++row)
                for (int pair = 0; pair < 4; ++pair)
                {
                    int y = i / 25 * 8 + row, x = i % 25 * 8 + pair * 2;
                    tile[row * 4 + pair] = (byte)(scene[y,x] | scene[y,x+1] << 4);
                }
            map[i] = (ushort)tiles.Add(tile);
        }
        int count = 0;
        foreach (var tile in map) count = Math.Max(count, tile + 1);
        output.Write(count);
        foreach (var tile in map) output.Write(tile);
        tiles.Write(output);
    }

    static void WriteAudio(byte[] cart, BinaryWriter output)
    {
        var file = output.BaseStream;
        long start = file.Position;
        for (int i = 0; i < 24; ++i) output.Write(0);
        for (int i = 0; i < 24; ++i)
        {
            long next = file.Position;
            file.Position = start + 4*i;
            output.Write((uint)(next-start));
            file.Position = next;
            int p = 0x412f7 + 3*i;
            // The ISR starts with index 3F. It consumes the stated byte count
            // after skipping the header, including six nibbles of the next block.
            GameComAudio.WriteSample(cart, ReadWord(cart,p)*256 + cart[p+2], output,0x3f,3);
        }
    }

    static int ReadWord(byte[] data, int offset)
    {
        return data[offset] * 256 + data[offset + 1];
    }

    // 20:45CF draws one of two objects over camera 06:19. Each source
    // descriptor is in the fixed 20 bank. The larger picture spans two OBJ.
    static void WriteSwitch(byte[] cart,BinaryWriter output)
    {
        foreach (int record in new[] {0x405fc,0x40603}) {
            int desc=0x40000+(ReadWord(cart,record)&8191);
            int width=cart[record+4],height=cart[record+5];
            for (int left=0;left<width;left+=64) {
                var pixels=new byte[64,64];
                for (int y=0;y<height;++y)
                    for (int x=left;x<Math.Min(width,left+64);++x) {
                        int raw=Tiles.Pixel(cart,cart[desc+5],(cart[desc+3]+x)&255,(cart[desc+4]+y)&255)-1;
                        if (raw!=0) pixels[y,x-left]=(byte)(1+((0xe0>>(2*raw))&3));
                    }
                Tiles.Write(output,pixels);
            }
        }
    }

    internal static void Write(byte[] cart, string directory, string input, byte[] systemBios)
    {
        string path = Path.Combine(directory, "resident-evil-2.gba");
        if (String.Equals(input,path,StringComparison.OrdinalIgnoreCase))
            throw new IOException("Input and output must be different files.");
        string temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var file = new FileStream(temp, FileMode.CreateNew, FileAccess.Write))
            using (var output = new BinaryWriter(file))
            {
                using (var runtime = Assembly.GetExecutingAssembly().GetManifestResourceStream("resident-evil-2"))
                    runtime.CopyTo(file);
                long header = file.Position;
                uint[] offsets = new uint[11];
                foreach (var offset in offsets) output.Write(offset);
                offsets[0] = (uint)(file.Position - header);
                Tiles.Palette(output, 0, 0xdfff8f, 0x6f8f4f, 0x0f4f2f, 0, 0xb3b4af, 0x292a2a);
                var bezel = new byte[160,240];
                Bezel.Draw(bezel);
                Tiles.Write(output,bezel);
                for (int i = 0; i < 4; ++i)
                {
                    offsets[1+i] = (uint)(file.Position - header);
                    WriteTitle(cart, output, i);
                }
                offsets[5] = (uint)(file.Position - header);
                output.Write(Cinematics.Build(cart));
                while ((file.Position & 3) != 0) output.Write((byte)0);
                offsets[6] = (uint)(file.Position - header);
                StreetAssets.Write(cart, output);
                while ((file.Position & 3) != 0) output.Write((byte)0);
                offsets[7] = (uint)(file.Position - header);
                Inventory.Write(cart, output,systemBios);
                while ((file.Position & 3) != 0) output.Write((byte)0);
                offsets[8] = (uint)(file.Position - header);
                WriteAudio(cart, output);
                while ((file.Position & 3) != 0) output.Write((byte)0);
                offsets[9] = (uint)(file.Position - header);
                Campaign.Write(cart, output);
                while ((file.Position & 3) != 0) output.Write((byte)0);
                offsets[10]=(uint)(file.Position-header);
                WriteSwitch(cart,output);
                file.Position = header;
                foreach (var offset in offsets) output.Write(offset);
            }
            Program.Publish(temp,path);
            Console.WriteLine(path);
        }
        finally { File.Delete(temp); }
    }
}
