using System;
using System.Collections.Generic;
using System.IO;

// The starting street of Hollywood Holocaust. CPU window addresses are kept
// in the same notation as the annotated cartridge banks in Ghidra.
internal sealed class Street
{
    readonly byte[] cart;
    readonly int room;
    readonly List<int> items=new List<int>();
    readonly int sceneryLevel;
    readonly TileAtlas backgrounds, sprites;
    internal Street(byte[] rom, int level, int number, TileAtlas backgrounds, TileAtlas sprites)
    {
        cart = rom;
        this.backgrounds=backgrounds;
        this.sprites=sprites;
        room = Word(0x27, Word(0x27, 0x8000+2*level)+2*number);
        sceneryLevel=Word(0x28,Word(0x28,0x8000)+2*level);
        int list=Word(0x22,Word(0x22,0x77c4)+2*level);
        for (int p=list; Read(0x22,p)!=255; p+=5)
            if (Read(0x22,p)==Read(0x27,room+16)) items.Add(p);
        if (items.Count>32) throw new InvalidDataException("Too many collectible items in room");
    }

    int Read(int bank, int address) { return cart[bank * 8192 + (address & 8191)]; }
    int Word(int bank, int address) { return Read(bank,address)*256 + Read(bank,address+1); }
    int Width { get { return Read(0x27,room); } }
    int Length { get { return Read(0x27,room+1); } }

    void Blit(byte[,] scene, int x, int y, int page, int sx, int sy,
              int w, int h, bool flip, bool keyed, bool mask=false)
    {
        for (int dy = Math.Max(0,-y); dy < Math.Min(h,136-y); dy++)
            for (int dx = Math.Max(0,-x); dx < Math.Min(w,200-x); dx++)
            {
                int u = sx + (flip ? w-1-dx : dx);
                byte color = Tiles.Pixel(cart, page, u & 255, (sy+dy) & 255);
                if (mask && Tiles.Pixel(cart,page+1,u & 255,(sy+dy) & 255)!=1) {
                    scene[y+dy,20+x+dx]=1;
                    continue;
                }
                if (!keyed || color != 1) scene[y+dy,20+x+dx] = color;
            }
    }

    void Walls(byte[,] scene, int lane, int depth, int distance)
    {
        int style = Read(0x27,room+15);
        int bank = style <= 3 ? 0x24 : 0x25;
        int views = Word(0x21,0x644e+style*2);
        int edge = Math.Min(lane, Width+1-lane);
        int table = Word(bank,views+2*Math.Min(edge-1,2));
        int phase=3*(distance-1)+depth%3;
        int list = Width==1
            ? Word(bank,Word(0x21,0x643a+style*2)+2*(4*phase+((Read(0x27,room+2)>>4)&3)))
            : Word(bank,table+2*phase);
        int shift = edge > 2 && (lane & 1) != 0 ? Read(bank,list+4)/2 : 0;
        bool mirror = Width>1 && lane >= Width-1;
        for (;; list += 8)
        {
            int flags = Read(bank,list+7);
            int x = Read(bank,list)-shift, y = Read(bank,list+1);
            int w = Read(bank,list+4), h = Read(bank,list+5);
            bool flip = ((flags & 64) != 0) != mirror;
            if (mirror) x = 200-x-w;
            int sx = Read(bank,list+2), sy = Read(bank,list+3), page = Read(bank,list+6);
            Blit(scene,x,y,page,sx,sy,w,h,flip,(flags & 1)!=0);
            if (shift != 0 && x < 0) Blit(scene,x+200,y,page,sx,sy,w,h,flip,(flags & 1)!=0);
            if ((flags & 128) != 0) return;
        }
    }

    void Object(byte[,] scene, int descriptor, int x, int y, bool flip)
    {
        Blit(scene,x,y,Read(0x26,descriptor+4),Read(0x26,descriptor+2),Read(0x26,descriptor+3),
             Read(0x26,descriptor),Read(0x26,descriptor+1),flip,true);
    }

    void Scenery(byte[,] scene, int lane, int depth, int distance)
    {
        int columns = Word(0x28,sceneryLevel+2*Read(0x27,room+16));
        for (int col=columns; Read(0x28,col)!=255; col+=3)
        {
            if (Read(0x28,col) != lane) continue;
            for (int p=Word(0x28,col+1); Read(0x28,p)!=255; p+=3)
            {
                if (Read(0x28,p) != depth+distance) continue;
                int variants=Word(0x28,p+1);
                int variant=Read(0x22,0x79e1+distance-1);
                if (variant > Read(0x28,variants)) continue;
                int record=variants+1+5*variant;
                int x=Read(0x28,record+2), flags=Read(0x28,record+4);
                if ((flags & 1)!=0) x-=256;
                Object(scene,Word(0x28,record),x,Read(0x28,record+3),(flags & 2)!=0);
            }
            return;
        }
    }

    void WriteItems(BinaryWriter output, int lane, int depth)
    {
        long start=output.BaseStream.Position;
        int count=0;
        output.Write(count);
        for (int distance=1; distance<=4; distance++)
        for (int index=items.Count-1; index>=0; index--)
        {
            int p=items[index];
            if (Read(0x22,p+1)!=depth+distance) continue;
            int type=Read(0x22,p+4);
            int poses=Word(0x22,0x7987+2*(type-1));
            int sprite=Word(0x22,poses+2*(distance-1));
            int objectLane=Read(0x22,p+2);
            int offset=50*(objectLane-lane);
            if (objectLane != lane && objectLane == 1) offset+=22;
            if (objectLane != lane && objectLane == Width) offset-=22;
            int x=Read(0x22,p+3)+offset;
            int y=Read(0x22,0x6a3b+distance);
            int left=x-Read(0x26,sprite)/2, top=y-Read(0x26,sprite+1);
            int right=Math.Min(200,left+Read(0x26,sprite)), bottom=Math.Min(136,y);
            left=Math.Max(0,left);
            top=Math.Max(0,top);
            if (left>=right || top>=bottom) continue;
            var ink=new byte[160,240];
            Object(ink,sprite,x-Read(0x26,sprite)/2,y-Read(0x26,sprite+1),false);
            var inFront=new byte[160,240];
            for (int near=distance-1; near>=1; near--) Scenery(inFront,lane,depth,near);
            for (int sy=top; sy<bottom; sy+=32)
                for (int sx=left; sx<right; sx+=32) {
                    var pixels=new byte[32,32];
                    bool visible=false;
                    for (int row=sy; row<Math.Min(bottom,sy+32); row++)
                        for (int col=sx; col<Math.Min(right,sx+32); col++)
                            if (inFront[row,20+col]==0 && ink[row,20+col]!=0) {
                                pixels[row-sy,col-sx]=ink[row,20+col];
                                visible=true;
                            }
                    if (!visible) continue;
                    output.Write((byte)index);
                    output.Write((byte)(20+sx));
                    output.Write((byte)sy);
                    output.Write((byte)0);
                    using (var buffer=new MemoryStream(512))
                    using (var image=new BinaryWriter(buffer)) {
                        Tiles.Write(image,pixels);
                        output.Write(sprites.Add(buffer.ToArray()));
                    }
                    count++;
                }
        }
        if (count>40) throw new InvalidDataException("Pickup sprites exceed GBA object VRAM");
        long end=output.BaseStream.Position;
        output.BaseStream.Position=start;
        output.Write(count);
        output.BaseStream.Position=end;
    }

    void Doors(byte[,] scene, int lane, int distance, bool opened)
    {
        if (distance>3) return;
        for (int door=0; door<3; door++) {
            int spec=Read(0x27,room+4+door);
            if (spec==255) continue;
            int position=spec&31, offset=lane-position;
            int kind=Read(0x27,room+7+door)==254 ? 9 : Read(0x27,room+17);
            int frame=opened ? 3 : 3-((spec>>5)&3);
            if (kind==0) {
                if (Math.Abs(offset)>3) continue;
                if (distance!=1 || offset!=0) {
                    frame=distance==1 ? 3 : distance==2 ? 10 : 17;
                    frame+=2*Math.Abs(offset)-(offset>0 ? 1 : 0);
                }
            } else {
                if (Math.Abs(offset)>1) continue;
                if (distance!=1 || offset!=0) {
                    frame=(distance-1)*6+(offset==0 ? 0 : distance==1 ? 4 : 1)+(offset<0 ? 1 : 0);
                    if (Width>1 && (position<=2 || position+2>Width) && (distance!=1 || offset==0)) frame+=3;
                }
            }
            int table=Word(0x26,0x8cc2+2*kind);
            int list=Word(0x26,table+2*frame);
            for (;; list+=8) {
                int flags=Read(0x26,list+7), page=Read(0x26,list+6);
                Blit(scene,Read(0x26,list),Read(0x26,list+1),page,Read(0x26,list+2),Read(0x26,list+3),
                    Read(0x26,list+4),Read(0x26,list+5),(flags&64)!=0,(flags&1)!=0,page>=0x58);
                if ((flags&128)!=0) break;
            }
        }
    }

    byte[,] View(int lane, int depth, bool opened=false)
    {
        var scene = new byte[160,240];
        Bezel.Draw(scene);
        Walls(scene,lane,depth,5);
        if (Length-depth < 5) Walls(scene,lane,depth,Length-depth);
        Doors(scene,lane,Length-depth,opened);
        for (int distance=4; distance>0; distance--) {
            Scenery(scene,lane,depth,distance);
        }
        return scene;
    }

    // Emit a hardware tile map plus its dictionary. Identical character data
    // occupies one VRAM slot, leaving the HUD tiles above slot 509 untouched.
    void WriteView(BinaryWriter output, byte[,] scene)
    {
        var ids = new Dictionary<int,ushort>();
        var references=new List<int>();
            var map = new ushort[17*32];
            for (int ty=0; ty<17; ty++)
                for (int tx=0; tx<30; tx++)
                {
                    var tile = new byte[32];
                    for (int y=0; y<8; y++)
                        for (int x=0; x<8; x+=2) {
                            int row=8*ty+y, col=8*tx+x;
                            tile[y*4+x/2]=(byte)(scene[row,col] | scene[row,col+1]<<4);
                        }
                    int key=backgrounds.Add(tile);
                    ushort id;
                    if (!ids.TryGetValue(key,out id)) {
                        id=(ushort)ids.Count;
                        ids.Add(key,id);
                        references.Add(key);
                    }
                    map[ty*32+tx]=id;
                }
            output.Write(references.Count);
            foreach (ushort id in map) output.Write(id);
            foreach (int reference in references) output.Write(reference);
    }

    internal void Write(BinaryWriter output)
    {
        long start=output.BaseStream.Position;
        output.Write((byte)Width);
        output.Write((byte)Length);
        output.Write((byte)Read(0x27,room+18));
        output.Write((byte)Read(0x27,room+2));
        for (int door=0; door<3; door++) {
            output.Write((byte)Read(0x27,room+4+door));
            output.Write((byte)Read(0x27,room+7+door));
            output.Write((byte)Read(0x27,room+10+door));
            output.Write((byte)0);
        }
        output.Write((byte)Read(0x27,room+13));
        output.Write((byte)Read(0x27,room+14));
        output.Write((ushort)0);
        long openTable=output.BaseStream.Position;
        for (int i=0; i<3; i++) output.Write(0);
        output.Write(items.Count);
        foreach (int p in items) {
            output.Write((byte)Read(0x22,p+1));
            output.Write((byte)Read(0x22,p+2));
            output.Write((byte)Read(0x22,p+3));
            output.Write((byte)Read(0x22,p+4));
        }
        long table=output.BaseStream.Position;
        for (int i=0; i<Width*Length; i++) output.Write(0);
        for (int z=0; z<Length; z++)
            for (int x=1; x<=Width; x++) {
                long view=output.BaseStream.Position;
                output.BaseStream.Position=table+4*(z*Width+x-1);
                output.Write((uint)(view-start));
                output.BaseStream.Position=view;
                WriteView(output,View(x,z));
                WriteItems(output,x,z);
            }
        for (int i=0; i<3; i++) {
            int spec=Read(0x27,room+4+i);
            if (spec==255) continue;
            long view=output.BaseStream.Position;
            output.BaseStream.Position=openTable+4*i;
            output.Write((uint)(view-start));
            output.BaseStream.Position=view;
            WriteView(output,View(spec&31,Length-1,true));
            WriteItems(output,spec&31,Length-1);
        }
    }
}
