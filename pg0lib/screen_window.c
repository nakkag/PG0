/*
 * PG0 library
 *
 * screen_window.c
 *
 * The screen window: runs on its own thread, shows the screen surface scaled to
 * the window, can be switched to full screen, and collects touch/key input.
 * Top right buttons: sound on/off, full screen/restore. Closing the window stops the script.
 *
 * Repaints never hold the surface lock for long: the screen surface is copied
 * under the lock, then composited and stretched to the window outside it.
 */

/* Include Files */
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <tchar.h>
#include <math.h>

#include "screen.h"

#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")

/* Define */
#define SC_WND_CLASS			TEXT("PG0ScreenWndClass")
#define SC_WND_TITLE			TEXT("PG0")
#define SC_INI_FILE				TEXT("pg0_screen.ini")
#define SC_INI_SECTION			TEXT("SCREEN")

#define PG0_MAIN_WND_CLASS		TEXT("PG0EditMainWndClass")
#define PG0_ID_MENUITEM_STOP	40022
#define PG0_IDI_ICON_MAIN		102

#define WM_SC_SHOW				(WM_APP + 1)
#define WM_SC_QUIT				(WM_APP + 2)
#define WM_SC_PAINT				(WM_APP + 3)

#define TIMER_PAINT				1
#define MIN_PAINT_INTERVAL		5		/* ms between repaints (200 fps at most) */

#define BUTTON_SIZE				40
#define BUTTON_GAP				10
#define BUTTON_COUNT			SC_BUTTON_COUNT
#define BUTTON_SOUND			SC_BUTTON_SOUND
#define BUTTON_FULLSCREEN		SC_BUTTON_FULLSCREEN
#define BUTTON_COLOR			SC_BUTTON_COLOR
#define BUTTON_RED				SC_BUTTON_RED

#define MIN_WINDOW_WIDTH		200
#define MIN_WINDOW_HEIGHT		150

#ifndef WM_TOUCH
#define WM_TOUCH				0x0240
#define TOUCHEVENTF_MOVE		0x0001
#define TOUCHEVENTF_DOWN		0x0002
#define TOUCHEVENTF_UP			0x0004
#define TOUCHEVENTF_PRIMARY		0x0010
typedef struct _TOUCHINPUT_COMPAT {
	LONG x;
	LONG y;
	HANDLE hSource;
	DWORD dwID;
	DWORD dwFlags;
	DWORD dwMask;
	DWORD dwTime;
	ULONG_PTR dwExtraInfo;
	DWORD cxContact;
	DWORD cyContact;
} TOUCHINPUT_COMPAT;
#define TOUCHINPUT TOUCHINPUT_COMPAT
#define HTOUCHINPUT HANDLE
#endif

#ifndef WM_DPICHANGED
#define WM_DPICHANGED			0x02E0
#endif

/* Struct */
typedef struct _SC_SETTINGS {
	RECT rect;
	BOOL fullscreen;
	BOOL mute;
} SC_SETTINGS;

typedef struct _SC_VIEW {
	double scale;
	double left;
	double top;
	int width;
	int height;
} SC_VIEW;

typedef BOOL (WINAPI *REGISTERTOUCHWINDOW)(HWND, ULONG);
typedef BOOL (WINAPI *GETTOUCHINPUTINFO)(HANDLE, UINT, TOUCHINPUT *, int);
typedef BOOL (WINAPI *CLOSETOUCHINPUTHANDLE)(HANDLE);
typedef UINT (WINAPI *GETDPIFORWINDOW)(HWND);

/* Global Variables */
static HWND g_hwnd = NULL;
static HANDLE g_thread = NULL;
static DWORD g_thread_id = 0;
static HANDLE g_ready = NULL;
static BOOL g_class_registered = FALSE;
static BOOL g_fullscreen = FALSE;
static WINDOWPLACEMENT g_placement;
static SC_SETTINGS g_settings;
static BOOL g_settings_loaded = FALSE;
static SC_VIEW g_view;
static DWORD *g_snap = NULL;			/* copy of the screen surface, taken under the lock */
static HBITMAP g_comp = NULL;			/* screen composited over the background color */
static HDC g_compdc = NULL;
static DWORD *g_comp_bits = NULL;
static int g_comp_w = 0, g_comp_h = 0;
static HBRUSH g_back_brush = NULL;
static LONG g_paint_pending = 0;
static DWORD g_last_paint = 0;
static BOOL g_use_gdi = FALSE;			/* Direct2D unavailable: paint with GDI */
static int g_dpi = 96;
static BOOL g_captured = FALSE;
static BOOL g_touch_active = FALSE;
static BOOL g_placed = FALSE;
static GETTOUCHINPUTINFO pGetTouchInputInfo = NULL;
static CLOSETOUCHINPUTHANDLE pCloseTouchInputHandle = NULL;

/* Local Function Prototypes */
static LRESULT CALLBACK ScreenProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/*
 * get_ini_path
 */
static BOOL get_ini_path(TCHAR *path)
{
	if (!lib_get_app_data_dir(path, MAX_PATH + 1)) {
		return FALSE;
	}
	lstrcat(path, TEXT("\\"));
	lstrcat(path, SC_INI_FILE);
	return TRUE;
}

/*
 * sc_settings_load - window placement and sound setting
 */
void sc_settings_load(void)
{
	TCHAR path[MAX_PATH + 1];

	if (g_settings_loaded) {
		return;
	}
	g_settings_loaded = TRUE;
	ZeroMemory(&g_settings, sizeof(g_settings));
	if (!get_ini_path(path)) {
		return;
	}
	g_settings.rect.left = GetPrivateProfileInt(SC_INI_SECTION, TEXT("left"), 0, path);
	g_settings.rect.top = GetPrivateProfileInt(SC_INI_SECTION, TEXT("top"), 0, path);
	g_settings.rect.right = GetPrivateProfileInt(SC_INI_SECTION, TEXT("width"), 0, path);
	g_settings.rect.bottom = GetPrivateProfileInt(SC_INI_SECTION, TEXT("height"), 0, path);
	g_settings.fullscreen = GetPrivateProfileInt(SC_INI_SECTION, TEXT("fullscreen"), 0, path) != 0;
	g_settings.mute = GetPrivateProfileInt(SC_INI_SECTION, TEXT("mute"), 0, path) != 0;
	g_sc.mute = g_settings.mute;
}

/*
 * sc_settings_save
 */
void sc_settings_save(void)
{
	TCHAR path[MAX_PATH + 1];
	TCHAR buf[32];

	if (!g_settings_loaded || !get_ini_path(path)) {
		return;
	}
	g_settings.mute = g_sc.mute;
	wsprintf(buf, TEXT("%d"), g_settings.rect.left);
	WritePrivateProfileString(SC_INI_SECTION, TEXT("left"), buf, path);
	wsprintf(buf, TEXT("%d"), g_settings.rect.top);
	WritePrivateProfileString(SC_INI_SECTION, TEXT("top"), buf, path);
	wsprintf(buf, TEXT("%d"), g_settings.rect.right);
	WritePrivateProfileString(SC_INI_SECTION, TEXT("width"), buf, path);
	wsprintf(buf, TEXT("%d"), g_settings.rect.bottom);
	WritePrivateProfileString(SC_INI_SECTION, TEXT("height"), buf, path);
	wsprintf(buf, TEXT("%d"), g_settings.fullscreen ? 1 : 0);
	WritePrivateProfileString(SC_INI_SECTION, TEXT("fullscreen"), buf, path);
	wsprintf(buf, TEXT("%d"), g_settings.mute ? 1 : 0);
	WritePrivateProfileString(SC_INI_SECTION, TEXT("mute"), buf, path);
}

/*
 * sc_input_reset - forget touches and keys
 */
void sc_input_reset(void)
{
	EnterCriticalSection(&g_sc.input_cs);
	ZeroMemory(&g_sc.touch, sizeof(g_sc.touch));
	g_sc.key_count = 0;
	LeaveCriticalSection(&g_sc.input_cs);
}

/*
 * window_dpi
 */
static int window_dpi(HWND hWnd)
{
	GETDPIFORWINDOW pGetDpiForWindow;
	HDC hdc;
	int dpi;

	pGetDpiForWindow = (GETDPIFORWINDOW)GetProcAddress(GetModuleHandle(TEXT("user32.dll")), "GetDpiForWindow");
	if (pGetDpiForWindow != NULL) {
		dpi = (int)pGetDpiForWindow(hWnd);
		if (dpi > 0) {
			return dpi;
		}
	}
	hdc = GetDC(hWnd);
	dpi = GetDeviceCaps(hdc, LOGPIXELSX);
	ReleaseDC(hWnd, hdc);
	return (dpi > 0) ? dpi : 96;
}

/*
 * scale_dpi
 */
static int scale_dpi(int v)
{
	return MulDiv(v, g_dpi, 96);
}

/*
 * button_rect - client rectangle of a top right button
 */
static void button_rect(HWND hWnd, int index, RECT *rc)
{
	RECT client;
	int size = scale_dpi(BUTTON_SIZE);
	int gap = scale_dpi(BUTTON_GAP);

	GetClientRect(hWnd, &client);
	rc->right = client.right - (BUTTON_COUNT - 1 - index) * (size + gap);
	rc->left = rc->right - size;
	rc->top = 0;
	rc->bottom = size;
}

/*
 * button_hit - index of the button under the point (-1: none)
 */
static int button_hit(HWND hWnd, int x, int y)
{
	RECT rc;
	int i;

	for (i = 0; i < BUTTON_COUNT; i++) {
		button_rect(hWnd, i, &rc);
		if (x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom) {
			return i;
		}
	}
	return -1;
}

/*
 * update_view - where the screen surface is shown in the client area
 */
static void update_view(HWND hWnd)
{
	RECT rc;
	int cw, ch, w, h;
	BOOL fit;

	GetClientRect(hWnd, &rc);
	cw = rc.right - rc.left;
	ch = rc.bottom - rc.top;
	EnterCriticalSection(&g_sc.cs);
	w = g_sc.screen.w;
	h = g_sc.screen.h;
	fit = g_sc.fit;
	LeaveCriticalSection(&g_sc.cs);
	g_view.width = w;
	g_view.height = h;
	if (w <= 0 || h <= 0) {
		g_view.scale = 1;
		g_view.left = 0;
		g_view.top = 0;
		return;
	}
	// a minimized window has no client area: keep the last view (a scale of 0 would break the coordinates)
	if (cw <= 0 || ch <= 0) {
		return;
	}
	if (fit) {
		g_view.scale = (double)cw / w;
		if (h * g_view.scale > ch) {
			g_view.scale = (double)ch / h;
		}
	} else {
		g_view.scale = 1;
	}
	g_view.left = (cw - w * g_view.scale) / 2;
	g_view.top = (ch - h * g_view.scale) / 2;
}

/*
 * to_screen_point - client coordinates to screen surface coordinates
 */
static void to_screen_point(int x, int y, double *sx, double *sy)
{
	*sx = (x - g_view.left) / g_view.scale;
	*sy = (y - g_view.top) / g_view.scale;
}

/*
 * draw_icon_line
 */
static void draw_icon_line(GpGraphics *g, GpPen *pen, RECT *rc, double x1, double y1, double x2, double y2)
{
	double s = rc->right - rc->left;
	GdipDrawLine(g, pen, (REAL)(rc->left + x1 * s), (REAL)(rc->top + y1 * s),
		(REAL)(rc->left + x2 * s), (REAL)(rc->top + y2 * s));
}

/*
 * draw_buttons - sound, full screen/restore and close icons
 */
static void draw_buttons(HWND hWnd, GpGraphics *g)
{
	RECT rc;
	GpPen *pen = NULL, *thin = NULL, *red = NULL;
	GpSolidFill *brush = NULL;
	GpPath *path = NULL;
	GpPointF pts[6];
	double s;
	int i;

	button_rect(hWnd, BUTTON_SOUND, &rc);
	s = rc.right - rc.left;
	GdipCreatePen1(BUTTON_COLOR, (REAL)(s * 0.115), UnitWorld, &pen);
	GdipCreatePen1(BUTTON_COLOR, (REAL)(s * 0.06), UnitWorld, &thin);
	GdipCreatePen1(BUTTON_RED, (REAL)(s * 0.06), UnitWorld, &red);
	GdipCreateSolidFill(BUTTON_COLOR, &brush);
	if (pen == NULL || thin == NULL || red == NULL || brush == NULL) {
		goto end;
	}

	/* speaker */
	pts[0].X = (REAL)(rc.left + 0.08 * s); pts[0].Y = (REAL)(rc.top + 0.36 * s);
	pts[1].X = (REAL)(rc.left + 0.27 * s); pts[1].Y = (REAL)(rc.top + 0.36 * s);
	pts[2].X = (REAL)(rc.left + 0.50 * s); pts[2].Y = (REAL)(rc.top + 0.19 * s);
	pts[3].X = (REAL)(rc.left + 0.50 * s); pts[3].Y = (REAL)(rc.top + 0.81 * s);
	pts[4].X = (REAL)(rc.left + 0.27 * s); pts[4].Y = (REAL)(rc.top + 0.64 * s);
	pts[5].X = (REAL)(rc.left + 0.08 * s); pts[5].Y = (REAL)(rc.top + 0.64 * s);
	if (GdipCreatePath(FillModeWinding, &path) == GpOk) {
		GdipAddPathLine2(path, pts, 6);
		GdipFillPath(g, brush, path);
		GdipDeletePath(path);
		path = NULL;
	}
	if (g_sc.mute) {
		draw_icon_line(g, red, &rc, 0.58, 0.36, 0.90, 0.64);
		draw_icon_line(g, red, &rc, 0.90, 0.36, 0.58, 0.64);
	} else {
		for (i = 0; i < 3; i++) {
			double r = 0.15 + 0.13 * i;
			if (GdipCreatePath(FillModeWinding, &path) == GpOk) {
				GdipAddPathArc(path, (REAL)(rc.left + (0.5 - r) * s), (REAL)(rc.top + (0.5 - r) * s),
					(REAL)(2 * r * s), (REAL)(2 * r * s), -45.0f, 90.0f);
				GdipDrawPath(g, thin, path);
				GdipDeletePath(path);
				path = NULL;
			}
		}
	}

	/* full screen / restore */
	button_rect(hWnd, BUTTON_FULLSCREEN, &rc);
	if (g_fullscreen) {
		GdipDrawRectangle(g, pen, (REAL)(rc.left + 0.22 * s), (REAL)(rc.top + 0.22 * s),
			(REAL)(0.56 * s), (REAL)(0.56 * s));
	} else {
		draw_icon_line(g, pen, &rc, 0.16, 0.42, 0.16, 0.16);
		draw_icon_line(g, pen, &rc, 0.10, 0.16, 0.42, 0.16);
		draw_icon_line(g, pen, &rc, 0.58, 0.16, 0.90, 0.16);
		draw_icon_line(g, pen, &rc, 0.84, 0.10, 0.84, 0.42);
		draw_icon_line(g, pen, &rc, 0.84, 0.58, 0.84, 0.84);
		draw_icon_line(g, pen, &rc, 0.90, 0.84, 0.58, 0.84);
		draw_icon_line(g, pen, &rc, 0.42, 0.84, 0.10, 0.84);
		draw_icon_line(g, pen, &rc, 0.16, 0.90, 0.16, 0.58);
	}

end:
	if (pen != NULL) GdipDeletePen(pen);
	if (thin != NULL) GdipDeletePen(thin);
	if (red != NULL) GdipDeletePen(red);
	if (brush != NULL) GdipDeleteBrush(brush);
}

/*
 * ensure_canvas_buffers - snapshot and composite buffers of the screen size
 */
static BOOL ensure_canvas_buffers(HWND hWnd, int w, int h)
{
	HDC hdc;
	BITMAPINFO bmi;
	void *bits = NULL;

	if (g_comp != NULL && g_comp_w == w && g_comp_h == h) {
		return TRUE;
	}
	if (g_comp != NULL) {
		DeleteObject(g_comp);
		g_comp = NULL;
		g_comp_bits = NULL;
	}
	if (g_snap != NULL) {
		HeapFree(GetProcessHeap(), 0, g_snap);
		g_snap = NULL;
	}
	g_comp_w = 0;
	g_comp_h = 0;
	if (w <= 0 || h <= 0) {
		return FALSE;
	}
	if (g_compdc == NULL) {
		hdc = GetDC(hWnd);
		g_compdc = CreateCompatibleDC(hdc);
		ReleaseDC(hWnd, hdc);
		if (g_compdc == NULL) {
			return FALSE;
		}
	}
	g_snap = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)w * h * sizeof(DWORD));
	if (g_snap == NULL) {
		return FALSE;
	}
	ZeroMemory(&bmi, sizeof(bmi));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = w;
	bmi.bmiHeader.biHeight = -h;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	g_comp = CreateDIBSection(g_compdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	if (g_comp == NULL) {
		HeapFree(GetProcessHeap(), 0, g_snap);
		g_snap = NULL;
		return FALSE;
	}
	g_comp_bits = bits;
	SelectObject(g_compdc, g_comp);
	g_comp_w = w;
	g_comp_h = h;
	return TRUE;
}

/*
 * free_canvas_buffers
 */
static void free_canvas_buffers(void)
{
	if (g_compdc != NULL) {
		DeleteDC(g_compdc);
		g_compdc = NULL;
	}
	if (g_comp != NULL) {
		DeleteObject(g_comp);
		g_comp = NULL;
		g_comp_bits = NULL;
	}
	if (g_snap != NULL) {
		HeapFree(GetProcessHeap(), 0, g_snap);
		g_snap = NULL;
	}
	if (g_back_brush != NULL) {
		DeleteObject(g_back_brush);
		g_back_brush = NULL;
	}
	g_comp_w = 0;
	g_comp_h = 0;
}

/*
 * composite_canvas - snapshot (premultiplied) over the background color
 */
static void composite_canvas(ARGB bg)
{
	const DWORD *src = g_snap;
	DWORD *dst = g_comp_bits;
	DWORD ba = bg >> 24;
	DWORD br = (bg >> 16) & 0xFF, bgc = (bg >> 8) & 0xFF, bb = bg & 0xFF;
	DWORD bgp;
	SIZE_T i, n = (SIZE_T)g_comp_w * g_comp_h;

	if (ba != 255) {
		/* a translucent background shows the window color through it, as the Direct2D presenter does */
		DWORD inv = 255 - ba;
		br = (br * ba + ((SC_BACK_COLOR >> 16) & 0xFF) * inv + 127) / 255;
		bgc = (bgc * ba + ((SC_BACK_COLOR >> 8) & 0xFF) * inv + 127) / 255;
		bb = (bb * ba + (SC_BACK_COLOR & 0xFF) * inv + 127) / 255;
	}
	bgp = 0xFF000000 | (br << 16) | (bgc << 8) | bb;

	for (i = 0; i < n; i++) {
		DWORD p = src[i];
		DWORD a = p >> 24;
		if (a == 255) {
			dst[i] = p;
		} else if (a == 0) {
			dst[i] = bgp;
		} else {
			DWORD inv = 255 - a;
			dst[i] = 0xFF000000 |
				((((p >> 16) & 0xFF) + (br * inv + 127) / 255) << 16) |
				((((p >> 8) & 0xFF) + (bgc * inv + 127) / 255) << 8) |
				((p & 0xFF) + (bb * inv + 127) / 255);
		}
	}
}

/*
 * paint_window
 */
static void paint_window(HWND hWnd)
{
	PAINTSTRUCT ps;
	HDC hdc;
	RECT rc, band;
	GpGraphics *g = NULL;
	ARGB bg;
	BOOL have, changed = FALSE;
	int cw, ch, w, h, left, top, dw, dh;

	hdc = BeginPaint(hWnd, &ps);
	GetClientRect(hWnd, &rc);
	cw = rc.right - rc.left;
	ch = rc.bottom - rc.top;
	if (cw <= 0 || ch <= 0) {
		EndPaint(hWnd, &ps);
		return;
	}
	update_view(hWnd);
	if (!g_use_gdi) {
		SC_PAINT_INFO info;
		int i;
		info.cw = cw;
		info.ch = ch;
		info.w = g_view.width;
		info.h = g_view.height;
		info.left = g_view.left;
		info.top = g_view.top;
		info.scale = g_view.scale;
		info.bg = g_sc.bg_color;
		info.mute = g_sc.mute;
		info.fullscreen = g_fullscreen;
		for (i = 0; i < BUTTON_COUNT; i++) {
			button_rect(hWnd, i, &info.button[i]);
		}
		if (sc_d2d_paint(hWnd, &info)) {
			EndPaint(hWnd, &ps);
			g_last_paint = timeGetTime();
			return;
		}
		g_use_gdi = TRUE;
	}
	if (g_back_brush == NULL) {
		g_back_brush = CreateSolidBrush(RGB((SC_BACK_COLOR >> 16) & 0xFF, (SC_BACK_COLOR >> 8) & 0xFF, SC_BACK_COLOR & 0xFF));
	}

	/* GDI: take a copy of the screen while holding the lock as briefly as possible */
	EnterCriticalSection(&g_sc.screen_cs);
	w = g_sc.screen.w;
	h = g_sc.screen.h;
	bg = g_sc.bg_color;
	have = (g_sc.screen.bits != NULL) && ensure_canvas_buffers(hWnd, w, h);
	if (have && InterlockedExchange(&g_sc.dirty, 0) != 0) {
		sc_surface_flush(&g_sc.screen);
		CopyMemory(g_snap, g_sc.screen.bits, (SIZE_T)w * h * sizeof(DWORD));
		changed = TRUE;
	}
	LeaveCriticalSection(&g_sc.screen_cs);
	if (changed) {
		composite_canvas(bg);
	}

	left = (int)floor(g_view.left + 0.5);
	top = (int)floor(g_view.top + 0.5);
	dw = (int)floor(w * g_view.scale + 0.5);
	dh = (int)floor(h * g_view.scale + 0.5);
	if (!have) {
		FillRect(hdc, &rc, g_back_brush);
	} else {
		/* letterbox bands */
		if (top > 0) {
			SetRect(&band, 0, 0, cw, top);
			FillRect(hdc, &band, g_back_brush);
		}
		if (top + dh < ch) {
			SetRect(&band, 0, top + dh, cw, ch);
			FillRect(hdc, &band, g_back_brush);
		}
		if (left > 0) {
			SetRect(&band, 0, top, left, top + dh);
			FillRect(hdc, &band, g_back_brush);
		}
		if (left + dw < cw) {
			SetRect(&band, left + dw, top, cw, top + dh);
			FillRect(hdc, &band, g_back_brush);
		}
		if (dw == w && dh == h) {
			BitBlt(hdc, left, top, w, h, g_compdc, 0, 0, SRCCOPY);
		} else {
			SetStretchBltMode(hdc, HALFTONE);
			SetBrushOrgEx(hdc, 0, 0, NULL);
			StretchBlt(hdc, left, top, dw, dh, g_compdc, 0, 0, w, h, SRCCOPY);
		}
	}
	if (GdipCreateFromHDC(hdc, &g) == GpOk) {
		GdipSetPixelOffsetMode(g, PixelOffsetModeHalf);
		GdipSetSmoothingMode(g, SmoothingModeAntiAlias);
		draw_buttons(hWnd, g);
		GdipDeleteGraphics(g);
	}
	EndPaint(hWnd, &ps);
	g_last_paint = timeGetTime();
}

/*
 * do_paint - repaint now
 */
static void do_paint(HWND hWnd)
{
	InvalidateRect(hWnd, NULL, FALSE);
	UpdateWindow(hWnd);
}

/*
 * schedule_paint - repaint, keeping a minimum interval between repaints
 * (a short Sleep is used because SetTimer cannot wait less than about 10 ms)
 */
static void schedule_paint(HWND hWnd)
{
	DWORD elapsed = timeGetTime() - g_last_paint;

	if (elapsed < MIN_PAINT_INTERVAL) {
		Sleep(MIN_PAINT_INTERVAL - elapsed);
	}
	do_paint(hWnd);
}

/*
 * sc_screen_dirty - the screen changed: ask the window thread for a repaint
 */
void sc_screen_dirty(void)
{
	InterlockedExchange(&g_sc.dirty, 1);
	if (g_hwnd != NULL && InterlockedCompareExchange(&g_paint_pending, 1, 0) == 0) {
		PostMessage(g_hwnd, WM_SC_PAINT, 0, 0);
	}
}

/*
 * save_window_rect - remember the normal window position
 */
static void save_window_rect(HWND hWnd)
{
	RECT rc;

	if (g_fullscreen || IsIconic(hWnd) || IsZoomed(hWnd) || !IsWindowVisible(hWnd)) {
		return;
	}
	GetWindowRect(hWnd, &rc);
	g_settings.rect.left = rc.left;
	g_settings.rect.top = rc.top;
	g_settings.rect.right = rc.right - rc.left;
	g_settings.rect.bottom = rc.bottom - rc.top;
}

/*
 * set_fullscreen
 */
static void set_fullscreen(HWND hWnd, BOOL on)
{
	if (on == g_fullscreen) {
		return;
	}
	if (on) {
		MONITORINFO mi;
		save_window_rect(hWnd);
		g_placement.length = sizeof(g_placement);
		GetWindowPlacement(hWnd, &g_placement);
		mi.cbSize = sizeof(mi);
		GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi);
		SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) & ~WS_OVERLAPPEDWINDOW);
		g_fullscreen = TRUE;
		SetWindowPos(hWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
			mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	} else {
		SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) | WS_OVERLAPPEDWINDOW);
		g_fullscreen = FALSE;
		if (g_placement.length == sizeof(g_placement)) {
			SetWindowPlacement(hWnd, &g_placement);
		} else if (g_settings.rect.right > 0 && g_settings.rect.bottom > 0) {
			SetWindowPos(hWnd, NULL, g_settings.rect.left, g_settings.rect.top,
				g_settings.rect.right, g_settings.rect.bottom, SWP_NOZORDER | SWP_NOOWNERZORDER);
		}
		SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
	g_settings.fullscreen = g_fullscreen;
	sc_settings_save();
	InvalidateRect(hWnd, NULL, FALSE);
}

/*
 * find_main_window - the PG0 editor window of this process
 */
static BOOL CALLBACK find_main_proc(HWND hWnd, LPARAM lParam)
{
	TCHAR cls[64];
	DWORD pid = 0;

	GetWindowThreadProcessId(hWnd, &pid);
	if (pid != GetCurrentProcessId()) {
		return TRUE;
	}
	if (GetClassName(hWnd, cls, 64) > 0 && lstrcmp(cls, PG0_MAIN_WND_CLASS) == 0) {
		*(HWND *)lParam = hWnd;
		return FALSE;
	}
	return TRUE;
}

static HWND find_main_window(void)
{
	HWND hMain = NULL;
	EnumWindows(find_main_proc, (LPARAM)&hMain);
	return hMain;
}

/*
 * request_stop - closing the window: stop the running script
 */
static void request_stop(void)
{
	HWND hMain = find_main_window();

	if (hMain != NULL) {
		PostMessage(hMain, WM_COMMAND, MAKEWPARAM(PG0_ID_MENUITEM_STOP, 0), 0);
		return;
	}
	/* command line runner: no editor to stop the script, so end the program */
	ExitProcess(0);
}

/*
 * key_name - JavaScript KeyboardEvent.key of a virtual key
 */
static void key_name(UINT vk, LPARAM lParam, TCHAR *name, int size)
{
	BYTE ks[256];
	WCHAR buf[8];
	UINT scan = (lParam >> 16) & 0xFF;
	int n;

	*name = TEXT('\0');
	switch (vk) {
	case VK_LEFT: lstrcpyn(name, TEXT("ArrowLeft"), size); return;
	case VK_RIGHT: lstrcpyn(name, TEXT("ArrowRight"), size); return;
	case VK_UP: lstrcpyn(name, TEXT("ArrowUp"), size); return;
	case VK_DOWN: lstrcpyn(name, TEXT("ArrowDown"), size); return;
	case VK_RETURN: lstrcpyn(name, TEXT("Enter"), size); return;
	case VK_ESCAPE: lstrcpyn(name, TEXT("Escape"), size); return;
	case VK_SPACE: lstrcpyn(name, TEXT(" "), size); return;
	case VK_TAB: lstrcpyn(name, TEXT("Tab"), size); return;
	case VK_BACK: lstrcpyn(name, TEXT("Backspace"), size); return;
	case VK_DELETE: lstrcpyn(name, TEXT("Delete"), size); return;
	case VK_INSERT: lstrcpyn(name, TEXT("Insert"), size); return;
	case VK_HOME: lstrcpyn(name, TEXT("Home"), size); return;
	case VK_END: lstrcpyn(name, TEXT("End"), size); return;
	case VK_PRIOR: lstrcpyn(name, TEXT("PageUp"), size); return;
	case VK_NEXT: lstrcpyn(name, TEXT("PageDown"), size); return;
	case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: lstrcpyn(name, TEXT("Shift"), size); return;
	case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: lstrcpyn(name, TEXT("Control"), size); return;
	case VK_MENU: case VK_LMENU: case VK_RMENU: lstrcpyn(name, TEXT("Alt"), size); return;
	case VK_LWIN: case VK_RWIN: lstrcpyn(name, TEXT("Meta"), size); return;
	case VK_CAPITAL: lstrcpyn(name, TEXT("CapsLock"), size); return;
	case VK_NUMLOCK: lstrcpyn(name, TEXT("NumLock"), size); return;
	case VK_SCROLL: lstrcpyn(name, TEXT("ScrollLock"), size); return;
	case VK_PAUSE: lstrcpyn(name, TEXT("Pause"), size); return;
	case VK_SNAPSHOT: lstrcpyn(name, TEXT("PrintScreen"), size); return;
	case VK_APPS: lstrcpyn(name, TEXT("ContextMenu"), size); return;
	case VK_CLEAR: lstrcpyn(name, TEXT("Clear"), size); return;
	case VK_CONVERT: lstrcpyn(name, TEXT("Convert"), size); return;
	case VK_NONCONVERT: lstrcpyn(name, TEXT("NonConvert"), size); return;
	case VK_KANJI: lstrcpyn(name, TEXT("KanjiMode"), size); return;
	case VK_PROCESSKEY: lstrcpyn(name, TEXT("Process"), size); return;
	}
	if (vk >= VK_F1 && vk <= VK_F24) {
		wsprintf(name, TEXT("F%d"), vk - VK_F1 + 1);
		return;
	}
	if (!GetKeyboardState(ks)) {
		ZeroMemory(ks, sizeof(ks));
	}
	ks[VK_CONTROL] &= ~0x80;
	ks[VK_LCONTROL] &= ~0x80;
	ks[VK_RCONTROL] &= ~0x80;
	ks[VK_MENU] &= ~0x80;
	ks[VK_LMENU] &= ~0x80;
	ks[VK_RMENU] &= ~0x80;
	n = ToUnicode(vk, scan, ks, buf, 8, 4);
	if (n > 0 && buf[0] >= 0x20) {
		if (n >= size) {
			n = size - 1;
		}
		CopyMemory(name, buf, sizeof(WCHAR) * n);
		name[n] = TEXT('\0');
		return;
	}
	if (n < 0) {
		lstrcpyn(name, TEXT("Dead"), size);
		return;
	}
	lstrcpyn(name, TEXT("Unidentified"), size);
}

/*
 * key_down
 */
static void key_down(UINT vk, LPARAM lParam)
{
	TCHAR name[SC_KEY_NAME_SIZE];
	int i;

	if (lParam & (1 << 30)) {
		/* auto repeat */
		return;
	}
	key_name(vk, lParam, name, SC_KEY_NAME_SIZE);
	EnterCriticalSection(&g_sc.input_cs);
	for (i = 0; i < g_sc.key_count; i++) {
		if (g_sc.keys[i].vk == vk) {
			lstrcpy(g_sc.keys[i].name, name);
			LeaveCriticalSection(&g_sc.input_cs);
			return;
		}
	}
	if (g_sc.key_count < SC_MAX_KEYS) {
		g_sc.keys[g_sc.key_count].vk = vk;
		lstrcpy(g_sc.keys[g_sc.key_count].name, name);
		g_sc.key_count++;
	}
	LeaveCriticalSection(&g_sc.input_cs);
}

/*
 * key_up
 */
static void key_up(UINT vk)
{
	int i;

	EnterCriticalSection(&g_sc.input_cs);
	for (i = 0; i < g_sc.key_count; i++) {
		if (g_sc.keys[i].vk == vk) {
			for (; i < g_sc.key_count - 1; i++) {
				g_sc.keys[i] = g_sc.keys[i + 1];
			}
			g_sc.key_count--;
			break;
		}
	}
	LeaveCriticalSection(&g_sc.input_cs);
}

/*
 * key_clear
 */
static void key_clear(void)
{
	EnterCriticalSection(&g_sc.input_cs);
	g_sc.key_count = 0;
	LeaveCriticalSection(&g_sc.input_cs);
}

/*
 * mouse_move - pointer position in screen coordinates
 */
static void mouse_move(int x, int y)
{
	double sx, sy;

	to_screen_point(x, y, &sx, &sy);
	EnterCriticalSection(&g_sc.input_cs);
	g_sc.touch.x = sx;
	g_sc.touch.y = sy;
	g_sc.touch.count = 1;
	g_sc.touch.pos[0].x = sx;
	g_sc.touch.pos[0].y = sy;
	LeaveCriticalSection(&g_sc.input_cs);
}

/*
 * mouse_down
 */
static void mouse_down(HWND hWnd, int x, int y, int button)
{
	mouse_move(x, y);
	EnterCriticalSection(&g_sc.input_cs);
	g_sc.touch.touch = 1;
	g_sc.touch.button = button;
	LeaveCriticalSection(&g_sc.input_cs);
	if (!g_captured) {
		SetCapture(hWnd);
		g_captured = TRUE;
	}
}

/*
 * mouse_up
 */
static void mouse_up(HWND hWnd)
{
	EnterCriticalSection(&g_sc.input_cs);
	g_sc.touch.touch = 0;
	g_sc.touch.button = 0;
	LeaveCriticalSection(&g_sc.input_cs);
	if (g_captured) {
		g_captured = FALSE;
		ReleaseCapture();
	}
}

/*
 * touch_input - WM_TOUCH: all contact points
 */
static void touch_input(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	TOUCHINPUT inputs[SC_MAX_TOUCH];
	UINT count = LOWORD(wParam);
	UINT i;
	int n = 0;
	BOOL primary_found = FALSE;
	double px = 0, py = 0;

	if (pGetTouchInputInfo == NULL) {
		return;
	}
	if (count > SC_MAX_TOUCH) {
		count = SC_MAX_TOUCH;
	}
	if (!pGetTouchInputInfo((HANDLE)lParam, count, inputs, sizeof(TOUCHINPUT))) {
		return;
	}
	EnterCriticalSection(&g_sc.input_cs);
	for (i = 0; i < count; i++) {
		POINT pt;
		double sx, sy;
		pt.x = inputs[i].x / 100;
		pt.y = inputs[i].y / 100;
		ScreenToClient(hWnd, &pt);
		to_screen_point(pt.x, pt.y, &sx, &sy);
		if (inputs[i].dwFlags & TOUCHEVENTF_PRIMARY) {
			primary_found = TRUE;
			px = sx;
			py = sy;
		}
		if (inputs[i].dwFlags & TOUCHEVENTF_UP) {
			continue;
		}
		g_sc.touch.pos[n].x = sx;
		g_sc.touch.pos[n].y = sy;
		n++;
	}
	if (primary_found) {
		g_sc.touch.x = px;
		g_sc.touch.y = py;
	} else if (n > 0) {
		g_sc.touch.x = g_sc.touch.pos[0].x;
		g_sc.touch.y = g_sc.touch.pos[0].y;
	}
	if (n > 0) {
		g_sc.touch.count = n;
		g_sc.touch.touch = 1;
		g_sc.touch.button = 0;
	} else {
		g_sc.touch.touch = 0;
		g_sc.touch.button = 0;
	}
	LeaveCriticalSection(&g_sc.input_cs);
	if (pCloseTouchInputHandle != NULL) {
		pCloseTouchInputHandle((HANDLE)lParam);
	}
	g_touch_active = (n > 0);
}

/*
 * initial_window_size - fit the screen surface into the work area
 */
static void initial_window_size(HWND hWnd)
{
	RECT work, rc;
	int w, h, maxw, maxh;
	DWORD style, exstyle;
	double scale = 1;

	SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
	EnterCriticalSection(&g_sc.cs);
	w = g_sc.screen.w;
	h = g_sc.screen.h;
	LeaveCriticalSection(&g_sc.cs);
	if (w <= 0 || h <= 0) {
		w = 640;
		h = 480;
	}
	maxw = (work.right - work.left) * 8 / 10;
	maxh = (work.bottom - work.top) * 8 / 10;
	if (w > maxw) {
		scale = (double)maxw / w;
	}
	if (h * scale > maxh) {
		scale = (double)maxh / h;
	}
	rc.left = 0;
	rc.top = 0;
	rc.right = (int)(w * scale);
	rc.bottom = (int)(h * scale);
	if (rc.right < MIN_WINDOW_WIDTH) rc.right = MIN_WINDOW_WIDTH;
	if (rc.bottom < MIN_WINDOW_HEIGHT) rc.bottom = MIN_WINDOW_HEIGHT;
	style = GetWindowLong(hWnd, GWL_STYLE);
	exstyle = GetWindowLong(hWnd, GWL_EXSTYLE);
	AdjustWindowRectEx(&rc, style, FALSE, exstyle);
	w = rc.right - rc.left;
	h = rc.bottom - rc.top;
	SetWindowPos(hWnd, NULL,
		work.left + ((work.right - work.left) - w) / 2,
		work.top + ((work.bottom - work.top) - h) / 2,
		w, h, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
}

/*
 * rect_visible - the saved rectangle is on a monitor
 */
static BOOL rect_visible(const RECT *rc)
{
	RECT r;
	r.left = rc->left;
	r.top = rc->top;
	r.right = rc->left + rc->right;
	r.bottom = rc->top + rc->bottom;
	return (MonitorFromRect(&r, MONITOR_DEFAULTTONULL) != NULL);
}

/*
 * show_window - apply the saved placement and show the window
 */
static void show_window(HWND hWnd)
{
	if (!g_placed) {
		g_placed = TRUE;
		if (g_settings.rect.right >= MIN_WINDOW_WIDTH && g_settings.rect.bottom >= MIN_WINDOW_HEIGHT && rect_visible(&g_settings.rect)) {
			SetWindowPos(hWnd, NULL, g_settings.rect.left, g_settings.rect.top,
				g_settings.rect.right, g_settings.rect.bottom, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
		} else {
			initial_window_size(hWnd);
		}
		if (g_settings.fullscreen) {
			ShowWindow(hWnd, SW_SHOWNORMAL);
			set_fullscreen(hWnd, TRUE);
		}
	}
	update_view(hWnd);
	ShowWindow(hWnd, SW_SHOW);
	SetForegroundWindow(hWnd);
	SetFocus(hWnd);
	InvalidateRect(hWnd, NULL, FALSE);
}

/*
 * ScreenProc - window procedure of the screen window
 */
static LRESULT CALLBACK ScreenProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE:
		{
			HMODULE user32 = GetModuleHandle(TEXT("user32.dll"));
			REGISTERTOUCHWINDOW pRegisterTouchWindow = (REGISTERTOUCHWINDOW)GetProcAddress(user32, "RegisterTouchWindow");
			pGetTouchInputInfo = (GETTOUCHINPUTINFO)GetProcAddress(user32, "GetTouchInputInfo");
			pCloseTouchInputHandle = (CLOSETOUCHINPUTHANDLE)GetProcAddress(user32, "CloseTouchInputHandle");
			if (pRegisterTouchWindow != NULL && pGetTouchInputInfo != NULL) {
				pRegisterTouchWindow(hWnd, 0);
			}
			ImmAssociateContext(hWnd, NULL);
			g_dpi = window_dpi(hWnd);
			g_placement.length = 0;
			g_paint_pending = 0;
			g_last_paint = timeGetTime() - MIN_PAINT_INTERVAL;
		}
		break;

	case WM_SC_PAINT:
		InterlockedExchange(&g_paint_pending, 0);
		schedule_paint(hWnd);
		break;

	case WM_TIMER:
		if (wParam == TIMER_PAINT) {
			KillTimer(hWnd, TIMER_PAINT);
			do_paint(hWnd);
		}
		break;

	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT:
		paint_window(hWnd);
		break;

	case WM_SIZE:
		update_view(hWnd);
		save_window_rect(hWnd);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_MOVE:
		save_window_rect(hWnd);
		break;

	case WM_EXITSIZEMOVE:
		save_window_rect(hWnd);
		sc_settings_save();
		break;

	case WM_GETMINMAXINFO:
		((MINMAXINFO *)lParam)->ptMinTrackSize.x = scale_dpi(MIN_WINDOW_WIDTH);
		((MINMAXINFO *)lParam)->ptMinTrackSize.y = scale_dpi(MIN_WINDOW_HEIGHT);
		break;

	case WM_DPICHANGED:
		g_dpi = HIWORD(wParam);
		if (!g_fullscreen) {
			RECT *rc = (RECT *)lParam;
			SetWindowPos(hWnd, NULL, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
				SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
		}
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT) {
			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(hWnd, &pt);
			SetCursor(LoadCursor(NULL, (button_hit(hWnd, pt.x, pt.y) >= 0) ? IDC_HAND : IDC_ARROW));
			return TRUE;
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);

	case WM_LBUTTONDOWN:
		{
			int hit = button_hit(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			switch (hit) {
			case BUTTON_SOUND:
				g_sc.mute = !g_sc.mute;
				if (g_sc.mute) {
					sc_sound_stop(SOUND_GROUP_ALL);
				}
				sc_settings_save();
				InvalidateRect(hWnd, NULL, FALSE);
				break;
			case BUTTON_FULLSCREEN:
				set_fullscreen(hWnd, !g_fullscreen);
				break;
			default:
				mouse_down(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0);
				break;
			}
		}
		break;

	case WM_MBUTTONDOWN:
		mouse_down(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 1);
		break;

	case WM_RBUTTONDOWN:
		mouse_down(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 2);
		break;

	case WM_MOUSEMOVE:
		if (!g_touch_active) {
			mouse_move(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		}
		break;

	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		if (g_captured) {
			mouse_up(hWnd);
		}
		break;

	case WM_CAPTURECHANGED:
		if (g_captured) {
			g_captured = FALSE;
			EnterCriticalSection(&g_sc.input_cs);
			g_sc.touch.touch = 0;
			g_sc.touch.button = 0;
			LeaveCriticalSection(&g_sc.input_cs);
		}
		break;

	case WM_TOUCH:
		touch_input(hWnd, wParam, lParam);
		return 0;

	case WM_CONTEXTMENU:
		return 0;

	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if (wParam == VK_F11 && !(lParam & (1 << 30))) {
			set_fullscreen(hWnd, !g_fullscreen);
			return 0;
		}
		key_down((UINT)wParam, lParam);
		if (msg == WM_SYSKEYDOWN && wParam == VK_F4) {
			return DefWindowProc(hWnd, msg, wParam, lParam);
		}
		return 0;

	case WM_KEYUP:
	case WM_SYSKEYUP:
		key_up((UINT)wParam);
		return 0;

	case WM_SYSCOMMAND:
		if ((wParam & 0xFFF0) == SC_KEYMENU) {
			return 0;
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);

	case WM_KILLFOCUS:
		key_clear();
		break;

	case WM_CLOSE:
		request_stop();
		return 0;

	case WM_SC_SHOW:
		show_window(hWnd);
		break;

	case WM_SC_QUIT:
		save_window_rect(hWnd);
		sc_settings_save();
		DestroyWindow(hWnd);
		break;

	case WM_DESTROY:
		KillTimer(hWnd, TIMER_PAINT);
		free_canvas_buffers();
		sc_d2d_shutdown();
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

/*
 * register_class
 */
static BOOL register_class(void)
{
	WNDCLASS wc;

	if (g_class_registered) {
		return TRUE;
	}
	ZeroMemory(&wc, sizeof(wc));
	wc.style = 0;
	wc.lpfnWndProc = ScreenProc;
	wc.hInstance = g_sc_hinst;
	wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(PG0_IDI_ICON_MAIN));
	if (wc.hIcon == NULL) {
		wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	}
	wc.hCursor = NULL;
	wc.hbrBackground = NULL;
	wc.lpszClassName = SC_WND_CLASS;
	if (!RegisterClass(&wc)) {
		return (GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
	}
	g_class_registered = TRUE;
	return TRUE;
}

/*
 * ui_thread - message loop of the screen window
 */
static DWORD WINAPI ui_thread(LPVOID param)
{
	MSG msg;

	if (register_class()) {
		g_hwnd = CreateWindowEx(0, SC_WND_CLASS, SC_WND_TITLE, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			NULL, NULL, g_sc_hinst, NULL);
	}
	SetEvent(g_ready);
	if (g_hwnd == NULL) {
		return 0;
	}
	for (;;) {
		/* posted messages are retrieved before input, and a script that keeps drawing
		   always has a WM_SC_PAINT posted: take the key, mouse and touch input first */
		if (!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE | PM_QS_INPUT) &&
			GetMessage(&msg, NULL, 0, 0) <= 0) {
			break;
		}
		if (msg.message == WM_QUIT) {
			break;
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	g_hwnd = NULL;
	return 0;
}

/*
 * sc_window_start - create the window thread (the window stays hidden)
 */
BOOL sc_window_start(void)
{
	if (g_thread != NULL && g_hwnd != NULL) {
		return TRUE;
	}
	sc_window_stop();
	sc_settings_load();
	g_fullscreen = FALSE;
	g_captured = FALSE;
	g_touch_active = FALSE;
	g_placed = FALSE;
	/* PG0_SCREEN_GDI=1 forces the GDI presenter (no Direct2D) */
	g_use_gdi = (GetEnvironmentVariable(TEXT("PG0_SCREEN_GDI"), NULL, 0) > 0);
	g_ready = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (g_ready == NULL) {
		return FALSE;
	}
	g_thread = CreateThread(NULL, 0, ui_thread, NULL, 0, &g_thread_id);
	if (g_thread == NULL) {
		CloseHandle(g_ready);
		g_ready = NULL;
		return FALSE;
	}
	WaitForSingleObject(g_ready, INFINITE);
	CloseHandle(g_ready);
	g_ready = NULL;
	if (g_hwnd == NULL) {
		WaitForSingleObject(g_thread, INFINITE);
		CloseHandle(g_thread);
		g_thread = NULL;
		return FALSE;
	}
	return TRUE;
}

/*
 * sc_window_show - show the window with the current screen settings
 */
void sc_window_show(void)
{
	if (g_hwnd != NULL) {
		PostMessage(g_hwnd, WM_SC_SHOW, 0, 0);
	}
}

/*
 * sc_window_stop - destroy the window and end its thread
 */
void sc_window_stop(void)
{
	if (g_thread == NULL) {
		return;
	}
	if (g_hwnd != NULL) {
		PostMessage(g_hwnd, WM_SC_QUIT, 0, 0);
	} else {
		PostThreadMessage(g_thread_id, WM_QUIT, 0, 0);
	}
	if (WaitForSingleObject(g_thread, 5000) == WAIT_TIMEOUT) {
		TerminateThread(g_thread, 0);
	}
	CloseHandle(g_thread);
	g_thread = NULL;
	g_hwnd = NULL;
	g_fullscreen = FALSE;
}
/* End of source */
