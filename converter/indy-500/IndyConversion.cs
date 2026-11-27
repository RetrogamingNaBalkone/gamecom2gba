using System;
using System.IO;
using System.Reflection;

// Writes indy-500.gba: the shared game.com runtime followed by an asset block with
// the verified cartridge, the external BIOS kernel, the LCD palette and the bezel.
internal static class IndyConversion
{
    internal static void Write(byte[] cart, byte[] bios, string directory, string input, string biosInput)
    {
        string path = Path.Combine(directory, "indy-500.gba");
        if (String.Equals(input, path, StringComparison.OrdinalIgnoreCase) ||
            String.Equals(biosInput, path, StringComparison.OrdinalIgnoreCase))
            throw new IOException("Inputs and output must be different files.");
        string temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var file = new FileStream(temp, FileMode.CreateNew, FileAccess.Write))
            using (var output = new BinaryWriter(file))
            {
                using (var runtime = Assembly.GetExecutingAssembly().GetManifestResourceStream("indy-500"))
                    runtime.CopyTo(file);
                long header = file.Position;
                output.Write(new[] { (byte)'G', (byte)'C', (byte)'A', (byte)'S' });
                uint[] offsets = new uint[5];
                foreach (var offset in offsets) output.Write(offset);
                offsets[0] = (uint)(file.Position - header);
                output.Write(cart);
                offsets[1] = (uint)cart.Length;
                offsets[2] = (uint)(file.Position - header);
                output.Write(bios);
                offsets[3] = (uint)(file.Position - header);
                // 0 backdrop, 1-4 current LCD mapping, 5-6 bezel, 8-12 the LCD shades (MAME gamecom palette).
                Tiles.Palette(output, 0, 0xdfff8f, 0x8fcf8f, 0x6f8f4f, 0x0f4f2f, 0xb3b4af, 0x292a2a, 0,
                              0x000000, 0x0f4f2f, 0x6f8f4f, 0x8fcf8f, 0xdfff8f);
                offsets[4] = (uint)(file.Position - header);
                var bezel = new byte[160, 240];
                Bezel.Draw(bezel);
                Tiles.Write(output, bezel);
                while ((file.Position & 3) != 0) output.Write((byte)0);
                file.Position = header + 4;
                foreach (var offset in offsets) output.Write(offset);
            }
            Program.Publish(temp, path);
            Console.WriteLine(path);
        }
        finally { File.Delete(temp); }
    }
}
