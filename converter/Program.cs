using System;
using System.IO;
using System.Reflection;

internal static class Program
{
    static void WritePreview(byte[] cart, Stream destination, int bonusFlags)
    {
        using (Stream runtime = Assembly.GetExecutingAssembly().GetManifestResourceStream("runtime"))
            runtime.CopyTo(destination);
        using (var output = new BinaryWriter(destination))
        {
            Tiles.Palette(output, 0, 0xdfff8f, 0x6f8f4f, 0x0f4f2f, 0, 0xb3b4af, 0x292a2a);
            Duke.WriteOpening(cart, output, bonusFlags);
        }
    }

    static void Publish(string temporary,string path)
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
        if (!(args.Length == 2 || (args.Length == 4 && args[2] == "--bonus-record"))
            || !args[1].EndsWith(".gba", StringComparison.OrdinalIgnoreCase))
        {
            Console.Error.WriteLine("Usage: gamecom-gba INPUT.zip|INPUT.tgc|INPUT.bin OUTPUT.gba [--bonus-record DUKE_RECORD.bin]");
            return 2;
        }
        string temporary = null;
        try
        {
            var cart = Cartridge.Open(args[0]);
            var path = Path.GetFullPath(args[1]);
            if (String.Equals(path, Path.GetFullPath(args[0]), StringComparison.OrdinalIgnoreCase))
                throw new IOException("Input and output must be different files.");
            int bonusFlags=0;
            if (args.Length==4) {
                if (String.Equals(path,Path.GetFullPath(args[3]),StringComparison.OrdinalIgnoreCase))
                    throw new IOException("Bonus record and output must be different files.");
                bonusFlags=Duke.ReadBonusFlags(cart,args[3]);
            }
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            using (var file = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write))
                WritePreview(cart, file, bonusFlags);
            Publish(temporary,path);
            Console.WriteLine("Duke graphics preview: " + path);
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e.Message); return 1; }
        finally { if (temporary != null && File.Exists(temporary)) File.Delete(temporary); }
    }
}
