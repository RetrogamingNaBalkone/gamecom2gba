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

    internal static long WriteOpening(byte[] cart, BinaryWriter output)
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
        output.Write(4*(levels+9));
        output.Write(0);
        output.Write(0);
        output.Write(0);
        long bonusFlagsOffset=output.BaseStream.Position;
        output.Write(0); // bonus map access mask
        output.Write(0);
        output.Write(0); // opening timeline
        output.Write(0); // sound directory
        var backgrounds=new BackgroundAtlas(136);
        var sprites=new TileAtlas();
        for (int level=0; level<levels; level++) Intermission.Write(cart,output,backgrounds,level);
        Intermission.WriteRetry(cart,output,backgrounds);
        Intermission.WriteExitEmblems(cart,output,backgrounds);
        for (int level=0; level<levels; level++) {
            WriteOffset(output,campaign,level);
            WriteRooms(cart,output,level,backgrounds,sprites);
        }
        WriteOffset(output,campaign,levels+3);
        new Actors(cart,sprites).Write(output);
        WriteOffset(output,campaign,levels+2);
        sprites.WriteSprites(output,backgrounds);
        WriteOffset(output,campaign,levels+6);
        Opening.Write(cart,output,backgrounds);
        WriteOffset(output,campaign,levels+7);
        DukeAudio.Write(cart,output);
        WriteOffset(output,campaign,levels+1);
        backgrounds.Write(output);
        WriteOffset(output,campaign,levels+5);
        WriteWeapons(cart,output);
        return bonusFlagsOffset;
    }

    static void WriteOffset(BinaryWriter output,long start,int index)
    {
        WriteDirectoryEntry(output,start,index+1);
    }

    internal static void WriteDirectoryEntry(BinaryWriter output,long start,int index)
    {
        long position=output.BaseStream.Position;
        output.BaseStream.Position=start+4*index;
        output.Write((uint)(position-start));
        output.BaseStream.Position=position;
    }

    static void WriteRooms(byte[] cart,BinaryWriter output,int level,BackgroundAtlas backgrounds,TileAtlas sprites)
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
        // OBJ tiles for the pickup notice displayed by 21:6E29, one row per item.
        for (int type=1;type<=9;type++) {
            int messageSlot=0x21*8192+0xe66+2*(type-1);
            int p=0x21*8192+((cart[messageSlot]*256+cart[messageSlot+1])&8191);
            int length=cart[p++];
            for (int col=0;col<20;col++) {
                var glyph=new byte[8,8];
                if (col<length) for (int row=0;row<8;row++) {
                    int bits=row<7 ? Intermission.GlyphRow((char)cart[p+col],row) : 0;
                    for (int x=0;x<8;x++) glyph[row,x]=(byte)((bits&(32>>x))!=0 ? 1 : 4);
                }
                Tiles.Write(output,glyph);
            }
            output.Write(length);
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
