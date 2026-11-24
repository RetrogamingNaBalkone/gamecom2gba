using System.IO;

internal static class Opening
{
    // Cartridge DMA source rectangles, destination and four-shade remap.
    // Masked objects use the adjacent graphics page for opaque light pixels.
    static readonly int[,] Parts={
        {59, 0, 0, 123, 122, 38, 19, 254, 0},
        {59, 0, 0, 123, 122, 38, 19, 249, 0},
        {59, 0, 0, 123, 122, 38, 19, 233, 0},
        {59, 0, 0, 123, 122, 38, 19, 229, 0},
        {59, 0, 0, 123, 122, 38, 19, 228, 0},
        {59, 0, 0, 123, 122, 38, 19, 224, 0},
        {59, 0, 0, 123, 122, 38, 19, 208, 0},
        {58, 0, 0, 138, 137, 31, 11, 208, 0},
        {80, 56, 96, 200, 160, 0, 0, 231, 0},
        {80, 56, 96, 200, 160, 0, 0, 230, 0},
        {80, 56, 96, 200, 160, 0, 0, 229, 0},
        {80, 56, 96, 200, 160, 0, 0, 228, 0},
        {33, 0, 0, 200, 160, 0, 0, 228, 0},
        {92, 92, 118, 68, 133, 62, 18, 228, 1},
        {108, 148, 0, 97, 59, 61, 36, 228, 1},
        {118, 0, 192, 142, 50, 43, 83, 228, 1},
        {100, 208, 156, 24, 42, 114, 36, 228, 1},
        {32, 0, 0, 200, 160, 0, 0, 228, 0},
        {108, 148, 0, 97, 59, 61, 34, 228, 1},
        {94, 132, 107, 26, 29, 120, 44, 228, 1},
        {35, 0, 0, 200, 160, 0, 0, 228, 0},
        {34, 0, 0, 200, 160, 0, 0, 228, 0},
        {102, 0, 218, 186, 38, 8, 0, 228, 1},
        {102, 0, 218, 186, 38, 8, 5, 228, 1},
        {102, 0, 218, 186, 38, 8, 36, 228, 1},
        {102, 0, 218, 186, 38, 8, 51, 228, 1},
        {102, 0, 218, 186, 38, 8, 82, 228, 1},
        {102, 0, 218, 186, 38, 8, 97, 228, 1},
        {102, 0, 218, 186, 38, 8, 108, 228, 1}
    };
    static readonly int[][] Poses={
        new[]{0},
        new[]{1},
        new[]{2},
        new[]{3},
        new[]{4},
        new[]{5},
        new[]{6},
        new[]{7},
        new[]{8},
        new[]{9},
        new[]{10},
        new[]{11},
        new[]{12, 13, 14, 15, 16},
        new[]{17, 13, 18, 19},
        new[]{20, 13, 18},
        new[]{21, 13, 14, 15},
        new[]{20, 13, 14, 22},
        new[]{21, 13, 14, 22},
        new[]{21, 13, 14, 23},
        new[]{12, 13, 14, 23},
        new[]{12, 13, 14, 24},
        new[]{17, 13, 14, 24},
        new[]{17, 13, 14, 25},
        new[]{20, 13, 14, 25},
        new[]{20, 13, 14, 26},
        new[]{21, 13, 14, 26},
        new[]{21, 13, 14, 27},
        new[]{12, 13, 14, 27},
        new[]{12, 13, 14, 28},
        new[]{17, 13, 14, 28},
        new[]{20, 13, 14, 28},
        new[]{21, 13, 14, 28}
    };
    static readonly int[,] Timeline={
        {0, 0},
        {12, 1},
        {35, 2},
        {47, 3},
        {59, 4},
        {71, 5},
        {95, 6},
        {119, 7},
        {153, 6},
        {201, 8},
        {294, 9},
        {339, 10},
        {384, 11},
        {787, 10},
        {832, 9},
        {876, 8},
        {966, 12},
        {978, 13},
        {990, 14},
        {1002, 15},
        {1014, 12},
        {1026, 13},
        {1038, 14},
        {1050, 15},
        {1062, 12},
        {1074, 13},
        {1085, 14},
        {1097, 15},
        {1109, 12},
        {1121, 13},
        {1133, 14},
        {1145, 15},
        {1157, 12},
        {1169, 13},
        {1193, 16},
        {1205, 17},
        {1217, 18},
        {1229, 19},
        {1241, 20},
        {1265, 21},
        {1277, 22},
        {1300, 23},
        {1312, 24},
        {1336, 25},
        {1348, 26},
        {1372, 27},
        {1384, 28},
        {1396, 29},
        {1420, 30},
        {1432, 31},
        {1444, 28},
        {1456, 29},
        {1468, 30},
        {1480, 31},
        {1492, 28},
        {1504, 29},
        {1516, 30},
        {1527, 31},
        {1539, 28},
        {1551, 29},
        {1563, 30},
        {1575, 31},
        {1587, 28},
        {1599, 29},
        {1611, 30},
        {1623, 31}
    };
    static void Blit(byte[] cart,byte[,] scene,int page,int sx,int sy,int w,int h,
                     int left,int top,int palette,bool masked) {
        for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
            int px=(sx+x)&255, py=(sy+y)&255;
            int raw=Tiles.Pixel(cart,page,px,py)-1;
            if (masked && Tiles.Pixel(cart,page+1,px,py)!=1) raw=0;
            else if (masked && raw==0) continue;
            scene[top+y,20+left+x]=(byte)(1+((palette>>(2*raw))&3));
        }
    }

    static byte[,] Background() {
        var scene=new byte[160,240];
        Bezel.Draw(scene);
        for (int y=0;y<160;y++) for (int x=20;x<220;x++) scene[y,x]=4;
        return scene;
    }

    internal static void Write(byte[] cart,BinaryWriter writer,BackgroundAtlas atlas) {
        var layers=new int[Poses.Length,2];
        for (int i=0;i<Poses.Length;i++) {
            var bg=Background();
            var fg=new byte[160,240];
            foreach (int p in Poses[i]) {
                bool masked=Parts[p,8]!=0;
                Blit(cart,masked ? fg : bg,Parts[p,0],Parts[p,1],Parts[p,2],Parts[p,3],
                     Parts[p,4],Parts[p,5],Parts[p,6],Parts[p,7],masked);
            }
            layers[i,0]=atlas.AddLayer(bg,160);
            layers[i,1]=atlas.AddLayer(fg,160);
        }
        writer.Write(Timeline.GetLength(0));
        for (int i=0;i<Timeline.GetLength(0);i++) {
            writer.Write(Timeline[i,0]);
            writer.Write(layers[Timeline[i,1],0]);
            writer.Write(layers[Timeline[i,1],1]);
        }

        string[][] menus={new[]{"NEW GAME","OPTIONS","QUIT"},
            new[]{"L.A. MELTDOWN","LUNAR APOCALYPSE","SHRAPNEL CITY"},
            new[]{"PIECE OF CAKE","LET'S ROCK","COME GET SOME","DAMN I'M GOOD"}};
        string[] heading={"","SELECT AN EPISODE","SELECT SKILL"};
        for (int m=0;m<menus.Length;m++) for (int option=0;option<menus[m].Length;option++) {
            var bg=Background();
            Blit(cart,bg,0x24,0,0,200,160,0,0,0xe4,false);
            if (m==0) {
                Blit(cart,bg,0x2f,144,136,80,72,61,67,0xe4,false);
                if (option==0) Blit(cart,bg,0x48,0,225,92,24,56,66,0xe4,false);
                if (option==1) Blit(cart,bg,0x47,176,112,80,24,61,90,0xe4,false);
                if (option==2) Blit(cart,bg,0x3e,200,213,56,24,75,115,0xe4,false);
                writer.Write(atlas.AddLayer(bg,160));
                continue;
            }
            Intermission.Text(bg,heading[m],65,1,1);
            for (int j=0;j<menus[m].Length;j++) {
                int y=(m==2 ? 80 : 96)+16*j;
                string label=menus[m][j];
                int left=120-3*label.Length-4, right=239-left;
                for (int row=y-3;row<=y+9;row++) for (int x=left;x<=right;x++)
                    bg[row,x]=(byte)(option==j ? 1 : 4);
                Intermission.Text(bg,label,y,1,(byte)(option==j ? 4 : 1));
            }
            writer.Write(atlas.AddLayer(bg,160));
        }
        for (int episode=0;episode<3;episode++) {
            var bg=Background();
            Blit(cart,bg,0x3a,0,0,138,137,31,6,0xd0,false);
            Blit(cart,bg,0x2e,64,240,72,16,64,67,0xd0,false);
            int[] numeralX={32,39,49};
            int width=episode==0 ? 7 : 10;
            Blit(cart,bg,0x2e,numeralX[episode],240,width,16,100-width/2,87,0xd0,false);
            writer.Write(atlas.AddLayer(bg,160));
        }
        for (int episode=0;episode<3;episode++) {
            var splash=Background();
            Blit(cart,splash,0x27-episode,0,0,200,160,0,0,0xe4,false);
            writer.Write(atlas.AddLayer(splash,160));
        }
        for (int option=0;option<3;option++) {
            var bg=MenuBackground(cart);
            Blit(cart,bg,0x34,0,136,120,72,41,67,0xe4,false);
            if (option==0) Blit(cart,bg,0x48,92,225,88,24,54,92,0xe4,false);
            if (option==1) Blit(cart,bg,0x47,176,88,80,24,32,115,0xe4,false);
            if (option==2) Blit(cart,bg,0x27,200,136,56,24,110,116,0xe4,false);
            writer.Write(atlas.AddLayer(bg,160));
        }
        for (int mute=0;mute<2;mute++) for (int exit=0;exit<2;exit++) {
            var bg=MenuBackground(cart);
            Blit(cart,bg,0x76,72,0,104,72,49,67,0xe4,false);
            if (exit!=0) Blit(cart,bg,0x27,200,136,56,24,74,115,0xe4,false);
            for (int x=0;x<32;x+=8)
                Blit(cart,bg,0x58,240,190,8,16,67+36*mute+x,93,0xe4,false);
            Label(bg,"ON",75,97,mute==0);
            Label(bg,"OFF",107,97,mute!=0);
            if (exit==0) Box(bg,64,90,74,21);
            writer.Write(atlas.AddLayer(bg,160));
        }
        for (int pos=0;pos<=21;pos++) {
            var bg=MenuBackground(cart);
            Blit(cart,bg,0x2f,0,136,144,72,29,67,0xe4,false);
            if (pos<20) Box(bg,(pos%10<5 ? 31 : 128)+9*(pos%5),91+13*(pos/10),9,11);
            if (pos==20) Blit(cart,bg,0x27,200,136,56,24,75,115,0xe4,false);
            for (int y=93;y<106;y++) for (int x=80;x<124;x++) bg[y,20+x]=4;
            Box(bg,86,95,37,18);
            if (pos==21) Intermission.Text(bg,"INVALID PASSWORD",140,1,1);
            writer.Write(atlas.AddLayer(bg,160));
        }
        // Reuse the twenty keyboard glyphs for entered and saved passwords.
        var keyboard=MenuBackground(cart);
        Blit(cart,keyboard,0x2f,0,136,144,72,29,67,0xe4,false);
        for (int pos=0;pos<20;pos++) {
            int left=(pos%10<5 ? 31 : 128)+9*(pos%5);
            for (int y=0;y<8;y++) for (int x=0;x<8;x+=2) {
                int row=93+13*(pos/10)+y,col=21+left+x;
                writer.Write((byte)(keyboard[row,col]|keyboard[row,col+1]<<4));
            }
        }
    }

    static byte[,] MenuBackground(byte[] cart) {
        var bg=Background();
        Blit(cart,bg,0x24,0,0,200,160,0,0,0xe4,false);
        return bg;
    }

    static void Box(byte[,] bg,int x,int y,int w,int h) {
        for (int i=0;i<w;i++) bg[y,20+x+i]=bg[y+h-1,20+x+i]=1;
        for (int i=0;i<h;i++) bg[y+i,20+x]=bg[y+i,20+x+w-1]=1;
    }

    static void Label(byte[,] bg,string text,int x,int y,bool selected) {
        foreach (char c in text) {
            for (int row=0;row<7;row++) {
                int bits=Intermission.GlyphRow(c,row);
                for (int col=0;col<5;col++) if ((bits&(16>>col))!=0)
                    bg[y+row,20+x+col]=(byte)(selected ? 4 : 1);
            }
            x+=8;
        }
    }
}
