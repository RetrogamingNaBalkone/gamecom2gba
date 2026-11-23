internal static class Bezel
{
    // Silhouettes redrawn at LCD resolution from the case lettering.
    // The dot in game.com is an orbiting planet. The R in TIGER has a swash.
    static readonly string[] GameCom={
        "  #######  #######   ###########    #####             #####   #####   ########### ",
        " ########  ########  ############  ######            ######  #######  ############",
        "###   ###       ###  ##   ##   ## ###               ###     ##    ### ##   ##   ##",
        "##     ##       ###  ##   ##   ## ##                ##     ###     ## ##   ##   ##",
        "##     ##   #######  ##   ##   ## ######            ##     ##      ## ##   ##   ##",
        "##     ##  ########  ##   ##   ## ######     ###  / ##     ##      ## ##   ##   ##",
        "##     ## ###    ##  ##   ##   ## ##        #####/  ##     ##      ## ##   ##   ##",
        "###    ## ##     ##  ##   ##   ## ##       ######   ###    ###     ## ##   ##   ##",
        " ####  ## ###    ##  ##   ##   ##  ######  /####     ###### ####  ### ##   ##   ##",
        "  #######  ########  ##   ##   ##   ######/ ###       #####   #####   ##   ##   ##",
        "       ##                                /",
        "       ##",
        " #######",
        " ######"
    };

    static readonly string[] Tiger={
        "########### ###    #######   #########  ########",
        "########### ###   ########  ########## ##########",
        "    ###    ###   ###       ###        ###    ###",
        "    ###    ###  ###        ###        ###    ###",
        "   ###    ###   ##   ####  ########   #########",
        "   ###    ###  ###   #### ########   ########",
        "   ###    ###  ###    ##  ###        ###  ###",
        "  ###    ###   ###   ### ###        ###    ###             ##",
        "  ###    ###    #######  #########  ###     ################",
        "  ##     ##      #####   ########   ##        ############"
    };

    static void Logo(byte[,] screen,string[] rows,int left) {
        int width=0;
        foreach (string row in rows) width=System.Math.Max(width,row.Length);
        for (int y=0;y<rows.Length;y++)
            for (int x=0;x<rows[y].Length;x++)
                if (rows[y][x]!=' ') screen[(160-width)/2+x,left+rows.Length-1-y]=6;
    }

    internal static void Draw(byte[,] screen)
    {
        for (int y = 0; y < 160; y++)
            for (int x = 0; x < 20; x++)
            {
                screen[y, x] = (byte)(x == 19 ? 6 : 5);
                screen[y, 239-x] = screen[y, x];
            }
        Logo(screen,GameCom,2);
        Logo(screen,Tiger,225);
    }
}
