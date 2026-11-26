using System;
using System.IO;
using System.Reflection;

internal static class Program
{
    static readonly Cartridge SupportedCartridge = new Cartridge("Duke Nukem 3D (USA)", 2097152,
        "faf6912c5094477fa83a19864f43eb2acd5929166d8b9267689d6661f5ceb5bd");

    static long BuildDuke(byte[] cart, MemoryStream rom)
    {
        using (Stream runtime = Assembly.GetExecutingAssembly().GetManifestResourceStream("runtime"))
            runtime.CopyTo(rom);
        var output = new BinaryWriter(rom);
        Tiles.Palette(output, 0, 0xdfff8f, 0x6f8f4f, 0x0f4f2f, 0, 0xb3b4af, 0x292a2a);
        return Duke.WriteOpening(cart, output);
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
        if (args.Length != 1)
        {
            Console.Error.WriteLine("Usage: gamecom2gba INPUT.zip|INPUT.tgc|INPUT.bin");
            Console.Error.WriteLine("Creates duke3d-12-maps.gba and duke3d-15-maps.gba next to the executable.");
            return 2;
        }
        var directory = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
        var standard = Path.Combine(directory, "duke3d-12-maps.gba");
        var extended = Path.Combine(directory, "duke3d-15-maps.gba");
        string suffix = "." + Guid.NewGuid().ToString("N") + ".tmp";
        var standardTemp = standard + suffix;
        var extendedTemp = extended + suffix;
        try
        {
            var input = Path.GetFullPath(args[0]);
            if (String.Equals(input, standard, StringComparison.OrdinalIgnoreCase) ||
                String.Equals(input, extended, StringComparison.OrdinalIgnoreCase))
                throw new IOException("Input and output must be different files.");
            var cart = SupportedCartridge.Open(input);
            using (var rom = new MemoryStream())
            {
                long bonusFlagsOffset = BuildDuke(cart, rom);
                using (var file = new FileStream(standardTemp, FileMode.CreateNew, FileAccess.Write))
                    rom.WriteTo(file);
                // The campaign header stores the mask as a little-endian word.
                rom.Position = bonusFlagsOffset;
                rom.WriteByte(0x07);
                using (var file = new FileStream(extendedTemp, FileMode.CreateNew, FileAccess.Write))
                    rom.WriteTo(file);
            }
            Publish(standardTemp, standard);
            Publish(extendedTemp, extended);
            Console.WriteLine(standard);
            Console.WriteLine(extended);
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e.Message); return 1; }
        finally
        {
            try { File.Delete(standardTemp); }
            finally { File.Delete(extendedTemp); }
        }
    }
}
