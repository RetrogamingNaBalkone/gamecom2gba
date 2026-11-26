using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;

internal sealed class Cartridge
{
    readonly int expectedSize;
    readonly string expectedHash;
    readonly string title;

    internal Cartridge(string title, int expectedSize, string expectedHash)
    {
        this.title = title;
        this.expectedSize = expectedSize;
        this.expectedHash = expectedHash;
    }

    static byte[] Identify(Stream source, int size, Cartridge[] supported, out Cartridge identified)
    {
        identified = null;
        using (var reader = new BinaryReader(source))
        using (var sha = SHA256.Create())
        {
            var data = reader.ReadBytes(size);
            if (data.Length != size || source.ReadByte() != -1) return null;
            var hash = BitConverter.ToString(sha.ComputeHash(data)).Replace("-", "").ToLowerInvariant();
            identified = supported.FirstOrDefault(c => c.expectedSize == size && c.expectedHash == hash);
            return identified == null ? null : data;
        }
    }

    internal static byte[] Open(string path, out Cartridge identified, params Cartridge[] supported)
    {
        identified = null;
        using (var file = File.OpenRead(path))
        {
            if (!String.Equals(Path.GetExtension(path), ".zip", StringComparison.OrdinalIgnoreCase))
            {
                if (!supported.Any(c => c.expectedSize == file.Length))
                    throw new InvalidDataException("Unsupported cartridge size.");
                var data = Identify(file, (int)file.Length, supported, out identified);
                if (data != null) return data;
                throw new InvalidDataException("Unsupported cartridge SHA-256.");
            }
            using (var zip = new ZipArchive(file, ZipArchiveMode.Read))
            {
                foreach (var entry in zip.Entries.Where(e => supported.Any(c => c.expectedSize == e.Length)))
                    using (var stream = entry.Open())
                    {
                        var data = Identify(stream, (int)entry.Length, supported, out identified);
                        if (data != null) return data;
                    }
                throw new InvalidDataException("ZIP contains no supported cartridge. Expected " +
                    String.Join(" or ", supported.Select(c => c.title)) + ".");
            }
        }
    }
}
