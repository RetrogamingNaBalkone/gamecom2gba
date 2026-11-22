using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;

internal static class Cartridge
{
    internal const int DukeSize = 2097152;
    const string DukeHash = "faf6912c5094477fa83a19864f43eb2acd5929166d8b9267689d6661f5ceb5bd";

    static byte[] Identify(Stream source, long size)
    {
        if (size != DukeSize) throw new InvalidDataException("Unsupported cartridge size.");
        using (var reader = new BinaryReader(source))
        using (var sha = SHA256.Create())
        {
            var data = reader.ReadBytes(DukeSize);
            if (data.Length != DukeSize || source.ReadByte() != -1 ||
                BitConverter.ToString(sha.ComputeHash(data)).Replace("-", "").ToLowerInvariant() != DukeHash)
                throw new InvalidDataException("Unsupported cartridge SHA-256. Expected Duke Nukem 3D (USA).");
            return data;
        }
    }

    internal static byte[] Open(string path)
    {
        using (var file = File.OpenRead(path))
        {
            if (!String.Equals(Path.GetExtension(path), ".zip", StringComparison.OrdinalIgnoreCase))
                return Identify(file, file.Length);
            using (var zip = new ZipArchive(file, ZipArchiveMode.Read))
            {
                var candidates = zip.Entries.Where(e => e.Length == DukeSize).ToArray();
                foreach (var entry in candidates)
                    using (var stream = entry.Open())
                        try { return Identify(stream, entry.Length); }
                        catch (InvalidDataException) { continue; }
                throw new InvalidDataException("ZIP contains no supported Duke Nukem 3D (USA) cartridge.");
            }
        }
    }
}
