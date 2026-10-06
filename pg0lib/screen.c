/*
 * PG0 library
 *
 * screen.c
 *
 * Screen drawing functions (screen.pg0), the same API as the web version's screen.js.
 * The screen is shown in a separate window (see screen_window.c).
 */

/* Include Files */
#include <windows.h>
#include <tchar.h>
#include <math.h>
#include <float.h>
#include <limits.h>

#include "screen.h"

#pragma comment(lib, "winmm.lib")

/* Define */
#define ERR_NO_SCREEN			TEXT("startScreen() has not been called")
#define DEFAULT_FONT_SIZE		30
#define DEFAULT_FONT_FACE		TEXT("sans-serif")
#define MAX_SURFACE_SIZE		16384

/* Global Variables */
SC_STATE g_sc;
HINSTANCE g_sc_hinst = NULL;
static BOOL g_cs_init = FALSE;

/* Local Function Prototypes */
void SFUNC _lib_unload(void);

/*
 * lib_attach - initialization when the library is loaded
 */
static void lib_attach(HINSTANCE hinst)
{
	g_sc_hinst = hinst;
	ZeroMemory(&g_sc, sizeof(g_sc));
	InitializeCriticalSection(&g_sc.cs);
	InitializeCriticalSection(&g_sc.screen_cs);
	InitializeCriticalSection(&g_sc.input_cs);
	g_cs_init = TRUE;
	g_sc.bg_color = 0xFFFFFFFF;
	g_sc.fit = TRUE;
	timeBeginPeriod(1);
}

/*
 * lib_detach - cleanup when the library is unloaded
 */
static void lib_detach(void)
{
	timeEndPeriod(1);
	if (g_cs_init) {
		DeleteCriticalSection(&g_sc.cs);
		DeleteCriticalSection(&g_sc.screen_cs);
		DeleteCriticalSection(&g_sc.input_cs);
		g_cs_init = FALSE;
	}
}

#ifdef PG0_STATIC_LIB
/*
 * screen_lib_init / screen_lib_term - the library built into a program (no DllMain)
 */
void screen_lib_init(void)
{
	lib_attach(GetModuleHandle(NULL));
}

void screen_lib_term(void)
{
	_lib_unload();
	lib_detach();
}
#else
/*
 * DllMain
 */
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
	switch (reason) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hinst);
		lib_attach(hinst);
		break;

	case DLL_PROCESS_DETACH:
		lib_detach();
		break;
	}
	return TRUE;
}
#endif

/*
 * free_images
 */
static void free_images(void)
{
	int i;

	for (i = 0; i < g_sc.image_count; i++) {
		if (g_sc.images[i] != NULL) {
			sc_surface_free(g_sc.images[i]);
			HeapFree(GetProcessHeap(), 0, g_sc.images[i]);
		}
	}
	if (g_sc.images != NULL) {
		HeapFree(GetProcessHeap(), 0, g_sc.images);
	}
	g_sc.images = NULL;
	g_sc.image_count = 0;
}

/*
 * free_surfaces
 */
static void free_surfaces(void)
{
	EnterCriticalSection(&g_sc.cs);
	EnterCriticalSection(&g_sc.screen_cs);
	free_images();
	sc_surface_free(&g_sc.layer);
	sc_surface_free(&g_sc.offscreen);
	sc_surface_free(&g_sc.screen);
	g_sc.offscreen_flag = FALSE;
	LeaveCriticalSection(&g_sc.screen_cs);
	LeaveCriticalSection(&g_sc.cs);
}

/*
 * _lib_unload - called by PG0 when the script ends: close the window and the sound
 */
void SFUNC _lib_unload(void)
{
	sc_sound_shutdown();
	sc_window_stop();
	sc_pool_shutdown();
	free_surfaces();
	sc_gdiplus_term();
	g_sc.started = FALSE;
}

/*
 * target_surface - the surface the drawing functions work on
 */
static SURFACE *target_surface(void)
{
	return g_sc.offscreen_flag ? &g_sc.offscreen : &g_sc.screen;
}

/*
 * clamp_int - floor of a number as an int (a cast of a huge value is undefined)
 */
static int clamp_int(double num)
{
	if (!(num > INT_MIN)) {
		return INT_MIN;
	}
	if (num > INT_MAX) {
		return INT_MAX;
	}
	return (int)floor(num);
}

/*
 * flatten_text - canvas draws every line break and tab of a text as a space
 */
static void flatten_text(TCHAR *text)
{
	for (; *text != TEXT('\0'); text++) {
		if (*text == TEXT('\n') || *text == TEXT('\r') || *text == TEXT('\t') || *text == TEXT('\f')) {
			*text = TEXT(' ');
		}
	}
}

/*
 * resolve_offscreen - perform the copy that startOffscreen() deferred (cs held)
 */
static void resolve_offscreen(void)
{
	if (g_sc.offscreen_pending) {
		g_sc.offscreen_pending = FALSE;
		EnterCriticalSection(&g_sc.screen_cs);
		g_sc.offscreen_synced = sc_surface_draw_over(&g_sc.offscreen, &g_sc.screen);
		LeaveCriticalSection(&g_sc.screen_cs);
	}
}

/*
 * draw_begin - the surface to draw on (a temporary layer in mask mode); NULL: no screen
 */
static SURFACE *draw_begin(void)
{
	SURFACE *target;

	EnterCriticalSection(&g_sc.cs);
	target = target_surface();
	if (target->bmp == NULL) {
		LeaveCriticalSection(&g_sc.cs);
		return NULL;
	}
	if (g_sc.offscreen_flag) {
		resolve_offscreen();
	}
	g_sc.offscreen_synced = FALSE;
	if (!g_sc.offscreen_flag) {
		/* drawing on the screen surface: keep repaints out until draw_end() */
		EnterCriticalSection(&g_sc.screen_cs);
	}
	if (target->mask_mode != MASK_NONE) {
		if (g_sc.layer.bmp == NULL || g_sc.layer.w != target->w || g_sc.layer.h != target->h) {
			if (!sc_surface_create(&g_sc.layer, target->w, target->h)) {
				if (!g_sc.offscreen_flag) {
					LeaveCriticalSection(&g_sc.screen_cs);
				}
				LeaveCriticalSection(&g_sc.cs);
				return NULL;
			}
		} else {
			sc_surface_clear(&g_sc.layer);
		}
		return &g_sc.layer;
	}
	return target;
}

/*
 * draw_end - composite the mask layer and mark the screen for repaint
 */
static void draw_end(void)
{
	SURFACE *target = target_surface();

	if (target->mask_mode != MASK_NONE && g_sc.layer.bmp != NULL) {
		sc_mask_composite(target, &g_sc.layer, target->mask_mode);
	}
	if (!g_sc.offscreen_flag) {
		sc_screen_dirty();
		LeaveCriticalSection(&g_sc.screen_cs);
	}
	LeaveCriticalSection(&g_sc.cs);
}

/*
 * opt_color - color option
 */
static void opt_color(VALUEINFO *opts, const TCHAR *key, ARGB *color)
{
	TCHAR *str;
	if (lib_opt_string(opts, key, &str)) {
		sc_parse_color(str, color);
	}
}

/*
 * opt_array - the parameter is an option array
 */
static VALUEINFO *opt_array(VALUEINFO *param, int index)
{
	VALUEINFO *vi = lib_param(param, index);
	if (vi != NULL && vi->v->type == TYPE_ARRAY) {
		return vi;
	}
	return NULL;
}

/*
 * _lib_func_startscreen - startScreen(width, height, option)
 */
int SFUNC _lib_func_startscreen(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *opts = NULL;
	ARGB color = 0xFFFFFFFF;
	double w = 1, h = 1, fit = 1;
	int cnt = lib_param_count(param);

	if (cnt >= 1 && param->v->type == TYPE_ARRAY) {
		/* startScreen({"width": w, "height": h, "color": c, "fit": f}) */
		opts = param;
		lib_opt_number(opts, TEXT("width"), &w);
		lib_opt_number(opts, TEXT("height"), &h);
	} else {
		if (cnt < 2) {
			return -2;
		}
		w = lib_to_float(param);
		h = lib_to_float(param->next);
		opts = opt_array(param, 2);
	}
	if (opts != NULL) {
		opt_color(opts, TEXT("color"), &color);
		lib_opt_number(opts, TEXT("fit"), &fit);
	}
	if (!(w >= 1)) w = 1;
	if (!(h >= 1)) h = 1;
	if (w > MAX_SURFACE_SIZE) w = MAX_SURFACE_SIZE;
	if (h > MAX_SURFACE_SIZE) h = MAX_SURFACE_SIZE;

	if (!sc_gdiplus_init()) {
		lstrcpy(ErrStr, TEXT("GDI+ initialization failed"));
		return -1;
	}
	sc_settings_load();
	sc_sound_stop(SOUND_GROUP_ALL);

	EnterCriticalSection(&g_sc.cs);
	EnterCriticalSection(&g_sc.screen_cs);
	free_images();
	sc_surface_free(&g_sc.layer);
	if (!sc_surface_create(&g_sc.screen, (int)w, (int)h) ||
		!sc_surface_create(&g_sc.offscreen, (int)w, (int)h)) {
		sc_surface_free(&g_sc.screen);
		sc_surface_free(&g_sc.offscreen);
		LeaveCriticalSection(&g_sc.screen_cs);
		LeaveCriticalSection(&g_sc.cs);
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	g_sc.offscreen_flag = FALSE;
	g_sc.offscreen_synced = FALSE;
	g_sc.offscreen_pending = FALSE;
	g_sc.bg_color = color;
	g_sc.fit = (fit != 0);
	sc_screen_dirty();
	LeaveCriticalSection(&g_sc.screen_cs);
	LeaveCriticalSection(&g_sc.cs);
	sc_input_reset();

	if (!sc_headless()) {
		if (!sc_window_start()) {
			lstrcpy(ErrStr, TEXT("Screen window creation failed"));
			return -1;
		}
		sc_window_show();
	}
	g_sc.started = TRUE;
	return 0;
}

/*
 * _lib_screen_size - size of the screen (FALSE before startScreen())
 * The _lib_screen_... functions are called by the program that runs the script
 * (pg0cmd), from any thread, to look at the screen and to operate it.
 */
BOOL SFUNC _lib_screen_size(int *w, int *h)
{
	BOOL ret;

	EnterCriticalSection(&g_sc.screen_cs);
	ret = (g_sc.started && g_sc.screen.bits != NULL);
	if (ret) {
		*w = g_sc.screen.w;
		*h = g_sc.screen.h;
	}
	LeaveCriticalSection(&g_sc.screen_cs);
	return ret;
}

/*
 * _lib_screen_save - save the screen as a PNG file, as the window shows it
 */
BOOL SFUNC _lib_screen_save(const TCHAR *path)
{
	DWORD *bits;
	ARGB bg;
	SIZE_T count;
	BOOL ret;
	int w, h;

	EnterCriticalSection(&g_sc.screen_cs);
	if (!g_sc.started || g_sc.screen.bits == NULL) {
		LeaveCriticalSection(&g_sc.screen_cs);
		return FALSE;
	}
	w = g_sc.screen.w;
	h = g_sc.screen.h;
	bg = g_sc.bg_color;
	count = (SIZE_T)w * h;
	bits = HeapAlloc(GetProcessHeap(), 0, count * sizeof(DWORD));
	if (bits != NULL) {
		sc_surface_flush(&g_sc.screen);
		CopyMemory(bits, g_sc.screen.bits, count * sizeof(DWORD));
	}
	LeaveCriticalSection(&g_sc.screen_cs);
	if (bits == NULL) {
		return FALSE;
	}
	sc_composite_background(bits, bits, count, bg);
	ret = sc_save_png(bits, w, h, path);
	HeapFree(GetProcessHeap(), 0, bits);
	return ret;
}

/*
 * _lib_screen_key - press or release a key (the name is the one inKey() uses)
 */
void SFUNC _lib_screen_key(const TCHAR *name, BOOL down)
{
	int i;

	if (name == NULL || *name == TEXT('\0')) {
		return;
	}
	EnterCriticalSection(&g_sc.input_cs);
	for (i = 0; i < g_sc.key_count; i++) {
		if (g_sc.keys[i].vk == SC_VK_INJECTED && str_cmp_i(g_sc.keys[i].name, name) == 0) {
			break;
		}
	}
	if (down) {
		if (i == g_sc.key_count && g_sc.key_count < SC_MAX_KEYS) {
			g_sc.keys[i].vk = SC_VK_INJECTED;
			lstrcpyn(g_sc.keys[i].name, name, SC_KEY_NAME_SIZE);
			g_sc.key_count++;
		}
	} else if (i < g_sc.key_count) {
		for (; i < g_sc.key_count - 1; i++) {
			g_sc.keys[i] = g_sc.keys[i + 1];
		}
		g_sc.key_count--;
	}
	LeaveCriticalSection(&g_sc.input_cs);
}

/*
 * _lib_screen_touch - move, press or release the pointer (in the coordinates of startScreen())
 */
void SFUNC _lib_screen_touch(int action, double x, double y)
{
	EnterCriticalSection(&g_sc.input_cs);
	g_sc.touch.x = x;
	g_sc.touch.y = y;
	g_sc.touch.count = 1;
	g_sc.touch.pos[0].x = x;
	g_sc.touch.pos[0].y = y;
	if (action == SC_TOUCH_DOWN) {
		g_sc.touch.touch = 1;
		g_sc.touch.button = 0;
	} else if (action == SC_TOUCH_UP) {
		g_sc.touch.touch = 0;
		g_sc.touch.button = 0;
	}
	LeaveCriticalSection(&g_sc.input_cs);
}

/*
 * _lib_func_sleep - sleep(ms)
 */
int SFUNC _lib_func_sleep(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	double time;

	if (param == NULL) {
		return -2;
	}
	time = floor(lib_to_float(param));
	if (_isnan(time)) {
		time = 0;
	}
	lib_sleep(ei, time);
	return 0;
}

/*
 * _lib_func_time - time(): milliseconds since 1970-01-01 (UTC)
 */
int SFUNC _lib_func_time(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	lib_set_float(ret, floor(lib_now_ms()));
	return 0;
}

/*
 * ms_to_local_time - milliseconds since 1970 to the local time
 */
static BOOL ms_to_local_time(double ms, SYSTEMTIME *local)
{
	ULARGE_INTEGER ul;
	FILETIME ft;
	SYSTEMTIME utc;

	if (_isnan(ms) || ms < -11644473600000.0) {
		ms = 0;
	}
	ul.QuadPart = (unsigned __int64)((__int64)(ms * 10000.0) + 116444736000000000LL);
	ft.dwLowDateTime = ul.LowPart;
	ft.dwHighDateTime = ul.HighPart;
	if (!FileTimeToSystemTime(&ft, &utc)) {
		return FALSE;
	}
	return SystemTimeToTzSpecificLocalTime(NULL, &utc, local);
}

/*
 * _lib_func_timestring - timeString(time, format)
 */
int SFUNC _lib_func_timestring(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	SYSTEMTIME st;
	TCHAR *format, *out, *p, *r;
	TCHAR buf[160];
	int size;

	if (param == NULL) {
		return -2;
	}
	if (!ms_to_local_time(floor(lib_to_float(param)), &st)) {
		lstrcpy(ErrStr, TEXT("Invalid time"));
		return -1;
	}
	if (param->next == NULL) {
		TCHAR date[64], time[64];
		GetDateFormat(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, date, 64);
		GetTimeFormat(LOCALE_USER_DEFAULT, 0, &st, NULL, time, 64);
		wsprintf(buf, TEXT("%s %s"), date, time);
		lib_set_string(ret, buf);
		return 0;
	}
	format = lib_to_string(param->next);
	if (format == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	size = lstrlen(format) * 4 + 16;
	out = mem_alloc(sizeof(TCHAR) * size);
	if (out == NULL) {
		mem_free(&format);
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	for (p = format, r = out; *p != TEXT('\0');) {
		if (str_cmp_n(p, TEXT("YYYY"), 4) == 0) {
			r += wsprintf(r, TEXT("%d"), st.wYear);
			p += 4;
		} else if (str_cmp_n(p, TEXT("MM"), 2) == 0) {
			r += wsprintf(r, TEXT("%02d"), st.wMonth);
			p += 2;
		} else if (str_cmp_n(p, TEXT("DD"), 2) == 0) {
			r += wsprintf(r, TEXT("%02d"), st.wDay);
			p += 2;
		} else if (str_cmp_n(p, TEXT("hh"), 2) == 0) {
			r += wsprintf(r, TEXT("%02d"), st.wHour);
			p += 2;
		} else if (str_cmp_n(p, TEXT("mm"), 2) == 0) {
			r += wsprintf(r, TEXT("%02d"), st.wMinute);
			p += 2;
		} else if (str_cmp_n(p, TEXT("ss"), 2) == 0) {
			r += wsprintf(r, TEXT("%02d"), st.wSecond);
			p += 2;
		} else if (*p == TEXT('M')) {
			r += wsprintf(r, TEXT("%d"), st.wMonth);
			p++;
		} else if (*p == TEXT('D')) {
			r += wsprintf(r, TEXT("%d"), st.wDay);
			p++;
		} else if (*p == TEXT('h')) {
			r += wsprintf(r, TEXT("%d"), st.wHour);
			p++;
		} else if (*p == TEXT('m')) {
			r += wsprintf(r, TEXT("%d"), st.wMinute);
			p++;
		} else if (*p == TEXT('s')) {
			r += wsprintf(r, TEXT("%d"), st.wSecond);
			p++;
		} else {
			*r++ = *p++;
		}
	}
	*r = TEXT('\0');
	mem_free(&format);
	ret->v->u.sValue = out;
	ret->v->type = TYPE_STRING;
	return 0;
}

/*
 * _lib_func_startoffscreen - startOffscreen()
 */
int SFUNC _lib_func_startoffscreen(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	EnterCriticalSection(&g_sc.cs);
	if (g_sc.screen.bmp == NULL) {
		LeaveCriticalSection(&g_sc.cs);
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	g_sc.offscreen_flag = TRUE;
	/* the copy of the screen into the offscreen is done on first use (resolve_offscreen);
	   nothing is copied when both already hold the same pixels or a full fill comes first */
	g_sc.offscreen_pending = !g_sc.offscreen_synced;
	LeaveCriticalSection(&g_sc.cs);
	return 0;
}

/*
 * _lib_func_endoffscreen - endOffscreen()
 */
int SFUNC _lib_func_endoffscreen(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	EnterCriticalSection(&g_sc.cs);
	if (g_sc.screen.bmp == NULL) {
		LeaveCriticalSection(&g_sc.cs);
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	if (g_sc.offscreen_flag) {
		resolve_offscreen();
	}
	g_sc.offscreen_flag = FALSE;
	if (!g_sc.offscreen_synced) {
		EnterCriticalSection(&g_sc.screen_cs);
		if (g_sc.offscreen.opaque) {
			/* the offscreen replaces the screen: swap the buffers instead of copying;
			   the mask modes stay with their canvases */
			SURFACE tmp = g_sc.screen;
			int mask = g_sc.screen.mask_mode;
			int omask = g_sc.offscreen.mask_mode;
			g_sc.screen = g_sc.offscreen;
			g_sc.offscreen = tmp;
			g_sc.screen.mask_mode = mask;
			g_sc.offscreen.mask_mode = omask;
			g_sc.offscreen_synced = FALSE;
		} else {
			g_sc.offscreen_synced = sc_surface_draw_over(&g_sc.screen, &g_sc.offscreen);
		}
		sc_screen_dirty();
		LeaveCriticalSection(&g_sc.screen_cs);
	}
	LeaveCriticalSection(&g_sc.cs);
	return 0;
}

/*
 * _lib_func_startmask - startMask(option)
 */
int SFUNC _lib_func_startmask(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *opts = opt_array(param, 0);
	SURFACE *target;
	TCHAR *dest;
	int mode = MASK_IN;

	if (opts != NULL && lib_opt_string(opts, TEXT("destination"), &dest) && str_cmp_i(dest, TEXT("out")) == 0) {
		mode = MASK_OUT;
	}
	EnterCriticalSection(&g_sc.cs);
	target = target_surface();
	if (target->bmp == NULL) {
		LeaveCriticalSection(&g_sc.cs);
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	target->mask_mode = mode;
	LeaveCriticalSection(&g_sc.cs);
	return 0;
}

/*
 * _lib_func_endmask - endMask()
 */
int SFUNC _lib_func_endmask(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	SURFACE *target;

	EnterCriticalSection(&g_sc.cs);
	target = target_surface();
	if (target->bmp == NULL) {
		LeaveCriticalSection(&g_sc.cs);
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	target->mask_mode = MASK_NONE;
	LeaveCriticalSection(&g_sc.cs);
	return 0;
}

/*
 * _lib_func_clearrect - clearRect(x, y, width, height)
 */
int SFUNC _lib_func_clearrect(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	SURFACE *target;
	double x, y, w, h;

	if (lib_param_count(param) < 4) {
		return -2;
	}
	x = lib_to_float(lib_param(param, 0));
	y = lib_to_float(lib_param(param, 1));
	w = lib_to_float(lib_param(param, 2));
	h = lib_to_float(lib_param(param, 3));
	EnterCriticalSection(&g_sc.cs);
	target = target_surface();
	if (g_sc.offscreen_flag) {
		resolve_offscreen();
	}
	g_sc.offscreen_synced = FALSE;
	if (target->bmp == NULL) {
		LeaveCriticalSection(&g_sc.cs);
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	if (!g_sc.offscreen_flag) {
		EnterCriticalSection(&g_sc.screen_cs);
	}
	sc_clear_rect(target, x, y, w, h);
	if (!g_sc.offscreen_flag) {
		sc_screen_dirty();
		LeaveCriticalSection(&g_sc.screen_cs);
	}
	LeaveCriticalSection(&g_sc.cs);
	return 0;
}

/*
 * _lib_func_drawline - drawLine(x1, y1, x2, y2, option)
 */
int SFUNC _lib_func_drawline(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	SURFACE *s;
	VALUEINFO *opts;
	ARGB color = 0xFF000000;
	double width = 1;
	double x1, y1, x2, y2;

	if (lib_param_count(param) < 4) {
		return -2;
	}
	x1 = lib_to_float(lib_param(param, 0));
	y1 = lib_to_float(lib_param(param, 1));
	x2 = lib_to_float(lib_param(param, 2));
	y2 = lib_to_float(lib_param(param, 3));
	opts = opt_array(param, 4);
	if (opts != NULL) {
		lib_opt_number(opts, TEXT("width"), &width);
		opt_color(opts, TEXT("color"), &color);
	}
	s = draw_begin();
	if (s == NULL) {
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	sc_draw_line(s, x1, y1, x2, y2, color, width);
	draw_end();
	return 0;
}

/*
 * _lib_func_drawrect - drawRect(x, y, width, height, option)
 */
int SFUNC _lib_func_drawrect(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	SURFACE *s;
	VALUEINFO *opts;
	ARGB color = 0xFF000000;
	double width = 1, fill = 0;
	double x, y, w, h;

	if (lib_param_count(param) < 4) {
		return -2;
	}
	x = lib_to_float(lib_param(param, 0));
	y = lib_to_float(lib_param(param, 1));
	w = lib_to_float(lib_param(param, 2));
	h = lib_to_float(lib_param(param, 3));
	opts = opt_array(param, 4);
	if (opts != NULL) {
		opt_color(opts, TEXT("color"), &color);
		lib_opt_number(opts, TEXT("width"), &width);
		lib_opt_number(opts, TEXT("fill"), &fill);
	}
	EnterCriticalSection(&g_sc.cs);
	if (g_sc.offscreen_flag && g_sc.offscreen_pending && fill != 0 &&
		sc_fill_covers_surface(&g_sc.offscreen, x, y, w, h, color)) {
		/* everything is overwritten: the deferred copy of the screen is not needed */
		g_sc.offscreen_pending = FALSE;
	}
	LeaveCriticalSection(&g_sc.cs);
	s = draw_begin();
	if (s == NULL) {
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	sc_draw_rect(s, x, y, w, h, color, width, fill != 0);
	draw_end();
	return 0;
}

/*
 * _lib_func_drawcircle - drawCircle(x, y, radiusX, option)
 */
int SFUNC _lib_func_drawcircle(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	SURFACE *s;
	VALUEINFO *opts;
	ARGB color = 0xFF000000;
	double width = 1, fill = 0, close = 0;
	double x, y, rx, ry, rotation = 0, start = 0, end = 2 * SC_PI;

	if (lib_param_count(param) < 3) {
		return -2;
	}
	x = lib_to_float(lib_param(param, 0));
	y = lib_to_float(lib_param(param, 1));
	rx = lib_to_float(lib_param(param, 2));
	ry = rx;
	opts = opt_array(param, 3);
	if (opts != NULL) {
		lib_opt_number(opts, TEXT("radius_y"), &ry);
		lib_opt_number(opts, TEXT("rotation"), &rotation);
		lib_opt_number(opts, TEXT("start"), &start);
		lib_opt_number(opts, TEXT("end"), &end);
		opt_color(opts, TEXT("color"), &color);
		lib_opt_number(opts, TEXT("width"), &width);
		lib_opt_number(opts, TEXT("fill"), &fill);
		lib_opt_number(opts, TEXT("close"), &close);
	}
	s = draw_begin();
	if (s == NULL) {
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	sc_draw_ellipse(s, x, y, rx, ry, rotation, start, end, color, width, fill != 0, close != 0);
	draw_end();
	return 0;
}

/*
 * _lib_func_drawpolyline - drawPolyline(coordinates, option)
 */
int SFUNC _lib_func_drawpolyline(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	SURFACE *s;
	VALUEINFO *opts, *vi;
	GpPointF *pts;
	ARGB color = 0xFF000000;
	double width = 1, fill = 0, close = 0;
	int count = 0, n;

	if (param == NULL || param->v->type != TYPE_ARRAY) {
		return -2;
	}
	opts = opt_array(param, 1);
	if (opts != NULL) {
		opt_color(opts, TEXT("color"), &color);
		lib_opt_number(opts, TEXT("width"), &width);
		lib_opt_number(opts, TEXT("fill"), &fill);
		lib_opt_number(opts, TEXT("close"), &close);
	}
	for (vi = param->v->u.array; vi != NULL; vi = vi->next) {
		if (vi->v->type == TYPE_ARRAY && lib_array_count(vi) >= 2) {
			count++;
		}
	}
	pts = mem_alloc(sizeof(GpPointF) * (count + 1));
	if (pts == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	n = 0;
	for (vi = param->v->u.array; vi != NULL; vi = vi->next) {
		if (vi->v->type == TYPE_ARRAY && lib_array_count(vi) >= 2) {
			pts[n].X = (REAL)lib_to_float(lib_find_index(vi, 0));
			pts[n].Y = (REAL)lib_to_float(lib_find_index(vi, 1));
			n++;
		}
	}
	s = draw_begin();
	if (s == NULL) {
		mem_free(&pts);
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	sc_draw_polyline(s, pts, count, color, width, fill != 0, close != 0);
	draw_end();
	mem_free(&pts);
	return 0;
}

/*
 * _lib_func_drawfill - drawFill(x, y, color)
 */
int SFUNC _lib_func_drawfill(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	SURFACE *target;
	TCHAR *str;
	ARGB color = 0xFF000000;
	int x, y;

	if (lib_param_count(param) < 3) {
		return -2;
	}
	x = lib_to_int(lib_param(param, 0));
	y = lib_to_int(lib_param(param, 1));
	str = lib_to_string(lib_param(param, 2));
	if (str != NULL) {
		sc_parse_color(str, &color);
		mem_free(&str);
	}
	color |= 0xFF000000;
	EnterCriticalSection(&g_sc.cs);
	target = target_surface();
	if (g_sc.offscreen_flag) {
		resolve_offscreen();
	}
	g_sc.offscreen_synced = FALSE;
	if (target->bmp == NULL) {
		LeaveCriticalSection(&g_sc.cs);
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	if (!g_sc.offscreen_flag) {
		EnterCriticalSection(&g_sc.screen_cs);
	}
	sc_draw_flood_fill(target, x, y, color);
	if (!g_sc.offscreen_flag) {
		sc_screen_dirty();
		LeaveCriticalSection(&g_sc.screen_cs);
	}
	LeaveCriticalSection(&g_sc.cs);
	return 0;
}

/*
 * _lib_func_drawscroll - drawScroll(x, y)
 */
int SFUNC _lib_func_drawscroll(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	SURFACE *target;
	int dx, dy;

	if (lib_param_count(param) < 2) {
		return -2;
	}
	dx = lib_to_int(lib_param(param, 0));
	dy = lib_to_int(lib_param(param, 1));
	EnterCriticalSection(&g_sc.cs);
	target = target_surface();
	if (g_sc.offscreen_flag) {
		resolve_offscreen();
	}
	g_sc.offscreen_synced = FALSE;
	if (target->bmp == NULL) {
		LeaveCriticalSection(&g_sc.cs);
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	if (!g_sc.offscreen_flag) {
		EnterCriticalSection(&g_sc.screen_cs);
	}
	sc_draw_scroll(target, dx, dy);
	if (!g_sc.offscreen_flag) {
		sc_screen_dirty();
		LeaveCriticalSection(&g_sc.screen_cs);
	}
	LeaveCriticalSection(&g_sc.cs);
	return 0;
}

/*
 * _lib_func_createimage - createImage(x, y, width, height, option)
 */
int SFUNC _lib_func_createimage(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	SURFACE *target, *img;
	VALUEINFO *opts;
	double id = -1;
	int x, y, w, h, index;

	if (lib_param_count(param) < 4) {
		return -2;
	}
	x = clamp_int(lib_to_float(lib_param(param, 0)));
	y = clamp_int(lib_to_float(lib_param(param, 1)));
	w = clamp_int(lib_to_float(lib_param(param, 2)));
	h = clamp_int(lib_to_float(lib_param(param, 3)));
	opts = opt_array(param, 4);
	if (opts != NULL) {
		lib_opt_number(opts, TEXT("id"), &id);
	}
	// a canvas of such a size cannot be created on the web either
	if (w <= 0 || h <= 0 || w > MAX_SURFACE_SIZE || h > MAX_SURFACE_SIZE) {
		lstrcpy(ErrStr, TEXT("Invalid image size"));
		return -1;
	}
	img = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SURFACE));
	if (img == NULL || !sc_surface_create(img, w, h)) {
		if (img != NULL) {
			HeapFree(GetProcessHeap(), 0, img);
		}
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	EnterCriticalSection(&g_sc.cs);
	target = target_surface();
	if (g_sc.offscreen_flag) {
		resolve_offscreen();
	}
	if (target->bmp == NULL) {
		LeaveCriticalSection(&g_sc.cs);
		sc_surface_free(img);
		HeapFree(GetProcessHeap(), 0, img);
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	sc_surface_copy_region(img, target, x, y);
	// compare as double: a huge id does not fit in an int
	if (id >= 0 && id < (double)g_sc.image_count) {
		index = (int)id;
		if (g_sc.images[index] != NULL) {
			sc_surface_free(g_sc.images[index]);
			HeapFree(GetProcessHeap(), 0, g_sc.images[index]);
		}
		g_sc.images[index] = img;
	} else {
		SURFACE **list;
		if (g_sc.images == NULL) {
			list = HeapAlloc(GetProcessHeap(), 0, sizeof(SURFACE *));
		} else {
			list = HeapReAlloc(GetProcessHeap(), 0, g_sc.images, sizeof(SURFACE *) * (g_sc.image_count + 1));
		}
		if (list == NULL) {
			LeaveCriticalSection(&g_sc.cs);
			sc_surface_free(img);
			HeapFree(GetProcessHeap(), 0, img);
			lstrcpy(ErrStr, LIB_ERR_ALLOC);
			return -1;
		}
		g_sc.images = list;
		g_sc.images[g_sc.image_count] = img;
		index = g_sc.image_count;
		g_sc.image_count++;
	}
	LeaveCriticalSection(&g_sc.cs);
	lib_set_int(ret, index);
	return 0;
}

/*
 * _lib_func_drawimage - drawImage(id, x, y, option)
 */
int SFUNC _lib_func_drawimage(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	SURFACE *s, *img;
	VALUEINFO *opts;
	double x, y, w, h, angle = 0, alpha = 1.0;
	BOOL rotate = FALSE;
	int index;

	if (lib_param_count(param) < 3) {
		return -2;
	}
	index = lib_to_int(lib_param(param, 0));
	x = lib_to_float(lib_param(param, 1));
	y = lib_to_float(lib_param(param, 2));
	opts = opt_array(param, 3);
	EnterCriticalSection(&g_sc.cs);
	if (index < 0 || index >= g_sc.image_count || g_sc.images[index] == NULL) {
		LeaveCriticalSection(&g_sc.cs);
		return 0;
	}
	img = g_sc.images[index];
	w = img->w;
	h = img->h;
	LeaveCriticalSection(&g_sc.cs);
	if (opts != NULL) {
		lib_opt_number(opts, TEXT("width"), &w);
		lib_opt_number(opts, TEXT("height"), &h);
		if (lib_opt_number(opts, TEXT("angle"), &angle)) {
			rotate = (angle != 0);
		}
		lib_opt_number(opts, TEXT("alpha"), &alpha);
	}
	s = draw_begin();
	if (s == NULL) {
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	if (index < g_sc.image_count && g_sc.images[index] != NULL) {
		sc_draw_image(s, g_sc.images[index], x, y, w, h, rotate, angle, alpha);
	}
	draw_end();
	return 0;
}

/*
 * text_options - font options shared by drawText and measureText
 */
static void text_options(VALUEINFO *opts, TCHAR **style, double *size, TCHAR **face)
{
	*style = TEXT("");
	*size = DEFAULT_FONT_SIZE;
	*face = DEFAULT_FONT_FACE;
	if (opts == NULL) {
		return;
	}
	lib_opt_string(opts, TEXT("fontstyle"), style);
	lib_opt_number(opts, TEXT("fontsize"), size);
	lib_opt_string(opts, TEXT("fontface"), face);
}

/*
 * _lib_func_drawtext - drawText(text, x, y, option)
 */
int SFUNC _lib_func_drawtext(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	SURFACE *s;
	VALUEINFO *opts;
	TCHAR *text, *style, *face;
	ARGB color = 0xFF000000;
	double x, y, width = 1, fill = 1, size;

	if (lib_param_count(param) < 3) {
		return -2;
	}
	text = lib_to_display_string(lib_param(param, 0));
	if (text == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	flatten_text(text);
	x = lib_to_float(lib_param(param, 1));
	y = lib_to_float(lib_param(param, 2));
	opts = opt_array(param, 3);
	text_options(opts, &style, &size, &face);
	if (opts != NULL) {
		opt_color(opts, TEXT("color"), &color);
		lib_opt_number(opts, TEXT("width"), &width);
		lib_opt_number(opts, TEXT("fill"), &fill);
	}
	s = draw_begin();
	if (s == NULL) {
		mem_free(&text);
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	sc_draw_text(s, text, x, y, color, width, fill != 0, style, size, face);
	draw_end();
	mem_free(&text);
	return 0;
}

/*
 * _lib_func_measuretext - measureText(text, option)
 */
int SFUNC _lib_func_measuretext(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *opts, *top = NULL, *last = NULL;
	TCHAR *text, *style, *face;
	double size, w = 0, h = 0;

	if (param == NULL) {
		return -2;
	}
	text = lib_to_display_string(param);
	if (text == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	flatten_text(text);
	opts = opt_array(param, 1);
	text_options(opts, &style, &size, &face);
	if (!sc_gdiplus_init()) {
		mem_free(&text);
		lstrcpy(ErrStr, TEXT("GDI+ initialization failed"));
		return -1;
	}
	EnterCriticalSection(&g_sc.cs);
	sc_measure_text(text, style, size, face, &w, &h);
	LeaveCriticalSection(&g_sc.cs);
	mem_free(&text);
	lib_list_append(&top, &last, lib_new_int(TEXT("width"), (int)(w + 0.5)));
	lib_list_append(&top, &last, lib_new_int(TEXT("height"), (int)(h + 0.5)));
	lib_set_array(ret, top);
	return 0;
}

/*
 * rgb_array - {"r": r, "g": g, "b": b}
 */
static void rgb_array(VALUEINFO *ret, int r, int g, int b)
{
	VALUEINFO *top = NULL, *last = NULL;

	lib_list_append(&top, &last, lib_new_int(TEXT("r"), r));
	lib_list_append(&top, &last, lib_new_int(TEXT("g"), g));
	lib_list_append(&top, &last, lib_new_int(TEXT("b"), b));
	lib_set_array(ret, top);
}

/*
 * _lib_func_rgbtopoint - rgbToPoint(x, y)
 */
int SFUNC _lib_func_rgbtopoint(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	SURFACE *target;
	DWORD p;
	int x, y;

	if (lib_param_count(param) < 2) {
		return -2;
	}
	x = (int)floor(lib_to_float(lib_param(param, 0)));
	y = (int)floor(lib_to_float(lib_param(param, 1)));
	EnterCriticalSection(&g_sc.cs);
	target = target_surface();
	if (g_sc.offscreen_flag) {
		resolve_offscreen();
	}
	if (target->bmp == NULL) {
		LeaveCriticalSection(&g_sc.cs);
		lstrcpy(ErrStr, ERR_NO_SCREEN);
		return -1;
	}
	p = sc_get_pixel(target, x, y);
	LeaveCriticalSection(&g_sc.cs);
	rgb_array(ret, (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF);
	return 0;
}

/*
 * _lib_func_rgbtohex - rgbToHex(rgb)
 */
int SFUNC _lib_func_rgbtohex(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *first;
	TCHAR buf[16];
	int r = 0, g = 0, b = 0;

	if (param == NULL || param->v->type != TYPE_ARRAY || lib_array_count(param) < 3) {
		return -2;
	}
	first = lib_find_index(param, 0);
	if (first->name == NULL || *first->name == TEXT('\0')) {
		r = lib_to_int(lib_find_index(param, 0));
		g = lib_to_int(lib_find_index(param, 1));
		b = lib_to_int(lib_find_index(param, 2));
	} else {
		double d;
		if (lib_opt_number(param, TEXT("r"), &d)) r = (int)d;
		if (lib_opt_number(param, TEXT("g"), &d)) g = (int)d;
		if (lib_opt_number(param, TEXT("b"), &d)) b = (int)d;
	}
	wsprintf(buf, TEXT("#%02x%02x%02x"), r & 0xFF, g & 0xFF, b & 0xFF);
	lib_set_string(ret, buf);
	return 0;
}

/*
 * _lib_func_hextorgb - hexToRgb(hex)
 */
int SFUNC _lib_func_hextorgb(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	ARGB color = 0;

	if (param == NULL || param->v->type != TYPE_STRING) {
		return -2;
	}
	if (!sc_parse_color(param->v->u.sValue, &color)) {
		color = 0;
	}
	rgb_array(ret, (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
	return 0;
}

/*
 * _lib_func_intouch - inTouch()
 */
int SFUNC _lib_func_intouch(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *top = NULL, *last = NULL, *ptop = NULL, *plast = NULL;
	SC_TOUCH touch;
	int i;

	EnterCriticalSection(&g_sc.input_cs);
	touch = g_sc.touch;
	LeaveCriticalSection(&g_sc.input_cs);

	lib_list_append(&top, &last, lib_new_int(TEXT("x"), (int)floor(touch.x)));
	lib_list_append(&top, &last, lib_new_int(TEXT("y"), (int)floor(touch.y)));
	lib_list_append(&top, &last, lib_new_int(TEXT("touch"), touch.touch));
	lib_list_append(&top, &last, lib_new_int(TEXT("button"), touch.button));
	for (i = 0; i < touch.count && i < SC_MAX_TOUCH; i++) {
		VALUEINFO *xy = NULL, *xylast = NULL;
		lib_list_append(&xy, &xylast, lib_new_int(TEXT("x"), (int)floor(touch.pos[i].x)));
		lib_list_append(&xy, &xylast, lib_new_int(TEXT("y"), (int)floor(touch.pos[i].y)));
		lib_list_append(&ptop, &plast, lib_new_array(NULL, xy));
	}
	lib_list_append(&top, &last, lib_new_array(TEXT("pos"), ptop));
	lib_set_array(ret, top);
	return 0;
}

/*
 * key_pressed - the key name is in the pressed list (case insensitive)
 */
static BOOL key_pressed(const TCHAR *name)
{
	int i;

	for (i = 0; i < g_sc.key_count; i++) {
		if (str_cmp_i(g_sc.keys[i].name, name) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * _lib_func_inkey - inKey(key)
 */
int SFUNC _lib_func_inkey(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *top = NULL, *last = NULL, *vi;
	TCHAR *text;
	BOOL all = TRUE;
	int i;

	EnterCriticalSection(&g_sc.input_cs);
	if (param == NULL) {
		for (i = 0; i < g_sc.key_count; i++) {
			lib_list_append(&top, &last, lib_new_string(NULL, g_sc.keys[i].name));
		}
		LeaveCriticalSection(&g_sc.input_cs);
		lib_set_array(ret, top);
		return 0;
	}
	if (param->v->type == TYPE_ARRAY) {
		for (vi = param->v->u.array; vi != NULL; vi = vi->next) {
			text = lib_to_string(vi);
			if (text == NULL || !key_pressed(text)) {
				all = FALSE;
			}
			mem_free(&text);
			if (!all) {
				break;
			}
		}
		lib_set_int(ret, all ? 1 : 0);
	} else {
		text = lib_to_string(param);
		lib_set_int(ret, (text != NULL && key_pressed(text)) ? 1 : 0);
		mem_free(&text);
	}
	LeaveCriticalSection(&g_sc.input_cs);
	return 0;
}

/*
 * note_frequency - "C4", "A#5", ... to a frequency (0 when invalid)
 */
static double note_frequency(const TCHAR *note)
{
	static const TCHAR *names[] = {
		TEXT("C"), TEXT("C#"), TEXT("D"), TEXT("D#"), TEXT("E"), TEXT("F"),
		TEXT("F#"), TEXT("G"), TEXT("G#"), TEXT("A"), TEXT("A#"), TEXT("B")
	};
	TCHAR name[3];
	const TCHAR *p = note;
	int octave = 4, index = -1, i, len = 0;

	if (p == NULL) {
		return 0;
	}
	if (!((*p >= TEXT('A') && *p <= TEXT('G')) || (*p >= TEXT('a') && *p <= TEXT('g')))) {
		return 0;
	}
	name[len++] = (TCHAR)((*p >= TEXT('a')) ? (*p - TEXT('a') + TEXT('A')) : *p);
	p++;
	if (*p == TEXT('#') || *p == TEXT('b')) {
		name[len++] = *p;
		p++;
	}
	name[len] = TEXT('\0');
	if (*p != TEXT('\0')) {
		const TCHAR *q;
		for (q = p; *q >= TEXT('0') && *q <= TEXT('9'); q++);
		if (q == p || *q != TEXT('\0')) {
			return 0;
		}
		octave = _ttoi(p);
	}
	for (i = 0; i < 12; i++) {
		if (lstrcmp(name, names[i]) == 0) {
			index = i;
			break;
		}
	}
	if (index < 0) {
		/* flats ("Db") are not in the table: same as the web version (index -1) */
		index = -1;
	}
	return 440.0 * pow(2.0, (index - 9 + (octave - 4) * 12) / 12.0);
}

/*
 * value_frequency - frequency of a note parameter (number or note name)
 */
static double value_frequency(VALUEINFO *vi)
{
	if (vi != NULL && vi->v->type == TYPE_STRING) {
		return note_frequency(vi->v->u.sValue);
	}
	return lib_to_float(vi);
}

/*
 * _lib_func_playsound - playSound(note, start, end, volume)
 */
int SFUNC _lib_func_playsound(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	SC_NOTE note;

	if (lib_param_count(param) < 3) {
		return -2;
	}
	sc_settings_load();
	if (g_sc.mute) {
		return 0;
	}
	note.freq = value_frequency(lib_param(param, 0));
	note.start = lib_to_float(lib_param(param, 1));
	note.len = lib_to_float(lib_param(param, 2));
	note.vol = 1.0;
	if (lib_param(param, 3) != NULL) {
		note.vol = lib_to_float(lib_param(param, 3));
	}
	sc_sound_play(&note, 1, FALSE, SOUND_GROUP_EFFECT);
	return 0;
}

/*
 * build_notes - note list of playMusic()/bgm() to a sequence
 */
static SC_NOTE *build_notes(VALUEINFO *list, int *count)
{
	VALUEINFO *vi, *first;
	SC_NOTE *notes;
	double start = 0, base_volume = 1.0;
	int cnt = 0, n = 0;

	for (vi = list->v->u.array; vi != NULL; vi = vi->next) {
		cnt++;
	}
	notes = HeapAlloc(GetProcessHeap(), 0, sizeof(SC_NOTE) * (cnt + 1));
	if (notes == NULL) {
		*count = 0;
		return NULL;
	}
	for (vi = list->v->u.array; vi != NULL; vi = vi->next) {
		if (vi->v->type != TYPE_ARRAY) {
			continue;
		}
		first = lib_find_index(vi, 0);
		if (first != NULL && first->name != NULL) {
			if (str_cmp_i(first->name, TEXT("start")) == 0) {
				start = lib_to_float(first);
				continue;
			} else if (str_cmp_i(first->name, TEXT("volume")) == 0) {
				base_volume = lib_to_float(first);
				continue;
			}
		}
		if (lib_array_count(vi) >= 2) {
			notes[n].freq = value_frequency(first);
			notes[n].len = lib_to_float(lib_find_index(vi, 1));
			notes[n].start = start;
			notes[n].vol = base_volume;
			if (lib_array_count(vi) >= 3) {
				notes[n].vol = lib_to_float(lib_find_index(vi, 2));
			}
			start += notes[n].len;
			n++;
		}
	}
	*count = n;
	return notes;
}

/*
 * play_notes - playMusic()/bgm() body
 */
static int play_notes(VALUEINFO *param, BOOL repeat_default, int group)
{
	VALUEINFO *opts;
	SC_NOTE *notes;
	double repeat = repeat_default ? 1 : 0;
	int count = 0;

	opts = opt_array(param, 1);
	if (opts != NULL) {
		lib_opt_number(opts, TEXT("repeat"), &repeat);
	}
	notes = build_notes(param, &count);
	if (notes == NULL) {
		return -1;
	}
	if (count > 0) {
		sc_sound_play(notes, count, repeat != 0, group);
	}
	HeapFree(GetProcessHeap(), 0, notes);
	return 0;
}

/*
 * _lib_func_playmusic - playMusic(notes, option)
 */
int SFUNC _lib_func_playmusic(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (param == NULL || param->v->type != TYPE_ARRAY) {
		return -2;
	}
	sc_settings_load();
	if (g_sc.mute) {
		return 0;
	}
	if (play_notes(param, FALSE, SOUND_GROUP_EFFECT) < 0) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	return 0;
}

/*
 * _lib_func_stopsound - stopSound()
 */
int SFUNC _lib_func_stopsound(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	sc_sound_stop(SOUND_GROUP_ALL);
	return 0;
}

/*
 * _lib_func_bgm - bgm(notes, option)
 */
int SFUNC _lib_func_bgm(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	sc_sound_stop(SOUND_GROUP_BGM);
	if (param == NULL || param->v->type != TYPE_ARRAY || lib_array_count(param) == 0) {
		return 0;
	}
	sc_settings_load();
	if (g_sc.mute) {
		return 0;
	}
	if (play_notes(param, TRUE, SOUND_GROUP_BGM) < 0) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	return 0;
}
/* End of source */
