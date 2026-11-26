using System;
using System.IO;

internal static class GameComAudio
{
    // SM8521 adaptive differential PCM, eight ranges and eight magnitudes.
    static readonly byte[,] Steps={
        {0,1,2,4,6,8,12,16},{1,3,5,9,13,17,25,33},
        {2,5,8,14,20,26,38,50},{3,7,11,19,27,35,41,67},
        {4,9,14,24,34,44,64,84},{5,11,17,29,41,53,77,111},
        {6,13,20,34,48,62,90,118},{7,15,23,39,55,71,103,125}
    };

    internal static void WriteSample(byte[] cart, int p, BinaryWriter output, int initialIndex=0,
                                     int trailingBytes=0)
    {
        int end=checked(p+((cart[p]*256+cart[p+1])*256+cart[p+2])+trailingBytes);
        if (end < p+3 || end > cart.Length) throw new InvalidDataException("Invalid ADPCM sample length.");
        int count=2*(end-p-3);
        output.Write(count);
        int value=128,index=initialIndex;
        for (int i=p+3;i<end;i++) for (int half=0;half<2;half++) {
            int code=(cart[i]>>(half*4))&15, lookup=index|(code>>1);
            int range=lookup>>3,magnitude=lookup&7;
            output.Write((byte)(value^128));
            int delta=Steps[range,magnitude];
            value=Math.Min(255,Math.Max(0,value+((code&1)==0 ? delta : -delta)));
            int shift=magnitude<2 ? -1 : magnitude<4 ? 0 : magnitude<6 ? 2 : magnitude-3;
            index=Math.Min(7,Math.Max(0,range+shift))*8;
        }
        // Silence while the main loop stops FIFO DMA on its next poll.
        for (int i=0;i<256+(4-count%4)%4;i++) output.Write((byte)0);
    }
}
