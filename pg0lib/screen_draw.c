/*
 * PG0 library
 *
 * screen_draw.c
 *
 * Drawing on 32bit premultiplied ARGB surfaces with GDI+ (canvas 2D semantics).
 */

/* Include Files */
#include <windows.h>
#include <tchar.h>
#include <math.h>
#include <float.h>

#include "screen.h"

/* Define */
#define FONT_CACHE_SIZE			16
#define MAX_MEASURE_SIZE		8192
#define LAYOUT_SIZE				1000000.0f

/* Struct */
typedef struct _NAMED_COLOR {
	const TCHAR *name;
	DWORD rgb;
} NAMED_COLOR;

typedef struct _FONT_CACHE {
	TCHAR name[LF_FACESIZE * 4];
	GpFontFamily *family;
} FONT_CACHE;

/* Global Variables */
static ULONG_PTR gdip_token = 0;
static BOOL gdip_started = FALSE;
static GpStringFormat *text_format = NULL;
static FONT_CACHE font_cache[FONT_CACHE_SIZE];
static int font_cache_count = 0;
static GpFontFamily *generic_sans = NULL;
static GpFontFamily *generic_serif = NULL;
static GpFontFamily *generic_mono = NULL;

static const NAMED_COLOR named_colors[] = {
	{TEXT("aliceblue"), 0xF0F8FF}, {TEXT("antiquewhite"), 0xFAEBD7}, {TEXT("aqua"), 0x00FFFF},
	{TEXT("aquamarine"), 0x7FFFD4}, {TEXT("azure"), 0xF0FFFF}, {TEXT("beige"), 0xF5F5DC},
	{TEXT("bisque"), 0xFFE4C4}, {TEXT("black"), 0x000000}, {TEXT("blanchedalmond"), 0xFFEBCD},
	{TEXT("blue"), 0x0000FF}, {TEXT("blueviolet"), 0x8A2BE2}, {TEXT("brown"), 0xA52A2A},
	{TEXT("burlywood"), 0xDEB887}, {TEXT("cadetblue"), 0x5F9EA0}, {TEXT("chartreuse"), 0x7FFF00},
	{TEXT("chocolate"), 0xD2691E}, {TEXT("coral"), 0xFF7F50}, {TEXT("cornflowerblue"), 0x6495ED},
	{TEXT("cornsilk"), 0xFFF8DC}, {TEXT("crimson"), 0xDC143C}, {TEXT("cyan"), 0x00FFFF},
	{TEXT("darkblue"), 0x00008B}, {TEXT("darkcyan"), 0x008B8B}, {TEXT("darkgoldenrod"), 0xB8860B},
	{TEXT("darkgray"), 0xA9A9A9}, {TEXT("darkgreen"), 0x006400}, {TEXT("darkgrey"), 0xA9A9A9},
	{TEXT("darkkhaki"), 0xBDB76B}, {TEXT("darkmagenta"), 0x8B008B}, {TEXT("darkolivegreen"), 0x556B2F},
	{TEXT("darkorange"), 0xFF8C00}, {TEXT("darkorchid"), 0x9932CC}, {TEXT("darkred"), 0x8B0000},
	{TEXT("darksalmon"), 0xE9967A}, {TEXT("darkseagreen"), 0x8FBC8F}, {TEXT("darkslateblue"), 0x483D8B},
	{TEXT("darkslategray"), 0x2F4F4F}, {TEXT("darkslategrey"), 0x2F4F4F}, {TEXT("darkturquoise"), 0x00CED1},
	{TEXT("darkviolet"), 0x9400D3}, {TEXT("deeppink"), 0xFF1493}, {TEXT("deepskyblue"), 0x00BFFF},
	{TEXT("dimgray"), 0x696969}, {TEXT("dimgrey"), 0x696969}, {TEXT("dodgerblue"), 0x1E90FF},
	{TEXT("firebrick"), 0xB22222}, {TEXT("floralwhite"), 0xFFFAF0}, {TEXT("forestgreen"), 0x228B22},
	{TEXT("fuchsia"), 0xFF00FF}, {TEXT("gainsboro"), 0xDCDCDC}, {TEXT("ghostwhite"), 0xF8F8FF},
	{TEXT("gold"), 0xFFD700}, {TEXT("goldenrod"), 0xDAA520}, {TEXT("gray"), 0x808080},
	{TEXT("green"), 0x008000}, {TEXT("greenyellow"), 0xADFF2F}, {TEXT("grey"), 0x808080},
	{TEXT("honeydew"), 0xF0FFF0}, {TEXT("hotpink"), 0xFF69B4}, {TEXT("indianred"), 0xCD5C5C},
	{TEXT("indigo"), 0x4B0082}, {TEXT("ivory"), 0xFFFFF0}, {TEXT("khaki"), 0xF0E68C},
	{TEXT("lavender"), 0xE6E6FA}, {TEXT("lavenderblush"), 0xFFF0F5}, {TEXT("lawngreen"), 0x7CFC00},
	{TEXT("lemonchiffon"), 0xFFFACD}, {TEXT("lightblue"), 0xADD8E6}, {TEXT("lightcoral"), 0xF08080},
	{TEXT("lightcyan"), 0xE0FFFF}, {TEXT("lightgoldenrodyellow"), 0xFAFAD2}, {TEXT("lightgray"), 0xD3D3D3},
	{TEXT("lightgreen"), 0x90EE90}, {TEXT("lightgrey"), 0xD3D3D3}, {TEXT("lightpink"), 0xFFB6C1},
	{TEXT("lightsalmon"), 0xFFA07A}, {TEXT("lightseagreen"), 0x20B2AA}, {TEXT("lightskyblue"), 0x87CEFA},
	{TEXT("lightslategray"), 0x778899}, {TEXT("lightslategrey"), 0x778899}, {TEXT("lightsteelblue"), 0xB0C4DE},
	{TEXT("lightyellow"), 0xFFFFE0}, {TEXT("lime"), 0x00FF00}, {TEXT("limegreen"), 0x32CD32},
	{TEXT("linen"), 0xFAF0E6}, {TEXT("magenta"), 0xFF00FF}, {TEXT("maroon"), 0x800000},
	{TEXT("mediumaquamarine"), 0x66CDAA}, {TEXT("mediumblue"), 0x0000CD}, {TEXT("mediumorchid"), 0xBA55D3},
	{TEXT("mediumpurple"), 0x9370DB}, {TEXT("mediumseagreen"), 0x3CB371}, {TEXT("mediumslateblue"), 0x7B68EE},
	{TEXT("mediumspringgreen"), 0x00FA9A}, {TEXT("mediumturquoise"), 0x48D1CC}, {TEXT("mediumvioletred"), 0xC71585},
	{TEXT("midnightblue"), 0x191970}, {TEXT("mintcream"), 0xF5FFFA}, {TEXT("mistyrose"), 0xFFE4E1},
	{TEXT("moccasin"), 0xFFE4B5}, {TEXT("navajowhite"), 0xFFDEAD}, {TEXT("navy"), 0x000080},
	{TEXT("oldlace"), 0xFDF5E6}, {TEXT("olive"), 0x808000}, {TEXT("olivedrab"), 0x6B8E23},
	{TEXT("orange"), 0xFFA500}, {TEXT("orangered"), 0xFF4500}, {TEXT("orchid"), 0xDA70D6},
	{TEXT("palegoldenrod"), 0xEEE8AA}, {TEXT("palegreen"), 0x98FB98}, {TEXT("paleturquoise"), 0xAFEEEE},
	{TEXT("palevioletred"), 0xDB7093}, {TEXT("papayawhip"), 0xFFEFD5}, {TEXT("peachpuff"), 0xFFDAB9},
	{TEXT("peru"), 0xCD853F}, {TEXT("pink"), 0xFFC0CB}, {TEXT("plum"), 0xDDA0DD},
	{TEXT("powderblue"), 0xB0E0E6}, {TEXT("purple"), 0x800080}, {TEXT("rebeccapurple"), 0x663399},
	{TEXT("red"), 0xFF0000}, {TEXT("rosybrown"), 0xBC8F8F}, {TEXT("royalblue"), 0x4169E1},
	{TEXT("saddlebrown"), 0x8B4513}, {TEXT("salmon"), 0xFA8072}, {TEXT("sandybrown"), 0xF4A460},
	{TEXT("seagreen"), 0x2E8B57}, {TEXT("seashell"), 0xFFF5EE}, {TEXT("sienna"), 0xA0522D},
	{TEXT("silver"), 0xC0C0C0}, {TEXT("skyblue"), 0x87CEEB}, {TEXT("slateblue"), 0x6A5ACD},
	{TEXT("slategray"), 0x708090}, {TEXT("slategrey"), 0x708090}, {TEXT("snow"), 0xFFFAFA},
	{TEXT("springgreen"), 0x00FF7F}, {TEXT("steelblue"), 0x4682B4}, {TEXT("tan"), 0xD2B48C},
	{TEXT("teal"), 0x008080}, {TEXT("thistle"), 0xD8BFD8}, {TEXT("tomato"), 0xFF6347},
	{TEXT("turquoise"), 0x40E0D0}, {TEXT("violet"), 0xEE82EE}, {TEXT("wheat"), 0xF5DEB3},
	{TEXT("white"), 0xFFFFFF}, {TEXT("whitesmoke"), 0xF5F5F5}, {TEXT("yellow"), 0xFFFF00},
	{TEXT("yellowgreen"), 0x9ACD32},
};

/* Local Function Prototypes */

/*
 * sc_gdiplus_init - start GDI+
 */
BOOL sc_gdiplus_init(void)
{
	GdiplusStartupInput input;
	GpStringFormat *generic;

	if (gdip_started) {
		return TRUE;
	}
	ZeroMemory(&input, sizeof(input));
	input.GdiplusVersion = 1;
	if (GdiplusStartup(&gdip_token, &input, NULL) != GpOk) {
		return FALSE;
	}
	gdip_started = TRUE;
	if (GdipStringFormatGetGenericTypographic(&generic) == GpOk) {
		INT flags = 0;
		GdipCloneStringFormat(generic, &text_format);
		if (text_format != NULL) {
			GdipGetStringFormatFlags(text_format, &flags);
			flags |= StringFormatFlagsNoWrap | StringFormatFlagsNoClip | StringFormatFlagsMeasureTrailingSpaces;
			GdipSetStringFormatFlags(text_format, flags);
			GdipSetStringFormatTrimming(text_format, StringTrimmingNone);
		}
	}
	return TRUE;
}

/*
 * sc_gdiplus_term - stop GDI+ (all surfaces must be freed before)
 */
void sc_gdiplus_term(void)
{
	int i;

	if (!gdip_started) {
		return;
	}
	for (i = 0; i < font_cache_count; i++) {
		if (font_cache[i].family != NULL) {
			GdipDeleteFontFamily(font_cache[i].family);
		}
	}
	font_cache_count = 0;
	generic_sans = NULL;
	generic_serif = NULL;
	generic_mono = NULL;
	if (text_format != NULL) {
		GdipDeleteStringFormat(text_format);
		text_format = NULL;
	}
	GdiplusShutdown(gdip_token);
	gdip_started = FALSE;
}

/*
 * sc_surface_create - allocate a transparent surface
 */
BOOL sc_surface_create(SURFACE *s, int w, int h)
{
	sc_surface_free(s);
	if (w <= 0 || h <= 0) {
		return FALSE;
	}
	s->bits = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)w * h * sizeof(DWORD));
	if (s->bits == NULL) {
		return FALSE;
	}
	if (GdipCreateBitmapFromScan0(w, h, w * sizeof(DWORD), PixelFormat32bppPARGB, (BYTE *)s->bits, &s->bmp) != GpOk) {
		HeapFree(GetProcessHeap(), 0, s->bits);
		s->bits = NULL;
		s->bmp = NULL;
		return FALSE;
	}
	s->w = w;
	s->h = h;
	s->g = NULL;
	s->mask_mode = MASK_NONE;
	s->opaque = FALSE;
	return TRUE;
}

/*
 * sc_surface_free
 */
void sc_surface_free(SURFACE *s)
{
	if (s->g != NULL) {
		GdipDeleteGraphics(s->g);
		s->g = NULL;
	}
	if (s->bmp != NULL) {
		GdipDisposeImage(s->bmp);
		s->bmp = NULL;
	}
	if (s->bits != NULL) {
		HeapFree(GetProcessHeap(), 0, s->bits);
		s->bits = NULL;
	}
	s->w = 0;
	s->h = 0;
	s->mask_mode = MASK_NONE;
}

/*
 * sc_surface_graphics - the drawing context of a surface (canvas like settings)
 */
GpGraphics *sc_surface_graphics(SURFACE *s)
{
	if (s->bmp == NULL) {
		return NULL;
	}
	if (s->g == NULL) {
		if (GdipGetImageGraphicsContext(s->bmp, &s->g) != GpOk) {
			s->g = NULL;
			return NULL;
		}
		GdipSetSmoothingMode(s->g, SmoothingModeAntiAlias);
		GdipSetPixelOffsetMode(s->g, PixelOffsetModeHalf);
		GdipSetInterpolationMode(s->g, InterpolationModeHighQualityBilinear);
		GdipSetCompositingMode(s->g, CompositingModeSourceOver);
		/* canvas blends in sRGB space without gamma correction */
		GdipSetCompositingQuality(s->g, CompositingQualityAssumeLinear);
		GdipSetTextRenderingHint(s->g, TextRenderingHintAntiAlias);
	}
	return s->g;
}

/*
 * sc_surface_flush - finish pending drawing before touching the bits
 */
void sc_surface_flush(SURFACE *s)
{
	if (s->g != NULL) {
		GdipFlush(s->g, FlushIntentionSync);
	}
}

/*
 * sc_surface_clear - make the surface transparent
 */
void sc_surface_clear(SURFACE *s)
{
	if (s->bits == NULL) {
		return;
	}
	sc_surface_flush(s);
	ZeroMemory(s->bits, (SIZE_T)s->w * s->h * sizeof(DWORD));
	s->opaque = FALSE;
}

/*
 * sc_surface_draw_over - draw a surface over another one of the same size (source-over)
 * returns TRUE when the source was fully opaque, i.e. dst now equals src
 */
BOOL sc_surface_draw_over(SURFACE *dst, SURFACE *src)
{
	const DWORD *s;
	DWORD *d;
	SIZE_T i, n;
	BOOL all_opaque = TRUE;

	if (dst->bits == NULL || src->bits == NULL || dst->w != src->w || dst->h != src->h) {
		return FALSE;
	}
	sc_surface_flush(src);
	sc_surface_flush(dst);
	if (src->opaque) {
		CopyMemory(dst->bits, src->bits, (SIZE_T)src->w * src->h * sizeof(DWORD));
		dst->opaque = TRUE;
		return TRUE;
	}
	s = src->bits;
	d = dst->bits;
	n = (SIZE_T)src->w * src->h;
	for (i = 0; i < n; i++) {
		DWORD p = s[i];
		DWORD a = p >> 24;
		if (a == 255) {
			d[i] = p;
		} else {
			DWORD q, inv;
			all_opaque = FALSE;
			if (a == 0) {
				continue;
			}
			q = d[i];
			inv = 255 - a;
			d[i] = ((((q >> 24) & 0xFF) * inv / 255 + a) << 24) |
				((((q >> 16) & 0xFF) * inv / 255 + ((p >> 16) & 0xFF)) << 16) |
				((((q >> 8) & 0xFF) * inv / 255 + ((p >> 8) & 0xFF)) << 8) |
				((q & 0xFF) * inv / 255 + (p & 0xFF));
		}
	}
	if (all_opaque) {
		dst->opaque = TRUE;
	}
	return all_opaque;
}

/*
 * blend_pixel - source-over of a straight color with the given alpha onto a premultiplied pixel
 */
static __inline void blend_pixel(DWORD *p, DWORD r, DWORD g, DWORD b, DWORD alpha)
{
	DWORD q = *p, inv = 255 - alpha;
	*p = ((((q >> 24) & 0xFF) * inv / 255 + alpha) << 24) |
		((((q >> 16) & 0xFF) * inv / 255 + r * alpha / 255) << 16) |
		((((q >> 8) & 0xFF) * inv / 255 + g * alpha / 255) << 8) |
		((q & 0xFF) * inv / 255 + b * alpha / 255);
}

/*
 * fill_circle_fast - anti-aliased filled circle written straight into the bits
 */
static BOOL fill_circle_fast(SURFACE *s, double cx, double cy, double r, ARGB color)
{
	DWORD a = color >> 24, cr = (color >> 16) & 0xFF, cg = (color >> 8) & 0xFF, cb = color & 0xFF;
	DWORD premul;
	int y, y0, y1;

	if (s->bits == NULL || !(r >= 0) || r > 1e6 || fabs(cx) > 1e8 || fabs(cy) > 1e8 || a == 0) {
		return FALSE;
	}
	premul = (a << 24) | ((cr * a / 255) << 16) | ((cg * a / 255) << 8) | (cb * a / 255);
	sc_surface_flush(s);
	y0 = (int)floor(cy - r - 1);
	y1 = (int)ceil(cy + r + 1);
	if (y0 < 0) y0 = 0;
	if (y1 > s->h) y1 = s->h;
	for (y = y0; y < y1; y++) {
		double dy = (y + 0.5) - cy;
		double ro2 = (r + 0.5) * (r + 0.5) - dy * dy;
		double ri2 = (r - 0.5) * (r - 0.5) - dy * dy;
		double ro, ri;
		int x, xs, xe, is, ie;
		DWORD *row;
		if (ro2 <= 0) {
			continue;
		}
		ro = sqrt(ro2);
		xs = (int)floor(cx - ro - 0.5);
		xe = (int)ceil(cx + ro + 0.5);
		if (xs < 0) xs = 0;
		if (xe > s->w) xe = s->w;
		if (xs >= xe) {
			continue;
		}
		if (r >= 0.5 && ri2 > 0) {
			ri = sqrt(ri2);
			is = (int)ceil(cx - ri - 0.5);
			ie = (int)floor(cx + ri - 0.5) + 1;
			if (is < xs) is = xs;
			if (ie > xe) ie = xe;
		} else {
			is = ie = xs;
		}
		row = s->bits + (SIZE_T)y * s->w;
		for (x = xs; x < xe; x++) {
			if (x >= is && x < ie) {
				if (a == 255) {
					row[x] = premul;
				} else {
					blend_pixel(row + x, cr, cg, cb, a);
				}
				continue;
			}
			{
				double dx = (x + 0.5) - cx;
				double cov = r - sqrt(dx * dx + dy * dy) + 0.5;
				DWORD alpha;
				if (cov <= 0) {
					continue;
				}
				if (cov > 1) {
					cov = 1;
				}
				alpha = (DWORD)(a * cov + 0.5);
				if (alpha == 0) {
					continue;
				}
				if (alpha == 255) {
					row[x] = premul;
				} else {
					blend_pixel(row + x, cr, cg, cb, alpha);
				}
			}
		}
	}
	return TRUE;
}

/*
 * sc_surface_copy_region - copy the pixels of src at (x, y) into dst (dst size)
 */
void sc_surface_copy_region(SURFACE *dst, SURFACE *src, int x, int y)
{
	int row, col, sx, sy;

	if (dst->bits == NULL || src->bits == NULL) {
		return;
	}
	sc_surface_flush(src);
	sc_surface_flush(dst);
	for (row = 0; row < dst->h; row++) {
		sy = y + row;
		if (sy < 0 || sy >= src->h) {
			ZeroMemory(dst->bits + (SIZE_T)row * dst->w, dst->w * sizeof(DWORD));
			continue;
		}
		for (col = 0; col < dst->w; col++) {
			sx = x + col;
			if (sx < 0 || sx >= src->w) {
				dst->bits[(SIZE_T)row * dst->w + col] = 0;
			} else {
				dst->bits[(SIZE_T)row * dst->w + col] = src->bits[(SIZE_T)sy * src->w + sx];
			}
		}
	}
}

/*
 * sc_mask_composite - keep (destination-in) or remove (destination-out) the drawn area
 */
void sc_mask_composite(SURFACE *dst, SURFACE *layer, int mode)
{
	SIZE_T i, n;
	DWORD p;
	unsigned int a, f;

	if (dst->bits == NULL || layer->bits == NULL || dst->w != layer->w || dst->h != layer->h) {
		return;
	}
	sc_surface_flush(dst);
	sc_surface_flush(layer);
	dst->opaque = FALSE;
	n = (SIZE_T)dst->w * dst->h;
	for (i = 0; i < n; i++) {
		a = layer->bits[i] >> 24;
		f = (mode == MASK_IN) ? a : (255 - a);
		if (f == 255) {
			continue;
		}
		if (f == 0) {
			dst->bits[i] = 0;
			continue;
		}
		p = dst->bits[i];
		dst->bits[i] =
			((((p >> 24) & 0xFF) * f / 255) << 24) |
			((((p >> 16) & 0xFF) * f / 255) << 16) |
			((((p >> 8) & 0xFF) * f / 255) << 8) |
			((p & 0xFF) * f / 255);
	}
}

/*
 * hex_value - value of a hexadecimal digit (-1 when invalid)
 */
static int hex_value(TCHAR c)
{
	if (c >= TEXT('0') && c <= TEXT('9')) {
		return c - TEXT('0');
	}
	if (c >= TEXT('a') && c <= TEXT('f')) {
		return c - TEXT('a') + 10;
	}
	if (c >= TEXT('A') && c <= TEXT('F')) {
		return c - TEXT('A') + 10;
	}
	return -1;
}

/*
 * parse_component - number of an rgb()/hsl() argument
 */
static const TCHAR *parse_component(const TCHAR *p, double *value, BOOL *percent)
{
	TCHAR *end;

	while (*p == TEXT(' ') || *p == TEXT(',') || *p == TEXT('/')) {
		p++;
	}
	*value = _tcstod(p, &end);
	if (end == p) {
		return NULL;
	}
	*percent = FALSE;
	if (*end == TEXT('%')) {
		*percent = TRUE;
		end++;
	}
	return end;
}

/*
 * hue_to_rgb
 */
static double hue_to_rgb(double p, double q, double t)
{
	if (t < 0) t += 1;
	if (t > 1) t -= 1;
	if (t < 1.0 / 6) return p + (q - p) * 6 * t;
	if (t < 1.0 / 2) return q;
	if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
	return p;
}

/*
 * clamp_byte
 */
static DWORD clamp_byte(double v)
{
	if (!(v > 0)) {
		return 0;
	}
	if (v > 255) {
		return 255;
	}
	return (DWORD)(v + 0.5);
}

/*
 * sc_parse_color - CSS color string to ARGB
 */
BOOL sc_parse_color(const TCHAR *str, ARGB *color)
{
	TCHAR buf[64];
	TCHAR *p, *r;
	int len, i, v[8];
	DWORD a = 255, rr, gg, bb;

	if (str == NULL) {
		return FALSE;
	}
	lstrcpyn(buf, str, 64);
	for (p = buf; *p == TEXT(' ') || *p == TEXT('\t'); p++);
	for (r = p + lstrlen(p); r > p && (*(r - 1) == TEXT(' ') || *(r - 1) == TEXT('\t')); r--);
	*r = TEXT('\0');
	CharLowerBuff(p, lstrlen(p));
	len = lstrlen(p);

	if (*p == TEXT('#')) {
		p++;
		len--;
		if (len != 3 && len != 4 && len != 6 && len != 8) {
			return FALSE;
		}
		for (i = 0; i < len; i++) {
			v[i] = hex_value(p[i]);
			if (v[i] < 0) {
				return FALSE;
			}
		}
		if (len <= 4) {
			rr = v[0] * 17;
			gg = v[1] * 17;
			bb = v[2] * 17;
			if (len == 4) {
				a = v[3] * 17;
			}
		} else {
			rr = v[0] * 16 + v[1];
			gg = v[2] * 16 + v[3];
			bb = v[4] * 16 + v[5];
			if (len == 8) {
				a = v[6] * 16 + v[7];
			}
		}
		*color = (a << 24) | (rr << 16) | (gg << 8) | bb;
		return TRUE;
	}
	if (str_cmp_n(p, TEXT("rgb"), 3) == 0) {
		double c[4];
		BOOL pc[4];
		const TCHAR *q;
		int n = 0;
		for (q = p; *q != TEXT('\0') && *q != TEXT('('); q++);
		if (*q != TEXT('(')) {
			return FALSE;
		}
		q++;
		for (n = 0; n < 4; n++) {
			const TCHAR *next = parse_component(q, &c[n], &pc[n]);
			if (next == NULL) {
				break;
			}
			q = next;
		}
		if (n < 3) {
			return FALSE;
		}
		rr = clamp_byte(pc[0] ? c[0] * 2.55 : c[0]);
		gg = clamp_byte(pc[1] ? c[1] * 2.55 : c[1]);
		bb = clamp_byte(pc[2] ? c[2] * 2.55 : c[2]);
		if (n == 4) {
			a = clamp_byte(pc[3] ? c[3] * 2.55 : c[3] * 255.0);
		}
		*color = (a << 24) | (rr << 16) | (gg << 8) | bb;
		return TRUE;
	}
	if (str_cmp_n(p, TEXT("hsl"), 3) == 0) {
		double c[4], h, s, l, q1, p1;
		BOOL pc[4];
		const TCHAR *q;
		int n = 0;
		for (q = p; *q != TEXT('\0') && *q != TEXT('('); q++);
		if (*q != TEXT('(')) {
			return FALSE;
		}
		q++;
		for (n = 0; n < 4; n++) {
			const TCHAR *next = parse_component(q, &c[n], &pc[n]);
			if (next == NULL) {
				break;
			}
			q = next;
		}
		if (n < 3) {
			return FALSE;
		}
		h = fmod(c[0], 360.0);
		if (h < 0) h += 360.0;
		h /= 360.0;
		s = c[1] / 100.0;
		l = c[2] / 100.0;
		if (s < 0) s = 0;
		if (s > 1) s = 1;
		if (l < 0) l = 0;
		if (l > 1) l = 1;
		if (s == 0) {
			rr = gg = bb = clamp_byte(l * 255.0);
		} else {
			q1 = (l < 0.5) ? l * (1 + s) : l + s - l * s;
			p1 = 2 * l - q1;
			rr = clamp_byte(hue_to_rgb(p1, q1, h + 1.0 / 3) * 255.0);
			gg = clamp_byte(hue_to_rgb(p1, q1, h) * 255.0);
			bb = clamp_byte(hue_to_rgb(p1, q1, h - 1.0 / 3) * 255.0);
		}
		if (n == 4) {
			a = clamp_byte(pc[3] ? c[3] * 2.55 : c[3] * 255.0);
		}
		*color = (a << 24) | (rr << 16) | (gg << 8) | bb;
		return TRUE;
	}
	if (lstrcmp(p, TEXT("transparent")) == 0) {
		*color = 0;
		return TRUE;
	}
	for (i = 0; i < sizeof(named_colors) / sizeof(NAMED_COLOR); i++) {
		if (lstrcmp(p, named_colors[i].name) == 0) {
			*color = 0xFF000000 | named_colors[i].rgb;
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * pen_width - canvas ignores non positive line widths
 */
static REAL pen_width(double width)
{
	if (!(width > 0) || !_finite(width)) {
		return 1.0f;
	}
	return (REAL)width;
}

/*
 * arc_angle - canvas ellipse() angles are parametric (the point is (rx cos t, ry sin t)),
 * GDI+ measures the angle of the ray through that point: convert for a non circular ellipse
 */
static double arc_angle(double t, double rx, double ry)
{
	if (rx == ry || !(rx > 0) || !(ry > 0)) {
		return t;
	}
	return atan2(ry * sin(t), rx * cos(t));
}

/*
 * sc_draw_line
 */
void sc_draw_line(SURFACE *s, double x1, double y1, double x2, double y2, ARGB color, double width)
{
	GpGraphics *g = sc_surface_graphics(s);
	GpPen *pen = NULL;

	if (g == NULL) {
		return;
	}
	if (GdipCreatePen1(color, pen_width(width), UnitWorld, &pen) != GpOk) {
		return;
	}
	GdipDrawLine(g, pen, (REAL)x1, (REAL)y1, (REAL)x2, (REAL)y2);
	GdipDeletePen(pen);
}

/*
 * fill_rect_fast - opaque fill of a pixel aligned rectangle straight into the bits
 * (the same result as the anti-aliased fill, without the GDI+ overhead)
 */
static BOOL fill_rect_fast(SURFACE *s, double x, double y, double w, double h, ARGB color)
{
	int x1, y1, x2, y2, row, col;
	DWORD *p;

	if (s->bits == NULL || (color >> 24) != 0xFF) {
		return FALSE;
	}
	if (x != floor(x) || y != floor(y) || w != floor(w) || h != floor(h)) {
		return FALSE;
	}
	if (fabs(x) > 1e8 || fabs(y) > 1e8 || w > 1e8 || h > 1e8) {
		return FALSE;
	}
	x1 = (int)x;
	y1 = (int)y;
	x2 = x1 + (int)w;
	y2 = y1 + (int)h;
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 > s->w) x2 = s->w;
	if (y2 > s->h) y2 = s->h;
	if (x1 >= x2 || y1 >= y2) {
		return TRUE;
	}
	sc_surface_flush(s);
	if (x1 == 0 && y1 == 0 && x2 == s->w && y2 == s->h) {
		s->opaque = TRUE;
	}
	for (row = y1; row < y2; row++) {
		p = s->bits + (SIZE_T)row * s->w + x1;
		for (col = x1; col < x2; col++) {
			*p++ = color;
		}
	}
	return TRUE;
}

/*
 * sc_fill_covers_surface - an opaque pixel aligned fill of the whole surface
 */
BOOL sc_fill_covers_surface(SURFACE *s, double x, double y, double w, double h, ARGB color)
{
	if ((color >> 24) != 0xFF || s->mask_mode != MASK_NONE) {
		return FALSE;
	}
	if (x != floor(x) || y != floor(y) || w != floor(w) || h != floor(h)) {
		return FALSE;
	}
	return (x <= 0 && y <= 0 && x + w >= s->w && y + h >= s->h);
}

/*
 * sc_draw_rect
 */
void sc_draw_rect(SURFACE *s, double x, double y, double w, double h, ARGB color, double width, BOOL fill)
{
	GpGraphics *g = sc_surface_graphics(s);

	if (g == NULL) {
		return;
	}
	if (w < 0) {
		x += w;
		w = -w;
	}
	if (h < 0) {
		y += h;
		h = -h;
	}
	if (fill && fill_rect_fast(s, x, y, w, h, color)) {
		return;
	}
	if (fill) {
		GpSolidFill *brush = NULL;
		if (GdipCreateSolidFill(color, &brush) != GpOk) {
			return;
		}
		GdipFillRectangle(g, brush, (REAL)x, (REAL)y, (REAL)w, (REAL)h);
		GdipDeleteBrush(brush);
	} else {
		GpPen *pen = NULL;
		if (GdipCreatePen1(color, pen_width(width), UnitWorld, &pen) != GpOk) {
			return;
		}
		GdipDrawRectangle(g, pen, (REAL)x, (REAL)y, (REAL)w, (REAL)h);
		GdipDeletePen(pen);
	}
}

/*
 * stroke_or_fill_path
 */
static void stroke_or_fill_path(GpGraphics *g, GpPath *path, ARGB color, double width, BOOL fill, BOOL close)
{
	if (fill) {
		GpSolidFill *brush = NULL;
		if (GdipCreateSolidFill(color, &brush) == GpOk) {
			GdipFillPath(g, brush, path);
			GdipDeleteBrush(brush);
		}
	} else {
		GpPen *pen = NULL;
		if (close) {
			GdipClosePathFigure(path);
		}
		if (GdipCreatePen1(color, pen_width(width), UnitWorld, &pen) == GpOk) {
			GdipDrawPath(g, pen, path);
			GdipDeletePen(pen);
		}
	}
}

/*
 * sc_draw_ellipse - canvas ellipse(x, y, rx, ry, rotation, start, end)
 */
void sc_draw_ellipse(SURFACE *s, double x, double y, double rx, double ry, double rotation,
	double start, double end, ARGB color, double width, BOOL fill, BOOL close)
{
	GpGraphics *g = sc_surface_graphics(s);
	GpPath *path = NULL;
	double sweep;

	if (g == NULL) {
		return;
	}
	rx = fabs(rx);
	ry = fabs(ry);
	if (fill && rx == ry && end - start >= 2 * SC_PI && fill_circle_fast(s, x, y, rx, color)) {
		return;
	}
	if (GdipCreatePath(FillModeWinding, &path) != GpOk) {
		return;
	}
	sweep = end - start;
	if (sweep >= 2 * SC_PI) {
		GdipAddPathEllipse(path, (REAL)-rx, (REAL)-ry, (REAL)(rx * 2), (REAL)(ry * 2));
	} else {
		double gstart, gsweep;
		sweep = fmod(sweep, 2 * SC_PI);
		if (sweep < 0) {
			sweep += 2 * SC_PI;
		}
		gstart = arc_angle(start, rx, ry);
		gsweep = fmod(arc_angle(start + sweep, rx, ry) - gstart, 2 * SC_PI);
		if (gsweep < 0) {
			gsweep += 2 * SC_PI;
		}
		GdipAddPathArc(path, (REAL)-rx, (REAL)-ry, (REAL)(rx * 2), (REAL)(ry * 2),
			(REAL)(gstart * 180.0 / SC_PI), (REAL)(gsweep * 180.0 / SC_PI));
	}
	GdipTranslateWorldTransform(g, (REAL)x, (REAL)y, MatrixOrderPrepend);
	GdipRotateWorldTransform(g, (REAL)(rotation * 180.0 / SC_PI), MatrixOrderPrepend);
	stroke_or_fill_path(g, path, color, width, fill, close);
	GdipResetWorldTransform(g);
	GdipDeletePath(path);
}

/*
 * sc_draw_polyline
 */
void sc_draw_polyline(SURFACE *s, GpPointF *pts, int count, ARGB color, double width, BOOL fill, BOOL close)
{
	GpGraphics *g = sc_surface_graphics(s);
	GpPath *path = NULL;

	if (g == NULL || count < 2) {
		return;
	}
	if (GdipCreatePath(FillModeWinding, &path) != GpOk) {
		return;
	}
	GdipAddPathLine2(path, pts, count);
	stroke_or_fill_path(g, path, color, width, fill, close);
	GdipDeletePath(path);
}

/*
 * sc_draw_flood_fill - fill the area of the same color around (x, y)
 */
void sc_draw_flood_fill(SURFACE *s, int x, int y, ARGB color)
{
	DWORD *bits, start_color, fill_color;
	POINT *stack;
	int stack_size, stack_count;
	int w, h;

	if (s->bits == NULL || x < 0 || y < 0 || x >= s->w || y >= s->h) {
		return;
	}
	sc_surface_flush(s);
	bits = s->bits;
	w = s->w;
	h = s->h;
	fill_color = color;
	start_color = bits[(SIZE_T)y * w + x];
	if (start_color == fill_color) {
		return;
	}
	stack_size = 1024;
	stack = HeapAlloc(GetProcessHeap(), 0, sizeof(POINT) * stack_size);
	if (stack == NULL) {
		return;
	}
	stack_count = 0;
	stack[stack_count].x = x;
	stack[stack_count].y = y;
	stack_count++;
	while (stack_count > 0) {
		int cx, cy, yy;
		BOOL reach_left = FALSE, reach_right = FALSE;
		stack_count--;
		cx = stack[stack_count].x;
		cy = stack[stack_count].y;
		yy = cy;
		while (yy >= 0 && bits[(SIZE_T)yy * w + cx] == start_color) {
			yy--;
		}
		yy++;
		while (yy < h && bits[(SIZE_T)yy * w + cx] == start_color) {
			bits[(SIZE_T)yy * w + cx] = fill_color;
			if (cx > 0) {
				if (bits[(SIZE_T)yy * w + cx - 1] == start_color) {
					if (!reach_left) {
						if (stack_count >= stack_size) {
							POINT *ns = HeapReAlloc(GetProcessHeap(), 0, stack, sizeof(POINT) * stack_size * 2);
							if (ns == NULL) {
								HeapFree(GetProcessHeap(), 0, stack);
								return;
							}
							stack = ns;
							stack_size *= 2;
						}
						stack[stack_count].x = cx - 1;
						stack[stack_count].y = yy;
						stack_count++;
						reach_left = TRUE;
					}
				} else {
					reach_left = FALSE;
				}
			}
			if (cx < w - 1) {
				if (bits[(SIZE_T)yy * w + cx + 1] == start_color) {
					if (!reach_right) {
						if (stack_count >= stack_size) {
							POINT *ns = HeapReAlloc(GetProcessHeap(), 0, stack, sizeof(POINT) * stack_size * 2);
							if (ns == NULL) {
								HeapFree(GetProcessHeap(), 0, stack);
								return;
							}
							stack = ns;
							stack_size *= 2;
						}
						stack[stack_count].x = cx + 1;
						stack[stack_count].y = yy;
						stack_count++;
						reach_right = TRUE;
					}
				} else {
					reach_right = FALSE;
				}
			}
			yy++;
		}
	}
	HeapFree(GetProcessHeap(), 0, stack);
}

/*
 * sc_draw_scroll - shift the pixels, wrapping around the edges
 */
void sc_draw_scroll(SURFACE *s, int dx, int dy)
{
	DWORD *tmp;
	int x, y, sx, sy, w, h;

	if (s->bits == NULL) {
		return;
	}
	w = s->w;
	h = s->h;
	dx = ((dx % w) + w) % w;
	dy = ((dy % h) + h) % h;
	if (dx == 0 && dy == 0) {
		return;
	}
	sc_surface_flush(s);
	tmp = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)w * h * sizeof(DWORD));
	if (tmp == NULL) {
		return;
	}
	CopyMemory(tmp, s->bits, (SIZE_T)w * h * sizeof(DWORD));
	for (y = 0; y < h; y++) {
		sy = (y - dy + h) % h;
		for (x = 0; x < w; x++) {
			sx = (x - dx + w) % w;
			s->bits[(SIZE_T)y * w + x] = tmp[(SIZE_T)sy * w + sx];
		}
	}
	HeapFree(GetProcessHeap(), 0, tmp);
}

/*
 * sc_clear_rect - make a rectangle transparent
 */
void sc_clear_rect(SURFACE *s, double x, double y, double w, double h)
{
	GpGraphics *g = sc_surface_graphics(s);
	GpSolidFill *brush = NULL;

	if (g == NULL) {
		return;
	}
	if (w < 0) {
		x += w;
		w = -w;
	}
	if (h < 0) {
		y += h;
		h = -h;
	}
	if (GdipCreateSolidFill(0, &brush) != GpOk) {
		return;
	}
	s->opaque = FALSE;
	GdipSetCompositingMode(g, CompositingModeSourceCopy);
	GdipFillRectangle(g, brush, (REAL)x, (REAL)y, (REAL)w, (REAL)h);
	GdipSetCompositingMode(g, CompositingModeSourceOver);
	GdipDeleteBrush(brush);
}

/* ---- worker pool: row-parallel pixel loops ---- */
/* Workers spin briefly for the next job before sleeping on a semaphore, so a burst
   of drawing calls is served with almost no wake-up latency. Job fields are published
   under pool_publishing while no worker is busy; completion is tracked per row. */
#define POOL_MAX_THREADS		15
#define POOL_DEFAULT_THREADS	7
#define POOL_MIN_PIXELS			3000	/* pixels of work per worker */
#define POOL_SPIN_US			150

typedef void (*ROW_FN)(int y0, int y1, void *ctx);

static HANDLE pool_threads[POOL_MAX_THREADS];
static int pool_count = 0;
static BOOL pool_checked = FALSE;
static HANDLE pool_sem = NULL;
static volatile LONG pool_quit = 0;
static volatile LONG pool_generation = 0;
static volatile LONG pool_publishing = 0;
static volatile LONG pool_busy = 0;
static volatile LONG pool_sleepers = 0;
static volatile LONG pool_next = 0;
static volatile LONG pool_rows = 0;
static volatile LONG pool_remaining = 0;
static LONG pool_chunk = 8;
static ROW_FN pool_fn = NULL;
static void *pool_ctx = NULL;
static LARGE_INTEGER pool_freq;

/*
 * pool_run_chunks - process row chunks of the current job until none are left
 */
static void pool_run_chunks(void)
{
	for (;;) {
		LONG y0 = InterlockedExchangeAdd(&pool_next, pool_chunk);
		LONG rows = pool_rows;
		LONG y1;
		if (y0 >= rows) {
			break;
		}
		y1 = y0 + pool_chunk;
		if (y1 > rows) {
			y1 = rows;
		}
		pool_fn((int)y0, (int)y1, pool_ctx);
		InterlockedExchangeAdd(&pool_remaining, -(y1 - y0));
	}
}

/*
 * pool_thread
 */
static DWORD WINAPI pool_thread(LPVOID param)
{
	LONG seen = 0;

	while (!pool_quit) {
		if (pool_generation == seen) {
			/* spin a little for the next job, then sleep */
			LARGE_INTEGER t0, t;
			BOOL found = FALSE;
			QueryPerformanceCounter(&t0);
			for (;;) {
				int i;
				for (i = 0; i < 64; i++) {
					if (pool_generation != seen) {
						found = TRUE;
						break;
					}
					YieldProcessor();
				}
				if (found || pool_quit) {
					break;
				}
				QueryPerformanceCounter(&t);
				if ((t.QuadPart - t0.QuadPart) * 1000000 / pool_freq.QuadPart > POOL_SPIN_US) {
					break;
				}
			}
			if (!found) {
				if (pool_quit) {
					break;
				}
				InterlockedIncrement(&pool_sleepers);
				if (pool_generation == seen) {
					WaitForSingleObject(pool_sem, INFINITE);
				}
				InterlockedDecrement(&pool_sleepers);
				continue;
			}
		}
		InterlockedIncrement(&pool_busy);
		if (pool_publishing) {
			InterlockedDecrement(&pool_busy);
			YieldProcessor();
			continue;
		}
		seen = pool_generation;
		pool_run_chunks();
		InterlockedDecrement(&pool_busy);
	}
	return 0;
}

/*
 * pool_init - start the workers (one per processor besides the caller)
 */
static void pool_init(void)
{
	SYSTEM_INFO si;
	int i, n;

	if (pool_checked) {
		return;
	}
	pool_checked = TRUE;
	QueryPerformanceFrequency(&pool_freq);
	GetSystemInfo(&si);
	n = (int)si.dwNumberOfProcessors - 1;
	if (n > POOL_DEFAULT_THREADS) {
		n = POOL_DEFAULT_THREADS;
	}
	{
		/* PG0_SCREEN_THREADS overrides the number of worker threads (0: none) */
		TCHAR buf[16];
		DWORD len = GetEnvironmentVariable(TEXT("PG0_SCREEN_THREADS"), buf, 16);
		if (len > 0 && len < 16) {
			n = _ttoi(buf);
		}
	}
	if (n > POOL_MAX_THREADS) {
		n = POOL_MAX_THREADS;
	}
	if (n <= 0) {
		return;
	}
	pool_sem = CreateSemaphore(NULL, 0, POOL_MAX_THREADS * 4, NULL);
	if (pool_sem == NULL) {
		return;
	}
	pool_quit = 0;
	pool_generation = 0;
	pool_publishing = 0;
	pool_busy = 0;
	pool_sleepers = 0;
	for (i = 0; i < n; i++) {
		pool_threads[pool_count] = CreateThread(NULL, 0, pool_thread, NULL, 0, NULL);
		if (pool_threads[pool_count] == NULL) {
			break;
		}
		pool_count++;
	}
}

/*
 * sc_pool_shutdown - end the workers
 */
void sc_pool_shutdown(void)
{
	int i;

	if (pool_count > 0) {
		InterlockedExchange(&pool_quit, 1);
		InterlockedIncrement(&pool_generation);
		ReleaseSemaphore(pool_sem, pool_count, NULL);
		WaitForMultipleObjects(pool_count, pool_threads, TRUE, 3000);
		for (i = 0; i < pool_count; i++) {
			CloseHandle(pool_threads[i]);
		}
		pool_count = 0;
	}
	if (pool_sem != NULL) {
		CloseHandle(pool_sem);
		pool_sem = NULL;
	}
	pool_checked = FALSE;
}

/*
 * parallel_rows - run fn over [0, rows) split across the workers and the caller
 */
static void parallel_rows(int rows, int pixels, ROW_FN fn, void *ctx)
{
	int workers, awake, wake;

	if (rows <= 0) {
		return;
	}
	pool_init();
	workers = pixels / POOL_MIN_PIXELS;
	if (workers > pool_count) {
		workers = pool_count;
	}
	if (workers <= 0 || rows < 4) {
		fn(0, rows, ctx);
		return;
	}
	/* publish the job while no worker reads the fields */
	InterlockedExchange(&pool_publishing, 1);
	while (pool_busy != 0) {
		YieldProcessor();
	}
	pool_fn = fn;
	pool_ctx = ctx;
	pool_rows = rows;
	pool_chunk = rows / ((workers + 1) * 2);
	if (pool_chunk < 2) pool_chunk = 2;
	if (pool_chunk > 32) pool_chunk = 32;
	pool_next = 0;
	pool_remaining = rows;
	MemoryBarrier();
	InterlockedIncrement(&pool_generation);
	InterlockedExchange(&pool_publishing, 0);
	/* spinning workers pick the job up by themselves; wake sleepers only when needed */
	awake = pool_count - pool_sleepers;
	wake = workers - awake;
	if (wake > 0) {
		if (wake > pool_sleepers) {
			wake = pool_sleepers;
		}
		if (wake > 0) {
			ReleaseSemaphore(pool_sem, wake, NULL);
		}
	}
	pool_run_chunks();
	while (pool_remaining > 0) {
		YieldProcessor();
	}
}

/* ---- image drawing ---- */
typedef struct _IMAGE_JOB {
	SURFACE *dst;
	SURFACE *src;
	int x0;			/* destination bounding box [x0, x1) */
	int x1;
	int y0;			/* first destination row of the job (rows are relative to it) */
	double cx;		/* destination center */
	double cy;
	double cos_a;
	double sin_a;
	double w;		/* destination size */
	double h;
	double su;		/* source pixels per destination pixel */
	double sv;
	DWORD alpha;	/* 0..256 */
} IMAGE_JOB;

/* two 8bit channels at once: x * m / 255 for m in 0..255 */
#define MUL255_PAIR(x, m)		((((x) * (m) + 0x00800080) + ((((x) * (m) + 0x00800080) >> 8) & 0x00FF00FF)) >> 8 & 0x00FF00FF)
/* two channels at once: linear interpolation with a weight in 0..256 */
#define LERP_PAIR(a, b, t)		((((a) * (256 - (t)) + (b) * (t)) >> 8) & 0x00FF00FF)

/*
 * blend_premul - source-over of a premultiplied pixel
 */
static __inline void blend_premul(DWORD *d, DWORD s)
{
	DWORD a = s >> 24, q, inv, rb, ag;
	if (a == 255) {
		*d = s;
		return;
	}
	if (a == 0) {
		return;
	}
	q = *d;
	inv = 255 - a;
	rb = MUL255_PAIR(q & 0x00FF00FF, inv);
	ag = MUL255_PAIR((q >> 8) & 0x00FF00FF, inv);
	*d = (((ag << 8) | rb) + s);
}

/*
 * scale_premul - premultiplied pixel times alpha (0..256)
 */
static __inline DWORD scale_premul(DWORD p, DWORD alpha)
{
	DWORD rb = ((p & 0x00FF00FF) * alpha >> 8) & 0x00FF00FF;
	DWORD ag = (((p >> 8) & 0x00FF00FF) * alpha >> 8) & 0x00FF00FF;
	return (ag << 8) | rb;
}

/*
 * sample_bilinear - source sample at fixed point (16.16) source coordinates
 */
static __inline DWORD sample_bilinear(const SURFACE *src, LONG u, LONG v)
{
	const DWORD *bits = src->bits;
	int sw = src->w, sh = src->h;
	int ix, iy, ix1, iy1;
	DWORD wx, wy, p00, p01, p10, p11, top, bottom;

	u -= 32768;
	v -= 32768;
	ix = u >> 16;
	iy = v >> 16;
	wx = (u >> 8) & 0xFF;
	wy = (v >> 8) & 0xFF;
	ix1 = ix + 1;
	iy1 = iy + 1;
	if (ix < 0 || iy < 0 || ix1 >= sw || iy1 >= sh) {
		if (ix < 0) ix = 0;
		if (iy < 0) iy = 0;
		if (ix1 < 0) ix1 = 0;
		if (iy1 < 0) iy1 = 0;
		if (ix >= sw) ix = sw - 1;
		if (iy >= sh) iy = sh - 1;
		if (ix1 >= sw) ix1 = sw - 1;
		if (iy1 >= sh) iy1 = sh - 1;
	}
	p00 = bits[(SIZE_T)iy * sw + ix];
	p01 = bits[(SIZE_T)iy * sw + ix1];
	p10 = bits[(SIZE_T)iy1 * sw + ix];
	p11 = bits[(SIZE_T)iy1 * sw + ix1];
	if ((p00 | p01 | p10 | p11) >> 24 == 0) {
		return 0;
	}
	if (p00 == p01 && p00 == p10 && p00 == p11) {
		return p00;
	}
	top = LERP_PAIR(p00 & 0x00FF00FF, p01 & 0x00FF00FF, wx) |
		(LERP_PAIR((p00 >> 8) & 0x00FF00FF, (p01 >> 8) & 0x00FF00FF, wx) << 8);
	bottom = LERP_PAIR(p10 & 0x00FF00FF, p11 & 0x00FF00FF, wx) |
		(LERP_PAIR((p10 >> 8) & 0x00FF00FF, (p11 >> 8) & 0x00FF00FF, wx) << 8);
	return LERP_PAIR(top & 0x00FF00FF, bottom & 0x00FF00FF, wy) |
		(LERP_PAIR((top >> 8) & 0x00FF00FF, (bottom >> 8) & 0x00FF00FF, wy) << 8);
}

/*
 * span_limit - restrict [lo, hi) to the x where 0 <= a * x + b < len
 */
static void span_limit(double a, double b, double len, double *lo, double *hi)
{
	double t0, t1;

	if (fabs(a) < 1e-12) {
		if (b < 0 || b >= len) {
			*hi = *lo;
		}
		return;
	}
	t0 = -b / a;
	t1 = (len - b) / a;
	if (a < 0) {
		double t = t0;
		t0 = t1;
		t1 = t;
	}
	if (t0 > *lo) *lo = t0;
	if (t1 < *hi) *hi = t1;
}

/*
 * image_rows - draw the destination rows [y0, y1) of a scaled, rotated image
 */
static void image_rows(int y0, int y1, void *ctx)
{
	const IMAGE_JOB *job = (const IMAGE_JOB *)ctx;
	const SURFACE *src = job->src;
	SURFACE *dst = job->dst;
	double hw = job->w / 2, hh = job->h / 2;
	const LONG uw = (LONG)src->w << 16, vh = (LONG)src->h << 16;
	const LONG du = (LONG)(job->cos_a * job->su * 65536.0);
	const LONG dv = (LONG)(-job->sin_a * job->sv * 65536.0);
	int y;

	for (y = job->y0 + y0; y < job->y0 + y1; y++) {
		double dy = (y + 0.5) - job->cy;
		/* local coordinates as a function of x: lx = cos * x + bx, ly = -sin * x + by */
		double bx = job->cos_a * (0.5 - job->cx) + job->sin_a * dy + hw;
		double by = -job->sin_a * (0.5 - job->cx) + job->cos_a * dy + hh;
		double lo = job->x0, hi = job->x1;
		double lx, ly;
		LONG u, v;
		DWORD *row;
		int x, xs, xe;
		span_limit(job->cos_a, bx, job->w, &lo, &hi);
		span_limit(-job->sin_a, by, job->h, &lo, &hi);
		if (hi <= lo) {
			continue;
		}
		xs = (int)ceil(lo);
		xe = (int)ceil(hi);
		if (xs < job->x0) xs = job->x0;
		if (xe > job->x1) xe = job->x1;
		if (xs >= xe) {
			continue;
		}
		lx = job->cos_a * xs + bx;
		ly = -job->sin_a * xs + by;
		u = (LONG)(lx * job->su * 65536.0);
		v = (LONG)(ly * job->sv * 65536.0);
		row = dst->bits + (SIZE_T)y * dst->w;
		for (x = xs; x < xe; x++, u += du, v += dv) {
			DWORD p;
			if ((ULONG)u >= (ULONG)uw || (ULONG)v >= (ULONG)vh) {
				continue;
			}
			p = sample_bilinear(src, u, v);
			if (p == 0) {
				continue;
			}
			if (job->alpha < 256) {
				p = scale_premul(p, job->alpha);
			}
			blend_premul(row + x, p);
		}
	}
}

/*
 * copy_rows - unscaled, unrotated image at an integer position
 */
static void copy_rows(int y0, int y1, void *ctx)
{
	const IMAGE_JOB *job = (const IMAGE_JOB *)ctx;
	const SURFACE *src = job->src;
	SURFACE *dst = job->dst;
	int sx0 = job->x0 - (int)job->cx;		/* cx, cy hold the integer destination origin here */
	int sy0 = job->y0 - (int)job->cy;
	int y, x, n = job->x1 - job->x0;

	for (y = y0; y < y1; y++) {
		const DWORD *s = src->bits + (SIZE_T)(sy0 + y) * src->w + sx0;
		DWORD *d = dst->bits + (SIZE_T)(job->y0 + y) * dst->w + job->x0;
		if (job->alpha < 256) {
			for (x = 0; x < n; x++) {
				blend_premul(d + x, scale_premul(s[x], job->alpha));
			}
		} else {
			for (x = 0; x < n; x++) {
				blend_premul(d + x, s[x]);
			}
		}
	}
}

/*
 * draw_image_fast - scaled/rotated image without GDI+
 */
static BOOL draw_image_fast(SURFACE *s, SURFACE *img, double x, double y, double w, double h,
	BOOL rotate, double angle, double alpha)
{
	IMAGE_JOB job;
	double cx, cy, ex, ey, c, sn;
	int bx0, by0, bx1, by1;

	if (s->bits == NULL || img->bits == NULL || !(w > 0) || !(h > 0) || img->w <= 0 || img->h <= 0) {
		return FALSE;
	}
	if (fabs(x) > 1e7 || fabs(y) > 1e7 || w > 1e7 || h > 1e7) {
		return FALSE;
	}
	if (alpha <= 0) {
		return TRUE;
	}
	if (alpha > 1) {
		alpha = 1;
	}
	sc_surface_flush(s);
	sc_surface_flush(img);
	job.dst = s;
	job.src = img;
	job.alpha = (DWORD)(alpha * 256.0 + 0.5);
	if (job.alpha > 256) {
		job.alpha = 256;
	}
	if (!rotate && w == img->w && h == img->h && x == floor(x) && y == floor(y)) {
		/* plain copy at an integer position */
		bx0 = (int)x;
		by0 = (int)y;
		bx1 = bx0 + img->w;
		by1 = by0 + img->h;
		if (bx0 < 0) bx0 = 0;
		if (by0 < 0) by0 = 0;
		if (bx1 > s->w) bx1 = s->w;
		if (by1 > s->h) by1 = s->h;
		if (bx0 >= bx1 || by0 >= by1) {
			return TRUE;
		}
		job.x0 = bx0;
		job.x1 = bx1;
		job.y0 = by0;
		job.cx = x;
		job.cy = y;
		parallel_rows(by1 - by0, (bx1 - bx0) * (by1 - by0), copy_rows, &job);
		return TRUE;
	}
	c = rotate ? cos(angle) : 1.0;
	sn = rotate ? sin(angle) : 0.0;
	cx = x + w / 2;
	cy = y + h / 2;
	/* bounding box of the rotated rectangle */
	ex = fabs(c) * w / 2 + fabs(sn) * h / 2;
	ey = fabs(sn) * w / 2 + fabs(c) * h / 2;
	bx0 = (int)floor(cx - ex);
	by0 = (int)floor(cy - ey);
	bx1 = (int)ceil(cx + ex) + 1;
	by1 = (int)ceil(cy + ey) + 1;
	if (bx0 < 0) bx0 = 0;
	if (by0 < 0) by0 = 0;
	if (bx1 > s->w) bx1 = s->w;
	if (by1 > s->h) by1 = s->h;
	if (bx0 >= bx1 || by0 >= by1) {
		return TRUE;
	}
	job.x0 = bx0;
	job.x1 = bx1;
	job.y0 = by0;
	job.cx = cx;
	job.cy = cy;
	job.cos_a = c;
	job.sin_a = sn;
	job.w = w;
	job.h = h;
	job.su = img->w / w;
	job.sv = img->h / h;
	parallel_rows(by1 - by0, (int)(w * h), image_rows, &job);
	return TRUE;
}

/*
 * sc_draw_image - draw an image, optionally scaled, rotated around its center and faded
 */
void sc_draw_image(SURFACE *s, SURFACE *img, double x, double y, double w, double h,
	BOOL rotate, double angle, double alpha)
{
	GpGraphics *g;
	GpImageAttributes *attr = NULL;
	GpColorMatrix cm;

	if (draw_image_fast(s, img, x, y, w, h, rotate, angle, alpha)) {
		return;
	}
	g = sc_surface_graphics(s);
	if (g == NULL || img->bmp == NULL) {
		return;
	}
	sc_surface_flush(img);
	if (GdipCreateImageAttributes(&attr) != GpOk) {
		return;
	}
	GdipSetImageAttributesWrapMode(attr, WrapModeTileFlipXY, 0, FALSE);
	if (alpha < 1.0) {
		ZeroMemory(&cm, sizeof(cm));
		cm.m[0][0] = 1.0f;
		cm.m[1][1] = 1.0f;
		cm.m[2][2] = 1.0f;
		cm.m[3][3] = (REAL)((alpha < 0) ? 0 : alpha);
		cm.m[4][4] = 1.0f;
		GdipSetImageAttributesColorMatrix(attr, ColorAdjustTypeBitmap, TRUE, &cm, NULL, ColorMatrixFlagsDefault);
	}
	if (rotate) {
		GdipTranslateWorldTransform(g, (REAL)(x + w / 2), (REAL)(y + h / 2), MatrixOrderPrepend);
		GdipRotateWorldTransform(g, (REAL)(angle * 180.0 / SC_PI), MatrixOrderPrepend);
		GdipDrawImageRectRect(g, img->bmp, (REAL)(-w / 2), (REAL)(-h / 2), (REAL)w, (REAL)h,
			0, 0, (REAL)img->w, (REAL)img->h, UnitPixel, attr, NULL, NULL);
		GdipResetWorldTransform(g);
	} else {
		GdipDrawImageRectRect(g, img->bmp, (REAL)x, (REAL)y, (REAL)w, (REAL)h,
			0, 0, (REAL)img->w, (REAL)img->h, UnitPixel, attr, NULL, NULL);
	}
	GdipDisposeImageAttributes(attr);
}

/*
 * parse_font_style - CSS font style/weight words to a GDI+ style
 */
static int parse_font_style(const TCHAR *style)
{
	TCHAR buf[128], *p, *r;
	int ret = FontStyleRegular;

	if (style == NULL) {
		return ret;
	}
	lstrcpyn(buf, style, 128);
	CharLowerBuff(buf, lstrlen(buf));
	for (p = buf; *p != TEXT('\0'); p = r) {
		while (*p == TEXT(' ') || *p == TEXT('\t')) {
			p++;
		}
		for (r = p; *r != TEXT('\0') && *r != TEXT(' ') && *r != TEXT('\t'); r++);
		if (*r != TEXT('\0')) {
			*r = TEXT('\0');
			r++;
		}
		if (*p == TEXT('\0')) {
			continue;
		}
		if (lstrcmp(p, TEXT("bold")) == 0 || lstrcmp(p, TEXT("bolder")) == 0) {
			ret |= FontStyleBold;
		} else if (lstrcmp(p, TEXT("italic")) == 0 || lstrcmp(p, TEXT("oblique")) == 0) {
			ret |= FontStyleItalic;
		} else if (*p >= TEXT('0') && *p <= TEXT('9')) {
			if (_ttoi(p) >= 600) {
				ret |= FontStyleBold;
			}
		}
	}
	return ret;
}

/*
 * find_family - a font family by name (NULL when the font is not installed)
 */
static GpFontFamily *find_family(const TCHAR *name)
{
	GpFontFamily *family = NULL;
	if (GdipCreateFontFamilyFromName(name, NULL, &family) != GpOk) {
		return NULL;
	}
	return family;
}

/*
 * generic_family - CSS generic family names
 */
static GpFontFamily *generic_family(const TCHAR *name, BOOL *owned)
{
	static const TCHAR *sans[] = {TEXT("Yu Gothic UI"), TEXT("Meiryo UI"), TEXT("Segoe UI"), TEXT("Arial"), NULL};
	static const TCHAR *serif[] = {TEXT("Yu Mincho"), TEXT("MS Mincho"), TEXT("Times New Roman"), NULL};
	static const TCHAR *mono[] = {TEXT("Consolas"), TEXT("MS Gothic"), TEXT("Courier New"), NULL};
	const TCHAR **list = NULL;
	GpFontFamily *family = NULL;
	int i;

	*owned = TRUE;
	if (lstrcmp(name, TEXT("sans-serif")) == 0 || lstrcmp(name, TEXT("system-ui")) == 0 ||
		lstrcmp(name, TEXT("ui-sans-serif")) == 0) {
		list = sans;
	} else if (lstrcmp(name, TEXT("serif")) == 0 || lstrcmp(name, TEXT("ui-serif")) == 0) {
		list = serif;
	} else if (lstrcmp(name, TEXT("monospace")) == 0 || lstrcmp(name, TEXT("ui-monospace")) == 0) {
		list = mono;
	} else if (lstrcmp(name, TEXT("cursive")) == 0) {
		return find_family(TEXT("Segoe Script"));
	} else if (lstrcmp(name, TEXT("fantasy")) == 0) {
		return find_family(TEXT("Impact"));
	} else {
		return NULL;
	}
	for (i = 0; list[i] != NULL; i++) {
		family = find_family(list[i]);
		if (family != NULL) {
			return family;
		}
	}
	*owned = FALSE;
	if (list == serif) {
		if (generic_serif == NULL) {
			GdipGetGenericFontFamilySerif(&generic_serif);
		}
		return generic_serif;
	}
	if (list == mono) {
		if (generic_mono == NULL) {
			GdipGetGenericFontFamilyMonospace(&generic_mono);
		}
		return generic_mono;
	}
	if (generic_sans == NULL) {
		GdipGetGenericFontFamilySansSerif(&generic_sans);
	}
	return generic_sans;
}

/*
 * resolve_family - a font family for a CSS font-family list
 */
static GpFontFamily *resolve_family(const TCHAR *face)
{
	TCHAR buf[LF_FACESIZE * 4], *p, *r, *e;
	GpFontFamily *family = NULL;
	BOOL owned = TRUE;
	int i;

	if (face == NULL || *face == TEXT('\0')) {
		face = TEXT("sans-serif");
	}
	for (i = 0; i < font_cache_count; i++) {
		if (lstrcmp(font_cache[i].name, face) == 0) {
			return font_cache[i].family;
		}
	}
	lstrcpyn(buf, face, LF_FACESIZE * 4);
	for (p = buf; *p != TEXT('\0') && family == NULL; p = r) {
		for (r = p; *r != TEXT('\0') && *r != TEXT(','); r++);
		if (*r == TEXT(',')) {
			*r = TEXT('\0');
			r++;
		}
		while (*p == TEXT(' ') || *p == TEXT('\t') || *p == TEXT('"') || *p == TEXT('\'')) {
			p++;
		}
		for (e = p + lstrlen(p); e > p && (*(e - 1) == TEXT(' ') || *(e - 1) == TEXT('\t') || *(e - 1) == TEXT('"') || *(e - 1) == TEXT('\'')); e--);
		*e = TEXT('\0');
		if (*p == TEXT('\0')) {
			continue;
		}
		CharLowerBuff(p, lstrlen(p));
		family = generic_family(p, &owned);
		if (family == NULL) {
			family = find_family(p);
			owned = TRUE;
		}
	}
	if (family == NULL) {
		family = generic_family(TEXT("sans-serif"), &owned);
	}
	if (family == NULL) {
		return NULL;
	}
	if (!owned) {
		/* generic families belong to GDI+ for the whole session */
		return family;
	}
	if (font_cache_count >= FONT_CACHE_SIZE) {
		GdipDeleteFontFamily(font_cache[0].family);
		for (i = 1; i < font_cache_count; i++) {
			font_cache[i - 1] = font_cache[i];
		}
		font_cache_count--;
	}
	lstrcpyn(font_cache[font_cache_count].name, face, LF_FACESIZE * 4);
	font_cache[font_cache_count].family = family;
	font_cache_count++;
	return family;
}

/*
 * text_top - layout top so that the alphabetic baseline sits at y + size (canvas rule)
 */
static double text_top(GpFontFamily *family, int style, double size, double y)
{
	UINT16 ascent = 0, em = 0;

	GdipGetCellAscent(family, style, &ascent);
	GdipGetEmHeight(family, style, &em);
	if (em == 0) {
		return y;
	}
	return y + size - size * (double)ascent / (double)em;
}

/*
 * sc_draw_text
 */
BOOL sc_draw_text(SURFACE *s, const TCHAR *text, double x, double y, ARGB color, double width, BOOL fill,
	const TCHAR *style, double size, const TCHAR *face)
{
	GpGraphics *g = sc_surface_graphics(s);
	GpFontFamily *family;
	GpRectF rc;
	int fs;

	if (g == NULL || text == NULL) {
		return FALSE;
	}
	if (!(size > 0)) {
		return TRUE;
	}
	family = resolve_family(face);
	if (family == NULL) {
		return FALSE;
	}
	fs = parse_font_style(style);
	rc.X = (REAL)x;
	rc.Y = (REAL)text_top(family, fs, size, y);
	rc.Width = LAYOUT_SIZE;
	rc.Height = LAYOUT_SIZE;
	if (fill) {
		GpFont *font = NULL;
		GpSolidFill *brush = NULL;
		if (GdipCreateFont(family, (REAL)size, fs, UnitPixel, &font) != GpOk) {
			return FALSE;
		}
		if (GdipCreateSolidFill(color, &brush) == GpOk) {
			GdipDrawString(g, text, -1, font, &rc, text_format, brush);
			GdipDeleteBrush(brush);
		}
		GdipDeleteFont(font);
	} else {
		GpPath *path = NULL;
		GpPen *pen = NULL;
		if (GdipCreatePath(FillModeWinding, &path) != GpOk) {
			return FALSE;
		}
		GdipAddPathString(path, text, -1, family, fs, (REAL)size, &rc, text_format);
		if (GdipCreatePen1(color, pen_width(width), UnitWorld, &pen) == GpOk) {
			GdipDrawPath(g, pen, path);
			GdipDeletePen(pen);
		}
		GdipDeletePath(path);
	}
	return TRUE;
}

/*
 * sc_measure_text - size of the drawn (inked) area of a text
 */
BOOL sc_measure_text(const TCHAR *text, const TCHAR *style, double size, const TCHAR *face, double *w, double *h)
{
	SURFACE tmp;
	GpFontFamily *family;
	GpFont *font = NULL;
	GpSolidFill *brush = NULL;
	GpGraphics *g;
	GpRectF layout, bounds;
	int fs, pad, tw, th, x, y, minx, miny, maxx, maxy;

	*w = 0;
	*h = 0;
	if (text == NULL || *text == TEXT('\0') || !(size > 0)) {
		return TRUE;
	}
	family = resolve_family(face);
	if (family == NULL) {
		return FALSE;
	}
	fs = parse_font_style(style);
	if (GdipCreateFont(family, (REAL)size, fs, UnitPixel, &font) != GpOk) {
		return FALSE;
	}
	ZeroMemory(&tmp, sizeof(tmp));
	if (!sc_surface_create(&tmp, 1, 1)) {
		GdipDeleteFont(font);
		return FALSE;
	}
	g = sc_surface_graphics(&tmp);
	layout.X = 0;
	layout.Y = 0;
	layout.Width = LAYOUT_SIZE;
	layout.Height = LAYOUT_SIZE;
	ZeroMemory(&bounds, sizeof(bounds));
	if (g != NULL) {
		GdipMeasureString(g, text, -1, font, &layout, text_format, &bounds, NULL, NULL);
	}
	sc_surface_free(&tmp);

	pad = (int)ceil(size) + 2;
	tw = (int)ceil(bounds.Width) + pad * 2;
	th = (int)ceil(bounds.Height) + pad * 2;
	if (tw > MAX_MEASURE_SIZE) tw = MAX_MEASURE_SIZE;
	if (th > MAX_MEASURE_SIZE) th = MAX_MEASURE_SIZE;
	if (!sc_surface_create(&tmp, tw, th)) {
		GdipDeleteFont(font);
		return FALSE;
	}
	g = sc_surface_graphics(&tmp);
	if (g != NULL && GdipCreateSolidFill(0xFF000000, &brush) == GpOk) {
		layout.X = (REAL)pad;
		layout.Y = (REAL)pad;
		GdipDrawString(g, text, -1, font, &layout, text_format, brush);
		GdipDeleteBrush(brush);
	}
	sc_surface_flush(&tmp);
	minx = tw;
	miny = th;
	maxx = -1;
	maxy = -1;
	for (y = 0; y < th; y++) {
		for (x = 0; x < tw; x++) {
			if ((tmp.bits[(SIZE_T)y * tw + x] >> 24) != 0) {
				if (x < minx) minx = x;
				if (x > maxx) maxx = x;
				if (y < miny) miny = y;
				if (y > maxy) maxy = y;
			}
		}
	}
	if (maxx >= minx && maxy >= miny) {
		*w = maxx - minx + 1;
		*h = maxy - miny + 1;
	}
	sc_surface_free(&tmp);
	GdipDeleteFont(font);
	return TRUE;
}

/*
 * sc_get_pixel - straight (non premultiplied) ARGB of a pixel
 */
DWORD sc_get_pixel(SURFACE *s, int x, int y)
{
	DWORD p, a, r, g, b;

	if (s->bits == NULL || x < 0 || y < 0 || x >= s->w || y >= s->h) {
		return 0;
	}
	sc_surface_flush(s);
	p = s->bits[(SIZE_T)y * s->w + x];
	a = p >> 24;
	if (a == 0) {
		return 0;
	}
	r = ((p >> 16) & 0xFF) * 255 / a;
	g = ((p >> 8) & 0xFF) * 255 / a;
	b = (p & 0xFF) * 255 / a;
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return (a << 24) | (r << 16) | (g << 8) | b;
}
/* End of source */
