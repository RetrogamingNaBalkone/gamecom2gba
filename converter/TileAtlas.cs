using System;
using System.Collections.Generic;
using System.IO;

// BG characters (32 bytes) and 32x32 OBJ images (512 bytes) each have
// their own fixed-stride atlas. Maps and OAM still use local VRAM slots.
internal sealed class TileAtlas
{
    readonly Dictionary<string,int> lookup=new Dictionary<string,int>();
    readonly List<byte[]> entries=new List<byte[]>();

    internal int Add(byte[] data)
    {
        string key=Convert.ToBase64String(data);
        int id;
        if (!lookup.TryGetValue(key,out id)) {
            id=entries.Count;
            lookup.Add(key,id);
            entries.Add(data);
        }
        return id;
    }

    internal void Write(BinaryWriter output)
    {
        foreach (byte[] entry in entries) output.Write(entry);
    }
}
