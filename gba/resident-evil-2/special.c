#include "special.h"
#include "audio.h"
#include "inventory.h"
#include "progress.h"

static const uint8_t *switch_tiles;
void re2_switch_init(const uint8_t *tiles)
{
    switch_tiles=tiles;
}

static int switch_room(void)
{
    return re2_progress[0x208]==6 && re2_progress[0x209]==0x19;
}

int re2_switch_press(void)
{
    unsigned x=re2_progress[0x234];
    if (!switch_room() || x<30 || x>=36 || (re2_progress[0xd6]&16)) return 0;
    re2_progress[0xd6]|=16;
    re2_progress[0xcc]=0x1a;
    re2_progress[0xcd]=0;
    re2_progress[0x20b]|=2;
    return 1;
}

const uint8_t *re2_switch_picture(unsigned index,int *x,int *y)
{
    if (!switch_room()) return 0;
    if (!(re2_progress[0xd6]&16)) {
        if (index) return 0;
        *x=33; *y=32;
        return switch_tiles;
    }
    if (!(re2_progress[0xd6]&32) || index>1) return 0;
    *x=64+64*index; *y=95;
    return switch_tiles+2048*(index+1);
}

/* The two large actors have independent handlers at 22:7069 and 22:6F81. */
void re2_final_sense(struct Re2Actor *boss,const struct Re2Actor *player)
{
    /* 22:6F48 handles the gas-cylinder explosion separately from the cue table. */
    if (boss->kind==13 && boss->action==7) {
        if (!re2_audio_active()) re2_sound(4);
        else if (re2_actor_random(5)==3) {
            unsigned roll=re2_actor_random(10);
            re2_sound_restart(roll<=4?5:4);
        }
    }
    if (!boss->health || !player->health) return;
    int range=boss->facing==6?boss->x-player->x:player->x-boss->x;
    if (boss->kind==13) {
        static const uint8_t reach[]={80,85,80};
        if (player->action>=0x6a && player->action<=0x6d) return;
        if (!(re2_progress[0xc4]&8) && range>=0 && range<=reach[player->depth] &&
            boss->action<=2) {
            static const uint8_t attack[]={4,9,5};
            re2_actor_action(boss,attack[player->depth]);
        }
        return;
    }
    if (boss->kind!=12 || (boss->action>=4 && boss->action<=11)) return;
    if (re2_progress[0xd2]) { --re2_progress[0xd2]; return; }
    if (player->action>=0x6a && player->action<=0x6d) return;
    if (range<0 || boss->action>=14) return;
    unsigned left=boss->facing==6;
    unsigned alternate=player->depth==boss->depth && re2_actor_random(10)==5;
    if (alternate) {
        if (range<40 || range>100) return;
        re2_actor_action(boss,10+left);
    } else {
        if (range<8 || range>50) return;
        re2_actor_action(boss,8-2*player->depth+left);
    }
    unsigned delay;
    do { delay=re2_actor_random(50); } while (delay<25);
    re2_progress[0xd2]=delay;
}

void re2_final_hit(struct Re2Actor *boss, struct Re2Actor *player)
{
    (void)player;
    if (!boss->health) return;
    unsigned weapon=re2_inventory_state.weapon;
    if (boss->kind==13) {
        if (!(re2_progress[0xc4]&8)) return;
        re2_progress[0xc2]|=8;
        if (weapon==1) return;
        boss->health=0;
        re2_actor_action(boss,7);
        return;
    }
    unsigned damage=weapon==1 ? 1 : weapon==2 ? 6 : 3;
    boss->health=boss->health>damage ? boss->health-damage : 0;
    unsigned left=boss->facing==6;
    if (!boss->health) {
        re2_progress[0xc3]|=32;
        re2_progress[0xc2]|=8;
    }
    re2_actor_action(boss,(boss->health ? 0x12u : 0x0eu)+left);
    boss->frame=0;
}

int re2_final_ai(struct Re2Actor *boss, struct Re2Actor *player)
{
    if (boss->kind!=12 && boss->kind!=13) return 0;
    ++boss->age;
    int delta=re2_actor_advance(boss);
    if (boss->frame) {
        boss->x+=delta;
        return 1;
    }
    if (!boss->health) {
        if (boss->kind==12) {
            re2_progress[0xc2]|=1;
            re2_actor_action(boss,0x10+(boss->facing==6));
        } else {
            re2_progress[0xc2]=(re2_progress[0xc2]|64)&~8;
            re2_actor_action(boss,8);
        }
        return 1;
    }
    int distance=player->x-boss->x;
    unsigned range=(unsigned)(distance<0 ? -distance : distance);
    unsigned lane=player->depth<3 ? player->depth : 1;
    if (boss->kind==13) {
        static const unsigned attack[]={4,9,5};
        if ((re2_progress[0xd6]&32 || re2_progress[0xc4]&8) &&
            re2_progress[0x209]==0x19 && boss->x<=175) {
            re2_progress[0xc4]|=8;
            re2_progress[0xc2]|=32;
            re2_progress[0xd6]&=~32;
            re2_actor_action(boss,6);
        } else if (boss->x>160) re2_actor_action(boss,0);
        else if (range<=40) re2_actor_action(boss,attack[lane]);
        else re2_actor_action(boss,re2_actor_random(7)==5 ? 9 : 1);
    } else {
        unsigned left=distance<0;
        unsigned action;
        if (boss->action>=4 && boss->action<=11) {
            left=boss->facing==6;
            action=left;
            boss->stagger=2;
        } else if (boss->stagger) {
            left=boss->facing==6;
            --boss->stagger;
            action=boss->stagger ? left : 2+left;
        } else if (left!=(boss->facing==6)) action=left ? 13 : 12;
        else action=2+left;
        boss->facing=left ? 6 : 2;
        re2_actor_action(boss,action);
    }
    boss->x+=re2_actor_delta(boss);
    return 1;
}
