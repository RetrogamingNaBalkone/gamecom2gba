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

    byte[] Identify(Stream source, long size)
    {
        if (size != expectedSize) throw new InvalidDataException("Unsupported cartridge size.");
        using (var reader = new BinaryReader(source))
        using (var sha = SHA256.Create())
        {
            var data = reader.ReadBytes(expectedSize);
            if (data.Length != expectedSize || source.ReadByte() != -1 ||
                BitConverter.ToString(sha.ComputeHash(data)).Replace("-", "").ToLowerInvariant() != expectedHash)
                throw new InvalidDataException("Unsupported cartridge SHA-256. Expected " + title + ".");
            return data;
        }
    }

    internal byte[] Open(string path)
    {
        using (var file = File.OpenRead(path))
        {
            if (!String.Equals(Path.GetExtension(path), ".zip", StringComparison.OrdinalIgnoreCase))
                return Identify(file, file.Length);
            using (var zip = new ZipArchive(file, ZipArchiveMode.Read))
            {
                var candidates = zip.Entries.Where(e => e.Length == expectedSize).ToArray();
                foreach (var entry in candidates)
                    using (var stream = entry.Open())
                        try { return Identify(stream, entry.Length); }
                        catch (InvalidDataException) { continue; }
                throw new InvalidDataException("ZIP contains no supported " + title + " cartridge.");
            }
        }
    }
}
