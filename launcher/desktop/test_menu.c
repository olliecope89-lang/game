/* Desktop check: renders the menu to PPM files. gcc -O2 -I../source ../source/menu.c test_menu.c -o test_menu */
#include <stdio.h>
#include <stdlib.h>
#include "menu.h"
static void dump(const char *fn, const Menu *m, int w, int h)
{
    uint32_t *fb = calloc((size_t)w * h, 4);
    menu_render(m, fb, w, h, w);
    FILE *f = fopen(fn, "wb"); fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) { unsigned char p[3] = { (fb[i] >> 16) & 255, (fb[i] >> 8) & 255, fb[i] & 255 }; fwrite(p, 1, 3, f); }
    fclose(f); free(fb);
}
int main(void)
{
    Menu m; menu_init(&m);
    menu_update(&m, 0, 0.1f);
    dump("menu0.ppm", &m, 1280, 720);
    menu_update(&m, MENU_BTN_DOWN, 0.016f); menu_update(&m, MENU_BTN_DOWN, 0.016f); /* clamps at last entry */
    if (m.sel != 1) { printf("FAIL: down clamp (sel=%d)\n", m.sel); return 1; }
    menu_update(&m, MENU_BTN_UP, 0.016f); menu_update(&m, MENU_BTN_UP, 0.016f);
    if (m.sel != 0) { printf("FAIL: up clamp (sel=%d)\n", m.sel); return 1; }
    menu_update(&m, MENU_BTN_DOWN, 0.016f);
    dump("menu1.ppm", &m, 1280, 720);
    menu_update(&m, MENU_BTN_OK, 0.016f);
    if (m.launch != 1) { printf("FAIL: launch (%d)\n", m.launch); return 1; }
    dump("menu_loading.ppm", &m, 1280, 720);
    menu_fail(&m);
    if (m.launch != -1 || m.msg_t <= 0) { printf("FAIL: fail state\n"); return 1; }
    dump("menu_err.ppm", &m, 1280, 720);
    dump("menu_1080.ppm", &m, 1920, 1080); /* scaling sanity */
    printf("OK  files: %s %s\n", menu_target_file(0), menu_target_file(1));
    return 0;
}
