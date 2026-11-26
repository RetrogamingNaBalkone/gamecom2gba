#include "audio.h"
#include "audio_timing.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG32(a) (*(volatile uint32_t *)(a))
static const uint32_t *samples;
static unsigned sample_id, sample_length;

static void update_rate(unsigned sample)
{
    const struct PcmTiming *timing=&pcm_timing[sample_id-1];
    unsigned block=sample/128;
    unsigned period=block<timing->count?pcm_periods[timing->offset+block]:timing->tail;
    REG16(0x04000100)=(uint16_t)-period;
}
int re2_audio_active(void)
{
    return sample_id != 0;
}

void re2_audio_init(const uint8_t *assets)
{
    samples = (const uint32_t *)assets;
    REG16(0x04000084) = 0x80;
    REG16(0x04000082) = 0x0b06;
}

void re2_audio_tick(void)
{
    if (!sample_id) return;
    unsigned position=REG16(0x04000104);
    if (position >= sample_length) {
        REG16(0x04000102) = 0;
        REG16(0x040000c6) = 0;
        REG16(0x04000106) = 0;
        REG16(0x04000082) = 0x0b06;
        sample_id = 0;
    } else {
        /* About one hundred samples play before the next VBlank. Take
         * the period at the midpoint. Timer 0 latches it on overflow. */
        update_rate(position+50);
    }
}

void re2_sound(unsigned id)
{
    re2_audio_tick();
    if (!id || id > 24 || (sample_id && sample_id <= id)) return;
    const uint32_t *pcm = (const uint32_t *)((const uint8_t *)samples + samples[id-1]);
    REG16(0x04000102) = 0;
    REG16(0x040000c6) = 0;
    REG16(0x04000082) = 0x0b06;
    REG16(0x04000106) = 0;
    REG16(0x04000104) = 0;
    REG16(0x04000106) = 0x84;
    REG32(0x040000bc) = (uintptr_t)(pcm + 1);
    REG32(0x040000c0) = 0x040000a0;
    REG16(0x040000c6) = 0xb640;
    sample_length = pcm[0];
    sample_id = id;
    update_rate(0);
    REG16(0x04000102) = 0x80;
}
