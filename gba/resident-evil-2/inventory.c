#include "inventory.h"
#include "video.h"
#include "progress.h"
#include "campaign.h"

#define EWRAM __attribute__((section(".ewram"), aligned(4)))
enum { A=1,B=2,SELECT=4,RIGHT=16,LEFT=32,UP=64,DOWN=128 };
enum Screen { ITEMS, COMMAND, CHECK, CONFIRM, COMBINE, MAP, FILES, DOCUMENT, STORAGE, PICKUP, FULL, REWARD };
enum Asset { STATUS_BG, MAP_BG, FILE_BG, LIGHTER, OUTLINE, PANEL, COMMANDS, DISABLED,
             USE_QUESTION, YES_NO, FILE_SELECTION, FONT, TEXT, PAUSE_PANEL, DIGITS, PULSE,
             ICONS, DESCRIPTIONS, BOX_BG, TAKE_QUESTION, CLOCK, DOCUMENT_ARROW };
struct Re2InventoryState re2_inventory_state EWRAM;
static const uint8_t *block EWRAM;
static uint32_t bg[4000] EWRAM;  /* 500 4bpp tiles in display order */
static uint16_t tilemap[500] EWRAM;
static enum Screen screen EWRAM;
static unsigned focus EWRAM, item EWRAM, file_id EWRAM, page EWRAM, ticks EWRAM;
static unsigned box_slot EWRAM,box_top EWRAM,bag_slot EWRAM,selected_side EWRAM;

static uint8_t cursor_tiles[32] EWRAM;
static int last_pulse EWRAM;
static unsigned clock_uploaded EWRAM;

static const uint8_t *asset(unsigned id)
{
    return block+((const uint32_t *)block)[id];
}

static void pixel(unsigned x,unsigned y,unsigned shade)
{
    if (x>=200 || y>=160) return;
    unsigned index=((y>>3)*25+(x>>3))*8+(y&7), shift=(x&7)*4;
    bg[index]=(bg[index]&~(15u<<shift))|(shade<<shift);
}

static void draw_patch(const uint8_t *p,unsigned left,unsigned top)
{
    unsigned w=p[0],h=p[2];
    p+=4;
    for(unsigned y=0;y<h;++y)
        for(unsigned x=0;x<w;++x) {
            unsigned c=*p++;
            if(c) pixel(left+x,top+y,c);
        }
}

static void patch(unsigned id,unsigned x,unsigned y) { draw_patch(asset(id),x,y); }

static const uint8_t *object(unsigned catalog,unsigned id)
{
    const uint8_t *table=asset(catalog);
    return table+((const uint32_t *)table)[id-1];
}

static unsigned slot_item(unsigned slot)
{
    return slot<6?re2_progress[0x262+slot]:13; /* lighter occupies the top-right slot */
}

static void icon(unsigned id,unsigned x,unsigned y)
{
    if(id && id<=29) {
        const uint8_t *p=object(ICONS,id);
        draw_patch(p+4,x+p[0],y+p[1]);
    }
}

static void description(unsigned id,int short_name,unsigned left,unsigned top)
{
    if(!id || id>29) return;
    const uint8_t *p=object(DESCRIPTIONS,id);
    if(!short_name) { draw_patch(p+4,left,top); return; }
    unsigned w=p[2],h=p[3],stride=p[4];
    if(w>stride) w=stride;
    if(h>p[6]) h=p[6];
    for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
        unsigned shade=p[8+y*stride+x];
        if(shade) pixel(left+x,top+y,shade);
    }
}

static void glyph(unsigned c,unsigned left,unsigned top,unsigned shade)
{
    const uint8_t *font=asset(FONT)+(c&127)*8;
    for(unsigned y=0;y<8;++y)
        for(unsigned x=0;x<8;++x)
            if(font[y]&(128>>x)) pixel(left+x,top+y,shade);
}

static void number(unsigned n,unsigned right,unsigned y)
{
    unsigned hundreds=0;
    while(n>=100) { n-=100; ++hundreds; }
    unsigned tens=0;
    while(n>=10) { n-=10; ++tens; }
    const uint8_t *digits=asset(DIGITS);
    for(unsigned place=0;place<(hundreds?3u:tens?2u:1u);++place) {
        const uint8_t *p=digits+48*(place==2?hundreds:place?tens:n);
        unsigned left=right-5-place*6;
        for(unsigned row=0;row<8;++row) for(unsigned x=0;x<6;++x) {
            if(*p) pixel(left+x,y+row,*p);
            ++p;
        }
    }
}

static const uint8_t *document(void)
{
    const uint8_t *texts=asset(TEXT);
    return texts+((const uint32_t *)texts)[file_id];
}

static void file_page(void)
{
    const uint8_t *p=document()+1;
    for(unsigned i=0;i<page;++i) p+=2+p[0]+256*p[1];
    unsigned size=p[0]+256*p[1], x=0,y=8;
    p+=2;
    for(unsigned i=0;i<size;++i) {
        if(p[i]==255) { x=0; y+=8; }
        else { glyph(p[i],x,y,4); x+=8; }
    }
    if(page+1<document()[0]) patch(DOCUMENT_ARROW,185,152);
}

static unsigned equipped(void)
{
    static const uint8_t ids[]={1,3,2,13,0};
    return re2_inventory_state.weapon<5?ids[re2_inventory_state.weapon]:0;
}

static int usable(unsigned id)
{
    return id==1 || id==2 || id==3 || id==6 || id==13 || id==14 || id==29;
}

static unsigned quantity(unsigned id)
{
    if(id==7) return re2_inventory_state.reserve;
    if(id==16) return re2_progress[0x297];
    if(id==11) return re2_progress[0x298];
    return 0;
}

static void equipment(void)
{
    int storage=screen==STORAGE;
    if(!storage) patch(LIGHTER,165,20);
    unsigned gun=equipped();
    icon(gun,storage?150:116,storage?14:13);
    if(gun==1 || gun==2) number(re2_inventory_state.magazine,storage?171:137,storage?32:31);
    for(unsigned i=0;i<6;++i) {
        unsigned id=slot_item(i),x=138+(i&1)*30,y=49+(i>>1)*30;
        icon(id,x,y);
        unsigned amount=quantity(id);
        if(amount) number(amount,x+21,y+18);
    }
}

static void status(void)
{
    equipment();
    unsigned id=screen>=PICKUP?re2_progress[0x270]:slot_item(item);
    if(screen<PICKUP && item<6) patch(OUTLINE,138+(item&1)*30,49+(item>>1)*30);
    if(screen==COMMAND || screen==COMBINE) {
        patch(COMMANDS,1,46);
        if(!usable(id)) patch(DISABLED,99,60);
        return;
    }
    for(unsigned y=51;y<=71;y+=20)
        for(unsigned x=31;x<=68;x+=37) patch(PANEL,x,y);
    icon(id,60,60);
    if(screen==CHECK || screen==REWARD) description(id,0,6,110);
    if(screen==CONFIRM || screen==PICKUP) {
        patch(screen==PICKUP?TAKE_QUESTION:USE_QUESTION,6,146);
        patch(YES_NO,90,147);
    }
}

static void storage(void)
{
    equipment();
    for(unsigned i=0;i<3;++i) {
        unsigned id=re2_progress[0x272+box_top+i];
        icon(id,90,18+20*i);
        description(id,1,6,25+20*i);
    }
    unsigned selected=selected_side?re2_progress[0x272+box_slot]:slot_item(bag_slot);
    description(selected,0,6,110);
}

static void render(void)
{
    if(screen==MAP) {
        const uint8_t *maps=asset(MAP_BG);
        unsigned area=re2_progress[0x208],camera=re2_progress[0x209];
        static const uint8_t counts[]={33,76,41,43,112,78,37};
        if(area>=7 || camera>=counts[area]) area=camera=0;
        const uint8_t *row=maps+((const uint32_t *)maps)[area]+camera*8;
        const uint8_t *map=maps+*(const uint32_t *)row;
        re2_video_room(map+1004,*(const uint32_t *)map,(const uint16_t *)(map+4));
        return;
    }
    if(screen==DOCUMENT) {
        for(unsigned i=0;i<4000;++i) bg[i]=0x11111111;
        file_page();
    } else {
        unsigned id=screen==FILES?FILE_BG:screen==STORAGE?BOX_BG:STATUS_BG;
        const uint32_t *src=(const uint32_t *)asset(id);
        for(unsigned i=0;i<4000;++i) bg[i]=src[i];
        if(screen==FILES) {
            draw_patch(asset(FILE_SELECTION)+468*file_id,71,8+11*file_id+(file_id==12?7:0));
        } else if(screen==STORAGE) storage();
        else status();
    }
    re2_video_room((const uint8_t *)bg,500,tilemap);
}

void re2_inventory_hide(void)
{
    clock_uploaded=0;
    for(unsigned i=0;i<24;++i) re2_video_hide(i);
}

void re2_inventory_time(const uint8_t *assets)
{
    block=assets;
    if(!clock_uploaded) {
        re2_video_obj_tiles(768,asset(CLOCK),53);
        clock_uploaded=1;
    }
    re2_video_ui_sprite(0,32,140,788,1,3,0);
    for(unsigned i=0;i<3;++i) {
        unsigned n=re2_progress[0x284+i],tens=0;
        while(n>=10) { n-=10; ++tens; }
        re2_video_ui_sprite(1+2*i,100+25*i,140,768+2*tens,2,0,0);
        re2_video_ui_sprite(2+2*i,110+25*i,140,768+2*n,2,0,0);
        if(i<2) re2_video_ui_sprite(7+i,120+25*i,143,820,0,0,0);
    }
}

static void cursor(void)
{
    static const uint8_t row_x[]={69,102,135,168,138,168,138,168,138,168,163};
    static const uint8_t row_y[]={0,0,0,0,48,48,79,79,109,109,15};
    int x=0,y=0;
    unsigned w=0,h=0;
    if(screen==ITEMS || screen==COMBINE) {
        unsigned index=screen==COMBINE?4+focus:focus;
        x=row_x[index]; y=row_y[index]; w=index<4?31:30; h=index<4?14:30;
    } else if(screen==COMMAND) {
        x=97; y=58+13*focus; w=36; h=12;
    } else if(screen==CONFIRM || screen==PICKUP) {
        x=88+23*focus; y=145; w=19; h=10;
    } else if(screen==STORAGE) {
        x=selected_side?89:138+30*(bag_slot&1);
        y=selected_side?16+20*(box_slot-box_top):49+30*(bag_slot>>1);
        w=30;h=selected_side?23:30;
    }
    for(unsigned i=0;i<4;++i) {
        if(w) re2_video_ui_sprite(i,x+((i&1)?(int)w-8:0),
                                 y+((i&2)?(int)h-8:0),768,0,0,i);
        else re2_video_hide(i);
    }
}

static void pulse(void)
{
    if(screen==MAP) {
        const uint8_t *maps=asset(MAP_BG);
        unsigned a=re2_progress[0x208],c=re2_progress[0x209];
        static const uint8_t counts[]={33,76,41,43,112,78,37};
        if(a>=7 || c>=counts[a]) a=c=0;
        const uint8_t *row=maps+((const uint32_t *)maps)[a]+8*c;
        if(ticks&8) re2_video_hide(4);
        else re2_video_ui_sprite(4,row[4],row[5],777,0,0,0);
        re2_video_hide(5); last_pulse=-1; return;
    }
    if(screen==FILES || screen==DOCUMENT || screen==STORAGE) {
        re2_video_hide(4); re2_video_hide(5); last_pulse=-1; return;
    }
    unsigned phase=0;
    while(phase<9 && ticks>=13*(phase+1)) ++phase;
    unsigned hp=re2_progress[0x238],group=hp<=30?0:hp<=60?1:2;
    unsigned sequence=group*11;
    int frame=sequence+1+(phase==9?0:phase+1);
    if(frame!=last_pulse) {
        const uint8_t *p=asset(PULSE)+260*frame,*label=asset(PULSE)+260*sequence;
        re2_video_obj_tiles(769,p+4,8); re2_video_ui_sprite(4,p[0],p[1],769,1,2,0);
        re2_video_obj_tiles(778,label+4,8); re2_video_ui_sprite(5,label[0],label[1],778,1,2,0);
    }
    last_pulse=frame;
}

void re2_inventory_reset(void)
{
    re2_inventory_state.magazine=18;
    re2_inventory_state.reserve=45;
    re2_inventory_state.weapon=0;
}

void re2_inventory_restore(void)
{
    re2_inventory_state.magazine=re2_progress[0x295];
    re2_inventory_state.reserve=re2_progress[0x296];
    switch(re2_progress[0x268]) {
        case 1:re2_inventory_state.weapon=0;break;
        case 3:re2_inventory_state.weapon=1;break;
        case 2:re2_inventory_state.weapon=2;break;
        case 13:re2_inventory_state.weapon=3;break;
        default:re2_inventory_state.weapon=4;break;
    }
}

void re2_inventory_store(void)
{
    re2_progress[0x295]=re2_inventory_state.magazine;
    re2_progress[0x296]=re2_inventory_state.reserve;
    re2_progress[0x268]=equipped();
}

void re2_inventory_enter(const uint8_t *assets)
{
    block=assets; screen=ITEMS; item=ticks=0; last_pulse=-1;
    focus=4;
    if((re2_progress[0xd6]&128) && re2_progress[0x270] && re2_progress[0x270]<=29) {
        screen=REWARD; focus=0;
    } else if(re2_pickup_pending()) {
        unsigned id=re2_progress[0x270];
        screen=FULL;
        for(unsigned i=0;i<6;++i)
            if(!slot_item(i) || ((id==7 || id==11 || id==16) && slot_item(i)==id)) screen=PICKUP;
        focus=0;
    }
    else if(re2_progress[0x261]==5 && (re2_progress[0x270]&128)) {
        screen=STORAGE; box_slot=box_top=bag_slot=selected_side=0;
    }
    for(unsigned i=0;i<500;++i) tilemap[i]=(uint16_t)i;
    for(unsigned i=0;i<128;++i) re2_video_hide(i);
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;x+=2) {
        unsigned a=(x<6 && !y) || (!x && y<6)?4:(x<5 && y==1)||(x==1 && y<5)?1:0;
        unsigned b=(x+1<6 && !y)?4:(x+1<5 && y==1)||(x+1==1 && y<5)?1:0;
        cursor_tiles[4*y+x/2]=(uint8_t)(a|(b<<4));
    }
    re2_video_obj_tiles(768,cursor_tiles,1);
    static const uint32_t marker[8]={0x1111,0x1111,0x1111,0x1111,0,0,0,0};
    re2_video_obj_tiles(777,(const uint8_t *)marker,1);
    render();
    cursor();
    pulse();
}

void re2_pause_enter(const uint8_t *assets)
{
    block=assets;
    const uint8_t *p=asset(PAUSE_PANEL);
    re2_video_obj_tiles(768,p+20,16*p[0]);
    for(unsigned y=0;y<3;++y) for(unsigned x=0;x<6;++x) {
        unsigned i=y*6+x;
        re2_video_ui_sprite(i,8+32*x,39+32*y,768+16*p[2+i],0,2,0);
    }
}

static void remove_item(unsigned slot)
{
    for(unsigned i=slot;i<5;++i) re2_progress[0x262+i]=re2_progress[0x263+i];
    re2_progress[0x267]=0;
}

int re2_inventory_pending(void)
{
    unsigned value=re2_progress[0x270],flags=re2_progress[0xd6];
    if(flags&64) return 0;
    return re2_pickup_pending() || ((flags&128) && value>0 && value<=29) ||
           (re2_progress[0x261]==5 && (value&128));
}

int re2_inventory_reload(unsigned id)
{
    if(id!=1 && id!=2) return 0;
    unsigned magazine=re2_inventory_state.magazine, cap=id==1?18:5;
    unsigned reserve=id==1?re2_inventory_state.reserve:re2_progress[0x297];
    unsigned n=magazine>=cap?0:cap-magazine;
    if(n>reserve) n=reserve;
    re2_inventory_state.magazine+=n;
    if(id==1) re2_inventory_state.reserve-=n;
    else re2_progress[0x297]-=n;
    return n!=0;
}

static void equip(void)
{
    unsigned id=slot_item(item),old=equipped();
    if(id==14 || id==29 || id==6) {
        unsigned hp=re2_progress[0x238]+(id==6?40:id==29?80:100);
        re2_progress[0x238]=hp>100?100:hp;
        remove_item(item);
        return;
    }
    if(id!=1 && id!=2 && id!=3 && id!=13) return;
    if(old==1 || old==2) {
        unsigned amount=(old==1?re2_inventory_state.reserve:re2_progress[0x297])+re2_inventory_state.magazine;
        if(amount>255) amount=255;
        if(old==1) re2_inventory_state.reserve=amount;
        else re2_progress[0x297]=amount;
        re2_inventory_state.magazine=0;
    }
    if(id==old) id=0;
    re2_inventory_state.weapon=id==1?0:id==3?1:id==2?2:id==13?3:4;
    if(id==1 || id==2) re2_inventory_reload(id);
    re2_progress[0x239]=id==1?0:id==2?1:id==3?2:id==13?1:1;
    re2_inventory_store();
}

static void navigate_items(unsigned dir)
{
    if(focus<4) {
        if(dir&LEFT && focus) --focus;
        else if(dir&RIGHT && focus<3) ++focus;
        else if(dir&DOWN) focus=focus==3?5:4;
    } else if(focus==10) {
        if(dir&UP) focus=3;
        else if(dir&LEFT) focus=2;
        else if(dir&DOWN) focus=5;
    } else if(dir&UP) focus=focus>=6?focus-2:focus==5?10:2;
    else if(dir&DOWN && focus<8) focus+=2;
    else if(dir&LEFT && (focus&1)) --focus;
    else if(dir&RIGHT && !(focus&1)) ++focus;
}

static int storage_tick(unsigned pressed)
{
    if((pressed&SELECT) || ((pressed&B) && !selected_side)) {
        re2_progress[0x270]=0; re2_progress[0x261]=2;
        re2_inventory_hide(); return 0;
    }
    if(selected_side) {
        if(pressed&UP && box_slot) --box_slot;
        if(pressed&DOWN && box_slot<12) ++box_slot;
        if(box_slot<box_top) box_top=box_slot;
        if(box_slot>box_top+2) box_top=box_slot-2;
        if(pressed&A) {
            re2_inventory_store();
            re2_box_exchange(bag_slot,box_slot);
            re2_inventory_restore();
        }
        if(pressed&(A|B)) selected_side=0;
    } else {
        if(pressed&UP && bag_slot>=2) bag_slot-=2;
        if(pressed&DOWN && bag_slot<4) bag_slot+=2;
        if(pressed&LEFT && (bag_slot&1)) --bag_slot;
        if(pressed&RIGHT && !(bag_slot&1)) ++bag_slot;
        if(pressed&A) selected_side=1;
    }
    if(pressed) { render(); cursor(); }
    return 1;
}

int re2_inventory_tick(unsigned held,unsigned pressed)
{
    (void)held;
    if(++ticks>=131) ticks=0;
    if(screen==STORAGE) return storage_tick(pressed);
    if(screen>=PICKUP) {
        if(pressed&(LEFT|RIGHT)) focus^=1;
        if(pressed&(A|B|SELECT)) {
            re2_inventory_store();
            if(screen==REWARD) {
                re2_progress[0x270]=0;
                re2_progress[0xd6]&=127;
            } else re2_pickup_confirm(screen==PICKUP && (pressed&A) && !focus);
            re2_inventory_restore();
            re2_inventory_hide(); return 0;
        }
        cursor(); pulse(); return 1;
    }
    if(pressed&SELECT || (pressed&B && screen==ITEMS)) {
        re2_inventory_hide(); return 0;
    }
    enum Screen old=screen;
    unsigned old_item=item,old_page=page,old_file=file_id;
    if(pressed&B) {
        if(screen==DOCUMENT) screen=FILES;
        else { screen=ITEMS; focus=4+item; }
    } else if(screen==ITEMS) {
        navigate_items(pressed);
        if(pressed&A) {
            if(focus==0) { screen=FILES; file_id=0; }
            else if(focus==1) screen=MAP;
            else if(focus==2) { focus=4; item=0; }
            else if(focus==3) { re2_inventory_hide(); return 2; }
            else if(slot_item(focus-4)) {
                if(focus-4==item) {
                    unsigned id=slot_item(item);
                    screen=COMMAND;
                    focus=usable(id)?0:1;
                } else item=focus-4;
            }
        }
    } else if(screen==COMMAND) {
        unsigned id=slot_item(item);
        unsigned first=usable(id)?0:1;
        if(pressed&UP && focus>first) --focus;
        if(pressed&DOWN && focus<2) ++focus;
        if(pressed&A) {
            screen=focus==0?CONFIRM:focus==1?CHECK:COMBINE;
            focus=screen==COMBINE?item:0;
        }
    } else if(screen==CHECK || screen==MAP) {
        if(pressed&A) { focus=old==MAP?1:4+item; screen=ITEMS; }
    } else if(screen==CONFIRM) {
        if(pressed&(LEFT|RIGHT)) focus^=1;
        if(pressed&A) {
            if(!focus) equip();
            screen=ITEMS; focus=4+item;
        }
    } else if(screen==COMBINE) {
        if(pressed&(LEFT|UP) && focus) --focus;
        if(pressed&(RIGHT|DOWN) && focus<5) ++focus;
        if(pressed&A) {
            unsigned a=slot_item(item),b=slot_item(focus);
            if((a==1&&b==7)||(a==7&&b==1)) { if(equipped()==1) re2_inventory_reload(1); }
            else if((a==2&&b==16)||(a==16&&b==2)) { if(equipped()==2) re2_inventory_reload(2); }
            else if(a==6 && b==6 && item!=focus) {
                re2_progress[0x262+item]=29;
                remove_item(focus);
            }
            screen=ITEMS; focus=4+item;
        }
    } else if(screen==FILES) {
        if(pressed&UP && file_id) --file_id;
        if(pressed&DOWN && file_id<12) ++file_id;
        if(pressed&A) {
            screen=file_id==12?ITEMS:DOCUMENT;
            page=0; focus=0;
        }
    } else if(screen==DOCUMENT) {
        if(pressed&A) screen=FILES;
        else if(pressed&(DOWN|RIGHT) && page+1<document()[0]) ++page;
        else if(pressed&(UP|LEFT) && page) --page;
    }
    if(old!=screen || old_item!=item || old_page!=page || old_file!=file_id) render();
    cursor();
    pulse();
    return 1;
}
