/*
 * PG0
 *
 * nEdit.c
 *
 * Copyright (C) 1996-2021 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <tchar.h>
#ifdef OP_XP_STYLE
#include <uxtheme.h>
#include <vssym32.h>
#endif	// OP_XP_STYLE

#include "nEdit.h"
#include "script_memory.h"
#include "script_string.h"

/* Define */
#define RESERVE_BUF						1024
#define RESERVE_INPUT					256
#define RESERVE_LINE					256
#define RESERVE_UNDO					256

#define DRAW_LEN						256
#define LINE_MAX						32768
#define TAB_STOP						4

// ホイールメッセージ
#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL					0x020A
#endif
// XPテーマ変更通知
#ifndef WM_THEMECHANGED
#define WM_THEMECHANGED					0x031A
#endif

#ifndef IS_HIGH_SURROGATE
#define IS_HIGH_SURROGATE(wch)			(((wch) & 0xfc00) == 0xd800)
#define IS_LOW_SURROGATE(wch)			(((wch) & 0xfc00) == 0xdc00)
#define IS_SURROGATE_PAIR(hs, ls)		(IS_HIGH_SURROGATE(hs) && IS_LOW_SURROGATE(ls))
#endif

#ifdef UNICODE
#define IS_LEAD_TBYTE(tb)				IS_HIGH_SURROGATE(tb)
#else
#define IS_LEAD_TBYTE(tb)				IsDBCSLeadByte(tb)
#endif

#define SWAP(a, b)						{a = b - a; b -= a; a += b;}
#define BUF_LEN							(bf->buf_len + bf->input_len - bf->del_len - bf->ip_len)

#define COLOR_COMMENT					RGB(0x00, 0x80, 0x00)
#define COLOR_KEYWORD					RGB(0x00, 0x00, 0xff)
#define COLOR_EXEC						RGB(0x80, 0xff, 0xff)
#define COLOR_ERROR						RGB(0xff, 0xb6, 0xc1)
#define COLOR_STRING_D					RGB(0x80, 0x00, 0x00)
#define COLOR_STRING_S					RGB(0x80, 0x01, 0x01)

#define KEYWORD_SIZE					10

/* Global Variables */
TCHAR keyword[5][KEYWORD_SIZE] = { TEXT("var"), TEXT("exit"), TEXT("if"), TEXT("else"), TEXT("while") };
TCHAR keyword_extension[12][KEYWORD_SIZE] = { TEXT("for"), TEXT("do"), TEXT("break"), TEXT("continue"),
	TEXT("switch"), TEXT("case"), TEXT("default"),
	TEXT("return"), TEXT("function"),
	TEXT("#import"), TEXT("#library"), TEXT("#option"),
};

/* Local Function Prototypes */
static BOOL string_to_clipboard(const HWND hWnd, const TCHAR *st, const TCHAR *en);
static void notify_message(const HWND hWnd, const BUFFER *bf, const int code);
static BOOL get_edit_rect(const HWND hWnd, const BUFFER *bf, RECT *rect);
static void set_scrollbar(const HWND hWnd, BUFFER *bf);
static void show_line(const HWND hWnd, BUFFER *bf, int line, BOOL begin_line);
static void ensure_visible(const HWND hWnd, BUFFER *bf);

static BOOL is_lead_byte(const BUFFER *bf, TCHAR *p);
static int get_char_extent(BUFFER *bf, TCHAR *str, int *ret_len);
static TCHAR *char_next(const BUFFER *bf, TCHAR *p);
static TCHAR *char_prev(const BUFFER *bf, TCHAR *p);
static TCHAR *index_to_char(const BUFFER *bf, const DWORD index);
static DWORD char_to_index(const BUFFER *bf, const TCHAR *p);
static int index_to_line(const BUFFER *bf, const DWORD index);

static BOOL line_alloc(BUFFER *bf, const int i, const int size);
static BOOL line_move(BUFFER *bf, const int i, const int len);
static DWORD line_get(const BUFFER *bf, const int lindex);
static void line_set(BUFFER *bf, const int lindex, const DWORD len);
static void line_flush(BUFFER *bf);
static void line_add_length(BUFFER *bf, const int index, const int len);
static int line_get_length(const BUFFER *bf, const DWORD index);
static DWORD line_get_next_index(BUFFER *bf, const DWORD index);
static BOOL line_set_info(const HWND hWnd, BUFFER *bf);
static int line_get_count(BUFFER *bf, const int lindex);
static int line_set_count(BUFFER *bf, const int lindex, const int old_cnt, const int line_cnt);
static void line_refresh(const HWND hWnd, const BUFFER *bf, const DWORD p, const DWORD r);

static BOOL undo_alloc(BUFFER *bf);
static void undo_free(BUFFER *bf, const int index);
static BOOL undo_set(BUFFER *bf, const int type, const DWORD st, const DWORD len);
static BOOL undo_exec(const HWND hWnd, BUFFER *bf);
static BOOL redo_exec(const HWND hWnd, BUFFER *bf);

static BOOL string_init(BUFFER *bf);
static BOOL string_set(const HWND hWnd, BUFFER *bf, const TCHAR *str, const DWORD len);
static BOOL string_insert(const HWND hWnd, BUFFER *bf, TCHAR *str, const int len, const BOOL insert_mode);
static void string_delete(const HWND hWnd, BUFFER *bf, DWORD st, DWORD en);
static void string_delete_char(const HWND hWnd, BUFFER *bf, DWORD st);
static BOOL string_flush(BUFFER *bf, const BOOL undo_flag);
static void set_select_indent(const HWND hWnd, BUFFER *bf);

static BOOL draw_init(const HWND hWnd, BUFFER *bf);
static void draw_free(BUFFER *bf);
static void draw_rect_color(const HWND hWnd, const HDC mdc, const BUFFER *bf, const int left, const int top, const int right, const int bottom, COLORREF color);
static void draw_rect(const HWND hWnd, const HDC mdc, const BUFFER *bf, const int left, const int top, const int right, const int bottom);
static int draw_string(const HWND hWnd, const HDC mdc, const BUFFER *bf, const RECT *drect, const int left, const int top, const TCHAR *str, const int len, const BOOL sel);
static void draw_line(const HWND hWnd, const HDC mdc, BUFFER *bf, const int i, const int left, const int right);

static void caret_set_size(const HWND hWnd, BUFFER *bf);
static int caret_char_to_caret(const HDC mdc, BUFFER *bf, const int i, const DWORD cp);
static DWORD caret_point_to_caret(BUFFER *bf, const int x, const int y);
static void caret_get_token(BUFFER *bf);
static void caret_move(const HWND hWnd, BUFFER *bf, const int key);

static LRESULT CALLBACK nedit_proc(const HWND hWnd, const UINT msg, const WPARAM wParam, const LPARAM lParam);

/*
 * string_to_clipboard - 文字列をクリップボードに設定
 */
static BOOL string_to_clipboard(const HWND hWnd, const TCHAR *st, const TCHAR *en)
{
	HANDLE hMem;
	TCHAR *buf;

	if (OpenClipboard(hWnd) == FALSE) {
		return FALSE;
	}
	if (EmptyClipboard() == FALSE) {
		CloseClipboard();
		return FALSE;
	}

	if ((hMem = GlobalAlloc(GHND, sizeof(TCHAR) * (en - st + 1))) == NULL) {
		CloseClipboard();
		return FALSE;
	}
	if ((buf = GlobalLock(hMem)) == NULL) {
		GlobalFree(hMem);
		CloseClipboard();
		return FALSE;
	}
	CopyMemory(buf, st, sizeof(TCHAR) * (en - st));
	*(buf + (en - st)) = TEXT('\0');
	GlobalUnlock(hMem);
#ifdef UNICODE
	SetClipboardData(CF_UNICODETEXT, hMem);
#else
	SetClipboardData(CF_TEXT, hMem);
#endif

	CloseClipboard();
	return TRUE;
}

/*
 * notify_message - 親ウィンドウに通知
 */
static void notify_message(const HWND hWnd, const BUFFER *bf, const int code)
{
	SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(bf->id, code), (LPARAM)hWnd);
}

/*
 * get_edit_rect - 描画領域の取得
 */
static BOOL get_edit_rect(const HWND hWnd, const BUFFER *bf, RECT *rect)
{
	BOOL ret;

	ret = GetClientRect(hWnd, rect);

	rect->left += bf->left_margin + bf->line_no_width;
	rect->top += bf->top_margin;
	rect->right -= bf->right_margin;
	rect->bottom -= bf->bottom_margin;
	return ret;
}

/*
 * set_scrollbar - スクロールバーの設定
 */
static void set_scrollbar(const HWND hWnd, BUFFER *bf)
{
	SCROLLINFO si;
	RECT rect;

	get_edit_rect(hWnd, bf, &rect);

	// 横スクロールバー
	if (bf->wordwrap == FALSE && (rect.right - rect.left) < bf->line_width) {
		int width = bf->line_width / bf->char_width + 1;

		EnableScrollBar(hWnd, SB_HORZ, ESB_ENABLE_BOTH);

		bf->max_x = width - ((rect.right - rect.left) / bf->char_width);
		bf->pos_x = (bf->pos_x < bf->max_x) ? bf->pos_x : bf->max_x;

		ZeroMemory(&si, sizeof(SCROLLINFO));
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_POS | SIF_RANGE | SIF_PAGE;
		si.nPage = (rect.right - rect.left) / bf->char_width;
		si.nMax = width - 1;
		si.nPos = bf->pos_x;
		SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);
	} else {
		EnableScrollBar(hWnd, SB_HORZ, ESB_DISABLE_BOTH);

		bf->pos_x = bf->max_x = 0;

		ZeroMemory(&si, sizeof(SCROLLINFO));
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_POS | SIF_PAGE | SIF_RANGE;
		si.nMax = (bf->wordwrap == TRUE) ? 0 : 1;
		SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);
	}

	// 縦スクロールバー
	if (((rect.bottom - rect.top) / bf->font_height) < bf->line_len) {
		EnableScrollBar(hWnd, SB_VERT, ESB_ENABLE_BOTH);

		bf->max_y = bf->line_len - ((rect.bottom - rect.top) / bf->font_height);
		bf->pos_y = (bf->pos_y < bf->max_y) ? bf->pos_y : bf->max_y;

		ZeroMemory(&si, sizeof(SCROLLINFO));
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_POS | SIF_RANGE | SIF_PAGE;
		si.nPage = (rect.bottom - rect.top) / bf->font_height;
		si.nMax = bf->line_len - 1;
		si.nPos = bf->pos_y;
		SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
	} else {
		EnableScrollBar(hWnd, SB_VERT, ESB_DISABLE_BOTH);

		bf->pos_y = bf->max_y = 0;

		ZeroMemory(&si, sizeof(SCROLLINFO));
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_POS | SIF_PAGE | SIF_RANGE;
		si.nMax = 1;
		SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
	}
}

/*
 * show_line - 指定行を表示
 */
static void show_line(const HWND hWnd, BUFFER *bf, int line, BOOL begin_line)
{
	RECT rect;
	int i;
	int x, y;

	get_edit_rect(hWnd, bf, &rect);
	x = caret_char_to_caret(bf->mdc, bf, line, bf->cp) + (bf->pos_x * bf->char_width);
	y = line;

	// x
	i = bf->pos_x;
	if (begin_line == TRUE) {
		bf->pos_x = 0;
	} else {
		if (x < (bf->pos_x + 5) * bf->char_width) {
			bf->pos_x = x / bf->char_width - 5;
			if (bf->pos_x < 0) {
				bf->pos_x = 0;
			}
		}
		if (x > ((bf->pos_x - 5) * bf->char_width) + (rect.right - rect.left)) {
			bf->pos_x = x / bf->char_width - (rect.right - rect.left) / bf->char_width + 5;
			if (bf->pos_x > bf->max_x) {
				bf->pos_x = bf->max_x;
			}
		}
	}
	if (i != bf->pos_x) {
		SetScrollPos(hWnd, SB_HORZ, bf->pos_x, TRUE);
		ScrollWindowEx(hWnd, (i - bf->pos_x) * bf->char_width, 0, NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
	}

	// y
	i = bf->pos_y;
	if (y < bf->pos_y) {
		bf->pos_y = y;
	}
	if (y > bf->pos_y + (rect.bottom - rect.top) / bf->font_height - 1) {
		bf->pos_y = y - (rect.bottom - rect.top) / bf->font_height + 1;
		if (bf->pos_y > bf->max_y) {
			bf->pos_y = bf->max_y;
		}
	}
	if (i != bf->pos_y) {
		SetScrollPos(hWnd, SB_VERT, bf->pos_y, TRUE);
		rect.left -= bf->line_no_width;
		ScrollWindowEx(hWnd, 0, (i - bf->pos_y) * bf->font_height, NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
	}
}

/*
 * ensure_visible - キャレット位置を表示
 */
static void ensure_visible(const HWND hWnd, BUFFER *bf)
{
	show_line(hWnd, bf, index_to_line(bf, bf->cp), FALSE);
}

/*
 * is_lead_byte - マルチバイトの先頭バイトかチェック
 */
static BOOL is_lead_byte(const BUFFER *bf, TCHAR *p)
{
	if (IS_LEAD_TBYTE((TBYTE)*p) == TRUE && char_to_index(bf, char_next(bf, p)) < BUF_LEN) {
		return TRUE;
	}
	return FALSE;
}

/*
 * get_char_extent - 文字の描画幅の取得
 */
static int get_char_extent(BUFFER *bf, TCHAR *str, int *ret_len)
{
	SIZE sz;
	int ret = 0;

#ifdef UNICODE
	*ret_len = (is_lead_byte(bf, str) == TRUE) ? 2 : 1;
	if (*ret_len == 1 && HIBYTE(*str) == 0 && *(bf->cwidth + LOBYTE(*str)) != 0) {
		ret = *(bf->cwidth + LOBYTE(*str));
	} else {
		GetTextExtentPoint32(bf->mdc, str, *ret_len, &sz);
		ret = sz.cx;
		if (*ret_len == 1 && HIBYTE(*str) == 0 && sz.cx < 256) {
			*(bf->cwidth + LOBYTE(*str)) = (BYTE)sz.cx;
		}
	}
#else
	*ret_len = (is_lead_byte(bf, str) == TRUE) ? 2 : 1;
	if (*ret_len == 1 && *(bf->cwidth + (unsigned char)*str) != 0) {
		ret = *(bf->cwidth + (unsigned char)*str);
	} else {
		GetTextExtentPoint32(bf->mdc, str, *ret_len, &sz);
		ret = sz.cx;
		if (*ret_len == 1 && sz.cx < 256) {
			*(bf->cwidth + (unsigned char)*str) = (BYTE)sz.cx;
		}
	}
#endif
	return ret;
}

/*
 * char_next - 次の文字を取得
 */
static TCHAR *char_next(const BUFFER *bf, TCHAR *p)
{
	if (bf->dp != NULL && p == bf->dp - 1) {
		return (bf->dp + bf->del_len);
	}

	if (bf->ip != NULL) {
		if (p == bf->ip - 1) {
			return bf->input_buf;
		} else if (p == bf->input_buf + bf->input_len - 1) {
			return (bf->ip + bf->ip_len);
		}
	}
	return (p + 1);
}

/*
 * char_prev - 前の文字を取得
 */
static TCHAR *char_prev(const BUFFER *bf, TCHAR *p)
{
	if (bf->dp != NULL && p == bf->dp + bf->del_len) {
		return (bf->dp - 1);
	}

	if (bf->ip != NULL) {
		if (p == (bf->ip + bf->ip_len)) {
			return (bf->input_buf + bf->input_len - 1);
		} else if (p == bf->input_buf) {
			return (bf->ip - 1);
		}
	}
	return (p - 1);
}

/*
 * index_to_char - 文字インデックスから文字位置を取得
 */
static TCHAR *index_to_char(const BUFFER *bf, const DWORD index)
{
	TCHAR *p;

	if (bf->dp != NULL && index >= (DWORD)(bf->dp - bf->buf)) {
		p = bf->buf + index + bf->del_len;
	} else if (bf->ip != NULL && index >= (DWORD)(bf->ip - bf->buf)) {
		if (index < (DWORD)(bf->ip - bf->buf + bf->input_len)) {
			p = bf->input_buf + (index - (bf->ip - bf->buf));
		} else {
			p = bf->buf + index - bf->input_len + bf->ip_len;
		}
	} else {
		p = bf->buf + index;
	}
	return p;
}

/*
 * char_to_index - 文字位置から文字インデックスを取得
 */
static DWORD char_to_index(const BUFFER *bf, const TCHAR *p)
{
	DWORD i;

	if (bf->dp != NULL && p >= bf->dp) {
		i = p - bf->buf - bf->del_len;
	} else if (bf->ip != NULL && !(p >= bf->buf && p < bf->ip)) {
		if (p >= bf->input_buf && p <= bf->input_buf + bf->input_len) {
			i = (bf->ip - bf->buf) + (p - bf->input_buf);
		} else {
			i = (p - bf->buf) + bf->input_len - bf->ip_len;
		}
	} else {
		i = p - bf->buf;
	}
	return i;
}

/*
 * index_to_line - 文字インデックスから行位置を取得
 */
static int index_to_line(const BUFFER *bf, const DWORD index)
{
	int low = 0;
	int high = bf->line_len - 1;
	int i;

	while (low <= high) {
		i = (low + high) / 2;

		if (line_get(bf, i) > index) {
			if (i > 0 && line_get(bf, i - 1) <= index) {
				return (i - 1);
			}
			high  = i - 1;
		} else if (line_get(bf, i) < index) {
			if (i < bf->line_len - 1 && line_get(bf, i + 1) > index) {
				return i;
			}
			low = i + 1;
		} else {
			return i;
		}
	}
	return (bf->line_len - 1);
}

/*
 * line_alloc - 行情報の確保
 */
static BOOL line_alloc(BUFFER *bf, const int i, const int size)
{
	int *ln;

	bf->line_size += size;
	if ((ln = mem_alloc(sizeof(int) * bf->line_size)) == NULL) {
		return FALSE;
	}
	if (bf->line != NULL) {
		CopyMemory(ln, bf->line, sizeof(int) * i);
		mem_free(&bf->line);
	}
	bf->line = ln;
	return TRUE;
}

/*
 * line_move - 行情報の移動
 */
static BOOL line_move(BUFFER *bf, const int i, const int len)
{
	int *ln;

	if (bf->line_len + len + 1 > bf->line_size) {
		// 追加
		bf->line_size = bf->line_len + len + 1 + RESERVE_LINE;
		if ((ln = mem_alloc(sizeof(int) * bf->line_size)) == NULL) {
			return FALSE;
		}
		CopyMemory(ln, bf->line, sizeof(int) * i);
		CopyMemory(ln + i + len, bf->line + i, sizeof(int) * (bf->line_len - i + 1));
		mem_free(&bf->line);
		bf->line = ln;
	} else {
		MoveMemory(bf->line + i + len, bf->line + i, sizeof(int) * (bf->line_len - i + 1));
	}
	bf->line_len += len;

	if (bf->line_len + 1 < bf->line_size - RESERVE_LINE) {
		// 解放
		bf->line_size = bf->line_len + 1 + RESERVE_LINE;
		if ((ln = mem_alloc(sizeof(int) * bf->line_size)) == NULL) {
			return FALSE;
		}
		CopyMemory(ln, bf->line, sizeof(int) * (bf->line_len + 1));
		mem_free(&bf->line);
		bf->line = ln;
	}
	return TRUE;
}

/*
 * line_get - 行頭インデックスの取得
 */
static DWORD line_get(const BUFFER *bf, const int lindex)
{
	if (bf->line_add_len != 0 && bf->line_add_index <= lindex) {
		return (*(bf->line + lindex) + bf->line_add_len);
	}
	return *(bf->line + lindex);
}

/*
 * line_set - 行頭インデックスを設定
 */
static void line_set(BUFFER *bf, const int lindex, const DWORD len)
{
	if (bf->line_add_len != 0 && bf->line_add_index <= lindex) {
		*(bf->line + lindex) = len - bf->line_add_len;
	} else {
		*(bf->line + lindex) = len;
	}
}

/*
 * line_flush - 行情報の反映
 */
static void line_flush(BUFFER *bf)
{
	int i;

	for (i = bf->line_add_index; i <= bf->line_len; i++) {
		*(bf->line + i) += bf->line_add_len;
	}
	bf->line_add_index = 0;
	bf->line_add_len = 0;
}

/*
 * line_add_length - 行情報に文字数を追加
 */
static void line_add_length(BUFFER *bf, const int index, const int len)
{
	int i;

	if (bf->line_add_index != 0 && bf->line_add_index < index) {
		for (i = bf->line_add_index; i < index; i++) {
			*(bf->line + i) += bf->line_add_len;
		}
	}
	bf->line_add_index = index;
	bf->line_add_len += len;
}

/*
 * line_get_length - 行の長さを取得
 */
static int line_get_length(const BUFFER *bf, const DWORD index)
{
	int i, j;

	i = line_get(bf, index_to_line(bf, index) + 1) - 1;
	j = line_get(bf, index_to_line(bf, index));
	for (; i >= j &&
		(*index_to_char(bf, i) == TEXT('\r') || *index_to_char(bf, i) == TEXT('\n')); i--);
	return (i - j + 1);
}

/*
 * line_get_next_index - 次の行頭の文字インデックスを取得
 */
static DWORD line_get_next_index(BUFFER *bf, const DWORD index)
{
	TCHAR *p;
	DWORD word = -1;
	DWORD i;
	int line_size;
	int cnt = 0;
	int offset = bf->left_margin + bf->line_no_width;
	int width;
	int tab;
	int clen;

	if (bf->wordwrap == TRUE && bf->width <= 0) {
		return BUF_LEN;
	}
	line_size = bf->width - bf->right_margin + bf->line_no_width;
	for (i = index; i < BUF_LEN; i++) {
		p = index_to_char(bf, i);
		switch (*p) {
		case TEXT('\r'):
		case TEXT('\n'):
			// new line
			if (*p == TEXT('\r') && (i + 1) < BUF_LEN && *(char_next(bf, p)) == TEXT('\n')) {
				i++;
			}
			if (bf->line_width < offset + bf->right_margin) {
				bf->line_width = offset + bf->right_margin;
			}
			return i;

		case TEXT('\t'):
			// tab
			tab = bf->tab_stop * bf->char_width - ((offset - (bf->left_margin + bf->line_no_width)) % (bf->tab_stop * bf->char_width));
			if (tab < bf->char_width) {
				tab += bf->tab_stop * bf->char_width;
			}
			if ((bf->wordwrap == TRUE && offset + tab > line_size) ||
				(bf->line_max > 0 && cnt + bf->tab_stop - (cnt % bf->tab_stop) > bf->line_max)) {
				// 折り返し
				if (bf->line_width < offset + bf->right_margin) {
					bf->line_width = offset + bf->right_margin;
				}
				if (word != -1 && word != index && clen == 1) {
					i = word;
				}
				return (i + ((cnt == 0) ? 1 : 0));
			}
			else {
				cnt += bf->tab_stop - (cnt % bf->tab_stop);
				offset += tab;
			}
			word = -1;
			break;
		default:
			if (bf->wordwrap == TRUE) {
				width = get_char_extent(bf, p, &clen);
			}
			else {
				width = bf->char_width * 2;
				clen = 1;
			}
			if ((bf->wordwrap == TRUE && offset + width > line_size) ||
				(bf->line_max > 0 && cnt + 1 > bf->line_max)) {
				// 折り返し
				if (bf->line_width < offset + bf->right_margin) {
					bf->line_width = offset + bf->right_margin;
				}
				if (word != -1 && word != index && clen == 1) {
					i = word;
				}
				return (i + ((cnt == 0) ? clen : 0));
			}
			else {
				cnt += clen;
				offset += width;
			}
			if (clen == 2) {
				i++;
				word = -1;
			}
			else if (*p != TEXT(' ')) {
				if (word == (DWORD)-1) {
					// 単語の開始位置
					word = i;
				}
			}
			else {
				word = -1;
			}
			break;
		}
	}
	if (bf->line_width < offset + bf->right_margin) {
		bf->line_width = offset + bf->right_margin;
	}
	return i;
}

/*
 * line_set_info - 行情報の設定
 */
static BOOL line_set_info(const HWND hWnd, BUFFER *bf)
{
	DWORD index;
	int i = 1;

	index = line_get_next_index(bf, 0);
	while (index < BUF_LEN) {
		switch (*index_to_char(bf, index)) {
		case TEXT('\r'):
		case TEXT('\n'):
			// 行頭のインデックスを設定
			line_set(bf, i, index + 1);
			if (++i >= bf->line_size && line_alloc(bf, i, BUF_LEN / 30) == FALSE) {
				return FALSE;
			}
			index++;
			break;

		default:
			// 行頭のインデックスを設定 (折り返し)
			line_set(bf, i, index);
			if (++i >= bf->line_size && line_alloc(bf, i, BUF_LEN / 30) == FALSE) {
				return FALSE;
			}
			break;
		}
		index = line_get_next_index(bf, index);
	}
	line_set(bf, i, index);
	bf->line_len = i;

	bf->line_no_width = 0;
	if (bf->show_line_no == TRUE) {
		int d;
		int v = bf->line_len;
		for (d = 0; v != 0; d++) {
			v = v / 10;
		}
		int line_no_width = (d + 1) * bf->char_width + (bf->char_width * 16 / 10);
		if (bf->line_no_width != line_no_width) {
			InvalidateRect(hWnd, NULL, FALSE);
		}
		bf->line_no_width = line_no_width;
	}
	return TRUE;
}

/*
 * line_get_count - 論理行数の取得
 */
static int line_get_count(BUFFER *bf, const int lindex)
{
	TCHAR *p;
	int i;

	for (i = lindex + 1; i < bf->line_len; i++) {
		p = index_to_char(bf, line_get(bf, i) - 1);
		if (*p == TEXT('\r') || *p == TEXT('\n')) {
			// 論理行の終端
			break;
		}
	}
	return (i - lindex);
}

/*
 * line_set_count - 行情報の設定
 */
static int line_set_count(BUFFER *bf, const int lindex, const int old_cnt, const int line_cnt)
{
	TCHAR *p;
	DWORD index;
	int i = lindex;
	int rcnt = 0;

	index = line_get(bf, lindex - 1);
	while (1) {
		index = line_get_next_index(bf, index);
		if ((i - lindex) >= old_cnt) {
			// 挿入行を移動
			line_move(bf, i, 1);
		}
		p = index_to_char(bf, index);
		if (*p == TEXT('\r') || *p == TEXT('\n')) {
			// 行頭のインデックスを設定
			index++;
			line_set(bf, i++, index);
			if (++rcnt >= line_cnt) {
				break;
			}
		} else {
			// 行頭のインデックスを設定 (折り返し)
			line_set(bf, i++, index);
			if (index >= BUF_LEN) {
				break;
			}
		}
	}

	if ((i - lindex) < old_cnt) {
		// 行情報の削除
		line_move(bf, i + old_cnt - (i - lindex), -(old_cnt - (i - lindex)));
	}
	return (i - lindex);
}

/*
 * line_refresh - 指定範囲文字列のある行を再描画対象にする
 */
static void line_refresh(const HWND hWnd, const BUFFER *bf, const DWORD p, const DWORD r)
{
	RECT rect;
	int i, j;

	// 文字位置から行のインデックスを取得
	i = index_to_line(bf, p);
	if (p == r) {
		j = i;
	} else {
		j = index_to_line(bf, r);
	}
	if (i > j) {
		SWAP(i, j);
	}
	i -= bf->pos_y;
	j -= bf->pos_y;

	// 再描画
	get_edit_rect(hWnd, bf, &rect);
	rect.left -= bf->line_no_width;
	if (i * bf->font_height > (rect.bottom - rect.top) || j * bf->font_height + bf->font_height < 0) {
		return;
	}
	if (j * bf->font_height + bf->font_height < (rect.bottom - rect.top)) {
		rect.bottom = j * bf->font_height + bf->font_height + bf->top_margin;
	}
	if (i * bf->font_height > 0) {
		rect.top = i * bf->font_height + bf->top_margin;
	}
	InvalidateRect(hWnd, &rect, FALSE);
}

/*
 * undo_alloc - UNDOの確保
 */
static BOOL undo_alloc(BUFFER *bf)
{
	UNDO *ud;

	bf->undo_size += RESERVE_UNDO;
	if ((ud = mem_calloc(sizeof(UNDO) * bf->undo_size)) == NULL) {
		return FALSE;
	}
	if (bf->undo != NULL) {
		CopyMemory(ud, bf->undo, sizeof(UNDO) * bf->undo_len);
		mem_free(&bf->undo);
	}
	bf->undo = ud;
	return TRUE;
}

/*
 * undo_free - UNDOの解放
 */
static void undo_free(BUFFER *bf, const int index)
{
	int i;

	for (i = index; i < bf->undo_size; i++) {
		(bf->undo + i)->type = 0;
		mem_free(&(bf->undo + i)->buf);
	}
}

/*
 * undo_set - UNDOのセット
 */
static BOOL undo_set(BUFFER *bf, const int type, const DWORD index, const DWORD len)
{
	undo_free(bf, bf->undo_len);
	if (bf->undo_len + 1 >= bf->undo_size && undo_alloc(bf) == FALSE) {
		return FALSE;
	}
	(bf->undo + bf->undo_len)->type = type;
	(bf->undo + bf->undo_len)->st = index;
	(bf->undo + bf->undo_len)->len = len;
	switch (type) {
	case UNDO_TYPE_INPUT:
		// 入力
		break;

	case UNDO_TYPE_DELETE:
		// 削除
		if (((bf->undo + bf->undo_len)->buf = mem_alloc(sizeof(TCHAR) * len)) == NULL) {
			return FALSE;
		}
		CopyMemory((bf->undo + bf->undo_len)->buf, bf->buf + index, sizeof(TCHAR) * len);
		break;
	}
	bf->undo_len++;
	return TRUE;
}

/*
 * undo_exec - UNDOの実行
 */
static BOOL undo_exec(const HWND hWnd, BUFFER *bf)
{
	int i;

	string_flush(bf, TRUE);

	i = bf->undo_len - 1;
	if (i < 0) {
		return TRUE;
	}
	bf->sp = bf->cp = (bf->undo + i)->st;
	switch ((bf->undo + i)->type) {
	case UNDO_TYPE_INPUT:
		if ((bf->undo + i)->buf == NULL) {
			if (((bf->undo + i)->buf = mem_alloc(sizeof(TCHAR) * (bf->undo + i)->len)) == NULL) {
				return FALSE;
			}
			CopyMemory((bf->undo + i)->buf, bf->buf + (bf->undo + i)->st, sizeof(TCHAR) * (bf->undo + i)->len);
		}
		string_delete(hWnd, bf, (bf->undo + i)->st, (bf->undo + i)->st + (bf->undo + i)->len);
		break;

	case UNDO_TYPE_DELETE:
		string_insert(hWnd, bf, (bf->undo + i)->buf, (bf->undo + i)->len, TRUE);
		break;
	}
	string_flush(bf, FALSE);
	bf->undo_len--;

	SendMessage(hWnd, EM_SETMODIFY, (bf->undo_len == bf->undo_pos) ? FALSE : TRUE, 0);

	bf->line_width = 0;
	line_set_info(hWnd, bf);
	set_scrollbar(hWnd, bf);
	ensure_visible(hWnd, bf);
	InvalidateRect(hWnd, NULL, FALSE);
	return TRUE;
}

/*
 * redo_exec - REDOの実行
 */
static BOOL redo_exec(const HWND hWnd, BUFFER *bf)
{
	int i;

	string_flush(bf, TRUE);

	i = bf->undo_len;
	if ((bf->undo + i)->type == 0) {
		return TRUE;
	}
	bf->sp = bf->cp = (bf->undo + i)->st;
	switch ((bf->undo + i)->type) {
	case UNDO_TYPE_INPUT:
		string_insert(hWnd, bf, (bf->undo + i)->buf, (bf->undo + i)->len, TRUE);
		break;

	case UNDO_TYPE_DELETE:
		string_delete(hWnd, bf, (bf->undo + i)->st, (bf->undo + i)->st + (bf->undo + i)->len);
		break;
	}
	string_flush(bf, FALSE);
	bf->undo_len++;

	SendMessage(hWnd, EM_SETMODIFY, (bf->undo_len == bf->undo_pos) ? FALSE : TRUE, 0);

	bf->line_width = 0;
	line_set_info(hWnd, bf);
	set_scrollbar(hWnd, bf);
	ensure_visible(hWnd, bf);
	InvalidateRect(hWnd, NULL, FALSE);
	return TRUE;
}

/*
 * string_init - 初期化
 */
static BOOL string_init(BUFFER *bf)
{
	// free
	mem_free(&bf->input_buf);
	mem_free(&bf->line);
	undo_free(bf, 0);
	mem_free(&bf->undo);

	// input init
	bf->input_size = RESERVE_INPUT;
	if ((bf->input_buf = mem_alloc(sizeof(TCHAR) * bf->input_size)) == NULL) {
		return FALSE;
	}
	*bf->input_buf = TEXT('\0');
	bf->input_len = 0;
	bf->ip = NULL;
	bf->ip_len = 0;

	// line init
	bf->line_size = RESERVE_LINE;
	if ((bf->line = mem_calloc(sizeof(int) * bf->line_size)) == NULL) {
		return FALSE;
	}
	bf->line_len = 0;
	bf->line_width = 0;

	// undo init
	bf->undo_size = RESERVE_UNDO;
	if ((bf->undo = mem_calloc(sizeof(UNDO) * bf->undo_size)) == NULL) {
		return FALSE;
	}
	bf->undo_len = 0;

	bf->pos_x = bf->max_x = 0;
	bf->pos_y = bf->max_y = 0;
	bf->sp = bf->cp = 0;
	bf->cpx = 0;
	bf->dp = NULL;
	bf->del_len = 0;
	bf->modified = FALSE;
	return TRUE;
}

/*
 * string_set - 文字列の設定
 */
static BOOL string_set(const HWND hWnd, BUFFER *bf, const TCHAR *str, const DWORD len)
{
	HCURSOR old_cursor;

	old_cursor = SetCursor(LoadCursor(NULL, IDC_WAIT));

	if (string_init(bf) == FALSE) {
		SetCursor(old_cursor);
		return FALSE;
	}

	// 文字列設定
	mem_free(&bf->buf);
	bf->buf_len = len;
	bf->buf_size = bf->buf_len + 1 + RESERVE_BUF;
	if ((bf->buf = mem_alloc(sizeof(TCHAR) * bf->buf_size)) == NULL) {
		SetCursor(old_cursor);
		return FALSE;
	}
	CopyMemory(bf->buf, str, sizeof(TCHAR) * (bf->buf_len + 1));
	if (bf->lowercase == TRUE) {
		// 小文字に変換
		CharLowerBuff(bf->buf, bf->buf_len);
	} else if (bf->uppercase == TRUE) {
		// 大文字に変換
		CharUpperBuff(bf->buf, bf->buf_len);
	}

	// 行情報の設定
	line_set_info(hWnd, bf);
	set_scrollbar(hWnd, bf);
	bf->error_line = -1;
	InvalidateRect(hWnd, NULL, FALSE);

	SetCursor(old_cursor);
	return TRUE;
}

/*
 * string_insert - 文字列の追加
 */
static BOOL string_insert(const HWND hWnd, BUFFER *bf, TCHAR *str, const int len, const BOOL insert_mode)
{
	TCHAR *p;
	DWORD ip_len;
	int lcnt1, lcnt2;
	int ret_cnt = 0;
	int st;
	int i;
	int ilen = len;
	int old_pos_x;
	BOOL sel = FALSE;

	// 入力制限 (挿入モード)
	if ((insert_mode == TRUE || (bf->cp != bf->sp)) && bf->limit_len != 0 && 
		(BUF_LEN - ((bf->cp > bf->sp) ? bf->cp - bf->sp : bf->sp - bf->cp)) + ilen > bf->limit_len) {
		i = bf->limit_len - (BUF_LEN - ((bf->cp > bf->sp) ? bf->cp - bf->sp : bf->sp - bf->cp));
		for (ilen = 0; ilen < i; ilen++) {
			if (IS_LEAD_TBYTE((TBYTE)*(str + ilen)) == TRUE ||
				(*(str + ilen) == TEXT('\r') && *(str + ilen + 1) == TEXT('\n'))) {
				if (ilen + 1 >= i) {
					break;
				}
				ilen++;
			}
		}
		// 親ウィンドウに通知
		notify_message(hWnd, bf, EN_MAXTEXT);
		if (ilen <= 0) {
			return FALSE;
		}
	}
	if (bf->cp != bf->sp) {
		// 選択文字の削除
		string_delete(hWnd, bf, bf->cp, bf->sp);
		if (bf->cp > bf->sp) {
			SWAP(bf->cp, bf->sp);
		}
		bf->sp = bf->cp;
		sel = TRUE;
	}
	if (bf->dp != NULL) {
		string_flush(bf, TRUE);
	}
	// 入力制限 (上書きモード)
	if (insert_mode == FALSE && sel == FALSE && bf->limit_len != 0) {
		for (i = bf->cp, p = str; i - (int)bf->cp < ilen && (p - str) < ilen; i++, p++){
			if ((DWORD)i >= BUF_LEN || *index_to_char(bf, i) == TEXT('\r') || *index_to_char(bf, i) == TEXT('\n')) {
				// 文末と行末の場合は以降を挿入モードと同じ動作
				if (BUF_LEN + ilen > bf->limit_len) {
					i = bf->limit_len - BUF_LEN;
					for (ilen = (p - str); ilen < i; ilen++) {
						if (IS_LEAD_TBYTE((TBYTE)*(str + ilen)) == TRUE ||
							(*(str + ilen) == TEXT('\r') && *(str + ilen + 1) == TEXT('\n'))) {
							if (ilen + 1 >= i) {
								break;
							}
							ilen++;
						}
					}
				}
				break;
			}
			if (IS_LEAD_TBYTE((TBYTE)*p) == TRUE && (int)(p - str) < ilen) {
				if (is_lead_byte(bf, index_to_char(bf, i)) == TRUE) {
					// 2バイトコードに2バイトの上書き
					i++;
				} else if ((BUF_LEN - (i - bf->cp)) + (p - str) + 1 > bf->limit_len) {
					// 1バイトコードに2バイトを上書きして制限を超える場合は以降を切捨て
					ilen = p - str;
					break;
				}
				p++;
			}
		}
		if (ilen != len) {
			// 親ウィンドウに通知
			notify_message(hWnd, bf, EN_MAXTEXT);
		}
		if (ilen <= 0) {
			return FALSE;
		}
	}

	// 改行数の取得
	for (p = str; (p - str) < ilen; p++) {
		if (*p == TEXT('\r') || *p == TEXT('\n')) {
			if (*p == TEXT('\r') && *(p + 1) == TEXT('\n')) {
				p++;
			}
			ret_cnt++;
		}
	}
	// 追加前の表示行数
	lcnt1 = line_get_count(bf, index_to_line(bf, (bf->ip == NULL) ? bf->cp : ((bf->ip - bf->buf) + bf->input_len)));

	// 入力バッファの設定
	if (bf->input_len + ilen + 1 > bf->input_size) {
		bf->input_size = bf->input_len + ilen + 1 + RESERVE_INPUT;
		if ((p = mem_alloc(sizeof(TCHAR) * bf->input_size)) == NULL) {
			return FALSE;
		}
		CopyMemory(p, bf->input_buf, sizeof(TCHAR) * bf->input_len);
		mem_free(&bf->input_buf);
		bf->input_buf = p;
	}
	CopyMemory(bf->input_buf + bf->input_len, str, sizeof(TCHAR) * ilen);
	if (bf->lowercase == TRUE) {
		// 小文字に変換
		CharLowerBuff(bf->input_buf + bf->input_len, ilen);
	} else if (bf->uppercase == TRUE) {
		// 大文字に変換
		CharUpperBuff(bf->input_buf + bf->input_len, ilen);
	}
	bf->input_len += ilen;
	if (bf->ip == NULL) {
		bf->ip = index_to_char(bf, bf->cp);
	}
	bf->cp += ilen;
	bf->sp = bf->cp;

	// 上書きモード
	for (p = str, ip_len = 0; sel == FALSE && insert_mode == FALSE && (p - str) < ilen; p++, ip_len++) {
		if (char_to_index(bf, bf->ip + bf->ip_len + ip_len) >= BUF_LEN ||
			*(bf->ip + bf->ip_len + ip_len) == TEXT('\r') || *(bf->ip + bf->ip_len + ip_len) == TEXT('\n')) {
			break;
		}
		if (is_lead_byte(bf, bf->ip + bf->ip_len + ip_len) == TRUE) {
			ip_len++;
		}
		if (IS_LEAD_TBYTE((TBYTE)*p) == TRUE && (p - str) < ilen) {
			p++;
		}
	}
	bf->ip_len += ip_len;

	// 行情報の設定
	st = (bf->ip - bf->buf) + (bf->input_len - ilen);
	i = index_to_line(bf, st) + 1;
	lcnt2 = (ret_cnt > 0) ? line_set_count(bf, i, 0, ret_cnt) : 0;
	lcnt2 += line_set_count(bf, i + lcnt2, lcnt1, 0);
	line_add_length(bf, i + lcnt2, ilen - ip_len);

	// 親ウィンドウに通知
	notify_message(hWnd, bf, EN_UPDATE);

	if (lcnt1 > lcnt2) {
		// 行数減少
		InvalidateRect(hWnd, NULL, FALSE);
	} else if (ret_cnt > 0 || lcnt1 != lcnt2) {
		// 複数行
		line_refresh(hWnd, bf, line_get(bf, i - 1), bf->buf_len + bf->input_len);
	} else if (lcnt2 > 1) {
		// 折り返しを含めて反映
		line_refresh(hWnd, bf, line_get(bf, i - 1), line_get(bf, i + lcnt2 - 2));
	} else {
		// 単一行
		line_refresh(hWnd, bf, st, st);
	}
	old_pos_x = bf->pos_x;
	bf->line_width = 0;
	line_set_info(hWnd, bf);
	set_scrollbar(hWnd, bf);
	ensure_visible(hWnd, bf);
	if (old_pos_x > bf->pos_x) {
		InvalidateRect(hWnd, NULL, FALSE);
	}

	if (bf->modified == FALSE) {
		bf->undo_pos = bf->undo_len;
	}
	bf->modified = TRUE;
	// 親ウィンドウに通知
	notify_message(hWnd, bf, EN_CHANGE);

	if (bf->error_line != -1) {
		int line = bf->error_line;
		bf->error_line = -1;
		line_refresh(hWnd, bf, line_get(bf, line), line_get(bf, line));
	}
	return TRUE;
}

/*
 * string_delete - 文字列の削除
 */
static void string_delete(const HWND hWnd, BUFFER *bf, DWORD st, DWORD en)
{
	int lcnt1, lcnt2;
	int ret_cnt;
	int i;

	if (st > en) {
		SWAP(st, en);
	}
	if (bf->ip != NULL || (bf->dp != NULL && bf->dp != bf->buf + st && bf->dp != bf->buf + en)) {
		string_flush(bf, TRUE);
	}
	if (en - st > bf->buf_len - bf->del_len || st >= bf->buf_len - bf->del_len) {
		return;
	}

	// 行数取得
	ret_cnt = index_to_line(bf, en) - index_to_line(bf, st);
	lcnt1 = line_get_count(bf, index_to_line(bf, en));

	// 削除位置設定
	bf->dp = bf->buf + st;
	bf->del_len += en - st;

	// 行情報の設定
	i = index_to_line(bf, st) + 1;
	if (ret_cnt > 0) {
		line_move(bf, i + ret_cnt, -ret_cnt);
	}
	lcnt2 = line_set_count(bf, i, lcnt1, 0);
	line_add_length(bf, i + lcnt2, st - en);

	// 親ウィンドウに通知
	notify_message(hWnd, bf, EN_UPDATE);

	if (ret_cnt > 0 || lcnt1 != lcnt2) {
		// 複数行
		InvalidateRect(hWnd, NULL, FALSE);
	} else if (lcnt2 > 1) {
		// 折り返しを含めて反映
		line_refresh(hWnd, bf, line_get(bf, i - 1), line_get(bf, i + lcnt2 - 2));
	} else {
		// 単一行
		line_refresh(hWnd, bf, st, en);
	}
	if (bf->modified == FALSE) {
		bf->undo_pos = bf->undo_len;
	}
	bf->modified = TRUE;
	// 親ウィンドウに通知
	notify_message(hWnd, bf, EN_CHANGE);

	if (bf->error_line != -1) {
		int line = bf->error_line;
		bf->error_line = -1;
		line_refresh(hWnd, bf, line_get(bf, line), line_get(bf, line));
	}
}

/*
 * string_delete_char - 文字の削除
 */
static void string_delete_char(const HWND hWnd, BUFFER *bf, DWORD st)
{
	int old_pos_x;
	int i = 1;

	if (bf->cp != bf->sp) {
		// 選択文字の削除
		string_delete(hWnd, bf, bf->cp, bf->sp);
		if (bf->cp > bf->sp) {
			SWAP(bf->cp, bf->sp);
		}
		bf->sp = bf->cp;
	} else {
		// 一文字削除
		if (is_lead_byte(bf, index_to_char(bf, st)) == TRUE) {
			i++;
		}
		if (*(index_to_char(bf, st)) == TEXT('\r') && *(index_to_char(bf, st + 1)) == TEXT('\n')) {
			i++;
		}
		string_delete(hWnd, bf, st, st + i);
	}
	old_pos_x = bf->pos_x;
	bf->line_width = 0;
	line_set_info(hWnd, bf);
	set_scrollbar(hWnd, bf);
	ensure_visible(hWnd, bf);
	if (old_pos_x > bf->pos_x) {
		InvalidateRect(hWnd, NULL, FALSE);
	}
}

/*
 * string_flush - 削除と入力バッファの反映
 */
static BOOL string_flush(BUFFER *bf, const BOOL undo_flag)
{
	TCHAR *p;

	if (bf->dp != NULL) {
		if (undo_flag == TRUE) {
			// undoに追加
			undo_set(bf, UNDO_TYPE_DELETE, bf->dp - bf->buf, bf->del_len);
		}

		// 削除文字列の反映
		MoveMemory(bf->dp, bf->dp + bf->del_len, sizeof(TCHAR) * (bf->buf_len - (bf->dp - bf->buf + bf->del_len) + 1));
		bf->buf_len -= bf->del_len;
		bf->dp = NULL;
		bf->del_len = 0;

		if (bf->buf_len + 1 < bf->buf_size - RESERVE_BUF) {
			bf->buf_size = bf->buf_len + 1 + RESERVE_BUF;
			p = mem_alloc(sizeof(TCHAR) * bf->buf_size);
			if (p == NULL) {
				return FALSE;
			}
			CopyMemory(p, bf->buf, sizeof(TCHAR) * (bf->buf_len + 1));
			mem_free(&bf->buf);
			bf->buf = p;
		}
		// 行情報の反映
		line_flush(bf);
		return TRUE;
	}
	if (bf->ip == NULL) {
		return TRUE;
	}

	if (undo_flag == TRUE) {
		// undoに追加
		if (bf->ip_len > 0) {
			undo_set(bf, UNDO_TYPE_DELETE, bf->ip - bf->buf, bf->ip_len);
		}
		undo_set(bf, UNDO_TYPE_INPUT, bf->ip - bf->buf, bf->input_len);
	}

	// 入力バッファの反映
	if (bf->buf_len + bf->input_len - bf->ip_len + 1 > bf->buf_size) {
		bf->buf_size = bf->buf_len + bf->input_len - bf->ip_len + 1 + RESERVE_BUF;
		if ((p = mem_alloc(sizeof(TCHAR) * bf->buf_size)) == NULL) {
			return FALSE;
		}
		if (bf->ip != bf->buf) {
			CopyMemory(p, bf->buf, sizeof(TCHAR) * (bf->ip - bf->buf));
		}
		CopyMemory(p + (bf->ip - bf->buf), bf->input_buf, sizeof(TCHAR) * bf->input_len);
		CopyMemory(p + (bf->ip - bf->buf) + bf->input_len, bf->ip + bf->ip_len, sizeof(TCHAR) * (bf->buf_len - (bf->ip + bf->ip_len - bf->buf) + 1));
		mem_free(&bf->buf);
		bf->buf = p;
	} else {
		MoveMemory(bf->ip + bf->input_len, bf->ip + bf->ip_len, sizeof(TCHAR) * (bf->buf_len - (bf->ip + bf->ip_len - bf->buf) + 1));
		CopyMemory(bf->ip, bf->input_buf, sizeof(TCHAR) * bf->input_len);
	}
	bf->buf_len += bf->input_len - bf->ip_len;

	// 入力バッファの解放
	if (bf->input_size > RESERVE_INPUT) {
		mem_free(&bf->input_buf);
		bf->input_size = RESERVE_INPUT;
		if ((bf->input_buf = mem_alloc(sizeof(TCHAR) * bf->input_size)) == NULL) {
			return FALSE;
		}
	}
	*bf->input_buf = TEXT('\0');
	bf->input_len = 0;
	bf->ip = NULL;
	bf->ip_len = 0;

	// 行情報の反映
	line_flush(bf);
	return TRUE;
}

/*
 * set_select_indent - 選択行のインデントを変更
 */
static void set_select_indent(const HWND hWnd, BUFFER *bf)
{
	DWORD st = (bf->cp > bf->sp) ? bf->sp : bf->cp;
	DWORD en = (bf->cp > bf->sp) ? bf->cp : bf->sp;
	DWORD first = st;
	TCHAR* buf;
	TCHAR* stc, * enc;
	TCHAR* p, * r;

	string_flush(bf, TRUE);

	// 選択範囲の拡張
	first = line_get(bf, index_to_line(bf, st));
	r = index_to_char(bf, en - 1);
	if (*r != TEXT('\r') && *r != TEXT('\n')) {
		for (; *index_to_char(bf, en) != TEXT('\r') && *index_to_char(bf, en) != TEXT('\n') && en < BUF_LEN; en++);
		if (*index_to_char(bf, en) == TEXT('\r') || *index_to_char(bf, en) == TEXT('\n')) {
			if (*index_to_char(bf, en) == TEXT('\r') && *index_to_char(bf, en + 1) == TEXT('\n')) {
				en += 2;
			}
			else {
				en++;
			}
		}
	}
	bf->sp = first;
	bf->cp = en;

	stc = index_to_char(bf, first);
	enc = index_to_char(bf, en);
	int ret_cnt = 0;
	if (GetKeyState(VK_SHIFT) >= 0) {
		// 行数カウント
		for (p = stc; p < enc; p++) {
			if (*p == TEXT('\r') || *p == TEXT('\n')) {
				if (*p == TEXT('\r') && *(p + 1) == TEXT('\n')) {
					p++;
				}
				ret_cnt++;
			}
		}
	}
	// 選択行のインデントを変更
	buf = mem_alloc(sizeof(TCHAR) * (enc - stc + ret_cnt + 1));
	if (buf == NULL) {
		return;
	}
	p = stc;
	r = buf;
	if (GetKeyState(VK_SHIFT) < 0) {
		if (*p == TEXT('\t')) {
			p++;
			en--;
		}
	}
	else {
		*(r++) = TEXT('\t');
		en++;
	}
	for (; p < enc; p++) {
		if (*p == TEXT('\r') || *p == TEXT('\n')) {
			if (*p == TEXT('\r') && *(p + 1) == TEXT('\n')) {
				*(r++) = *(p++);
			}
			*(r++) = *p;

			if (p + 1 < enc) {
				if (GetKeyState(VK_SHIFT) < 0) {
					if (*(p + 1) == TEXT('\t')) {
						p++;
						en--;
					}
				}
				else {
					*(r++) = TEXT('\t');
					en++;
				}
			}
		}
		else {
			*(r++) = *p;
		}
	}
	*r = TEXT('\0');
	string_insert(hWnd, bf, buf, lstrlen(buf), TRUE);
	mem_free(&buf);

	// 選択範囲の再設定
	bf->sp = first;
	bf->cp = en;
	InvalidateRect(hWnd, NULL, FALSE);
}

/*
 * draw_init - 描画情報の初期化
 */
static BOOL draw_init(const HWND hWnd, BUFFER *bf)
{
	HDC hdc;
	HBITMAP hBmp;
	HRGN hrgn[2];
	RECT rect;
	TEXTMETRIC tm;

	GetClientRect(hWnd, &rect);
	GetTextMetrics(bf->mdc, &tm);

	hdc = GetDC(hWnd);
	hBmp = CreateCompatibleBitmap(hdc, rect.right, tm.tmHeight + bf->spacing);
	bf->ret_bmp = SelectObject(bf->mdc, hBmp);
	ReleaseDC(hWnd, hdc);

	// リージョンの作成
	hrgn[0] = CreateRectRgnIndirect(&rect);
	bf->hrgn = CreateRectRgnIndirect(&rect);
	// 除去するリージョンの作成
	get_edit_rect(hWnd, bf, &rect);
	rect.left -= bf->line_no_width;
	hrgn[1] = CreateRectRgnIndirect(&rect);
	// リージョンの結合
	CombineRgn(bf->hrgn, hrgn[0], hrgn[1], RGN_DIFF);
	DeleteObject(hrgn[0]);
	DeleteObject(hrgn[1]);
	return TRUE;
}

/*
 * draw_free - 描画情報の解放
 */
static void draw_free(BUFFER *bf)
{
	HBITMAP hBmp;

	hBmp = SelectObject(bf->mdc, bf->ret_bmp);
	DeleteObject(hBmp);
	DeleteObject(bf->hrgn);
	bf->hrgn = NULL;
}

/*
 * draw_rect - 背景描画
 */
static void draw_rect_color(const HWND hWnd, const HDC mdc, const BUFFER *bf, const int left, const int top, const int right, const int bottom, COLORREF color)
{
	RECT trect;
	HBRUSH hBrush;

	if (right < 0) {
		return;
	}
	SetRect(&trect, left, top, right, bottom);

	hBrush = CreateSolidBrush(color);
	FillRect(mdc, &trect, hBrush);
	DeleteObject(hBrush);
}

/*
 * draw_rect - 矩形描画
 */
static void draw_rect(const HWND hWnd, const HDC mdc, const BUFFER *bf, const int left, const int top, const int right, const int bottom)
{
	RECT trect;
	HBRUSH hBrush;

	if (right < 0) {
		return;
	}
	if (bf->no_hide_sel == FALSE && GetFocus() != hWnd) {
		return;
	}
	SetRect(&trect, left, top, right, bottom);

	if (GetFocus() != hWnd) {
		hBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
	} else {
		hBrush = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
	}
	FillRect(mdc, &trect, hBrush);
	DeleteObject(hBrush);
}

/*
 * draw_string - 文字列描画
 */
static int draw_string(const HWND hWnd, const HDC mdc, const BUFFER *bf, const RECT *drect, const int left, const int top, const TCHAR *str, const int len, const BOOL sel)
{
	RECT rect;
	SIZE sz;

	GetTextExtentPoint32(bf->mdc, str, len, &sz);
	if (left + sz.cx >= drect->left) {
		if (sel == TRUE && (bf->no_hide_sel == TRUE || GetFocus() == hWnd)) {
			SetTextColor(mdc, GetSysColor((GetFocus() != hWnd) ? COLOR_BTNTEXT : COLOR_HIGHLIGHTTEXT));
			SetBkColor(mdc, GetSysColor((GetFocus() != hWnd) ? COLOR_BTNFACE : COLOR_HIGHLIGHT));
			draw_rect(hWnd, mdc, bf, left, drect->top, left + sz.cx, drect->bottom);
		}
		SetRect(&rect, left, drect->top, left + sz.cx, drect->bottom);
		ExtTextOut(mdc, left, top, 0, &rect, str, len, NULL);
		if (sel == TRUE) {
			SetTextColor(mdc, GetSysColor(COLOR_WINDOWTEXT));
			SetBkColor(mdc, GetSysColor(COLOR_WINDOW));
		}
	}
	return sz.cx;
}

/*
 * str_cmp_ni - ２つの文字列を大文字、小文字を区別しないで比較を行う
 */
static int str_cmp_ni(const TCHAR *buf1, const TCHAR *buf2, const int len)
{
#define ToLower(c)				((c >= TEXT('A') && c <= TEXT('Z')) ? (c - TEXT('A') + TEXT('a')) : c)
	int i = 0;
	for(; ToLower(*buf1) == ToLower(*buf2) && *buf1 != TEXT('\0') && i < len; i++, buf1++, buf2++);
	return ((i == len) ? 0 : ToLower(*buf1) - ToLower(*buf2));
}
static int check_word(const TCHAR *p, const TCHAR *word) {
	int len = lstrlen(word);
	if(str_cmp_ni(p, word, len) == 0){
		return len;
	}
	return 0;
}

/*
 * show_line_no - 行番号の描画
 */
static void show_line_no(const HDC mdc, BUFFER* bf, int line, const RECT* drect) {
	HBRUSH hBrush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
	RECT rect;
	SetRect(&rect, 0, drect->top, bf->line_no_width + bf->left_margin, drect->bottom);
	FillRect(bf->mdc, &rect, hBrush);
	DeleteObject(hBrush);

	HPEN hPen, retPen;
	hPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
	retPen = (HPEN)SelectObject(mdc, hPen);
	MoveToEx(mdc, bf->line_no_width - 1, drect->top, NULL);
	LineTo(mdc, bf->line_no_width - 1, drect->bottom);
	SelectObject(mdc, retPen);
	DeleteObject(hPen);

	SetTextColor(mdc, GetSysColor(COLOR_HIGHLIGHT));
	TCHAR tmp[100];
	wsprintf(tmp, TEXT("%d"), line + 1);
	SetTextAlign(mdc, TA_RIGHT);
	TextOut(mdc, bf->line_no_width - (bf->char_width * 8 / 10), drect->top, tmp, lstrlen(tmp));
	SetTextAlign(mdc, TA_LEFT);
}

/*
 * draw_line - 1行描画
 */
static void draw_line(const HWND hWnd, const HDC mdc, BUFFER *bf, const int i, const int left, const int right)
{
	RECT drect;
	TCHAR *p, *r, *s, *prev = NULL;
	DWORD j;
	int offset;
	int top;
	int width;
	int tab;
	COLORREF cur_color;
	BOOL break_word = TRUE;
	BOOL string_end = FALSE;

	offset = (bf->left_margin + bf->line_no_width) - (bf->pos_x * bf->char_width);
	width = (bf->pos_x * bf->char_width) + right;
	top = bf->spacing / 2;
	drect.left = left;
	drect.top = 0;
	drect.bottom = drect.top + bf->font_height;

	cur_color = GetSysColor(COLOR_WINDOWTEXT);
	SetTextColor(mdc, cur_color);

	if (bf->hightlight_line == i) {
		draw_rect_color(hWnd, mdc, bf, drect.left, drect.top, width, drect.bottom, COLOR_EXEC);
	}
	if (bf->error_line == i) {
		draw_rect_color(hWnd, mdc, bf, drect.left, drect.top, width, drect.bottom, COLOR_ERROR);
	}
	for (j = line_get(bf, i), s = p = index_to_char(bf, line_get(bf, i)); j < line_get(bf, i + 1) && offset <= width; j++, p = char_next(bf, p)) {
		if (s != p) {
			if (bf->ip != NULL && (p == bf->ip + bf->ip_len || p == bf->input_buf)) {
				// 入力バッファの出力
				r = (p == bf->input_buf) ? bf->ip : (bf->input_buf + bf->input_len);
				offset += draw_string(hWnd, mdc, bf, &drect, offset, top, s, r - s, bf->sel);
				s = p;
				if (cur_color != COLOR_COMMENT && cur_color != COLOR_STRING_D && cur_color != COLOR_STRING_S) {
					cur_color = GetSysColor(COLOR_WINDOWTEXT);
					SetTextColor(mdc, cur_color);
				}
			} else if (bf->dp != NULL && p == bf->dp + bf->del_len) {
				// 削除文字列
				offset += draw_string(hWnd, mdc, bf, &drect, offset, top, s, bf->dp - s, bf->sel);
				s = p;
				if (cur_color != COLOR_COMMENT && cur_color != COLOR_STRING_D && cur_color != COLOR_STRING_S) {
					cur_color = GetSysColor(COLOR_WINDOWTEXT);
					SetTextColor(mdc, cur_color);
				}
			} else if ((j % DRAW_LEN) == 0) {
				offset += draw_string(hWnd, mdc, bf, &drect, offset, top, s, p - s, bf->sel);
				s = p;
			}
		}
		if ((j >= bf->sp && j < bf->cp) || (j >= bf->cp && j < bf->sp)) {
			if (bf->sel == FALSE) {
				// 選択開始
				offset += draw_string(hWnd, mdc, bf, &drect, offset, top, s, p - s, bf->sel);
				s = p;
				bf->sel = TRUE;
			}
		} else if (bf->sel == TRUE) {
			// 選択終了
			offset += draw_string(hWnd, mdc, bf, &drect, offset, top, s, p - s, bf->sel);
			s = p;
			bf->sel = FALSE;
			SetTextColor(mdc, cur_color);
		}

		// 予約語の色変更
		if (break_word == TRUE && cur_color != COLOR_COMMENT && cur_color != COLOR_STRING_D && cur_color != COLOR_STRING_S) {
			int keyword_len = 0;
			int i;

			for (i = 0; i < sizeof(keyword) / sizeof(TCHAR) / KEYWORD_SIZE; i++) {
				int len;
				if((len = check_word(p, keyword[i])) > 0){
					keyword_len = len;
					break;
				}
			}
			if (bf->extension_mode == TRUE) {
				for (i = 0; i < sizeof(keyword_extension) / sizeof(TCHAR) / KEYWORD_SIZE; i++) {
					int len;
					if ((len = check_word(p, keyword_extension[i])) > 0) {
						keyword_len = len;
						break;
					}
				}
			}
			if (keyword_len > 0) {
				TCHAR c = *(p + keyword_len);
				if ((c >= TEXT('a') && c <= TEXT('z')) || (c >= TEXT('A') && c <= TEXT('Z')) ||
					(c >= TEXT('0') && c <= TEXT('9')) || c == TEXT('_')) {
					keyword_len = 0;
				}
			}
			if (keyword_len > 0) {
				// 予約語開始
				offset += draw_string(hWnd, mdc, bf, &drect, offset, top, s, p - s, bf->sel);
				s = p;
				cur_color = COLOR_KEYWORD;
				SetTextColor(mdc, cur_color);
			}
		}
		if (cur_color == COLOR_KEYWORD &&
			!((*p >= TEXT('a') && *p <= TEXT('z')) || (*p >= TEXT('A') && *p <= TEXT('Z')) ||
			(*p >= TEXT('0') && *p <= TEXT('9')) || *p == TEXT('_') || *p == TEXT('#'))) {
			// 予約後と変数終了
			offset += draw_string(hWnd, mdc, bf, &drect, offset, top, s, p - s, bf->sel);
			s = p;
			cur_color = GetSysColor(COLOR_WINDOWTEXT);
			SetTextColor(mdc, cur_color);
		}
		if (!((*p >= TEXT('a') && *p <= TEXT('z')) || (*p >= TEXT('A') && *p <= TEXT('Z')) ||
			(*p >= TEXT('0') && *p <= TEXT('9')) || *p == TEXT('_') || *p == TEXT('#'))) {
			break_word = TRUE;
		} else {
			break_word = FALSE;
		}
		// コメントの色変更
		if (*p == TEXT('/') && *(p + 1) == TEXT('/') && cur_color != COLOR_STRING_D && cur_color != COLOR_STRING_S) {
			offset += draw_string(hWnd, mdc, bf, &drect, offset, top, s, p - s, bf->sel);
			s = p;
			cur_color = COLOR_COMMENT;
			SetTextColor(mdc, cur_color);
			string_end = FALSE;
		}
		// 文字列終了
		if (string_end == TRUE) {
			string_end = FALSE;
			offset += draw_string(hWnd, mdc, bf, &drect, offset, top, s, p - s, bf->sel);
			s = p;
			cur_color = GetSysColor(COLOR_WINDOWTEXT);
			SetTextColor(mdc, cur_color);
		}
		// 文字列の色変更
		if (bf->extension_mode == TRUE &&
			*p == TEXT('"') && (prev == NULL || *prev != TEXT('\\')) &&
			cur_color != COLOR_COMMENT && cur_color != COLOR_STRING_S) {
			if (cur_color == COLOR_STRING_D) {
				string_end = TRUE;
			} else {
				offset += draw_string(hWnd, mdc, bf, &drect, offset, top, s, p - s, bf->sel);
				s = p;
				cur_color = COLOR_STRING_D;
				SetTextColor(mdc, cur_color);
			}
		}
		if (bf->extension_mode == TRUE && 
			*p == TEXT('\'') && (prev == NULL || *prev != TEXT('\\')) &&
			cur_color != COLOR_COMMENT && cur_color != COLOR_STRING_D) {
			if (cur_color == COLOR_STRING_S) {
				string_end = TRUE;
			} else {
				offset += draw_string(hWnd, mdc, bf, &drect, offset, top, s, p - s, bf->sel);
				s = p;
				cur_color = COLOR_STRING_S;
				SetTextColor(mdc, cur_color);
			}
		}
		if (*p == TEXT('\r') || *p == TEXT('\n')) {
			// return
			offset += draw_string(hWnd, mdc, bf, &drect, offset, top, s, p - s, bf->sel);
			s = p;
			if (bf->sel == TRUE && offset + bf->char_width >= 0) {
				draw_rect(hWnd, mdc, bf, offset, top - (bf->spacing / 2),
					offset + bf->char_width, top - (bf->spacing / 2) + bf->font_height);
			}
			cur_color = GetSysColor(COLOR_WINDOWTEXT);
			SetTextColor(mdc, cur_color);
			string_end = FALSE;
			break;
		} else if (*p == TEXT('\t')) {
			// Tab
			offset += draw_string(hWnd, mdc, bf, &drect, offset, top, s, p - s, bf->sel);

			tab = bf->tab_stop * bf->char_width -
				((offset - (bf->left_margin + bf->line_no_width) + (bf->pos_x * bf->char_width)) % (bf->tab_stop * bf->char_width));
			if (tab < bf->char_width) {
				tab += bf->tab_stop * bf->char_width;
			}
			if (bf->sel == TRUE && offset + tab >= 0) {
				draw_rect(hWnd, mdc, bf, offset, top - (bf->spacing / 2),
					offset + tab, top - (bf->spacing / 2) + bf->font_height);
			}
			offset += tab;

			s = char_next(bf, p);
			prev = p;
			continue;
		}
		if (is_lead_byte(bf, p) == TRUE && char_next(bf, p) == (p + 1)) {
			p = char_next(bf, p);
			j++;
		} else if ((cur_color == COLOR_STRING_S || cur_color == COLOR_STRING_D) && *p == TEXT('\\') && (prev != NULL && *prev == TEXT('\\'))) {
			prev = NULL;
			continue;
		}
		prev = p;
	}
	if (s != p && bf->ip != NULL && (p == bf->ip + bf->ip_len || p == bf->input_buf)) {
		r = (p == bf->input_buf) ? bf->ip : (bf->input_buf + bf->input_len);
	} else if (s != p && bf->dp != NULL && p == bf->dp + bf->del_len) {
		r = bf->dp;
	} else {
		r = p;
	}
	draw_string(hWnd, mdc, bf, &drect, offset, top, s, r - s, bf->sel);
	if (bf->show_line_no == TRUE) {
		// 行番号の描画
		show_line_no(mdc, bf, i, &drect);
	}
}

/*
 * caret_set_size - キャレットのサイズ設定
 */
static void caret_set_size(const HWND hWnd, BUFFER *bf)
{
	TCHAR *p;
	int csize;
	int len;

	p = index_to_char(bf, bf->cp);
	csize = get_char_extent(bf, p, &len);
	if (csize <= 0) {
		csize = bf->char_width;
	}
	DestroyCaret();
	CreateCaret(hWnd, NULL, csize, bf->font_height);
}

/*
 * caret_char_to_caret - 文字位置からキャレットの位置取得
 */
static int caret_char_to_caret(const HDC mdc, BUFFER *bf, const int i, const DWORD cp)
{
	SIZE sz;
	TCHAR *p, *r, *s;
	DWORD j;
	int offset;
	int tab;

	offset = (bf->left_margin + bf->line_no_width) - (bf->pos_x * bf->char_width);
	for (j = line_get(bf, i), s = p = index_to_char(bf, line_get(bf, i)); j < line_get(bf, i + 1); j++, p = char_next(bf, p)) {
		r = NULL;
		if (s != p && bf->ip != NULL && (p == bf->ip + bf->ip_len || p == bf->input_buf)) {
			// 入力バッファ
			r = (p == bf->input_buf) ? bf->ip : (bf->input_buf + bf->input_len);
		} else if (s != p && bf->dp != NULL && p == bf->dp + bf->del_len) {
			// 削除文字列
			r = bf->dp;
		}
		if (r != NULL) {
			GetTextExtentPoint32(mdc, s, r - s, &sz);
			offset += sz.cx;
			s = p;
		}
		if (j >= cp) {
			break;
		}

		if (*p == TEXT('\r') || *p == TEXT('\n')) {
			break;
		} else if (*p == TEXT('\t')) {
			// tab
			GetTextExtentPoint32(mdc, s, p - s, &sz);
			offset += sz.cx;
			tab = bf->tab_stop * bf->char_width - ((offset - (bf->left_margin + bf->line_no_width) + (bf->pos_x * bf->char_width)) % (bf->tab_stop * bf->char_width));
			if (tab < bf->char_width) {
				tab += bf->tab_stop * bf->char_width;
			}
			offset += tab;
			s = char_next(bf, p);
			continue;
		}
		if (is_lead_byte(bf, p) == TRUE) {
			p = char_next(bf, p);
			j++;
		}
	}
	if (s != p && bf->ip != NULL && (p == bf->ip + bf->ip_len || p == bf->input_buf)) {
		r = (p == bf->input_buf) ? bf->ip : (bf->input_buf + bf->input_len);
	} else if (s != p && bf->dp != NULL && p == bf->dp + bf->del_len) {
		r = bf->dp;
	} else {
		r = p;
	}
	GetTextExtentPoint32(mdc, s, r - s, &sz);
	return (offset + sz.cx);
}

/*
 * caret_point_to_caret - 座標からキャレットの位置取得
 */
static DWORD caret_point_to_caret(BUFFER *bf, const int x, const int y)
{
	TCHAR *p;
	DWORD j;
	int offset, old;
	int tab;
	int clen = 1;
	int i;

	i = bf->pos_y + ((y - bf->top_margin) / bf->font_height);
	if (i < 0) {
		return 0;
	} else if (i >= bf->line_len) {
		i = bf->line_len - 1;
	}
	old = offset = (bf->left_margin + bf->line_no_width) - (bf->pos_x * bf->char_width);
	for (j = line_get(bf, i), p = index_to_char(bf, line_get(bf, i)); j < line_get(bf, i + 1); j++, p = char_next(bf, p)) {
		if (*p == TEXT('\r') || *p == TEXT('\n')) {
			return j;
		} else if (*p == TEXT('\t')) {
			// tab
			clen = 1;
			tab = bf->tab_stop * bf->char_width - ((offset - (bf->left_margin + bf->line_no_width) + (bf->pos_x * bf->char_width)) % (bf->tab_stop * bf->char_width));
			if (tab < bf->char_width) {
				tab += bf->tab_stop * bf->char_width;
			}
			offset += tab;
			if (offset > x) {
				break;
			}
			old = offset;
		} else {
			offset += get_char_extent(bf, p, &clen);
			if (offset > x) {
				if ((offset - old) / 2 < x - old) {
					j += clen;
				}
				break;
			}
			old = offset;
			if (clen == 2) {
				p = char_next(bf, p);
				j++;
			}
		}
	}
	if (j == line_get(bf, i + 1) && (i + 1) < bf->line_len) {
		j -= clen;
	}
	return j;
}

/*
 * caret_get_token - キャレット位置のトークンを取得
 */
static void caret_get_token(BUFFER *bf)
{
	TCHAR *p, *r;
	int i;

	i = index_to_line(bf, bf->cp);
	for (; i > 0; i--) {
		// 論理行の先頭に移動
		p = index_to_char(bf, line_get(bf, i) - 1);
		if (*p == TEXT('\r') || *p == TEXT('\n')) {
			break;
		}
	}
	p = index_to_char(bf, line_get(bf, i));
	while (char_to_index(bf, p) < BUF_LEN && *p != TEXT('\r') && *p != TEXT('\n')) {
#ifdef UNICODE
		if (is_lead_byte(bf, p) == TRUE || WideCharToMultiByte(CP_ACP, 0, p, 1, NULL, 0, NULL, NULL) != 1) {
			r = p;
			while (is_lead_byte(bf, p) == TRUE || WideCharToMultiByte(CP_ACP, 0, p, 1, NULL, 0, NULL, NULL) != 1) {
				if (is_lead_byte(bf, p) == TRUE) {
					p = char_next(bf, p);
				}
				p = char_next(bf, p);
			}
#else
		if (is_lead_byte(bf, p) == TRUE) {
			r = p;
			while (is_lead_byte(bf, p) == TRUE && *(p + 1) != TEXT('\0')) {
				p = char_next(bf, p);
				p = char_next(bf, p);
		}
#endif
		} else if ((*p >= TEXT('a') && *p <= TEXT('z')) || (*p >= TEXT('A') && *p <= TEXT('Z')) ||
					(*p >= TEXT('0') && *p <= TEXT('9')) || *p == TEXT('_')) {
			r = p;
			while (char_to_index(bf, p) < BUF_LEN && is_lead_byte(bf, p) == FALSE &&
				(*p >= TEXT('a') && *p <= TEXT('z')) || (*p >= TEXT('A') && *p <= TEXT('Z')) ||
					(*p >= TEXT('0') && *p <= TEXT('9')) || *p == TEXT('_')) {
				p = char_next(bf, p);
			}
		} else {
			r = p;
			p = char_next(bf, p);
		}
		if (p > index_to_char(bf, bf->cp)) {
			bf->cp = char_to_index(bf, p);
			bf->sp = char_to_index(bf, r);
			break;
		}
	}
}

/*
 * caret_move - キャレットの移動
 */
static void caret_move(const HWND hWnd, BUFFER *bf, const int key)
{
	RECT rect;
	POINT pt;
	TCHAR *p;
	DWORD oldcp, oldsp;
	DWORD j;
	int i, t;

	get_edit_rect(hWnd, bf, &rect);

	oldcp = bf->cp;
	oldsp = bf->sp;
	i = index_to_line(bf, bf->cp);

	switch (key) {
	case VK_HOME:
		if (GetKeyState(VK_CONTROL) < 0) {
			// 全体の先頭
			bf->cp = 0;
		} else {
			// 論理行頭
			for (; *(index_to_char(bf, bf->cp)) == TEXT('\r') || *(index_to_char(bf, bf->cp)) == TEXT('\n'); bf->cp--)
				;
			for (; bf->cp > 0 && *(index_to_char(bf, bf->cp)) != TEXT('\r') && *(index_to_char(bf, bf->cp)) != TEXT('\n'); bf->cp--)
				;
			if (bf->cp > 0) {
				bf->cp++;
			}
		}
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case VK_END:
		if (GetKeyState(VK_CONTROL) < 0) {
			// 全体の末尾
			bf->cp = BUF_LEN;
		} else {
			// 論理行末
			for (; bf->cp < BUF_LEN && *(index_to_char(bf, bf->cp)) != TEXT('\r') && *(index_to_char(bf, bf->cp)) != TEXT('\n'); bf->cp++)
				;
		}
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case VK_PRIOR:
		// Page UP
		if (GetCaretPos(&pt) == FALSE) {
			break;
		}
		if (bf->cpx == 0) {
			bf->cpx = pt.x;
		}
		bf->cp = caret_point_to_caret(bf, bf->cpx, pt.y - (rect.bottom - rect.top));
		if (GetKeyState(VK_SHIFT) >= 0) {
			bf->sp = bf->cp;
		}
		SendMessage(hWnd, WM_VSCROLL, SB_PAGEUP, 0);
		break;

	case VK_NEXT:
		// Page Down
		if (GetCaretPos(&pt) == FALSE) {
			break;
		}
		if (bf->cpx == 0) {
			bf->cpx = pt.x;
		}
		bf->cp = caret_point_to_caret(bf, bf->cpx, pt.y + (rect.bottom - rect.top));
		if (GetKeyState(VK_SHIFT) >= 0) {
			bf->sp = bf->cp;
		}
		SendMessage(hWnd, WM_VSCROLL, SB_PAGEDOWN, 0);
		break;

	case VK_UP:
		if (--i < 0) {
			if (GetKeyState(VK_SHIFT) >= 0) {
				bf->sp = bf->cp;
			}
			break;
		}
		if (GetCaretPos(&pt) == FALSE) {
			break;
		}
		if (bf->cpx == 0) {
			bf->cpx = pt.x;
		}
		bf->cp = caret_point_to_caret(bf, bf->cpx, pt.y - bf->font_height);
		break;

	case VK_DOWN:
		if (++i > bf->line_len - 1) {
			if (GetKeyState(VK_SHIFT) >= 0) {
				bf->sp = bf->cp;
			}
			break;
		}
		if (GetCaretPos(&pt) == FALSE) {
			break;
		}
		if (bf->cpx == 0) {
			bf->cpx = pt.x;
		}
		bf->cp = caret_point_to_caret(bf, bf->cpx, pt.y + bf->font_height);
		break;

	case VK_LEFT:
		bf->cpx = 0;
		if (bf->cp != bf->sp && GetKeyState(VK_SHIFT) >= 0) {
			// 選択解除
			if (bf->cp > bf->sp) {
				SWAP(bf->cp, bf->sp);
			}
			bf->sp = bf->cp;
			break;
		}
		if (bf->cp == line_get(bf, i) && --i < 0) {
			break;
		}
		for (t = j = line_get(bf, i); j < line_get(bf, i + 1) && j < bf->cp; j++) {
			t = j;
			p = index_to_char(bf, j);
			if (*p == TEXT('\r') || *p == TEXT('\n')) {
				break;
			}
			if (is_lead_byte(bf, p) == TRUE) {
				p = char_next(bf, p);
				j++;
			}
		}
		bf->cp = t;
		break;

	case VK_RIGHT:
		bf->cpx = 0;
		if (bf->cp != bf->sp && GetKeyState(VK_SHIFT) >= 0) {
			// 選択解除
			if (bf->cp < bf->sp) {
				SWAP(bf->cp, bf->sp);
			}
			bf->sp = bf->cp;
			break;
		}
		if (bf->cp >= BUF_LEN) {
			break;
		}
		if ((bf->cp + 1) < BUF_LEN &&
			*(index_to_char(bf, bf->cp)) == TEXT('\r') && *(index_to_char(bf, bf->cp + 1)) == TEXT('\n')) {
			bf->cp += 2;
		} else if (*(index_to_char(bf, bf->cp)) == TEXT('\r') || *(index_to_char(bf, bf->cp)) == TEXT('\n')) {
			bf->cp++;
		} else {
			if (is_lead_byte(bf, index_to_char(bf, bf->cp)) == TRUE) {
				bf->cp += 2;
			} else {
				bf->cp++;
			}
		}
		break;
	}
	if (GetKeyState(VK_SHIFT) >= 0) {
		bf->sp = bf->cp;
	}
	if (oldsp != bf->sp) {
		line_refresh(hWnd, bf, oldcp, oldsp);
	}
	line_refresh(hWnd, bf, oldcp, bf->cp);
	ensure_visible(hWnd, bf);
}

/*
 * nedit_proc - Editウィンドウプロシージャ
 */
static LRESULT CALLBACK nedit_proc(const HWND hWnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
	BUFFER *bf;
	HDC hdc;
	RECT rect;
	TCHAR in[3];
	DWORD cp, sp;
	static DWORD DoubleClkTime;
	static DWORD TripleClkTime;
	static POINT ClickPt;
	int len;
	int i, j;
#ifdef OP_XP_STYLE
	static FARPROC _OpenThemeData;
	static FARPROC _CloseThemeData;
	static FARPROC _DrawThemeBackground;
#endif	// OP_XP_STYLE
	HIMC hIMC;
	COMPOSITIONFORM cf;
	POINT pt;

	switch (msg) {
	case WM_CREATE:
		if ((bf = mem_calloc(sizeof(BUFFER))) == NULL) {
			return -1;
		}
#ifdef OP_XP_STYLE
		// XP
		if ((bf->hModThemes = LoadLibrary(TEXT("uxtheme.dll"))) != NULL) {
			if (_OpenThemeData == NULL) {
				_OpenThemeData = GetProcAddress(bf->hModThemes, "OpenThemeData");
			}
			if (_OpenThemeData != NULL) {
				bf->hTheme = (HTHEME)_OpenThemeData(hWnd, L"Edit");
			}
		}
#endif	// OP_XP_STYLE

		bf->id = (int)((LPCREATESTRUCT)lParam)->hMenu;

		i = (int)GetWindowLongPtr(hWnd, GWL_STYLE);
		bf->tab_stop = TAB_STOP;
		bf->left_margin = bf->top_margin = bf->right_margin = bf->bottom_margin = 1;
		bf->spacing = 0;
		bf->line_max = LINE_MAX;
		bf->wordwrap = (i & WS_HSCROLL) ? FALSE : TRUE;
		bf->insert_mode = TRUE;
		bf->lock = (i & ES_READONLY) ? TRUE : FALSE;
		bf->no_hide_sel = (i & ES_NOHIDESEL) ? TRUE : FALSE;
		bf->lowercase = (i & ES_LOWERCASE) ? TRUE : FALSE;
		bf->uppercase = (i & ES_UPPERCASE) ? TRUE : FALSE;
		bf->hightlight_line = -1;
		bf->error_line = -1;

		if (string_init(bf) == FALSE) {
			return -1;
		}
		// バッファの初期化
		bf->buf_size = RESERVE_BUF;
		if ((bf->buf = mem_alloc(sizeof(TCHAR) * bf->buf_size)) == NULL) {
			return -1;
		}
		*bf->buf = TEXT('\0');

		// 描画情報の初期化
		hdc = GetDC(hWnd);
		bf->mdc = CreateCompatibleDC(hdc);
		ReleaseDC(hWnd, hdc);
		draw_init(hWnd, bf);

		SetMapMode(bf->mdc, MM_TEXT);
		SetTextCharacterExtra(bf->mdc, 0);
		SetTextJustification(bf->mdc, 0, 0);
		SetTextAlign(bf->mdc, TA_TOP | TA_LEFT);
		SetBkMode(bf->mdc, TRANSPARENT);

		// buffer info to window long
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)bf);
		SendMessage(hWnd, WM_REFLECT, 0, 0);

		// 初期文字列の設定
		if (((LPCREATESTRUCT)lParam)->lpszName != NULL) {
			SendMessage(hWnd, WM_SETTEXT, 0, (LPARAM)((LPCREATESTRUCT)lParam)->lpszName);
		}
		break;

	case WM_DESTROY:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) != NULL) {
			SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)0);

#ifdef OP_XP_STYLE
			// XP
			if (bf->hTheme != NULL) {
				if (_CloseThemeData == NULL) {
					_CloseThemeData = GetProcAddress(bf->hModThemes, "CloseThemeData");
				}
				if (_CloseThemeData != NULL) {
					_CloseThemeData(bf->hTheme);
				}
			}
			if (bf->hModThemes != NULL) {
				FreeLibrary(bf->hModThemes);
			}
#endif	// OP_XP_STYLE
			draw_free(bf);
			if (bf->hfont != NULL) {
				SelectObject(bf->mdc, bf->ret_font);
				DeleteObject(bf->hfont);
			}
			DeleteDC(bf->mdc);
			mem_free(&bf->buf);
			mem_free(&bf->input_buf);
			mem_free(&bf->line);
			undo_free(bf, 0);
			mem_free(&bf->undo);
			mem_free(&bf);
		}
		if (GetFocus() == hWnd) {
			DestroyCaret();
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);

	case WM_SETFOCUS:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		CreateCaret(hWnd, NULL, 0, bf->font_height);
		ShowCaret(hWnd);
		line_refresh(hWnd, bf, bf->cp, bf->sp);
		// 親ウィンドウに通知
		notify_message(hWnd, bf, EN_SETFOCUS);
		break;

	case WM_KILLFOCUS:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		HideCaret(hWnd);
		DestroyCaret();
		line_refresh(hWnd, bf, bf->cp, bf->sp);
		// 親ウィンドウに通知
		notify_message(hWnd, bf, EN_KILLFOCUS);
		break;

	case WM_SIZE:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		if (wParam == SIZE_MINIMIZED) {
			break;
		}
		get_edit_rect(hWnd, bf, &rect);
		if (bf->wordwrap == TRUE && (rect.right - rect.left) > 0) {
			// 行情報の再設定
			bf->width = (rect.right - rect.left);
			bf->line_len = 0;
			line_set_info(hWnd, bf);
		}
		set_scrollbar(hWnd, bf);

		draw_free(bf);
		draw_init(hWnd, bf);

		InvalidateRect(hWnd, NULL, FALSE);

		// IMEの設定
		hIMC = ImmGetContext(hWnd);
		GetCaretPos(&pt);
		cf.dwStyle = CFS_POINT | CFS_RECT;
		cf.ptCurrentPos.x = pt.x;
		cf.ptCurrentPos.y = pt.y + (bf->spacing / 2);
		get_edit_rect(hWnd, bf, &cf.rcArea);
		ImmSetCompositionWindow(hIMC, &cf);
		ImmReleaseContext(hWnd, hIMC);
		break;

	case WM_GETDLGCODE:
		return DLGC_WANTALLKEYS;

	case WM_HSCROLL:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		get_edit_rect(hWnd, bf, &rect);
		i = bf->pos_x;
		switch ((int)LOWORD(wParam)) {
		case SB_TOP:
			bf->pos_x = 0;
			break;

		case SB_BOTTOM:
			bf->pos_x = bf->max_x;
			break;

		case SB_LINELEFT:
			bf->pos_x = (bf->pos_x > 0) ? bf->pos_x - 1 : 0;
			break;

		case SB_LINERIGHT:
			bf->pos_x = (bf->pos_x < bf->max_x) ? bf->pos_x + 1 : bf->max_x;
			break;

		case SB_PAGELEFT:
			bf->pos_x = (bf->pos_x - ((rect.right - rect.left) / bf->char_width) > 0) ?
				bf->pos_x - ((rect.right - rect.left) / bf->char_width) : 0;
			break;

		case SB_PAGERIGHT:
			bf->pos_x = (bf->pos_x + ((rect.right - rect.left) / bf->char_width) < bf->max_x) ?
				bf->pos_x + ((rect.right - rect.left) / bf->char_width) : bf->max_x;
			break;

		case SB_THUMBPOSITION:
		case SB_THUMBTRACK:
			{
				SCROLLINFO si;

				ZeroMemory(&si, sizeof(SCROLLINFO));
				si.cbSize = sizeof(SCROLLINFO);
				si.fMask = SIF_ALL;
				GetScrollInfo(hWnd, SB_HORZ, &si);
				bf->pos_x = si.nTrackPos;
			}
			break;
		}
		switch ((int)LOWORD(wParam)) {
		case SB_TOP:
		case SB_BOTTOM:
		case SB_LINELEFT:
		case SB_LINERIGHT:
		case SB_PAGELEFT:
		case SB_PAGERIGHT:
			if (i - bf->pos_x != 0) {
				// 親ウィンドウに通知
				notify_message(hWnd, bf, EN_HSCROLL);
			}
			break;
		}
		SetScrollPos(hWnd, SB_HORZ, bf->pos_x, TRUE);
		ScrollWindowEx(hWnd, (i - bf->pos_x) * bf->char_width, 0, NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
		break;

	case WM_VSCROLL:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		get_edit_rect(hWnd, bf, &rect);
		rect.left -= bf->line_no_width;
		i = bf->pos_y;
		switch ((int)LOWORD(wParam)) {
		case SB_TOP:
			bf->pos_y = 0;
			break;

		case SB_BOTTOM:
			bf->pos_y = bf->max_y;
			break;

		case SB_LINEUP:
			bf->pos_y = (bf->pos_y > 0) ? bf->pos_y - 1 : 0;
			break;

		case SB_LINEDOWN:
			bf->pos_y = (bf->pos_y < bf->max_y) ? bf->pos_y + 1 : bf->max_y;
			break;

		case SB_PAGEUP:
			bf->pos_y = (bf->pos_y - (((rect.bottom - rect.top) / bf->font_height) - 1) > 0) ?
				bf->pos_y - (((rect.bottom - rect.top) / bf->font_height) - 1) : 0;
			break;

		case SB_PAGEDOWN:
			bf->pos_y = (bf->pos_y + (((rect.bottom - rect.top) / bf->font_height) - 1) < bf->max_y) ?
				bf->pos_y + (((rect.bottom - rect.top) / bf->font_height) - 1) : bf->max_y;
			break;

		case SB_THUMBPOSITION:
		case SB_THUMBTRACK:
			{
				SCROLLINFO si;

				ZeroMemory(&si, sizeof(SCROLLINFO));
				si.cbSize = sizeof(SCROLLINFO);
				si.fMask = SIF_ALL;
				GetScrollInfo(hWnd, SB_VERT, &si);
				bf->pos_y = si.nTrackPos;
			}
			break;
		}
		switch ((int)LOWORD(wParam)) {
		case SB_TOP:
		case SB_BOTTOM:
		case SB_LINEUP:
		case SB_LINEDOWN:
		case SB_PAGEUP:
		case SB_PAGEDOWN:
			if (i - bf->pos_y != 0) {
				// 親ウィンドウに通知
				notify_message(hWnd, bf, EN_VSCROLL);
			}
			break;
		}
		SetScrollPos(hWnd, SB_VERT, bf->pos_y, TRUE);
		ScrollWindowEx(hWnd, 0, (i - bf->pos_y) * bf->font_height, NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
		break;

	case WM_KEYDOWN:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		switch (wParam) {
		case VK_APPS:
			SendMessage(hWnd, WM_CONTEXTMENU, 0, 0);
			break;

		case VK_INSERT:
			if (GetKeyState(VK_CONTROL) < 0) {
				// コピー
				SendMessage(hWnd, WM_COPY, 0, 0);
			} else if (GetKeyState(VK_SHIFT) < 0) {
				// 貼り付け
				SendMessage(hWnd, WM_PASTE, 0, 0);
			} else {
				// 入力モード切替
				bf->insert_mode = !bf->insert_mode;

				DestroyCaret();
				CreateCaret(hWnd, NULL, 0, bf->font_height);
				ShowCaret(hWnd);
				line_refresh(hWnd, bf, bf->cp, bf->cp);
			}
			break;

		case VK_DELETE:
			if (bf->lock == TRUE) {
				break;
			}
			if (GetKeyState(VK_SHIFT) < 0) {
				// 切り取り
				SendMessage(hWnd, WM_CUT, 0, 0);
			} else {
				// 削除
				string_delete_char(hWnd, bf, bf->cp);
			}
			break;

		case VK_BACK:
			if (bf->lock == TRUE) {
				break;
			}
			if (bf->cp == bf->sp) {
				if (bf->cp <= 0) {
					break;
				}
				caret_move(hWnd, bf, VK_LEFT);
			}
			string_delete_char(hWnd, bf, bf->cp);
			break;

		case 'C':
			if (GetKeyState(VK_CONTROL) < 0) {
				SendMessage(hWnd, WM_COPY, 0, 0);
			}
			break;

		case 'X':
			if (GetKeyState(VK_CONTROL) < 0) {
				SendMessage(hWnd, WM_CUT, 0, 0);
			}
			break;

		case 'V':
			if (GetKeyState(VK_CONTROL) < 0) {
				SendMessage(hWnd, WM_PASTE, 0, 0);
			}
			break;

		case 'Z':
			if (GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) < 0) {
				// やり直し
				SendMessage(hWnd, EM_REDO, 0, 0);
			} else if (GetKeyState(VK_CONTROL) < 0) {
				// 元に戻す
				SendMessage(hWnd, EM_UNDO, 0, 0);
			}
			break;

		case 'Y':
			if (GetKeyState(VK_CONTROL) < 0) {
				SendMessage(hWnd, EM_REDO, 0, 0);
			}
			break;

		case VK_TAB:
			if (GetKeyState(VK_CONTROL) < 0) {
				SetFocus(GetNextWindow(hWnd, GW_HWNDNEXT));
			}
			break;

		case VK_HOME:
		case VK_END:
		case VK_PRIOR:
		case VK_NEXT:
		case VK_UP:
		case VK_DOWN:
		case VK_LEFT:
		case VK_RIGHT:
			string_flush(bf, TRUE);
			caret_move(hWnd, bf, (int)wParam);
			break;
		}
		break;

	case WM_CHAR:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->lock == TRUE) {
			break;
		}
		if (GetKeyState(VK_CONTROL) < 0) {
			break;
		}
		switch (wParam) {
		case TEXT(':'):
			string_insert(hWnd, bf, TEXT(":"), 1, bf->insert_mode);
			{
				TCHAR *p = index_to_char(bf, line_get(bf, index_to_line(bf, bf->cp)));
				TCHAR *r = NULL, *s;
				BOOL case_flag = FALSE;
				BOOL switch_flag = FALSE;
				int i;

				string_flush(bf, TRUE);
				// case、defaultの行かチェック
				for (s = p; *s != TEXT('\0') && *s != TEXT('\r') && *s != TEXT('\n'); s = char_next(bf, s)) {
					if (str_cmp_ni(s, TEXT("case"), lstrlen(TEXT("case"))) == 0 || str_cmp_ni(s, TEXT("default"), lstrlen(TEXT("default"))) == 0) {
						case_flag = TRUE;
					}
				}
				if (case_flag == FALSE) {
					break;
				}
				// switch文の検索
				i = index_to_line(bf, bf->cp) - 1;
				while (switch_flag == FALSE && i >= 0) {
					r = index_to_char(bf, line_get(bf, i));
					for (s = r; *s != TEXT('\0') && *s != TEXT('\r') && *s != TEXT('\n'); s = char_next(bf, s)) {
						if (str_cmp_ni(s, TEXT("switch"), lstrlen(TEXT("switch"))) == 0) {
							switch_flag = TRUE;
						}
					}
					i--;
				}
				if (switch_flag == TRUE) {
					// switch文のインデントを取得
					for (; *r == TEXT('\t') || *r == TEXT(' '); r = char_next(bf, r)) {
						if (*p != TEXT('\t') && *p != TEXT(' ')) {
							break;
						}
						p = char_next(bf, p);
					}
					//  caseをswitch文のインデントに合わせる
					if (*p == TEXT('\t') || *p == TEXT(' ')) {
						int st = char_to_index(bf, p);
						int en;
						for (; *p == TEXT('\t') || *p == TEXT(' '); p = char_next(bf, p));
						en = char_to_index(bf, p);
						string_delete(hWnd, bf, st, en);
						bf->cp = bf->sp = bf->cp - (en - st);
					}
				}
			}
			string_flush(bf, TRUE);
			break;
		case VK_RETURN:
			// 改行
			{
				// 自動インデント
				TCHAR *cp = index_to_char(bf, bf->cp - 1);
				TCHAR *p = index_to_char(bf, line_get(bf, index_to_line(bf, bf->cp)));
				if (p == index_to_char(bf, bf->cp)) {
					// 行の先頭の場合は自動インデントしない
					string_insert(hWnd, bf, TEXT("\r\n"), 2, TRUE);
					break;
				}
				string_insert(hWnd, bf, TEXT("\r\n"), 2, TRUE);

				for (; *p == TEXT('\t') || *p == TEXT(' '); p = char_next(bf, p)) {
					// 前行と同じインデントを維持
					string_insert(hWnd, bf, p, 1, TRUE);
				}
				for (; *cp == TEXT('\t') || *cp == TEXT(' '); cp = char_prev(bf, cp));
				if (*cp == TEXT('{') || *cp == TEXT(':')) {
					// ブロックに入った場合はインデントを増やす
					string_insert(hWnd, bf, TEXT("\t"), 1, TRUE);
				}
			}
			string_flush(bf, TRUE);
			break;

		case VK_BACK:
			break;

		case VK_ESCAPE:
			break;

		case VK_TAB:
			{
				BOOL line_select_flag = FALSE;
				if (bf->cp != bf->sp) {
					DWORD st = (bf->cp > bf->sp) ? bf->sp : bf->cp;
					DWORD en = (bf->cp > bf->sp) ? bf->cp : bf->sp;
					DWORD i;
					// 選択範囲に改行が含まれるかチェック
					for (i = st; i < en; i++) {
						if (*index_to_char(bf, i) == TEXT('\r') || *index_to_char(bf, i) == TEXT('\n')) {
							line_select_flag = TRUE;
							break;
						}
					}
				}
				if (line_select_flag == FALSE) {
					// 選択なしか、選択に改行が含まれていない場合はTABを入力
					in[0] = (TCHAR)wParam;
					string_insert(hWnd, bf, in, 1, bf->insert_mode);
					break;
				}
				// 選択行のインデントを変更
				set_select_indent(hWnd, bf);
			}
			break;

		default:
			in[0] = (TCHAR)wParam;
			// 自動インデント
			if (*in == TEXT('}') && *index_to_char(bf, bf->cp - 1) == TEXT('\t')) {
				// ブロックから抜ける場合はインデントを減らす
				caret_move(hWnd, bf, VK_LEFT);
				string_delete_char(hWnd, bf, bf->cp);
			}
			string_insert(hWnd, bf, in, 1, bf->insert_mode);
			break;
		}
		break;

	case WM_IME_CHAR:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->lock == TRUE) {
			break;
		}
		if (GetKeyState(VK_CONTROL) < 0) {
			break;
		}
#ifdef UNICODE
		in[0] = (TCHAR)wParam;
		string_insert(hWnd, bf, in, 1, bf->insert_mode);
#else
		in[0] = (TCHAR)wParam >> 8;
		in[1] = (TCHAR)wParam & 0xFF;
		string_insert(hWnd, bf, in, 2, bf->insert_mode);
#endif
		break;

	case WM_IME_STARTCOMPOSITION:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) != NULL) {
			LOGFONT lf;

			ensure_visible(hWnd, bf);

			hIMC = ImmGetContext(hWnd);
			if (bf->hfont != NULL) {
				// フォントの設定
				GetObject(bf->hfont, sizeof(LOGFONT), &lf);
				ImmSetCompositionFont(hIMC, &lf);
			}
			// 位置の設定
			GetCaretPos(&pt);
			cf.dwStyle = CFS_POINT | CFS_RECT;
			cf.ptCurrentPos.x = pt.x;
			cf.ptCurrentPos.y = pt.y + (bf->spacing / 2);
			get_edit_rect(hWnd, bf, &cf.rcArea);
			ImmSetCompositionWindow(hIMC, &cf);
			ImmReleaseContext(hWnd, hIMC);

			HideCaret(hWnd);
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);

	case WM_IME_COMPOSITION:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) != NULL &&
			bf->lock == FALSE && (lParam & GCS_RESULTSTR)) {
			TCHAR *buf;

			// 確定文字列をバッファに追加
			hIMC = ImmGetContext(hWnd);
			len = ImmGetCompositionString(hIMC, GCS_RESULTSTR, NULL, 0);
			buf = mem_calloc(len + sizeof(TCHAR));
			if (buf != NULL) {
				ImmGetCompositionString(hIMC, GCS_RESULTSTR, buf, len);
				string_insert(hWnd, bf, buf, len / sizeof(TCHAR), bf->insert_mode);
				UpdateWindow(hWnd);
				mem_free(&buf);
			}
			// 位置の設定
			GetCaretPos(&pt);
			cf.dwStyle = CFS_POINT | CFS_RECT;
			cf.ptCurrentPos.x = pt.x;
			cf.ptCurrentPos.y = pt.y + (bf->spacing / 2);
			get_edit_rect(hWnd, bf, &cf.rcArea);
			ImmSetCompositionWindow(hIMC, &cf);
			ImmReleaseContext(hWnd, hIMC);
			break;
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);

	case WM_IME_ENDCOMPOSITION:
		ShowCaret(hWnd);
		return DefWindowProc(hWnd, msg, wParam, lParam);

	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		if (bf->show_line_no == TRUE && LOWORD(lParam) <= bf->line_no_width) {
			SetCursor(LoadCursor(0, IDC_ARROW));
		}
		else {
			SetCursor(LoadCursor(0, IDC_IBEAM));
		}
		GetCursorPos(&pt);
		if (msg == WM_LBUTTONDOWN && (GetTickCount() - DoubleClkTime) <= GetDoubleClickTime() &&
			abs(ClickPt.x - pt.x) < GetSystemMetrics(SM_CXDOUBLECLK) && abs(ClickPt.y - pt.y) < GetSystemMetrics(SM_CYDOUBLECLK)) {
			// ダブルクリックのため単語選択
			string_flush(bf, TRUE);
			SetFocus(hWnd);
			bf->cpx = 0;
			cp = bf->cp;
			bf->sp = bf->cp;
			sp = caret_point_to_caret(bf, (short)LOWORD(lParam), (short)HIWORD(lParam));
			// 単語選択
			caret_get_token(bf);
			ensure_visible(hWnd, bf);
			line_refresh(hWnd, bf, cp, sp);
			line_refresh(hWnd, bf, bf->cp, bf->sp);

			DoubleClkTime = 0;
			TripleClkTime = GetTickCount();
			break;
		}
		if (msg == WM_LBUTTONDOWN && (GetTickCount() - TripleClkTime) <= GetDoubleClickTime() &&
			abs(ClickPt.x - pt.x) < GetSystemMetrics(SM_CXDOUBLECLK) && abs(ClickPt.y - pt.y) < GetSystemMetrics(SM_CYDOUBLECLK)) {
			// トリプルクリックのため行選択
			bf->sp = line_get(bf, index_to_line(bf, bf->cp));
			for (; *index_to_char(bf, bf->cp) != TEXT('\r') && *index_to_char(bf, bf->cp) != TEXT('\n') && bf->cp < BUF_LEN; bf->cp++);
			if (*index_to_char(bf, bf->cp) == TEXT('\r')) {
				bf->cp++;
			}
			if (*index_to_char(bf, bf->cp) == TEXT('\n')) {
				bf->cp++;
			}
			line_refresh(hWnd, bf, bf->cp, bf->sp);
			DoubleClkTime = 0;
			TripleClkTime = 0;
			break;
		}
		if (bf->show_line_no == TRUE && msg == WM_LBUTTONDOWN && LOWORD(lParam) <= bf->line_no_width) {
			// 行番号クリックのため行選択
			string_flush(bf, TRUE);
			cp = bf->cp;
			sp = bf->sp;
			bf->cp = caret_point_to_caret(bf, (short)LOWORD(lParam), (short)HIWORD(lParam));
			ensure_visible(hWnd, bf);

			if (GetKeyState(VK_SHIFT) >= 0) {
				bf->sp = line_get(bf, index_to_line(bf, bf->cp));
			}
			if (bf->sp <= bf->cp) {
				for (; *index_to_char(bf, bf->cp) != TEXT('\r') && *index_to_char(bf, bf->cp) != TEXT('\n') && bf->cp < BUF_LEN; bf->cp++);
				if (*index_to_char(bf, bf->cp) == TEXT('\r')) {
					bf->cp++;
				}
				if (*index_to_char(bf, bf->cp) == TEXT('\n')) {
					bf->cp++;
				}
			}
			line_refresh(hWnd, bf, cp, sp);
			line_refresh(hWnd, bf, bf->cp, bf->sp);
			DoubleClkTime = 0;
			TripleClkTime = 0;

			bf->mousedown = TRUE;
			bf->cpx = 0;
			SetCapture(hWnd);
			SetFocus(hWnd);
			break;
		}
		if (bf->show_line_no == TRUE && msg == WM_LBUTTONUP && LOWORD(lParam) <= bf->line_no_width) {
			ReleaseCapture();
			bf->mousedown = FALSE;
			break;
		}
		if (msg == WM_MOUSEMOVE) {
			if (!(wParam & MK_LBUTTON) || bf->mousedown == FALSE) {
				break;
			}
		} else if (msg == WM_LBUTTONDOWN) {
			string_flush(bf, TRUE);
			SetCapture(hWnd);
			bf->mousedown = TRUE;
			bf->cpx = 0;

			DoubleClkTime = GetTickCount();
			TripleClkTime = 0;
			GetCursorPos(&ClickPt);
		} else if (msg == WM_LBUTTONUP) {
			if (bf->mousedown == FALSE) {
				break;
			}
			ReleaseCapture();
			bf->mousedown = FALSE;
			bf->cpx = 0;
		}
		SetFocus(hWnd);

		cp = bf->cp;
		sp = bf->sp;
		bf->cp = caret_point_to_caret(bf, (short)LOWORD(lParam), (short)HIWORD(lParam));
		ensure_visible(hWnd, bf);
		if (msg == WM_LBUTTONDOWN && GetKeyState(VK_SHIFT) >= 0) {
			bf->sp = bf->cp;
		}
		if (sp != bf->sp) {
			line_refresh(hWnd, bf, cp, sp);
		}
		line_refresh(hWnd, bf, cp, bf->cp);
		break;

	case WM_RBUTTONDOWN:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		string_flush(bf, TRUE);
		SetFocus(hWnd);
		bf->cpx = 0;
		cp = caret_point_to_caret(bf, (short)LOWORD(lParam), (short)HIWORD(lParam));
		if (!(bf->cp >= cp && bf->sp <= cp || bf->sp >= cp && bf->cp <= cp)) {
			bf->cp = bf->sp = cp;
			ensure_visible(hWnd, bf);
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;

	case WM_RBUTTONUP:
		SendMessage(hWnd, WM_CONTEXTMENU, (WPARAM)hWnd, lParam);
		break;

	case WM_MOUSEWHEEL:
		for (i = 0; i < 3; i++) {
			SendMessage(hWnd, WM_VSCROLL, ((short)HIWORD(wParam) > 0) ? SB_LINEUP : SB_LINEDOWN, 0);
		}
		break;

	case WM_PAINT:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) != NULL) {
			PAINTSTRUCT ps;
			HBRUSH hBrush;

			hdc = BeginPaint(hWnd, &ps);
			GetClientRect(hWnd, &rect);
			SetRect(&rect, ps.rcPaint.left, 0, ps.rcPaint.right, bf->font_height);

			bf->no_hide_sel = (GetWindowLongPtr(hWnd, GWL_STYLE) & ES_NOHIDESEL) ? TRUE : FALSE;

			// caret pos
			j = -1;
			if (GetFocus() == hWnd) {
				j = index_to_line(bf, bf->cp);
			}

			bf->sel = FALSE;
			SetTextColor(bf->mdc, GetSysColor(COLOR_WINDOWTEXT));
			SetBkColor(bf->mdc, GetSysColor(COLOR_WINDOW));

			hBrush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
			i = bf->pos_y + (ps.rcPaint.top / bf->font_height) - 1;

			int line_bottom = bf->pos_y + (ps.rcPaint.bottom / bf->font_height) + 1;
			for (; i < bf->line_len && i < line_bottom; i++) {
				if (i == j) {
					// set caret
					if (bf->insert_mode == FALSE) {
						caret_set_size(hWnd, bf);
					}
					len = caret_char_to_caret(bf->mdc, bf, j, bf->cp);
					if (len < bf->line_no_width) {
						len = -1;
					}
					SetCaretPos(len, (j - bf->pos_y)* bf->font_height + bf->top_margin);
					j = -1;
				}
				// 1行描画
				FillRect(bf->mdc, &rect, hBrush);
				draw_line(hWnd, bf->mdc, bf, i, ps.rcPaint.left, ps.rcPaint.right);
				BitBlt(hdc,
					ps.rcPaint.left, (i - bf->pos_y) * bf->font_height + bf->top_margin,
					ps.rcPaint.right, bf->font_height,
					bf->mdc, ps.rcPaint.left, 0, SRCCOPY);
			}
			if ((i - bf->pos_y) * bf->font_height + bf->top_margin < ps.rcPaint.bottom) {
				// 文末の余白描画
				SetRect(&rect,
					ps.rcPaint.left, (i - bf->pos_y) * bf->font_height + bf->top_margin,
					ps.rcPaint.right, ps.rcPaint.bottom);
				FillRect(hdc, &rect, hBrush);
			}
			// 矩形描画
			FillRgn(hdc, bf->hrgn, hBrush);
			DeleteObject(hBrush);

			if (j != -1 && GetFocus() == hWnd) {
				len = caret_char_to_caret(bf->mdc, bf, j, bf->cp);
				if (len < bf->line_no_width) {
					len = -1;
				}
				SetCaretPos(len, (j - bf->pos_y)* bf->font_height + bf->top_margin);
			}
			EndPaint(hWnd, &ps);
		}
		break;

#ifdef OP_XP_STYLE
	case WM_NCPAINT:
		{
			HRGN hrgn;
			DWORD stats;
			RECT clip_rect;

			if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->hTheme == NULL) {
				return DefWindowProc(hWnd, msg, wParam, lParam);
			}
			// XP用の背景描画
			if (_DrawThemeBackground == NULL) {
				_DrawThemeBackground = GetProcAddress(bf->hModThemes, "DrawThemeBackground");
			}
			if (_DrawThemeBackground == NULL) {
				return DefWindowProc(hWnd, msg, wParam, lParam);
			}
			// 状態の設定
			if (IsWindowEnabled(hWnd) == 0) {
				stats = ETS_DISABLED;
			} else if (GetFocus() == hWnd) {
				stats = ETS_FOCUSED;
			} else {
				stats = ETS_NORMAL;
			}
			// ウィンドウ枠の描画
			hdc = GetDCEx(hWnd, (HRGN)wParam, DCX_WINDOW | DCX_INTERSECTRGN);
			if (hdc == NULL) {
				hdc = GetWindowDC(hWnd);
			}
			GetWindowRect(hWnd, &rect);
			OffsetRect(&rect, -rect.left, -rect.top);
			ExcludeClipRect(hdc, rect.left + GetSystemMetrics(SM_CXEDGE), rect.top + GetSystemMetrics(SM_CYEDGE),
				rect.right - GetSystemMetrics(SM_CXEDGE), rect.bottom - GetSystemMetrics(SM_CYEDGE));
			clip_rect = rect;
			_DrawThemeBackground(bf->hTheme, hdc, EP_EDITTEXT, stats, &rect, &clip_rect);
			ReleaseDC(hWnd, hdc);

			// スクロールバーの描画
			GetWindowRect(hWnd, (LPRECT)&rect);
			hrgn = CreateRectRgn(rect.left + GetSystemMetrics(SM_CXEDGE), rect.top + GetSystemMetrics(SM_CYEDGE),
				rect.right - GetSystemMetrics(SM_CXEDGE), rect.bottom - GetSystemMetrics(SM_CYEDGE));
			CombineRgn(hrgn, hrgn, (HRGN)wParam, RGN_AND);
			DefWindowProc(hWnd, WM_NCPAINT, (WPARAM)hrgn, 0);
			DeleteObject(hrgn);
		}
		break;

	case WM_THEMECHANGED:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->hModThemes == NULL) {
			break;
		}
		// XPテーマの変更
		if (bf->hTheme != NULL) {
			if (_CloseThemeData == NULL) {
				_CloseThemeData = GetProcAddress(bf->hModThemes, "CloseThemeData");
			}
			if (_CloseThemeData != NULL) {
				_CloseThemeData(bf->hTheme);
			}
			bf->hTheme = NULL;
		}
		if (_OpenThemeData == NULL) {
			_OpenThemeData = GetProcAddress(bf->hModThemes, "OpenThemeData");
		}
		if (_OpenThemeData != NULL) {
			bf->hTheme = (HTHEME)_OpenThemeData(hWnd, L"Edit");
		}
		break;
#endif	// OP_XP_STYLE

	case WM_SETTEXT:
		// テキストを設定
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || lParam == 0) {
			break;
		}
		return string_set(hWnd, bf, (TCHAR *)lParam, lstrlen((TCHAR *)lParam));

	case WM_GETTEXT:
		// テキストを取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || lParam == 0) {
			break;
		}
		string_flush(bf, TRUE);
		lstrcpyn((TCHAR *)lParam, bf->buf, (int)wParam);
		return lstrlen((TCHAR *)lParam);

	case WM_GETTEXTLENGTH:
		// テキストの長さを取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		return BUF_LEN;

	case WM_SETFONT:
		// フォントを設定
		{
			LOGFONT lf;

			if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
				break;
			}
			if (bf->hfont != NULL) {
				SelectObject(bf->mdc, bf->ret_font);
				DeleteObject(bf->hfont);
			}
			ZeroMemory(bf->cwidth, sizeof(BYTE) * 256);

			if (GetObject((HGDIOBJ)wParam, sizeof(LOGFONT), &lf) == 0) {
				break;
			}
			bf->hfont = CreateFontIndirect((CONST LOGFONT *)&lf);
			bf->ret_font = SelectObject(bf->mdc, bf->hfont);

			SendMessage(hWnd, WM_REFLECT, 0, 0);
			draw_free(bf);
			draw_init(hWnd, bf);
		}
		break;

	case WM_GETFONT:
		// フォントを取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		return (LRESULT)bf->hfont;

	case WM_CLEAR:
		// 削除
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->lock == TRUE) {
			break;
		}
		if (bf->cp != bf->sp) {
			string_delete_char(hWnd, bf, bf->cp);
		}
		break;

	case WM_COPY:
		// コピー
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		string_flush(bf, TRUE);
		if (bf->cp != bf->sp) {
			if (bf->sp > bf->cp) {
				string_to_clipboard(hWnd, index_to_char(bf, bf->cp), index_to_char(bf, bf->sp));
			} else {
				string_to_clipboard(hWnd, index_to_char(bf, bf->sp), index_to_char(bf, bf->cp));
			}
		}
		break;

	case WM_CUT:
		// 切り取り
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->lock == TRUE) {
			break;
		}
		SendMessage(hWnd, WM_COPY, 0, 0);
		SendMessage(hWnd, WM_CLEAR, 0, 0);
		break;

	case WM_PASTE:
		// 貼り付け
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->lock == TRUE) {
			break;
		}
#ifdef UNICODE
		if (IsClipboardFormatAvailable(CF_UNICODETEXT) == 0) {
#else
		if (IsClipboardFormatAvailable(CF_TEXT) == 0) {
#endif
			break;
		}
		if (OpenClipboard(hWnd) != 0) {
			HANDLE hclip;
			TCHAR *p;
			HCURSOR old_cursor;

			old_cursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
#ifdef UNICODE
			hclip = GetClipboardData(CF_UNICODETEXT);
#else
			hclip = GetClipboardData(CF_TEXT);
#endif
			if ((p = GlobalLock(hclip)) != NULL) {
				string_insert(hWnd, bf, p, lstrlen(p), TRUE);
				GlobalUnlock(hclip);
			}
			CloseClipboard();
			SetCursor(old_cursor);
		}
		break;

	case EM_CANUNDO:
		// UNDO可能か取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		return ((bf->undo_len > 0 || bf->ip != NULL || bf->dp != NULL) ? TRUE : FALSE);

	case EM_CANREDO:
		// REDO可能か取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		return (((bf->undo + bf->undo_len)->type != 0) ? TRUE : FALSE);

	case EM_EMPTYUNDOBUFFER:
		// UNDOバッファをクリア
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		undo_free(bf, 0);
		mem_free(&bf->undo);

		bf->undo_size = RESERVE_UNDO;
		if ((bf->undo = mem_calloc(sizeof(UNDO) * bf->undo_size)) == NULL) {
			return FALSE;
		}
		bf->undo_len = 0;
		break;

	case EM_GETFIRSTVISIBLELINE:
		// 一番上に表示されている行番号を取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		return bf->pos_y;

	case EM_GETLINE:
		// 1行の文字列を取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || lParam == 0) {
			break;
		}
		if (wParam < 0 || wParam >= (WPARAM)bf->line_len) {
			return 0;
		}
		CopyMemory((TCHAR *)lParam,
			index_to_char(bf, line_get(bf, (int)wParam)),
			sizeof(TCHAR) * line_get_length(bf, line_get(bf, (int)wParam)));
		return line_get_length(bf, line_get(bf, (int)wParam));

	case EM_GETLINECOUNT:
		// 行数取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		return bf->line_len;

	case EM_GETMODIFY:
		// 変更フラグを取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		return bf->modified;

	case EM_GETRECT:
		// 描画領域の取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || lParam == 0) {
			break;
		}
		get_edit_rect(hWnd, bf, (RECT *)lParam);
		break;

	case EM_GETSEL:
		// 選択されているインデックスを取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		i = ((bf->sp < bf->cp) ? bf->sp : bf->cp);
		j = ((bf->sp < bf->cp) ? bf->cp : bf->sp);
		if (wParam != 0) {
			*((LPDWORD)wParam) = i;
		}
		if (lParam != 0) {
			*((LPDWORD)lParam) = j;
		}
		return MAKELPARAM(i, j);

	case EM_LIMITTEXT:
		// 入力文字数制限
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		bf->limit_len = (DWORD)wParam;
		if (bf->limit_len < 0) {
			bf->limit_len = 0;
		}
		break;

	case EM_LINEFROMCHAR:
		// 特定文字インデックスが含まれる行番号を取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		if (wParam == -1) {
			if (bf->sp < bf->cp) {
				return index_to_line(bf, bf->sp);
			} else {
				return index_to_line(bf, bf->cp);
			}
		}
		return index_to_line(bf, (DWORD)wParam);

	case EM_LINEINDEX:
		// 行番号の文字インデックスを取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		if (wParam == -1) {
			return line_get(bf, index_to_line(bf, bf->cp));
		}
		if (wParam < 0 || wParam >= (WPARAM)bf->line_len) {
			return -1;
		}
		return line_get(bf, (int)wParam);

	case EM_LINELENGTH:
		// 行の長さを取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		if (wParam == -1) {
			if (bf->sp == bf->cp) {
				return line_get_length(bf, bf->cp);
			}
			i = ((bf->sp < bf->cp) ? bf->sp : bf->cp);
			i = i - line_get(bf, index_to_line(bf, i));
			j = ((bf->sp < bf->cp) ? bf->cp : bf->sp);
			j = line_get_length(bf, j) - (j - line_get(bf, index_to_line(bf, j)));
			return (i + j);
		}
		return line_get_length(bf, (DWORD)wParam);

	case EM_LINESCROLL:
		// 水平、垂直にスクロール
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		get_edit_rect(hWnd, bf, &rect);
		if (wParam != 0) {
			i = (int)wParam;
			if (bf->pos_x + i < 0) {
				i = bf->pos_x * -1;
			}
			if (bf->pos_x + i > bf->max_x) {
				i = bf->max_x - bf->pos_x;
			}
			SetScrollPos(hWnd, SB_HORZ, bf->pos_x + i, TRUE);
			ScrollWindowEx(hWnd, -((int)i * bf->char_width), 0, NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
			bf->pos_x += i;
		}
		if (lParam != 0) {
			i = (int)lParam;
			if (bf->pos_y + i < 0) {
				i = bf->pos_y * -1;
			}
			if (bf->pos_y + i > bf->max_y) {
				i = bf->max_y - bf->pos_y;
			}
			SetScrollPos(hWnd, SB_VERT, bf->pos_y + i, TRUE);
			rect.left -= bf->line_no_width;
			ScrollWindowEx(hWnd, 0, -((int)i * bf->font_height), NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
			bf->pos_y += i;
		}
		break;

	case EM_REPLACESEL:
		// 選択範囲のテキストを置き換える
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || lParam == 0) {
			break;
		}
		string_insert(hWnd, bf, (TCHAR *)lParam, lstrlen((TCHAR *)lParam), TRUE);
		break;

	case EM_SCROLL:
		// テキストを垂直にスクロール
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		get_edit_rect(hWnd, bf, &rect);
		switch (wParam) {
		case SB_LINEUP:
			i = (bf->pos_y > 0) ? -1 : 0;
			break;

		case SB_LINEDOWN:
			i = (bf->pos_y < bf->max_y) ? 1 : 0;
			break;

		case SB_PAGEUP:
			i = (bf->pos_y - (((rect.bottom - rect.top) / bf->font_height) - 1) > 0) ?
				-(((rect.bottom - rect.top) / bf->font_height) - 1) : bf->pos_y;
			break;

		case SB_PAGEDOWN:
			i = (bf->pos_y + (((rect.bottom - rect.top) / bf->font_height) - 1) < bf->max_y) ?
				(((rect.bottom - rect.top) / bf->font_height) - 1) : bf->max_y - bf->pos_y;
			break;

		default:
			return FALSE;
		}
		if (i == 0) {
			return 0;
		}
		SendMessage(hWnd, WM_VSCROLL, wParam, 0);
		return MAKELRESULT(i, TRUE);

	case EM_SCROLLCARET:
		// キャレット位置にスクロール
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		ensure_visible(hWnd, bf);
		InvalidateRect(hWnd, NULL, FALSE);
		return !0;

	case EM_SETMODIFY:
		// 変更フラグをセット
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		bf->modified = (BOOL)wParam;
		break;

	case EM_GETREADONLY:
		// 読み取り専用フラグの取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		return bf->lock;

	case EM_SETREADONLY:
		// 読み取り専用の設定
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		bf->lock = (BOOL)wParam;
		if (bf->lock == TRUE) {
			SetWindowLongPtr(hWnd, GWL_STYLE, GetWindowLongPtr(hWnd, GWL_STYLE) | ES_READONLY);
			if (bf->himc == (HIMC)NULL) {
				bf->himc = ImmAssociateContext(hWnd, (HIMC)NULL);
			}
		} else if (bf->lock == FALSE) {
			SetWindowLongPtr(hWnd, GWL_STYLE, GetWindowLongPtr(hWnd, GWL_STYLE) & ~ES_READONLY);
			if (bf->himc != (HIMC)NULL) {
				ImmAssociateContext(hWnd, bf->himc);
				bf->himc = (HIMC)NULL;
			}
		}
		return !0;

	case EM_SETRECT:
		// 描画領域の設定 (再描画あり)
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || lParam == 0) {
			break;
		}
		SendMessage(hWnd, EM_SETRECTNP, wParam, lParam);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case EM_SETRECTNP:
		// 描画領域の設定 (再描画なし)
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || lParam == 0) {
			break;
		}
		GetClientRect(hWnd, &rect);
		bf->left_margin = ((RECT *)lParam)->left + 1;
		bf->top_margin = ((RECT *)lParam)->top + 1;
		bf->right_margin = rect.right - ((RECT *)lParam)->right + 1;
		bf->bottom_margin = rect.bottom - ((RECT *)lParam)->bottom + 1;
		draw_free(bf);
		draw_init(hWnd, bf);
		SendMessage(hWnd, WM_REFLECT, 1, 0);
		break;

	case EM_SETSEL:
		// 選択インデックスの設定
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		string_flush(bf, TRUE);

		i = bf->sp;
		j = bf->cp;

		sp = ((DWORD)wParam < (DWORD)lParam) ? (DWORD)wParam : (DWORD)lParam;
		cp = ((DWORD)wParam < (DWORD)lParam) ? (DWORD)lParam : (DWORD)wParam;
		if (sp < 0) {
			sp = 0;
		}
		if (sp > bf->buf_len) {
			sp = bf->buf_len;
		}
		if (cp < 0 || cp > bf->buf_len) {
			cp = bf->buf_len;
		}
		bf->sp = sp; bf->cp = cp;
		bf->cpx = 0;
		line_refresh(hWnd, bf, i, j);
		line_refresh(hWnd, bf, bf->cp, bf->sp);
		break;

	case EM_SETTABSTOPS:
		// タブストップの設定
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		if (wParam < 0 || lParam == 0) {
			return FALSE;
		} else if (wParam == 0) {
			bf->tab_stop = TAB_STOP;
		} else {
			bf->tab_stop = *((LPDWORD)lParam) / 4;
		}
		SendMessage(hWnd, WM_REFLECT, 0, 0);
		return TRUE;

	case WM_UNDO:
	case EM_UNDO:
		// 元に戻す
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->lock == TRUE) {
			break;
		}
		undo_exec(hWnd, bf);
		break;

	case EM_REDO:
		// やり直し
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->lock == TRUE) {
			break;
		}
		redo_exec(hWnd, bf);
		break;

	case WM_GETBUFFERINFO:
		// 内部バッファの取得
		if (lParam == 0) {
			break;
		}
		*((BUFFER **)lParam) = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		break;

	case WM_REFLECT:
		// 描画情報の更新
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) != NULL) {
			TEXTMETRIC tm;
			HCURSOR old_cursor;

			old_cursor = SetCursor(LoadCursor(NULL, IDC_WAIT));

			// フォント情報の取得
			GetTextMetrics(bf->mdc, &tm);
			bf->font_height = tm.tmHeight + bf->spacing;
			bf->char_width = tm.tmAveCharWidth;
			if (GetFocus() == hWnd) {
				DestroyCaret();
				CreateCaret(hWnd, NULL, 0, bf->font_height);
				ShowCaret(hWnd);
			}
			get_edit_rect(hWnd, bf, &rect);
			bf->width = (rect.right - rect.left);
			bf->line_width = 0;
			bf->line_len = 0;
			line_set_info(hWnd, bf);
			set_scrollbar(hWnd, bf);
			if (wParam == 0) {
				InvalidateRect(hWnd, NULL, FALSE);
			}
			SetCursor(old_cursor);
		}
		break;

	case WM_GETWORDWRAP:
		// 折り返しフラグの取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		return bf->wordwrap;

	case WM_SETWORDWRAP:
		// 折り返し設定
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		bf->wordwrap = (BOOL)wParam;
		i = bf->pos_y;
		set_scrollbar(hWnd, bf);
		bf->pos_y = i;
		SendMessage(hWnd, WM_REFLECT, 0, 0);
		break;

	case WM_GETMEMSIZE:
		// 内部メモリのサイズ取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		return (BUF_LEN * sizeof(TCHAR));

	case WM_GETMEM:
		// 内部メモリの取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || lParam == 0) {
			break;
		}
		string_flush(bf, TRUE);
		CopyMemory((TCHAR *)lParam, bf->buf, (BUF_LEN * sizeof(TCHAR)));
		return (BUF_LEN * sizeof(TCHAR));

	case WM_SETMEM:
		// メモリを内部メモリに設定
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || lParam == 0) {
			break;
		}
		return string_set(hWnd, bf, (TCHAR *)lParam, (DWORD)(wParam / sizeof(TCHAR)));

	case WM_SET_HIGHLIGHT_LINE:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		{
			int old_line = bf->hightlight_line;
			bf->hightlight_line = (int)lParam;
			if ((int)lParam >= 0) {
				show_line(hWnd, bf, (int)lParam, TRUE);
			}
			line_refresh(hWnd, bf, line_get(bf, old_line), line_get(bf, old_line));
			line_refresh(hWnd, bf, line_get(bf, bf->hightlight_line), line_get(bf, bf->hightlight_line));
		}
		break;

	case WM_SET_ERROR_LINE:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		{
			int old_line = bf->error_line;
			bf->error_line = (int)lParam;
			if ((int)lParam >= 0) {
				show_line(hWnd, bf, (int)lParam, TRUE);
			}
			line_refresh(hWnd, bf, line_get(bf, old_line), line_get(bf, old_line));
			line_refresh(hWnd, bf, line_get(bf, bf->error_line), line_get(bf, bf->error_line));
		}
		break;

	case WM_SET_EXTENSION:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		bf->extension_mode = (BOOL)wParam;
		SendMessage(hWnd, WM_REFLECT, 0, 0);
		break;

	case WM_SHOW_LINE_NO:
		if ((bf = (BUFFER*)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		bf->show_line_no = (BOOL)wParam;
		SendMessage(hWnd, WM_REFLECT, 0, 0);
		SendMessage(hWnd, WM_SIZE, 0, 0);
		break;

	case WM_FINDTEXT:
		// 内部メモリの取得
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || lParam == 0) {
			break;
		}
		string_flush(bf, TRUE);
		{
			TCHAR *p = index_to_char(bf, bf->cp);
			TCHAR *r;
			int len = lstrlen((TCHAR *)lParam);
			BOOL match = FALSE;
			for (r = p; *r != TEXT('\0'); r++) {
				if (wParam == 0 && str_cmp_ni(r, (TCHAR *)lParam, len) == 0) {
					match = TRUE;
					break;
				}
				if (wParam == 1 && str_cmp_n(r, (TCHAR *)lParam, len) == 0) {
					match = TRUE;
					break;
				}
			}
			if (match == FALSE) {
				for (r = bf->buf; r < p; r++) {
					if (wParam == 0 && str_cmp_ni(r, (TCHAR *)lParam, len) == 0) {
						match = TRUE;
						break;
					}
					if (wParam == 1 && str_cmp_n(r, (TCHAR *)lParam, len) == 0) {
						match = TRUE;
						break;
					}
				}
			}
			if (match == TRUE) {
				SendMessage(hWnd, EM_SETSEL, r - bf->buf, r - bf->buf + len);
				show_line(hWnd, bf, index_to_line(bf, bf->cp), FALSE);
			}
		}
		break;

	case WM_CONTEXTMENU:
		if ((bf = (BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) != NULL) {
			HMENU hMenu;
			POINT apos;
			DWORD st, en;
			WORD lang;

			SendMessage(hWnd, EM_GETSEL, (WPARAM)&st, (LPARAM)&en);
			lang = PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale()));

			// メニューの作成
			hMenu = CreatePopupMenu();
			AppendMenu(hMenu, MF_STRING | ((SendMessage(hWnd, EM_CANUNDO, 0, 0) == TRUE) ? 0 : MF_GRAYED), EM_UNDO,
				(lang != LANG_JAPANESE) ? TEXT("&Undo") : TEXT("元に戻す(&U)"));
			AppendMenu(hMenu, MF_STRING | ((SendMessage(hWnd, EM_CANREDO, 0, 0) == TRUE) ? 0 : MF_GRAYED), EM_REDO,
				(lang != LANG_JAPANESE) ? TEXT("&Redo") : TEXT("やり直し(&R)"));
			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenu, MF_STRING | ((st != en) ? 0 : MF_GRAYED), WM_CUT,
				(lang != LANG_JAPANESE) ? TEXT("Cu&t") : TEXT("切り取り(&T)"));
			AppendMenu(hMenu, MF_STRING | ((st != en) ? 0 : MF_GRAYED), WM_COPY,
				(lang != LANG_JAPANESE) ? TEXT("&Copy") : TEXT("コピー(&C)"));
			AppendMenu(hMenu, MF_STRING, WM_PASTE,
				(lang != LANG_JAPANESE) ? TEXT("&Paste") : TEXT("貼り付け(&P)"));
			AppendMenu(hMenu, MF_STRING | ((st != en) ? 0 : MF_GRAYED), WM_CLEAR,
				(lang != LANG_JAPANESE) ? TEXT("&Delete") : TEXT("削除(&D)"));
			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenu, MF_STRING, 1,
				(lang != LANG_JAPANESE) ? TEXT("Select &All") : TEXT("すべて選択(&A)"));

			// メニューの表示
			GetCursorPos((LPPOINT)&apos);
			i = TrackPopupMenu(hMenu, TPM_TOPALIGN | TPM_RETURNCMD, apos.x, apos.y, 0, hWnd, NULL);
			DestroyMenu(hMenu);
			switch (i) {
			case 0:
				break;
			case 1:
				SendMessage(hWnd, EM_SETSEL, 0, -1);
				break;
			default:
				SendMessage(hWnd, i, 0, 0);
				break;
			}
		}
		break;

	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

/*
 * RegisterNedit - クラスの登録
 */
BOOL RegisterNedit(const HINSTANCE hInstance)
{
	WNDCLASS wc;

	wc.style = 0;
	wc.lpfnWndProc = (WNDPROC)nedit_proc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = NULL;
	wc.hCursor = NULL;
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName = 0;
	wc.lpszClassName = NEDIT_WND_CLASS;
	return RegisterClass(&wc);
}
/* End of source */
