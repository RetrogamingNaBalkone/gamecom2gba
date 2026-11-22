using System.IO;

internal static class Duke
{
    // Cartridge pointers in the fourth MMU window are big-endian.
    static int Offset(int bank, int address) { return bank * 8192 + address - 0x8000; }
    static int Pointer(byte[] cart, int bank, int address)
    {
        int p = Offset(bank, address);
        return cart[p] * 256 + cart[p+1];
    }

    static void Draw(byte[] cart, byte[,] pixels, int x, int y, int page,
                     int sx, int sy, int width, int height, bool transparent)
    {
        for (int row = 0; row < height; row++)
            for (int col = 0; col < width; col++)
            {
                byte ink = Tiles.Pixel(cart, page, sx + col, sy + row);
                if (!transparent || ink != 1) pixels[y+row, x+col] = ink;
            }
    }

    static void DrawObject(byte[] cart, byte[,] screen, int descriptor, int x, int y, bool transparent)
    {
        int p = Offset(0x26, descriptor);
        Draw(cart, screen, 20+x, y, cart[p+4], cart[p+2], cart[p+3], cart[p], cart[p+1], transparent);
    }

    internal static void WriteOpening(byte[] cart, BinaryWriter output)
    {
        var screen = new byte[160,240];
        Bezel.Draw(screen);
        int list = Pointer(cart, 0x24, 0x82c4);
        bool last;
        do {
            int p = Offset(0x24, list);
            Draw(cart, screen, 20+cart[p], cart[p+1], cart[p+6], cart[p+2], cart[p+3],
                 cart[p+4], cart[p+5], false);
            last = (cart[p+7] & 0x80) != 0;
            list += 8;
        } while (!last);

        DrawObject(cart, screen, 0x8bfe, 171, 64, true);
        DrawObject(cart, screen, 0x837a, 34, 13, true);
        DrawObject(cart, screen, 0x8217, 0, 136, false);
        Tiles.Write(output, screen);

        // The pistol is a separate hardware object. Its paired ROM page paints
        // light ink into otherwise transparent pixels on the original LCD.
        foreach (int descriptor in new[] {0x86c2,0x86b3,0x869f,0x86a4,0x86ae,0x86b8})
            WriteWeapon(cart, output, descriptor);
        long campaign=output.BaseStream.Position;
        const int levels=15;
        output.Write(levels);
        for (int level=0; level<levels; level++) output.Write(0);
        output.Write(4*(levels+4));
        output.Write(0);
        output.Write(0);
        var backgrounds=new TileAtlas();
        var sprites=new TileAtlas();
        for (int level=0; level<levels; level++) Intermission.Write(cart,output,level);
        for (int level=0; level<levels; level++) {
            WriteOffset(output,campaign,level);
            WriteRooms(cart,output,level,backgrounds,sprites);
        }
        WriteOffset(output,campaign,levels+1);
        backgrounds.Write(output);
        WriteOffset(output,campaign,levels+2);
        sprites.Write(output);
    }

    static void WriteOffset(BinaryWriter output,long start,int index)
    {
        long position=output.BaseStream.Position;
        output.BaseStream.Position=start+4*(index+1);
        output.Write((uint)(position-start));
        output.BaseStream.Position=position;
    }

    static void WriteRooms(byte[] cart,BinaryWriter output,int level,TileAtlas backgrounds,TileAtlas sprites)
    {
        long rooms=output.BaseStream.Position;
        int entry=0x8000+2*level;
        int tableEnd=level==14 ? Pointer(cart,0x27,Pointer(cart,0x27,0x8000)) : Pointer(cart,0x27,entry+2);
        int count=(tableEnd-Pointer(cart,0x27,entry))/2;
        output.Write(count);
        for (int i=0; i<count; i++) output.Write(0);
        for (int i=0; i<count; i++) {
            WriteOffset(output,rooms,i);
            try {
                new Street(cart,level,i,backgrounds,sprites).Write(output);
            } catch (System.IndexOutOfRangeException e) {
                throw new InvalidDataException("Invalid scene data for stage "+(level+1)+", room "+i,e);
            }
        }
    }

    static void WriteWeapon(byte[] cart, BinaryWriter output, int descriptor)
    {
        var pistol = new byte[64,64];
        int weapon = Offset(0x26, descriptor);
        int left = 32 - (cart[weapon] + 1) / 2;
        int top = 64 - cart[weapon+1];
        for (int y = 0; y < cart[weapon+1]; y++)
            for (int x = 0; x < cart[weapon]; x++)
            {
                int sx = cart[weapon+2] + x, sy = cart[weapon+3] + y;
                byte ink = Tiles.Pixel(cart, cart[weapon+4], sx, sy);
                bool light = Tiles.Pixel(cart, cart[weapon+4]+1, sx, sy) != 1;
                pistol[top+y,left+x] = light ? (byte)1 : ink == 1 ? (byte)0 : ink;
            }
        Tiles.Write(output, pistol);
    }
}
