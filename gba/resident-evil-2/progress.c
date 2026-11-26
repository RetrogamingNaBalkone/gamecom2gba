#include "progress.h"

uint8_t re2_progress[0x29c];
static unsigned fraction;

/* Game.com records 1B3A and 1B3B, in the order of the cartridge's scatter lists. */
static const uint16_t fields[] = {
    0x208,0x209,0x20b,0xd3,0xd4,0xd5,0x290,0x291,0x292,0x293,0x294,0x25b,0x25c,
    0x238,0x262,0x263,0x264,0x265,0x266,0x267,0x268,0x272,0x273,0x274,0x275,0x276,
    0x277,0x278,0x279,0x27a,0x27b,0x287,0x286,0x285,0x284,0x20e,0x296,0x297,0x295,
    0x299,0x29a,0x253,0x254,0x255,0x256,0x257,0x258,0x27c,0x27d,0x27e,0x298,0x29b
};

void re2_progress_new(void)
{
    for (unsigned i=0;i<sizeof(re2_progress);++i) re2_progress[i]=0;
    for (unsigned i=0x290;i<=0x294;++i) re2_progress[i]=255;
    for (unsigned i=0x253;i<=0x258;++i) re2_progress[i]=255;
    re2_progress[0x25c]=255;
    re2_progress[0x238]=100;
    re2_progress[0x262]=1;
    re2_progress[0x263]=3;
    re2_progress[0x264]=7;
    re2_progress[0x268]=1;
    for (unsigned i=0x272;i<=0x274;++i) re2_progress[i]=14;
    re2_progress[0x296]=45;
    re2_progress[0x297]=5;
    re2_progress[0x295]=18;
    fraction=0;
}

/* The cartridge advances D7 once per main-loop tick, including cutscenes.
 * At a typewriter it copies the live counters into the saved-time fields. */
void re2_progress_time(unsigned timer_ticks, unsigned period)
{
    fraction += timer_ticks;
    while (fraction >= period) {
        fraction -= period;
        if (++re2_progress[0xd7]<20) continue;
        re2_progress[0xd7]=0;
        if (++re2_progress[0xd8]<60) continue;
        re2_progress[0xd8]=0;
        if (++re2_progress[0xd9]<60) continue;
        re2_progress[0xd9]=0;
        if (re2_progress[0xda]<99) ++re2_progress[0xda];
        else re2_progress[0xd8]=re2_progress[0xd9]=59;
    }
}

static void capture_time(void)
{
    for (unsigned i=0;i<4;++i) re2_progress[0x284+i]=re2_progress[0xda-i];
}

/* One player-visible save. Alternating copies preserve it during a power loss.
 * SRAM on the GBA requires byte accesses. Header: commit, R, 2, version,
 * sequence, then the 52 payload bytes and a 32-bit checksum. */
static volatile uint8_t *const sram=(volatile uint8_t *)0x0e000000;
enum { COPY_SIZE=64, PAYLOAD=5, COUNT=sizeof(fields)/sizeof(fields[0]), SUM=PAYLOAD+COUNT };
static uint32_t checksum(volatile uint8_t *copy)
{
    uint32_t sum=2166136261u;
    for (unsigned i=1;i<SUM;++i) sum=(sum^copy[i])*16777619u;
    return sum;
}
static int valid(volatile uint8_t *copy)
{
    if (copy[0]!=0xa5 || copy[1]!='R' || copy[2]!='2' || copy[3]!=1) return 0;
    uint32_t sum=checksum(copy);
    for (unsigned i=0;i<4;++i)
        if (copy[SUM+i]!=(uint8_t)(sum>>(8*i))) return 0;
    return 1;
}
static volatile uint8_t *latest(void)
{
    volatile uint8_t *second=sram+COPY_SIZE;
    if (!valid(sram)) return valid(second) ? second : 0;
    if (!valid(second)) return sram;
    return (int8_t)(second[4]-sram[4])>0 ? second : sram;
}
int re2_save_available(void) { return latest()!=0; }
int re2_save_load(void)
{
    volatile uint8_t *copy=latest();
    if (!copy) return 0;
    re2_progress_new();
    for (unsigned i=0;i<COUNT;++i) re2_progress[fields[i]]=copy[PAYLOAD+i];
    for (unsigned i=0;i<4;++i) re2_progress[0xda-i]=re2_progress[0x284+i];
    re2_progress[0x261]=2;
    re2_progress[0x269]=6;
    re2_progress[0x282]=1;
    return 1;
}
void re2_save_commit(void)
{
    capture_time();
    volatile uint8_t *old=latest();
    volatile uint8_t *copy=old==sram ? sram+COPY_SIZE : sram;
    copy[0]=0;
    copy[1]='R'; copy[2]='2'; copy[3]=1;
    copy[4]=old ? old[4]+1 : 0;
    for (unsigned i=0;i<COUNT;++i) copy[PAYLOAD+i]=re2_progress[fields[i]];
    uint32_t sum=checksum(copy);
    for (unsigned i=0;i<4;++i) copy[SUM+i]=(uint8_t)(sum>>(8*i));
    copy[0]=0xa5;
}

/* The best clear time lives independently of the typewriter snapshot. */
void re2_save_finish(void)
{
    capture_time();
    volatile uint8_t *record=sram+2*COPY_SIZE;
    if (record[0]==0x2a) {
        unsigned i=0;
        while (i<3 && record[1+i]==re2_progress[0x284+i]) ++i;
        if (i==3 || record[1+i]<re2_progress[0x284+i]) return;
    }
    record[0]=0;
    for (unsigned i=0;i<3;++i) record[1+i]=re2_progress[0x284+i];
    record[0]=0x2a;
}
