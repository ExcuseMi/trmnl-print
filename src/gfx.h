/* A 1-bit canvas over the landscape framebuffer (MSB leftmost, 1 = white), drawn in one of the X's four positions
   (APP_ORIENT_BAR_*; app_orient_to_fb of app_api.h is the one mapping). The app's screen is SW x SH in that position;
   the canvas is a window of it, (ox, oy) its origin, W x H its size (gfx_content): a hint strip along the touch
   bar's edge stays outside, and what the window holds beyond the screen is clipped. Every write is clipped to the
   screen, the canvas and the clip rectangle. Shapes follow Pillow's ImageDraw (inclusive boxes, outlines drawn inward), so the C
   screens match the mockup renderer in tools/mock. SPDX-License-Identifier: MIT */
#ifndef GFX_H
#define GFX_H
#include <stdint.h>
#include <app_api.h>
#include "font.h"

enum { GFX_BLACK = 0, GFX_WHITE = 1, GFX_NONE = -1 };

typedef struct {
    uint8_t *fb;
    int fb_w, fb_h, stride; /* the framebuffer, always landscape */
    int orient; /* APP_ORIENT_BAR_*: where the touch bar is for the user */
    int SW, SH; /* the app's screen in this position */
    int ox, oy; /* canvas origin in the screen */
    int W, H; /* logical size */
    int cx0, cy0, cx1, cy1; /* clip, logical, inclusive */
} gfx_t;

void gfx_init(gfx_t *g, uint8_t *fb, int fb_w, int fb_h, int stride, int orient);
/* The X was turned: the same framebuffer in another position (canvas = whole screen until gfx_content) */
void gfx_set_orient(gfx_t *g, int orient);
/* The canvas for screens laid out with a hint row of `reserve` at the bottom of their H (ui.h UI_HH): the row is the
   strip along the bar's edge, `depth` deep (the screens' own row is at the bottom, so for the left and right bar
   the canvas is narrower and its H is longer by `reserve`, clipped), and stays outside the canvas. */
void gfx_content(gfx_t *g, int depth, int reserve);
/* The whole screen as the canvas (for the hint strip) */
void gfx_screen(gfx_t *g);
/* Every framebuffer pixel c, whatever the canvas */
void gfx_fill_all(gfx_t *g, int c);
void gfx_clip(gfx_t *g, int x0, int y0, int x1, int y1); /* intersected with the canvas */
void gfx_noclip(gfx_t *g);
void gfx_px(gfx_t *g, int x, int y, int c);
int gfx_get(const gfx_t *g, int x, int y); /* 1 white, 0 black or outside */
void gfx_hspan(gfx_t *g, int x0, int x1, int y, int c);
void gfx_fill(gfx_t *g, int c);
void gfx_fill_rect(gfx_t *g, float x0, float y0, float x1, float y1, int c);
/* rectangle with corner radius r (0: square), filled with `fill` and outlined `w` px inward with `outline` */
void gfx_rrect(gfx_t *g, float x0, float y0, float x1, float y1, float r, int w, int fill, int outline);
void gfx_ellipse(gfx_t *g, float x0, float y0, float x1, float y1, int w, int fill, int outline);
void gfx_poly(gfx_t *g, const float *xy, int n, int c); /* filled, even-odd */
void gfx_poly_outline(gfx_t *g, const float *xy, int n, int w, int c);
void gfx_line(gfx_t *g, float x0, float y0, float x1, float y1, int w, int c);
/* Text with the pen at (x64 / 64, baseline y); returns the advance in 1/64 px */
int32_t gfx_text(gfx_t *g, const font_face_t *f, int32_t x64, int y, const char *s, int tab, int c);
/* Ordered 8x8 Bayer dither: blackens `density` (0..1) of the pixels of the ring rin < r <= rout */
void gfx_dither_ring(gfx_t *g, float cx, float cy, float rout, float rin, float density);
#endif
