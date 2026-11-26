using System;
using System.IO;
using System.Collections.Generic;

internal static class Campaign
{
    static int Address(int bank, int cpu) { return bank * 8192 + (cpu & 8191); }
    static int Word(byte[] rom, int p) { return (rom[p] << 8) | rom[p+1]; }
    static int Lookup(byte[] rom, int bank, int directory, int index)
    {
        return Address(bank, Word(rom, Address(bank, directory) + 2*index));
    }

    static void Pickups(byte[] rom, int area, int cam, BinaryWriter directory,
                        BinaryWriter records, BinaryWriter graphics, Dictionary<string, byte> variants)
    {
        int region=Lookup(rom,0x29,0x9236,area);
        int p=Address(0x29,Word(rom,region+2*cam));
        int first=checked((int)records.BaseStream.Position / 8), count=0;
        while (Word(rom,p)!=255) {
            int flag=Word(rom,p);
            if (flag<0x290 || flag>0x294 || count>=2)
                throw new InvalidDataException("Unexpected RE2 pickup list");
            int desc=Address(0x29,Word(rom,p+4)), width=rom[p+8], height=rom[p+9];
            int bits=rom[p+10], sx=rom[desc+3], sy=rom[desc+4], page=rom[desc+5];
            int[] palettes={0xe4,0xe0,0xc4,0x24};
            int palette=palettes[rom[desc]];
            string key=desc+"/"+bits+"/"+width+"/"+height;
            byte tile;
            if (!variants.TryGetValue(key,out tile)) {
                tile=checked((byte)variants.Count);
                variants.Add(key,tile);
                byte[,] pixels=new byte[32,32];
                for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
                    int u=sx+((bits&64)!=0 ? width-1-x : x);
                    int v=sy+((bits&128)!=0 ? height-1-y : y);
                    int raw=Tiles.Pixel(rom,page,u&255,v&255)-1;
                    if (raw!=0 || (bits&1)==0) pixels[y,x]=(byte)(1+((palette>>(2*raw))&3));
                }
                Tiles.Write(graphics,pixels);
            }
            records.Write((byte)(flag-0x290));
            records.Write(rom[p+2]); records.Write(rom[p+3]); records.Write(tile);
            records.Write(rom,p+6,4);
            ++count; p+=11;
        }
        directory.Write(checked((byte)first));
        directory.Write((byte)count);
    }

    static void Props(byte[] rom,BinaryWriter output)
    {
        using (var descriptions=new MemoryStream())
        using (var graphics=new MemoryStream())
        using (var records=new BinaryWriter(descriptions))
        using (var tiles=new BinaryWriter(graphics)) {
            var offsets=new int[7];
            var unique=new Dictionary<string,byte>();
            int[] palettes={0xe4,0xe0,0xc4,0x24};
            for (int id=0;id<7;++id) {
                offsets[id]=32+checked((int)descriptions.Length);
                int p=Address(0x24,0x78b3)+rom[Address(0x24,0x78ac)+id];
                int n=rom[p++],width=rom[Address(0x29,Word(rom,p))+1];
                records.Write((byte)width); records.Write(rom[p+2]); records.Write(rom[p+3]);
                int pieces=0;
                for (int i=0;i<n;++i) {
                    int q=p+9*i;
                    pieces+=((rom[q+6]+63)/64)*((rom[q+7]+63)/64);
                }
                records.Write(checked((byte)pieces));
                for (int i=0;i<n;++i,p+=9) {
                    int desc=Address(0x29,Word(rom,p)),w=rom[p+6],h=rom[p+7],mode=rom[p+8];
                    int index=rom[desc],palette=palettes[index<4 ? index : 0];
                    for (int top=0;top<h;top+=64) for (int left=0;left<w;left+=64) {
                        var pixels=new byte[64,64];
                        for (int y=top;y<Math.Min(top+64,h);++y)
                            for (int x=left;x<Math.Min(left+64,w);++x) {
                                int u=rom[desc+3]+((mode&64)==0 ? x : w-1-x);
                                int v=rom[desc+4]+((mode&128)==0 ? y : h-1-y);
                                int raw=Tiles.Pixel(rom,rom[desc+5],u&255,v&255)-1;
                                if (raw!=0 || (mode&1)==0) pixels[y-top,x-left]=(byte)(1+((palette>>(2*raw))&3));
                            }
                        byte[] bytes;
                        using (var data=new MemoryStream()) using (var writer=new BinaryWriter(data)) {
                            Tiles.Write(writer,pixels);
                            bytes=data.ToArray();
                        }
                        byte tile;
                        string key=Convert.ToBase64String(bytes);
                        if (!unique.TryGetValue(key,out tile)) {
                            tile=checked((byte)unique.Count);
                            unique.Add(key,tile);
                            tiles.Write(bytes);
                        }
                        records.Write(rom[p+3]); records.Write(checked((byte)(rom[p+4]+left)));
                        records.Write(checked((byte)(rom[p+5]+top))); records.Write(tile);
                    }
                }
            }
            output.Write(checked(32+(int)descriptions.Length));
            foreach (int offset in offsets) output.Write(offset);
            descriptions.WriteTo(output.BaseStream);
            graphics.WriteTo(output.BaseStream);
        }
    }

    internal static void Write(byte[] rom, BinaryWriter output)
    {
        int[] counts = {33,76,41,43,112,78,37};
        using (var rows = new MemoryStream())
        using (var walls = new MemoryStream())
        using (var directory = new MemoryStream())
        using (var items = new MemoryStream())
        using (var graphics = new MemoryStream())
        using (var itemDirectory = new BinaryWriter(directory))
        using (var itemRecords = new BinaryWriter(items))
        using (var itemGraphics = new BinaryWriter(graphics))
        using (var views = new BinaryWriter(rows)) {
            var variants = new Dictionary<string,byte>();
            for (int area=0; area<counts.Length; ++area) {
                int cameras = Lookup(rom,0x28,0x8000,area);
                int perspective = Lookup(rom,0x24,0x6e79,area);
                for (int cam=0; cam<counts[area]; ++cam) {
                    Pickups(rom,area,cam,itemDirectory,itemRecords,itemGraphics,variants);
                    int view = Address(0x28, Word(rom,cameras+2*cam));
                    int project = Address(0x24, Word(rom,perspective+2*cam));
                    views.Write(rom, project, 9);
                    views.Write((byte)0);
                    int script = Word(rom,view+2);
                    int p = Address(0x33,script);
                    for (int dir=0; dir<4; ++dir) {
                        views.Write(script == 0 ? (ushort)65535 : checked((ushort)(p-0x66000)));
                        if (script != 0 && dir < 3) {
                            while (p < 0x67ac2 && rom[p] != 255) ++p;
                            if (p >= 0x67ac2) throw new InvalidDataException("Missing RE2 exit delimiter");
                            ++p;
                        }
                    }
                    views.Write(checked((ushort)walls.Position));
                    int obstacle = Word(rom,view+4);
                    if (obstacle == 0) walls.WriteByte(0);
                    else {
                        int start = Address(0x28,obstacle);
                        walls.Write(rom,start,1 + 3*rom[start]);
                    }
                }
            }
            output.Write(new byte[] {67,65,77,49});
            uint edges=checked((uint)(88+rows.Length)), wallOffset=edges+0x1ac2;
            uint itemDirOffset=checked(wallOffset+(uint)walls.Length);
            uint itemsOffset=checked(itemDirOffset+(uint)directory.Length);
            uint tileOffset=checked(itemsOffset+(uint)items.Length+3)&~3u;
            output.Write(edges);
            output.Write(wallOffset);
            // Seven typewriter records and the seven regional door flag masks.
            output.Write(rom, Address(0x24,0x6d56),28);
            for (int i=0;i<7;++i)
                output.Write(rom[Address(0x24,0x78b6)+rom[Address(0x24,0x78ac)+i]]);
            output.Write((byte)0);
            output.Write(itemDirOffset);
            output.Write(itemsOffset);
            output.Write(tileOffset);
            output.Write(rom,Address(0x24,0x6cc1),24);
            output.Write(checked(tileOffset+(uint)graphics.Length));
            rows.WriteTo(output.BaseStream);
            output.Write(rom,0x66000,0x1ac2);
            walls.WriteTo(output.BaseStream);
            directory.WriteTo(output.BaseStream);
            items.WriteTo(output.BaseStream);
            for (long pad=itemsOffset+items.Length;pad<tileOffset;++pad) output.Write((byte)0);
            graphics.WriteTo(output.BaseStream);
            Props(rom,output);
        }
    }
}
