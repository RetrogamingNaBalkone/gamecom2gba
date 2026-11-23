using System;
using System.IO;

internal static class DukeAudio
{
    // SM8521 adaptive differential PCM, eight ranges and eight magnitudes.
    static readonly byte[,] Steps={
        {0,1,2,4,6,8,12,16},{1,3,5,9,13,17,25,33},
        {2,5,8,14,20,26,38,50},{3,7,11,19,27,35,41,67},
        {4,9,14,24,34,44,64,84},{5,11,17,29,41,53,77,111},
        {6,13,20,34,48,62,90,118},{7,15,23,39,55,71,103,125}
    };
    static int Word(byte[] cart,int p) { return cart[p]*256+cart[p+1]; }
    static int Triple(byte[] cart,int p) { return Word(cart,p)*256+cart[p+2]; }

    internal static void Write(byte[] cart,BinaryWriter output) {
        long start=output.BaseStream.Position;
        // Two score pointers per scene, from the driver's tables at 50a2/50ae.
        int[] tracksAt={0x8d6e,0x8e22,0x8000,0x81f5,0x82d0,0x84b9,0x8654,0x88a9,0x8c8d,0x8cdb};
        const int samples=27;
        for (int i=0;i<samples+tracksAt.Length;i++) output.Write(0);
        for (int id=0;id<samples;id++) {
            Duke.WriteDirectoryEntry(output,start,id);
            int p=Triple(cart,0x26*8192+0xc71+3*id);
            int end=p+Triple(cart,p);
            int count=2*(end-p-3);
            output.Write(count);
            int value=128,range=0;
            for (int i=p+3;i<end;i++) for (int half=0;half<2;half++) {
                int code=(cart[i]>>(half*4))&15, magnitude=code>>1;
                output.Write((byte)(value^128));
                int delta=Steps[range,magnitude];
                value=Math.Min(255,Math.Max(0,value+((code&1)==0 ? delta : -delta)));
                int shift=magnitude<2 ? -1 : magnitude<4 ? 0 : magnitude<6 ? 2 : magnitude-3;
                range=Math.Min(7,Math.Max(0,range+shift));
            }
            // Silence while the main loop stops FIFO DMA on its next poll.
            for (int i=0;i<256+(4-count%4)%4;i++) output.Write((byte)0);
        }
        for (int i=0;i<tracksAt.Length;i++) {
            Duke.WriteDirectoryEntry(output,start,samples+i);
            int p=0x2b*8192+(tracksAt[i]&8191);
            while (cart[p]!=0) {
                int pitch=cart[p],period=pitch==128 ? 0 : Word(cart,0x20*8192+0xdaa+2*(pitch-16));
                int frequency=period==0 ? 0 : 2048-(int)Math.Round(131072.0*(period-1)*32/4915200);
                output.Write((ushort)Math.Max(0,frequency));
                output.Write(cart[p+1]); output.Write(cart[p+2]);
                p+=3;
            }
            output.Write(0);
        }
    }
}
