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

    internal static int ReadBonusFlags(byte[] cart,string path)
    {
        using (var input=File.OpenRead(path)) {
            if (input.Length!=0x2b && input.Length!=0x2000)
                throw new InvalidDataException("Expected a 43-byte Duke record or an 8192-byte game.com NVRAM dump.");
            byte[] bytes;
            using (var reader=new BinaryReader(input)) bytes=reader.ReadBytes((int)input.Length);
            // The cartridge initializes the name and record version at 21:6F18.
            int record=-1;
            for (int p=0;p<=bytes.Length-0x2b;p++) {
                int i=0;
                while (i<12 && bytes[p+i]==cart[0x21*8192+(0x6f18&8191)+i]) i++;
                if (i!=12) continue;
                if (record>=0) throw new InvalidDataException("Multiple Duke records in NVRAM.");
                record=p;
            }
            if (record<0) throw new InvalidDataException("Duke system record not found.");
            int tag=bytes[record+0x15], flags=bytes[record+0x16];
            if (((tag+flags)&255)!=bytes[record+0x17])
                throw new InvalidDataException("Invalid bonus record checksum.");
            return flags&7;
        }
    }

    internal static void WriteOpening(byte[] cart, BinaryWriter output,int bonusFlags)
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

        long campaign=output.BaseStream.Position;
        const int levels=15;
        output.Write(levels);
        for (int level=0; level<levels; level++) output.Write(0);
        output.Write(4*(levels+7));
        output.Write(0);
        output.Write(0);
        output.Write(0);
        output.Write(bonusFlags);
        output.Write(0);
        var backgrounds=new TileAtlas();
        var sprites=new TileAtlas();
        for (int level=0; level<levels; level++) Intermission.Write(cart,output,level);
        Intermission.WriteRetry(cart,output);
        for (int level=0; level<levels; level++) {
            WriteOffset(output,campaign,level);
            WriteRooms(cart,output,level,backgrounds,sprites);
        }
        WriteOffset(output,campaign,levels+3);
        new Actors(cart,sprites).Write(output);
        WriteOffset(output,campaign,levels+1);
        backgrounds.Write(output);
        WriteOffset(output,campaign,levels+2);
        sprites.Write(output);
        WriteOffset(output,campaign,levels+5);
        WriteWeapons(cart,output);
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

    static void WriteWeapons(byte[] cart,BinaryWriter output)
    {
        long start=output.BaseStream.Position;
        const int groups=16, slots=6;
        for (int i=0;i<groups*slots;i++) output.Write(0);
        for (int kind=0;kind<4;kind++) {
            int entry=0x21*8192+(0x6f52&8191)+2*kind;
            int p=0x21*8192+((cart[entry]*256+cart[entry+1])&8191);
            for (int col=0;col<8;col++) {
                char letter=cart[p]==0 ? ' ' : (char)cart[p++];
                for (int row=0;row<7;row++) output.Write((byte)Intermission.GlyphRow(letter,row));
            }
        }
        var frames=new System.Collections.Generic.Dictionary<int,int>();
        int table=0x21*8192+(0x712b&8191);
        for (int group=0;group<groups;group++) {
            int last=0;
            for (int phase=0;phase<slots;phase++) {
                if (cart[table]!=255) {
                    last=cart[table]*256+cart[table+1];
                    table+=2;
                }
                int offset;
                if (!frames.TryGetValue(last,out offset)) {
                    offset=(int)(output.BaseStream.Position-start);
                    frames.Add(last,offset);
                    if (last<0x8000) output.Write(0); // empty hand or unused phase
                    else WriteWeaponFrame(cart,output,Offset(0x26,last));
                }
                long end=output.BaseStream.Position;
                output.BaseStream.Position=start+4*(group*slots+phase);
                output.Write(offset);
                output.BaseStream.Position=end;
            }
            if (cart[table++]!=255) throw new InvalidDataException("Invalid weapon sequence.");
        }
    }

    static void WriteWeaponFrame(byte[] cart,BinaryWriter output,int p)
    {
        int width=cart[p],height=cart[p+1],page=cart[p+4];
        output.Write((byte)width);
        output.Write((byte)height);
        output.Write((ushort)0);
        for (int top=0;top<height;top+=32)
            for (int left=0;left<width;left+=32) {
                var tile=new byte[32,32];
                for (int y=0;y<32 && top+y<height;y++)
                    for (int x=0;x<32 && left+x<width;x++) {
                        int sx=(cart[p+2]+left+x)&255,sy=(cart[p+3]+top+y)&255;
                        byte ink=Tiles.Pixel(cart,page,sx,sy);
                        tile[y,x]=Tiles.Pixel(cart,page+1,sx,sy)!=1 ? (byte)1 : ink==1 ? (byte)0 : ink;
                    }
                Tiles.Write(output,tile);
            }
    }
}
