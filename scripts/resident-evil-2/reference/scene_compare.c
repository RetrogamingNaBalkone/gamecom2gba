/* Run each original scene from the same settled street state. Native code
 * consumes the production CIN1 catalog. Only the test host flattens tiles. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "sys/gcsystem.h"
#include "sys/ppu.h"
#include "cinema.h"
#include "scene_time.h"
#include "opening.h"

static gcsystem_t machine, checkpoint;
static uint8_t reference[32000], native[32000];

void re2_sound(unsigned id) { (void)id; }
int re2_video_room(const uint8_t *tiles,unsigned count,const uint16_t *map)
{
    static const uint8_t shades[]={0,4,2,1,0};
    (void)count;
    for (unsigned y=0;y<160;++y) for (unsigned x=0;x<200;++x) {
        unsigned tile=map[(y/8)*25+x/8];
        unsigned pair=tiles[32*tile+4*(y%8)+(x%8)/2];
        native[200*y+x]=shades[(pair>>(4*(x%2)))&15];
    }
    return 1;
}

void re2_video_overlay(const uint8_t *tiles,const uint16_t *map,unsigned behind_actors)
{
    (void)tiles; (void)map; (void)behind_actors;
    /* The street, crane and actors are compared together in mGBA. */
}

int boot(const char *internal,const char *external,const char *cart)
{
    gcsystem_init(&machine);
    if (gcsystem_load_internal(&machine,internal) || gcsystem_load_external(&machine,external) ||
        gcsystem_load_cart(&machine,cart)) return 0;
    gcsystem_reset(&machine);
    return 1;
}

void advance(unsigned frames,unsigned a,unsigned b,unsigned c,int column,int row)
{
    gcbus_set_buttons(&machine.bus,a,b,c);
    for (int i=0;i<13;++i) gcbus_set_touch(&machine.bus,i,i==column ? 1u<<row : 0);
    while (frames--) gcsystem_run_frame(&machine);
}

void save_checkpoint(void) { checkpoint=machine; }

int compare_openings(void)
{
    static const unsigned start[]={1516,5207}, end[]={4312,7715};
    const struct Cue *cues[]={opening_1,opening_2};
    const unsigned counts[]={sizeof(opening_1)/sizeof(*opening_1),sizeof(opening_2)/sizeof(*opening_2)};
    unsigned next=0, changed=0;
    for (unsigned frame=1516;frame<7715;++frame) {
        advance(1,255,255,255,-1,0);
        unsigned i=frame>=start[1];
        if (frame>=end[i]) continue;
        if (frame==start[i]) {
            re2_cinema_start(i+1);
            next=changed=0;
        }
        if (next<counts[i] && frame-start[i]==cues[i][next].time) {
            unsigned slot=cues[i][next++].slot;
            if (machine.bus.ram[0xcd]!=slot || machine.bus.ram[0xcc]!=i+1) return -(int)frame;
            re2_cinema_slot(slot);
            changed=frame;
        }
        if (changed && frame==changed+7) {
            gc_render_shades(&machine.bus,reference);
            if (memcmp(native,reference,sizeof(native))) return frame;
        }
    }
    return 0;
}

static unsigned requested, sample_index, sample_count;
static unsigned sample_frame;
static unsigned dac_cycles[65536];
unsigned sample_cycle_at(unsigned i) { return dac_cycles[i]; }
static const uint8_t *expected_pcm;
static int mismatch;

static void sample_bus_write(void *ctx,uint16_t address,uint8_t value)
{
    if (requested && address==0xca && machine.cpu.pc==0x7136 && machine.bus.ram[0x27]==0x21) {
        /* Set the sample argument at the cartridge's normal request service. */
        value=requested-1;
        machine.cpu.reg[(machine.cpu.ps0&0xf8)+3]=value;
        requested=0;
    }
    if (!requested && address==0x4e && machine.cpu.pc==0x5351) {
        unsigned cycle=sample_frame*machine.cycles_per_frame+machine.bus.cur_cycle;
        if (sample_index<65536) dac_cycles[sample_index]=cycle;
        if (sample_index<sample_count && !mismatch &&
            (uint8_t)(value^128)!=expected_pcm[sample_index]) mismatch=1+(int)sample_index;
        ++sample_index;
    }
    gcbus_write(ctx,address,value);
}

int compare_sample(unsigned id,const uint8_t *pcm,unsigned count)
{
    machine=checkpoint;
    requested=id;
    expected_pcm=pcm;
    sample_count=count;
    sample_index=mismatch=0;
    machine.cpu.wr=sample_bus_write;
    machine.bus.ram[0xcc]=15;
    machine.bus.ram[0xcd]=machine.bus.ram[0xce]=0;
    machine.bus.ram[0x20b]|=2;
    for (unsigned frame=0;frame<400;++frame) {
        sample_frame=frame;
        advance(1,255,255,255,-1,0);
        if (mismatch) return mismatch;
        if (sample_index && !(machine.bus.ram[0xa8]&1)) return sample_index==count ? 0 : -2;
    }
    return -1;
}


int compare(unsigned scene)
{
    machine=checkpoint;
    uint8_t *state=machine.bus.ram;
    if (scene==40) state[0xc2]|=1;
    else {
        state[0xcc]=scene;
        state[0xcd]=state[0xce]=0;
        state[0x20b]|=2;
    }
    re2_cinema_start(scene);
    unsigned cursor=0, hold=scene_timing_offset[scene], changed=0, deadline=0;
    for (unsigned frame=0;frame<4000;++frame) {
        advance(1,255,255,255,-1,0);
        if (machine.cpu.trapped) return -1;
        if (state[0xcc]!=scene) {
            if (!cursor) continue;
            return frame==deadline && !re2_cinema_advance() ? 0 : -2;
        }
        if (state[0xcd]!=cursor) {
            if (cursor && frame!=deadline) return -3;
            cursor=state[0xcd];
            if (!re2_cinema_advance() || re2_cinema_position()!=cursor) return -4;
            deadline=frame+scene_frame_holds[hold++];
            changed=frame;
        }
        if (frame!=changed+7 || scene==26 || (scene==25 && cursor<=3)) continue;
        gc_render_shades(&machine.bus,reference);
        unsigned rows=(scene==29 || scene==40) ? 140 : 160;
        if (memcmp(native,reference,rows*200)) return (int)cursor;
    }
    return -5;
}
