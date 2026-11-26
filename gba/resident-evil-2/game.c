#include <stdint.h>
#include "audio.h"
#include "cinema.h"
#include "video.h"
#include "street.h"
#include "inventory.h"
#include "opening.h"
#include "scene_time.h"
#include "progress.h"
#include "campaign.h"
#include "special.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define COUNT(x) (sizeof(x)/sizeof((x)[0]))
extern const uint32_t assets[];

static const uint8_t *section(unsigned id)
{
    return (const uint8_t *)assets + assets[id];
}

static void title(unsigned layout)
{
    const uint8_t *block = section(1 + layout);
    re2_video_room(block + 1004, *(const uint32_t *)block, (const uint16_t *)(block+4));
}

static void clear_objects(void)
{
    for (unsigned i=0;i<128;++i) re2_video_hide(i);
}

static unsigned pause_code(unsigned step,unsigned keys)
{
    /* Bank 21:7152 selects sequence 1 on the first B. The other two
       pointer-table entries have no selection path in the US cartridge. */
    static const unsigned sequence[]={2,2,2,2,2,4,2};
    if (!keys) return step;
    if (keys!=sequence[step]) return 0;
    if (++step==COUNT(sequence)) {
        re2_progress[0xd6]|=4;
        re2_sound(16);
        return 0;
    }
    return step;
}

enum Mode { OPENING, TITLE, TITLE_WAIT, STREET, INVENTORY, PAUSE, SCENE, ENDING_FADE };
void play(void)
{
    const uint8_t *video = section(0);
    re2_video_init((const uint16_t *)video,video+32);
    re2_audio_init(section(8));
    re2_cinema_init(section(5));
    re2_campaign_init(section(9));
    re2_switch_init(section(10));
    re2_progress_new();
    enum Mode mode = OPENING;
    unsigned movie=1, next=0, time=0, last_slot=0, selected=0, previous=0;
    unsigned active_scene=0, code_step=0, fade_step=0, scene_hold=0;
    unsigned inherited_audio=0;
    unsigned blink_time=0, title_layout=1;
    const unsigned ticks_per_frame=4389;
    REG16(0x0400010a)=0x81;   /* Timer 2: 262144 ticks/s, 4389 per display period. */
    unsigned last_clock=REG16(0x04000108);
    re2_cinema_start(1);
    title(3);
    for (;;) {
        unsigned clock=REG16(0x04000108);
        unsigned elapsed=(uint16_t)(clock-last_clock);
        time += elapsed;
        if (mode!=PAUSE && mode!=TITLE_WAIT && !(mode==OPENING && movie==1 && time<299*ticks_per_frame) &&
            !(mode==ENDING_FADE && fade_step==0))
            re2_progress_time(elapsed,re2_audio_active() ? 19400u : 14410u);
        last_clock=clock;
        unsigned held=(~REG16(0x04000130))&1023;
        unsigned pressed=held & ~previous;
        previous=held;
        re2_audio_tick();
        if (mode == OPENING) {
            const struct Cue *cues = movie == 1 ? opening_1 : opening_2;
            unsigned count = movie == 1 ? COUNT(opening_1) : COUNT(opening_2);
            while (next < count && cues[next].time * ticks_per_frame <= time) {
                last_slot = cues[next++].slot;
                re2_cinema_slot(last_slot);
            }
            if (((pressed & 1) && (movie == 2 || last_slot > 0x12)) ||
                time >= ticks_per_frame * (movie == 1 ? 2796u : 2508u)) {
                mode=TITLE;
                selected=0;
                blink_time=0;
                title_layout=1;
                time=0;
                title(1);
            }
        } else if (mode == TITLE) {
            /* CE cycles through the two selected lists and the unlit list
               at 20:48A8. Each phase lasts four cartridge loop ticks. */
            blink_time+=re2_audio_active() ? elapsed*3/4 : elapsed;
            if (blink_time>=12*14410u) blink_time-=12*14410u;
            if (pressed & (64|128)) {
                selected ^= 1;
                time=blink_time=0;
                re2_sound(15);
            } else if (pressed & 1) {
                re2_sound(16);
                mode=TITLE_WAIT;
                time=0;
            } else if (time>=895*ticks_per_frame) {
                mode=OPENING;
                movie=2;
                next=time=last_slot=0;
                re2_cinema_start(2);
            }
            unsigned layout=blink_time<8*14410u ? 1+selected : 0;
            if (mode==TITLE && layout!=title_layout) {
                title_layout=layout;
                title(layout);
            }
        } else if (mode==TITLE_WAIT && time>=23*ticks_per_frame) {
            if (!selected || re2_save_load()) {
                re2_inventory_restore();
                mode=STREET;
                re2_street_enter(section(6));
            } else {
                time=blink_time=0;
                title_layout=1+selected;
                title(title_layout);
                mode=TITLE;
            }
        } else if (mode == STREET) {
            unsigned action=re2_progress[0x236];
            /* 20:4194 gates the inventory during recoil, death and scene changes. */
            if ((pressed&4) && re2_progress[0x238] && !(re2_progress[0x20b]&2) &&
                !(action>=0x48 && action<0x60)) {
                mode=INVENTORY;
                re2_inventory_enter(section(7));
            } else if (pressed & 8) {
                mode=PAUSE;
                code_step=0;
                re2_pause_enter(section(7));
            } else {
                re2_street_tick(held,pressed);
                if (re2_progress[0xc2]&1) {
                    mode=ENDING_FADE;
                    re2_progress[0xc2]|=2;
                    time=fade_step=0;
                } else if (re2_inventory_pending()) {
                    mode=INVENTORY;
                    re2_inventory_enter(section(7));
                }
                unsigned id=re2_progress[0xcc];
                if (mode==STREET && (re2_progress[0x20b]&2) && id>0 && id<=40) {
                    if (re2_progress[0x208]==4 && re2_progress[0x209]==0x1f &&
                        (re2_progress[0x20b]&4)) {
                        re2_progress[0x20b]=2;
                        re2_progress[0xcc]=id=13;
                    }
                    if (id!=0x19 && id!=0x1a) clear_objects();
                    active_scene=id;
                    inherited_audio=re2_audio_active();
                    scene_hold=scene_timing_offset[id];
                    mode=SCENE;
                    re2_cinema_start(id);
                    re2_cinema_advance();
                    re2_progress[0xcd]=re2_cinema_position();
                    if (id==0x1d) re2_inventory_time(section(7));
                    time=0;
                }
            }
        } else if (mode == ENDING_FADE) {
            static const uint8_t fade[]={0xe4,0xe5,0xea,0xff};
            static const uint8_t at_frame[]={87,113,143,172,202};
            if (time>=at_frame[fade_step]*ticks_per_frame) {
                if (fade_step<COUNT(fade)) re2_video_lcd_palette(fade[fade_step++]);
                else {
                    re2_save_finish();
                    re2_progress[0xc2]|=4;
                    re2_progress[0xcc]=active_scene=0x28;
                    inherited_audio=0;
                    time=0;
                    scene_hold=scene_timing_offset[active_scene];
                    clear_objects();
                    re2_video_lcd_palette(0xe4);
                    re2_cinema_start(active_scene);
                    re2_cinema_advance();
                    re2_progress[0xcd]=re2_cinema_position();
                    re2_inventory_time(section(7));
                    mode=SCENE;
                }
            }
        } else if (mode == SCENE) {
            if (active_scene==0x1a) re2_street_tick(0,0);
            unsigned delay=scene_frame_holds[scene_hold]*ticks_per_frame;
            /* Footsteps or gunfire can still be playing as a door opens.
               The cartridge DAC interrupt stretches main-loop ticks and can
               suppress a lower priority scene sound. Count those ticks live. */
            if (inherited_audio) {
                if (re2_audio_active()) time-=elapsed/4;
                delay=(re2_cinema_delay()+1)*14410u;
            }
            if (time>=delay) {
                time-=delay;
                ++scene_hold;
                if (!re2_cinema_advance()) {
                    re2_progress[0xcc]=active_scene|128;
                    re2_progress[0xcd]=0;
                    re2_progress[0x20b]&=0x7d;
                    re2_progress[0xd6]&=~64u;
                    if (active_scene==0x1a) re2_progress[0xd6]|=32;
                    re2_inventory_hide();
                    if (active_scene==0x19 || active_scene==0x28) {
                        re2_progress_new();
                        mode=OPENING;
                        movie=1;
                        next=last_slot=0;
                        time=301*ticks_per_frame;
                        re2_cinema_start(movie);
                        title(3);
                    } else {
                        mode=STREET;
                        re2_street_resume();
                    }
                } else {
                    re2_progress[0xcd]=re2_cinema_position();
                    if ((active_scene==0x28 && re2_cinema_position()>=0x1e) ||
                        (active_scene==0x19 && re2_cinema_position()==4)) clear_objects();
                }
            }
        } else if (mode==INVENTORY || mode==PAUSE) {
            if (mode==PAUSE && !(pressed&8)) code_step=pause_code(code_step,pressed);
            int result=mode==INVENTORY ? re2_inventory_tick(held,pressed) : !(pressed&8);
            if (result==2) {
                re2_progress_new();
                mode=OPENING;
                movie=1;
                next=time=last_slot=0;
                re2_cinema_start(1);
                clear_objects();
                title(3);
            } else if (!result) {
                mode=STREET;
                re2_inventory_hide();
                re2_street_resume();
            }
        }
        re2_video_present();
    }
}
