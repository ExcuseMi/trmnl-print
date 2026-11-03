/* The X's position (API 2.5) with the fallbacks for older firmware: follow it, the bar's zones, the orientation
   event. The same small header sits in the Game Boy app (apps/trmnl-x/src/app_orient.h): apps/common of the firmware
   fork is not ours to change. SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#ifndef APP_ORIENT_H
#define APP_ORIENT_H
#include <app_api.h>

#define AO_SLOT_FOLLOW 44 /* api->size >= APP_API_SLOT(44): device_orientation, orientation_follow, touchbar_layout */

static inline int ao_has_position(const app_api_t *api) { return api->size >= APP_API_SLOT(AO_SLOT_FOLLOW); }

/* Starts following the X's position (settle 0: the firmware's default wait); returns the position to draw in now.
   Firmware before 2.5: the two positions of screen_orientation (2.2), else the bar at the bottom. */
static inline int ao_start(const app_api_t *api)
{
    if (ao_has_position(api)) {
        api->orientation_follow(true, 0);
        int o = api->device_orientation();
        return o >= APP_ORIENT_BAR_BOTTOM && o <= APP_ORIENT_BAR_RIGHT ? o : APP_ORIENT_BAR_BOTTOM;
    }
    if (api->size >= APP_API_SLOT(36) && api->screen_orientation() == APP_ORIENT_PORTRAIT) return APP_ORIENT_BAR_LEFT;
    return APP_ORIENT_BAR_BOTTOM;
}

/* Where the bar is in position o. Older firmware has only the bottom and the left (its portrait): the thirds of the
   panel's bottom strip, as touchbar_layout gives them. */
static inline void ao_layout(const app_api_t *api, int o, int panel_w, int panel_h, app_touchbar_t *out)
{
    if (ao_has_position(api) && api->touchbar_layout(o, out) == 0) return;
    int left = o == APP_ORIENT_BAR_LEFT;
    out->width = (int16_t)(left ? panel_h : panel_w);
    out->height = (int16_t)(left ? panel_w : panel_h);
    out->orientation = (uint8_t)(left ? APP_ORIENT_BAR_LEFT : APP_ORIENT_BAR_BOTTOM);
    out->edge = out->orientation;
    out->reserved = 0;
    for (int z = 0; z < 3; z++) {
        int a = panel_w * z / 3, b = panel_w * (z + 1) / 3 - a;
        out->zone[z] = left ? (app_rect_t){ 0, (int16_t)a, APP_TOUCHBAR_DEPTH, (int16_t)b }
                            : (app_rect_t){ (int16_t)a, (int16_t)(panel_h - APP_TOUCHBAR_DEPTH), (int16_t)b, APP_TOUCHBAR_DEPTH };
    }
}

/* An orientation event that changes the position: sets *o and returns 1; any other event 0 */
static inline int ao_event(const app_event_t *e, int *o)
{
    if (e->type != APP_EVENT_ORIENTATION || e->key > APP_ORIENT_BAR_RIGHT || (int)e->key == *o) return 0;
    *o = (int)e->key;
    return 1;
}
#endif
