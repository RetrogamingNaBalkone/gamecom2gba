using System;
using System.Collections.Generic;
using System.IO;

internal sealed class StreetAssets
{
    readonly byte[] cart;
    readonly BinaryWriter output;
    readonly long start;
    readonly Dictionary<int,int> graphics = new Dictionary<int,int>();
    readonly Dictionary<int,int> poses = new Dictionary<int,int>();
    readonly Dictionary<int,int> sequences = new Dictionary<int,int>();
    readonly Dictionary<int,int> planes = new Dictionary<int,int>();
    readonly TileAtlas roomTiles = new TileAtlas();

    StreetAssets(byte[] cart, BinaryWriter output)
    {
        this.cart = cart;
        this.output = output;
        start = output.BaseStream.Position;
    }

    int Word(int p) { return cart[p] * 256 + cart[p+1]; }
    static int At(int bank, int cpu) { return bank * 8192 + (cpu & 8191); }
    int Position { get { return checked((int)(output.BaseStream.Position - start)); } }
    void Align() { while ((Position & 3) != 0) output.Write((byte)0); }
    void Set(int pos, int value)
    {
        long end = output.BaseStream.Position;
        output.BaseStream.Position = start + pos;
        output.Write(value);
        output.BaseStream.Position = end;
    }

    int Plane(int bank, int ptr, bool foreground)
    {
        int key=At(bank,ptr)*2+(foreground?1:0), cached;
        if (planes.TryGetValue(key,out cached)) return cached;
        var pixels = new byte[160,200];
        if (!foreground)
            for (int y=0; y<160; ++y) for (int x=0; x<200; ++x) pixels[y,x]=1;
        int list=At(bank,ptr), count=ptr==0xff?0:cart[list++];
        if (count==0 && foreground) return 0;
        // Bank 24:7575 reads seven-byte placements and selects palette via the
        // six-byte bank-29 descriptor. A horizontal mirror uses display X.
        for (int i=0; i<count; ++i,list+=7) {
            int g=At(0x29,Word(list)), sx=cart[g+3], sy=cart[g+4];
            int dx=cart[list+2], dy=cart[list+3], w=cart[list+4], h=cart[list+5];
            int flags=cart[list+6], palette;
            switch (cart[g]) {
                case 1: palette=0xe0; break;
                case 2: palette=0xc4; break;
                case 3: palette=0x24; break;
                default: palette=0xe4; break;
            }
            for (int y=0; y<h && dy+y<160; ++y)
                for (int x=0; x<w && dx+x<200; ++x) {
                    int u=sx+((flags&64)!=0?w-1-x:x), v=sy+((flags&128)!=0?h-1-y:y);
                    int raw=Tiles.Pixel(cart,cart[g+5],u&255,v&255)-1;
                    if (raw!=0 || (flags&1)==0) pixels[dy+y,dx+x]=(byte)(1+((palette>>(2*raw))&3));
                }
        }
        Align();
        int address=Position, total=0;
        var atlas=new TileAtlas();
        var tileIds=new List<int>();
        var map=new ushort[500];
        for (int i=0; i<500; ++i) {
            byte[] tile=new byte[32];
            for (int y=0; y<8; ++y)
                for (int x=0; x<8; x+=2) {
                    int u=i%25*8+x,v=i/25*8+y;
                    tile[y*4+x/2]=(byte)(pixels[v,u]|pixels[v,u+1]<<4);
                }
            map[i]=(ushort)atlas.Add(tile);
            if (map[i]==tileIds.Count) tileIds.Add(roomTiles.Add(tile));
            total=Math.Max(total,map[i]+1);
        }
        if (total>(foreground?292:512)) throw new InvalidDataException("RE2 room exceeds its GBA tile bank at "+key.ToString("X"));
        output.Write(total);
        foreach (ushort tile in map) output.Write(tile);
        for (int first=0;first<tileIds.Count;) {
            int run=1;
            while (run<256 && first+run<tileIds.Count &&
                   tileIds[first+run]==tileIds[first]+run) ++run;
            output.Write(tileIds[first] | ((run-1)<<24));
            first+=run;
        }
        planes.Add(key,address);
        return address;
    }

    int Graphic(int ptr)
    {
        int id;
        if (graphics.TryGetValue(ptr,out id)) return id;
        int g=At(ptr>=0xa000?0x30:0x25,ptr), w=cart[g],h=cart[g+1];
        Align(); id=Position; graphics.Add(ptr,id);
        output.Write((ushort)w); output.Write((ushort)h);
        for (int by=0; by<h; by+=32) for (int bx=0; bx<w; bx+=32) {
            var piece=new byte[32,32];
            for (int y=0; y<32 && by+y<h; ++y) for (int x=0; x<32 && bx+x<w; ++x) {
                int raw=Tiles.Pixel(cart,cart[g+4],(cart[g+2]+bx+x)&255,(cart[g+3]+by+y)&255)-1;
                if (raw!=0) piece[y,x]=(byte)(1+((0xe0>>(2*raw))&3));
            }
            Tiles.Write(output,piece);
        }
        return id;
    }

    int Pose(int kind,int bank,int ptr)
    {
        int p=At(bank,ptr), result, key=p*16+kind;
        if (poses.TryGetValue(key,out result)) return result;
        var offsets=new List<int>();
        var positions=new List<int>();
        int cursor=p+3;
        for (int part=3;part>0;--part,cursor+=5) {
            // 21:64DD switches one body part to a variant beyond the FE
            // marker for actors which share the same animation skeleton.
            int selected=cursor;
            if (cart[At(0x21,0x650b+kind)]==part) {
                while (cart[selected]!=0xfe) selected+=5;
                selected+=1+5*cart[At(0x21,0x6518+kind)];
            }
            if (cart[selected]==0xfe) break;
            positions.Add(selected);
            offsets.Add(Graphic(Word(selected+3)));
            if (part==1 && cart[cursor+5]==0xff) { cursor++; part=4; }
        }
        Align(); result=Position; poses.Add(key,result);
        output.Write(cart[p]); output.Write(cart[p+1]); output.Write(cart[p+2]); output.Write((byte)offsets.Count);
        for (int i=0;i<offsets.Count;++i) {
            int s=positions[i];
            output.Write(cart[s]); output.Write(cart[s+1]); output.Write(cart[s+2]); output.Write((byte)0);
            output.Write(offsets[i]);
        }
        return result;
    }

    int Sequence(int kind,int action,int depth)
    {
        int table=Word(At(0x21,0x6821+2*kind));
        int row=Word(At(0x21,table+2*action));
        int s=At(0x21,row+3*depth);
        int bank=cart[s], p=At(bank,Word(s+1)),result,key=(p*16+kind)*168+action;
        if (sequences.TryGetValue(key,out result)) return result;
        var parts=new List<int>();
        for (int f=p;cart[f]!=0x80;f+=4) {
            if (parts.Count==128) throw new InvalidDataException("Unterminated RE2 actor sequence.");
            parts.Add(Pose(kind,bank,Word(f+2)));
        }
        Align(); result=Position; sequences.Add(key,result);
        output.Write(parts.Count);
        for (int i=0;i<parts.Count;++i) {
            output.Write(cart[p+4*i]); output.Write(cart[p+4*i+1]);
            output.Write((byte)AudioCue(kind,action,i,true));
            output.Write((byte)AudioCue(kind,action,i,false));
            output.Write(parts[i]);
        }
        return result;
    }

    // 20:48D8 traverses one FF-delimited row per action. FD supplies
    // separate live/dead cue masks, while FE with no mask is unconditional.
    int AudioCue(int kind,int action,int frame,bool alive)
    {
        int ptr=Word(At(0x20,0x4977+kind*2));
        if (ptr==0 || kind==2) return 0;
        int row=At(0x20,ptr);
        for (int i=0;i<=action;++i) {
            while (cart[row]!=255 && cart[row]!=0) ++row;
            if (cart[row]==0) return 0;
            ++row;
            if (i!=action) {
                while (cart[row]!=255 && !(cart[row]==0 && cart[row+1]==0)) ++row;
            }
        }
        if (cart[row]==0xfd) {
            ++row;
            if (alive) {
                while (cart[row]!=0xfe) ++row;
                row+=2;
            }
        }
        bool match=cart[row]==0xfe;
        while (cart[row]!=0xfe) {
            if (cart[row++]==frame) match=true;
        }
        int cue=cart[row+1];
        return match && cue<=24 ? cue:0;
    }

    void Export()
    {
        // Seven two-level camera directories, followed by fixed-size room
        // records. Area 5's directory sits between areas 1 and 2.
        var directories=new int[7];
        var ends=new List<int>();
        for (int i=0;i<7;++i) {
            directories[i]=Word(At(0x28,0x8000+2*i));
            ends.Add(directories[i]);
        }
        ends.Add(Word(At(0x28,directories[0])));
        ends.Sort();
        for (int i=0;i<9;++i) output.Write(0);
        int actors=Position;
        Set(0,actors);
        for (int i=0;i<13*168*3;++i) output.Write(0);
        for (int area=0;area<7;++area) {
            Align(); Set(4+4*area,Position);
            int rooms=(ends[ends.IndexOf(directories[area])+1]-directories[area])/2;
            output.Write(rooms);
            int cameras=Position;
            for (int i=0;i<rooms*3;++i) output.Write(0);
            for (int i=0;i<rooms;++i) {
                // Four aliased area-3 records refer to invalid placement
                // lists in the bank that 24:74BA selects for area 6.
                if (area==6 && i>=19 && i<=22) continue;
                int room=At(0x28,Word(At(0x28,directories[area]+2*i)));
                int bank=RoomBank(area,i);
                Set(cameras+12*i,Plane(bank,Word(room),false));
                Set(cameras+12*i+4,Plane(bank,Word(room+6),true));
                Set(cameras+12*i+8,Position);
                int spawns=At(0x36,Word(At(0x36,directories[area]+2*i)));
                while (cart[spawns]!=255) {
                    output.Write(cart,spawns,4);
                    spawns+=4;
                }
                output.Write((byte)255);
                if (area==6) {
                    int chaseDir=Word(At(0x36,0x86f8+2*area));
                    int entry=At(0x36,Word(At(0x36,chaseDir+2*i)));
                    while (cart[entry]!=255) {
                        output.Write(cart,entry,3);
                        entry+=3;
                    }
                    output.Write((byte)255);
                }
            }
        }
        int[] counts={87,140,87,87,140,140,10,18,16,38,8,20,10};
        Align();
        Set(32,Position);
        roomTiles.Write(output);
        for (int kind=1;kind<=counts.Length;++kind) {
            // No start-of-room record uses these alternate body selectors.
            if (kind>=4 && kind<=6) continue;
            for (int action=0;action<counts[kind-1];++action) for (int depth=0;depth<3;++depth) {
                // Bank 2E:9DD1 is a dangling third-depth slot for these
                // two zombie actions. The other depths are valid.
                if ((kind==1 || kind==3 || kind==4) && depth==2 && (action==72 || action==85)) continue;
                Set(actors+4*(((kind-1)*168+action)*3+depth),Sequence(kind,action,depth));
            }
        }
    }

    // Cartridge routine 24:7493 maps the active camera's placement bank.
    static int RoomBank(int area,int camera)
    {
        switch (area) {
            case 0: case 4: return camera<0x2c?0x2a:camera<0x48?0x2c:0x32;
            case 1: return camera<0x27?0x2b:0x2c;
            case 3: return 0x2d;
            case 5: return camera<0x3f?0x31:0x2d;
            case 6: return camera<0x1b?0x32:0x29;
            default: return 0x2b;
        }
    }

    internal static void Write(byte[] cart,BinaryWriter output)
    {
        new StreetAssets(cart,output).Export();
    }
}
