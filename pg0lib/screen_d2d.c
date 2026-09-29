/*
 * PG0 library
 *
 * screen_d2d.c
 *
 * Direct2D presentation of the screen surface: the premultiplied BGRA surface is
 * uploaded to a GPU bitmap and drawn scaled into the window. d2d1.dll is loaded
 * at run time; when it is missing the window falls back to GDI (screen_window.c).
 * The interfaces are called through the C vtable layouts in d2d1_c.h.
 */

/* Include Files */
#include <windows.h>
#include <initguid.h>
#include "d2d1_c.h"
#include <math.h>

#include "screen.h"

/* Define */
#ifndef D2DERR_RECREATE_TARGET
#define D2DERR_RECREATE_TARGET	((HRESULT)0x8899000CL)
#endif

/* method types (the vtable entries are untyped) */
typedef HRESULT (WINAPI *D2D1CREATEFACTORY)(D2D1_FACTORY_TYPE type, REFIID riid, const D2D1_FACTORY_OPTIONS *options, void **factory);
typedef ULONG (STDMETHODCALLTYPE *PFN_Release)(void *This);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreatePathGeometry)(ID2D1Factory *This, ID2D1PathGeometry **geometry);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateHwndRenderTarget)(ID2D1Factory *This,
	const D2D1_RENDER_TARGET_PROPERTIES *props, const D2D1_HWND_RENDER_TARGET_PROPERTIES *hwndProps, ID2D1HwndRenderTarget **target);
typedef HRESULT (STDMETHODCALLTYPE *PFN_Resize)(ID2D1HwndRenderTarget *This, const D2D1_SIZE_U *size);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateBitmap)(ID2D1HwndRenderTarget *This, D2D1_SIZE_U size,
	const void *srcData, UINT32 pitch, const D2D1_BITMAP_PROPERTIES *props, ID2D1Bitmap **bitmap);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateSolidColorBrush)(ID2D1HwndRenderTarget *This,
	const D2D1_COLOR_F *color, const D2D1_BRUSH_PROPERTIES *props, ID2D1SolidColorBrush **brush);
typedef void (STDMETHODCALLTYPE *PFN_DrawLine)(ID2D1HwndRenderTarget *This, D2D1_POINT_2F p0, D2D1_POINT_2F p1,
	ID2D1Brush *brush, FLOAT width, ID2D1StrokeStyle *style);
typedef void (STDMETHODCALLTYPE *PFN_DrawRectangle)(ID2D1HwndRenderTarget *This, const D2D1_RECT_F *rect,
	ID2D1Brush *brush, FLOAT width, ID2D1StrokeStyle *style);
typedef void (STDMETHODCALLTYPE *PFN_FillRectangle)(ID2D1HwndRenderTarget *This, const D2D1_RECT_F *rect, ID2D1Brush *brush);
typedef void (STDMETHODCALLTYPE *PFN_DrawGeometry)(ID2D1HwndRenderTarget *This, ID2D1Geometry *geometry,
	ID2D1Brush *brush, FLOAT width, ID2D1StrokeStyle *style);
typedef void (STDMETHODCALLTYPE *PFN_FillGeometry)(ID2D1HwndRenderTarget *This, ID2D1Geometry *geometry,
	ID2D1Brush *brush, ID2D1Brush *opacityBrush);
typedef void (STDMETHODCALLTYPE *PFN_DrawBitmap)(ID2D1HwndRenderTarget *This, ID2D1Bitmap *bitmap,
	const D2D1_RECT_F *dest, FLOAT opacity, D2D1_BITMAP_INTERPOLATION_MODE mode, const D2D1_RECT_F *src);
typedef void (STDMETHODCALLTYPE *PFN_Clear)(ID2D1HwndRenderTarget *This, const D2D1_COLOR_F *color);
typedef void (STDMETHODCALLTYPE *PFN_BeginDraw)(ID2D1HwndRenderTarget *This);
typedef HRESULT (STDMETHODCALLTYPE *PFN_EndDraw)(ID2D1HwndRenderTarget *This, D2D1_TAG *tag1, D2D1_TAG *tag2);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CopyFromMemory)(ID2D1Bitmap *This, const D2D1_RECT_U *dest, const void *src, UINT32 pitch);
typedef HRESULT (STDMETHODCALLTYPE *PFN_Open)(ID2D1PathGeometry *This, ID2D1GeometrySink **sink);
typedef void (STDMETHODCALLTYPE *PFN_BeginFigure)(ID2D1GeometrySink *This, D2D1_POINT_2F start, D2D1_FIGURE_BEGIN begin);
typedef void (STDMETHODCALLTYPE *PFN_AddLines)(ID2D1GeometrySink *This, const D2D1_POINT_2F *points, UINT32 count);
typedef void (STDMETHODCALLTYPE *PFN_AddArc)(ID2D1GeometrySink *This, const D2D1_ARC_SEGMENT *arc);
typedef void (STDMETHODCALLTYPE *PFN_EndFigure)(ID2D1GeometrySink *This, D2D1_FIGURE_END end);
typedef HRESULT (STDMETHODCALLTYPE *PFN_Close)(ID2D1GeometrySink *This);

#define RELEASE(obj)				(((PFN_Release)((obj)->lpVtbl->Release))(obj))
#define RT(method)					((PFN_##method)(d2d_target->lpVtbl->method))
#define FACTORY(method)				((PFN_##method)(d2d_factory->lpVtbl->method))
#define SINK(sink, method)			((PFN_##method)((sink)->lpVtbl->method))

/* Global Variables */
static HMODULE d2d_module = NULL;
static ID2D1Factory *d2d_factory = NULL;
static ID2D1HwndRenderTarget *d2d_target = NULL;
static ID2D1Bitmap *d2d_bitmap = NULL;
static int d2d_bitmap_w = 0, d2d_bitmap_h = 0;
static int d2d_target_w = 0, d2d_target_h = 0;
static BOOL d2d_checked = FALSE;
static BOOL d2d_ok = FALSE;

/* Local Function Prototypes */

/*
 * color_f - ARGB to D2D color
 */
static D2D1_COLOR_F color_f(ARGB c)
{
	D2D1_COLOR_F f;
	f.a = ((c >> 24) & 0xFF) / 255.0f;
	f.r = ((c >> 16) & 0xFF) / 255.0f;
	f.g = ((c >> 8) & 0xFF) / 255.0f;
	f.b = (c & 0xFF) / 255.0f;
	return f;
}

/*
 * point_f
 */
static D2D1_POINT_2F point_f(double x, double y)
{
	D2D1_POINT_2F p;
	p.x = (FLOAT)x;
	p.y = (FLOAT)y;
	return p;
}

/*
 * sc_d2d_available - Direct2D can be used (loads d2d1.dll once)
 */
BOOL sc_d2d_available(void)
{
	D2D1CREATEFACTORY create;

	if (d2d_checked) {
		return d2d_ok;
	}
	d2d_checked = TRUE;
	d2d_module = LoadLibrary(TEXT("d2d1.dll"));
	if (d2d_module == NULL) {
		return FALSE;
	}
	create = (D2D1CREATEFACTORY)GetProcAddress(d2d_module, "D2D1CreateFactory");
	if (create == NULL) {
		return FALSE;
	}
	if (FAILED(create(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&d2d_factory))) {
		d2d_factory = NULL;
		return FALSE;
	}
	d2d_ok = TRUE;
	return TRUE;
}

/*
 * release_bitmap
 */
static void release_bitmap(void)
{
	if (d2d_bitmap != NULL) {
		RELEASE(d2d_bitmap);
		d2d_bitmap = NULL;
	}
	d2d_bitmap_w = 0;
	d2d_bitmap_h = 0;
}

/*
 * sc_d2d_release_target - drop the render target (window destroyed or device lost)
 */
void sc_d2d_release_target(void)
{
	release_bitmap();
	if (d2d_target != NULL) {
		RELEASE(d2d_target);
		d2d_target = NULL;
	}
	d2d_target_w = 0;
	d2d_target_h = 0;
}

/*
 * sc_d2d_shutdown - release everything (the window thread ends)
 */
void sc_d2d_shutdown(void)
{
	sc_d2d_release_target();
	if (d2d_factory != NULL) {
		RELEASE(d2d_factory);
		d2d_factory = NULL;
	}
	if (d2d_module != NULL) {
		FreeLibrary(d2d_module);
		d2d_module = NULL;
	}
	d2d_checked = FALSE;
	d2d_ok = FALSE;
}

/*
 * ensure_target - render target of the client size
 */
static BOOL ensure_target(HWND hWnd, int cw, int ch)
{
	D2D1_RENDER_TARGET_PROPERTIES props;
	D2D1_HWND_RENDER_TARGET_PROPERTIES hprops;
	D2D1_SIZE_U size;

	if (d2d_factory == NULL) {
		return FALSE;
	}
	size.width = (UINT32)cw;
	size.height = (UINT32)ch;
	if (d2d_target != NULL) {
		if (d2d_target_w != cw || d2d_target_h != ch) {
			if (FAILED(RT(Resize)(d2d_target, &size))) {
				sc_d2d_release_target();
				return FALSE;
			}
			d2d_target_w = cw;
			d2d_target_h = ch;
		}
		return TRUE;
	}
	ZeroMemory(&props, sizeof(props));
	props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
	props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
	props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
	props.dpiX = 96.0f;
	props.dpiY = 96.0f;
	props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
	props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
	ZeroMemory(&hprops, sizeof(hprops));
	hprops.hwnd = hWnd;
	hprops.pixelSize = size;
	hprops.presentOptions = D2D1_PRESENT_OPTIONS_IMMEDIATELY;
	if (FAILED(FACTORY(CreateHwndRenderTarget)(d2d_factory, &props, &hprops, &d2d_target))) {
		d2d_target = NULL;
		return FALSE;
	}
	d2d_target_w = cw;
	d2d_target_h = ch;
	return TRUE;
}

/*
 * ensure_bitmap - GPU bitmap of the screen size
 */
static BOOL ensure_bitmap(int w, int h)
{
	D2D1_BITMAP_PROPERTIES props;
	D2D1_SIZE_U size;

	if (d2d_bitmap != NULL && d2d_bitmap_w == w && d2d_bitmap_h == h) {
		return TRUE;
	}
	release_bitmap();
	if (w <= 0 || h <= 0) {
		return FALSE;
	}
	size.width = (UINT32)w;
	size.height = (UINT32)h;
	ZeroMemory(&props, sizeof(props));
	props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
	props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
	props.dpiX = 96.0f;
	props.dpiY = 96.0f;
	if (FAILED(RT(CreateBitmap)(d2d_target, size, NULL, 0, &props, &d2d_bitmap))) {
		d2d_bitmap = NULL;
		return FALSE;
	}
	d2d_bitmap_w = w;
	d2d_bitmap_h = h;
	return TRUE;
}

/*
 * draw_line
 */
static void draw_line(ID2D1SolidColorBrush *brush, const RECT *rc, double x1, double y1, double x2, double y2, double width)
{
	double s = rc->right - rc->left;
	RT(DrawLine)(d2d_target,
		point_f(rc->left + x1 * s, rc->top + y1 * s), point_f(rc->left + x2 * s, rc->top + y2 * s),
		(ID2D1Brush *)brush, (FLOAT)(width * s), NULL);
}

/*
 * draw_arc - arc of a circle around the center of the button, from -45 to +45 degrees
 */
static void draw_arc(ID2D1SolidColorBrush *brush, const RECT *rc, double r, double width)
{
	ID2D1PathGeometry *geom = NULL;
	ID2D1GeometrySink *sink = NULL;
	D2D1_ARC_SEGMENT arc;
	double s = rc->right - rc->left;
	double cx = rc->left + 0.5 * s, cy = rc->top + 0.5 * s;
	double k = 0.70710678;

	if (FAILED(FACTORY(CreatePathGeometry)(d2d_factory, &geom))) {
		return;
	}
	if (SUCCEEDED(SINK(geom, Open)(geom, &sink))) {
		SINK(sink, BeginFigure)(sink, point_f(cx + r * s * k, cy - r * s * k), D2D1_FIGURE_BEGIN_HOLLOW);
		arc.point = point_f(cx + r * s * k, cy + r * s * k);
		arc.size.width = (FLOAT)(r * s);
		arc.size.height = (FLOAT)(r * s);
		arc.rotationAngle = 0;
		arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
		arc.arcSize = D2D1_ARC_SIZE_SMALL;
		SINK(sink, AddArc)(sink, &arc);
		SINK(sink, EndFigure)(sink, D2D1_FIGURE_END_OPEN);
		SINK(sink, Close)(sink);
		RELEASE(sink);
		RT(DrawGeometry)(d2d_target, (ID2D1Geometry *)geom, (ID2D1Brush *)brush, (FLOAT)(width * s), NULL);
	}
	RELEASE(geom);
}

/*
 * draw_buttons - sound and full screen/restore icons
 */
static void draw_buttons(const SC_PAINT_INFO *info)
{
	ID2D1SolidColorBrush *gray = NULL, *red = NULL;
	ID2D1PathGeometry *geom = NULL;
	ID2D1GeometrySink *sink = NULL;
	D2D1_COLOR_F c;
	D2D1_POINT_2F pts[6];
	const RECT *rc;
	double s;
	int i;

	c = color_f(SC_BUTTON_COLOR);
	if (FAILED(RT(CreateSolidColorBrush)(d2d_target, &c, NULL, &gray))) {
		return;
	}
	c = color_f(SC_BUTTON_RED);
	if (FAILED(RT(CreateSolidColorBrush)(d2d_target, &c, NULL, &red))) {
		RELEASE(gray);
		return;
	}

	/* speaker */
	rc = &info->button[SC_BUTTON_SOUND];
	s = rc->right - rc->left;
	pts[0] = point_f(rc->left + 0.08 * s, rc->top + 0.36 * s);
	pts[1] = point_f(rc->left + 0.27 * s, rc->top + 0.36 * s);
	pts[2] = point_f(rc->left + 0.50 * s, rc->top + 0.19 * s);
	pts[3] = point_f(rc->left + 0.50 * s, rc->top + 0.81 * s);
	pts[4] = point_f(rc->left + 0.27 * s, rc->top + 0.64 * s);
	pts[5] = point_f(rc->left + 0.08 * s, rc->top + 0.64 * s);
	if (SUCCEEDED(FACTORY(CreatePathGeometry)(d2d_factory, &geom))) {
		if (SUCCEEDED(SINK(geom, Open)(geom, &sink))) {
			SINK(sink, BeginFigure)(sink, pts[0], D2D1_FIGURE_BEGIN_FILLED);
			SINK(sink, AddLines)(sink, pts + 1, 5);
			SINK(sink, EndFigure)(sink, D2D1_FIGURE_END_CLOSED);
			SINK(sink, Close)(sink);
			RELEASE(sink);
			RT(FillGeometry)(d2d_target, (ID2D1Geometry *)geom, (ID2D1Brush *)gray, NULL);
		}
		RELEASE(geom);
	}
	if (info->mute) {
		draw_line(red, rc, 0.58, 0.36, 0.90, 0.64, 0.06);
		draw_line(red, rc, 0.90, 0.36, 0.58, 0.64, 0.06);
	} else {
		for (i = 0; i < 3; i++) {
			draw_arc(gray, rc, 0.15 + 0.13 * i, 0.06);
		}
	}

	/* full screen / restore */
	rc = &info->button[SC_BUTTON_FULLSCREEN];
	s = rc->right - rc->left;
	if (info->fullscreen) {
		D2D1_RECT_F r;
		r.left = (FLOAT)(rc->left + 0.22 * s);
		r.top = (FLOAT)(rc->top + 0.22 * s);
		r.right = (FLOAT)(rc->left + 0.78 * s);
		r.bottom = (FLOAT)(rc->top + 0.78 * s);
		RT(DrawRectangle)(d2d_target, &r, (ID2D1Brush *)gray, (FLOAT)(0.115 * s), NULL);
	} else {
		draw_line(gray, rc, 0.16, 0.42, 0.16, 0.16, 0.115);
		draw_line(gray, rc, 0.10, 0.16, 0.42, 0.16, 0.115);
		draw_line(gray, rc, 0.58, 0.16, 0.90, 0.16, 0.115);
		draw_line(gray, rc, 0.84, 0.10, 0.84, 0.42, 0.115);
		draw_line(gray, rc, 0.84, 0.58, 0.84, 0.84, 0.115);
		draw_line(gray, rc, 0.90, 0.84, 0.58, 0.84, 0.115);
		draw_line(gray, rc, 0.42, 0.84, 0.10, 0.84, 0.115);
		draw_line(gray, rc, 0.16, 0.90, 0.16, 0.58, 0.115);
	}
	RELEASE(red);
	RELEASE(gray);
}

/*
 * sc_d2d_paint - present the screen; FALSE when Direct2D cannot be used
 */
BOOL sc_d2d_paint(HWND hWnd, const SC_PAINT_INFO *info)
{
	ID2D1SolidColorBrush *brush = NULL;
	D2D1_COLOR_F c;
	D2D1_RECT_F dest;
	HRESULT hr;
	BOOL have = FALSE, failed = FALSE;

	if (!sc_d2d_available() || !ensure_target(hWnd, info->cw, info->ch)) {
		return FALSE;
	}

	/* upload the surface while holding the lock as briefly as possible */
	EnterCriticalSection(&g_sc.screen_cs);
	if (g_sc.screen.bits != NULL && info->w == g_sc.screen.w && info->h == g_sc.screen.h) {
		BOOL fresh = (d2d_bitmap == NULL || d2d_bitmap_w != info->w || d2d_bitmap_h != info->h);
		have = ensure_bitmap(info->w, info->h);
		if (have && (fresh || InterlockedExchange(&g_sc.dirty, 0) != 0)) {
			sc_surface_flush(&g_sc.screen);
			if (FAILED(((PFN_CopyFromMemory)(d2d_bitmap->lpVtbl->CopyFromMemory))(d2d_bitmap, NULL,
					g_sc.screen.bits, (UINT32)(info->w * sizeof(DWORD))))) {
				have = FALSE;
			}
		}
		if (!have) {
			/* the GPU bitmap cannot be built or filled: leave the screen dirty for the GDI presenter */
			InterlockedExchange(&g_sc.dirty, 1);
			failed = TRUE;
		}
	}
	LeaveCriticalSection(&g_sc.screen_cs);
	if (failed) {
		sc_d2d_release_target();
		return FALSE;
	}

	RT(BeginDraw)(d2d_target);
	c = color_f(SC_BACK_COLOR);
	RT(Clear)(d2d_target, &c);
	if (have) {
		dest.left = (FLOAT)info->left;
		dest.top = (FLOAT)info->top;
		dest.right = (FLOAT)(info->left + info->w * info->scale);
		dest.bottom = (FLOAT)(info->top + info->h * info->scale);
		c = color_f(info->bg);
		if (SUCCEEDED(RT(CreateSolidColorBrush)(d2d_target, &c, NULL, &brush))) {
			RT(FillRectangle)(d2d_target, &dest, (ID2D1Brush *)brush);
			RELEASE(brush);
		}
		RT(DrawBitmap)(d2d_target, d2d_bitmap, &dest, 1.0f,
			(info->scale == 1.0) ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
	}
	draw_buttons(info);
	hr = RT(EndDraw)(d2d_target, NULL, NULL);
	if (hr == D2DERR_RECREATE_TARGET) {
		/* the device was lost: the next paint builds a new target and uploads again */
		sc_d2d_release_target();
		InterlockedExchange(&g_sc.dirty, 1);
	}
	return TRUE;
}
/* End of source */
