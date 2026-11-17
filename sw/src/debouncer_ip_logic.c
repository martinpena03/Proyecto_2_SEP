
#include "interfaces.h"

void debcfg_default(DebCfg *c)
{
    if (c == NULL) return;
    c->deb_ms       = 20u;              /* ctante para debouncear         */
    c->btn_mask     = 0x7u;             /* S1, S2 y JOY_SEL habilitados               */
    c->umbral_shake = 300u;             /* humbral para el shake */
    c->led_pattern  = 0xFu;             /* El modo luz lo representamos con los 4 leds prendidos        */
}


int debcfg_validate(const DebCfg *c)
{
    if (c == NULL)                                         return -1;
    if (c->deb_ms < DEB_MS_MIN || c->deb_ms > DEB_MS_MAX)  return -2;
    if (c->btn_mask < 1u || c->btn_mask > 0x7u)            return -3;
    if ((c->btn_mask & 0x1u) == 0u)                        return -4;
    if (c->umbral_shake < SHAKE_MG_MIN || c->umbral_shake > SHAKE_MG_MAX)   return -6;
    if (c->led_pattern < LED_PAT_MIN || c->led_pattern > LED_PAT_MAX)       return -7;
    return 0;
}
