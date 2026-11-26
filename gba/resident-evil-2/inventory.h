#ifndef RE2_INVENTORY_H
#define RE2_INVENTORY_H
#include <stdint.h>

struct Re2InventoryState {
    unsigned magazine, reserve, weapon; /* 0 handgun, 1 knife */
};
extern struct Re2InventoryState re2_inventory_state;
void re2_inventory_reset(void);
void re2_inventory_restore(void);
void re2_inventory_store(void);
int re2_inventory_reload(unsigned item_id);
int re2_inventory_pending(void);
void re2_inventory_enter(const uint8_t *assets);
int re2_inventory_tick(unsigned held, unsigned pressed);
void re2_inventory_hide(void);
void re2_pause_enter(const uint8_t *assets);
void re2_inventory_time(const uint8_t *assets);
#endif
