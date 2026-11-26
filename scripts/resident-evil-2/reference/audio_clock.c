/* Exercise the production FIFO and cascaded sample counter on the GBA. */
#include <stdint.h>
#include "audio.h"

extern const uint8_t assets[];
#define REG16(a) (*(volatile uint16_t *)(a))

void play(void)
{
    volatile uint32_t *report=(volatile uint32_t *)0x03007000;
    unsigned id=0, frame=0;
    re2_audio_init(assets);
    for (;;) {
        while (REG16(0x04000006)>=160) {}
        while (REG16(0x04000006)<160) {}
        re2_audio_tick();
        if (!re2_audio_active() && id<24) {
            re2_sound(++id);
            frame=0;
        }
        report[0]=id;
        report[1]=frame++;
        report[2]=REG16(0x04000104);
    }
}
