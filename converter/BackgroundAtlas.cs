using System.IO;
using System.Collections.Generic;

// A layout references rows of three-character strips. Characters share
// 4bpp scanlines across all three backgrounds and the intermission cards.
internal sealed class BackgroundAtlas
{
    readonly TileAtlas data=new TileAtlas();
    readonly Dictionary<uint,int> lineIds=new Dictionary<uint,int>();
    readonly List<uint> lines=new List<uint>();
    internal readonly int Empty;

    internal BackgroundAtlas()
    {
        Empty=AddLayer(new byte[136,240]);
    }

    internal int Add(byte[] bytes) { return data.AddOffset(bytes); }
    internal void Write(BinaryWriter output)
    {
        output.Write(4+4*lines.Count);
        foreach (uint line in lines) output.Write(line);
        data.Write(output);
    }

    internal int AddTile(byte[] literal,int start)
    {
        var packed=new byte[16];
        bool overflow=false;
        for (int y=0;y<8;y++) {
            uint line=0;
            for (int i=0;i<4;i++) line|=(uint)literal[start+4*y+i]<<(8*i);
            int id;
            if (!lineIds.TryGetValue(line,out id)) {
                if (lines.Count==65536) { overflow=true; continue; }
                id=lines.Count;
                lineIds.Add(line,id);
                lines.Add(line);
            }
            packed[2*y]=(byte)id;
            packed[2*y+1]=(byte)(id>>8);
        }
        // Without a 16-bit code for a scanline, store this tile verbatim.
        // All dictionary offsets are word aligned, leaving the low bit free.
        if (!overflow) return Add(packed);
        var tile=new byte[32];
        System.Array.Copy(literal,start,tile,0,32);
        return Add(tile)|1;
    }

    internal int AddLayer(byte[,] image,int height=136)
    {
        using (var layout=new MemoryStream())
        using (var layoutWriter=new BinaryWriter(layout)) {
            var tileBytes=new byte[32];
            for (int top=0;top<height;top+=8) {
                using (var row=new MemoryStream())
                using (var rowWriter=new BinaryWriter(row)) {
                    for (int left=0;left<240;left+=24) {
                        var strip=new byte[12];
                        for (int tile=0;tile<3;tile++) {
                            int index=0;
                            for (int y=top;y<top+8;y++)
                                for (int dx=0;dx<8;dx+=2) {
                                    int x=left+8*tile+dx;
                                    tileBytes[index++]=(byte)(image[y,x]|(image[y,x+1]<<4));
                                }
                            int id=AddTile(tileBytes,0);
                            for (int i=0;i<4;i++) strip[4*tile+i]=(byte)(id>>(8*i));
                        }
                        rowWriter.Write(Add(strip));
                    }
                    layoutWriter.Write(Add(row.ToArray()));
                }
            }
            return Add(layout.ToArray());
        }
    }
}
