/* Two-game launcher menu: portable logic + software rendering (no PS3 code in here). */
#ifndef MENU_H
#define MENU_H
#include <stdint.h>

#define MENU_W 1280
#define MENU_H_PX 720

enum { MENU_BTN_UP = 1, MENU_BTN_DOWN = 2, MENU_BTN_OK = 4 };

#define MENU_ITEMS 2

typedef struct {
    int   sel;        /* highlighted entry */
    int   launch;     /* -1 = nothing yet, else index the front-end must start */
    float t;          /* animation clock */
    float msg_t;      /* seconds left to show the error message */
} Menu;

void menu_init(Menu *m);
/* pressed = buttons that went down this frame (edge, not held) */
void menu_update(Menu *m, unsigned pressed, float dt);
void menu_fail(Menu *m);                       /* call if starting the game failed */
void menu_render(const Menu *m, uint32_t *fb, int w, int h, int pitch_px);
const char *menu_target_file(int index);       /* "RAGDOLL.SELF" / "FLOPPY.SELF" */
#endif
