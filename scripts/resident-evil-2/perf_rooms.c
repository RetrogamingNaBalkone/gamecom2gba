#include "video.h"

#define TIMER (*(volatile uint16_t *)0x04000100)
extern const uint32_t assets[];
static volatile uint32_t *const mailbox=(void *)0x03007000;
static const uint16_t colors[16]={0,0x477f,0x262d,0x1521,0};
static const uint8_t bezel[19200]={0};

void play(void)
{
    re2_video_init(colors,bezel);
    const uint8_t *start=(const uint8_t *)assets;
    const uint8_t *atlas=start+assets[0];
    *(volatile uint16_t *)0x04000102=0x81;
    unsigned previous=TIMER;
    for (unsigned frame=1;;++frame) {
        unsigned begin=TIMER;
        for (unsigned front=0;front<=1;++front) {
            if (!assets[1+front]) continue;
            const uint8_t *plane=start+assets[1+front];
            const uint8_t *tiles=start+assets[3+front];
            const uint16_t *map=(const uint16_t *)(plane+4);
            unsigned count=*(const uint32_t *)plane;
            if (frame&1) re2_video_plane(atlas,plane,front);
            else if (front) re2_video_front(tiles,count,map);
            else re2_video_room(tiles,count,map);
        }
        unsigned cost=(uint16_t)(TIMER-begin);
        re2_video_present();
        unsigned end=TIMER;
        mailbox[1]=cost;
        mailbox[2]=(uint16_t)(end-previous);
        mailbox[0]=frame;
        previous=end;
    }
}
