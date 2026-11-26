using System;
using System.IO;
using System.Collections.Generic;

internal sealed class Actors
{
    readonly byte[] cart;
    readonly TileAtlas sprites;
    internal Actors(byte[] cart,TileAtlas sprites) { this.cart=cart; this.sprites=sprites; }
    int Byte(int bank,int address) { return cart[bank*8192+(address&8191)]; }
    int Word(int bank,int address) { return Byte(bank,address)*256+Byte(bank,address+1); }

    // Type, action, viewing distance and animation phase select a sequence of
    // five-byte frame entries. Each carries its own origin and art descriptor.
    internal void Write(BinaryWriter output)
    {
        long start=output.BaseStream.Position;
        const int slots=4*11*4*4;
        for (int i=0;i<slots+12;i++) output.Write(0);
        var written=new Dictionary<int,uint>();
        for (int i=0;i<slots;i++) {
            int type=i/(11*16), action=i/16%11, distance=i/4%4;
            int states=Word(0x22,0x70f6+2*type);
            int sequence=Word(0x22,states+2*action)+3*distance;
            int phase=Math.Min(i%4,Byte(0x22,sequence));
            int entry=Word(0x22,sequence+1)+5*phase;
            int key=2*entry+(distance<2 ? 1 : 0);
            uint offset;
            if (!written.TryGetValue(key,out offset)) {
                offset=(uint)(output.BaseStream.Position-start);
                written.Add(key,offset);
                WriteFrame(output,Word(0x22,entry+3),Byte(0x22,entry+1),Byte(0x22,entry+2),distance<2);
            }
            long end=output.BaseStream.Position;
            output.BaseStream.Position=start+4*i;
            output.Write(offset);
            output.BaseStream.Position=end;
        }
        int slot=slots;
        foreach (int type in new int[]{6,7,9}) {
            int poses=Word(0x22,0x7987+2*(type-1));
            for (int distance=0;distance<4;distance++) {
                int descriptor=Word(0x22,poses+2*distance);
                long position=output.BaseStream.Position;
                output.BaseStream.Position=start+4*slot++;
                output.Write((uint)(position-start));
                output.BaseStream.Position=position;
                WriteFrame(output,descriptor,Byte(0x26,descriptor)/2,Byte(0x26,descriptor+1),false);
            }
        }
    }

    void WriteFrame(BinaryWriter output,int descriptor,int anchorX,int anchorY,bool masked)
    {
        int width=Byte(0x26,descriptor),height=Byte(0x26,descriptor+1);
        int sx=Byte(0x26,descriptor+2),sy=Byte(0x26,descriptor+3),page=Byte(0x26,descriptor+4);
        output.Write((byte)width);
        output.Write((byte)height);
        output.Write((byte)anchorX);
        output.Write((byte)anchorY);
        for (int top=0;top<height;top+=32)
            for (int left=0;left<width;left+=32) {
                var pixels=new byte[32,32];
                for (int y=0;y<Math.Min(32,height-top);y++)
                    for (int x=0;x<Math.Min(32,width-left);x++) {
                        int u=(sx+left+x)&255,v=(sy+top+y)&255;
                        byte color=Tiles.Pixel(cart,page,u,v);
                        pixels[y,x]=masked && Tiles.Pixel(cart,page+1,u,v)!=1 ? (byte)1 :
                                    color==1 ? (byte)0 : color;
                    }
                using (var buffer=new MemoryStream(512))
                using (var writer=new BinaryWriter(buffer)) {
                    Tiles.Write(writer,pixels);
                    output.Write(sprites.Add(buffer.ToArray()));
                }
            }
    }
}
