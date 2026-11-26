#include <stdint.h>

#define REG16(a) (*(volatile uint16_t *)(a))
enum { A = 1, B = 2, SELECT = 4, START = 8, R = 256, L = 512 };
enum { RIGHT = 16, LEFT = 32, UP = 64, DOWN = 128 };
extern const uint8_t assets[];
enum { SHOT = 4470, FLASH = 2194, RELOAD = 21395, HURT_POSE = 3398 };

static void copy_words(const void *src, uintptr_t dest, unsigned count) {
    *(volatile uintptr_t *)0x040000d4 = (uintptr_t)src;
    *(volatile uintptr_t *)0x040000d8 = dest;
    *(volatile uint32_t *)0x040000dc = 0x84000000u | count;
}

static void full_scene(const uint8_t *tiles) {
    copy_words(tiles,0x06000000,4800);
    volatile uint16_t *map=(volatile uint16_t *)0x0600f800;
    for (unsigned y=0; y<20; y++)
        for (unsigned x=0; x<30; x++) map[y*32+x]=30*y+x;
}

struct Atlas {
    const uint8_t *backgrounds, *sprites, *actors;
};

static const uint32_t *actor_list(const uint8_t *room) {
    return (const uint32_t *)(room+36+4*room[32]);
}

static uint32_t view_entry(const uint8_t *room,unsigned lane,unsigned depth) {
    const uint32_t *spawns=actor_list(room);
    const uint32_t *offsets=spawns+1+*spawns;
    return offsets[depth*room[0]+lane-1];
}

// Prepare masked actor tiles and their OAM entries before publishing them.
// A new mask at the old sprite position exposes pixels along scenery edges.
enum { ACTOR_TILE_BASE=384, ACTOR_PIECES=40, PIECE_WORDS=128, SCENERY_WORDS=17*30*8 };
static uint16_t shadow_oam[128][4] __attribute__((section(".ewram"),aligned(4)));
static uint32_t actor_tiles[ACTOR_PIECES][PIECE_WORDS] __attribute__((section(".ewram")));
static uint32_t scenery_tiles[SCENERY_WORDS] __attribute__((section(".ewram")));
static unsigned pending_actor_pieces __attribute__((section(".ewram")));
static unsigned scenery_pending __attribute__((section(".ewram")));
struct FloorPiece { uint16_t y,x,tile,distance; };
static struct FloorPiece floor_pieces[ACTOR_PIECES] __attribute__((section(".ewram")));

static void publish_objects(void) {
    // At most 36,800 bytes of tiles, plus OAM, fit in one GBA VBlank.
    if (scenery_pending) {
        copy_words(scenery_tiles,0x06008c00,SCENERY_WORDS);
        scenery_pending=0;
    }
    if (pending_actor_pieces) {
        copy_words(actor_tiles,0x06010000+32*ACTOR_TILE_BASE,pending_actor_pieces*PIECE_WORDS);
        pending_actor_pieces=0;
    }
    copy_words(shadow_oam,0x07000000,256);
}

struct ViewObjects {
    unsigned items;
    const uint32_t *occlusion;
    const uint32_t *bounds;
};

/* BG0 holds walls and the HUD. BG1 holds doors; BG2 holds scenery.
   The latter two start at character 96 of their character blocks to
   leave the preceding layer's last tiles intact. */
static const uint32_t *previous_layout[3] __attribute__((section(".ewram")));

static void reset_backgrounds(void) {
    for (unsigned i=0;i<3;i++) previous_layout[i]=0;
    REG16(0x04000008)=0x1f02;
    REG16(0x0400000a)=0x1e05;
    REG16(0x0400000c)=0x1d08;
    for (unsigned layer=1;layer<=2;layer++) {
        volatile uint16_t *map=(volatile uint16_t *)(0x0600f800-0x800*layer);
        for (unsigned y=0;y<20;y++)
            for (unsigned x=0;x<30;x++) map[32*y+x]=96+30*y+x;
    }
}

static const uint8_t *bg_records(const uint8_t *atlas) {
    return atlas+*(const uint32_t *)atlas;
}

static void decode_tile(const uint8_t *records,const uint32_t *lines,unsigned tile,uintptr_t dest) {
    if (tile&1) {
        copy_words(records+tile-1,dest,8);
        return;
    }
    volatile uint32_t *vram=(volatile uint32_t *)dest;
    const uint16_t *codes=(const uint16_t *)(records+tile);
    for (unsigned y=0;y<8;y++) vram[y]=lines[codes[y]];
}

static void load_sprite(const struct Atlas *atlas,unsigned image,uint32_t *dest) {
    const uint32_t *tiles=(const uint32_t *)atlas->sprites+16*image;
    const uint8_t *records=bg_records(atlas->backgrounds);
    const uint32_t *lines=(const uint32_t *)atlas->backgrounds+1;
    for (unsigned i=0;i<16;i++)
        decode_tile(records,lines,tiles[i],(uintptr_t)(dest+8*i));
}

static void update_layer(const uint8_t *bank,unsigned layer,unsigned offset,unsigned height) {
    const uint8_t *atlas=bg_records(bank);
    const uint32_t *lines=(const uint32_t *)bank+1;
    const uint32_t *next=(const uint32_t *)(atlas+offset);
    const uint32_t *old=previous_layout[layer];
    if (next==old) return;
    uintptr_t dest=layer==2 ? (uintptr_t)scenery_tiles : 0x06000000+layer*0x4000+(layer ? 96*32 : 0);
    for (unsigned y=0;y<height;y++,dest+=960) {
        if (old && old[y]==next[y]) continue;
        const uint32_t *row=(const uint32_t *)(atlas+next[y]);
        const uint32_t *prior=old ? (const uint32_t *)(atlas+old[y]) : 0;
        for (unsigned x=0;x<10;x++) {
            if (prior && prior[x]==row[x]) continue;
            const uint32_t *strip=(const uint32_t *)(atlas+row[x]);
            for (unsigned t=0;t<3;t++)
                decode_tile(atlas,lines,strip[t],dest+96*x+32*t);
        }
    }
    previous_layout[layer]=next;
    if (layer==2) scenery_pending=1;
}

static struct ViewObjects show_view(const struct Atlas *atlas, const uint8_t *room, unsigned lane, unsigned depth,
                      unsigned open_doors, uint32_t taken) {
    unsigned offset=view_entry(room,lane,depth)&0x7fffffff;
    if (open_doors && depth+1==room[1]) {
        for (int i=0; i<3; i++)
            if ((open_doors & (1u<<i)) && (room[4+4*i]&31)==lane)
                offset=((const uint32_t *)(room+20))[i];
    }
    const uint32_t *view=(const uint32_t *)(room+offset);
    for (unsigned layer=0;layer<3;layer++) update_layer(atlas->backgrounds,layer,view[layer],17);
    const uint32_t *objects=(const uint32_t *)(bg_records(atlas->backgrounds)+view[3]);
    const uint32_t *masks=(const uint32_t *)(bg_records(atlas->backgrounds)+view[4]);
    unsigned count=*objects++;
    unsigned slot=0;
    for (unsigned i=0; i<count; i++,objects+=2) {
        const uint8_t *sprite=(const uint8_t *)objects;
        if (taken & (1u<<sprite[0])) continue;
        unsigned tile=ACTOR_TILE_BASE+16*slot;
        load_sprite(atlas,objects[1],actor_tiles[slot]);
        floor_pieces[slot++]=(struct FloorPiece){sprite[2],0x8000|sprite[1],tile,sprite[3]};
    }
    struct ViewObjects result={slot,masks,masks+1+3*masks[0]};
    return result;
}

struct Enemy {
    unsigned short x;
    uint8_t z,type,health,action,phase;
    uint8_t steps,engaged,strafe_only,sight,contact;
    uint16_t reaction;
    uint8_t screen_x,ground;
    unsigned attack_time,walk_time;
};
struct Drop { uint16_t x; uint8_t z,type; };
static uint16_t walk_pose_ticks[18] __attribute__((section(".ewram")));
static uint8_t walk_pose[18] __attribute__((section(".ewram")));
static struct Drop drops[256][18] __attribute__((section(".ewram")));

struct Encounter {
    struct Enemy enemies[18];
    unsigned count;
    unsigned seed;
    struct ViewObjects view;
    struct Drop *loot;
};

static void enter_encounter(struct Encounter *enc,const uint8_t *room,const uint8_t *damage,struct Drop *loot) {
    static const uint8_t health[]={9,7,12,15};
    const uint32_t *list=actor_list(room);
    enc->count=*list++;
    enc->loot=loot;
    enc->seed=0x9f82;
    for (unsigned i=0;i<enc->count;i++) {
        struct Enemy *e=&enc->enemies[i];
        unsigned spawn=list[i];
        e->x=spawn;
        e->z=spawn>>16;
        e->type=(spawn>>24)&3;
        e->sight=spawn>>26;
        e->health=health[e->type]-damage[i];
        e->action=room[0]==1 ? 8 : 7;
        e->phase=0;
        walk_pose[i]=0;
        /* Wide rooms start with one movement decision already due. */
        walk_pose_ticks[i]=room[0]>1 ? 7650 : 0;
        e->reaction=0;
        e->screen_x=e->ground=0;
        e->attack_time=0;
        e->walk_time=room[0]>1 ? 6800 : 0;
        e->steps=0;
        e->engaged=0;
        e->strafe_only=0;
        e->contact=0;
    }
}

static int camera_x(unsigned lane) { return lane==1 ? 0 : (int)lane*50-72; }

static unsigned sight_lane(unsigned x,unsigned lane) {
    if ((int)x<camera_x(lane)) {
        do { lane--; } while (lane && (int)x<camera_x(lane+1));
    } else if ((int)x>=camera_x(lane)+200) {
        lane+=5;
        while ((int)x>=camera_x(lane+1)) lane++;
    }
    return lane;
}

static int occupied(const struct Encounter *enc,unsigned lane,unsigned z) {
    for (unsigned i=0;i<enc->count;i++) {
        const struct Enemy *e=&enc->enemies[i];
        if (e->health && e->z==z && e->sight==lane) return 1;
    }
    return 0;
}

// Clear covered OBJ pixels in tile order before DMA to VRAM.
static void clip_piece(const uint32_t *masks,unsigned distance,int x,int y,int flip,uint32_t *pixels) {
    if (distance==1) return;
    unsigned count=*masks++;
    for (unsigned i=0;i<count;i++,masks+=3) {
        const uint8_t *mask=(const uint8_t *)masks;
        if (mask[2]>=distance) break; /* converter emits scenery nearest first */
        int dx=mask[0]-x,dy=mask[1]-y;
        if (dx<=-8 || dx>=32 || dy<=-8 || dy>=32) continue;
        for (int row=0;row<8;row++) {
            unsigned bits=mask[4+row];
            int py=dy+row;
            if (!bits || (unsigned)py>=32) continue;
            for (int col=0;col<8;col++) {
                int px=dx+col;
                if (!(bits&(1u<<col)) || (unsigned)px>=32) continue;
                if (flip) px=31-px;
                unsigned cell=8*((py/8)*4+px/8)+py%8;
                pixels[cell]&=~(15u<<(4*(px%8)));
            }
        }
    }
}

static unsigned enemy_random(struct Encounter *enc,unsigned depth);
enum { PLAYER_FX, MONSTER_FX };
static void sample(unsigned voice,unsigned id);

static int fire_at(struct Encounter *enc,unsigned lane,unsigned depth,unsigned pose,unsigned weapon,unsigned skill,uint8_t *damage) {
    static const uint8_t power[][4]={{3,2,1,1},{5,4,3,2},{14,10,10,8},{16,16,16,16}};
    for (unsigned distance=1;distance<=(weapon ? 4u : 1u);distance++)
        for (unsigned i=0;i<enc->count;i++) {
            struct Enemy *e=&enc->enemies[i];
            int x=(int)e->x-camera_x(lane);
            if (!e->health || e->action==1 || e->z!=depth+distance || x<0 || x>=200) continue;
            if (pose==1 && (x<50 || x>150)) continue;
            if ((pose==2 && x>75) || (pose==4 && x<125)) continue;
            unsigned hit=e->health>power[weapon][skill] ? power[weapon][skill] : e->health;
            damage[i]+=hit;
            e->health-=hit;
            static const uint8_t calls[2][4]={{25,26,25,8},{25,26,25,7}};
            sample(MONSTER_FX,calls[!e->health][e->type]);
            if (!e->health) {
                unsigned roll=enemy_random(enc,depth);
                enc->loot[i]=(struct Drop){e->x,e->z,e->type ? 7 : (roll&1) ? 9 : 6};
            }
            e->action=1;
            e->phase=e->type==0 ? 2 : e->type==3 ? 0 : 1;
            e->reaction=(e->phase+1)*HURT_POSE;
            e->attack_time=0;
            e->walk_time=0;
            e->steps=0;
            e->contact=0;
            return e->health ? 1 : 2;
        }
    return 0;
}

static int animate_hits(struct Encounter *enc,unsigned dt) {
    int changed=0;
    for (unsigned i=0;i<enc->count;i++) {
        struct Enemy *e=&enc->enemies[i];
        if (!e->reaction) continue;
        e->reaction=e->reaction>dt ? e->reaction-dt : 0;
        unsigned phase=(e->reaction>HURT_POSE)+(e->reaction>2*HURT_POSE);
        if (!e->reaction) {
            e->action=e->health ? 8 : 0;
            changed=1;
        }
        if (phase!=e->phase) { e->phase=phase; changed=1; }
    }
    return changed;
}

static void draw_enemies(const struct Atlas *atlas,const struct Encounter *enc,unsigned lane,unsigned depth) {
    static const uint8_t ground[]={0,138,118,99,80};
    unsigned object=48;
    unsigned slot=enc->view.items;
    for (unsigned distance=1;distance<=4;distance++) {
        for (unsigned j=enc->count;j>0;j--) {
            const struct Enemy *e=&enc->enemies[j-1];
            int fallen=!e->health && !e->reaction;
            const struct Drop *drop=&enc->loot[j-1];
            int x=(int)(fallen ? drop->x : e->x)-camera_x(lane);
            if ((fallen && !drop->type) || (fallen ? drop->z : e->z)!=depth+distance || x<0 || x>=200) continue;
            unsigned phase=e->action>=3 ? walk_pose[j-1] : e->phase;
            unsigned id=fallen ? 4*11*16+4*(drop->type==6 ? 0 : drop->type==7 ? 1 : 2)+distance-1 :
                                (((e->type*11+e->action)*4+distance-1)*4)+phase;
            const uint8_t *frame=atlas->actors+((const uint32_t *)atlas->actors)[id];
            static const uint8_t item_ground[]={0,139,120,102,80};
            int width=frame[0],height=frame[1],top=(fallen ? item_ground[distance] : ground[distance])-frame[3];
            int flip=!fallen && e->action>=5 && e->action<=8;
            int left=x-(flip ? width-frame[2] : frame[2]);
            const uint32_t *pieces=(const uint32_t *)(frame+4);
            for (int y=0;y<height;y+=32)
                for (int u=0;u<width;u+=32) {
                    unsigned image=*pieces++;
                    int px=left+(flip ? width-u-32 : u), py=top+y;
                    if (px>=200 || px+32<=0 || py>=136 || py+32<=0 || slot>=ACTOR_PIECES) continue;
                    unsigned tile=ACTOR_TILE_BASE+16*slot;
                    uint32_t *pixels=actor_tiles[slot++];
                    load_sprite(atlas,image,pixels);
                    clip_piece(enc->view.occlusion,distance,px,py,flip,pixels);
                    uint16_t *oam=shadow_oam[object++];
                    oam[0]=py&255;
                    oam[1]=0x8000|(flip ? 0x1000 : 0)|((px+20)&511);
                    oam[2]=tile;

                }
        }
        for (unsigned i=0;i<enc->view.items;i++) {
            const struct FloorPiece *p=&floor_pieces[i];
            if (p->distance!=distance) continue;
            uint16_t *oam=shadow_oam[object++];
            oam[0]=p->y; oam[1]=p->x; oam[2]=p->tile;
        }
    }
    for (;object<48+ACTOR_PIECES;object++) shadow_oam[object][0]=0x200;
    pending_actor_pieces=slot;
}

/* Timer 2 ticks at 16,384 Hz. The opening pistol trace averages 16.3
 * display periods between shots and 78 from the sixth shot to the next.
 * Presentation can miss a VBlank without advancing the weapon clock twice.
 */
/* Nine lateral steps in the reference take 118 display intervals from
 * first to last (14.75 per step). Timer 2 runs at 16,384 Hz. */
enum { STREET_STEP = 4046, LOBBY_STEP = 4320 };
struct Weapon {
    unsigned remaining, magazine, delay, age, pose, kind;
};

struct Inventory {
    unsigned key, armor, health, shells, rockets, weapons;
};

/* scripts/duke3d/test_preview.py reads this IWRAM layout through the emulator. */
_Static_assert(sizeof(struct Enemy)==24 && sizeof(struct Encounter)==456 &&
               sizeof(struct Weapon)==24 && sizeof(struct Inventory)==24,"IWRAM layout");

/* Redrawn lettering for the two-line game.com system dialog. Each glyph
   occupies an 8x16 cell with nine ink rows starting on the fourth row. */
static const char pause_letters[]="Pre sAUSEtocniu";
static const uint8_t pause_font[][9]={
    {126,65,65,65,126,64,64,64,64}, /* P */
    {0,0,0,92,98,64,64,64,64},      /* r */
    {0,0,0,60,66,126,64,66,60},     /* e */
    {0},
    {0,0,0,60,66,48,12,66,60},      /* s */
    {8,20,34,65,65,127,65,65,65},   /* A */
    {65,65,65,65,65,65,65,34,28},   /* U */
    {62,65,64,64,62,1,1,65,62},     /* S */
    {127,64,64,64,124,64,64,64,127},/* E */
    {0,16,16,124,16,16,16,18,12},   /* t */
    {0,0,0,60,66,66,66,66,60},      /* o */
    {0,0,0,60,66,64,64,66,60},      /* c */
    {0,0,0,92,98,66,66,66,66},      /* n */
    {0,8,0,24,8,8,8,8,62},          /* i */
    {0,0,0,66,66,66,66,70,58}       /* u */
};

static void prepare_pause_dialog(void) {
    /* BG3 uses the space between scenery tiles and the gameplay maps. */
    volatile uint32_t *tiles=(volatile uint32_t *)0x0600cc00;
    volatile uint16_t *map=(volatile uint16_t *)0x0600e000;
    for (unsigned tile=0;tile<4;tile++) {
        for (unsigned y=0;y<8;y++) {
            uint32_t row=tile&1 ? 0x11114444 : 0x11111111;
            *tiles++=(tile&2) && y<4 ? 0x44444444 : row;
        }
    }
    for (unsigned i=0;i<sizeof pause_font/sizeof *pause_font;i++) {
        for (unsigned y=0;y<16;y++) {
            unsigned bits=y>=3 && y<12 ? pause_font[i][y-3] : 0;
            uint32_t row=0;
            for (unsigned x=0;x<8;x++)
                row|=(uint32_t)(bits&(128>>x) ? 4 : 1)<<(4*x);
            *tiles++=row;
        }
    }
    for (unsigned y=0;y<8;y++) for (unsigned x=0;x<21;x++) {
        unsigned tile=(x==0 || x==20) + 2*(y==0 || y==7);
        map[32*y+x]=(96+tile) | (x==20 ? 0x400 : 0) | (y==7 ? 0x800 : 0);
    }
    const char *lines[]={"Press PAUSE to","continue"};
    for (unsigned line=0;line<2;line++) {
        unsigned at=(2+2*line)*32+(line ? 6 : 3);
        for (const char *s=lines[line];*s;s++,at++) {
            unsigned i=0;
            while (pause_letters[i]!=*s) i++;
            map[at]=100+2*i;
            map[at+32]=101+2*i;
        }
    }
    REG16(0x0400000e)=0x1c0c;
    REG16(0x0400001c)=512-36;
    REG16(0x0400001e)=512-48;
    REG16(0x04000042)=(20<<8)|220;
    REG16(0x04000046)=136;
}

/* Record six separate button edges while paused, as at 23:6A42.
   The two original lower-right touches map to Select. */
static void pause_input(uint16_t sequence[6],unsigned *count,unsigned pressed,
                        struct Inventory *inventory,struct Weapon *weapon) {
    static const uint16_t items[]={A,L,B,L,SELECT,SELECT};
    static const uint16_t weapons[]={R,A,B,A,B,B};
    if (!pressed) return;
    sequence[(*count)++]=pressed;
    if (*count<6) return;
    *count=0;
    int give_items=1,give_weapons=1;
    for (unsigned i=0;i<6;i++) {
        give_items&=sequence[i]==items[i];
        give_weapons&=sequence[i]==weapons[i];
    }
    if (give_items) { inventory->key=1; inventory->armor=100; }
    if (give_weapons) {
        inventory->weapons=7;
        inventory->shells=inventory->rockets=50;
        weapon->remaining=200;
    }
}

static int corridor_step_clear(const struct Encounter *enc,const struct Enemy *enemy,const uint8_t *room) {
    unsigned next=enemy->z-1;
    if (view_entry(room,1,next)&0x80000000u) return 0;
    for (unsigned i=0;i<enc->count;i++) {
        const struct Enemy *other=&enc->enemies[i];
        if (other->health && other->z==next) return 0;
    }
    return 1;
}

/* A hit on the original occupies the shared combat loop for about 29
   display periods. Two attackers therefore repeat more slowly than one. */
enum { CLOSE_ATTACK_PERIOD=27600, HIT_PAUSE=7950 };

static unsigned random_with_carry(struct Encounter *enc,unsigned depth,unsigned carry) {
    unsigned hi=(enc->seed>>8)&255, rotated=((hi<<1)&255)|carry;
    unsigned sum=(enc->seed&255)+rotated+(hi>>7);
    sum=((sum&255)^depth)+0x90+(sum>>8);
    enc->seed=((rotated+(sum&255)+(sum>>8))<<8)|(sum&255);
    return enc->seed&65535;
}

static unsigned enemy_random(struct Encounter *enc,unsigned depth) {
    return random_with_carry(enc,depth,0);
}

/* Player timing varies the speech rolls without consuming the enemies' random stream. */
static unsigned speech_roll(unsigned *seed,unsigned now,unsigned choices) {
    *seed=(*seed+now)*1664525u+1013904223u;
    return ((*seed>>16)*choices)>>16;
}

static const uint8_t *actor_frame(const struct Atlas *atlas,const struct Enemy *e,unsigned distance) {
    unsigned id=((e->type*11+e->action)*4+distance-1)*4+e->phase;
    return atlas->actors+((const uint32_t *)atlas->actors)[id];
}

static int street_clear(const struct Atlas *atlas,const struct Encounter *enc,const struct Enemy *e,
                        const uint8_t *room,unsigned lane,unsigned depth,unsigned z) {
    int screen=e->screen_x;
    unsigned current=e->z-depth;
    unsigned distance=current>4 ? 4 : current;
    unsigned layer=z<e->z ? distance-1 : distance+1;
    int bottom=e->ground;
    const uint8_t *frame=actor_frame(atlas,e,distance);
    int radius=frame[0]/2;
    const uint32_t *bounds=enc->view.bounds;
    if (e->sight!=lane && e->sight && e->sight<=room[0]) {
        const uint32_t *v=(const uint32_t *)(room+(view_entry(room,e->sight,depth)&0x7fffffff));
        const uint32_t *masks=(const uint32_t *)(bg_records(atlas->backgrounds)+v[4]);
        bounds=masks+1+3*masks[0];
    }
    const uint8_t *box=(const uint8_t *)(bounds+1);
    for (unsigned i=0;i<*bounds;i++,box+=5) {
        if (box[0]==layer && screen+radius>box[1] && screen-radius<box[2] &&
            bottom>box[3] && bottom-frame[1]<box[3]+box[4]) return 0;
    }
    int current_overlap=0;
    for (unsigned i=0;i<enc->count;i++) {
        const struct Enemy *other=&enc->enemies[i];
        if (other==e || !other->health || other->sight!=e->sight || other->z<=depth) continue;
        unsigned d=other->z-depth;
        if (d!=distance && d!=layer) continue;
        const uint8_t *other_frame=actor_frame(atlas,other,distance);
        int other_bottom=other->ground;
        if (bottom<other_bottom-other_frame[1] || bottom-frame[1]>other_bottom) continue;
        int spacing=radius+other_frame[0]/2;
        int dx=screen-other->screen_x;
        if (dx < -spacing || dx > spacing) continue;
        if (d==layer) return 0;
        /* 22:69CD breaks a current-layer overlap by actor table order. */
        if (!current_overlap) current_overlap=other>e ? 1 : -1;
    }
    return z<e->z ? current_overlap<=0 : current_overlap>=0;
}

/* Bank 22 dispatches one decision every seven AI passes. A depth step
   has a preparation pose and a pending move. A strafe consumes a count
   of twenty-pixel steps, and turns at the limit for the current depth. */
static void choose_strafe(struct Encounter *enc,struct Enemy *e,unsigned depth) {
    static const uint8_t strides[]={2,4,8,6,2,6,5,4,5};
    e->steps=strides[enemy_random(enc,depth)%9];
    e->action=(random_with_carry(enc,depth,enc->seed>>16)&0x400) ? 10 : 7;
}

static void choose_street_move(struct Encounter *enc,struct Enemy *e,unsigned depth,
                               int dx,int clear,int room_end) {
    unsigned d=e->z-depth;
    unsigned roll=enemy_random(enc,depth),hi=roll>>8,lo=roll&255;
    int visible=(unsigned)dx<200;
    if (e->engaged && d<4 && !visible) {
        e->action=dx<0 ? 10 : 7;
        e->steps=1;
        return;
    }
    int shifted=e->action==3;
    int retreat=d>=4 ? hi<30 : lo<(d==3 ? 60u : 50u);
    int advance=d>=4 ? hi<180 : !!(hi&(d==3 ? 2u : 3u));
    if (!e->engaged && e->strafe_only) {
        choose_strafe(enc,e,depth);
        if (shifted) { e->engaged=e->steps&1; e->strafe_only=e->steps&2; e->steps=0; }
    } else if (!e->engaged && retreat && e->z<room_end) {
        e->action=4;
        e->steps=1;
    } else if ((e->engaged || advance) && d>1) {
        if (e->engaged && d<4) {
            if (clear) { e->action=3; e->steps=2; }
            else choose_strafe(enc,e,depth);
        } else if (e->action==3) {
            /* 22:6479 backs up the record cursor and decrements the
               action byte. Depth only changes on a prepared step. */
            e->action=2;
            e->engaged=e->strafe_only=0;
        } else {
            e->action=e->action<8 ? 8 : 5;
        }
    } else {
        choose_strafe(enc,e,depth);
        if (shifted) { e->engaged=e->steps&1; e->strafe_only=e->steps&2; e->steps=0; }
    }
}

static int advance_street(const struct Atlas *atlas,struct Encounter *enc,const uint8_t *room,
                          unsigned lane,unsigned depth,unsigned dt) {
    if (room[0]==1) return 0;
    int camera=camera_x(lane),changed=0;
    unsigned decision_period=6800;
    for (unsigned i=0;i<enc->count;i++) {
        struct Enemy *e=&enc->enemies[i];
        if (!e->health || e->z<=depth) continue;
        if (e->contact) decision_period=5900;
        int dx=(int)e->x-camera;
        if ((unsigned)dx<200) {
            static const uint8_t ground[]={0,138,118,99,80};
            e->screen_x=dx;
            if (e->z-depth<=4) e->ground=ground[e->z-depth];
        }
    }
    for (unsigned i=0;i<enc->count;i++) {
        struct Enemy *e=&enc->enemies[i];
        if (!e->health || e->reaction || e->z<=depth) continue;
        unsigned previous_sight=e->sight;
        e->sight=sight_lane(e->x,lane);
        int dx=(int)e->x-camera;
        unsigned distance=e->z-depth;
        int in_contact=distance==1 && e->sight==lane;
        if (!in_contact) e->contact=0;
        if (e->contact) {
            if (e->attack_time+dt>=CLOSE_ATTACK_PERIOD-HIT_PAUSE) {
                unsigned roll=enemy_random(enc,depth);
                if (!e->engaged && (roll&1) && e->z+1<room[1]) {
                    e->contact=0;
                    e->action=4;
                    e->steps=1;
                    e->walk_time=0;
                    changed=1;
                }
            }
            continue;
        }
        e->walk_time+=dt;
        unsigned visual_period=decision_period+decision_period/8;
        unsigned ticks=walk_pose_ticks[i]+(dt>visual_period ? visual_period : dt);
        if (ticks>=visual_period) {
            ticks-=visual_period;
            walk_pose[i]=(walk_pose[i]+1)&3;
            changed=1;
        }
        walk_pose_ticks[i]=ticks;
        if (e->walk_time<decision_period) continue;
        e->walk_time-=decision_period;
        changed=1;
        e->phase=(e->phase+1)&3;
        if (distance==1 && !(e->action==4 && e->steps)) {
            unsigned roll=enemy_random(enc,depth);
            if (in_contact && (e->engaged || !(roll&1) || e->z+1>=room[1])) {
                e->contact=1;
                e->attack_time=CLOSE_ATTACK_PERIOD-HIT_PAUSE;
                /* Attacks that mature on the same game.com animation
                   update share the hit pause, even across two actors. */
                for (unsigned j=0;j<enc->count;j++) {
                    const struct Enemy *other=&enc->enemies[j];
                    if (other!=e && other->contact && other->health &&
                        other->attack_time+1968>=CLOSE_ATTACK_PERIOD-HIT_PAUSE)
                        e->attack_time=other->attack_time;
                }
                e->steps=0;
            } else if (e->z+1<room[1]) {
                e->action=4;
                e->steps=1;
            }
            continue;
        }
        if (!e->steps) {
            int clear=distance>1 && !(room[19]==0x11 && room[18]==3 && e->z<=6) &&
                      street_clear(atlas,enc,e,room,lane,depth,e->z-1);
            if (e->action==5 || e->action==8) {
                if (clear) { e->action=3; e->steps=2; }
                else choose_strafe(enc,e,depth);
                continue;
            }
            if (e->action==6 || e->action==9) {
                e->action=e->action==9 ? 10 : 7;
                continue;
            }
            if ((e->action==7 || e->action==10) && previous_sight==lane) e->engaged=1;
            choose_street_move(enc,e,depth,dx,clear,room[1]-1);
            continue;
        }
        e->steps--;
        if (e->action==7 || e->action==10) {
            int x=(int)e->x+(e->action==7 ? -20 : 20);
            int limit=camera_x(room[0])+108-10*(int)(distance<4 ? distance : 4);
            if ((e->action==7 && x<=96) || (e->action==10 && x>=limit)) {
                e->steps=0;
                e->action=e->action==7 ? 9 : 6;
            } else { e->x=x; e->sight=sight_lane(x,lane); }
        } else if (!e->steps) {
            unsigned z=e->action==4 ? e->z+1 : e->z-1;
            if (z>depth && z<room[1] && street_clear(atlas,enc,e,room,lane,depth,z)) {
                e->z=z;
                if (z==depth+1) e->attack_time=CLOSE_ATTACK_PERIOD-5760;
            }
        }
    }
    /* The first-person drawing pass ends with a masked blit (21:65E7).
       It shares RD3 with the actor RNG between decision passes. */
    enc->seed=(enc->seed&0xff00)|0x82;
    return changed;
}

static int advance_corridor(struct Encounter *enc,const uint8_t *room,unsigned depth,unsigned dt) {
    if (room[0]!=1) return 0;
    /* The first west corridor enters action 3, walks from depth 3 to 2
       after 49 LCD periods, then strikes 21 periods after the step. */
    enum { STEP=13440, FIRST_STRIDE=7680, STRIKE_LEAD=5760 };
    int changed=0;
    for (unsigned i=0;i<enc->count;i++) {
        struct Enemy *e=&enc->enemies[i];
        if (!e->health || e->reaction || e->z<=depth) continue;
        if (e->z==depth+1) {
            if (!e->contact) {
                /* When Duke closes the gap, the current stride finishes
                   before the strike. A recovered actor starts a fresh stride.
                   Bank 22 action 3 takes 8/12/12/4 updates at distance one. */
                static const uint8_t stride_updates[]={8,12,12,4};
                unsigned lead=stride_updates[e->type]*3*274u;
                if (e->action==3) {
                    unsigned end=e->walk_time<FIRST_STRIDE ? FIRST_STRIDE : STEP;
                    unsigned remaining=end>e->walk_time ? end-e->walk_time : 0;
                    /* Fraction of this stride in 8-bit fixed point. */
                    unsigned scale=end==FIRST_STRIDE ? (1u<<24)/FIRST_STRIDE
                                                     : (1u<<24)/(STEP-FIRST_STRIDE);
                    unsigned fraction=remaining*scale>>16;
                    lead=lead*fraction>>8;
                }
                e->attack_time=CLOSE_ATTACK_PERIOD-lead;
                e->contact=1;
            }
            continue;
        }
        e->contact=0;
        if (!corridor_step_clear(enc,e,room)) continue;
        e->walk_time+=dt;
        unsigned visual_dt=dt-(dt>>3);
        unsigned cycle=walk_pose_ticks[i]+(visual_dt>STEP ? STEP : visual_dt);
        if (cycle>=STEP) cycle-=STEP;
        walk_pose_ticks[i]=cycle;
        /* Two strides in 49 LCD periods. The first starts partway through
           pose 3. The trace changes pose at frames 4,12,21,28,34,40,46. */
        unsigned phase=cycle<7680 ? 3-(cycle>=1097)-(cycle>=3291)-(cycle>=5760)
                                  : 3-(cycle>=9325)-(cycle>=10971)-(cycle>=12617);
        if (e->action!=3 || e->phase!=phase) changed=1;
        e->action=3;
        walk_pose[i]=e->phase=phase;
        if (e->walk_time>=STEP) {
            e->walk_time-=STEP;
            e->z--;
            if (e->z==depth+1) {
                e->contact=1;
                e->attack_time=CLOSE_ATTACK_PERIOD-STRIKE_LEAD;
            }
            changed=1;
        }
    }
    return changed;
}

/* At one cell of depth an enemy in the player's view can strike. The
   original trace repeats a solitary close strike every 100-101 LCD periods. */
static int close_attacks(struct Encounter *enc,struct Inventory *inv,unsigned lane,unsigned depth,unsigned dt,int wide,unsigned skill) {
    unsigned period=wide ? CLOSE_ATTACK_PERIOD-HIT_PAUSE : CLOSE_ATTACK_PERIOD;
    static const uint8_t type_damage[]={2,1,4,5};
    int changed=0;
    for (unsigned i=0;i<enc->count;i++) {
        struct Enemy *e=&enc->enemies[i];
        int x=(int)e->x-camera_x(lane);
        if (!e->health || e->reaction || e->z!=depth+1 || (unsigned)x>=200 || (wide && !e->contact)) {
            e->attack_time=0;
            if (!wide && e->action==2) { e->action=8; e->phase=0; changed=1; }
            continue;
        }
        e->attack_time+=dt;
        if (e->attack_time>=period) {
            e->attack_time-=period;
            unsigned hit=6+2*skill+type_damage[e->type];
            unsigned absorb=hit/2<inv->armor ? hit/2 : inv->armor;
            inv->armor-=absorb;
            hit-=absorb;
            inv->health=inv->health>hit ? inv->health-hit : 0;
            e->action=2;
            e->phase=1;
            changed=1;
        } else if (e->action==2) {
            unsigned phase=e->attack_time<15435 || e->attack_time>=23760;
            if (e->phase!=phase) { e->phase=phase; changed=1; }
        }
    }
    return changed;
}

static unsigned capped(unsigned value,unsigned limit) {
    return value<limit ? value : limit;
}

static struct {
    const uint8_t *art;
    unsigned ticks;
} notice __attribute__((section(".ewram")));

static void hide_notice(void) {
    for (unsigned i=0;i<20;i++) shadow_oam[i][0]=0x200;
    notice.ticks=0;
}

static void pickup_notice(unsigned type) {
    /* Cartridge pickup samples at 23:6d64, indexed by item type. */
    static const uint8_t effects[]={1,1,5,14,23,1,1,1,1};
    const uint8_t *tiles=notice.art+(type-1)*(20*32+4);
    unsigned length=*(const uint32_t *)(tiles+20*32);
    hide_notice();
    copy_words(tiles,0x06010000+320*32,8*length);
    for (unsigned i=0;i<length;i++) {
        uint16_t *oam=shadow_oam[i];
        oam[0]=0;
        oam[1]=120-4*length+8*i;
        oam[2]=320+i;
    }
    notice.ticks=24576;
    sample(PLAYER_FX,effects[type-1]);
}

static int take_item(unsigned type,struct Inventory *inv,struct Weapon *gun) {
    switch (type) {
        case 1: if (inv->key) return 0; inv->key=1; break;
        case 2: if (inv->armor==100) return 0; inv->armor=100; break;
        case 3: if (inv->health==100) return 0; inv->health=capped(inv->health+30,100); break;
        case 4: case 5:
            if (!(type==4 && !(inv->weapons&4)) && inv->rockets==50) return 0;
            if (type==4) inv->weapons|=4;
            inv->rockets=capped(inv->rockets+5,50);
            break;
        case 6: case 9:
            if (!(type==9 && !(inv->weapons&2)) && inv->shells==50) return 0;
            if (type==9) inv->weapons|=2;
            inv->shells=capped(inv->shells+6,50);
            break;
        case 7: case 8:
            if (!(type==8 && !(inv->weapons&1)) && gun->remaining==200) return 0;
            if (type==8) inv->weapons|=1;
            gun->remaining=capped(gun->remaining+12,200);
            break;
        default: return 0;
    }
    pickup_notice(type);
    return 1;
}

static uint32_t collect(const uint8_t *room, unsigned lane, unsigned depth, uint32_t taken,
                        struct Inventory *inv, struct Weapon *gun) {
    for (unsigned i=0; i<room[32]; i++) {
        const uint8_t *item=room+36+4*i;
        if ((taken & (1u<<i)) || (unsigned)(item[0]-depth)>1) continue;
        int x=item[2]+50*((int)item[1]-(int)lane);
        if (item[1]!=lane && item[1]==1) x+=22;
        if (item[1]!=lane && item[1]==room[0]) x-=22;
        if (x<=20 || x>=180) continue;
        if (!take_item(item[3],inv,gun)) continue;
        taken|=1u<<i;
    }
    return taken;
}

static unsigned ammo(const struct Weapon *w,const struct Inventory *inv) {
    switch (w->kind) {
        case 1: return w->remaining;
        case 2: return inv->shells;
        case 3: return inv->rockets;
        default: return 0;
    }
}

static void cycle_weapon(struct Weapon *w,const struct Inventory *inv) {
    do { w->kind=(w->kind+1)&3; }
    while (w->kind && (!(inv->weapons&(1u<<(w->kind-1))) || !ammo(w,inv)));
    w->pose=0;
    w->delay=0;
    w->magazine=6;
}

static int advance(struct Weapon *w,struct Inventory *inv,unsigned dt,unsigned keys) {
    if (w->age<65536) w->age+=dt;
    if (dt < w->delay) {
        w->delay -= dt;
        return 0;
    }
    unsigned overdue = dt - w->delay;
    w->delay = 0;
    if (!(keys & (B|L|R))) return 0;
    if (w->kind && !ammo(w,inv)) { cycle_weapon(w,inv); return 0; }
    w->pose = keys & B ? 1 : keys & L ? 2 : 4;
    unsigned interval;
    switch (w->kind) {
        case 1:
            if (!w->magazine) w->magazine=6;
            w->remaining--; w->magazine--;
            interval=w->magazine || !w->remaining ? SHOT : RELOAD;
            if (w->pose!=1) interval+=FLASH;
            break;
        case 2: inv->shells--; interval=21120; break;
        case 3: inv->rockets--; interval=w->pose==1 ? 11000 : 21900; break;
        default: interval=13440; break;
    }
    w->age = overdue;
    w->delay = overdue < interval ? interval - overdue : 0;
    return 1;
}

static unsigned weapon_frame(const struct Weapon *w) {
    unsigned idle=w->kind*24;
    if (!w->pose) return idle;
    unsigned direction=w->pose==1 ? 0 : w->pose==2 ? 1 : 2;
    unsigned hand=idle+6*direction,t=w->age;
    if (!direction) {
        static const uint16_t windup[]={3017,823,1371,2194};
        if (t<windup[w->kind]) return idle;
        t-=windup[w->kind];
    }
    switch (w->kind) {
        case 0: return t<4950 ? hand+2 : t<9900 ? hand+3 : idle;
        case 1:
            if (t<FLASH) return hand+2;
            if (direction && t<SHOT) return hand+3;
            if (direction) t-=FLASH;
            if (!w->magazine && w->remaining && t<19750)
                return idle+18+(t<4660 ? 1 : t<9600 ? 2 : t<14540 ? 3 : 4);
            return idle;
        case 2: return t<5500 ? hand+2 : t<11000 ? idle+19 : t<16300 ? idle+20 : idle;
        default:
            if (t<5500) return hand+2;
            if (direction && t<11000) return hand+3;
            if (direction && t<16500) return hand+4;
            return idle;
    }
}

static void draw_weapon(const uint8_t *frame) {
    unsigned width=frame[0],height=frame[1],tile=0;
    int left=20+(200-width)/2,top=136-height;
    unsigned object=24;
    const uint8_t *pieces=frame+4;
    for (unsigned y=0;y<height;y+=32)
        for (unsigned x=0;x<width;x+=32) {
            copy_words(pieces,0x06010000+tile*32,128);
            uint16_t *oam=shadow_oam[object++];
            oam[0]=(top+y)&255;
            oam[1]=0x8000|((left+x)&511);
            oam[2]=tile;
            pieces+=512; tile+=16;
        }
    for (;object<48;object++) shadow_oam[object][0]=0x200;
}

static const uint8_t digits[10][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}
};

static void score_number(unsigned y,unsigned value) {
    static const unsigned powers[]={100000,10000,1000,100,10,1};
    volatile uint16_t *chars=(volatile uint16_t *)0x06000000;
    unsigned x=20+64;
    int started=0;
    value=capped(value,999999);
    for (unsigned i=0;i<6;i++,x+=8) {
        unsigned n=0;
        while (value>=powers[i]) { value-=powers[i]; n++; }
        if (!n && !started && i!=5) continue;
        started=1;
        for (unsigned row=0;row<5;row++)
            for (unsigned col=0;col<3;col++)
                if (digits[n][row]&(4u>>col)) {
                    unsigned px=x+col,py=y+row;
                    unsigned word=((py/8)*30+px/8)*16+2*(py%8)+(px%8)/4;
                    unsigned shift=4*(px%4);
                    chars[word]=(chars[word]&~(15u<<shift))|(1u<<shift);
                }
    }
}

/* BG0's HUD cells use dedicated tiles. Change only the affected glyphs. */
static void digit(unsigned x, unsigned value) {
    volatile uint16_t *tiles = (volatile uint16_t *)0x06000000;
    for (unsigned row=0; row<5; row++) {
        unsigned y = 151 + row;
        for (unsigned col=0; col<3; col++) {
            unsigned px = x + col;
            unsigned tile = (y/8)*30 + px/8;
            unsigned index = tile*16 + (y%8)*2 + (px%8)/4;
            unsigned shift = (px%4)*4;
            unsigned ink = value<10 && (digits[value][row] & (4u>>col)) ? 1 : 4;
            tiles[index] = (tiles[index] & ~(15u << shift)) | (ink << shift);
        }
    }
}

static void hud_number(unsigned x,unsigned value,unsigned *previous) {
    if (*previous==value) return;
    *previous=value;
    unsigned tens=(value*205u)>>11;
    unsigned hundreds=(tens*205u)>>11;
    digit(x,hundreds ? hundreds : 10);
    digit(x+8,tens ? tens-10*hundreds : 10);
    digit(x+16,value-10*tens);
}

static void weapon_label(const uint8_t *glyphs) {
    volatile uint16_t *tiles=(volatile uint16_t *)0x06000000;
    for (unsigned col=0;col<8;col++)
        for (unsigned row=0;row<7;row++) {
            unsigned bits=*glyphs++,y=150+row;
            for (unsigned i=0;i<8;i++) {
                unsigned x=92+col*8+i;
                unsigned index=((y/8)*30+x/8)*16+(y%8)*2+(x%8)/4;
                unsigned shift=4*(x%4);
                unsigned ink=(bits&(32u>>i)) ? 1 : 4;
                tiles[index]=(tiles[index]&~(15u<<shift))|(ink<<shift);
            }
        }
}

static void reset_rooms(unsigned count,uint32_t *taken,uint8_t *unlocked,uint8_t damage[][18]) {
    for (unsigned i=0;i<count;i++) {
        taken[i]=0;
        unlocked[i]=0;
        volatile uint8_t *row=damage[i];
        for (unsigned j=0;j<18;j++) {
            row[j]=0;
            drops[i][j].type=0;
        }
    }
}

/* Keep the live room and encounter in IWRAM, also readable by emulator debuggers. */
static volatile struct {
    unsigned level,room_id,lane,depth;
} position __attribute__((section(".iwram.position")));
static struct Encounter encounter __attribute__((section(".iwram.encounter")));
static struct Weapon w __attribute__((section(".iwram.weapon")));
static struct Inventory inventory __attribute__((section(".iwram.inventory")));

/* Timer 0 clocks signed PCM into both Direct Sound FIFOs. Timer 2 (16,384 Hz)
   also schedules both music voices and the opening independently of rendering. */
static struct {
    const uint32_t *directory;
    const uint8_t *notes[2], *track[2];
    unsigned remaining[2], sample_ticks[2];
    const int8_t *speech;
    unsigned speech_length, mixed_end;
} audio __attribute__((section(".ewram")));

/* The longest line is 35,556 bytes. Allow one trailing effect and DMA
   lookahead past the frame where sound_tick stops the FIFO. */
static int8_t speech_mix[49152] __attribute__((section(".ewram"),aligned(4)));
static unsigned muted __attribute__((section(".ewram")));

static const uint8_t *sound_data(unsigned id) {
    return (const uint8_t *)audio.directory+audio.directory[id];
}

/* 4.9152 MHz source, timer period 208, one DAC sample per three interrupts. */
enum { PCM_DIVISOR=2130 };
/* 25 ms is exactly 2048 fifths of a Timer 2 tick. */
enum { MUSIC_TICK_SCALE=5, MUSIC_UNIT=2048 };
enum { TITLE_SCORE=27, EPISODE_SCORE=29, DEATH_SCORE=35 };

static void stop_sample(unsigned voice) {
    REG16(0x040000c6+12*voice)=0;
    REG16(0x04000082)=0x330e|(voice ? 0x8000 : 0x0800);
    audio.sample_ticks[voice]=0;
    if (voice==PLAYER_FX) {
        audio.speech=0;
        REG16(0x04000106)=0;
    }
}

static void sample(unsigned voice,unsigned id) {
    if (muted) return;
    const uint32_t *pcm=(const uint32_t *)sound_data(id);
    unsigned speech=voice==PLAYER_FX &&
        ((1u<<id)&((1u<<2)|(1u<<4)|(1u<<5)|(1u<<14)|(1u<<16)|
                    (1u<<17)|(1u<<18)|(1u<<20)|(1u<<23)|(1u<<24)));
    const int8_t *source=(const int8_t *)(pcm+1);
    unsigned length=*pcm;
    if (audio.speech && voice==PLAYER_FX && !speech) {
        /* Timer 1 counts DAC periods. Start beyond the DMA FIFO lookahead.
           Replace the previous effect while retaining every speech sample. */
        unsigned cursor=REG16(0x04000104);
        unsigned start=cursor+64;
        unsigned end=start+length;
        if (cursor<audio.speech_length && end+256<sizeof speech_mix) {
            unsigned limit=end>audio.mixed_end ? end : audio.mixed_end;
            if (end>audio.speech_length) limit+=256;
            for (unsigned i=start;i<limit;i++) {
                int line=i<audio.speech_length ? audio.speech[i] : 0;
                speech_mix[i]=i<end ? (line+source[i-start])/2 : line;
            }
            audio.mixed_end=end;
            unsigned total=end>audio.speech_length ? end : audio.speech_length;
            audio.sample_ticks[voice]=((total-cursor)*PCM_DIVISOR)>>10;
            return;
        }
    }
    stop_sample(voice);
    if (speech && length+256<sizeof speech_mix) {
        audio.speech=source;
        audio.speech_length=length;
        audio.mixed_end=0;
        copy_words(source,(uintptr_t)speech_mix,(length+3)/4);
        for (unsigned i=length;i<length+256;i++) speech_mix[i]=0;
        source=speech_mix;
        REG16(0x04000104)=0;
        REG16(0x04000106)=0x84;
    }
    *(volatile uintptr_t *)(0x040000bc+12*voice)=(uintptr_t)source;
    *(volatile uintptr_t *)(0x040000c0+12*voice)=0x040000a0+4*voice;
    REG16(0x040000c6+12*voice)=0xb640;
    audio.sample_ticks[voice]=(*pcm*PCM_DIVISOR)>>10;
}

static void music(unsigned first) {
    REG16(0x04000080)=muted ? 0 : 0x3377;
    for (unsigned i=0;i<2;i++) {
        audio.track[i]=sound_data(first+i);
        audio.notes[i]=audio.track[i];
        audio.remaining[i]=0;
    }
}

static void stop_music(void) {
    audio.notes[0]=audio.notes[1]=0;
    REG16(0x04000080)=0;
}

static void music_tick(unsigned ticks) {
    if (!audio.notes[0]) return;
    ticks*=MUSIC_TICK_SCALE;
    for (;;) {
        /* The first voice sets the shared repeat point. A voice that ends
           early stays silent until then. */
        if (!audio.remaining[0] && !audio.notes[0][3]) {
            for (unsigned i=0;i<2;i++) {
                audio.notes[i]=audio.track[i];
                audio.remaining[i]=0;
            }
        }
        unsigned step=ticks;
        for (unsigned i=0;i<2;i++) {
            if (!audio.notes[i]) continue;
            if (!audio.remaining[i]) {
                const uint8_t *n=audio.notes[i];
                unsigned freq=*(const uint16_t *)n;
                unsigned volume=freq ? (n[2]*(i ? 6u : 8u))>>5 : 0;
                /* Approximate the cartridge's two decays with the PSG
                   envelope. Only the score controls when the next note starts. */
                REG16(0x04000062+6*i)=(volume<<12)|0x0280;
                REG16(0x04000064+8*i)=0x8000|freq;
                if (!n[3]) {
                    audio.notes[i]=0;
                    continue;
                }
                audio.notes[i]=n+4;
                audio.remaining[i]=n[3]*MUSIC_UNIT;
            }
            if (step>audio.remaining[i]) step=audio.remaining[i];
        }
        if (!ticks) return;
        audio.remaining[0]-=step;
        if (audio.notes[1]) audio.remaining[1]-=step;
        ticks-=step;
    }
}

static void sound_tick(unsigned ticks) {
    for (unsigned i=0;i<2;i++) if (audio.sample_ticks[i]) {
        if (ticks>=audio.sample_ticks[i]) stop_sample(i);
        else audio.sample_ticks[i]-=ticks;
    }
    music_tick(ticks);
}

static unsigned next_tick(unsigned *previous) {
    while (REG16(0x04000006)>=160) {}
    while (REG16(0x04000006)<160) {}
    unsigned now=REG16(0x04000108);
    unsigned dt=(uint16_t)(now-*previous);
    *previous=now;
    sound_tick(dt);
    return dt;
}

/* Byte-wide, commit-last SRAM record. Each build keeps its own latest
   level-entry checkpoint, just as the cartridge checks its system record. */
static volatile uint8_t *save_slot __attribute__((section(".ewram")));
static const uint8_t *password_tiles __attribute__((section(".ewram")));
static uint8_t passcode[4] __attribute__((section(".ewram")));


static unsigned saved_crc(const uint8_t *data) {
    unsigned crc=0xa7;
    for (unsigned i=0;i<16;i++) {
        crc^=data[i];
        for (unsigned j=0;j<8;j++) crc=((crc<<1)^((crc&128) ? 7 : 0))&255;
    }
    return crc;
}

static void save_entry(unsigned level,unsigned skill,unsigned frags) {
    uint8_t data[16]={level,skill,inventory.health,inventory.armor,inventory.shells,inventory.rockets,
        inventory.weapons,w.kind,w.remaining,w.magazine,frags,frags>>8};
    unsigned seed=(REG16(0x04000108)<<16)^saved_crc(data);
    for (unsigned i=0;i<4;i++) {
        seed=1664525u*seed+1013904223u;
        data[12+i]=passcode[i]=(seed>>16)*20u>>16;
    }
    save_slot[0]=0;
    for (unsigned i=0;i<16;i++) save_slot[1+i]=data[i];
    save_slot[17]=saved_crc(data);
    save_slot[0]=0xd3;
}

static int load_entry(const uint8_t *code,unsigned *level,unsigned *skill,unsigned *frags) {
    uint8_t data[16];
    if (save_slot[0]!=0xd3) return 0;
    for (unsigned i=0;i<16;i++) data[i]=save_slot[1+i];
    if (saved_crc(data)!=save_slot[17] || data[0]>=15 || data[1]>=4 || data[7]>=4) return 0;
    for (unsigned i=0;i<4;i++) if (data[12+i]!=code[i]) return 0;
    for (unsigned i=0;i<4;i++) passcode[i]=code[i];
    *level=data[0]; *skill=data[1]; *frags=data[10]+256u*data[11];
    inventory=(struct Inventory){0,data[3],data[2],data[4],data[5],data[6]};
    w=(struct Weapon){data[8],data[9],0,FLASH*2,0,data[7]};
    return 1;
}

static void draw_code(const uint8_t *code,unsigned length,unsigned x,unsigned y) {
    // Write four-pixel words into BG0's linear display tiles.
    for (unsigned i=0;i<length;i++) {
        const uint16_t *glyph=(const uint16_t *)(password_tiles+32*code[i]);
        for (unsigned row=0;row<8;row++) for (unsigned half=0;half<2;half++) {
            unsigned px=x+8*i+4*half,py=y+row;
            unsigned at=16*(30*(py/8)+px/8)+2*(py&7)+(px&7)/4;
            ((volatile uint16_t *)0x06000000)[at]=glyph[2*row+half];
        }
    }
}

enum MenuPage { INTRO, MAIN, EPISODE, SKILL, NUMBER, SPLASH, ENTERING,
                OPTIONS, SOUNDS, PASSWORD };

static unsigned opening(const struct Atlas *atlas,const uint32_t *opening_data,
                        const uint32_t *cards,unsigned *skill,unsigned *frags,int *resumed) {
    unsigned count=*opening_data++;
    const uint32_t *menu=opening_data+3*count;
    unsigned page=INTRO,cursor=0,episode=0,frame=0,level=0,length=0;
    uint8_t code[4]={0};
    int invalid=0;
    password_tiles=(const uint8_t *)(menu+45);
    unsigned elapsed=0, old_keys=0, previous=REG16(0x04000108);
    unsigned previous_page=INTRO;
    int redraw=1;
    reset_backgrounds();
    for (;;) {
        unsigned dt=next_tick(&previous);
        unsigned keys=~REG16(0x04000130)&1023, pressed=keys&~old_keys;
        old_keys=keys;
        elapsed+=dt;
        if (!page) {
            enum { COPYRIGHT=201*274u, COPYRIGHT_VISIBLE=384*274u, DUKE_TITLE=966*274u };
            unsigned shown=elapsed-dt;
            int next=pressed&(A|START);
            if (shown<150*274u && elapsed>=150*274u) sample(PLAYER_FX,11);
            if (next && shown<COPYRIGHT) elapsed=COPYRIGHT_VISIBLE;
            else if (next && shown<DUKE_TITLE) elapsed=DUKE_TITLE;
            while (frame+1<count && elapsed>=opening_data[3*(frame+1)]*274u) {
                frame++;
                redraw=1;
            }
            if (!audio.notes[0] && elapsed>=DUKE_TITLE) music(TITLE_SCORE);
            if (elapsed>=1635*274u || (next && shown>=DUKE_TITLE)) {
                page=MAIN;
                if (!audio.notes[0]) music(TITLE_SCORE);
                redraw=1;
            }
        } else if (page<=3) {
            unsigned options=page==SKILL ? 4 : 3;
            if (pressed&(UP|DOWN)) {
                cursor=(pressed&UP) ? (cursor ? cursor-1 : options-1) :
                    (cursor+1==options ? 0 : cursor+1);
                redraw=1;
            } else if ((pressed&B) && page>1) {
                page--;
                cursor=page==2 ? episode : 0;
                redraw=1;
            } else if (pressed&(A|START)) {
                if (page==MAIN && cursor==1) {
                    page=OPTIONS; cursor=0; redraw=1;
                    continue;
                }
                if (page==MAIN && cursor==2) {
                    stop_sample(PLAYER_FX); stop_sample(MONSTER_FX);
                    stop_music();
                    page=INTRO; frame=elapsed=cursor=0; redraw=1;
                    continue;
                }
                if (page==EPISODE) { episode=cursor; level=5*episode; }
                if (page==3) *skill=cursor;
                page++;
                cursor=page==3 ? *skill : page==2 ? episode : 0;
                elapsed=0;
                redraw=1;
            }
        } else if (page==OPTIONS) {
            if (pressed&B) { page=MAIN; cursor=1; }
            else if (pressed&(A|START)) {
                if (cursor==0) { page=PASSWORD; length=0; invalid=0; }
                else if (cursor==1) page=SOUNDS;
                else page=MAIN;
                cursor=page==MAIN ? 1 : 0;
            } else if (pressed&UP) cursor=0;
            else if (pressed&(DOWN|LEFT)) cursor=1;
            else if (pressed&RIGHT) cursor=2;
            redraw|=pressed!=0;
        } else if (page==SOUNDS) {
            if ((pressed&B) || (cursor && (pressed&(A|START)))) { page=OPTIONS; cursor=1; }
            else if (pressed&(UP|DOWN)) cursor^=1;
            else if (!cursor && (pressed&(RIGHT|LEFT|A|START))) {
                muted=(pressed&RIGHT) ? 1 : (pressed&LEFT) ? 0 : !muted;
                save_slot[18]=muted ? 0xad : 0;
                REG16(0x04000080)=muted ? 0 : 0x3377;
                stop_sample(PLAYER_FX); stop_sample(MONSTER_FX);
            }
            redraw|=pressed!=0;
        } else if (page==PASSWORD) {
            if (pressed) invalid=0;
            if ((pressed&(START|R)) && length==4) {
                if (load_entry(code,&level,skill,frags)) {
                    *resumed=1; episode=(level>=5)+(level>=10);
                    page=ENTERING; elapsed=0; music(EPISODE_SCORE+2*episode);
                } else { invalid=1; length=0; }
            } else if ((pressed&B) && length) length--;
            else if ((pressed&B) || ((pressed&A) && cursor==20)) { page=OPTIONS; cursor=0; }
            else if ((pressed&A) && length<4) code[length++]=cursor;
            else if (pressed&UP) cursor=cursor>=10 ? cursor-10 : cursor;
            else if (pressed&DOWN) cursor=cursor<10 ? cursor+10 : 20;
            else if (cursor<20 && (pressed&LEFT) && cursor!=0 && cursor!=10) cursor--;
            else if (cursor<20 && (pressed&RIGHT) && cursor!=9 && cursor!=19) cursor++;
            redraw|=pressed!=0;
        } else {
            unsigned duration=page==4 ? 176 : 122;
            if (elapsed>=duration*274u || (pressed&(A|START))) {
                if (page==ENTERING) break;
                page++;
                elapsed=0;
                if (page==SPLASH) music(EPISODE_SCORE+2*episode);
                redraw=1;
            }
        }
        if (redraw) {
            if (!page) {
                update_layer(atlas->backgrounds,0,opening_data[3*frame+1],20);
                update_layer(atlas->backgrounds,1,opening_data[3*frame+2],20);
                REG16(0x04000000)=0x0300;
            } else {
                unsigned layout;
                switch (page) {
                    case MAIN: layout=menu[cursor]; break;
                    case EPISODE: layout=menu[3+cursor]; break;
                    case SKILL: layout=menu[6+cursor]; break;
                    case NUMBER: layout=menu[10+episode]; break;
                    case SPLASH: layout=menu[13+episode]; break;
                    case OPTIONS: layout=menu[16+cursor]; break;
                    case SOUNDS: layout=menu[19+2*muted+cursor]; break;
                    case PASSWORD: layout=menu[23+(invalid ? 21 : cursor)]; break;
                    default: layout=cards[3*level+2]; break;
                }
                // Entered password glyphs are drawn into the layout tiles.
                if (page==PASSWORD || previous_page==PASSWORD) previous_layout[0]=0;
                update_layer(atlas->backgrounds,0,layout,20);
                if (page==PASSWORD) draw_code(code,length,108,100);
                REG16(0x04000000)=0x0100;
            }
            previous_page=page;
            redraw=0;
        }
    }
    return level;
}


void play(void) {
    for (unsigned i=0;i<128;i++) shadow_oam[i][0]=0x200;
    pending_actor_pieces=scenery_pending=0;
    w=(struct Weapon){48, 6, 0, FLASH*2, 0, 1};
    const uint8_t *campaign=assets + 32 + 19200;
    const uint32_t *levels=(const uint32_t *)campaign;
    const uint8_t *weapons=campaign+levels[6+levels[0]];
    notice.art=weapons+16*6*4+4*56;
    hide_notice();
    const uint32_t *cards=(const uint32_t *)(campaign+levels[1+levels[0]]);
    unsigned last_weapon_frame=~0u,last_kind=~0u;
    struct Atlas atlas={campaign+levels[2+levels[0]],campaign+levels[3+levels[0]],campaign+levels[4+levels[0]]};
    audio.directory=(const uint32_t *)(campaign+levels[8+levels[0]]);
    audio.notes[0]=audio.notes[1]=0;
    REG16(0x04000084)=0x80;
    REG16(0x04000080)=0x3377;
    stop_sample(PLAYER_FX);
    stop_sample(MONSTER_FX);
    REG16(0x04000100)=(uint16_t)-PCM_DIVISOR;
    REG16(0x04000102)=0x80;
    REG16(0x0400010a)=0x83;
    save_slot=(volatile uint8_t *)(0x0e000000u+32*(levels[5+levels[0]]&7));
    muted=save_slot[18]==0xad;
    REG16(0x04000080)=muted ? 0 : 0x3377;
    inventory=(struct Inventory){0,0,100,0,0,1};
    unsigned skill=1,total_frags=0;
    int resumed=0;
    const uint32_t *opening_data=(const uint32_t *)(campaign+levels[7+levels[0]]);
    const uint32_t *menu=opening_data+1+3*opening_data[0];
    position.level=opening(&atlas,opening_data,cards,&skill,&total_frags,&resumed);
    if (!resumed) save_entry(position.level,skill,0);
    full_scene(assets+32);
    unsigned frags=0;
    unsigned next_level=position.level+1;
    int exit_active=0;
    unsigned exit_time=0, exit_screen=0;
    const uint32_t *exit_view=0;
    unsigned death_time=0;
    unsigned hurt_pause=0;
    unsigned taunt_time=0,taunt_seed=0;
    const uint8_t *rooms=campaign+levels[1+position.level];
    unsigned room_count=*(const uint32_t *)rooms;
    uint32_t taken[256];
    uint8_t unlocked[256];
    uint8_t damage[256][18];
    reset_rooms(room_count,taken,unlocked,damage);
    position.room_id=0;
    const uint32_t *directory=(const uint32_t *)rooms+1;
    const uint8_t *room=rooms+directory[0];
    unsigned open_doors=0;
    position.lane=room[2];
    position.depth=0;
    unsigned walk_delay=0;
    enter_encounter(&encounter,room,damage[0],drops[0]);
    reset_backgrounds();
    encounter.view=show_view(&atlas,room,position.lane,position.depth,0,taken[0]);
    REG16(0x04000040)=(20<<8)|220;
    REG16(0x04000044)=136;
    REG16(0x04000048)=0x17;
    REG16(0x0400004a)=1;
    prepare_pause_dialog();
    int show_gameplay=1;
    draw_enemies(&atlas,&encounter,position.lane,position.depth);
    REG16(0x0400010a) = 0x83;
    unsigned previous = REG16(0x04000108);
    if (position.level==0) sample(PLAYER_FX,17);
    unsigned old_keys = START | A;
    unsigned last_health=255,last_armor=255,last_ammo=255,last_key=255;
    int paused = 0;
    uint16_t pause_sequence[6];
    unsigned pause_count=0;
    int entering = 1;
    for (;;) {
        while (REG16(0x04000006) >= 160) {}
        while (REG16(0x04000006) < 160) {}
        unsigned now = REG16(0x04000108);
        publish_objects();
        if (show_gameplay) {
            REG16(0x04000000)=0x3740;
            show_gameplay=0;
        }
        unsigned dt = (uint16_t)(now - previous);
        previous = now;
        sound_tick(dt);
        unsigned keys = ~REG16(0x04000130) & 1023;
        if (entering) {
            entering = (keys & (A|START)) != 0;
            keys &= ~(A|START);
        }
        unsigned pressed=keys & ~old_keys;
        int retry=0;
        if (!inventory.health) {
            old_keys=keys;
            unsigned menu_at=99000; /* 361 LCD periods from the fatal hit. */
            if (death_time<menu_at) {
                death_time+=dt;
                if (!audio.notes[0] && death_time>=238*274u) music(DEATH_SCORE);
                if (death_time>=menu_at) {
                    REG16(0x04000000)=0x0100;
                    previous_layout[0]=0;
                    update_layer(atlas.backgrounds,0,cards[3*levels[0]],20);
                    draw_code(passcode,4,104,120);
                }
                continue;
            }
            if (!(pressed&A)) continue;
            retry=1;
        }
        if (exit_active || retry) {
            old_keys=keys;
            if (retry) {
                next_level=position.level;
                total_frags-=frags;
                inventory=(struct Inventory){0,0,100,0,0,1};
                w=(struct Weapon){48,6,0,FLASH*2,0,1};
                death_time=0;
            } else {
                unsigned before=exit_time;
                exit_time+=dt;
                if ((before<11*274 && exit_time>=11*274) ||
                    (before<129*274 && exit_time>=129*274)) sample(PLAYER_FX,11);
                unsigned episode=(next_level>=5)+(next_level>=10);
                int new_episode=episode!=(unsigned)((position.level>=5)+(position.level>=10));
                unsigned splash_at=122100+176*274;
                unsigned entering_at=new_episode ? splash_at+122*274 : 122100;
                if (new_episode && (pressed&(A|START))) {
                    if (exit_screen==8 && exit_time<splash_at) exit_time=splash_at;
                    if (exit_screen==9 && exit_time<entering_at) exit_time=entering_at;
                }
                unsigned screen=exit_time>=entering_at ? 10 :
                    exit_time>=splash_at ? 9 : exit_time>=122100 ? 8 : exit_time>=80100 ? 7 :
                    exit_time>=166*274 ? 6 : exit_time>=117*274 ? 5 : exit_time>=93*274 ? 4 :
                    exit_time>=69*274 ? 3 : exit_time>=48*274 ? 2 : exit_time>=24*274 ? 1 : 0;
                if (screen!=exit_screen) {
                    if (screen<=2) update_layer(atlas.backgrounds,1,exit_view[4+screen],17);
                    else {
                        unsigned card=screen<=5 ? 3*levels[0]+1+(screen==4) :
                            screen>=8 ? 3*next_level+2 : 3*position.level+screen-6;
                        unsigned layout=screen==8 ? menu[10+episode] :
                            screen==9 ? menu[13+episode] : cards[card];
                        if (exit_screen==7) sample(PLAYER_FX,4);
                        if (screen==9) music(EPISODE_SCORE+2*episode);
                        REG16(0x04000000)=0x0100;
                        previous_layout[0]=0;
                        update_layer(atlas.backgrounds,0,layout,20);
                    }
                    if (screen==7) {
                        score_number(96,total_frags);
                        score_number(128,frags);
                    }
                    exit_screen=screen;
                }
                if (exit_time<entering_at+32900) continue;
            }
            unsigned episode=(next_level>=5)+(next_level>=10);
            if (retry)
                music(EPISODE_SCORE+2*episode);
            position.level=next_level;
            frags=0;
            rooms=campaign+levels[1+position.level];
            directory=(const uint32_t *)rooms+1;
            room_count=directory[-1];
            position.room_id=0;
            room=rooms+directory[0];
            reset_rooms(room_count,taken,unlocked,damage);
            position.lane=room[2];
            position.depth=0;
            open_doors=0;
            inventory.key=0;
            if (!retry) save_entry(position.level,skill,total_frags);
            walk_delay=0;
            hurt_pause=0;
            taunt_time=0;
            exit_active=0;
            entering=1;
            hide_notice();
            full_scene(assets+32);
            reset_backgrounds();
            enter_encounter(&encounter,room,damage[0],drops[0]);
            encounter.view=show_view(&atlas,room,position.lane,position.depth,0,0);
            draw_enemies(&atlas,&encounter,position.lane,position.depth);
            last_health=last_armor=last_ammo=last_key=255;
            last_kind=~0u;
            show_gameplay=1;
            if (retry && position.level==0) sample(PLAYER_FX,17);
            continue;
        }
        if (pressed & START) {
            paused=!paused; pause_count=0;
            /* WIN0 clips the dialog over all sprites. WIN1 preserves the
               gameplay window outside it, including the bezel and HUD. */
            REG16(0x04000040)=paused ? (36<<8)|204 : (20<<8)|220;
            REG16(0x04000044)=paused ? (48<<8)|112 : 136;
            REG16(0x04000048)=paused ? 0x1708 : 0x17;
            REG16(0x04000000)=paused ? 0x7f40 : 0x3740;
        }
        else if (paused) pause_input(pause_sequence,&pause_count,pressed,&inventory,&w);
        old_keys = keys;
        if (!paused) {
            if (notice.ticks && dt>=notice.ticks) hide_notice();
            else if (notice.ticks) notice.ticks-=dt;
            if (taunt_time && dt>=taunt_time && !audio.speech) {
                static const uint8_t taunts[]={18,24,20};
                taunt_time=0;
                unsigned choice=speech_roll(&taunt_seed,now,sizeof taunts);
                sample(PLAYER_FX,taunts[choice]);
            } else if (taunt_time) taunt_time=dt<taunt_time ? taunt_time-dt : 1;
            int changed=animate_hits(&encounter,dt);
            if (advance(&w,&inventory,dt,keys)) {
                /* PCM directory reads at 20:5114 during an actual attack. */
                static const uint8_t effects[]={3,10,12,11};
                sample(PLAYER_FX,effects[w.kind]);
                int impact=fire_at(&encounter,position.lane,position.depth,w.pose,w.kind,skill,damage[position.room_id]);
                if (impact==2) {
                    frags++;
                    total_frags++;
                    /* 31 actor updates in the cartridge take about 114 LCD periods. */
                    if (!taunt_time && speech_roll(&taunt_seed,now,8)==0)
                        taunt_time=114*274u;
                }
                changed|=impact;
            }
            unsigned elapsed=dt;
            unsigned held=elapsed<hurt_pause ? elapsed : hurt_pause;
            elapsed-=held;
            hurt_pause-=held;
            unsigned health=inventory.health;
            if (elapsed) {
                changed|=advance_corridor(&encounter,room,position.depth,elapsed);
                changed|=advance_street(&atlas,&encounter,room,position.lane,position.depth,elapsed);
                changed|=close_attacks(&encounter,&inventory,position.lane,position.depth,elapsed,room[0]>1,skill);
            }
            if (inventory.health && inventory.health<health) sample(MONSTER_FX,9);
            if (room[0]>1 && inventory.health<health) hurt_pause=HIT_PAUSE;
            if (!inventory.health) {
                stop_music();
                stop_sample(MONSTER_FX);
                sample(PLAYER_FX,21);
                if (changed) draw_enemies(&atlas,&encounter,position.lane,position.depth);
                hud_number(20+13,0,&last_health);
                hud_number(20+41,inventory.armor,&last_armor);
                continue;
            }
            unsigned direction=keys & (UP|DOWN|LEFT|RIGHT);
            int door=-1;
            if (position.depth+1 == room[1]) {
                for (int i=0; i<3; i++) {
                    unsigned spec=room[4+4*i];
                    if (spec!=255 && (spec&31)==position.lane) {
                        door=i;
                        if (pressed&A) {
                            unsigned bit=1u<<i;
                            if ((spec&128) && !(unlocked[position.room_id]&bit)) {
                                if (inventory.key) {
                                    inventory.key=0;
            if (!retry) save_entry(position.level,skill,total_frags);
                                    unlocked[position.room_id]|=bit;
                                    sample(PLAYER_FX,13);
                                } else sample(PLAYER_FX,16);
                            } else {
                                open_doors ^= bit;
                                if (room[5+4*i]!=254) sample(PLAYER_FX,0);
                                encounter.view=show_view(&atlas,room,position.lane,position.depth,open_doors,taken[position.room_id]);
                                changed=1;
                                if (room[5+4*i]==254 && (open_doors&bit)) {
                                    next_level=position.level+1;
                                    /* The full campaign visits Launching Facility from
                                       Toxic Dump, then rejoins the route at Abyss. */
                                    if (levels[5+levels[0]]==7) {
                                        if (position.level==2) next_level=4;
                                        if (position.level==4) next_level=3;
                                        if (position.level==3) next_level=5;
                                    }
                                    unsigned bonus=next_level==4 ? 4 : next_level==9 ? 2 : next_level==14 ? 1 : 0;
                                    if (bonus && !(levels[5+levels[0]]&bonus)) next_level++;
                                    if (next_level==levels[0]) next_level=0;
                                    exit_active=1;
                                    exit_view=(const uint32_t *)(room+((const uint32_t *)(room+20))[i]);
                                    exit_time=0;
                                    exit_screen=0;
                                }
                            }
                        }
                        break;
                    }
                }
            }
            if (door<0 && (pressed&A) && !w.delay) cycle_weapon(&w,&inventory);
            if (walk_delay > dt) walk_delay-=dt;
            else if (direction) {
                unsigned x=position.lane, z=position.depth;
                const uint8_t *old_room=room;
                switch (direction) {
                    case UP:
                        if (z+1<room[1]) z++;
                        else if (room[0]==1 && room[1]>1 && (room[3]&0x30) && room[5]<room_count) {
                            position.room_id=room[5];
                            room=rooms+directory[position.room_id];
                            x=1;
                            z=0;
                        } else if (door>=0 && (!!(open_doors & (1u<<door)) != !(room[4+4*door]&0x60))) {
                            const uint8_t *exit=room+4+4*door;
                            if (exit[1]<room_count) {
                                position.room_id=exit[1];
                                room=rooms+directory[position.room_id];
                                x=exit[2];
                                z=0;
                                open_doors=0;
                            }
                        }
                        break;
                    case DOWN:
                        if (z>0) z--;
                        else {
                            unsigned back=room[16];
                            if (room[17]!=255 && x!=1)
                                back=x==room[0] ? room[17] : 255;
                            if (back<room_count) {
                                position.room_id=back;
                                room=rooms+directory[position.room_id];
                                x=1;
                                z=room[1]-1;
                            }
                        }
                        break;
                    case LEFT: case RIGHT:
                        if (room[3]==0x30 && room[1]==1) {
                            const uint8_t *branch=room+(direction==LEFT ? 8 : 4);
                            if (branch[1]<room_count) {
                                position.room_id=branch[1];
                                room=rooms+directory[position.room_id];
                                x=branch[2];
                                z=0;
                            }
                        } else if (direction==LEFT && x>1) x--;
                        else if (direction==RIGHT && x<room[0]) x++;
                        break;
                }
                if (old_room==room && ((view_entry(room,x,z)&0x80000000u) ||
                    ((x!=position.lane || z!=position.depth) && occupied(&encounter,x,z)))) {
                    x=position.lane;
                    z=position.depth;
                }
                if (old_room!=room) {
                    open_doors=0;
                    enter_encounter(&encounter,room,damage[position.room_id],drops[position.room_id]);
                }
                if (position.lane!=x || position.depth!=z || old_room!=room) {
                    position.lane=x;
                    position.depth=z;
                    taken[position.room_id]=collect(room,x,z,taken[position.room_id],&inventory,&w);
                    encounter.view=show_view(&atlas,room,x,z,open_doors,taken[position.room_id]);
                    changed=1;
                }
                unsigned overdue=walk_delay ? dt-walk_delay : 0;
                unsigned step=position.room_id==1 ? LOBBY_STEP : STREET_STEP;
                walk_delay=overdue < step ? step-overdue : 0;
            } else walk_delay=0;
            for (unsigned j=0;j<encounter.count;j++) {
                struct Drop *drop=&encounter.loot[j];
                int px=(int)drop->x-camera_x(position.lane);
                if (drop->type && !encounter.enemies[j].reaction &&
                    (unsigned)(drop->z-position.depth)<=1 && px>20 && px<180 &&
                    take_item(drop->type,&inventory,&w)) {
                    drop->type=0;
                    changed=1;
                }
            }
            if (changed) draw_enemies(&atlas,&encounter,position.lane,position.depth);
        }
        unsigned art=((const uint32_t *)weapons)[weapon_frame(&w)];
        if (last_weapon_frame!=art) {
            draw_weapon(weapons+art);
            last_weapon_frame=art;
        }
        hud_number(20+13,inventory.health,&last_health);
        hud_number(20+41,inventory.armor,&last_armor);
        if (last_kind!=w.kind) {
            weapon_label(weapons+16*6*4+56*w.kind);
            last_kind=w.kind;
            last_ammo=255;
        }
        unsigned supply=ammo(&w,&inventory);
        if (w.kind==1) hud_number(20+149,supply,&last_ammo);
        else if (supply!=last_ammo) {
            unsigned tens=(supply*205u)>>11;
            digit(20+149,tens ? tens : 10);
            digit(20+157,w.kind ? supply-10*tens : 10);
            digit(20+165,10);
            last_ammo=supply;
        }
        if (last_key!=inventory.key) {
            digit(20+180,inventory.key);
            last_key=inventory.key;
        }
    }
}
