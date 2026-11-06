/*
 * stubs.c - Reemplazos en el PC de las funciones de hardware (dueño A).
 * Permiten compilar y probar game_engine.c, game_fsm.c y render.c sin la Zybo.
 * Dueño: B (se amplía a medida que se necesiten).
 */
#include <stdio.h>
#include "interfaces.h"

int  sensors_init(void)        { return 0; }
void sensors_sample(void)      { }
int  sensors_shake(void)       { return 0; }
u8   sensors_crowd_level(void) { return 50; }
UiEvent sensors_joy_event(void){ return UI_NONE; }

Theme theme_current(void)      { return THEME_DARK; }

void buzzer_tone(Buzzer_t *bz, u16 f_hz, u8 vol)
{
    (void)bz;
    printf("[buzzer] %u Hz, vol %u %%\n", f_hz, vol);
}

int sd_mount(void) { return 0; }

int sd_read_file(const char *path, char *buf, u32 max, u32 *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    *len = (u32)fread(buf, 1, max, f);
    fclose(f);
    return 0;
}

int sd_write_file(const char *path, const char *buf, u32 len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(buf, 1, len, f);
    fclose(f);
    return 0;
}
