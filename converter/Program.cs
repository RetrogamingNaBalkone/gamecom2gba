using System;
using System.IO;
using System.Reflection;

internal static class Program
{
    static readonly Cartridge Duke = new Cartridge("Duke Nukem 3D (USA)", 2097152,
        "faf6912c5094477fa83a19864f43eb2acd5929166d8b9267689d6661f5ceb5bd");
    static readonly Cartridge ResidentEvil = new Cartridge("Resident Evil 2 (USA)", 2097152,
        "80d449b89d02ba07ec386c54724e02033eb5373cb6621d5762f7184029ff2cac");
    static readonly Cartridge Indy500 = new Cartridge("Indy 500 (USA)", 1048576,
        "58cc3a3a4c33fec49bb9260ae83650a6d22b1004b4767a61307d197f6e423d5a");
    static readonly Cartridge SystemFontBios = new Cartridge("game.com US external BIOS",262144,
        "4cade226b44f09ebf5897b137d5564e5579b4d11b23dcabacd308fb0d18e7077");

    internal static void Publish(string temporary,string path)
    {
        for (int attempt=0;;attempt++) {
            try {
                if (File.Exists(path)) File.Replace(temporary,path,null);
                else File.Move(temporary,path);
                return;
            } catch (IOException error) {
                int code=error.HResult&0xffff;
                // A Windows scanner may hold the destination briefly. A second
                // converter can also create it between Exists and Move.
                if (attempt==4 || !File.Exists(temporary) ||
                    (code!=32 && code!=33 && code!=80 && code!=183 && code!=1175)) throw;
                System.Threading.Thread.Sleep(50<<attempt);
            }
        }
    }

    static int Main(string[] args)
    {
        if (args.Length < 1 || args.Length > 2)
        {
            Console.Error.WriteLine("Usage: gamecom2gba INPUT.zip|INPUT.tgc|INPUT.bin [EXTERNAL-BIOS.zip|EXTERNAL-BIOS.bin]");
            Console.Error.WriteLine("Creates the GBA image(s) for the supplied game next to the executable.");
            Console.Error.WriteLine("Resident Evil 2 can use the external BIOS for the original system font.");
            Console.Error.WriteLine("Indy 500 requires the US external BIOS.");
            return 2;
        }
        try
        {
            string input = Path.GetFullPath(args[0]);
            string directory = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            Cartridge identified;
            byte[] cart = Cartridge.Open(input, out identified, Duke, ResidentEvil, Indy500);
            if (identified == Indy500) {
                if (args.Length != 2) throw new ArgumentException("Indy 500 needs the US external BIOS as the second input.");
                string biosPath = Path.GetFullPath(args[1]);
                Cartridge matched;
                byte[] bios = Cartridge.Open(biosPath, out matched, SystemFontBios);
                IndyConversion.Write(cart, bios, directory, input, biosPath);
            } else if (identified == Duke) {
                if (args.Length!=1) throw new ArgumentException("Duke Nukem 3D takes only a cartridge dump.");
                DukeConversion.Write(cart, directory, input);
            } else {
                byte[] fontBios=null;
                if (args.Length==2) {
                    string biosPath=Path.GetFullPath(args[1]);
                    if (String.Equals(biosPath,Path.Combine(directory,"resident-evil-2.gba"),StringComparison.OrdinalIgnoreCase))
                        throw new IOException("BIOS input and output must be different files.");
                    Cartridge matched;
                    fontBios=Cartridge.Open(biosPath,out matched,SystemFontBios);
                }
                ResidentEvilConversion.Write(cart,directory,input,fontBios);
            }
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error.Message);
            return 1;
        }
    }
}
