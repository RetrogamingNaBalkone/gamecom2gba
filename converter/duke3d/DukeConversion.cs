using System;
using System.IO;
using System.Reflection;

internal static class DukeConversion
{
    static long BuildDuke(byte[] cart, MemoryStream rom)
    {
        using (Stream runtime = Assembly.GetExecutingAssembly().GetManifestResourceStream("runtime"))
            runtime.CopyTo(rom);
        var output = new BinaryWriter(rom);
        Tiles.Palette(output, 0, 0xdfff8f, 0x6f8f4f, 0x0f4f2f, 0, 0xb3b4af, 0x292a2a);
        return Duke.WriteOpening(cart, output);
    }

    internal static void Write(byte[] cart, string directory, string input)
    {
        var standard = Path.Combine(directory, "duke3d-12-maps.gba");
        var extended = Path.Combine(directory, "duke3d-15-maps.gba");
        string suffix = "." + Guid.NewGuid().ToString("N") + ".tmp";
        var standardTemp = standard + suffix;
        var extendedTemp = extended + suffix;
        try
        {
            if (String.Equals(input, standard, StringComparison.OrdinalIgnoreCase) ||
                String.Equals(input, extended, StringComparison.OrdinalIgnoreCase))
                throw new IOException("Input and output must be different files.");
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
            Program.Publish(standardTemp, standard);
            Program.Publish(extendedTemp, extended);
            Console.WriteLine(standard);
            Console.WriteLine(extended);
        }
        finally
        {
            try { File.Delete(standardTemp); }
            finally { File.Delete(extendedTemp); }
        }
    }
}
