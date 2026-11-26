#include "campaign.h"
#include "progress.h"
#include "puzzle.h"

static const uint8_t *catalog, *scripts, *obstacles;
static const uint8_t *pickup;
static const uint16_t area_start[] = {0,33,109,150,193,305,383,420};

static int quotient(int numerator, int denominator)
{
#if defined(__thumb__)
    register int n __asm__("r0")=numerator, d __asm__("r1")=denominator;
    __asm__ volatile("swi 0x06" : "+r"(n), "+r"(d) : : "r3","cc");
    return n;
#else
    return numerator/denominator;
#endif
}

static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1]<<8; }
static unsigned le32(const uint8_t *p) { return le16(p) | le16(p+2)<<16; }

void re2_campaign_init(const uint8_t *assets)
{
    catalog=assets;
    scripts=assets+le32(assets+4);
    obstacles=assets+le32(assets+8);
    pickup=0;
}

static const uint8_t *view(unsigned area, unsigned camera)
{
    if (!catalog || area>=7 || camera>=(unsigned)(area_start[area+1]-area_start[area]))
        return 0;
    return catalog+88+20*(area_start[area]+camera);
}

int re2_project_x(unsigned area, unsigned cam, int x, unsigned from, unsigned to)
{
    const uint8_t *params=view(area,cam);
    if (!params || to>2 || from>2) return x;
    int pivot=params[0];
    while (from!=to) {
        int right=x>=pivot, inward=from<to;
        unsigned pair=(right?4:0)+2*(inward ? from : from-1);
        int num=params[1+pair], den=params[2+pair];
        int divisor=inward ? den : den-num;
        if (divisor<=0) return x;
        int distance=right ? x-pivot : pivot-x;
        int step=quotient(distance*num,divisor);
        if (!inward) {
            step=quotient(step*17,20);
            x+=right ? step : -step;
            if (x<15) x=15;
            if (x>185) x=185;
            --from;
        } else {
            x+=right ? -step : step;
            ++from;
        }
    }
    return x;
}

static const uint8_t *edge(const uint8_t *room, unsigned direction)
{
    if (!room || direction>6 || (direction&1)) return 0;
    unsigned pos=le16(room+10+2*((direction/2+3)&3));
    return pos==65535 ? 0 : scripts+pos;
}

static void request_scene(unsigned id)
{
    re2_progress[0xcc]=id;
    re2_progress[0xcd]=0;
    re2_progress[0x20b]|=2;
}

static void arrive(unsigned x, unsigned facing)
{
    re2_progress[0x234]=x;
    if (re2_progress[0x28b]&8) facing^=0x40;
    re2_progress[0x233]=facing;
    unsigned lane=facing&15;
    re2_progress[0x235]=lane==0 ? 155 : lane==1 ? 123 : 97;
}

/* A transition script contains one operation, optionally prefixed by 0A/0C.
   The original uses 020A as a provisional destination and commits it on success. */
static int enter(const uint8_t *p, unsigned op, unsigned *area, unsigned *cam,
                 unsigned *scene)
{
    unsigned x=re2_progress[0x234], facing=re2_progress[0x233];
    unsigned lane=facing&15;
    switch (op) {
    case 0x0a: {
        unsigned next_area=*p++;
        if (next_area>=7 || !enter(p+1,p[0],area,cam,scene)) return 0;
        if (next_area!=5 && (next_area!=1 || *area==0)) {
            for (unsigned i=0;i<5;++i) re2_progress[0x290+i]=255;
            for (unsigned i=0;i<6;++i) re2_progress[0x253+i]=255;
            re2_progress[0x20f]=re2_progress[0xd5]=0;
        }
        *area=next_area;
        return 1;
    }
    case 0x0c:
        re2_progress[0x20e]=*p++;
        return enter(p+1,p[0],area,cam,scene);
    case 0:
    case 0x11:
        if (op==0x11 && lane==2) return 0;
        re2_progress[0x234]=re2_project_x(*area,*cam,p[0],0,lane);
        return 1;
    case 1:
    case 6:
        arrive(p[0],p[1]);
        return 1;
    case 2:
        re2_progress[0x233]^=0x40;
        re2_progress[0x234]=(uint8_t)(199-x);
        re2_progress[0x235]=155;
        return 1;
    case 3:
    case 7:
        if (*p++!=lane) return 0;
        arrive(p[0],p[1]);
        return 1;
    case 4:
        arrive(p[1+(lane>2?2:lane)],p[0]);
        return 1;
    case 5:
    case 0x10: {
        unsigned new_facing=p[0];
        if (x<p[1]) new_facing=op==5 ? new_facing|2 : new_facing&0xf0;
        else if (x<p[2]) new_facing=op==5 ? new_facing|1 : (new_facing&~2u)|1;
        unsigned new_lane=new_facing&15;
        arrive(re2_project_x(*area,*cam,p[3],0,new_lane),new_facing);
        return 1;
    }
    case 8:
        if (x<p[0] || x>=p[1]) return 0;
        arrive(p[2],p[3]);
        return 1;
    case 9:
    case 0x0b:
        if (lane!=*p++) {
            p+=2;
            *cam=*p++;
            if (op==9 && lane!=*p++) return 0;
        }
        arrive(p[0],p[1]);
        return 1;
    case 0x0d:
        if (x<p[0]) {
            *cam=p[4]; *scene=p[5]; p+=6;
            if (x<p[0] || x>=p[1]) return 0;
        }
        arrive(p[2],p[3]);
        return 1;
    case 0x0e: {
        unsigned flag=p[0]*256u+p[1];
        if (flag>=0x29c || !(re2_progress[flag]&p[2]) || lane!=p[3]) return 0;
        arrive(p[4],p[5]);
        return 1;
    }
    case 0x0f:
        if (x>=*p++) { p+=2; *cam=*p++; }
        arrive(p[0],p[1]);
        return 1;
    }
    return 0;
}

static int transition(unsigned dest, const uint8_t *p, unsigned op, unsigned scene)
{
    unsigned area=re2_progress[0x208];
    re2_progress[0x20a]=dest;
    if (!enter(p,op,&area,&dest,&scene) || !view(area,dest)) {
        re2_progress[0x20a]=re2_progress[0x209];
        return 0;
    }
    re2_progress[0x20a]=dest;
    if (dest==0x1b && re2_progress[0x209]==0x20 && (re2_progress[0x20b]&4) && scene)
        dest=0x24;
    re2_progress[0x208]=area;
    re2_progress[0x209]=dest;
    if (scene) request_scene(scene);
    return 1;
}

static int blocked_by_wall(const uint8_t *room, int dir, int x, int y, unsigned lane)
{
    const uint8_t *p=obstacles+le16(room+18);
    for (unsigned i=*p++; i; --i,p+=3) {
        if (p[0]!=lane || x<p[1] || x>=p[2]) continue;
        if (dir==2 || dir==6) return 1;
        if (dir==0 && ((lane==1 && y>=154 && y<157) || (lane==2 && y>=122 && y<125)))
            return 1;
        if (dir==4 && ((lane==0 && y>=96 && y<99) || (lane==1 && y>=122 && y<125)))
            return 1;
    }
    return 0;
}

int re2_campaign_move(unsigned dir, int dx, int dy)
{
    unsigned area=re2_progress[0x208], cam=re2_progress[0x209], lane=re2_progress[0x233]&15;
    const uint8_t *room=view(area,cam);
    if ((!dx && !dy) || !room) return 0;
    if (re2_progress[0x28b]&8) dir^=4;
    const uint8_t *e=edge(room,dir);
    if (!e) return 1;
    int x=re2_progress[0x234]+dx, y=re2_progress[0x235]+dy;
    int crossed=0;
    if (dir==0 || dir==4) {
        if ((dir==0 && y==95) || (dir==4 && y==156)) return 1;
        crossed=dir==0 ? y<95 : y>156;
    } else {
        int boundary=re2_project_x(area,cam,e[0],0,lane);
        crossed=dir==2 ? x>=boundary : x<=boundary;
    }
    if (crossed) {
        if (e[1]==255 || (e[2]&128)) return 1;
        return transition(e[1],e+3,e[2],0) ? 2 : 1;
    }
    return blocked_by_wall(room,dir,x,re2_progress[0x235],lane);
}

static int door_action(const uint8_t *room)
{
    unsigned area=re2_progress[0x208],cam=re2_progress[0x209],face=re2_progress[0x233];
    unsigned dir=face>>4,lane=face&15,x=re2_progress[0x234];
    const uint8_t *e=edge(room,dir);
    if (!e || e[1]==255 || !(e[2]&128)) return 0;
    if (dir==0 || dir==4) {
        if (lane!=e[0]) return 0;
    } else {
        unsigned bound=re2_project_x(area,cam,e[0],0,lane);
        unsigned distance=dir==2 ? bound-x : x-bound;
        if (distance>=10) return 0;
    }
    unsigned scene=e[3];
    re2_progress[0x20a]=e[1];
    const uint8_t *p=e+4;
    if (scene&128) {
        unsigned flag=scene&127;
        if (flag>=7 || !(catalog[40+flag]&re2_progress[0x20f])) return 0;
        scene=*p++;
    }
    if (*p==0xf0) {
        unsigned key=p[1],mask=p[2];
        p+=3;
        if (!(re2_progress[0xd5]&mask)) {
            unsigned slot=0;
            while (slot<6 && re2_progress[0x262+slot]!=key) ++slot;
            if (slot==6) {
                unsigned locked=key==0x18 ? 0x1e : key==0x15 ? 0x1f : key==0x16 ? 0x20 :
                                key==0x17 ? 0x21 : key==0x11 ? 0x22 : key==0x1a ? 0x23 : 0x24;
                request_scene(locked);
                return 1;
            }
            if (key==0x1a) re2_progress[0x262+slot]=0;
            re2_progress[0xd5]|=mask;
        }
    }
    return transition(e[1],p,e[2]&127,scene);
}

static const uint8_t *ground(unsigned index)
{
    unsigned area=re2_progress[0x208],cam=re2_progress[0x209];
    if (!view(area,cam)) return 0;
    const uint8_t *dir=catalog+le32(catalog+48)+2*(area_start[area]+cam);
    if (index>=dir[1]) return 0;
    const uint8_t *item=catalog+le32(catalog+52)+8*(dir[0]+index);
    return (re2_progress[0x290+item[0]] & item[1]) ? item : 0;
}

const uint8_t *re2_ground_item(unsigned index,int *x,int *y)
{
    const uint8_t *item=ground(index);
    if (!item) return 0;
    *x=item[4]; *y=item[5];
    return catalog+le32(catalog+56)+512*item[3];
}

static const uint8_t *room_prop(void)
{
    static const uint8_t locations[][2]={{3,0x26},{3,0x15},{2,0x0a},{5,0x13},
                                         {5,0x14},{5,3},{5,4}};
    if (!catalog) return 0;
    const uint8_t *props=catalog+le32(catalog+84);
    for (unsigned i=0;i<7;++i)
        if (locations[i][0]==re2_progress[0x208] && locations[i][1]==re2_progress[0x209])
            return props+le32(props+4+4*i);
    return 0;
}

const uint8_t *re2_world_prop(unsigned part,int *x,int *y)
{
    const uint8_t *prop=room_prop();
    if (!prop || part>=prop[3]) return 0;
    const uint8_t *piece=prop+4+4*part;
    int base=(re2_progress[0x20f]&prop[1]) ? piece[0] : re2_progress[0x20e];
    *x=base+piece[1]; *y=piece[2];
    const uint8_t *props=catalog+le32(catalog+84);
    return props+le32(props)+2048*piece[3];
}

int re2_push_start(void)
{
    const uint8_t *prop=room_prop();
    unsigned face=re2_progress[0x233],object_x=re2_progress[0x20e],x=re2_progress[0x234];
    if (!prop || (re2_progress[0x20f]&prop[1])) return 0;
    if (face==0x22 && (uint8_t)(object_x-x)<15) return 0x7e;
    if (face==0x62 && (uint8_t)(x-object_x-prop[0])<15) return 0x7f;
    return 0;
}

static void prop_reward(unsigned item,unsigned progress_mask,unsigned reward_bit)
{
    unsigned slot=0;
    while (slot<6 && re2_progress[0x262+slot]) ++slot;
    if (slot<6) {
        re2_progress[0x262+slot]=item;
        re2_progress[0xd4]|=reward_bit;
    } else re2_progress[0x20f]&=(uint8_t)~progress_mask;
    re2_progress[0x270]=item;
    re2_progress[0xd6]|=0xc0;
    if (item==0x18) {
        re2_progress[0x234]=80; re2_progress[0x233]=0x61; re2_progress[0x235]=123;
        re2_progress[0x209]=0x18;
        request_scene(0x18);
    } else {
        re2_progress[0x234]=120; re2_progress[0x233]=2; re2_progress[0x209]=3;
        request_scene(0x17);
    }
}

int re2_push_step(unsigned frame)
{
    static const uint8_t deltas[]={0,3,3,3,8,0};
    const uint8_t *prop=room_prop();
    if (!prop || frame>=sizeof deltas || (prop[1]&re2_progress[0x20f])) return 1;
    int right=(re2_progress[0x233]&0xf0)==0x20;
    int next=(int)re2_progress[0x20e]+(right ? deltas[frame] : -deltas[frame]);
    if (right ? next<prop[2] : next>prop[2]) {
        re2_progress[0x20e]=next;
        return 0;
    }
    re2_progress[0x20e]=prop[2];
    re2_progress[0x20f]|=prop[1];
    unsigned finished=re2_progress[0x20f];
    if ((finished&3)==3 && !(re2_progress[0xd4]&16)) prop_reward(0x18,3,16);
    else if ((finished&12)==12 && !(re2_progress[0xd4]&32)) prop_reward(0x1c,12,32);
    return 1;
}

static unsigned count_address(unsigned item)
{
    if (item==7) return 0x296;
    if (item==16) return 0x297;
    if (item==11) return 0x298;
    return 0;
}

static int find_pickup(void)
{
    int x=re2_progress[0x234], y=re2_progress[0x235];
    for (unsigned i=0;i<2;++i) {
        const uint8_t *item=ground(i);
        if (!item) continue;
        int xmin=item[4]-5;
        int lane_y=item[5]>=139 ? 155 : item[5]>=110 ? 123 : 97;
        if (y!=lane_y || x<xmin || x>item[4]+item[6]) continue;
        pickup=item;
        re2_progress[0x270]=item[2];
        return 1;
    }
    return 0;
}

int re2_pickup_pending(void)
{
    return pickup && re2_progress[0x270]==pickup[2] ? pickup[2] : 0;
}

void re2_pickup_confirm(int accept)
{
    int id=re2_pickup_pending();
    if (id && accept) {
        unsigned addr=count_address(id);
        for (unsigned i=0;i<6;++i) {
            uint8_t *slot=re2_progress+0x262+i;
            if (*slot && (!addr || *slot!=id)) continue;
            unsigned increment=id==7 ? 40 : id==16 ? 20 : 4;
            if (addr) {
                unsigned amount=re2_progress[addr]+increment;
                re2_progress[addr]=(*slot && id!=11 && amount>255) ? 255 : (uint8_t)amount;
            }
            *slot=(uint8_t)id;
            re2_progress[0x290+pickup[0]]&=(uint8_t)~pickup[1];
            break;
        }
    }
    pickup=0;
    re2_progress[0x270]=0;
}

static void compact(uint8_t *items,unsigned capacity)
{
    unsigned next=0;
    for (unsigned i=0;i<capacity;++i)
        if (items[i]) items[next++]=items[i];
    while (next<capacity) items[next++]=0;
}

static void transfer_stack(unsigned item, int into_box)
{
    unsigned addr=count_address(item);
    if (!addr) return;
    unsigned src=addr+(into_box ? 0 : 3), dst=addr+(into_box ? 3 : 0);
    unsigned count=re2_progress[src]+re2_progress[dst];
    re2_progress[dst]=count>255 ? 255 : count;
    re2_progress[src]=0;
    uint8_t *items=re2_progress+(into_box ? 0x272 : 0x262);
    unsigned found=0, n=into_box ? 13 : 6;
    for (unsigned i=0;i<n;++i) {
        if (items[i]==item && found++) items[i]=0;
    }
}

void re2_box_exchange(unsigned carried, unsigned box)
{
    if (carried>=6 || box>=13) return;
    uint8_t *bag=re2_progress+0x262,*storage=re2_progress+0x272;
    unsigned old=bag[carried], take=storage[box];
    bag[carried]=take; storage[box]=old;
    if (old==re2_progress[0x268]) re2_progress[0x268]=0;
    compact(bag,6); compact(storage,13);
    if (old!=take) {
        transfer_stack(old,1);
        transfer_stack(take,0);
    } else if (count_address(old)) {
        unsigned addr=count_address(old),n=re2_progress[addr];
        re2_progress[addr]=re2_progress[addr+3];
        re2_progress[addr+3]=n;
    }
}

int re2_campaign_action(void)
{
    if (!catalog || re2_progress[0x236]>0x16 || (re2_progress[0x236]&1)) return 0;
    unsigned area=re2_progress[0x208],cam=re2_progress[0x209];
    const uint8_t *room=view(area,cam);
    if (door_action(room)) return 1;
    if (re2_puzzle_action()) return 1;
    if (re2_progress[0x233]==2 && re2_progress[0x235]<100) {
        for (unsigned i=0;i<8;++i) {
            const uint8_t *box=catalog+60+3*i;
            int x=(int)re2_progress[0x234]-box[2];
            if (box[0]!=area || box[1]!=cam || x<0 || x>=40) continue;
            re2_progress[0x270]|=128;
            re2_progress[0x261]=5;
            return 1;
        }
    }
    for (unsigned i=0;i<7;++i) {
        const uint8_t *station=catalog+12+4*i;
        if (station[0]!=area || station[1]!=cam || station[2]!=re2_progress[0x233]) continue;
        int offset=(int)re2_progress[0x234]-station[3];
        if (offset<0 || offset>=15 || !re2_progress[0x298]) return 0;
        --re2_progress[0x298];
        re2_save_commit();
        request_scene(0x1d);
        return 1;
    }
    return find_pickup();
}
