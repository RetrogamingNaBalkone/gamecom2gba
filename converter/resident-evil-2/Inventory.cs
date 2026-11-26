using System;
using System.Collections.Generic;
using System.IO;

internal static class Inventory
{
    // Display-list rectangles are expressed in screen coordinates, after the
    // game.com DMA engine's X/Y transpose. Zero is transparent on the GBA.
    static byte[,] Cut(byte[] cart, int bank, int sx, int sy, int width, int height,
                       int palette, bool opaque)
    {
        var result = new byte[height,width];
        for (int y=0; y<height; ++y)
            for (int x=0; x<width; ++x) {
                int c=Tiles.Pixel(cart,bank,sx+x,sy+y)-1;
                if (c!=0 || opaque) result[y,x]=(byte)(1+((palette>>(2*c))&3));
            }
        return result;
    }

    static byte[] TileImage(byte[,] image)
    {
        using (var stream=new MemoryStream())
        using (var writer=new BinaryWriter(stream)) {
            Tiles.Write(writer,image);
            return stream.ToArray();
        }
    }

    static byte[] Patch(byte[] cart, int bank, int sx, int sy, int w, int h, int pal, bool opaque)
    {
        var image=Cut(cart,bank,sx,sy,w,h,pal,opaque);
        var bytes=new byte[4+w*h];
        bytes[0]=(byte)w; bytes[2]=(byte)h;
        Buffer.BlockCopy(image,0,bytes,4,w*h);
        return bytes;
    }

    static void Stamp(byte[,] image, byte[,] part, int left, int top)
    {
        for (int y=0; y<part.GetLength(0) && top+y<image.GetLength(0); ++y)
            for (int x=0; x<part.GetLength(1) && left+x<image.GetLength(1); ++x)
                if (part[y,x]!=0) image[top+y,left+x]=part[y,x];
    }

    static byte[,] Solid(int h, int w, byte color)
    {
        var image=new byte[h,w];
        for (int y=0;y<h;++y) for (int x=0;x<w;++x) image[y,x]=color;
        return image;
    }

    static int Pointer(byte[] cart,int bank,int address)
    {
        return bank*8192+((cart[address]*256+cart[address+1])&8191);
    }

    // 24:794A selects a map by camera range, then draws the camera's marker.
    static byte[] Maps(byte[] cart)
    {
        int[] counts={33,76,41,43,112,78,37};
        var layouts=new Dictionary<int,int>();
        using (var s=new MemoryStream()) using (var w=new BinaryWriter(s)) {
            int table=28;
            foreach(int n in counts) { w.Write(table); table+=8*n; }
            s.SetLength(table);
            int row=28;
            for(int area=0;area<counts.Length;++area) {
                int markers=Pointer(cart,0x2d,0x5bbeb+2*area);
                int range=Pointer(cart,0x34,0x68000+2*area);
                for(int camera=0;camera<counts[area];++camera,row+=8) {
                    while(camera>cart[range]) range+=3;
                    int p=Pointer(cart,0x34,range+1),pos;
                    if(!layouts.TryGetValue(p,out pos)) {
                        pos=(int)s.Length; layouts.Add(p,pos);
                        var image=Solid(160,200,4);
                        int n=cart[p++];
                        for(int i=0;i<n;++i,p+=5) {
                            int desc=Pointer(cart,0x34,0x68b47+2*(cart[p]-1));
                            int flags=cart[desc+5];
                            var patch=Cut(cart,cart[desc+4],cart[desc+2],cart[desc+3],
                                          cart[p+3],cart[p+4],flags==3?0x24:0xe4,(flags&1)==0);
                            Stamp(image,patch,cart[p+1],cart[p+2]);
                        }
                        byte[] tileBytes=TileImage(image);
                        var atlas=new TileAtlas(); ushort[] map=new ushort[500]; int unique=0;
                        for(int i=0;i<map.Length;++i) {
                            var tile=new byte[32]; Buffer.BlockCopy(tileBytes,i*32,tile,0,32);
                            map[i]=(ushort)atlas.Add(tile); unique=Math.Max(unique,map[i]+1);
                        }
                        s.Position=pos; w.Write(unique); foreach(ushort t in map) w.Write(t); atlas.Write(w);
                    }
                    s.Position=row; w.Write(pos); w.Write(cart[markers+2*camera]);
                    w.Write(cart[markers+2*camera+1]); w.Write((ushort)0);
                }
            }
            return s.ToArray();
        }
    }

    // File reader and PAUSE use the system font on game.com. These small
    // original glyphs allow the GBA converter to work from the cartridge alone.
    static byte[] Font(byte[] bios)
    {
        var rows=new byte[128*8];
        if (bios!=null) {
            for(int c=32;c<128;++c) for(int y=0;y<8;++y) for(int x=0;x<8;++x)
                if(Tiles.Pixel(bios,6,(c>>5)*8+x,(c&31)*8+y)==4)
                    rows[c*8+y]|=(byte)(128>>x);
            return rows;
        }
        string[] glyphs={
            "A0e11111f111111","B1e11111e11111e","C0f10101010100f","D1e11111111111e",
            "E1f10101e10101f","F1f10101e101010","G0e11101711110e","H1111111f111111",
            "I0e04040404040e","J0702020212120c","K11121418141211","L1010101010101f",
            "M111b1515111111","N11191915131311","O0e11111111110e","P1e11111e101010",
            "Q0e11111115120d","R1e11111e141211","S0f10100e01011e","T1f040404040404",
            "U1111111111110e","V11111111110a04","W1111111515150a","X11110a040a1111",
            "Y11110a04040404","Z1f01020408101f",
            "a00000e010f110f","b1010161911111e","c00000e1110100f","d01010d1311110f",
            "e00000e111f100e","f0609081c080808","g00000f11110f010e","h10101619111111",
            "i04000c0404040e","j020006020202120c","k10101214181412","l0c04040404040e",
            "m00001a15151515","n00001619111111","o00000e1111110e","p00001e11111e1010",
            "q00000f11110f0101","r00001619101010","s00000f100e011e","t08081c08080906",
            "u0000111111130d","v00001111110a04","w0000111115150a","x0000110a040a11",
            "y00001111110f010e","z00001f0204081f",
            "00e11131519110e","1040c040404040e","20e11010204081f","31e01010e01011e",
            "402060a121f0202","51f10101e01011e","60e10101e11110e","71f010204080808",
            "80e11110e11110e","90e11110f01010e","!04040404040004","?0e110102040004",
            ".00000000000004",",00000000000408","'04040000000000","-0000001f000000",
            "/01020204080810",":00000400000400",";00000400000408","(02040808080402",
            ")08040202020408","\"0a0a0000000000","*00040e1f0e0400",">10080402040810"};
        foreach (string g in glyphs)
            for (int y=0;2*y+2<g.Length;++y)
                rows[g[0]*8+y]=(byte)(Convert.ToByte(g.Substring(1+2*y,2),16)<<3);
        return rows;
    }

    static byte[] Pause(byte[] cart, byte[] font, byte[] bios)
    {
        var image=new byte[96,192];
        for (int y=0;y<85;++y)
            for (int x=0;x<177;++x)
                image[y,x]=(byte)(x<4 || x>=173 || y<4 || y>=81 ? 4 : 1);
        for (int line=0;line<2;++line) {
            int p=line==0?0x4323f:0x4324e, left=line==0?32:56;
            for (int i=0;cart[p+i]>=32 && cart[p+i]<127;++i)
                for (int y=0;y<16;++y) for (int x=0;x<8;++x) {
                    int c=cart[p+i];
                    int shade=bios==null ? ((font[c*8+y/2]&(128>>x))!=0 ? 4 : 1)
                                         : Tiles.Pixel(bios,6,64+(c>>4)*8+x,(c&15)*16+y);
                    image[25+16*line+y,left+i*8+x]=(byte)shade;
                }
        }
        var cells=new TileAtlas();
        var ids=new byte[18]; int count=0;
        for (int i=0;i<18;++i) {
            var cell=new byte[32,32];
            for (int y=0;y<32;++y) for (int x=0;x<32;++x)
                cell[y,x]=image[i/6*32+y,i%6*32+x];
            ids[i]=(byte)cells.Add(TileImage(cell));
            count=Math.Max(count,ids[i]+1);
        }
        if (count>16) throw new InvalidDataException("Pause OBJ capacity");
        using (var s=new MemoryStream()) using (var w=new BinaryWriter(s)) {
            w.Write((ushort)count); w.Write(ids); cells.Write(w); return s.ToArray();
        }
    }

    static byte[] Documents(byte[] cart)
    {
        int[] files={0x6bb54,0x6e000,0x6e117,0x6e26d,0x6ee21,0x6f041,
                     0x6f895,0x51899,0x6923e,0x69712,0x6d40b,0x6d007};
        using (var s=new MemoryStream()) using (var w=new BinaryWriter(s)) {
            foreach (int ignored in files) w.Write(0);
            for (int i=0;i<files.Length;++i) {
                int p=files[i], first=cart[p]*256+cart[p+1];
                int pages=((first&8191)-(p&8191))/2;
                long start=s.Position;
                s.Position=4*i; w.Write((int)start); s.Position=start;
                w.Write((byte)pages);
                for (int page=0;page<pages;++page) {
                    int address=(p&~8191)+((cart[p+2*page]*256+cart[p+2*page+1])&8191);
                    int end=address;
                    while (end<cart.Length && cart[end]!=0xfe) ++end;
                    if (end==cart.Length || end-address>2048) throw new InvalidDataException("Invalid document page");
                    w.Write((ushort)(end-address)); w.Write(cart,address,end-address);
                }
            }
            return s.ToArray();
        }
    }

    // 34:8CB6 indexes item icons. At 34:912C each description record also
    // supplies the crop used for the short name on the item-box screen.
    static byte[] Objects(byte[] cart,bool descriptions)
    {
        using (var stream=new MemoryStream()) using(var w=new BinaryWriter(stream)) {
            for(int i=0;i<29;++i) w.Write(0);
            for(int id=0;id<29;++id) {
                int row=(descriptions?0x6912c:0x68cb6)+(descriptions?6:4)*id;
                int d=Pointer(cart,0x34,row),flags=cart[d+5];
                long start=stream.Position;
                stream.Position=4*id; w.Write((int)start); stream.Position=start;
                w.Write(cart[row+2]);w.Write(cart[row+3]);
                w.Write(descriptions?cart[row+4]:(byte)0);w.Write(descriptions?cart[row+5]:(byte)0);
                w.Write(Patch(cart,cart[d+4],cart[d+2],cart[d+3],cart[d],cart[d+1],
                              flags==1?0xe0:flags==3?0x24:0xe4,(flags&1)==0));
            }
            return stream.ToArray();
        }
    }

    internal static void Write(byte[] cart,BinaryWriter output,byte[] systemBios=null)
    {
        var parts=new List<byte[]>();
        parts.Add(TileImage(Cut(cart,0x31,0,0,200,160,0xe4,true)));
        parts.Add(Maps(cart));
        var files=Solid(160,200,1);
        Stamp(files,Cut(cart,0x30,200,0,38,146,0xe4,true),82,8);
        parts.Add(TileImage(files));
        // Keep this order in sync with the asset ids in inventory.c.
        int[,] patches={
            {0x32,114,236,22,19,0xe0,0}, {0x31,200,34,30,30,0x24,0},
            {0x31,200,64,37,20,0xe4,1}, {0x31,0,160,133,50,0xe4,1},
            {0x33,58,202,33,7,0xe4,1}, {0x32,166,124,62,8,0x24,0},
            // Skip the cartridge's selection arrow before the YES / NO labels.
            {0x32,224,181,30,6,0x24,0} };
        for(int i=0;i<patches.GetLength(0);++i)
            parts.Add(Patch(cart,patches[i,0],patches[i,1],patches[i,2],patches[i,3],
                            patches[i,4],patches[i,5],patches[i,6]!=0));
        int[,] focus={{0,158},{0,150},{174,142},{116,142},{58,142},{0,142},{158,130},
                     {190,106},{188,88},{174,150},{116,150},{58,150},{0,202}};
        using (var s=new MemoryStream()) {
            for(int i=0;i<13;++i) {
                byte[] data=Patch(cart,0x33,focus[i,0],focus[i,1],58,8,0xe4,true);
                s.Write(data,0,data.Length);
            }
            parts.Add(s.ToArray());
        }
        var font=Font(systemBios);
        parts.Add(font); parts.Add(Documents(cart)); parts.Add(Pause(cart,font,systemBios));
        using (var s=new MemoryStream()) {
            for(int i=0;i<10;++i) {
                int x=i==0?206:i==1?252:256-5*i;
                var digit=new byte[8,6];
                Stamp(digit,Cut(cart,0x23,x,i==1?0:18,i==1?3:5,8,0xc7,true),i==1?1:0,0);
                byte[] data=new byte[48];
                Buffer.BlockCopy(digit,0,data,0,48);
                s.Write(data,0,data.Length);
            }
            parts.Add(s.ToArray());
        }
        using (var s=new MemoryStream()) {
            for(int hp=0;hp<3;++hp) {
                int group=Pointer(cart,0x34,0x68bee+3*hp);
                for(int frame=0;frame<11;++frame) {
                    int row=group+frame*4,d=Pointer(cart,0x34,row);
                    var obj=new byte[16,32];
                    var tile=Cut(cart,cart[d+4],cart[d+2],cart[d+3],cart[d],cart[d+1],0x24,false);
                    Stamp(obj,tile,0,0); byte[] data=TileImage(obj);
                    s.WriteByte((byte)(50+cart[row+2]));s.WriteByte((byte)(16+cart[row+3]));
                    s.WriteByte(0);s.WriteByte(0);s.Write(data,0,data.Length);
                }
            }
            parts.Add(s.ToArray());
        }
        parts.Add(Objects(cart,false)); parts.Add(Objects(cart,true));
        parts.Add(TileImage(Cut(cart,0x30,0,0,200,160,0xe4,true)));
        int take=Pointer(cart,0x34,0x68c9e);
        parts.Add(Patch(cart,cart[take+4],cart[take+2],cart[take+3],cart[take],cart[take+1],0x24,false));
        using (var s=new MemoryStream()) {
            for(int i=0;i<13;++i) {
                int d=0x493a5+5*i;
                if(i==10) continue; // typewriter photograph, handled by the cinema
                var sprite=new byte[i==11?32:i==12?8:16,i==11?64:8];
                Stamp(sprite,Cut(cart,cart[d+4],cart[d+2],cart[d+3],cart[d],cart[d+1],0xe4,true),0,0);
                byte[] data=TileImage(sprite);s.Write(data,0,data.Length);
            }
            parts.Add(s.ToArray());
        }
        parts.Add(Patch(cart,0x32,0xee,0x38,15,8,0xe4,true));
        long start=output.BaseStream.Position;
        foreach (byte[] ignored in parts) output.Write(0);
        for (int i=0;i<parts.Count;++i) {
            while ((output.BaseStream.Position&3)!=0) output.Write((byte)0);
            long where=output.BaseStream.Position;
            output.BaseStream.Position=start+4*i; output.Write((int)(where-start));
            output.BaseStream.Position=where; output.Write(parts[i]);
        }
    }
}
