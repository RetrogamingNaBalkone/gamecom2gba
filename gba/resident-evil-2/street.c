#include "street.h"
#include "video.h"
#include "audio.h"
#include "inventory.h"
#include "progress.h"
#include "campaign.h"
#include "special.h"

#include "actor.h"

static struct Re2Actor player, foes[3];
static const uint8_t *data;
static unsigned camera, elapsed, fire_pressed, tile_slots, last_clock;
static unsigned enemy_delay[3];
static unsigned armed, grabbed, player_delay, struck;
static unsigned random_calls;
static unsigned encounter_mask,encounter_byte;
/* Bank 22:62B3 retains two previous rooms. Type 13 has a distinct
 * re-entry animation in 22:61A8. A missing boss stays missing. */
struct BossRoom {
    unsigned key,health;
};
static struct BossRoom previous_boss_rooms[2];
static unsigned current_boss_room;
static unsigned paint_id;
static const uint8_t *cache[48];
static unsigned last_used[48];
static const int lane_y[] = {155,123,97};
static struct {
    int x,sign;
    unsigned distance,error,span,remaining;
} walk_display;

static void snap_walk(void)
{
    walk_display.x=player.x;
    walk_display.remaining=0;
}

static void blend_walk(unsigned periods)
{
    int dx=player.x-walk_display.x;
    walk_display.sign=dx<0?-1:1;
    walk_display.distance=dx<0?-dx:dx;
    walk_display.span=walk_display.remaining=periods;
    walk_display.error=periods/2;
}

static int advance_walk_display(void)
{
    if (!walk_display.remaining) return 0;
    --walk_display.remaining;
    walk_display.error+=walk_display.distance;
    int before=walk_display.x;
    while (walk_display.error>=walk_display.span) {
        walk_display.error-=walk_display.span;
        walk_display.x+=walk_display.sign;
    }
    return before!=walk_display.x;
}

static uint32_t word(const void *address)
{
    return *(const uint32_t *)address;
}

static const uint8_t *sequence(const struct Re2Actor *actor)
{
    unsigned index=((actor->kind-1)*168+actor->action)*3+actor->depth;
    return data + word(data+word(data)+4*index);
}

static const uint8_t *room_entry(void)
{
    unsigned area=re2_progress[0x208];
    if (area>=7) return 0;
    const uint8_t *directory=data+word(data+4+4*area);
    if (camera>=word(directory)) return 0;
    const uint8_t *planes=directory+4+12*camera;
    return word(planes)?planes:0;
}

static void load_room(void)
{
    const uint8_t *planes=room_entry();
    if (!planes) return;
    const uint8_t *atlas=data+word(data+32);
    re2_video_plane(atlas,data+word(planes),0);
    if (word(planes+4)) {
        re2_video_plane(atlas,data+word(planes+4),1);
    }
}

static void spawn(void)
{
    struck=0;
    re2_progress[0xc3]&=0x1f;
    snap_walk();
    unsigned area=re2_progress[0x208];
    unsigned key=area==6 ? camera+1 : 0;
    unsigned saved_hp=0,from_cache=0;
    if (key && current_boss_room) {
        unsigned found=previous_boss_rooms[0].key==key?0:1;
        if (previous_boss_rooms[found].key==key) {
            from_cache=1;
            saved_hp=previous_boss_rooms[found].health;
        }
        if (previous_boss_rooms[0].key!=key) previous_boss_rooms[1]=previous_boss_rooms[0];
        unsigned health=0;
        for (unsigned i=0;i<3;++i) if (foes[i].kind==13) health=foes[i].health;
        previous_boss_rooms[0]=(struct BossRoom){current_boss_room,health};
    } else {
        previous_boss_rooms[0].key=previous_boss_rooms[1].key=0;
    }
    current_boss_room=key;
    const uint8_t *room=room_entry();
    const uint8_t *list=room?data+word(room+8):0;
    encounter_mask=0;
    if (list && list[0]!=255) {
        unsigned area=re2_progress[0x208],ordinal=0;
        /* 22:60C4 counts only nonempty encounter records. Area 5 is
         * the continuation of area 1's 76-camera catalogue. */
        for (unsigned pass=0;pass<(area==5?2u:1u);++pass) {
            unsigned previous=area==5 && !pass;
            const uint8_t *dir=data+word(data+4+4*(previous?1:area));
            unsigned limit=previous?word(dir):camera;
            for (unsigned c=0;c<limit;++c) {
                unsigned pos=word(dir+12+12*c);
                if (pos && data[pos]!=255) ++ordinal;
            }
        }
        if (ordinal<48) {
            encounter_mask=1u<<(ordinal&7);
            encounter_byte=0x253+ordinal/8;
            if (!(re2_progress[encounter_byte]&encounter_mask)) list=0;
        }
    }
    for (unsigned i=0;i<3;++i) {
        enemy_delay[i]=0;
        struct Re2Actor *e=&foes[i];
        if (!list || list[0]==255) {
            e->kind=e->health=0;
            continue;
        }
        unsigned kind=list[0],dir=list[1]>>4,lane=list[1]&15;
        /* Bank 36 lists supply the two packed position fields and HP;
         * the ordinary zombie idle sequence depends on starting facing. */
        static const unsigned idle[]={0x10,0x12,0x11,0x1a};
        *e=(struct Re2Actor){kind,dir,lane,(kind<=3?idle[dir/2]:0),0,
                          list[2],lane_y[lane],list[3],0,0};
        /* 22:642A selects a side and initial pose for the larger actors
         * from Leon's entry position instead of the nominal table fields. */
        if (kind>=7) {
            int left=player.x>=100;
            e->facing=left?2:6;
            switch (kind) {
                case 7: e->depth=player.depth; e->x=left?10:190; e->action=left?6:7; break;
                case 8: e->depth=1; e->x=left?30:170; e->action=left?0:1; break;
                case 9: e->depth=player.depth; e->x=left?20:180; e->action=left?1:0; break;
                case 10: e->depth=1; e->facing=2; e->x=5; e->action=0; break;
                case 12: e->depth=1; e->x=left?5:195; e->action=left?0:1; break;
                case 13: e->depth=1; e->facing=6; e->x=220; e->action=2; break;
            }
            if ((kind==10 && (re2_progress[0xc2]&128)) ||
                (kind==13 && ((re2_progress[0xc2]&64) ||
                 (re2_progress[0x20a]==0x19 && !(re2_progress[0xc2]&32)))))
                e->kind=0;
            if (kind==13 && re2_progress[0x20a]==0x19) { e->x=160; e->action=6; }
            if (kind==13 && from_cache) e->kind=0;
            e->y=lane_y[e->depth];
        }
        list+=4;
    }
    if (saved_hp && !(re2_progress[0xc2]&64)) {
        unsigned trapped=re2_progress[0xc2]&32;
        foes[0]=(struct Re2Actor){13,6,1,trapped?6:1,0,trapped?160:180,123,saved_hp,0,0};
    }
}

static void follow_cached_boss(void)
{
    struct BossRoom *previous=&previous_boss_rooms[0];
    if (!previous->health || !current_boss_room || (re2_progress[0xc2]&64)) return;
    const uint8_t *room=room_entry();
    if (!room) return;
    const uint8_t *entry=data+word(room+8);
    while (*entry!=255) entry+=4;
    for (++entry;*entry!=255;entry+=3) {
        if (entry[0]+1!=previous->key) continue;
        /* The lane/occupancy check at 20:4DBD uses the active camera byte
         * as its obstruction X in this area. 20:4D79 caps Leon at X=100. */
        if (player.x>100 || (!player.depth && player.x>(int)camera-18 &&
                                            player.x<(int)camera+18)) return;
        unsigned slot=0;
        while (slot<3 && foes[slot].kind) ++slot;
        if (slot==3 || re2_actor_random(120)!=100) return;
        unsigned lane=entry[1]&15;
        foes[slot]=(struct Re2Actor){13,entry[1]>>4,lane,2,0,entry[2],lane_y[lane],previous->health,0,0};
        previous->health=0;
        enemy_delay[slot]=0;
        return;
    }
}

static int cached_chunk(const uint8_t *image)
{
    unsigned oldest=0;
    for (unsigned i=0;i<48;++i) {
        if (cache[i]==image) {
            last_used[i]=paint_id;
            return (int)i;
        }
        if (last_used[i]<last_used[oldest]) oldest=i;
    }
    if (last_used[oldest]==paint_id || !re2_video_obj_tiles(16*oldest,image,16))
        return -1;
    cache[oldest]=image;
    last_used[oldest]=paint_id;
    return (int)oldest;
}

/* Each source body part is a set of native hardware objects. Mirrored
 * frame descriptors give separate body and part origins on the cartridge. */
static void draw_actor(const struct Re2Actor *a)
{
    if (!a->kind) return;
    const uint8_t *s=sequence(a);
    if (s==data || !word(s)) return;
    const uint8_t *frame=s+4+8*a->frame;
    const uint8_t *pose=data+word(frame+4);
    unsigned flip=frame[1]&64;
    int x=a==&player?walk_display.x:a->x;
    int left=x-(flip?pose[2]:pose[0]), top=a->y-pose[1];
    for (int i=(int)pose[3]-1;i>=0;--i) {
        const uint8_t *part=pose+4+8*i, *g=data+word(part+4);
        int w=*(const uint16_t *)g,h=*(const uint16_t *)(g+2);
        int mirror=flip && part[2]!=255;
        int x=left+(mirror?part[2]:part[0]), y=top+part[1];
        const uint8_t *image=g+4;
        for (int v=0;v<h;v+=32) for (int u=0;u<w;u+=32,image+=512) {
            int dx=x+(mirror?w-u-32:u),dy=y+v;
            if (dx<=-32 || dy<=-32 || dx>=200 || dy>=160 || tile_slots>=128) continue;
            int location=cached_chunk(image);
            if (location>=0) re2_video_sprite(tile_slots++,dx,dy,16*location,0,2,mirror);
        }
    }
}

static void paint(void)
{
    for (unsigned i=24;i<tile_slots;++i) re2_video_hide(i);
    tile_slots=24;
    ++paint_id;
    unsigned prop_count=0;
    int prop_x[4],prop_y[4];
    for (unsigned i=0;i<4;++i) {
        const uint8_t *image=re2_world_prop(i,&prop_x[i],&prop_y[i]);
        if (!image) image=re2_switch_picture(i,&prop_x[i],&prop_y[i]);
        if (!image) break;
        unsigned first=i*4;
        unsigned resident=0;
        while (resident<4 && cache[first+resident]==image+512*resident) ++resident;
        if (resident!=4) {
            if (!re2_video_obj_tiles(16*first,image,64)) break;
            for (unsigned n=0;n<4;++n) cache[first+n]=image+n*512;
        }
        for (unsigned n=0;n<4;++n) last_used[first+n]=paint_id;
        ++prop_count;
    }
    /* Enemies appear in front of Leon at equal depth, following bank 20's
     * actor order. OAM prioritizes lower-numbered objects. */
    const struct Re2Actor *order[4]={&foes[0],&foes[1],&foes[2],&player};
    for (unsigned i=1;i<4;++i) {
        const struct Re2Actor *actor=order[i];
        unsigned j=i;
        while (j && actor->y>order[j-1]->y) {order[j]=order[j-1];--j;}
        order[j]=actor;
    }
    for (unsigned i=0;i<4;++i) draw_actor(order[i]);
    for (unsigned i=0;i<2;++i) {
        int x,y;
        const uint8_t *item=re2_ground_item(i,&x,&y);
        if (!item || tile_slots>=128) continue;
        int location=cached_chunk(item);
        if (location>=0) re2_video_sprite(tile_slots++,x,y,16*location,0,2,0);
    }
    for (unsigned i=0;i<prop_count && tile_slots<128;++i)
        re2_video_sprite(tile_slots++,prop_x[i],prop_y[i],64*i,0,3,0);
}

void re2_actor_action(struct Re2Actor *a,unsigned action)
{
    if (a->action!=action) {
        a->action=action;
        a->frame=0;
    }
}

static void store_player(void)
{
    re2_progress[0x233]=(player.facing<<4)|player.depth;
    re2_progress[0x234]=player.x;
    re2_progress[0x235]=player.y;
    re2_progress[0x236]=player.action;
    re2_progress[0x237]=player.frame;
    re2_progress[0x238]=player.health;
    for (unsigned i=0;i<3;++i) {
        const struct Re2Actor *e=&foes[i];
        uint8_t *p=re2_progress+0x217+i*9;
        p[0]=e->kind; p[1]=(e->facing<<4)|e->depth; p[2]=e->x; p[3]=e->y;
        p[4]=e->action; p[5]=e->frame; p[6]=e->health;
    }
}

static void load_player(void)
{
    camera=re2_progress[0x209];
    player.facing=re2_progress[0x233]>>4;
    player.depth=re2_progress[0x233]&15;
    player.x=re2_progress[0x234];
    player.y=re2_progress[0x235];
    player.health=re2_progress[0x238];
    player.action=player.facing;
    player.frame=0;
}

static int move(unsigned direction,int dx,int dy)
{
    store_player();
    int result=re2_campaign_move(direction,dx,dy);
    if (result==0) {
        player.x+=dx;
        player.y+=dy;
    } else if (result==2) {
        load_player();
        spawn();
        load_room();
    }
    return result;
}

int re2_actor_advance(struct Re2Actor *a)
{
    const uint8_t *s=sequence(a);
    if (s==data || !word(s)) return 0;
    if (++a->frame>=word(s)) a->frame=0;
    return (int8_t)s[4+8*a->frame];
}

int re2_actor_delta(const struct Re2Actor *a)
{
    const uint8_t *s=sequence(a);
    return s==data || !word(s)?0:(int8_t)s[4+8*a->frame];
}

static unsigned clock_now(void)
{
    return *(volatile uint16_t *)0x04000108;
}

unsigned re2_actor_random(unsigned limit)
{
    /* 21:6301 adds and subtracts the call sequence on alternating calls. */
    unsigned timer=(clock_now()>>8)&255;
    ++random_calls;
    unsigned value=(timer+((random_calls&1)?0-random_calls:random_calls))&255;
    while (value>=limit) value-=limit;
    return value;
}

static void actor_audio(void)
{
    int zombies=0;
    for (unsigned i=0;i<3;++i) {
        struct Re2Actor *e=&foes[i];
        if (!e->kind) continue;
        const uint8_t *s=sequence(e);
        if (s==data || !word(s)) continue;
        unsigned cue=s[4+8*e->frame+(e->health?2:3)];
        if (cue) re2_sound(cue);
        if (e->kind<=3) zombies=1;
    }
    if (zombies && re2_actor_random(100)==84) re2_sound(10);
}

/* The action delay in 23:6E90 is counted in original scene updates. */
static unsigned player_hold(void)
{
    unsigned action=player.action, units=4;
    if ((action>=0x30 && action<0x50) || (action>=0x58 && action<=0x5f) ||
        (action>=0x72 && action<=0x75)) units=5;
    if (action>=0x50 && action<=0x57) units=6;
    if (action==0x68 || action==0x69) units=3;
    if ((action==0x60 || action==0x61) && player.health<=60) units=2;
    return units;
}

/* 23:6C8A tests the angular sector in screen coordinates. Firearms have a
 * near limit for horizontal shots; the knife uses a maximum reach. */
static int hit_distance(const struct Re2Actor *enemy)
{
    unsigned dir=player.facing,weapon_id=re2_inventory_state.weapon;
    int dx=enemy->x-player.x;
    if (dir==2 || dir==6) {
        if (enemy->depth!=player.depth) return -1;
        if (dir==6) dx=-dx;
        const uint8_t guns[]={35,22,15},knife[]={75,55,45};
        if (dx<0 || (weapon_id==1?dx>knife[player.depth]:dx<guns[player.depth])) return -1;
        return dx;
    }
    int lanes=(int)enemy->depth-(int)player.depth;
    if (dir>=3 && dir<=5) lanes=-lanes;
    if (lanes<=0 || (weapon_id==1 && lanes==2)) return -1;
    int radius=(weapon_id==2?20:10)+(lanes==2?5:0);
    if ((dir==0 || dir==1 || dir==7) && enemy->kind>3) radius+=5;
    if (dir==0 || dir==4)
        return dx>=-radius && dx<=radius?lanes:-1;
    if (dir==7 || dir==5) dx=-dx;
    return dx>=2*radius && dx<=6*radius?lanes:-1;
}

static void wound(struct Re2Actor *target)
{
    unsigned knife=re2_inventory_state.weapon==1;
    if (target->kind==12 || target->kind==13) {
        re2_final_hit(target,&player);
        return;
    }
    unsigned damage=knife?3:re2_inventory_state.weapon==2?20:6;
    if (target->kind>=7) {
        unsigned left=target->facing==6,action;
        if (target->kind==10) damage>>=1;
        /* 22:6E1E kills the diving attacker during its crossing. */
        if (target->kind==7) {
            if (target->action>=2 || !target->frame) return;
            damage=target->health;
        }
        target->health=target->health>damage?target->health-damage:0;
        switch (target->kind) {
            case 7: action=(target->health?0:4)+left; break;
            case 8: action=(target->health?14:10)+left; break;
            case 9: action=(target->health?15:7)-left; break;
            default: action=(target->health?30:2)+left; break;
        }
        re2_actor_action(target,action);
        target->frame=0;
        return;
    }
    /* 22:68BC makes the next hit lethal at ten HP or below. */
    target->health=target->health>10 && target->health>damage?target->health-damage:0;
    static const uint8_t recoil[4][3]={
        {0x48,0x48,0x42},{0x17,0x17,0x3f},{0x47,0x47,0x41},{0x1f,0x1f,0x40}
    };
    target->facing=target->depth<player.depth?0:target->depth>player.depth?4:
                   target->facing<4?2:6;
    unsigned weapon=knife?2:re2_inventory_state.weapon==2?1:0;
    re2_actor_action(target,recoil[target->facing/2][weapon]);
    target->frame=0;
    if (!target->health || (target->health<=10 && re2_actor_random(3)==2))
        re2_progress[0xc3]|=0x20u<<(target-foes);
    target->stagger=3;
}

static void hit_enemy(void)
{
    struct Re2Actor *target=0;
    int nearest=256;
    for (unsigned i=0;i<3;++i) {
        struct Re2Actor *e=&foes[i];
        if (!e->kind || !e->health || (e->kind<=3 && (re2_progress[0xc3]&(0x20u<<i))) ||
            e->action==0xe || e->action==0xf ||
            (e->kind==10 && (e->action==0x1c || e->action==0x1d ||
             (e->action>=0x12 && e->action<=0x15)))) continue;
        int distance=hit_distance(e);
        if (distance<0) continue;
        if (re2_inventory_state.weapon==2) wound(e);
        else if (distance<nearest) {target=e;nearest=distance;}
    }
    if (target) wound(target);
}

static void weapon(void)
{
    unsigned dir=player.facing;
    unsigned weapon_id=re2_inventory_state.weapon;
    unsigned knife=weapon_id==1;
    unsigned ready=(knife?8:weapon_id==2?0x28:0x18)+dir;
    unsigned equip=(knife?0x38:weapon_id==2?0x40:0x30)+dir;
    unsigned fire=(knife?0x50:weapon_id==2?0x58:0x48)+dir;
    if (player.action==fire) {
        re2_actor_advance(&player);
        if (player.frame==2) {
            re2_sound(knife?6:weapon_id==2?4:5);
            hit_enemy();
        }
        if (weapon_id==2 && player.frame==4) re2_sound(3);
        if (!player.frame) {
            unsigned next=ready;
            if (!knife) {
                if (re2_progress[0xd6]&4) re2_inventory_state.magazine=1;
                else if (re2_inventory_state.magazine && !--re2_inventory_state.magazine &&
                         re2_inventory_reload(weapon_id==2?2:1)) {
                    re2_sound(3);
                    next=equip;
                }
                re2_inventory_store();
            }
            re2_actor_action(&player,next);
        }
        return;
    }
    if (player.action==equip) {
        re2_actor_advance(&player);
        if (!player.frame) re2_actor_action(&player,ready);
        return;
    }
    if (!armed) {
        re2_actor_action(&player,dir);
        return;
    }
    if (player.action!=ready) {
        re2_actor_action(&player,equip);
        re2_actor_advance(&player);
        return;
    }
    if (fire_pressed && !knife && !re2_inventory_state.magazine) {
        re2_sound(3);
        if (re2_progress[0xd6]&4) re2_inventory_state.magazine=1;
        re2_inventory_store();
        fire_pressed=0;
    } else if (fire_pressed) {
        re2_actor_action(&player,fire);
        re2_actor_advance(&player);
        fire_pressed=0;
    }
}

static void update_player(unsigned held)
{
    unsigned direction=player.facing;
    if (struck) {
        player.x+=re2_actor_advance(&player);
        if (player.action<0x80 && player.frame==3) {
            unsigned damage=struck==7?5:struck==10 || struck==12?20:15;
            player.health=player.health>damage?player.health-damage:0;
            if (!player.health) {
                struck=0;
                re2_actor_action(&player,0x80+direction);
                re2_actor_advance(&player);
            }
        } else if (!player.frame) {
            if (struck==13 && player.action<0x80) {
                re2_actor_action(&player,0x8a+(direction==6));
                player.x+=re2_actor_advance(&player);
            } else {
                struck=0;
                re2_actor_action(&player,direction);
            }
        }
        return;
    }
    if (player.action>=0x80) {
        if (player.action==0x89) {
            re2_progress[0xcc]=0x19;
            re2_progress[0x20b]|=2;
            return;
        }
        re2_actor_advance(&player);
        if (!player.frame) re2_actor_action(&player,0x89);
        return;
    }
    if (player.action==0x76 || player.action==0x77) {
        player.x+=re2_actor_advance(&player);
        if (!player.frame) re2_actor_action(&player,direction);
        return;
    }
    if (grabbed) {
        struct Re2Actor *attacker=&foes[grabbed-1];
        if (!attacker->health) {
            grabbed=0;
            re2_actor_action(&player,direction);
            return;
        }
        int shift=re2_actor_advance(&player);
        player.x+=shift;
        if (player.action>=0x72 && player.action<=0x75 && player.frame==3) {
            unsigned damage=attacker->kind<=3?10:
                attacker->kind==7 || attacker->kind==11?5:
                attacker->kind==10 || attacker->kind==12?20:15;
            player.health=player.health>damage?player.health-damage:0;
            if (!player.health) {
                re2_actor_action(&player,0x80+direction);
                re2_actor_advance(&player);
                grabbed=0;
            }
        } else if (!player.frame && player.action<0x72) {
            re2_actor_action(&player,direction==6?0x73:0x72);
            re2_actor_advance(&player);
        }
        return;
    }
    if (player.action==0x7e || player.action==0x7f) {
        int dx=re2_actor_advance(&player);
        store_player();
        if (re2_push_step(player.frame)) {
            unsigned previous_camera=camera;
            load_player();
            if (previous_camera!=camera) { spawn(); load_room(); }
        } else {
            player.x+=dx;
            if (!player.frame && !(held&1)) re2_actor_action(&player,direction);
        }
        return;
    }
    if ((armed && !(held&(16|32|64|128))) || (player.action>=0x30 && player.action<0x60)) {
        weapon();
        return;
    }
    if (held&(16|32)) {
        player.facing=(direction+((held&16)?1:7))&7;
        re2_actor_action(&player,player.facing);
        return;
    }
    if (!(held&(64|128))) {
        if (armed) weapon();
        else re2_actor_action(&player,direction);
        return;
    }
    if (direction&1) {
        /* Finish the turn to a cardinal heading before walking. */
        player.facing=direction==1 || direction==7 ? 0 : 4;
        re2_actor_action(&player,player.facing);
        return;
    }
    unsigned forward=(held&128)?(direction^4):direction;
    if (forward==0 || forward==4) {
        int next=(int)player.depth+(forward==0?1:-1);
        if (player.action!=0x6a && player.action!=0x6b) {
            if (next<0 || next>2) {
                int edge=forward==0?94:157;
                /* 23:6016 requests the step before the move replaces the
                 * active pose with the destination camera's standing pose. */
                if (move(forward,0,edge-player.y)==2 && !re2_audio_active()) re2_sound(13);
                return;
            }
            player.depth=(unsigned)next;
            re2_actor_action(&player,forward==0?0x6a:0x6b);
        }
        re2_actor_advance(&player);
        if (player.frame==3) {
            unsigned previous=forward==0?player.depth-1:player.depth+1;
            int x=re2_project_x(re2_progress[0x208],camera,player.x,previous,player.depth);
            int blocked=move(forward,x-player.x,lane_y[player.depth]-player.y);
            if (blocked==1) {
                player.depth=previous;
                re2_actor_action(&player,direction);
            }
        }
        if (!player.frame) re2_actor_action(&player,direction);
        return;
    }
    unsigned action=(held&2?0x68:0x60)+(forward==6);
    re2_actor_action(&player,action);
    if (!re2_audio_active()) re2_sound(13);
    int delta=re2_actor_advance(&player);
    move(forward,delta,0);
}

static void strike(const struct Re2Actor *enemy)
{
    if (struck || grabbed || !player.health) return;
    struck=enemy->kind;
    if (enemy->kind==13 || (player.facing==0 && player.depth==2) ||
        (player.facing==4 && player.depth==0)) player.facing=enemy->facing==2?6:2;
    else if (player.facing!=0 && player.facing!=4) player.facing=player.facing>=5?6:2;
    static const uint8_t recoil[]={0x75,0x72,0x74,0x73};
    re2_actor_action(&player,recoil[player.facing/2]);
    re2_actor_advance(&player);
    player_delay=player_hold();
}

static void catch_player(struct Re2Actor *enemy)
{
    grabbed=(unsigned)(enemy-foes)+1;
    player.facing=enemy->x<player.x?6:2;
    re2_actor_action(&player,player.facing==6?0x6f:0x6e);
    re2_actor_advance(&player);
    player_delay=player_hold();
}

static void update_large_enemy(struct Re2Actor *enemy)
{
    unsigned kind=enemy->kind,left=enemy->facing==6;
    int motion=re2_actor_advance(enemy);
    if (enemy->frame) enemy->x+=motion;
    if (!enemy->health) {
        if (enemy->frame) return;
        unsigned end=kind==7?8+left:kind==8?12+left:kind==9?13-left:4+left;
        if (kind==10) re2_progress[0xc2]|=128;
        re2_actor_action(enemy,end);
        return;
    }
    int distance=player.x-enemy->x;
    unsigned range=(unsigned)(distance<0?-distance:distance);
    unsigned aligned=enemy->depth==player.depth;
    unsigned action=enemy->action;
    static const uint8_t reach[]={50,45,40};
    int towards=left?distance<=0:distance>=0;
    if (kind==7 && aligned && range<=30 &&
        (action<=1 || action==6 || action==7)) strike(enemy);
    if (kind==9 && aligned && towards && action<=1 &&
        range>=20 && range<=reach[player.depth]) strike(enemy);
    if (!grabbed && !struck && player.health && kind==8 && aligned &&
        towards && range<=35 && action<=3) {
        re2_actor_action(enemy,4+left);
        player.x=enemy->x+(left?-40:40);
        catch_player(enemy);
        return;
    }
    if (grabbed==(unsigned)(enemy-foes)+1 && kind==8) return;
    if (kind==10 && !grabbed && !struck && player.health &&
        action<=1 && range<=90 && enemy->frame==2 && !aligned) {
        /* 22:7399 and the lane-indexed tongue table at 22:7573. */
        static const uint8_t tongue[]={27,25,23};
        re2_actor_action(enemy,tongue[player.depth]-left);
        return;
    }
    if (kind==10 && action>=22 && action<=27 && enemy->frame==3 && !grabbed)
        catch_player(enemy);
    if (kind==10 && grabbed==(unsigned)(enemy-foes)+1) {
        if (action<=1 && range<=55) { re2_actor_action(enemy,10+left); return; }
        if (!enemy->frame && action==10+left) { re2_actor_action(enemy,34+left); return; }
        if (action==34+left) return;
    }
    if (enemy->frame) return;
    if (enemy->x<=0 || enemy->x>=200) {
        left=!left;
        if ((left?player.x>90:player.x<110) && re2_actor_random(22)!=17) return;
        enemy->facing=left?6:2;
        enemy->x=left?180:20;
        enemy->depth=player.depth;
        enemy->y=lane_y[enemy->depth];
        re2_actor_action(enemy,(kind==7?6:kind==8?2:0)+(kind==9?!left:left));
        return;
    }
    switch (kind) {
        case 7: re2_actor_action(enemy,left); break;
        case 8:
            re2_actor_action(enemy,(!aligned && re2_actor_random(17)==15?0:2)+left);
            break;
        case 9:
            if (aligned && !towards && range>=reach[player.depth]) {
                left=!left;
                enemy->facing=left?6:2;
                re2_actor_action(enemy,4+!left);
            } else re2_actor_action(enemy,!left);
            break;
        case 10:
            left=distance<0;
            enemy->facing=left?6:2;
            re2_actor_action(enemy,(aligned && range<=40?6:0)+left);
            break;
    }
    enemy->x+=re2_actor_delta(enemy);
}

static void update_enemy(struct Re2Actor *enemy)
{
    if (!enemy->kind) return;
    if (enemy->kind==12 || enemy->kind==13) {
        re2_final_ai(enemy,&player);
        unsigned action=enemy->action;
        int reach=enemy->facing==6?enemy->x-player.x:player.x-enemy->x;
        if (enemy->kind==12 && action>=4 && action<=11 && enemy->frame>=2 &&
            reach>=25 && reach<=50) strike(enemy);
        if (enemy->kind==13 && (action==4 || action==5 || action==9) &&
            enemy->frame==2 && reach>=0 && reach<=(player.depth==1?85:80)) strike(enemy);
        return;
    }
    if (enemy->kind>=7) {
        update_large_enemy(enemy);
        return;
    }
    unsigned down_bit=0x20u<<(enemy-foes);
    if (re2_progress[0xc3]&down_bit) {
        /* 22:678E selects directional fall, prone and rise poses.
         * A live prone zombie rolls its recovery at the end of the pose. */
        static const uint8_t fall[]={0x45,0x18,0x43,0x20};
        static const uint8_t prone[]={0x46,0x19,0x44,0x21};
        static const uint8_t rise[]={0x3e,0x3b,0x3d,0x3c};
        enemy->x+=re2_actor_advance(enemy);
        if (enemy->frame) return;
        unsigned dir=enemy->facing/2;
        if (enemy->action==prone[dir]) {
            if (enemy->health && re2_actor_random(5)==3) {
                re2_progress[0xc3]&=(uint8_t)~down_bit;
                re2_actor_action(enemy,rise[dir]);
                enemy->stagger=word(sequence(enemy));
            }
        } else if (enemy->action==fall[dir]) re2_actor_action(enemy,prone[dir]);
        else {
            if (enemy->x<0x55 || enemy->x>0x73) enemy->facing=enemy->depth==0?0:4;
            re2_actor_action(enemy,fall[enemy->facing/2]);
        }
        return;
    }
    ++enemy->age;
    if (enemy->stagger) {
        enemy->x+=re2_actor_advance(enemy);
        --enemy->stagger;
        return;
    }
    int dx=player.x-enemy->x;
    int range=dx<0?-dx:dx;
    if (enemy->action==0x10 || enemy->action==0x11 || enemy->action==0x12 ||
        enemy->action==0x1a) {
        re2_actor_advance(enemy);
        if (!enemy->frame) {
            if ((enemy->facing==0 && player.depth>enemy->depth) ||
                (enemy->facing==4 && player.depth<enemy->depth)) {
                enemy->depth=enemy->facing==0?enemy->depth+1:enemy->depth-1;
                re2_actor_action(enemy,enemy->facing==0?0x0e:0x0f);
            } else {
                unsigned direction=dx>=0?2:6;
                unsigned next=enemy->facing==direction?(dx>=0?0x15:0):dx>=0?5:0;
                enemy->facing=direction;
                re2_actor_action(enemy,next);
            }
        }
        return;
    }
    if (grabbed==(unsigned)(enemy-foes)+1 || !player.health) {
        re2_actor_advance(enemy);
        if (!enemy->frame) re2_actor_action(enemy,dx>=0?0x22:0x23);
        return;
    }
    if (enemy->action==0x0e || enemy->action==0x0f) {
        re2_actor_advance(enemy);
        if (!enemy->frame) {
            enemy->y=lane_y[enemy->depth];
            enemy->facing=dx>=0?2:6;
            re2_actor_action(enemy,dx>=0?0x15:0);
        }
        return;
    }
    if (range>30 || enemy->depth!=player.depth) {
        enemy->facing=dx>0?2:6;
        enemy->x+=re2_actor_advance(enemy);
        if (!enemy->frame) {
            if (range<20 && player.depth!=enemy->depth) {
                int back=player.depth>enemy->depth;
                enemy->depth=back?enemy->depth+1:enemy->depth-1;
                enemy->facing=back?0:4;
                re2_actor_action(enemy,back?0x0e:0x0f);
                return;
            }
            unsigned next=dx>0?(player.x-enemy->x>50?0x15:0x16):0;
            if (enemy->action!=next) {
                re2_actor_action(enemy,next);
                enemy->x+=(int8_t)sequence(enemy)[4];
            }
        }
    } else {
        re2_actor_action(enemy,dx>=0?0x24:0x25);
        if (!grabbed && player.health) {
            grabbed=(unsigned)(enemy-foes)+1;
            player.facing=dx>=0?6:2;
            player.x=enemy->x+(dx>=0?14:-14);
            re2_actor_action(&player,dx>=0?0x6f:0x6e);
            re2_actor_advance(&player);
            player_delay=player_hold();
        }
    }
}

void re2_street_resume(void)
{
    last_clock=clock_now();
    snap_walk();
    player.health=re2_progress[0x238];
    if (re2_inventory_state.weapon>=3) armed=0;
    for (unsigned i=0;i<48;++i) cache[i]=0;
    load_room();
    paint();
}

void re2_street_enter(const uint8_t *assets)
{
    data=assets;
    current_boss_room=0;
    camera=re2_progress[0x209];
    elapsed=fire_pressed=armed=grabbed=0;
    player_delay=4;
    tile_slots=24;
    /* Initial records at 0232 and 0217 when the original enters area 0. */
    player=(struct Re2Actor){2,4,0,4,0,100,155,100,0,0};
    if (re2_progress[0x235]) load_player();
    spawn();
    store_player();
    re2_street_resume();
}

void re2_street_tick(unsigned held,unsigned pressed)
{
    if ((pressed&256) && grabbed && foes[grabbed-1].kind<=3 && player.health) {
        struct Re2Actor *e=&foes[grabbed-1];
        grabbed=0; armed=0;
        re2_actor_action(e,e->facing==2?0x26:0x27);
        e->stagger=word(sequence(e));
        re2_actor_action(&player,player.facing==6?0x77:0x76);
        player.x+=re2_actor_advance(&player);
    } else if ((pressed&256) && !grabbed && !struck && re2_inventory_state.weapon<=2) {
        armed^=1;
        fire_pressed=0;
        if (player.action<0x30) player_delay=1;
    }
    if ((pressed&1) && !armed && !grabbed && player.health) {
        store_player();
        re2_inventory_store();
        unsigned push=re2_push_start();
        if (push) {
            re2_actor_action(&player,push);
            paint();
            return;
        }
        if (re2_switch_press()) {
            paint();
            return;
        }
        if (re2_campaign_action()) {
            unsigned old_camera=camera;
            load_player();
            snap_walk();
            if (old_camera!=camera) spawn();
            load_room();
            paint();
            return;
        }
    }
    if (armed && (pressed&1) && player.action>=8 && player.action<0x30) {
        fire_pressed=1;
        player_delay=1;
    }
    if (!(held&1)) fire_pressed=0;
    unsigned now=clock_now();
    elapsed+=(uint16_t)(now-last_clock);
    last_clock=now;
    /* The original bank-21 scene loop ticks every 3.35 LCD periods,
     * slowing to about 4.45 while PCM interrupts run. */
    unsigned hold=re2_audio_active()?19531u:14746u;
    int moved=advance_walk_display();
    if (elapsed<hold) {
        if (moved) paint();
        return;
    }
    do {
        elapsed-=hold;
        follow_cached_boss();
        if (--player_delay==0) {
            int old_x=player.x;
            unsigned old_camera=camera;
            update_player(held);
            player_delay=player_hold();
            /* 23:7181 checks the active PCM flag before requesting a
             * footstep. Samples retain their original duration and pitch. */
            if (!re2_audio_active() && player.action>=0x60 && player.action<=0x6d &&
                (player.action<=0x63 || player.action>=0x68 || (player.frame&1)))
                re2_sound(13);
            if (player.action>=0x80 && player.action<=0x87 && player.frame==1)
                re2_sound(1);
            if (camera==old_camera && old_x!=player.x &&
                (player.action==0x60 || player.action==0x61 ||
                 player.action==0x68 || player.action==0x69)) {
                unsigned fast=hold<15000;
                unsigned periods=player_delay==2?(fast?7:9):
                    player_delay==3?(fast?10:13):(fast?13:18);
                blend_walk(periods);
            } else snap_walk();
        }
        /* 22:6C15 supplies each species' counter reload. */
        static const uint8_t delay[]={0,4,0,4,4,0,0,3,3,3,2,4,3,3};
        for (unsigned i=0;i<3;++i) {
            unsigned kind=foes[i].kind;
            if (kind>=12) re2_final_sense(&foes[i],&player);
            if (kind && ++enemy_delay[i]>delay[kind]) {
                enemy_delay[i]=0;
                update_enemy(&foes[i]);
            }
        }
        actor_audio();
        if (grabbed || struck) snap_walk();
        hold=re2_audio_active()?19531u:14746u;
    } while (elapsed>=hold);
    store_player();
    if (encounter_mask && !foes[0].health && !foes[1].health && !foes[2].health) {
        re2_progress[encounter_byte]&=(uint8_t)~encounter_mask;
        encounter_mask=0;
    }
    paint();
}
