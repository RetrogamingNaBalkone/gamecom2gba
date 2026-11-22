using System;
using System.IO;

internal static class Tiles
{
    // ROM DMA pages contain 256 columns of 256 two-bit pixels.
    internal static byte Pixel(byte[] cart, int page, int x, int y)
    {
        int packed = cart[(page << 14) + x * 64 + y / 4];
        return (byte)(1 + ((packed >> (6 - 2 * (y % 4))) & 3));
    }

    internal static void Write(BinaryWriter output, byte[,] screen)
    {
        for (int top = 0; top < screen.GetLength(0); top += 8)
            for (int left = 0; left < screen.GetLength(1); left += 8)
                for (int y = top; y < top + 8; y++)
                    for (int x = left; x < left + 8; x += 2)
                        output.Write((byte)(screen[y, x] | screen[y, x + 1] << 4));
    }

    internal static void Palette(BinaryWriter output, params int[] rgb)
    {
        for (int i = 0; i < 16; i++)
        {
            int c = i < rgb.Length ? rgb[i] : 0;
            output.Write((ushort)(((c >> 19) & 31) | (((c >> 11) & 31) << 5) | (((c >> 3) & 31) << 10)));
        }
    }
}
