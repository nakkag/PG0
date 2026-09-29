/*
 * PG0 library
 *
 * screen.h
 *
 * Shared state of the screen library (screen.pg0).
 */

#ifndef PG0_SCREEN_H
#define PG0_SCREEN_H

/* Include Files */
#include <windows.h>
#include <tchar.h>

#include "gdiplus_flat.h"
#include "lib_common.h"

/* Define */
#define SC_MAX_TOUCH			16
#define SC_MAX_KEYS				64
#define SC_KEY_NAME_SIZE		32
#define SC_BACK_COLOR			0xFFF1F3F4
#define SC_PI					3.14159265358979323846

enum {
	MASK_NONE = 0,
	MASK_IN,
	MASK_OUT
};

enum {
	SOUND_GROUP_ALL = -1,
	SOUND_GROUP_EFFECT = 0,
	SOUND_GROUP_BGM = 1
};

/* Struct */
typedef struct _SURFACE {
	int w;
	int h;
	DWORD *bits;
	GpBitmap *bmp;
	GpGraphics *g;
	int mask_mode;
} SURFACE;

typedef struct _SC_POINT {
	double x;
	double y;
} SC_POINT;

typedef struct _SC_TOUCH {
	double x;
	double y;
	int touch;
	int button;
	int count;
	SC_POINT pos[SC_MAX_TOUCH];
} SC_TOUCH;

typedef struct _SC_KEY {
	UINT vk;
	TCHAR name[SC_KEY_NAME_SIZE];
} SC_KEY;

typedef struct _SC_NOTE {
	double freq;
	double start;
	double len;
	double vol;
} SC_NOTE;

typedef struct _SC_STATE {
	CRITICAL_SECTION cs;				/* surfaces, options, dirty */
	CRITICAL_SECTION input_cs;			/* touch, keys */
	BOOL started;
	SURFACE screen;
	SURFACE offscreen;
	SURFACE layer;
	SURFACE **images;
	int image_count;
	BOOL offscreen_flag;
	ARGB bg_color;
	BOOL fit;
	LONG dirty;
	SC_TOUCH touch;
	SC_KEY keys[SC_MAX_KEYS];
	int key_count;
	BOOL mute;
} SC_STATE;

/* Global Variables */
extern SC_STATE g_sc;
extern HINSTANCE g_sc_hinst;

/* Function Prototypes */
/* screen_draw.c */
BOOL sc_gdiplus_init(void);
void sc_gdiplus_term(void);
BOOL sc_surface_create(SURFACE *s, int w, int h);
void sc_surface_free(SURFACE *s);
GpGraphics *sc_surface_graphics(SURFACE *s);
void sc_surface_flush(SURFACE *s);
void sc_surface_clear(SURFACE *s);
void sc_surface_draw_over(SURFACE *dst, SURFACE *src);
void sc_surface_copy_region(SURFACE *dst, SURFACE *src, int x, int y);
void sc_mask_composite(SURFACE *dst, SURFACE *layer, int mode);
BOOL sc_parse_color(const TCHAR *str, ARGB *color);
void sc_draw_line(SURFACE *s, double x1, double y1, double x2, double y2, ARGB color, double width);
void sc_draw_rect(SURFACE *s, double x, double y, double w, double h, ARGB color, double width, BOOL fill);
void sc_draw_ellipse(SURFACE *s, double x, double y, double rx, double ry, double rotation,
	double start, double end, ARGB color, double width, BOOL fill, BOOL close);
void sc_draw_polyline(SURFACE *s, GpPointF *pts, int count, ARGB color, double width, BOOL fill, BOOL close);
void sc_draw_flood_fill(SURFACE *s, int x, int y, ARGB color);
void sc_draw_scroll(SURFACE *s, int dx, int dy);
void sc_clear_rect(SURFACE *s, double x, double y, double w, double h);
void sc_draw_image(SURFACE *s, SURFACE *img, double x, double y, double w, double h,
	BOOL rotate, double angle, double alpha);
BOOL sc_draw_text(SURFACE *s, const TCHAR *text, double x, double y, ARGB color, double width, BOOL fill,
	const TCHAR *style, double size, const TCHAR *face);
BOOL sc_measure_text(const TCHAR *text, const TCHAR *style, double size, const TCHAR *face, double *w, double *h);
DWORD sc_get_pixel(SURFACE *s, int x, int y);

/* screen_window.c */
BOOL sc_window_start(void);
void sc_window_show(void);
void sc_window_stop(void);
void sc_settings_load(void);
void sc_settings_save(void);
void sc_input_reset(void);

/* screen_sound.c */
BOOL sc_sound_play(const SC_NOTE *notes, int count, BOOL repeat, int group);
void sc_sound_stop(int group);
void sc_sound_shutdown(void);

#endif
/* End of source */
