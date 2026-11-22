#include <stdint.h>

#define REG16(a) (*(volatile uint16_t *)(a))
enum { A = 1, B = 2, SELECT = 4, START = 8, R = 256, L = 512 };
enum { RIGHT = 16, LEFT = 32, UP = 64, DOWN = 128 };
extern const uint8_t assets[];
enum { SHOT = 4470, FLASH = 2194, RELOAD = 21395 };

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

struct ViewObjects {
    unsigned items;
    const uint32_t *occlusion;
};

static struct ViewObjects show_view(const struct Atlas *atlas, const uint8_t *room, unsigned lane, unsigned depth,
                      unsigned open_doors, uint32_t taken) {
    unsigned offset=view_entry(room,lane,depth)&0x7fffffff;
    if (open_doors && depth+1==room[1]) {
        for (int i=0; i<3; i++)
            if ((open_doors & (1u<<i)) && (room[4+4*i]&31)==lane)
                offset=((const uint32_t *)(room+20))[i];
    }
    const uint32_t *view=(const uint32_t *)(room+offset);
    unsigned map_words=17*32/2;
    const uint32_t *tiles=view+1+map_words;
    for (unsigned i=0; i<*view; ) {
        unsigned start=i++;
        while (i<*view && tiles[i]==tiles[i-1]+1) i++;
        copy_words(atlas->backgrounds+32*tiles[start],0x06000000+32*start,8*(i-start));
    }
    copy_words(view+1,0x0600f800,map_words);
    const uint32_t *objects=tiles+*view;
    unsigned count=*objects++;
    volatile uint16_t *oam=(volatile uint16_t *)0x07000200;
    unsigned slot=0;
    for (unsigned i=0; i<count; i++,objects+=2) {
        const uint8_t *sprite=(const uint8_t *)objects;
        if (taken & (1u<<sprite[0])) continue;
        unsigned tile=384+16*slot++;
        copy_words(atlas->sprites+512*objects[1],0x06010000+32*tile,128);
        oam[0]=sprite[2];
        oam[1]=0x8000|sprite[1];
        oam[2]=tile;
        oam+=4;
    }
    struct ViewObjects result={slot,objects};
    for (; slot<40; slot++,oam+=4) oam[0]=0x200;
    return result;
}

struct Enemy {
    unsigned short x;
    uint8_t z,type,health,action,phase;
    unsigned reaction,attack_time,walk_time;
};

struct Encounter {
    struct Enemy enemies[18];
    unsigned count;
    struct ViewObjects view;
};

static void enter_encounter(struct Encounter *enc,const uint8_t *room,const uint8_t *damage) {
    static const uint8_t health[]={9,7,12,15};
    const uint32_t *list=actor_list(room);
    enc->count=*list++;
    for (unsigned i=0;i<enc->count;i++) {
        struct Enemy *e=&enc->enemies[i];
        unsigned spawn=list[i];
        e->x=spawn;
        e->z=spawn>>16;
        e->type=spawn>>24;
        e->health=health[e->type]-damage[i];
        e->action=8;
        e->phase=0;
        e->reaction=0;
        e->attack_time=0;
        e->walk_time=0;
    }
}

static int camera_x(unsigned lane) { return lane==1 ? 0 : (int)lane*50-72; }

// Clear covered OBJ pixels in tile order. OBJ VRAM accepts halfword writes.
static void clip_piece(const uint32_t *masks,unsigned distance,int x,int y,int flip,unsigned tile) {
    if (distance==1) return;
    unsigned count=*masks++;
    for (unsigned i=0;i<count;i++,masks+=3) {
        const uint8_t *mask=(const uint8_t *)masks;
        if (mask[2]>=distance) break; /* converter emits scenery nearest first */
        int dx=mask[0]-x,dy=mask[1]-y;
        if (dx<=-8 || dx>=32 || dy<=-8 || dy>=32) continue;
        volatile uint16_t *vram=(volatile uint16_t *)(0x06010000+32*tile);
        for (int row=0;row<8;row++) {
            unsigned bits=mask[4+row];
            int py=dy+row;
            if (!bits || (unsigned)py>=32) continue;
            for (int col=0;col<8;col++) {
                int px=dx+col;
                if (!(bits&(1u<<col)) || (unsigned)px>=32) continue;
                if (flip) px=31-px;
                unsigned cell=16*((py/8)*4+px/8)+2*(py%8)+(px%8)/4;
                vram[cell]&=~(15u<<(4*(px%4)));
            }
        }
    }
}

static int fire_at(struct Encounter *enc,unsigned lane,unsigned depth,unsigned pose,unsigned weapon,uint8_t *damage) {
    static const uint8_t power[]={2,4,10,16};
    for (unsigned distance=1;distance<=(weapon ? 4u : 1u);distance++)
        for (unsigned i=0;i<enc->count;i++) {
            struct Enemy *e=&enc->enemies[i];
            int x=(int)e->x-camera_x(lane);
            if (!e->health || e->action==1 || e->z!=depth+distance || x<0 || x>=200) continue;
            if (pose==1 && (x<50 || x>150)) continue;
            if ((pose==2 && x>75) || (pose==4 && x<125)) continue;
            unsigned hit=e->health>power[weapon] ? power[weapon] : e->health;
            damage[i]+=hit;
            e->health-=hit;
            e->action=1;
            e->phase=1;
            e->reaction=SHOT;
            e->attack_time=0;
            e->walk_time=0;
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
        unsigned phase=e->reaction>SHOT/2 ? 1 : 0;
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
    volatile uint16_t *oam=(volatile uint16_t *)0x070000c0;
    unsigned slot=enc->view.items;
    for (unsigned distance=1;distance<=4;distance++)
        for (unsigned j=enc->count;j>0;j--) {
            const struct Enemy *e=&enc->enemies[j-1];
            int x=(int)e->x-camera_x(lane);
            if ((!e->health && !e->reaction) || e->z!=depth+distance || x<0 || x>=200) continue;
            unsigned id=(((e->type*11+e->action)*4+distance-1)*4)+e->phase;
            const uint8_t *frame=atlas->actors+((const uint32_t *)atlas->actors)[id];
            int width=frame[0],height=frame[1],top=ground[distance]-frame[3];
            int flip=e->action>=5 && e->action<=8;
            int left=x-(flip ? width-frame[2] : frame[2]);
            const uint32_t *pieces=(const uint32_t *)(frame+4);
            for (int y=0;y<height;y+=32)
                for (int u=0;u<width;u+=32) {
                    unsigned image=*pieces++;
                    int px=left+(flip ? width-u-32 : u), py=top+y;
                    if (px>=200 || px+32<=0 || py>=136 || py+32<=0 || slot>=40) continue;
                    unsigned tile=384+16*slot++;
                    copy_words(atlas->sprites+512*image,0x06010000+32*tile,128);
                    clip_piece(enc->view.occlusion,distance,px,py,flip,tile);
                    oam[0]=py&255;
                    oam[1]=0x8000|(flip ? 0x1000 : 0)|((px+20)&511);
                    oam[2]=tile;
                    oam+=4;
                }
        }
    for (;oam<(volatile uint16_t *)0x07000200;oam+=4) oam[0]=0x200;
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

enum { CLOSE_ATTACK_PERIOD=27600 };

static int advance_corridor(struct Encounter *enc,const uint8_t *room,unsigned depth,unsigned dt) {
    if (room[0]!=1) return 0;
    /* The first west corridor enters action 3, walks from depth 3 to 2
       after 49 LCD periods, then strikes 21 periods after the step. */
    enum { STEP=13440, STRIKE_LEAD=5760 };
    int changed=0;
    for (unsigned i=0;i<enc->count;i++) {
        struct Enemy *e=&enc->enemies[i];
        if (!e->health || e->reaction || e->z<=depth+1) continue;
        if (!corridor_step_clear(enc,e,room)) continue;
        e->walk_time+=dt;
        unsigned cycle=e->walk_time;
        if (cycle>=STEP) cycle-=STEP;
        if (cycle>=6720) cycle-=6720;
        unsigned phase=3-(cycle>=1680)-(cycle>=3360)-(cycle>=5040);
        if (e->action!=3 || e->phase!=phase) changed=1;
        e->action=3;
        e->phase=phase;
        if (e->walk_time>=STEP) {
            e->walk_time-=STEP;
            e->z--;
            if (e->z==depth+1) e->attack_time=CLOSE_ATTACK_PERIOD-STRIKE_LEAD;
            changed=1;
        }
    }
    return changed;
}

/* At one cell of depth an enemy in the player's view can strike. The
   original trace repeats a solitary close strike every 100-101 LCD periods. */
static int close_attacks(struct Encounter *enc,struct Inventory *inv,unsigned lane,unsigned depth,unsigned dt) {
    static const uint8_t type_damage[]={2,1,4,5};
    int changed=0;
    for (unsigned i=0;i<enc->count;i++) {
        struct Enemy *e=&enc->enemies[i];
        int x=(int)e->x-camera_x(lane);
        if (!e->health || e->reaction || e->z!=depth+1 || (unsigned)x>=200) {
            e->attack_time=0;
            if (e->action==2) { e->action=8; e->phase=0; changed=1; }
            continue;
        }
        e->attack_time+=dt;
        if (e->attack_time>=CLOSE_ATTACK_PERIOD) {
            e->attack_time-=CLOSE_ATTACK_PERIOD;
            unsigned hit=8+type_damage[e->type];
            unsigned absorb=hit/2<inv->armor ? hit/2 : inv->armor;
            inv->armor-=absorb;
            hit-=absorb;
            inv->health=inv->health>hit ? inv->health-hit : 0;
            e->action=2;
            e->phase=1;
            changed=1;
        } else if (e->action==2) {
            unsigned phase=e->attack_time<13720 || e->attack_time>=21120;
            if (e->phase!=phase) { e->phase=phase; changed=1; }
        }
    }
    return changed;
}

static unsigned capped(unsigned value,unsigned limit) {
    return value<limit ? value : limit;
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
        switch (item[3]) {
            case 1: if (inv->key) continue; inv->key=1; break;
            case 2: if (inv->armor==100) continue; inv->armor=100; break;
            case 3: if (inv->health==100) continue; inv->health=capped(inv->health+30,100); break;
            case 4: case 5:
                if (!(item[3]==4 && !(inv->weapons&4)) && inv->rockets==50) continue;
                if (item[3]==4) inv->weapons|=4;
                inv->rockets=capped(inv->rockets+5,50);
                break;
            case 6: case 9:
                if (!(item[3]==9 && !(inv->weapons&2)) && inv->shells==50) continue;
                if (item[3]==9) inv->weapons|=2;
                inv->shells=capped(inv->shells+6,50);
                break;
            case 7: case 8:
                if (!(item[3]==8 && !(inv->weapons&1)) && gun->remaining==200) continue;
                if (item[3]==8) inv->weapons|=1;
                gun->remaining=capped(gun->remaining+12,200);
                break;
            default: continue;
        }
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
    if (!(keys & (A|L|R))) return 0;
    if (w->kind && !ammo(w,inv)) { cycle_weapon(w,inv); return 0; }
    w->pose = keys & A ? 1 : keys & L ? 2 : 4;
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
    switch (w->kind) {
        case 0: return t<4950 ? hand+2 : t<9900 ? hand+3 : idle;
        case 1:
            if (t<FLASH) return hand+2;
            if (direction && t<SHOT) return hand+3;
            if (direction) t-=FLASH;
            if (!w->magazine && w->remaining && t<RELOAD)
                return idle+18+(t<7300 ? 1 : t<12250 ? 2 : t<17200 ? 3 : 4);
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
    volatile uint16_t *oam=(volatile uint16_t *)0x07000000;
    const uint8_t *pieces=frame+4;
    for (unsigned y=0;y<height;y+=32)
        for (unsigned x=0;x<width;x+=32) {
            copy_words(pieces,0x06010000+tile*32,128);
            oam[0]=(top+y)&255;
            oam[1]=0x8000|((left+x)&511);
            oam[2]=tile;
            pieces+=512; tile+=16; oam+=4;
        }
    for (;oam<(volatile uint16_t *)0x070000c0;oam+=4) oam[0]=0x200;
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
        for (unsigned j=0;j<18;j++) row[j]=0;
    }
}

void play(void) {
    struct Weapon w = {48, 6, 0, FLASH*2, 0, 1};
    const uint8_t *campaign=assets + 32 + 2*19200;
    const uint32_t *levels=(const uint32_t *)campaign;
    const uint8_t *weapons=campaign+levels[6+levels[0]];
    unsigned last_weapon_frame=~0u,last_kind=~0u;
    struct Atlas atlas={campaign+levels[2+levels[0]],campaign+levels[3+levels[0]],campaign+levels[4+levels[0]]};
    unsigned level=0;
    unsigned frags=0,total_frags=0;
    unsigned next_level=1;
    int exit_active=0;
    unsigned exit_time=0, exit_screen=0;
    unsigned death_time=0;
    const uint8_t *rooms=campaign+levels[1];
    unsigned room_count=*(const uint32_t *)rooms;
    uint32_t taken[256];
    uint8_t unlocked[256];
    uint8_t damage[256][18];
    reset_rooms(room_count,taken,unlocked,damage);
    struct Inventory inventory={0,0,100,0,0,1};
    unsigned room_id=0;
    const uint32_t *directory=(const uint32_t *)rooms+1;
    const uint8_t *room=rooms+directory[0];
    unsigned open_doors=0;
    unsigned lane=room[2], depth=0, walk_delay=0;
    struct Encounter encounter;
    enter_encounter(&encounter,room,damage[0]);
    encounter.view=show_view(&atlas,room,lane,depth,0,taken[0]);
    REG16(0x04000040)=(20<<8)|220;
    REG16(0x04000044)=136;
    REG16(0x04000048)=0x11;
    REG16(0x0400004a)=1;
    REG16(0x04000000)=0x3140;
    draw_enemies(&atlas,&encounter,lane,depth);
    REG16(0x0400010a) = 0x83;
    unsigned previous = REG16(0x04000108);
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
        unsigned dt = (uint16_t)(now - previous);
        previous = now;
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
                if (death_time>=menu_at) {
                    REG16(0x04000000)=0x0100;
                    full_scene(campaign+levels[1+levels[0]]+19200*3*levels[0]);
                }
                continue;
            }
            if (!(pressed&A)) continue;
            retry=1;
        }
        if (exit_active || retry) {
            old_keys=keys;
            if (retry) {
                next_level=level;
                total_frags-=frags;
                inventory=(struct Inventory){0,0,100,0,0,1};
                w=(struct Weapon){48,6,0,FLASH*2,0,1};
                death_time=0;
            } else {
                exit_time+=dt;
                /* The reference trace reaches the score screen after 292 LCD
                   periods, the next level title after 445, and play after 565. */
                unsigned screen=exit_time>=122100 ? 3 : exit_time>=80100 ? 2 : exit_time>=19200 ? 1 : 0;
                if (screen!=exit_screen) {
                    REG16(0x04000000)=0x0100;
                    unsigned card_level=screen==3 ? next_level : level;
                    full_scene(campaign+levels[1+levels[0]]+19200*(3*card_level+screen-1));
                    if (screen==2) {
                        score_number(96,total_frags);
                        score_number(128,frags);
                    }
                    exit_screen=screen;
                }
                if (exit_time<155000) continue;
            }
            level=next_level;
            frags=0;
            rooms=campaign+levels[1+level];
            directory=(const uint32_t *)rooms+1;
            room_count=directory[-1];
            room_id=0;
            room=rooms+directory[0];
            reset_rooms(room_count,taken,unlocked,damage);
            lane=room[2];
            depth=0;
            open_doors=0;
            inventory.key=0;
            walk_delay=0;
            exit_active=0;
            entering=1;
            full_scene(assets+32+19200);
            enter_encounter(&encounter,room,damage[0]);
            encounter.view=show_view(&atlas,room,lane,depth,0,0);
            draw_enemies(&atlas,&encounter,lane,depth);
            last_health=last_armor=last_ammo=last_key=255;
            last_kind=~0u;
            REG16(0x04000000)=0x3140;
            continue;
        }
        if (pressed & START) { paused=!paused; pause_count=0; }
        else if (paused) pause_input(pause_sequence,&pause_count,pressed,&inventory,&w);
        old_keys = keys;
        if (!paused) {
            int changed=animate_hits(&encounter,dt);
            if (advance(&w,&inventory,dt,keys)) {
                int impact=fire_at(&encounter,lane,depth,w.pose,w.kind,damage[room_id]);
                if (impact==2) { frags++; total_frags++; }
                changed|=impact;
            }
            changed|=advance_corridor(&encounter,room,depth,dt);
            changed|=close_attacks(&encounter,&inventory,lane,depth,dt);
            if (!inventory.health) {
                if (changed) draw_enemies(&atlas,&encounter,lane,depth);
                hud_number(20+13,0,&last_health);
                hud_number(20+41,inventory.armor,&last_armor);
                continue;
            }
            unsigned direction=keys & (UP|DOWN|LEFT|RIGHT);
            int door=-1;
            if (depth+1 == room[1]) {
                for (int i=0; i<3; i++) {
                    unsigned spec=room[4+4*i];
                    if (spec!=255 && (spec&31)==lane) {
                        door=i;
                        if (pressed&B) {
                            unsigned bit=1u<<i;
                            if ((spec&128) && !(unlocked[room_id]&bit)) {
                                if (inventory.key) {
                                    inventory.key=0;
                                    unlocked[room_id]|=bit;
                                }
                            } else {
                                open_doors ^= bit;
                                encounter.view=show_view(&atlas,room,lane,depth,open_doors,taken[room_id]);
                                changed=1;
                                if (room[5+4*i]==254 && (open_doors&bit)) {
                                    next_level=level+1;
                                    unsigned bonus=next_level==4 ? 4 : next_level==9 ? 2 : next_level==14 ? 1 : 0;
                                    if (bonus && !(levels[5+levels[0]]&bonus)) next_level++;
                                    if (next_level==levels[0]) next_level=0;
                                    exit_active=1;
                                    exit_time=0;
                                    exit_screen=0;
                                }
                            }
                        }
                        break;
                    }
                }
            }
            if (door<0 && (pressed&B) && !w.delay) cycle_weapon(&w,&inventory);
            if (walk_delay > dt) walk_delay-=dt;
            else if (direction) {
                unsigned x=lane, z=depth;
                const uint8_t *old_room=room;
                switch (direction) {
                    case UP:
                        if (z+1<room[1]) z++;
                        else if (room[0]==1 && room[1]>1 && (room[3]&0x30) && room[5]<room_count) {
                            room_id=room[5];
                            room=rooms+directory[room_id];
                            x=1;
                            z=0;
                        } else if (door>=0 && (!!(open_doors & (1u<<door)) != !(room[4+4*door]&0x60))) {
                            const uint8_t *exit=room+4+4*door;
                            if (exit[1]<room_count) {
                                room_id=exit[1];
                                room=rooms+directory[room_id];
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
                                room_id=back;
                                room=rooms+directory[room_id];
                                x=1;
                                z=room[1]-1;
                            }
                        }
                        break;
                    case LEFT: case RIGHT:
                        if (room[3]==0x30 && room[1]==1) {
                            const uint8_t *branch=room+(direction==LEFT ? 8 : 4);
                            if (branch[1]<room_count) {
                                room_id=branch[1];
                                room=rooms+directory[room_id];
                                x=branch[2];
                                z=0;
                            }
                        } else if (direction==LEFT && x>1) x--;
                        else if (direction==RIGHT && x<room[0]) x++;
                        break;
                }
                if (old_room==room && (view_entry(room,x,z)&0x80000000u)) {
                    x=lane;
                    z=depth;
                }
                if (old_room!=room) {
                    open_doors=0;
                    enter_encounter(&encounter,room,damage[room_id]);
                }
                if (lane!=x || depth!=z || old_room!=room) {
                    taken[room_id]=collect(room,x,z,taken[room_id],&inventory,&w);
                    encounter.view=show_view(&atlas,room,x,z,open_doors,taken[room_id]);
                    changed=1;
                    lane=x;
                    depth=z;
                }
                unsigned overdue=walk_delay ? dt-walk_delay : 0;
                unsigned step=room_id==1 ? LOBBY_STEP : STREET_STEP;
                walk_delay=overdue < step ? step-overdue : 0;
            } else walk_delay=0;
            if (changed) draw_enemies(&atlas,&encounter,lane,depth);
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
