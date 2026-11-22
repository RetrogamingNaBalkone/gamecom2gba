using System.Collections.Generic;

internal static class Bezel
{
    // Hand-lettered at final display size. Each five-bit row is a glyph scanline.
    static readonly Dictionary<char, int[]> Glyphs = new Dictionary<char, int[]> {
        {'g',new[]{0,15,17,17,15,1,14}}, {'a',new[]{0,14,1,15,17,15,0}},
        {'m',new[]{0,0,26,21,21,21,0}}, {'e',new[]{0,14,17,31,16,15,0}},
        {'.',new[]{0,0,0,0,0,4,0}}, {'c',new[]{0,15,16,16,16,15,0}},
        {'o',new[]{0,14,17,17,17,14,0}}, {'T',new[]{31,4,4,4,4,4,4}},
        {'I',new[]{14,4,4,4,4,4,14}}, {'G',new[]{15,16,16,23,17,17,15}},
        {'E',new[]{31,16,16,30,16,16,31}}, {'R',new[]{30,17,17,30,20,18,17}}
    };

    static void Letter(byte[,] screen, string word, int left, byte ink)
    {
        int start = (160 - (word.Length * 12 - 2)) / 2;
        for (int i = 0; i < word.Length; i++)
            for (int y = 0; y < 7; y++)
                for (int x = 0; x < 5; x++)
                    if ((Glyphs[word[i]][y] & (16 >> x)) != 0)
                        for (int dy = 0; dy < 2; dy++)
                            for (int dx = 0; dx < 2; dx++)
                                screen[start + 12*i + 2*x + dx, left + 12 - 2*y + dy] = ink;
    }

    internal static void Draw(byte[,] screen)
    {
        for (int y = 0; y < 160; y++)
            for (int x = 0; x < 20; x++)
            {
                screen[y, x] = (byte)(x == 19 ? 6 : 5);
                screen[y, 239-x] = screen[y, x];
            }
        Letter(screen, "game.com", 3, 6);
        Letter(screen, "TIGER", 223, 6);
    }
}
