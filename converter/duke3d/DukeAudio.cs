using System;
using System.IO;

internal static class DukeAudio
{
    static int Word(byte[] cart,int p) { return cart[p]*256+cart[p+1]; }
    static int Triple(byte[] cart,int p) { return Word(cart,p)*256+cart[p+2]; }

    internal static void Write(byte[] cart,BinaryWriter output) {
        long start=output.BaseStream.Position;
        // Two score pointers per scene, from the driver's tables at 50a2/50ae.
        int[] tracksAt={0x8d6e,0x8e22,0x8000,0x81f5,0x82d0,0x84b9,0x8654,0x88a9,0x8c8d,0x8cdb};
        int[] trackBounds=(int[])tracksAt.Clone();
        Array.Sort(trackBounds);
        const int samples=27;
        for (int i=0;i<samples+tracksAt.Length;i++) output.Write(0);
        for (int id=0;id<samples;id++) {
            Duke.WriteDirectoryEntry(output,start,id);
            int p=Triple(cart,0x26*8192+0xc71+3*id);
            GameComAudio.WriteSample(cart,p,output);
        }
        for (int i=0;i<tracksAt.Length;i++) {
            Duke.WriteDirectoryEntry(output,start,samples+i);
            // Some voices have no terminator before the next score's data.
            int next=Array.BinarySearch(trackBounds,tracksAt[i])+1;
            int end=0x2b*8192+(next<trackBounds.Length ? trackBounds[next]-0x8000 : 8192);
            int p=0x2b*8192+(tracksAt[i]&8191);
            while (p+2<end && cart[p]!=0) {
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
