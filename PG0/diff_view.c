/*
 * PG0
 *
 * diff_view.c
 *
 * View of the changes from the previous version in the revision history, drawn like the web version:
 * added lines in green, removed lines in red and changed lines in yellow, with the unchanged lines
 * away from the changes folded. The code of the lines can be selected and copied.
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <windowsx.h>
#include <tchar.h>
#include <math.h>

#include "script_memory.h"
#include "dpi.h"
#include "text_diff.h"
#include "diff_view.h"

/* Define */
#define COLOR_BACK						RGB(0xFF, 0xFF, 0xFF)
#define COLOR_TEXT						RGB(0x1F, 0x23, 0x28)
#define COLOR_BORDER					RGB(0xD0, 0xD7, 0xDE)
#define COLOR_GUTTER_TEXT				RGB(0x6E, 0x77, 0x81)
#define COLOR_INSERT					RGB(0xE6, 0xFF, 0xEC)
#define COLOR_INSERT_GUTTER				RGB(0xCC, 0xFF, 0xD8)
#define COLOR_INSERT_GUTTER_TEXT		RGB(0x1A, 0x7F, 0x37)
#define COLOR_DELETE					RGB(0xFF, 0xEB, 0xE9)
#define COLOR_DELETE_GUTTER				RGB(0xFF, 0xD7, 0xD5)
#define COLOR_DELETE_GUTTER_TEXT		RGB(0xCF, 0x22, 0x2E)
#define COLOR_CHANGE					RGB(0xFF, 0xF8, 0xC5)
#define COLOR_CHANGE_GUTTER				RGB(0xFD, 0xEC, 0xA6)
#define COLOR_CHANGE_GUTTER_TEXT		RGB(0x7D, 0x4E, 0x00)
// the changed words: rgba(255, 129, 130, 0.45) and rgba(74, 194, 107, 0.45) of the web version on COLOR_CHANGE
#define COLOR_CHANGE_OLD_WORD			RGB(0xFF, 0xC2, 0xA7)
#define COLOR_CHANGE_NEW_WORD			RGB(0xAE, 0xE0, 0x9D)
#define COLOR_FOLD						RGB(0xDD, 0xF4, 0xFF)
#define COLOR_FOLD_HOVER				RGB(0xB6, 0xE3, 0xFF)
#define COLOR_FOLD_TEXT					RGB(0x57, 0x60, 0x6A)
#define COLOR_FOCUS						RGB(0x09, 0x69, 0xDA)

#define TIMER_SPINNER					1
#define TIMER_SCROLL					2
#define SPINNER_INTERVAL				80
#define SPINNER_DOTS					8
#define SCROLL_INTERVAL					50

// unchanged lines kept in view next to each change; the rest of them fold away
#define CONTEXT							3
#define TAB_SIZE						4
#define LABEL_SIZE						64

// sizes in pixels at 96 DPI
#define DEFAULT_FONT_SIZE				16
#define NUM_PADDING						8
#define MARK_PADDING					6
#define CODE_PADDING_RIGHT				16
#define FOLD_PADDING					8
#define CHEVRON_WIDTH					2
#define FOCUS_WIDTH						2
#define KEY_SCROLL						40

/* Global Variables */
// a row shown as it is, or a run of unchanged rows folded behind a bar
typedef struct _SEGMENT {
	int start;
	int count;
	BOOL fold;
	BOOL open;
} SEGMENT;

// a line of the view: a row, or the bar of a fold
typedef struct _ENTRY {
	int row;				// the row, or the first row of the fold
	int seg;				// the fold of a bar, -1 for a row
} ENTRY;

// a place in the code: a row and an offset in it
typedef struct _TEXT_POS {
	int row;
	int ch;
} TEXT_POS;

typedef struct _VIEW_DATA {
	DIFF_RESULT *dr;
	SEGMENT *segs;
	int seg_count;
	int *row_seg;			// segment of each row
	BOOL has_fold;
	ENTRY *entries;
	int entry_count;
	int *widths;			// width of the code of each row
	int *pos;				// positions of the characters of a row (see row_positions)
	int content_width;		// width of the longest row shown
	int digits;
	TCHAR fold_text[LABEL_SIZE];
	BOOL line_no;
	BOOL loading;
	int spinner;
	HFONT hFont;
	int font_height;
	int em;
	int line_height;
	int char_width;
	int space_width;
	int top;				// first entry in view
	int scroll_x;
	int wheel;
	TEXT_POS anchor;
	TEXT_POS caret;
	BOOL selecting;
	int hover;				// entry of the fold bar under the mouse (-1 for none)
	int press;				// entry of the fold bar being pressed
	int focus;				// fold with the keyboard focus (-1 for none)
	BOOL key_focus;			// the focus was moved with the keyboard, so it is drawn
	BOOL tracking;
} VIEW_DATA;

/* Local Function Prototypes */

/*
 * grow - allocate or extend a buffer (mem_realloc does not take NULL)
 */
static void *grow(void *mem, const int size)
{
	return (mem == NULL) ? mem_alloc(size) : mem_realloc(mem, size);
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
 * set_clipboard - copy a text to the clipboard
 */
static BOOL set_clipboard(const HWND hWnd, const TCHAR *str)
{
	HGLOBAL hMem;
	TCHAR *p;
	int size = sizeof(TCHAR) * (lstrlen(str) + 1);

	if ((hMem = GlobalAlloc(GMEM_MOVEABLE, size)) == NULL) {
		return FALSE;
	}
	if ((p = GlobalLock(hMem)) == NULL) {
		GlobalFree(hMem);
		return FALSE;
	}
	CopyMemory(p, str, size);
	GlobalUnlock(hMem);
	if (OpenClipboard(hWnd) == FALSE) {
		GlobalFree(hMem);
		return FALSE;
	}
	EmptyClipboard();
	if (SetClipboardData(CF_UNICODETEXT, hMem) == NULL) {
		GlobalFree(hMem);
		CloseClipboard();
		return FALSE;
	}
	CloseClipboard();
	return TRUE;
}

/*
 * gutter_width - width of the line numbers and the marks, which stay at the left edge
 */
static int gutter_width(const VIEW_DATA *vd)
{
	int width = vd->char_width + Scale(MARK_PADDING) * 2;

	if (vd->line_no) {
		width += (vd->digits * vd->char_width + Scale(NUM_PADDING) * 2) * 2;
	}
	return width;
}

/*
 * page_lines - number of lines in view
 */
static int page_lines(const HWND hWnd, const VIEW_DATA *vd)
{
	RECT rect;

	GetClientRect(hWnd, &rect);
	return (rect.bottom / vd->line_height > 0) ? rect.bottom / vd->line_height : 1;
}

/*
 * row_visible - the row is not folded away
 */
static BOOL row_visible(const VIEW_DATA *vd, const int row)
{
	const SEGMENT *seg = &vd->segs[vd->row_seg[row]];

	return (!seg->fold || seg->open);
}

/*
 * row_positions - positions of the characters of a row from the left of the code (pos[0..len])
 */
static void row_positions(const HDC hdc, const VIEW_DATA *vd, const DIFF_ROW *row, int *pos)
{
	SIZE sz;
	int tab = vd->space_width * TAB_SIZE;
	int i = 0, s, j;

	pos[0] = 0;
	while (i < row->len) {
		if (row->text[i] == TEXT('\t')) {
			pos[i + 1] = (pos[i] / tab + 1) * tab;
			i++;
			continue;
		}
		for (s = i; i < row->len && row->text[i] != TEXT('\t'); i++);
		// the characters between tabs are measured together, as they are drawn
		if (GetTextExtentExPoint(hdc, row->text + s, i - s, 0, NULL, pos + s + 1, &sz) == FALSE) {
			for (j = s + 1; j <= i; j++) {
				pos[j] = (j - s) * vd->char_width;
			}
		}
		for (j = s + 1; j <= i; j++) {
			pos[j] += pos[s];
		}
	}
}

/*
 * view_free_result - free the comparison shown
 */
static void view_free_result(VIEW_DATA *vd)
{
	text_diff_free(vd->dr);
	vd->dr = NULL;
	mem_free((void **)&vd->segs);
	mem_free((void **)&vd->row_seg);
	mem_free((void **)&vd->entries);
	mem_free((void **)&vd->widths);
	mem_free((void **)&vd->pos);
	vd->seg_count = vd->entry_count = 0;
	vd->has_fold = FALSE;
	vd->content_width = 0;
}

/*
 * view_update_width - width of the longest row shown (the folded rows do not widen the view)
 */
static void view_update_width(VIEW_DATA *vd)
{
	int i;

	vd->content_width = 0;
	if (vd->dr == NULL || vd->widths == NULL) {
		return;
	}
	for (i = 0; i < vd->dr->count; i++) {
		if (vd->widths[i] > vd->content_width && row_visible(vd, i)) {
			vd->content_width = vd->widths[i];
		}
	}
}

/*
 * view_measure - width of each row
 */
static void view_measure(const HWND hWnd, VIEW_DATA *vd)
{
	HDC hdc;
	HFONT hRetFont;
	int i, max_len = 0;

	mem_free((void **)&vd->widths);
	mem_free((void **)&vd->pos);
	vd->content_width = 0;
	if (vd->dr == NULL) {
		return;
	}
	for (i = 0; i < vd->dr->count; i++) {
		if (vd->dr->rows[i].len > max_len) {
			max_len = vd->dr->rows[i].len;
		}
	}
	vd->widths = mem_alloc(sizeof(int) * (vd->dr->count + 1));
	vd->pos = mem_alloc(sizeof(int) * (max_len + 1));
	if (vd->widths == NULL || vd->pos == NULL) {
		mem_free((void **)&vd->widths);
		mem_free((void **)&vd->pos);
		return;
	}
	hdc = GetDC(hWnd);
	hRetFont = SelectObject(hdc, vd->hFont);
	for (i = 0; i < vd->dr->count; i++) {
		row_positions(hdc, vd, &vd->dr->rows[i], vd->pos);
		vd->widths[i] = vd->pos[vd->dr->rows[i].len];
	}
	SelectObject(hdc, hRetFont);
	ReleaseDC(hWnd, hdc);
	view_update_width(vd);
}

/*
 * view_set_font - font of the code
 */
static void view_set_font(const HWND hWnd, VIEW_DATA *vd, const HFONT hFont)
{
	LOGFONT lf;
	TEXTMETRIC tm;
	SIZE sz;
	HDC hdc;
	HFONT hRetFont;

	if (vd->hFont != NULL) {
		DeleteObject(vd->hFont);
		vd->hFont = NULL;
	}
	if (hFont != NULL && GetObject(hFont, sizeof(LOGFONT), &lf) != 0) {
		vd->hFont = CreateFontIndirect(&lf);
	}
	if (vd->hFont == NULL) {
		ZeroMemory(&lf, sizeof(LOGFONT));
		lf.lfHeight = -Scale(DEFAULT_FONT_SIZE);
		lf.lfCharSet = DEFAULT_CHARSET;
		lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
		vd->hFont = CreateFontIndirect(&lf);
	}
	hdc = GetDC(hWnd);
	hRetFont = SelectObject(hdc, vd->hFont);
	GetTextMetrics(hdc, &tm);
	GetTextExtentPoint32(hdc, TEXT("0"), 1, &sz);
	vd->char_width = (sz.cx > 0) ? sz.cx : 1;
	GetTextExtentPoint32(hdc, TEXT(" "), 1, &sz);
	vd->space_width = (sz.cx > 0) ? sz.cx : 1;
	SelectObject(hdc, hRetFont);
	ReleaseDC(hWnd, hdc);
	vd->font_height = tm.tmHeight;
	vd->em = tm.tmHeight - tm.tmInternalLeading;
	// line-height of the web version is 1.5
	vd->line_height = vd->em * 3 / 2;
	if (vd->line_height < vd->font_height) {
		vd->line_height = vd->font_height;
	}
	if (vd->line_height <= 0) {
		vd->line_height = 1;
	}
	view_measure(hWnd, vd);
}

/*
 * view_set_scrollbars - update the scroll bars (the vertical one counts lines, the horizontal one pixels)
 */
static void view_set_scrollbars(const HWND hWnd, VIEW_DATA *vd)
{
	SCROLLINFO si;
	RECT rect;
	int page = page_lines(hWnd, vd);
	int width = (vd->dr != NULL) ? gutter_width(vd) + vd->content_width + Scale(CODE_PADDING_RIGHT) : 0;

	GetClientRect(hWnd, &rect);
	if (vd->top > vd->entry_count - page) {
		vd->top = vd->entry_count - page;
	}
	if (vd->top < 0) {
		vd->top = 0;
	}
	if (vd->scroll_x > width - rect.right) {
		vd->scroll_x = width - rect.right;
	}
	if (vd->scroll_x < 0) {
		vd->scroll_x = 0;
	}
	ZeroMemory(&si, sizeof(SCROLLINFO));
	si.cbSize = sizeof(SCROLLINFO);
	si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
	si.nMin = 0;
	si.nMax = (vd->entry_count > 0) ? vd->entry_count - 1 : 0;
	si.nPage = page;
	si.nPos = vd->top;
	SetScrollInfo(hWnd, SB_VERT, &si, TRUE);

	// showing or hiding this one sizes the view again
	si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	si.nMax = (width > 0) ? width - 1 : 0;
	si.nPage = rect.right;
	si.nPos = vd->scroll_x;
	SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);
}

/*
 * view_scroll - scroll the view
 */
static void view_scroll(const HWND hWnd, VIEW_DATA *vd, const int top, const int x)
{
	vd->top = top;
	vd->scroll_x = x;
	view_set_scrollbars(hWnd, vd);
	InvalidateRect(hWnd, NULL, FALSE);
}

/*
 * view_build_entries - lines of the view from the folds open and closed
 */
static BOOL view_build_entries(VIEW_DATA *vd)
{
	ENTRY *entries;
	int i, c, n = 0;

	for (i = 0; i < vd->seg_count; i++) {
		n += (vd->segs[i].fold && vd->segs[i].open) ? vd->segs[i].count + 1 : 1;
	}
	if ((entries = grow(vd->entries, sizeof(ENTRY) * (n + 1))) == NULL) {
		return FALSE;
	}
	vd->entries = entries;
	vd->entry_count = 0;
	for (i = 0; i < vd->seg_count; i++) {
		const SEGMENT *seg = &vd->segs[i];
		vd->entries[vd->entry_count].row = seg->start;
		vd->entries[vd->entry_count].seg = (seg->fold) ? i : -1;
		vd->entry_count++;
		if (seg->fold && seg->open) {
			for (c = 0; c < seg->count; c++) {
				vd->entries[vd->entry_count].row = seg->start + c;
				vd->entries[vd->entry_count].seg = -1;
				vd->entry_count++;
			}
		}
	}
	return TRUE;
}

/*
 * view_build_segments - fold the runs of unchanged rows away from the changes
 */
static BOOL view_build_segments(VIEW_DATA *vd)
{
	const DIFF_RESULT *dr = vd->dr;
	BOOL *keep;
	int i, c, end;

	keep = mem_calloc(sizeof(BOOL) * (dr->count + 1));
	vd->segs = mem_alloc(sizeof(SEGMENT) * (dr->count + 1));
	vd->row_seg = mem_alloc(sizeof(int) * (dr->count + 1));
	if (keep == NULL || vd->segs == NULL || vd->row_seg == NULL) {
		mem_free((void **)&keep);
		return FALSE;
	}
	for (i = 0; i < dr->count; i++) {
		if (dr->rows[i].type == DIFF_EQUAL) {
			continue;
		}
		for (c = (i - CONTEXT > 0) ? i - CONTEXT : 0; c <= i + CONTEXT && c < dr->count; c++) {
			keep[c] = TRUE;
		}
	}
	vd->seg_count = 0;
	for (i = 0; i < dr->count; i = end) {
		SEGMENT *seg = &vd->segs[vd->seg_count];
		for (end = i; end < dr->count && !keep[end]; end++);
		seg->start = i;
		seg->open = FALSE;
		if (end - i >= 2) {
			seg->count = end - i;
			seg->fold = TRUE;
			vd->has_fold = TRUE;
		} else {
			seg->count = 1;
			seg->fold = FALSE;
			end = i + 1;
		}
		for (c = i; c < end; c++) {
			vd->row_seg[c] = vd->seg_count;
		}
		vd->seg_count++;
	}
	mem_free((void **)&keep);
	return TRUE;
}

/*
 * view_set_result - show a comparison (the view frees it)
 */
static void view_set_result(const HWND hWnd, VIEW_DATA *vd, DIFF_RESULT *dr)
{
	int i, last = 1;

	view_free_result(vd);
	vd->top = vd->scroll_x = 0;
	vd->anchor.row = vd->anchor.ch = vd->caret.row = vd->caret.ch = 0;
	vd->hover = vd->press = vd->focus = -1;
	vd->key_focus = FALSE;
	if ((vd->dr = dr) != NULL) {
		if (view_build_segments(vd) == FALSE || view_build_entries(vd) == FALSE) {
			view_free_result(vd);
		}
	}
	if (vd->dr != NULL) {
		for (i = 0; i < vd->dr->count; i++) {
			if (vd->dr->rows[i].old_num > last) {
				last = vd->dr->rows[i].old_num;
			}
			if (vd->dr->rows[i].new_num > last) {
				last = vd->dr->rows[i].new_num;
			}
		}
	}
	for (vd->digits = 1; last >= 10; last /= 10) {
		vd->digits++;
	}
	view_measure(hWnd, vd);
	view_set_scrollbars(hWnd, vd);
	InvalidateRect(hWnd, NULL, FALSE);
}

/*
 * view_toggle_fold - open or close a fold
 */
static void view_toggle_fold(const HWND hWnd, VIEW_DATA *vd, const int seg)
{
	if (seg < 0 || seg >= vd->seg_count || !vd->segs[seg].fold) {
		return;
	}
	vd->segs[seg].open = !vd->segs[seg].open;
	if (view_build_entries(vd) == FALSE) {
		vd->segs[seg].open = !vd->segs[seg].open;
		return;
	}
	view_update_width(vd);
	view_set_scrollbars(hWnd, vd);
	InvalidateRect(hWnd, NULL, FALSE);
}

/*
 * fold_entry - line of the bar of a fold
 */
static int fold_entry(const VIEW_DATA *vd, const int seg)
{
	int i;

	for (i = 0; i < vd->entry_count; i++) {
		if (vd->entries[i].seg == seg) {
			return i;
		}
	}
	return -1;
}

/*
 * view_move_focus - move the keyboard focus to the next (or previous) fold bar, from the lines in view at first
 */
static void view_move_focus(const HWND hWnd, VIEW_DATA *vd, const BOOL back)
{
	int page = page_lines(hWnd, vd);
	int i, e, top = vd->top;

	if (!vd->has_fold) {
		return;
	}
	if (vd->focus < 0) {
		e = -1;
		if (!back) {
			for (i = vd->top; i < vd->entry_count && vd->entries[i].seg < 0; i++);
			if (i >= vd->entry_count) {
				for (i = 0; i < vd->entry_count && vd->entries[i].seg < 0; i++);
			}
		} else {
			for (i = vd->top + page - 1; i >= vd->entry_count; i--);
			for (; i >= 0 && vd->entries[i].seg < 0; i--);
			if (i < 0) {
				for (i = vd->entry_count - 1; i >= 0 && vd->entries[i].seg < 0; i--);
			}
		}
		if (i < 0 || i >= vd->entry_count) {
			return;
		}
		vd->focus = vd->entries[i].seg;
	} else {
		i = vd->focus;
		do {
			i = (i + ((back) ? -1 : 1) + vd->seg_count) % vd->seg_count;
		} while (!vd->segs[i].fold);
		vd->focus = i;
	}
	vd->key_focus = TRUE;
	if ((e = fold_entry(vd, vd->focus)) >= 0) {
		if (e < top) {
			top = e;
		} else if (e >= top + page) {
			top = e - page + 1;
		}
	}
	view_scroll(hWnd, vd, top, vd->scroll_x);
}

/*
 * entry_at - line at a height of the client area (it can be out of the lines)
 */
static int entry_at(const VIEW_DATA *vd, const int y)
{
	return vd->top + ((y >= 0) ? y / vd->line_height : -((vd->line_height - 1 - y) / vd->line_height));
}

/*
 * text_pos_at - place in the code nearest to a point of the client area
 */
static BOOL text_pos_at(const HWND hWnd, VIEW_DATA *vd, const POINT pt, TEXT_POS *ret)
{
	const DIFF_ROW *row;
	const ENTRY *entry;
	HDC hdc;
	HFONT hRetFont;
	int gutter = gutter_width(vd);
	int e, x, i;

	if (vd->dr == NULL || vd->entry_count == 0) {
		return FALSE;
	}
	e = entry_at(vd, pt.y);
	entry = &vd->entries[(e < 0) ? 0 : ((e >= vd->entry_count) ? vd->entry_count - 1 : e)];
	ret->row = entry->row;
	ret->ch = 0;
	if (entry->seg >= 0) {
		// the bar of a fold stands at the start of its rows
		return TRUE;
	}
	row = &vd->dr->rows[entry->row];
	if (e < 0) {
		return TRUE;
	}
	if (e >= vd->entry_count) {
		ret->ch = row->len;
		return TRUE;
	}
	x = ((pt.x > gutter) ? pt.x : gutter) - gutter + vd->scroll_x;
	if (vd->pos == NULL || x <= 0) {
		return TRUE;
	}
	hdc = GetDC(hWnd);
	hRetFont = SelectObject(hdc, vd->hFont);
	row_positions(hdc, vd, row, vd->pos);
	SelectObject(hdc, hRetFont);
	ReleaseDC(hWnd, hdc);
	for (i = 0; i < row->len && x >= (vd->pos[i] + vd->pos[i + 1]) / 2; i++);
	if (i > 0 && i < row->len && IS_LOW_SURROGATE(row->text[i]) && IS_HIGH_SURROGATE(row->text[i - 1])) {
		i--;
	}
	ret->ch = i;
	return TRUE;
}

/*
 * pos_cmp - order of two places in the code
 */
static int pos_cmp(const TEXT_POS *a, const TEXT_POS *b)
{
	return (a->row != b->row) ? a->row - b->row : a->ch - b->ch;
}

/*
 * has_selection - some of the code is selected
 */
static BOOL has_selection(const VIEW_DATA *vd)
{
	return (vd->dr != NULL && pos_cmp(&vd->anchor, &vd->caret) != 0);
}

/*
 * selection_range - start and end of the selection
 */
static void selection_range(const VIEW_DATA *vd, TEXT_POS *start, TEXT_POS *end)
{
	if (pos_cmp(&vd->anchor, &vd->caret) <= 0) {
		*start = vd->anchor;
		*end = vd->caret;
	} else {
		*start = vd->caret;
		*end = vd->anchor;
	}
}

/*
 * row_selection - selected characters of a row between start and end
 */
static void row_selection(const VIEW_DATA *vd, const int row, const TEXT_POS *start, const TEXT_POS *end, int *from, int *to)
{
	int len = vd->dr->rows[row].len;

	*from = (row == start->row) ? start->ch : 0;
	*to = (row == end->row) ? end->ch : len;
	if (*from > len) {
		*from = len;
	}
	if (*to > len) {
		*to = len;
	}
}

/*
 * view_copy - copy the code of the selected lines: the line numbers, the marks and the folded lines are left out
 */
static void view_copy(const HWND hWnd, VIEW_DATA *vd)
{
	TEXT_POS start, end;
	TCHAR *buf, *p;
	BOOL first = TRUE;
	int r, from, to, len = 0;

	if (!has_selection(vd)) {
		return;
	}
	selection_range(vd, &start, &end);
	for (r = start.row; r <= end.row; r++) {
		if (row_visible(vd, r)) {
			row_selection(vd, r, &start, &end, &from, &to);
			len += to - from + 2;
		}
	}
	if ((buf = mem_alloc(sizeof(TCHAR) * (len + 1))) == NULL) {
		return;
	}
	p = buf;
	for (r = start.row; r <= end.row; r++) {
		if (!row_visible(vd, r)) {
			continue;
		}
		if (!first) {
			*(p++) = TEXT('\r');
			*(p++) = TEXT('\n');
		}
		first = FALSE;
		row_selection(vd, r, &start, &end, &from, &to);
		CopyMemory(p, vd->dr->rows[r].text + from, sizeof(TCHAR) * (to - from));
		p += to - from;
	}
	*p = TEXT('\0');
	if (!first) {
		set_clipboard(hWnd, buf);
	}
	mem_free(&buf);
}

/*
 * view_select_all - select the code of all the lines
 */
static void view_select_all(const HWND hWnd, VIEW_DATA *vd)
{
	if (vd->dr == NULL || vd->dr->count == 0) {
		return;
	}
	vd->anchor.row = 0;
	vd->anchor.ch = 0;
	vd->caret.row = vd->dr->count - 1;
	vd->caret.ch = vd->dr->rows[vd->dr->count - 1].len;
	InvalidateRect(hWnd, NULL, FALSE);
}

/*
 * char_class - kind of a character for selecting a word (0: word, 1: space, 2: other)
 */
static int char_class(const TCHAR c)
{
	if ((c >= TEXT('A') && c <= TEXT('Z')) || (c >= TEXT('a') && c <= TEXT('z')) ||
		(c >= TEXT('0') && c <= TEXT('9')) || c == TEXT('_')) {
		return 0;
	}
	return (c == TEXT(' ') || c == TEXT('\t') || c == 0x3000) ? 1 : 2;
}

/*
 * view_select_word - select the word at a place
 */
static void view_select_word(const HWND hWnd, VIEW_DATA *vd, const TEXT_POS *tp)
{
	const DIFF_ROW *row = &vd->dr->rows[tp->row];
	int start, end, cls;

	vd->anchor = vd->caret = *tp;
	if (row->len > 0) {
		start = (tp->ch < row->len) ? tp->ch : row->len - 1;
		if (start > 0 && IS_LOW_SURROGATE(row->text[start]) && IS_HIGH_SURROGATE(row->text[start - 1])) {
			start--;
		}
		cls = char_class(row->text[start]);
		end = start + 1;
		if (cls == 2) {
			if (IS_HIGH_SURROGATE(row->text[start]) && end < row->len && IS_LOW_SURROGATE(row->text[end])) {
				end++;
			}
		} else {
			for (; start > 0 && char_class(row->text[start - 1]) == cls; start--);
			for (; end < row->len && char_class(row->text[end]) == cls; end++);
		}
		vd->anchor.ch = start;
		vd->caret.ch = end;
	}
	InvalidateRect(hWnd, NULL, FALSE);
}

/*
 * draw_row - draw a line of the code
 */
static void draw_row(const HDC hdc, const VIEW_DATA *vd, const int index, const RECT *rect, const TEXT_POS *start, const TEXT_POS *end)
{
	const DIFF_ROW *row = &vd->dr->rows[index];
	const TCHAR *mark = NULL;
	COLORREF back, gutter_back, gutter_text, word = COLOR_BACK;
	COLORREF sel_back = GetSysColor(COLOR_HIGHLIGHT);
	COLORREF sel_text = GetSysColor(COLOR_HIGHLIGHTTEXT);
	RECT r, fr, clip;
	TCHAR num[16];
	int gutter = gutter_width(vd);
	int x0 = gutter - vd->scroll_x;
	int text_y = rect->top + (vd->line_height - vd->font_height) / 2;
	int from = 0, to = 0, part = 0, part_end, i, j, k, x;
	BOOL eol = FALSE;

	switch (row->type) {
	case DIFF_INSERT:
		back = COLOR_INSERT;
		gutter_back = COLOR_INSERT_GUTTER;
		gutter_text = COLOR_INSERT_GUTTER_TEXT;
		mark = TEXT("+");
		break;
	case DIFF_DELETE:
		back = COLOR_DELETE;
		gutter_back = COLOR_DELETE_GUTTER;
		gutter_text = COLOR_DELETE_GUTTER_TEXT;
		mark = TEXT("-");
		break;
	case DIFF_CHANGE_OLD:
	case DIFF_CHANGE_NEW:
		back = COLOR_CHANGE;
		gutter_back = COLOR_CHANGE_GUTTER;
		gutter_text = COLOR_CHANGE_GUTTER_TEXT;
		word = (row->type == DIFF_CHANGE_OLD) ? COLOR_CHANGE_OLD_WORD : COLOR_CHANGE_NEW_WORD;
		mark = (row->type == DIFF_CHANGE_OLD) ? TEXT("-") : TEXT("+");
		break;
	default:
		back = COLOR_BACK;
		gutter_back = COLOR_BACK;
		gutter_text = COLOR_GUTTER_TEXT;
		break;
	}
	fill_rect(hdc, rect, back);

	// the code, in runs of characters drawn alike: in one part, all selected or not, and no tab
	if (vd->pos != NULL) {
		row_positions(hdc, vd, row, vd->pos);
		if (start != NULL && index >= start->row && index <= end->row) {
			row_selection(vd, index, start, end, &from, &to);
			eol = (index < end->row);
		}
		SetRect(&clip, gutter, rect->top, rect->right, rect->bottom);
		part_end = (row->part_count > 0) ? row->parts[0].start + row->parts[0].len : row->len;
		for (i = 0; i < row->len; i = j) {
			BOOL changed = (row->part_count > 0 && row->parts[part].changed);
			BOOL selected = (i >= from && i < to);
			COLORREF color = (selected) ? sel_back : ((changed) ? word : back);
			if (row->text[i] == TEXT('\t')) {
				j = i + 1;
			} else {
				for (j = i + 1; j < row->len && j < part_end && row->text[j] != TEXT('\t') && (j >= from && j < to) == selected; j++);
			}
			SetRect(&r, x0 + vd->pos[i], rect->top, x0 + vd->pos[j], rect->bottom);
			if (r.left >= clip.right) {
				break;
			}
			if (r.right > clip.left) {
				if (color != back && IntersectRect(&fr, &r, &clip)) {
					fill_rect(hdc, &fr, color);
				}
				if (row->text[i] != TEXT('\t')) {
					SetTextColor(hdc, (selected) ? sel_text : COLOR_TEXT);
					ExtTextOut(hdc, r.left, text_y, ETO_CLIPPED, &clip, row->text + i, j - i, NULL);
				}
			}
			if (j >= part_end && part + 1 < row->part_count) {
				part++;
				part_end = row->parts[part].start + row->parts[part].len;
			}
		}
		// the line break is selected too
		if (eol) {
			SetRect(&r, x0 + vd->pos[row->len], rect->top, x0 + vd->pos[row->len] + vd->space_width, rect->bottom);
			if (IntersectRect(&fr, &r, &clip)) {
				fill_rect(hdc, &fr, sel_back);
			}
		}
	}

	// the gutter stays at the left edge over the code scrolled sideways
	SetRect(&r, 0, rect->top, gutter, rect->bottom);
	fill_rect(hdc, &r, gutter_back);
	SetTextColor(hdc, gutter_text);
	x = 0;
	if (vd->line_no) {
		for (k = 0; k < 2; k++) {
			int n = (k == 0) ? row->old_num : row->new_num;
			if (n > 0) {
				SetRect(&r, x + Scale(NUM_PADDING), rect->top, x + Scale(NUM_PADDING) + vd->digits * vd->char_width, rect->bottom);
				wsprintf(num, TEXT("%d"), n);
				DrawText(hdc, num, -1, &r, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
			}
			x += vd->digits * vd->char_width + Scale(NUM_PADDING) * 2;
		}
	}
	if (mark != NULL) {
		SetRect(&r, x, rect->top, gutter, rect->bottom);
		DrawText(hdc, mark, 1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
	}
}

/*
 * draw_fold - draw the bar of a fold
 */
static void draw_fold(const HWND hWnd, const HDC hdc, const VIEW_DATA *vd, const int index, const RECT *rect)
{
	const int seg = vd->entries[index].seg;
	TCHAR label[LABEL_SIZE + 16];
	HPEN hPen, hRetPen;
	POINT pts[3];
	RECT r;
	// a corner of 0.35em with a border of 2px turned to point right or down
	int arm = vd->em * 35 / 100 + Scale(CHEVRON_WIDTH);
	int h = arm * 7 / 10;
	int left = Scale(FOLD_PADDING) + vd->em * 3 / 10;
	int cx = left + arm / 2;
	int cy = rect->top + (rect->bottom - rect->top) / 2;

	fill_rect(hdc, rect, (vd->hover == index || vd->press == index) ? COLOR_FOLD_HOVER : COLOR_FOLD);
	if (vd->segs[seg].open) {
		pts[0].x = cx - h;
		pts[0].y = cy - h / 2;
		pts[1].x = cx;
		pts[1].y = cy + h / 2;
		pts[2].x = cx + h;
		pts[2].y = cy - h / 2;
	} else {
		pts[0].x = cx - h / 2;
		pts[0].y = cy - h;
		pts[1].x = cx + h / 2;
		pts[1].y = cy;
		pts[2].x = cx - h / 2;
		pts[2].y = cy + h;
	}
	hPen = CreatePen(PS_SOLID, Scale(CHEVRON_WIDTH), COLOR_FOLD_TEXT);
	hRetPen = SelectObject(hdc, hPen);
	Polyline(hdc, pts, 3);
	SelectObject(hdc, hRetPen);
	DeleteObject(hPen);

	wsprintf(label, (*vd->fold_text != TEXT('\0')) ? vd->fold_text : TEXT("%d"), vd->segs[seg].count);
	r = *rect;
	r.left = left + arm + vd->em * 8 / 10;
	SetTextColor(hdc, COLOR_FOLD_TEXT);
	DrawText(hdc, label, -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

	if (vd->focus == seg && vd->key_focus && GetFocus() == hWnd) {
		HBRUSH hBrush = CreateSolidBrush(COLOR_FOCUS);
		int i;
		r = *rect;
		for (i = 0; i < Scale(FOCUS_WIDTH); i++) {
			FrameRect(hdc, &r, hBrush);
			InflateRect(&r, -1, -1);
		}
		DeleteObject(hBrush);
	}
}

/*
 * draw_spinner - draw the loading indicator
 */
static void draw_spinner(const HDC hdc, const VIEW_DATA *vd, const RECT *client)
{
	int cx = client->right / 2;
	int cy = client->bottom / 2;
	int i;

	for (i = 0; i < SPINNER_DOTS; i++) {
		double a = 3.14159265358979 * 2 * i / SPINNER_DOTS;
		int level = (i - vd->spinner + SPINNER_DOTS * 2) % SPINNER_DOTS;
		int c = 0x60 + level * (0xE0 - 0x60) / (SPINNER_DOTS - 1);
		fill_circle(hdc, cx + (int)(Scale(14) * sin(a)), cy - (int)(Scale(14) * cos(a)), Scale(3), RGB(c, c, c));
	}
}

/*
 * view_paint - draw the view
 */
static void view_paint(const HWND hWnd, VIEW_DATA *vd)
{
	PAINTSTRUCT ps;
	TEXT_POS start = {0, 0}, end = {0, 0};
	RECT client, rect;
	HDC hdc, mdc;
	HBITMAP hBmp, hRetBmp;
	HFONT hRetFont;
	BOOL selected = has_selection(vd);
	int i, y;

	hdc = BeginPaint(hWnd, &ps);
	GetClientRect(hWnd, &client);
	mdc = CreateCompatibleDC(hdc);
	hBmp = CreateCompatibleBitmap(hdc, (client.right > 0) ? client.right : 1, (client.bottom > 0) ? client.bottom : 1);
	hRetBmp = SelectObject(mdc, hBmp);
	hRetFont = SelectObject(mdc, vd->hFont);
	SetBkMode(mdc, TRANSPARENT);
	fill_rect(mdc, &client, COLOR_BACK);

	if (selected) {
		selection_range(vd, &start, &end);
	}
	for (i = vd->top, y = 0; i < vd->entry_count && y < client.bottom; i++, y += vd->line_height) {
		SetRect(&rect, 0, y, client.right, y + vd->line_height);
		if (vd->entries[i].seg >= 0) {
			draw_fold(hWnd, mdc, vd, i, &rect);
		} else {
			draw_row(mdc, vd, vd->entries[i].row, &rect, (selected) ? &start : NULL, &end);
		}
	}
	if (vd->loading) {
		draw_spinner(mdc, vd, &client);
	}

	BitBlt(hdc, 0, 0, client.right, client.bottom, mdc, 0, 0, SRCCOPY);
	SelectObject(mdc, hRetFont);
	SelectObject(mdc, hRetBmp);
	DeleteObject(hBmp);
	DeleteDC(mdc);
	EndPaint(hWnd, &ps);
}

/*
 * fold_at - line of the fold bar at a point (-1 when there is none)
 */
static int fold_at(const HWND hWnd, const VIEW_DATA *vd, const POINT pt)
{
	RECT rect;
	int e;

	GetClientRect(hWnd, &rect);
	if (!PtInRect(&rect, pt)) {
		return -1;
	}
	e = entry_at(vd, pt.y);
	return (e >= 0 && e < vd->entry_count && vd->entries[e].seg >= 0) ? e : -1;
}

/*
 * view_proc - window procedure of the diff view
 */
static LRESULT CALLBACK view_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	VIEW_DATA *vd = (VIEW_DATA *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	TEXT_POS tp;
	POINT pt;
	RECT rect;
	int i;

	if (vd == NULL && msg != WM_CREATE) {
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	switch (msg) {
	case WM_CREATE:
		if ((vd = mem_calloc(sizeof(VIEW_DATA))) == NULL) {
			return -1;
		}
		vd->hover = vd->press = vd->focus = -1;
		vd->line_no = TRUE;
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)vd);
		view_set_font(hWnd, vd, NULL);
		view_set_scrollbars(hWnd, vd);
		break;

	case WM_DESTROY:
		if (vd == NULL) {
			break;
		}
		KillTimer(hWnd, TIMER_SPINNER);
		KillTimer(hWnd, TIMER_SCROLL);
		view_free_result(vd);
		if (vd->hFont != NULL) {
			DeleteObject(vd->hFont);
		}
		mem_free((void **)&vd);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
		break;

	case WM_SETFONT:
		view_set_font(hWnd, vd, (HFONT)wParam);
		view_set_scrollbars(hWnd, vd);
		if (LOWORD(lParam)) {
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;

	case WM_SIZE:
		view_set_scrollbars(hWnd, vd);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT:
		view_paint(hWnd, vd);
		break;

	case WM_NCPAINT:
		// the border is gray like the web version
		DefWindowProc(hWnd, msg, wParam, lParam);
		{
			HDC hdc = GetWindowDC(hWnd);
			HBRUSH hBrush = CreateSolidBrush(COLOR_BORDER);
			GetWindowRect(hWnd, &rect);
			OffsetRect(&rect, -rect.left, -rect.top);
			FrameRect(hdc, &rect, hBrush);
			DeleteObject(hBrush);
			ReleaseDC(hWnd, hdc);
		}
		return 0;

	case WM_TIMER:
		if (wParam == TIMER_SPINNER) {
			vd->spinner = (vd->spinner + 1) % SPINNER_DOTS;
			InvalidateRect(hWnd, NULL, FALSE);
		} else if (wParam == TIMER_SCROLL && vd->selecting) {
			// the selection goes on past the edges, scrolling the view
			int top = vd->top, x = vd->scroll_x;
			GetCursorPos(&pt);
			ScreenToClient(hWnd, &pt);
			GetClientRect(hWnd, &rect);
			if (pt.y < 0) {
				top--;
			} else if (pt.y >= rect.bottom) {
				top++;
			}
			if (pt.x < gutter_width(vd)) {
				x -= vd->char_width * 2;
			} else if (pt.x >= rect.right) {
				x += vd->char_width * 2;
			}
			if (top != vd->top || x != vd->scroll_x) {
				view_scroll(hWnd, vd, top, x);
				if (text_pos_at(hWnd, vd, pt, &tp)) {
					vd->caret = tp;
				}
			}
		}
		break;

	case WM_VSCROLL:
		{
			SCROLLINFO si;
			int page = page_lines(hWnd, vd);
			ZeroMemory(&si, sizeof(SCROLLINFO));
			si.cbSize = sizeof(SCROLLINFO);
			si.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_VERT, &si);
			switch (LOWORD(wParam)) {
			case SB_LINEUP: i = vd->top - 1; break;
			case SB_LINEDOWN: i = vd->top + 1; break;
			case SB_PAGEUP: i = vd->top - page; break;
			case SB_PAGEDOWN: i = vd->top + page; break;
			case SB_THUMBTRACK: case SB_THUMBPOSITION: i = si.nTrackPos; break;
			case SB_TOP: i = 0; break;
			case SB_BOTTOM: i = vd->entry_count; break;
			default: i = vd->top; break;
			}
			view_scroll(hWnd, vd, i, vd->scroll_x);
		}
		break;

	case WM_HSCROLL:
		{
			SCROLLINFO si;
			GetClientRect(hWnd, &rect);
			ZeroMemory(&si, sizeof(SCROLLINFO));
			si.cbSize = sizeof(SCROLLINFO);
			si.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_HORZ, &si);
			switch (LOWORD(wParam)) {
			case SB_LINELEFT: i = vd->scroll_x - Scale(KEY_SCROLL); break;
			case SB_LINERIGHT: i = vd->scroll_x + Scale(KEY_SCROLL); break;
			case SB_PAGELEFT: i = vd->scroll_x - rect.right; break;
			case SB_PAGERIGHT: i = vd->scroll_x + rect.right; break;
			case SB_THUMBTRACK: case SB_THUMBPOSITION: i = si.nTrackPos; break;
			case SB_LEFT: i = 0; break;
			case SB_RIGHT: i = si.nMax; break;
			default: i = vd->scroll_x; break;
			}
			view_scroll(hWnd, vd, vd->top, i);
		}
		break;

	case WM_MOUSEWHEEL:
		if (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) {
			view_scroll(hWnd, vd, vd->top, vd->scroll_x - GET_WHEEL_DELTA_WPARAM(wParam) * Scale(KEY_SCROLL) * 3 / WHEEL_DELTA);
		} else {
			UINT lines = 3;
			int step;
			SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
			if (lines == WHEEL_PAGESCROLL) {
				lines = page_lines(hWnd, vd);
			}
			// small turns of a touchpad add up
			vd->wheel += GET_WHEEL_DELTA_WPARAM(wParam) * (int)lines;
			step = vd->wheel / WHEEL_DELTA;
			vd->wheel -= step * WHEEL_DELTA;
			if (step != 0) {
				view_scroll(hWnd, vd, vd->top - step, vd->scroll_x);
			}
		}
		break;

	case WM_MOUSEHWHEEL:
		view_scroll(hWnd, vd, vd->top, vd->scroll_x + GET_WHEEL_DELTA_WPARAM(wParam) * Scale(KEY_SCROLL) * 3 / WHEEL_DELTA);
		break;

	case WM_MOUSEMOVE:
		if (!vd->tracking) {
			TRACKMOUSEEVENT tme;
			tme.cbSize = sizeof(TRACKMOUSEEVENT);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = hWnd;
			tme.dwHoverTime = 0;
			vd->tracking = TrackMouseEvent(&tme);
		}
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
		if (vd->selecting) {
			if (text_pos_at(hWnd, vd, pt, &tp) && pos_cmp(&tp, &vd->caret) != 0) {
				vd->caret = tp;
				InvalidateRect(hWnd, NULL, FALSE);
			}
			break;
		}
		i = fold_at(hWnd, vd, pt);
		if (i != vd->hover) {
			vd->hover = i;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;

	case WM_MOUSELEAVE:
		vd->tracking = FALSE;
		if (vd->hover >= 0) {
			vd->hover = -1;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;

	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT && vd->dr != NULL) {
			GetCursorPos(&pt);
			ScreenToClient(hWnd, &pt);
			i = entry_at(vd, pt.y);
			if (i >= 0 && i < vd->entry_count) {
				if (vd->entries[i].seg >= 0) {
					SetCursor(LoadCursor(NULL, IDC_HAND));
				} else {
					SetCursor(LoadCursor(NULL, (pt.x >= gutter_width(vd)) ? IDC_IBEAM : IDC_ARROW));
				}
				return TRUE;
			}
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);

	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		SetFocus(hWnd);
		vd->key_focus = FALSE;
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
		if ((i = fold_at(hWnd, vd, pt)) >= 0) {
			// a fold bar opens or closes when it is released on it
			vd->press = i;
			vd->focus = vd->entries[i].seg;
			SetCapture(hWnd);
			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		vd->focus = -1;
		if (text_pos_at(hWnd, vd, pt, &tp) == FALSE) {
			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		if (msg == WM_LBUTTONDBLCLK) {
			view_select_word(hWnd, vd, &tp);
			break;
		}
		vd->caret = tp;
		if (!(wParam & MK_SHIFT)) {
			vd->anchor = tp;
		}
		vd->selecting = TRUE;
		SetCapture(hWnd);
		SetTimer(hWnd, TIMER_SCROLL, SCROLL_INTERVAL, NULL);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_LBUTTONUP:
		if (GetCapture() != hWnd) {
			break;
		}
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
		i = vd->press;
		ReleaseCapture();
		if (i >= 0 && fold_at(hWnd, vd, pt) == i) {
			view_toggle_fold(hWnd, vd, vd->entries[i].seg);
		}
		break;

	case WM_CAPTURECHANGED:
		KillTimer(hWnd, TIMER_SCROLL);
		vd->selecting = FALSE;
		if (vd->press >= 0) {
			vd->press = -1;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;

	case WM_GETDLGCODE:
		i = DLGC_WANTARROWS | DLGC_WANTCHARS;
		// Tab moves between the fold bars, and Enter opens or closes the one with the focus
		if (vd->has_fold) {
			i |= DLGC_WANTTAB;
		}
		if (lParam != 0 && ((MSG *)lParam)->message == WM_KEYDOWN && ((MSG *)lParam)->wParam == VK_RETURN && vd->focus >= 0) {
			i |= DLGC_WANTMESSAGE;
		}
		return i;

	case WM_KEYDOWN:
		{
			BOOL ctrl = (GetKeyState(VK_CONTROL) < 0);
			int page = page_lines(hWnd, vd);
			switch (wParam) {
			case VK_UP: view_scroll(hWnd, vd, vd->top - 1, vd->scroll_x); break;
			case VK_DOWN: view_scroll(hWnd, vd, vd->top + 1, vd->scroll_x); break;
			case VK_PRIOR: view_scroll(hWnd, vd, vd->top - page, vd->scroll_x); break;
			case VK_NEXT: view_scroll(hWnd, vd, vd->top + page, vd->scroll_x); break;
			case VK_HOME: view_scroll(hWnd, vd, 0, vd->scroll_x); break;
			case VK_END: view_scroll(hWnd, vd, vd->entry_count, vd->scroll_x); break;
			case VK_LEFT: view_scroll(hWnd, vd, vd->top, vd->scroll_x - Scale(KEY_SCROLL)); break;
			case VK_RIGHT: view_scroll(hWnd, vd, vd->top, vd->scroll_x + Scale(KEY_SCROLL)); break;
			case VK_TAB: view_move_focus(hWnd, vd, (GetKeyState(VK_SHIFT) < 0)); break;
			case VK_RETURN:
			case VK_SPACE:
				if (vd->focus >= 0) {
					view_toggle_fold(hWnd, vd, vd->focus);
				} else if (wParam == VK_SPACE) {
					view_scroll(hWnd, vd, vd->top + ((GetKeyState(VK_SHIFT) < 0) ? -page : page), vd->scroll_x);
				}
				break;
			case 'A':
				if (ctrl) {
					view_select_all(hWnd, vd);
				}
				break;
			case 'C':
			case VK_INSERT:
				if (ctrl) {
					view_copy(hWnd, vd);
				}
				break;
			}
		}
		break;

	case WM_SETFOCUS:
	case WM_KILLFOCUS:
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_COPY:
		view_copy(hWnd, vd);
		break;

	case DVM_SETRESULT:
		view_set_result(hWnd, vd, (DIFF_RESULT *)lParam);
		break;

	case DVM_SETLOADING:
		vd->loading = (BOOL)wParam;
		if (vd->loading) {
			SetTimer(hWnd, TIMER_SPINNER, SPINNER_INTERVAL, NULL);
		} else {
			KillTimer(hWnd, TIMER_SPINNER);
		}
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case DVM_SETLINENO:
		vd->line_no = (BOOL)wParam;
		view_set_scrollbars(hWnd, vd);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case DVM_SETFOLDTEXT:
		lstrcpyn(vd->fold_text, (lParam != 0) ? (TCHAR *)lParam : TEXT(""), LABEL_SIZE);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case DVM_SELECTALL:
		view_select_all(hWnd, vd);
		break;

	case DVM_HASSELECTION:
		return has_selection(vd);

	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

/*
 * diff_view_register - register the window class
 */
BOOL diff_view_register(const HINSTANCE hInstance)
{
	WNDCLASS wc;

	ZeroMemory(&wc, sizeof(WNDCLASS));
	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = (WNDPROC)view_proc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszClassName = DIFF_VIEW_WND_CLASS;
	return (RegisterClass(&wc) != 0);
}
/* End of source */
