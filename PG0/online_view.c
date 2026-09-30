/*
 * PG0
 *
 * online_view.c
 *
 * Views of the online dialogs: the script list and the row of genre chips,
 * drawn like the web version.
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <windowsx.h>
#include <tchar.h>
#include <stdlib.h>
#include <math.h>

#include "script_memory.h"
#include "dpi.h"
#include "online_view.h"

#pragma comment(lib, "msimg32.lib")

/* Define */
#define COLOR_BACK						RGB(0xFF, 0xFF, 0xFF)
#define COLOR_TEXT						RGB(0x00, 0x00, 0x00)
#define COLOR_SUB_TEXT					RGB(0xAA, 0xAA, 0xAA)
#define COLOR_MEMO						RGB(0x80, 0x80, 0x80)
#define COLOR_LINE						RGB(0xDD, 0xDD, 0xDD)
#define COLOR_BORDER					RGB(0x99, 0x99, 0x99)
#define COLOR_HOVER						RGB(0xEE, 0xEE, 0xEE)
#define COLOR_PRESS						RGB(0xDD, 0xDD, 0xDD)
#define COLOR_TAG_BACK					RGB(0xE5, 0xE5, 0xE5)
#define COLOR_TAG_HOVER					RGB(0xCC, 0xCC, 0xCC)
#define COLOR_TAG_TEXT					RGB(0x55, 0x55, 0x55)
#define COLOR_CURRENT					RGB(0xBB, 0xBB, 0xBB)
#define COLOR_MENU_HOVER				RGB(0xCC, 0xCC, 0xCC)
#define COLOR_MENU_PRESS				RGB(0xAA, 0xAA, 0xAA)
#define COLOR_MENU_DOT					RGB(0x80, 0x80, 0x80)
#define COLOR_CHIP_BORDER				RGB(0xBB, 0xBB, 0xBB)
#define COLOR_CHIP_TEXT					RGB(0x33, 0x33, 0x33)
#define COLOR_CHIP_ACTIVE				RGB(0x00, 0x6C, 0xFF)
#define COLOR_ARROW						RGB(0x80, 0x80, 0x80)
#define COLOR_ARROW_HOVER				RGB(0x33, 0x33, 0x33)

#define TIMER_SPINNER					1
#define SPINNER_INTERVAL				80
#define SPINNER_DOTS					8

// sizes in pixels at 96 DPI
#define ITEM_PADDING					10
#define ITEM_PADDING_RIGHT				40
#define LINE_GAP						3
#define AUTHOR_MARGIN					5
#define TAG_PADDING_X					8
#define TAG_PADDING_Y					3
#define TAG_MARGIN						5
#define BADGE_PADDING_X					10
#define BADGE_PADDING_Y					4
#define BADGE_MARGIN					10
#define MENU_SIZE						40
#define MORE_PADDING					20
#define LINE_SCROLL						40
#define CHIP_PADDING_X					10
#define CHIP_PADDING_Y					4
#define CHIP_MARGIN						6
#define CHIP_ROW_PADDING				2
#define ARROW_WIDTH						36
#define ARROW_ICON						20
#define DRAG_THRESHOLD					5
#define WHEEL_SCROLL					60

/* Global Variables */
typedef enum _PART {
	PART_NONE = 0,
	PART_ITEM,
	PART_MENU,
	PART_TAG,
	PART_MORE
} PART;

typedef struct _HIT {
	int index;
	PART part;
	int tag;
} HIT;

typedef struct _LIST_DATA {
	ONLINE_ITEM **items;
	int count;
	int size;
	BOOL more;
	BOOL loading;
	TCHAR *more_text;
	TCHAR *current_text;
	TCHAR sel_tag[ONLINE_TAG_SIZE];
	HFONT hNameFont;
	HFONT hSmallFont;
	HFONT hTagFont;
	int name_height;
	int small_height;
	int tag_height;
	int scroll;
	int focus;
	HIT hover;
	HIT press;
	BOOL tracking;
	int spinner;
} LIST_DATA;

typedef struct _CHIPS_DATA {
	TCHAR **labels;
	int *widths;
	int count;
	int size;
	int sel;
	int focus;
	int hover;
	int hover_arrow;
	int press;
	int scroll;
	HFONT hFont;
	int chip_height;
	BOOL tracking;
	BOOL dragging;
	BOOL dragged;
	int drag_x;
	int drag_scroll;
	BOOL key_focus;
	BOOL mouse_focus;
} CHIPS_DATA;

/* Local Function Prototypes */

/*
 * grow - allocate or extend a buffer (mem_realloc does not take NULL)
 */
static void *grow(void *mem, const int size)
{
	return (mem == NULL) ? mem_alloc(size) : mem_realloc(mem, size);
}

/*
 * online_item_free - free an item
 */
void online_item_free(ONLINE_ITEM *item)
{
	int i;

	if (item == NULL) {
		return;
	}
	mem_free(&item->name);
	mem_free(&item->author);
	mem_free(&item->memo);
	mem_free(&item->time);
	for (i = 0; i < item->tag_count; i++) {
		mem_free(&item->tag_labels[i]);
	}
	mem_free(&item);
}

/*
 * item_copy - copy an item
 */
static ONLINE_ITEM *item_copy(const ONLINE_ITEM *src)
{
	ONLINE_ITEM *item;
	int i;

	if ((item = mem_calloc(sizeof(ONLINE_ITEM))) == NULL) {
		return NULL;
	}
	CopyMemory(item, src, sizeof(ONLINE_ITEM));
	item->name = alloc_copy((src->name != NULL) ? src->name : TEXT(""));
	item->author = alloc_copy((src->author != NULL) ? src->author : TEXT(""));
	item->memo = (src->memo != NULL) ? alloc_copy(src->memo) : NULL;
	item->time = alloc_copy((src->time != NULL) ? src->time : TEXT(""));
	for (i = 0; i < item->tag_count; i++) {
		item->tag_labels[i] = alloc_copy((src->tag_labels[i] != NULL) ? src->tag_labels[i] : src->tags[i]);
	}
	return item;
}

/*
 * create_font - font of the given height derived from the font of the control
 */
static HFONT create_font(const HFONT hFont, const int numerator, const int denominator)
{
	LOGFONT lf;

	if (hFont == NULL || GetObject(hFont, sizeof(LOGFONT), &lf) == 0) {
		NONCLIENTMETRICS ncm;
		ZeroMemory(&ncm, sizeof(NONCLIENTMETRICS));
		ncm.cbSize = sizeof(NONCLIENTMETRICS);
		SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);
		lf = ncm.lfMessageFont;
	}
	lf.lfHeight = MulDiv(lf.lfHeight, numerator, denominator);
	return CreateFontIndirect(&lf);
}

/*
 * font_height - height of a line of the font
 */
static int font_height(const HWND hWnd, const HFONT hFont)
{
	TEXTMETRIC tm;
	HDC hdc;
	HFONT hRetFont;

	hdc = GetDC(hWnd);
	hRetFont = SelectObject(hdc, hFont);
	GetTextMetrics(hdc, &tm);
	SelectObject(hdc, hRetFont);
	ReleaseDC(hWnd, hdc);
	return tm.tmHeight;
}

/*
 * text_width - width of a text
 */
static int text_width(const HDC hdc, const HFONT hFont, const TCHAR *str)
{
	HFONT hRetFont;
	SIZE sz = {0, 0};

	hRetFont = SelectObject(hdc, hFont);
	GetTextExtentPoint32(hdc, str, lstrlen(str), &sz);
	SelectObject(hdc, hRetFont);
	return sz.cx;
}

/*
 * fill_rect - fill a rectangle with a color
 */
static void fill_rect(const HDC hdc, const RECT *rect, const COLORREF color)
{
	SetBkColor(hdc, color);
	ExtTextOut(hdc, 0, 0, ETO_OPAQUE, rect, NULL, 0, NULL);
}

/*
 * fill_round_rect - fill a rounded rectangle (pill) with a color and an optional border
 */
static void fill_round_rect(const HDC hdc, const RECT *rect, const COLORREF color, const COLORREF border, const int radius)
{
	HBRUSH hBrush, hRetBrush;
	HPEN hPen, hRetPen;

	hBrush = CreateSolidBrush(color);
	hPen = CreatePen(PS_SOLID, 1, border);
	hRetBrush = SelectObject(hdc, hBrush);
	hRetPen = SelectObject(hdc, hPen);
	RoundRect(hdc, rect->left, rect->top, rect->right, rect->bottom, radius * 2, radius * 2);
	SelectObject(hdc, hRetPen);
	SelectObject(hdc, hRetBrush);
	DeleteObject(hPen);
	DeleteObject(hBrush);
}

/*
 * fill_circle - fill a circle
 */
static void fill_circle(const HDC hdc, const int x, const int y, const int r, const COLORREF color)
{
	HBRUSH hBrush, hRetBrush;
	HPEN hRetPen;

	hBrush = CreateSolidBrush(color);
	hRetBrush = SelectObject(hdc, hBrush);
	hRetPen = SelectObject(hdc, GetStockObject(NULL_PEN));
	Ellipse(hdc, x - r, y - r, x + r + 1, y + r + 1);
	SelectObject(hdc, hRetPen);
	SelectObject(hdc, hRetBrush);
	DeleteObject(hBrush);
}

/*
 * draw_text - draw a single line of text
 */
static void draw_text(const HDC hdc, const HFONT hFont, const TCHAR *str, RECT *rect, const COLORREF color, const UINT format)
{
	HFONT hRetFont;

	hRetFont = SelectObject(hdc, hFont);
	SetTextColor(hdc, color);
	SetBkMode(hdc, TRANSPARENT);
	DrawText(hdc, str, -1, rect, DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER | format);
	SelectObject(hdc, hRetFont);
}

/*
 * list_free_fonts - free the fonts of the list
 */
static void list_free_fonts(LIST_DATA *ld)
{
	if (ld->hNameFont != NULL) {
		DeleteObject(ld->hNameFont);
	}
	if (ld->hSmallFont != NULL) {
		DeleteObject(ld->hSmallFont);
	}
	if (ld->hTagFont != NULL) {
		DeleteObject(ld->hTagFont);
	}
	ld->hNameFont = ld->hSmallFont = ld->hTagFont = NULL;
}

/*
 * list_set_font - make the fonts of the list from the font of the dialog
 */
static void list_set_font(const HWND hWnd, LIST_DATA *ld, const HFONT hFont)
{
	list_free_fonts(ld);
	ld->hNameFont = create_font(hFont, 5, 4);
	ld->hSmallFont = create_font(hFont, 1, 1);
	ld->hTagFont = create_font(hFont, 9, 10);
	ld->name_height = font_height(hWnd, ld->hNameFont);
	ld->small_height = font_height(hWnd, ld->hSmallFont);
	ld->tag_height = font_height(hWnd, ld->hTagFont);
}

/*
 * info_height - height of the line of the time, the author and the genres
 */
static int info_height(const LIST_DATA *ld)
{
	int tag = ld->tag_height + Scale(TAG_PADDING_Y) * 2;
	return (tag > ld->small_height) ? tag : ld->small_height;
}

/*
 * name_height - height of the line of the name
 */
static int name_height(const LIST_DATA *ld, const ONLINE_ITEM *item)
{
	int badge = ld->small_height + Scale(BADGE_PADDING_Y) * 2;
	return (item->current && badge > ld->name_height) ? badge : ld->name_height;
}

/*
 * item_height - height of an item including its bottom line (index == count for "read more")
 */
static int item_height(const LIST_DATA *ld, const int index)
{
	const ONLINE_ITEM *item;
	int height;

	if (index >= ld->count) {
		return Scale(MORE_PADDING) * 2 + ld->name_height + 1;
	}
	item = ld->items[index];
	height = Scale(ITEM_PADDING) * 2 + name_height(ld, item) + Scale(LINE_GAP) + info_height(ld) + 1;
	if (item->memo != NULL && *item->memo != TEXT('\0')) {
		height += Scale(LINE_GAP) + ld->small_height;
	}
	return height;
}

/*
 * row_count - number of rows including "read more"
 */
static int row_count(const LIST_DATA *ld)
{
	return ld->count + ((ld->more) ? 1 : 0);
}

/*
 * item_top - position of an item in the whole list
 */
static int item_top(const LIST_DATA *ld, const int index)
{
	int i, y = 0;

	for (i = 0; i < index; i++) {
		y += item_height(ld, i);
	}
	return y;
}

/*
 * content_height - height of the whole list
 */
static int content_height(const LIST_DATA *ld)
{
	return item_top(ld, row_count(ld));
}

/*
 * list_set_scrollbar - update the scroll bar
 */
static void list_set_scrollbar(const HWND hWnd, LIST_DATA *ld)
{
	SCROLLINFO si;
	RECT rect;
	int height = content_height(ld);

	GetClientRect(hWnd, &rect);
	if (ld->scroll > height - rect.bottom) {
		ld->scroll = height - rect.bottom;
	}
	if (ld->scroll < 0) {
		ld->scroll = 0;
	}
	ZeroMemory(&si, sizeof(SCROLLINFO));
	si.cbSize = sizeof(SCROLLINFO);
	si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
	si.nMin = 0;
	si.nMax = (height > 0) ? height - 1 : 0;
	si.nPage = rect.bottom;
	si.nPos = ld->scroll;
	SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
}

/*
 * list_scroll_to - scroll the list
 */
static void list_scroll_to(const HWND hWnd, LIST_DATA *ld, const int pos)
{
	ld->scroll = pos;
	list_set_scrollbar(hWnd, ld);
	InvalidateRect(hWnd, NULL, FALSE);
}

/*
 * list_ensure_visible - scroll so that the item can be seen
 */
static void list_ensure_visible(const HWND hWnd, LIST_DATA *ld, const int index)
{
	RECT rect;
	int top, bottom;

	if (index < 0 || index >= row_count(ld)) {
		return;
	}
	GetClientRect(hWnd, &rect);
	top = item_top(ld, index);
	bottom = top + item_height(ld, index);
	if (top < ld->scroll) {
		list_scroll_to(hWnd, ld, top);
	} else if (bottom > ld->scroll + rect.bottom) {
		list_scroll_to(hWnd, ld, bottom - rect.bottom);
	}
}

/*
 * menu_rect - area of the menu button of an item
 */
static void menu_rect(const RECT *item_rect, RECT *rect)
{
	int cy = item_rect->top + (item_rect->bottom - 1 - item_rect->top) / 2;

	rect->right = item_rect->right;
	rect->left = rect->right - Scale(MENU_SIZE);
	rect->top = cy - Scale(MENU_SIZE) / 2;
	rect->bottom = rect->top + Scale(MENU_SIZE);
}

/*
 * item_layout - position of the lines and the genre badges of an item
 */
static int item_layout(const HDC hdc, const LIST_DATA *ld, const ONLINE_ITEM *item, const RECT *item_rect,
	RECT *name_rect, RECT *memo_rect, RECT *info_rect, RECT *tag_rects)
{
	int x, y, right, n = 0, i;

	right = item_rect->right - Scale(ITEM_PADDING_RIGHT);
	y = item_rect->top + Scale(ITEM_PADDING);
	SetRect(name_rect, item_rect->left + Scale(ITEM_PADDING), y, right, y + name_height(ld, item));
	y = name_rect->bottom;
	if (item->memo != NULL && *item->memo != TEXT('\0')) {
		y += Scale(LINE_GAP);
		SetRect(memo_rect, name_rect->left, y, right, y + ld->small_height);
		y = memo_rect->bottom;
	} else {
		SetRectEmpty(memo_rect);
	}
	y += Scale(LINE_GAP);
	SetRect(info_rect, name_rect->left, y, right, y + info_height(ld));

	// the genres follow the time and the author
	x = info_rect->left + text_width(hdc, ld->hSmallFont, item->time);
	if (*item->author != TEXT('\0')) {
		x += Scale(AUTHOR_MARGIN) + text_width(hdc, ld->hSmallFont, item->author);
	}
	for (i = 0; i < item->tag_count; i++) {
		int width = text_width(hdc, ld->hTagFont, item->tag_labels[i]) + Scale(TAG_PADDING_X) * 2;
		int height = ld->tag_height + Scale(TAG_PADDING_Y) * 2;
		x += Scale(TAG_MARGIN);
		if (x + width > right) {
			break;
		}
		tag_rects[i].left = x;
		tag_rects[i].right = x + width;
		tag_rects[i].top = info_rect->top + (info_rect->bottom - info_rect->top - height) / 2;
		tag_rects[i].bottom = tag_rects[i].top + height;
		x += width;
		n++;
	}
	return n;
}

/*
 * list_hit - part of the list at a point
 */
static HIT list_hit(const HWND hWnd, const LIST_DATA *ld, const POINT pt)
{
	HIT hit = {-1, PART_NONE, -1};
	RECT client, rect, name_rect, memo_rect, info_rect, menu, tag_rects[ONLINE_MAX_TAGS];
	int i, y, rows = row_count(ld);

	GetClientRect(hWnd, &client);
	if (!PtInRect(&client, pt)) {
		return hit;
	}
	y = -ld->scroll;
	for (i = 0; i < rows; i++) {
		int height = item_height(ld, i);
		if (pt.y >= y && pt.y < y + height) {
			hit.index = i;
			if (i >= ld->count) {
				hit.part = PART_MORE;
				return hit;
			}
			hit.part = PART_ITEM;
			SetRect(&rect, 0, y, client.right, y + height);
			if (ld->items[i]->menu) {
				menu_rect(&rect, &menu);
				if (PtInRect(&menu, pt)) {
					hit.part = PART_MENU;
					return hit;
				}
			}
			{
				HDC hdc = GetDC(hWnd);
				int n = item_layout(hdc, ld, ld->items[i], &rect, &name_rect, &memo_rect, &info_rect, tag_rects);
				int j;
				ReleaseDC(hWnd, hdc);
				for (j = 0; j < n; j++) {
					if (PtInRect(&tag_rects[j], pt)) {
						// the badge of the genre already chosen is a part of the item
						if (*ld->sel_tag == TEXT('\0') || lstrcmp(ld->items[i]->tags[j], ld->sel_tag) != 0) {
							hit.part = PART_TAG;
							hit.tag = j;
						}
						break;
					}
				}
			}
			return hit;
		}
		y += height;
	}
	return hit;
}

/*
 * same_hit - two hits point to the same part
 */
static BOOL same_hit(const HIT *a, const HIT *b)
{
	return (a->index == b->index && a->part == b->part && a->tag == b->tag);
}

/*
 * list_draw_item - draw an item
 */
static void list_draw_item(const HWND hWnd, const HDC hdc, const LIST_DATA *ld, const int index, const RECT *rect)
{
	const ONLINE_ITEM *item;
	RECT name_rect, memo_rect, info_rect, tag_rects[ONLINE_MAX_TAGS], r;
	COLORREF back = COLOR_BACK;
	int n, i, x;

	if (ld->press.index == index) {
		back = COLOR_PRESS;
	} else if (ld->hover.index == index || (ld->focus == index && GetFocus() == hWnd)) {
		back = COLOR_HOVER;
	}
	r = *rect;
	r.bottom--;
	fill_rect(hdc, &r, back);
	r.top = r.bottom;
	r.bottom++;
	fill_rect(hdc, &r, COLOR_LINE);

	if (index >= ld->count) {
		r = *rect;
		r.bottom--;
		draw_text(hdc, ld->hNameFont, (ld->more_text != NULL) ? ld->more_text : TEXT(""), &r, COLOR_SUB_TEXT, DT_CENTER);
		return;
	}
	item = ld->items[index];
	n = item_layout(hdc, ld, item, rect, &name_rect, &memo_rect, &info_rect, tag_rects);

	// name (and the badge of the current version)
	if (item->current && ld->current_text != NULL) {
		int badge = text_width(hdc, ld->hSmallFont, ld->current_text) + Scale(BADGE_PADDING_X) * 2;
		int name = text_width(hdc, ld->hNameFont, item->name);
		r = name_rect;
		if (r.left + name + Scale(BADGE_MARGIN) + badge > name_rect.right) {
			r.right = name_rect.right - Scale(BADGE_MARGIN) - badge;
		} else {
			r.right = r.left + name;
		}
		draw_text(hdc, ld->hNameFont, item->name, &r, (item->private_mode) ? COLOR_SUB_TEXT : COLOR_TEXT, DT_LEFT | DT_END_ELLIPSIS);
		r.left = r.right + Scale(BADGE_MARGIN);
		r.right = r.left + badge;
		fill_round_rect(hdc, &r, COLOR_CURRENT, COLOR_CURRENT, (r.bottom - r.top) / 2);
		draw_text(hdc, ld->hSmallFont, ld->current_text, &r, COLOR_BACK, DT_CENTER);
	} else {
		r = name_rect;
		draw_text(hdc, ld->hNameFont, item->name, &r, (item->private_mode) ? COLOR_SUB_TEXT : COLOR_TEXT, DT_LEFT | DT_END_ELLIPSIS);
	}

	// memo
	if (!IsRectEmpty(&memo_rect)) {
		r = memo_rect;
		draw_text(hdc, ld->hSmallFont, item->memo, &r, COLOR_MEMO, DT_LEFT | DT_END_ELLIPSIS);
	}

	// time, author and genres
	r = info_rect;
	draw_text(hdc, ld->hSmallFont, item->time, &r, COLOR_SUB_TEXT, DT_LEFT | DT_END_ELLIPSIS);
	x = info_rect.left + text_width(hdc, ld->hSmallFont, item->time);
	if (*item->author != TEXT('\0') && x < info_rect.right) {
		r = info_rect;
		r.left = x + Scale(AUTHOR_MARGIN);
		draw_text(hdc, ld->hSmallFont, item->author, &r, COLOR_SUB_TEXT, DT_LEFT | DT_END_ELLIPSIS);
	}
	for (i = 0; i < n; i++) {
		BOOL hover = (ld->hover.index == index && ld->hover.part == PART_TAG && ld->hover.tag == i);
		COLORREF color = (hover) ? COLOR_TAG_HOVER : COLOR_TAG_BACK;
		r = tag_rects[i];
		fill_round_rect(hdc, &r, color, color, Scale(10));
		draw_text(hdc, ld->hTagFont, item->tag_labels[i], &r, COLOR_TAG_TEXT, DT_CENTER);
	}

	// menu button
	if (item->menu) {
		RECT menu;
		int cx, cy;
		menu_rect(rect, &menu);
		cx = menu.left + (menu.right - menu.left) / 2;
		cy = menu.top + (menu.bottom - menu.top) / 2;
		if (ld->hover.index == index && ld->hover.part == PART_MENU) {
			BOOL press = (ld->press.index == index && ld->press.part == PART_MENU);
			fill_circle(hdc, cx, cy, Scale(MENU_SIZE) / 2 - 1, (press) ? COLOR_MENU_PRESS : COLOR_MENU_HOVER);
		}
		for (i = -1; i <= 1; i++) {
			fill_circle(hdc, cx, cy + i * Scale(6), Scale(2), COLOR_MENU_DOT);
		}
	}
}

/*
 * list_draw_spinner - draw the loading indicator
 */
static void list_draw_spinner(const HDC hdc, const LIST_DATA *ld, const RECT *client)
{
	int cx = client->right / 2;
	int cy = client->bottom / 2;
	int i;

	for (i = 0; i < SPINNER_DOTS; i++) {
		double a = 3.14159265358979 * 2 * i / SPINNER_DOTS;
		int level = (i - ld->spinner + SPINNER_DOTS * 2) % SPINNER_DOTS;
		int c = 0x60 + level * (0xE0 - 0x60) / (SPINNER_DOTS - 1);
		fill_circle(hdc, cx + (int)(Scale(14) * sin(a)), cy - (int)(Scale(14) * cos(a)), Scale(3), RGB(c, c, c));
	}
}

/*
 * list_paint - draw the list
 */
static void list_paint(const HWND hWnd, LIST_DATA *ld)
{
	PAINTSTRUCT ps;
	RECT client, rect;
	HDC hdc, mdc;
	HBITMAP hBmp, hRetBmp;
	int i, y, rows = row_count(ld);

	hdc = BeginPaint(hWnd, &ps);
	GetClientRect(hWnd, &client);
	mdc = CreateCompatibleDC(hdc);
	hBmp = CreateCompatibleBitmap(hdc, (client.right > 0) ? client.right : 1, (client.bottom > 0) ? client.bottom : 1);
	hRetBmp = SelectObject(mdc, hBmp);
	fill_rect(mdc, &client, COLOR_BACK);

	y = -ld->scroll;
	for (i = 0; i < rows && y < client.bottom; i++) {
		int height = item_height(ld, i);
		if (y + height > 0) {
			SetRect(&rect, 0, y, client.right, y + height);
			list_draw_item(hWnd, mdc, ld, i, &rect);
		}
		y += height;
	}
	if (ld->loading && ld->count == 0) {
		list_draw_spinner(mdc, ld, &client);
	}

	BitBlt(hdc, 0, 0, client.right, client.bottom, mdc, 0, 0, SRCCOPY);
	SelectObject(mdc, hRetBmp);
	DeleteObject(hBmp);
	DeleteDC(mdc);
	EndPaint(hWnd, &ps);
}

/*
 * list_notify - tell the parent that something was chosen
 */
static void list_notify(const HWND hWnd, LIST_DATA *ld, const int code, const int index, const int tag)
{
	ONLINE_NOTIFY on;

	ZeroMemory(&on, sizeof(ONLINE_NOTIFY));
	on.code = code;
	on.index = index;
	on.tag = tag;
	if (code == OLN_MENU && index >= 0 && index < ld->count) {
		RECT client, rect, menu;
		int top = item_top(ld, index) - ld->scroll;
		GetClientRect(hWnd, &client);
		SetRect(&rect, 0, top, client.right, top + item_height(ld, index));
		menu_rect(&rect, &menu);
		on.pt.x = menu.left;
		on.pt.y = menu.bottom;
		ClientToScreen(hWnd, &on.pt);
	}
	SendMessage(GetParent(hWnd), WM_ONLINE_NOTIFY, GetDlgCtrlID(hWnd), (LPARAM)&on);
}

/*
 * list_activate - act on the focused row (Enter)
 */
static void list_activate(const HWND hWnd, LIST_DATA *ld, const int index)
{
	if (index < 0 || index >= row_count(ld)) {
		return;
	}
	if (index >= ld->count) {
		list_notify(hWnd, ld, OLN_MORE, index, -1);
	} else {
		list_notify(hWnd, ld, OLN_OPEN, index, -1);
	}
}

/*
 * list_set_focus - move the keyboard focus
 */
static void list_set_focus(const HWND hWnd, LIST_DATA *ld, int index)
{
	int rows = row_count(ld);

	if (rows == 0) {
		ld->focus = -1;
		return;
	}
	if (index < 0) {
		index = 0;
	}
	if (index >= rows) {
		index = rows - 1;
	}
	ld->focus = index;
	list_ensure_visible(hWnd, ld, index);
	InvalidateRect(hWnd, NULL, FALSE);
}

/*
 * list_proc - window procedure of the list
 */
static LRESULT CALLBACK list_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	LIST_DATA *ld = (LIST_DATA *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	HIT hit;
	POINT pt;
	int i;

	switch (msg) {
	case WM_CREATE:
		if ((ld = mem_calloc(sizeof(LIST_DATA))) == NULL) {
			return -1;
		}
		ld->focus = -1;
		ld->hover.index = ld->press.index = -1;
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)ld);
		list_set_font(hWnd, ld, NULL);
		list_set_scrollbar(hWnd, ld);
		break;

	case WM_DESTROY:
		if (ld == NULL) {
			break;
		}
		KillTimer(hWnd, TIMER_SPINNER);
		for (i = 0; i < ld->count; i++) {
			online_item_free(ld->items[i]);
		}
		mem_free((void **)&ld->items);
		mem_free(&ld->more_text);
		mem_free(&ld->current_text);
		list_free_fonts(ld);
		mem_free(&ld);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
		break;

	case WM_SETFONT:
		list_set_font(hWnd, ld, (HFONT)wParam);
		list_set_scrollbar(hWnd, ld);
		if (LOWORD(lParam)) {
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;

	case WM_SIZE:
		list_set_scrollbar(hWnd, ld);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT:
		list_paint(hWnd, ld);
		break;

	case WM_NCPAINT:
		// the border is gray like the web version
		DefWindowProc(hWnd, msg, wParam, lParam);
		{
			HDC hdc = GetWindowDC(hWnd);
			HBRUSH hBrush = CreateSolidBrush(COLOR_BORDER);
			RECT rect;
			GetWindowRect(hWnd, &rect);
			OffsetRect(&rect, -rect.left, -rect.top);
			FrameRect(hdc, &rect, hBrush);
			DeleteObject(hBrush);
			ReleaseDC(hWnd, hdc);
		}
		return 0;

	case WM_TIMER:
		if (wParam == TIMER_SPINNER) {
			ld->spinner = (ld->spinner + 1) % SPINNER_DOTS;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;

	case WM_VSCROLL:
		{
			SCROLLINFO si;
			RECT rect;
			GetClientRect(hWnd, &rect);
			ZeroMemory(&si, sizeof(SCROLLINFO));
			si.cbSize = sizeof(SCROLLINFO);
			si.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_VERT, &si);
			switch (LOWORD(wParam)) {
			case SB_LINEUP: i = ld->scroll - Scale(LINE_SCROLL); break;
			case SB_LINEDOWN: i = ld->scroll + Scale(LINE_SCROLL); break;
			case SB_PAGEUP: i = ld->scroll - rect.bottom; break;
			case SB_PAGEDOWN: i = ld->scroll + rect.bottom; break;
			case SB_THUMBTRACK: case SB_THUMBPOSITION: i = si.nTrackPos; break;
			case SB_TOP: i = 0; break;
			case SB_BOTTOM: i = content_height(ld); break;
			default: i = ld->scroll; break;
			}
			list_scroll_to(hWnd, ld, i);
		}
		break;

	case WM_MOUSEWHEEL:
		{
			UINT lines = 3;
			SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
			if (lines == WHEEL_PAGESCROLL) {
				RECT rect;
				GetClientRect(hWnd, &rect);
				i = rect.bottom;
			} else {
				i = lines * Scale(LINE_SCROLL) / 2;
			}
			list_scroll_to(hWnd, ld, ld->scroll - GET_WHEEL_DELTA_WPARAM(wParam) * i / WHEEL_DELTA);
		}
		break;

	case WM_MOUSEMOVE:
		if (!ld->tracking) {
			TRACKMOUSEEVENT tme;
			tme.cbSize = sizeof(TRACKMOUSEEVENT);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = hWnd;
			tme.dwHoverTime = 0;
			ld->tracking = TrackMouseEvent(&tme);
		}
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
		hit = list_hit(hWnd, ld, pt);
		if (!same_hit(&hit, &ld->hover)) {
			ld->hover = hit;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;

	case WM_MOUSELEAVE:
		ld->tracking = FALSE;
		ld->hover.index = -1;
		ld->hover.part = PART_NONE;
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT && ld->hover.part != PART_NONE) {
			SetCursor(LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);

	case WM_LBUTTONDOWN:
		SetFocus(hWnd);
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
		ld->press = list_hit(hWnd, ld, pt);
		if (ld->press.index >= 0) {
			ld->focus = ld->press.index;
			SetCapture(hWnd);
		}
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_LBUTTONUP:
		if (GetCapture() != hWnd) {
			break;
		}
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
		hit = list_hit(hWnd, ld, pt);
		{
			HIT press = ld->press;
			ld->press.index = -1;
			ld->press.part = PART_NONE;
			ReleaseCapture();
			InvalidateRect(hWnd, NULL, FALSE);
			if (!same_hit(&hit, &press)) {
				break;
			}
			switch (press.part) {
			case PART_ITEM: list_notify(hWnd, ld, OLN_OPEN, press.index, -1); break;
			case PART_MENU: list_notify(hWnd, ld, OLN_MENU, press.index, -1); break;
			case PART_TAG: list_notify(hWnd, ld, OLN_TAG, press.index, press.tag); break;
			case PART_MORE: list_notify(hWnd, ld, OLN_MORE, press.index, -1); break;
			default: break;
			}
		}
		break;

	case WM_CAPTURECHANGED:
		if (ld->press.index >= 0) {
			ld->press.index = -1;
			ld->press.part = PART_NONE;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;

	case WM_CONTEXTMENU:
		if (GET_X_LPARAM(lParam) == -1 && GET_Y_LPARAM(lParam) == -1) {
			i = ld->focus;
		} else {
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);
			ScreenToClient(hWnd, &pt);
			hit = list_hit(hWnd, ld, pt);
			i = hit.index;
		}
		if (i >= 0 && i < ld->count && ld->items[i]->menu) {
			ld->focus = i;
			list_notify(hWnd, ld, OLN_MENU, i, -1);
		}
		break;

	case WM_GETDLGCODE:
		if (lParam != 0 && ((MSG *)lParam)->message == WM_KEYDOWN && ((MSG *)lParam)->wParam == VK_RETURN) {
			return DLGC_WANTARROWS | DLGC_WANTMESSAGE;
		}
		return DLGC_WANTARROWS;

	case WM_KEYDOWN:
		{
			RECT rect;
			int rows = row_count(ld);
			GetClientRect(hWnd, &rect);
			switch (wParam) {
			case VK_UP: list_set_focus(hWnd, ld, (ld->focus < 0) ? 0 : ld->focus - 1); break;
			case VK_DOWN: list_set_focus(hWnd, ld, ld->focus + 1); break;
			case VK_HOME: list_set_focus(hWnd, ld, 0); break;
			case VK_END: list_set_focus(hWnd, ld, rows - 1); break;
			case VK_PRIOR:
			case VK_NEXT:
				if (rows > 0) {
					int y = item_top(ld, (ld->focus < 0) ? 0 : ld->focus) + ((wParam == VK_PRIOR) ? -rect.bottom : rect.bottom);
					for (i = 0; i < rows - 1 && item_top(ld, i + 1) <= y; i++);
					list_set_focus(hWnd, ld, i);
				}
				break;
			case VK_RETURN: list_activate(hWnd, ld, ld->focus); break;
			}
		}
		break;

	case WM_SETFOCUS:
		if (ld->focus < 0 && row_count(ld) > 0) {
			ld->focus = 0;
		}
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_KILLFOCUS:
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case OLM_ADDITEM:
		{
			ONLINE_ITEM *item;
			if (ld->count >= ld->size) {
				ONLINE_ITEM **items = grow(ld->items, sizeof(ONLINE_ITEM *) * (ld->size + 32));
				if (items == NULL) {
					return -1;
				}
				ld->items = items;
				ld->size += 32;
			}
			if ((item = item_copy((ONLINE_ITEM *)lParam)) == NULL) {
				return -1;
			}
			ld->items[ld->count++] = item;
			list_set_scrollbar(hWnd, ld);
			InvalidateRect(hWnd, NULL, FALSE);
			return ld->count - 1;
		}

	case OLM_DELETEITEM:
		if ((int)wParam < 0 || (int)wParam >= ld->count) {
			return FALSE;
		}
		online_item_free(ld->items[wParam]);
		MoveMemory(ld->items + wParam, ld->items + wParam + 1, sizeof(ONLINE_ITEM *) * (ld->count - wParam - 1));
		ld->count--;
		if (ld->focus >= row_count(ld)) {
			ld->focus = row_count(ld) - 1;
		}
		ld->hover.index = ld->press.index = -1;
		list_set_scrollbar(hWnd, ld);
		InvalidateRect(hWnd, NULL, FALSE);
		return TRUE;

	case OLM_CLEAR:
		for (i = 0; i < ld->count; i++) {
			online_item_free(ld->items[i]);
		}
		ld->count = 0;
		ld->more = FALSE;
		ld->scroll = 0;
		ld->focus = -1;
		ld->hover.index = ld->press.index = -1;
		list_set_scrollbar(hWnd, ld);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case OLM_GETITEM:
		if ((int)wParam < 0 || (int)wParam >= ld->count) {
			return 0;
		}
		return (LRESULT)ld->items[wParam];

	case OLM_GETCOUNT:
		return ld->count;

	case OLM_FINDCID:
		for (i = 0; i < ld->count; i++) {
			if (lstrcmp(ld->items[i]->cid, (TCHAR *)lParam) == 0) {
				return i;
			}
		}
		return -1;

	case OLM_SETLOADING:
		ld->loading = (BOOL)wParam;
		if (ld->loading) {
			SetTimer(hWnd, TIMER_SPINNER, SPINNER_INTERVAL, NULL);
		} else {
			KillTimer(hWnd, TIMER_SPINNER);
		}
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case OLM_SETMORE:
		ld->more = (BOOL)wParam;
		if (ld->focus >= row_count(ld)) {
			ld->focus = row_count(ld) - 1;
		}
		list_set_scrollbar(hWnd, ld);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case OLM_SETMORETEXT:
		mem_free(&ld->more_text);
		ld->more_text = alloc_copy((TCHAR *)lParam);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case OLM_SETCURRENTTEXT:
		mem_free(&ld->current_text);
		ld->current_text = alloc_copy((TCHAR *)lParam);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case OLM_SETSELTAG:
		lstrcpyn(ld->sel_tag, (lParam != 0) ? (TCHAR *)lParam : TEXT(""), ONLINE_TAG_SIZE);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

/*
 * chips_content_width - width of all the chips
 */
static int chips_content_width(const CHIPS_DATA *cd)
{
	int i, width = 0;

	for (i = 0; i < cd->count; i++) {
		width += cd->widths[i] + Scale(CHIP_MARGIN);
	}
	return width;
}

/*
 * chips_max_scroll - how far the row can be scrolled
 */
static int chips_max_scroll(const HWND hWnd, const CHIPS_DATA *cd)
{
	RECT rect;
	int max;

	GetClientRect(hWnd, &rect);
	max = chips_content_width(cd) - rect.right;
	return (max > 0) ? max : 0;
}

/*
 * chips_scroll_to - scroll the row
 */
static void chips_scroll_to(const HWND hWnd, CHIPS_DATA *cd, int pos)
{
	int max = chips_max_scroll(hWnd, cd);

	if (pos > max) {
		pos = max;
	}
	if (pos < 0) {
		pos = 0;
	}
	cd->scroll = pos;
	InvalidateRect(hWnd, NULL, FALSE);
}

/*
 * chip_left - position of a chip in the whole row
 */
static int chip_left(const CHIPS_DATA *cd, const int index)
{
	int i, x = 0;

	for (i = 0; i < index; i++) {
		x += cd->widths[i] + Scale(CHIP_MARGIN);
	}
	return x;
}

/*
 * chips_arrows - which scroll arrows are shown
 */
static void chips_arrows(const HWND hWnd, const CHIPS_DATA *cd, BOOL *prev, BOOL *next)
{
	int max = chips_max_scroll(hWnd, cd);

	*prev = (cd->scroll > 1);
	*next = (cd->scroll < max - 1);
}

/*
 * chips_ensure_visible - scroll so that the chip is clear of the arrows
 */
static void chips_ensure_visible(const HWND hWnd, CHIPS_DATA *cd, const int index)
{
	RECT rect;
	int left, right, margin = Scale(ARROW_WIDTH);

	if (index < 0 || index >= cd->count) {
		return;
	}
	GetClientRect(hWnd, &rect);
	left = chip_left(cd, index) - cd->scroll;
	right = left + cd->widths[index];
	if (left < margin) {
		chips_scroll_to(hWnd, cd, cd->scroll - (margin - left));
	} else if (right > rect.right - margin) {
		chips_scroll_to(hWnd, cd, cd->scroll + (right - (rect.right - margin)));
	}
}

/*
 * chips_hit - chip at a point (-1 none, -2 the left arrow, -3 the right arrow)
 */
static int chips_hit(const HWND hWnd, const CHIPS_DATA *cd, const POINT pt)
{
	RECT rect;
	BOOL prev, next;
	int i, x;

	GetClientRect(hWnd, &rect);
	if (!PtInRect(&rect, pt)) {
		return -1;
	}
	chips_arrows(hWnd, cd, &prev, &next);
	if (prev && pt.x < Scale(ARROW_WIDTH)) {
		return -2;
	}
	if (next && pt.x >= rect.right - Scale(ARROW_WIDTH)) {
		return -3;
	}
	x = pt.x + cd->scroll;
	for (i = 0; i < cd->count; i++) {
		int left = chip_left(cd, i);
		int top = Scale(CHIP_ROW_PADDING);
		if (x >= left && x < left + cd->widths[i] && pt.y >= top && pt.y < top + cd->chip_height) {
			return i;
		}
	}
	return -1;
}

/*
 * fade_rect - white fading out toward the inside of the row (the ends of the chip row)
 */
static void fade_rect(const HDC hdc, const RECT *rect, const BOOL solid_right)
{
	BITMAPINFO bi;
	BLENDFUNCTION bf;
	HDC mdc;
	HBITMAP hBmp, hRetBmp;
	DWORD *bits;
	int width = rect->right - rect->left;
	int solid = width * 55 / 100;
	int x;

	if (width <= 0) {
		return;
	}
	ZeroMemory(&bi, sizeof(BITMAPINFO));
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = width;
	bi.bmiHeader.biHeight = 1;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	if ((hBmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0)) == NULL) {
		return;
	}
	for (x = 0; x < width; x++) {
		// distance from the solid side
		int d = (solid_right) ? width - 1 - x : x;
		int a = (d < solid) ? 255 : 255 - 255 * (d - solid) / (width - solid);
		bits[x] = ((DWORD)a << 24) | ((DWORD)a << 16) | ((DWORD)a << 8) | (DWORD)a;
	}
	mdc = CreateCompatibleDC(hdc);
	hRetBmp = SelectObject(mdc, hBmp);
	bf.BlendOp = AC_SRC_OVER;
	bf.BlendFlags = 0;
	bf.SourceConstantAlpha = 255;
	bf.AlphaFormat = AC_SRC_ALPHA;
	AlphaBlend(hdc, rect->left, rect->top, width, rect->bottom - rect->top, mdc, 0, 0, width, 1, bf);
	SelectObject(mdc, hRetBmp);
	DeleteDC(mdc);
	DeleteObject(hBmp);
}

/*
 * draw_chevron - draw an arrow of the chip row
 */
static void draw_chevron(const HDC hdc, const int cx, const int cy, const BOOL left, const COLORREF color)
{
	HPEN hPen, hRetPen;
	POINT pts[3];
	int w = Scale(ARROW_ICON) / 4;
	int h = Scale(ARROW_ICON) / 2 - Scale(2);

	pts[0].x = cx + ((left) ? w : -w);
	pts[0].y = cy - h;
	pts[1].x = cx + ((left) ? -w : w);
	pts[1].y = cy;
	pts[2].x = pts[0].x;
	pts[2].y = cy + h;
	hPen = CreatePen(PS_SOLID, Scale(2), color);
	hRetPen = SelectObject(hdc, hPen);
	Polyline(hdc, pts, 3);
	SelectObject(hdc, hRetPen);
	DeleteObject(hPen);
}

/*
 * chips_paint - draw the chip row
 */
static void chips_paint(const HWND hWnd, CHIPS_DATA *cd)
{
	PAINTSTRUCT ps;
	RECT client, rect;
	HDC hdc, mdc;
	HBITMAP hBmp, hRetBmp;
	BOOL prev, next;
	int i;

	hdc = BeginPaint(hWnd, &ps);
	GetClientRect(hWnd, &client);
	mdc = CreateCompatibleDC(hdc);
	hBmp = CreateCompatibleBitmap(hdc, (client.right > 0) ? client.right : 1, (client.bottom > 0) ? client.bottom : 1);
	hRetBmp = SelectObject(mdc, hBmp);
	fill_rect(mdc, &client, COLOR_BACK);

	for (i = 0; i < cd->count; i++) {
		COLORREF back = COLOR_BACK, border = COLOR_CHIP_BORDER, text = COLOR_CHIP_TEXT;
		BOOL focused = (GetFocus() == hWnd && cd->focus == i);
		rect.left = chip_left(cd, i) - cd->scroll;
		rect.right = rect.left + cd->widths[i];
		rect.top = Scale(CHIP_ROW_PADDING);
		rect.bottom = rect.top + cd->chip_height;
		if (rect.right < 0 || rect.left > client.right) {
			continue;
		}
		if (i == cd->sel) {
			back = border = COLOR_CHIP_ACTIVE;
			text = COLOR_BACK;
		} else if (cd->hover == i || focused) {
			back = COLOR_HOVER;
		}
		fill_round_rect(mdc, &rect, back, border, cd->chip_height / 2);
		draw_text(mdc, cd->hFont, cd->labels[i], &rect, text, DT_CENTER);
		if (focused && cd->key_focus) {
			RECT fr = rect;
			InflateRect(&fr, -Scale(3), -Scale(3));
			SetTextColor(mdc, (i == cd->sel) ? COLOR_BACK : COLOR_TEXT);
			SetBkColor(mdc, back);
			DrawFocusRect(mdc, &fr);
		}
	}

	chips_arrows(hWnd, cd, &prev, &next);
	if (prev) {
		SetRect(&rect, 0, 0, Scale(ARROW_WIDTH), client.bottom);
		fade_rect(mdc, &rect, FALSE);
		draw_chevron(mdc, Scale(ARROW_ICON) / 2, client.bottom / 2, TRUE, (cd->hover_arrow == 0) ? COLOR_ARROW_HOVER : COLOR_ARROW);
	}
	if (next) {
		SetRect(&rect, client.right - Scale(ARROW_WIDTH), 0, client.right, client.bottom);
		fade_rect(mdc, &rect, TRUE);
		draw_chevron(mdc, client.right - Scale(ARROW_ICON) / 2, client.bottom / 2, FALSE, (cd->hover_arrow == 1) ? COLOR_ARROW_HOVER : COLOR_ARROW);
	}

	BitBlt(hdc, 0, 0, client.right, client.bottom, mdc, 0, 0, SRCCOPY);
	SelectObject(mdc, hRetBmp);
	DeleteObject(hBmp);
	DeleteDC(mdc);
	EndPaint(hWnd, &ps);
}

/*
 * chips_measure - size of the chips
 */
static void chips_measure(const HWND hWnd, CHIPS_DATA *cd)
{
	HDC hdc;
	int i;

	cd->chip_height = font_height(hWnd, cd->hFont) + Scale(CHIP_PADDING_Y) * 2 + 2;
	hdc = GetDC(hWnd);
	for (i = 0; i < cd->count; i++) {
		cd->widths[i] = text_width(hdc, cd->hFont, cd->labels[i]) + Scale(CHIP_PADDING_X) * 2 + 2;
	}
	ReleaseDC(hWnd, hdc);
}

/*
 * chips_select - choose a chip
 */
static void chips_select(const HWND hWnd, CHIPS_DATA *cd, const int index)
{
	ONLINE_NOTIFY on;

	if (index < 0 || index >= cd->count) {
		return;
	}
	ZeroMemory(&on, sizeof(ONLINE_NOTIFY));
	on.code = OCN_SELECT;
	on.index = index;
	SendMessage(GetParent(hWnd), WM_ONLINE_NOTIFY, GetDlgCtrlID(hWnd), (LPARAM)&on);
}

/*
 * chips_proc - window procedure of the chip row
 */
static LRESULT CALLBACK chips_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	CHIPS_DATA *cd = (CHIPS_DATA *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	POINT pt;
	int i;

	switch (msg) {
	case WM_CREATE:
		if ((cd = mem_calloc(sizeof(CHIPS_DATA))) == NULL) {
			return -1;
		}
		cd->sel = cd->focus = cd->hover = cd->press = cd->hover_arrow = -1;
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)cd);
		cd->hFont = create_font(NULL, 1, 1);
		chips_measure(hWnd, cd);
		break;

	case WM_DESTROY:
		if (cd == NULL) {
			break;
		}
		for (i = 0; i < cd->count; i++) {
			mem_free(&cd->labels[i]);
		}
		mem_free((void **)&cd->labels);
		mem_free(&cd->widths);
		if (cd->hFont != NULL) {
			DeleteObject(cd->hFont);
		}
		mem_free(&cd);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
		break;

	case WM_SETFONT:
		if (cd->hFont != NULL) {
			DeleteObject(cd->hFont);
		}
		cd->hFont = create_font((HFONT)wParam, 1, 1);
		chips_measure(hWnd, cd);
		chips_scroll_to(hWnd, cd, cd->scroll);
		break;

	case WM_SIZE:
		chips_scroll_to(hWnd, cd, cd->scroll);
		break;

	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT:
		chips_paint(hWnd, cd);
		break;

	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
		if (chips_max_scroll(hWnd, cd) > 0) {
			int delta = GET_WHEEL_DELTA_WPARAM(wParam) * Scale(WHEEL_SCROLL) / WHEEL_DELTA;
			chips_scroll_to(hWnd, cd, (msg == WM_MOUSEWHEEL) ? cd->scroll - delta : cd->scroll + delta);
			return 0;
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);

	case WM_MOUSEMOVE:
		if (!cd->tracking) {
			TRACKMOUSEEVENT tme;
			tme.cbSize = sizeof(TRACKMOUSEEVENT);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = hWnd;
			tme.dwHoverTime = 0;
			cd->tracking = TrackMouseEvent(&tme);
		}
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
		if (cd->dragging) {
			int dx = pt.x - cd->drag_x;
			if (!cd->dragged && abs(dx) > Scale(DRAG_THRESHOLD)) {
				cd->dragged = TRUE;
			}
			if (cd->dragged) {
				chips_scroll_to(hWnd, cd, cd->drag_scroll - dx);
			}
			break;
		}
		i = chips_hit(hWnd, cd, pt);
		{
			int hover = (i >= 0) ? i : -1;
			int arrow = (i == -2) ? 0 : ((i == -3) ? 1 : -1);
			if (hover != cd->hover || arrow != cd->hover_arrow) {
				cd->hover = hover;
				cd->hover_arrow = arrow;
				InvalidateRect(hWnd, NULL, FALSE);
			}
		}
		break;

	case WM_MOUSELEAVE:
		cd->tracking = FALSE;
		cd->hover = cd->hover_arrow = -1;
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT && (cd->hover >= 0 || cd->hover_arrow >= 0)) {
			SetCursor(LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);

	case WM_LBUTTONDOWN:
		cd->mouse_focus = TRUE;
		SetFocus(hWnd);
		cd->mouse_focus = FALSE;
		cd->key_focus = FALSE;
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
		i = chips_hit(hWnd, cd, pt);
		if (i == -2 || i == -3) {
			RECT rect;
			GetClientRect(hWnd, &rect);
			chips_scroll_to(hWnd, cd, cd->scroll + ((i == -2) ? -1 : 1) * rect.right * 7 / 10);
			break;
		}
		cd->press = i;
		cd->dragging = TRUE;
		cd->dragged = FALSE;
		cd->drag_x = pt.x;
		cd->drag_scroll = cd->scroll;
		SetCapture(hWnd);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_LBUTTONUP:
		if (GetCapture() != hWnd) {
			break;
		}
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
		i = chips_hit(hWnd, cd, pt);
		{
			BOOL dragged = cd->dragged;
			int press = cd->press;
			cd->dragging = cd->dragged = FALSE;
			cd->press = -1;
			ReleaseCapture();
			if (!dragged && press >= 0 && press == i) {
				cd->focus = i;
				chips_select(hWnd, cd, i);
			}
		}
		break;

	case WM_CAPTURECHANGED:
		cd->dragging = cd->dragged = FALSE;
		cd->press = -1;
		break;

	case WM_GETDLGCODE:
		if (lParam != 0 && ((MSG *)lParam)->message == WM_KEYDOWN && ((MSG *)lParam)->wParam == VK_RETURN) {
			return DLGC_WANTARROWS | DLGC_WANTMESSAGE;
		}
		return DLGC_WANTARROWS;

	case WM_KEYDOWN:
		switch (wParam) {
		case VK_LEFT:
		case VK_RIGHT:
		case VK_HOME:
		case VK_END:
			if (cd->count == 0) {
				break;
			}
			i = (cd->focus < 0) ? cd->sel : cd->focus;
			if (wParam == VK_LEFT) {
				i--;
			} else if (wParam == VK_RIGHT) {
				i++;
			} else if (wParam == VK_HOME) {
				i = 0;
			} else {
				i = cd->count - 1;
			}
			if (i < 0) {
				i = 0;
			}
			if (i >= cd->count) {
				i = cd->count - 1;
			}
			cd->focus = i;
			cd->key_focus = TRUE;
			chips_ensure_visible(hWnd, cd, i);
			InvalidateRect(hWnd, NULL, FALSE);
			break;
		case VK_RETURN:
		case VK_SPACE:
			chips_select(hWnd, cd, (cd->focus < 0) ? cd->sel : cd->focus);
			break;
		}
		break;

	case WM_SETFOCUS:
		if (cd->focus < 0) {
			cd->focus = (cd->sel >= 0) ? cd->sel : 0;
		}
		cd->key_focus = !cd->mouse_focus;
		if (cd->key_focus) {
			chips_ensure_visible(hWnd, cd, cd->focus);
		}
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_KILLFOCUS:
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case OCM_ADDCHIP:
		if (cd->count >= cd->size) {
			TCHAR **labels = grow(cd->labels, sizeof(TCHAR *) * (cd->size + 16));
			int *widths;
			if (labels == NULL) {
				return -1;
			}
			cd->labels = labels;
			if ((widths = grow(cd->widths, sizeof(int) * (cd->size + 16))) == NULL) {
				return -1;
			}
			cd->widths = widths;
			cd->size += 16;
		}
		if ((cd->labels[cd->count] = alloc_copy((TCHAR *)lParam)) == NULL) {
			return -1;
		}
		cd->widths[cd->count] = 0;
		cd->count++;
		chips_measure(hWnd, cd);
		InvalidateRect(hWnd, NULL, FALSE);
		return cd->count - 1;

	case OCM_SETSEL:
		cd->sel = (int)wParam;
		cd->focus = (int)wParam;
		chips_ensure_visible(hWnd, cd, cd->sel);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case OCM_GETSEL:
		return cd->sel;

	case OCM_GETHEIGHT:
		return cd->chip_height + Scale(CHIP_ROW_PADDING) * 2;

	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

/*
 * online_view_register - register the window classes
 */
BOOL online_view_register(const HINSTANCE hInstance)
{
	WNDCLASS wc;

	ZeroMemory(&wc, sizeof(WNDCLASS));
	wc.lpfnWndProc = (WNDPROC)list_proc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszClassName = ONLINE_LIST_WND_CLASS;
	if (RegisterClass(&wc) == 0) {
		return FALSE;
	}
	wc.lpfnWndProc = (WNDPROC)chips_proc;
	wc.lpszClassName = ONLINE_CHIPS_WND_CLASS;
	return (RegisterClass(&wc) != 0);
}
/* End of source */
