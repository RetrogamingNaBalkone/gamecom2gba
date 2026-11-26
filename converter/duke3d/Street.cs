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
    readonly int sceneryBank;
    readonly int stageId;
    readonly BackgroundAtlas backgrounds;
    readonly TileAtlas sprites;
    internal Street(byte[] rom, int level, int number, BackgroundAtlas backgrounds, TileAtlas sprites)
    {
        cart = rom;
        this.backgrounds=backgrounds;
        this.sprites=sprites;
        room = Word(0x27, Word(0x27, 0x8000+2*level)+2*number);
        int episode=level/5, stage=level%5;
        stageId=((episode+1)<<4)|(stage+1);
        sceneryBank=episode==0 ? 0x28 : 0x2a;
        sceneryLevel=Word(sceneryBank,Word(0x28,0x8000+2*episode)+2*stage);
        int list=Word(0x22,Word(0x22,0x77c4+2*episode)+2*stage);
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

    bool Scenery(byte[,] scene, int lane, int depth, int distance, List<byte> bounds=null)
    {
        bool drew=false;
        int set=Read(0x27,room+16);
        if ((set&0xf0)!=0) return false;
        int columns = Word(sceneryBank,sceneryLevel+2*set);
        // Dark Side room 17 has no cartridge-backed scenery list.
        if (columns<0x8000 || columns>=0xa000) return false;
        for (int col=columns; Read(sceneryBank,col)!=255; col+=3)
        {
            if (Read(sceneryBank,col) != lane) continue;
            for (int p=Word(sceneryBank,col+1); Read(sceneryBank,p)!=255; p+=3)
            {
                if (Read(sceneryBank,p) != depth+distance) continue;
                int variants=Word(sceneryBank,p+1);
                int variant=Read(0x22,0x79e1+distance-1);
                if (variant > Read(sceneryBank,variants)) continue;
                int record=variants+1+5*variant;
                int x=Read(sceneryBank,record+2), flags=Read(sceneryBank,record+4);
                if ((flags & 1)!=0) x-=256;
                int descriptor=Word(sceneryBank,record);
                if (bounds!=null) {
                    int left=Math.Max(0,x),right=Math.Min(200,x+Read(0x26,descriptor));
                    if (right>left) bounds.AddRange(new byte[]{(byte)distance,(byte)left,(byte)right,
                        (byte)Read(sceneryBank,record+3),(byte)Read(0x26,descriptor+1)});
                }
                if (scene!=null) {
                    Object(scene,descriptor,x,Read(sceneryBank,record+3),(flags & 2)!=0);
                    drew=true;
                }
                else if (bounds==null && x<150 && (x>=50 || x+Read(0x26,descriptor)>=60)) return true;
            }
            break;
        }
        return drew;
    }

    void WriteOcclusion(BinaryWriter output,int lane,int depth)
    {
        long start=output.BaseStream.Position;
        output.Write(0);
        int count=0;
        var bounds=new List<byte>();
        for (int distance=1;distance<4;distance++) {
            var ink=new byte[160,240];
            if (!Scenery(ink,lane,depth,distance,bounds)) continue;
            for (int y=0;y<136;y+=8)
                for (int x=0;x<200;x+=8) {
                    ulong mask=0;
                    for (int dy=0;dy<8;dy++)
                        for (int dx=0;dx<8;dx++)
                            if (ink[y+dy,20+x+dx]!=0) mask|=1UL<<(8*dy+dx);
                    if (mask==0) continue;
                    output.Write((byte)x);
                    output.Write((byte)y);
                    output.Write((ushort)distance);
                    output.Write(mask);
                    count++;
                }
        }
        Scenery(null,lane,depth,4,bounds);
        output.Write(bounds.Count/5);
        output.Write(bounds.ToArray());
        while ((output.BaseStream.Position&3)!=0) output.Write((byte)0);
        long end=output.BaseStream.Position;
        output.BaseStream.Position=start;
        output.Write(count);
        output.BaseStream.Position=end;
    }

    bool Blocked(int lane,int depth)
    {
        bool overrideRoom=false;
        for (int p=0x6ec7; Read(0x23,p)!=255; p+=4) {
            if (Read(0x23,p)!=stageId || Read(0x23,p+1)!=Read(0x27,room+16)) continue;
            overrideRoom=true;
            if (Read(0x23,p+2)==lane && Read(0x23,p+3)==((depth-1)&255)) return true;
        }
        return !overrideRoom && Scenery(null,lane,depth-1,1);
    }

    void WriteActors(BinaryWriter output)
    {
        int set=Read(0x27,room+16);
        int episode=(stageId>>4)-1, stage=(stageId&15)-1;
        int list=Word(0x29,Word(0x29,0x8000+2*episode)+2*stage);
        int choices=stageId==0x11 ? 2 : stageId==0x12 ? 3 : 4;
        var spawns=new List<int>();
        int rng=0xa59b;
        if ((set&0xf0)==0) {
            for (int p=list; Read(0x29,p)!=255; p+=3) {
                if (Read(0x29,p)==set)
                    spawns.Add(Spawn(Read(0x29,p+1),Read(0x29,p+2),rng%choices));
                rng=NextSeed(rng);
            }
        } else {
            for (int z=3;z<Length;z+=5) {
                int type=spawns.Count==0 ? (set&3)%choices : rng%choices;
                spawns.Add(Spawn(1,z,type));
                rng=NextSeed(rng);
            }
        }
        if (spawns.Count>18) throw new InvalidDataException("Actor list exceeds the room limit");
        output.Write(spawns.Count);
        foreach (int spawn in spawns) output.Write(spawn);
    }

    int Spawn(int lane,int depth,int type)
    {
        int worldX=Word(0x22,0x79ed+2*lane)+(lane==1 ? 80 : 10);
        return worldX|(depth<<16)|(type<<24)|(lane<<26);
    }

    // 20:50D0, seeded at the first Hollywood Holocaust spawn of the reference run.
    static int NextSeed(int seed)
    {
        int hi=seed>>8,lo=seed&255;
        int rotated=(hi<<1)&255;
        int sum=lo+rotated+(hi>>7);
        sum=(sum&255)+0x90+(sum>>8);
        return (((rotated+(sum&255)+(sum>>8))&255)<<8)|(sum&255);
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
                    output.Write((byte)distance);
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

    void Doors(byte[,] scene, int lane, int distance, bool opened,int exitFrame=-1)
    {
        if (distance>3) return;
        for (int door=0; door<3; door++) {
            int spec=Read(0x27,room+4+door);
            if (spec==255) continue;
            int position=spec&31, offset=lane-position;
            int kind=Read(0x27,room+7+door)==254 ? 9 : Read(0x27,room+17);
            int frame=3-((spec>>5)&3);
            if (opened) frame=frame==3 ? 0 : 3;
            if (exitFrame>=0 && kind==9 && offset==0 && distance==1) frame=exitFrame;
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

    void WriteLayers(BinaryWriter writer,int lane,int depth,bool opened)
    {
        var walls=new byte[160,240];
        Bezel.Draw(walls);
        Walls(walls,lane,depth,5);
        if (Length-depth<5) Walls(walls,lane,depth,Length-depth);
        writer.Write(backgrounds.AddLayer(walls,136));

        int doors=backgrounds.Empty;
        if (Length-depth<=3) {
            var layer=new byte[136,240];
            Doors(layer,lane,Length-depth,opened,opened ? 1 : -1);
            doors=backgrounds.AddLayer(layer,136);
        }
        writer.Write(doors);
        var scenery=new byte[136,240];
        bool visible=false;
        for (int distance=4;distance>0;distance--)
            visible|=Scenery(scenery,lane,depth,distance);
        writer.Write(visible ? backgrounds.AddLayer(scenery,136) : backgrounds.Empty);
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
        output.Write((byte)Read(0x27,room+16));
        output.Write((byte)stageId);
        long openTable=output.BaseStream.Position;
        for (int i=0; i<3; i++) output.Write(0);
        output.Write(items.Count);
        foreach (int p in items) {
            output.Write((byte)Read(0x22,p+1));
            output.Write((byte)Read(0x22,p+2));
            output.Write((byte)Read(0x22,p+3));
            output.Write((byte)Read(0x22,p+4));
        }
        WriteActors(output);
        long table=output.BaseStream.Position;
        for (int i=0; i<Width*Length; i++) output.Write(0);
        var views=new Dictionary<string,uint>();
        for (int z=0; z<Length; z++)
            for (int x=1; x<=Width; x++)
                StoreView(output,views,start,table+4*(z*Width+x-1),x,z,false);
        for (int i=0; i<3; i++) {
            int spec=Read(0x27,room+4+i);
            if (spec==255) continue;
            StoreView(output,views,start,openTable+4*i,spec&31,Length-1,true);
        }
    }

    void StoreView(BinaryWriter output,Dictionary<string,uint> views,long roomStart,
                   long entry,int x,int z,bool opened)
    {
        using (var buffer=new MemoryStream())
        using (var writer=new BinaryWriter(buffer)) {
            WriteLayers(writer,x,z,opened);
            using (var parts=new MemoryStream())
            using (var records=new BinaryWriter(parts)) {
                WriteItems(records,x,z);
                writer.Write(backgrounds.Add(parts.ToArray()));
                parts.SetLength(0);
                WriteOcclusion(records,x,z);
                writer.Write(backgrounds.Add(parts.ToArray()));
            }
            for (int door=0;opened && door<3;door++) {
                if ((Read(0x27,room+4+door)&31)!=x || Read(0x27,room+7+door)!=254) continue;
                for (int frame=2;frame<=3;frame++) {
                    var layer=new byte[136,240];
                    Doors(layer,x,1,true,frame);
                    writer.Write(backgrounds.AddLayer(layer,136));
                }
                break;
            }
            byte[] bytes=buffer.ToArray();
            string key=Convert.ToBase64String(bytes);
            uint offset;
            if (!views.TryGetValue(key,out offset)) {
                offset=(uint)(output.BaseStream.Position-roomStart);
                views.Add(key,offset);
                output.Write(bytes);
            }
            long end=output.BaseStream.Position;
            output.BaseStream.Position=entry;
            output.Write(offset|(!opened && Blocked(x,z) ? 0x80000000u : 0));
            output.BaseStream.Position=end;
        }
    }
}
