using System;
using System.Collections.Generic;
using System.IO;

// Intern identical records. Sprite descriptors use entry indices; the
// background dictionary uses byte offsets to its variable-size records.
internal sealed class TileAtlas
{
    readonly Dictionary<string,int> lookup=new Dictionary<string,int>();
    readonly List<byte[]> entries=new List<byte[]>();
    readonly List<int> offsets=new List<int>();
    int length;

    internal int Add(byte[] data)
    {
        string key=Convert.ToBase64String(data);
        int id;
        if (!lookup.TryGetValue(key,out id)) {
            id=entries.Count;
            lookup.Add(key,id);
            offsets.Add(length);
            length=checked(length+data.Length);
            entries.Add(data);
        }
        return id;
    }

    internal int AddOffset(byte[] data)
    {
        return offsets[Add(data)];
    }

    internal void Write(BinaryWriter output)
    {
        foreach (byte[] entry in entries) output.Write(entry);
    }

    internal void WriteSprites(BinaryWriter output, BackgroundAtlas backgrounds)
    {
        foreach (byte[] sprite in entries)
            for (int pos=0;pos<sprite.Length;pos+=32)
                output.Write(backgrounds.AddTile(sprite,pos));
    }
}
