using System.IO;

internal static class Intermission
{
    // Five-bit scanlines for the port's status lettering.
    static readonly string[] Font={
        "0e11111f111111","1e11111e11111e","0f10101010100f","1e11111111111e",
        "1f10101e10101f","1f10101e101010","0f10101711110f","1111111f111111",
        "0e04040404040e","0702020212120c","11121418141211","1010101010101f",
        "111b1515111111","11191915131311","0e11111111110e","1e11111e101010",
        "0e11111115120d","1e11111e141211","0f10100e01011e","1f040404040404",
        "1111111111110e","11111111110a04","1111111515150a","11110a040a1111",
        "11110a04040404","1f01020408101f"};

    internal static int GlyphRow(char letter,int row)
    {
        if (letter==' ') return 0;
        if (letter=='.') return row==6 ? 4 : 0;
        if (letter=='0') return row==0 || row==6 ? 14 : 17;
        return System.Convert.ToInt32(Font[letter-'A'].Substring(2*row,2),16);
    }

    static void Text(byte[,] scene,string text,int top,int scale,byte color)
    {
        int left=20+(200-(6*text.Length-1)*scale)/2;
        foreach (char c in text) {
            if (c!=' ') for (int y=0; y<7; y++) {
                int bits=GlyphRow(c,y);
                for (int x=0; x<5; x++)
                    if ((bits&(16>>x))!=0)
                        for (int dy=0; dy<scale; dy++)
                            for (int dx=0; dx<scale; dx++)
                                scene[top+y*scale+dy,left+x*scale+dx]=color;
            }
            left+=6*scale;
        }
    }

    static void Name(byte[] cart,byte[,] scene,int level)
    {
        int table=0x21*8192+(0x7597&8191)+2*level;
        int p=0x21*8192+((cart[table]*256+cart[table+1])&8191);
        for (int y=80; y<=96; y+=16) {
            int length=cart[p++];
            Text(scene,System.Text.Encoding.ASCII.GetString(cart,p,length),y,2,1);
            p+=length;
        }
    }

    internal static void Write(byte[] cart,BinaryWriter output,BackgroundAtlas backgrounds,int level)
    {
        var scene=new byte[160,240];
        Bezel.Draw(scene);
        for (int y=0; y<160; y++) for (int x=20; x<220; x++) scene[y,x]=4;
        Text(scene,"COMPLETED",32,2,1);
        Name(cart,scene,level);
        output.Write(backgrounds.AddLayer(scene,160));
        for (int y=0; y<160; y++) for (int x=0; x<200; x++) scene[y,x+20]=Tiles.Pixel(cart,0x24,x,y);
        Text(scene,"TOTAL FRAGS",80,1,1);
        Text(scene,"FRAGS",112,1,1);
        output.Write(backgrounds.AddLayer(scene,160));
        for (int y=0; y<160; y++) for (int x=20; x<220; x++) scene[y,x]=4;
        Text(scene,"ENTERING",32,2,1);
        Name(cart,scene,level);
        output.Write(backgrounds.AddLayer(scene,160));
    }

    internal static void WriteRetry(byte[] cart,BinaryWriter output,BackgroundAtlas backgrounds)
    {
        var scene=new byte[160,240];
        Bezel.Draw(scene);
        for (int y=0;y<160;y++)
            for (int x=0;x<200;x++) scene[y,x+20]=Tiles.Pixel(cart,0x24,x,y);
        Text(scene,"AHH THEY GOT YOU",64,1,1);
        Text(scene,"A  RESTART SAME LEVEL",96,1,1);
        output.Write(backgrounds.AddLayer(scene,160));
    }
}
