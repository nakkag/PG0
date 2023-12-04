/*
 * PG0
 *
 * variable_view.c
 *
 * Copyright (C) 1996-2021 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#include <windows.h>
#ifdef OP_XP_STYLE
#include <uxtheme.h>
#include <vssym32.h>
#endif	// OP_XP_STYLE

#include "variable_view.h"
#include "script.h"
#include "script_memory.h"
#include "script_string.h"
#include "script_utility.h"
#include "frame.h"

/* Define */
/* ホイールメッセージ */
#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL			0x020A
#endif
// XPテーマ変更通知
#ifndef WM_THEMECHANGED
#define WM_THEMECHANGED			0x031A
#endif

// タイマーID
#define TIMER_SEP				1

/* Global Variables */
VALUE_VIEW_INFO *vi_list;
int vi_list_count;
int alloc_count;

BOOL hex_mode;

/* Local Function Prototypes */
static HFONT CreateViewFont(const HDC hdc, const VARIABLE_BUFFER *bf);
static void SetScrollBar(HWND hWnd, VARIABLE_BUFFER *bf);
static BOOL Variable2Clipboard(const HWND hWnd, const VALUE_VIEW_INFO *vvi);
static int getNameMaxWidth(HDC mdc, VARIABLE_BUFFER *bf, VALUE_VIEW_INFO *vvi, int count);
static int getDrawMaxWidth(HDC mdc, VARIABLE_BUFFER *bf);
static RECT getTreeIconRect(HDC mdc, VARIABLE_BUFFER *bf, VALUE_VIEW_INFO *vvi);
static void DrawHeader(HDC mdc, VARIABLE_BUFFER *bf);
static void DrawSeparator(HDC mdc, VARIABLE_BUFFER *bf, int height);
static void DrawTreeIcon(HWND hWnd, HDC mdc, VARIABLE_BUFFER *bf, VALUE_VIEW_INFO *vvi, int top);
static BOOL DrawLine(HWND hWnd, HDC mdc, VARIABLE_BUFFER *bf, VALUE_VIEW_INFO *vvi, int count, int index);
static void initValueList(VALUE_VIEW_INFO *vvi, int vvi_count);
static int findValueList(VALUEINFO *vi, VALUE_VIEW_INFO *vvi, int vvi_count);
static void deselectValueList(VALUE_VIEW_INFO *vvi, int vvi_count);
static void trimValueList(VALUE_VIEW_INFO *vvi, int *count);
static int getValueCount(VALUEINFO *vi);
static void setValueList(VALUEINFO *vi, VALUE_VIEW_INFO *vvi, int *vvi_count);
static void listValueinfo(EXECINFO *ei);
static void freeValueinfo(VALUE_VIEW_INFO *vvi, int count);
static void setValueIndex(VALUE_VIEW_INFO *vvi, int count, int *index, int indent);
static int getValueDrawCount(VALUE_VIEW_INFO *vvi, int count);
static VALUE_VIEW_INFO *indexToValueViewInfo(VALUE_VIEW_INFO *vvi, int count, int index);
static VALUE_VIEW_INFO *getSelectValueViewInfo(VALUE_VIEW_INFO *vvi, int count);
static void ShowSelectValueViewInfo(HWND hWnd, VARIABLE_BUFFER *bf);
static LRESULT CALLBACK VariableProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

/*
 * CreateViewFont - フォントを作成する
 */
static HFONT CreateViewFont(const HDC hdc, const VARIABLE_BUFFER *bf)
{
	return CreateFontIndirect((CONST LOGFONT *)&(bf->lf));
}

/*
 * SetScrollBar - スクロールバーの設定
 */
static void SetScrollBar(HWND hWnd, VARIABLE_BUFFER *bf)
{
	SCROLLINFO si;
	RECT rect;

	GetClientRect(hWnd, &rect);

	// 横スクロールバー
	if (rect.right < bf->draw_width) {
		EnableScrollBar(hWnd, SB_HORZ, ESB_ENABLE_BOTH);

		bf->max_x = bf->draw_width / bf->CharWidth - rect.right / bf->CharWidth;
		bf->pos_x = (bf->pos_x < bf->max_x) ? bf->pos_x : bf->max_x;

		ZeroMemory(&si, sizeof(SCROLLINFO));
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_POS | SIF_RANGE | SIF_PAGE;
		si.nPage = rect.right / bf->CharWidth;
		si.nMax = (bf->draw_width / bf->CharWidth) - 1;
		si.nPos = bf->pos_x;
		SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);
	} else {
		EnableScrollBar(hWnd, SB_HORZ, ESB_DISABLE_BOTH);

		bf->pos_x = bf->max_x = 0;

		ZeroMemory(&si, sizeof(SCROLLINFO));
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_POS | SIF_PAGE | SIF_RANGE;
		si.nMax = 1;
		SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);
	}

	// 縦スクロールバー
	if(((rect.bottom - bf->header_height) / bf->FontHeight) < bf->view_line){
		EnableScrollBar(hWnd, SB_VERT, ESB_ENABLE_BOTH);

		bf->max_y = bf->view_line - ((rect.bottom - bf->header_height) / bf->FontHeight);
		bf->pos_y = (bf->pos_y < bf->max_y) ? bf->pos_y : bf->max_y;

		ZeroMemory(&si, sizeof(SCROLLINFO));
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_POS | SIF_RANGE | SIF_PAGE;
		si.nPage = (rect.bottom - bf->header_height) / bf->FontHeight;
		si.nMax = bf->view_line - 1;
		si.nPos = bf->pos_y;
		SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
	}else{
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
 * Variable2Clipboard - 変数名と値をクリップボードに設定
 */
static BOOL Variable2Clipboard(const HWND hWnd, const VALUE_VIEW_INFO *vvi)
{
	HANDLE hMem;
	TCHAR *buf;

	//クリップボードを開く
	if(OpenClipboard(hWnd) == FALSE){
		return FALSE;
	}
	if(EmptyClipboard() == FALSE){
		CloseClipboard();
		return FALSE;
	}
	if((hMem = GlobalAlloc(GHND, sizeof(TCHAR) * (lstrlen(vvi->name) + 3 + lstrlen(vvi->buf) + 1))) == NULL){
		CloseClipboard();
		return FALSE;
	}
	if((buf = GlobalLock(hMem)) == NULL){
		GlobalFree(hMem);
		CloseClipboard();
		return FALSE;
	}
	wsprintf(buf, TEXT("%s = %s"), vvi->name, vvi->buf);
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
 * createVariableName - インデント付の変数名を作成
 */
static TCHAR *createVariableName(VALUE_VIEW_INFO *vvi)
{
	TCHAR *p = mem_alloc(sizeof(TCHAR) * (lstrlen(vvi->name) + vvi->indent * 2 + 1));
	TCHAR *r;
	for (r = p; r - p < (vvi->indent * 2); r++) {
		*r = TEXT(' ');
	}
	lstrcpy(p + vvi->indent * 2, vvi->name);
	return p;
}

/*
 * getNameMaxWidth - 変数名の描画長さ取得
 */
static int _getNameMaxWidth(HDC mdc, VARIABLE_BUFFER *bf, VALUE_VIEW_INFO *vvi, int count)
{
	TCHAR *p;
	SIZE sz;
	int lineWidth;
	int i;
	int ret = 0;

	for (i = 0; i < count; i++) {
		p = createVariableName(vvi + i);
		GetTextExtentPoint32(mdc, p, lstrlen(p), &sz);
		mem_free(&p);

		lineWidth = bf->LeftMargin + sz.cx;
		if (ret < lineWidth) {
			ret = lineWidth;
		}
		if (vvi[i].child != NULL && vvi[i].open == TRUE) {
			lineWidth = _getNameMaxWidth(mdc, bf, vvi[i].child, vvi[i].child_count);
			if (ret < lineWidth) {
				ret = lineWidth;
			}
		}
	}
	return ret;
}
static int getNameMaxWidth(HDC mdc, VARIABLE_BUFFER *bf, VALUE_VIEW_INFO *vvi, int count)
{
	HFONT hFont;
	HFONT hRetFont;
	int ret;

	hFont = CreateViewFont(mdc, bf);
	hRetFont = SelectObject(mdc, hFont);
	ret = _getNameMaxWidth(mdc, bf, vvi, count);
	hFont = SelectObject(mdc, hRetFont);
	DeleteObject(hFont);
	return ret;
}

/*
 * getDrawMaxWidth - 1行の描画長さ取得
 */
static int _getDrawMaxWidth(HDC mdc, VARIABLE_BUFFER* bf, VALUE_VIEW_INFO* vvi, int count)
{
	SIZE sz;
	int lineWidth;
	int i;
	int ret = 0;

	for (i = 0; i < count; i++) {
		GetTextExtentPoint32(mdc, vvi[i].buf, lstrlen(vvi[i].buf), &sz);
		lineWidth = bf->sep_size + bf->CharWidth + sz.cx + bf->RightMargin;
		if (ret < lineWidth) {
			ret = lineWidth;
		}
		if (vvi[i].child != NULL && vvi[i].open == TRUE) {
			lineWidth = _getDrawMaxWidth(mdc, bf, vvi[i].child, vvi[i].child_count);
			if (ret < lineWidth) {
				ret = lineWidth;
			}
		}
	}
	return ret;
}
static int getDrawMaxWidth(HDC mdc, VARIABLE_BUFFER *bf)
{
	HFONT hFont;
	HFONT hRetFont;
	int ret;

	hFont = CreateViewFont(mdc, bf);
	hRetFont = SelectObject(mdc, hFont);
	ret = _getDrawMaxWidth(mdc, bf, vi_list, vi_list_count);
	hFont = SelectObject(mdc, hRetFont);
	DeleteObject(hFont);
	return ret;
}

/*
 * getTreeIconRect - ツリー開閉アイコンの位置を取得
 */
static RECT getTreeIconRect(HDC mdc, VARIABLE_BUFFER *bf, VALUE_VIEW_INFO *vvi)
{
	HFONT hFont;
	HFONT hRetFont;
	RECT retRect;
	SIZE sz;
	TCHAR *p, *r;

	ZeroMemory(&retRect, sizeof(RECT));
	hFont = CreateViewFont(mdc, bf);
	hRetFont = SelectObject(mdc, hFont);

	if(bf->Fixed == FALSE){
		p = createVariableName(vvi);
		for (r = p; *r == TEXT(' '); r++);
		*r = TEXT('\0');
		GetTextExtentPoint32(mdc, p, lstrlen(p), &sz);
		mem_free(&p);
		retRect.right = bf->LeftMargin + sz.cx;
	}else{
		retRect.right = bf->LeftMargin + (vvi->indent * 2) * bf->CharWidth;
	}
	retRect.left = retRect.right - bf->FontHeight;
	if (retRect.left < 0) {
		retRect.left = 0;
	}
	hFont = SelectObject(mdc, hRetFont);
	DeleteObject(hFont);
	return retRect;
}

/*
 * DrawHeader - ヘッダー描画
 */
static void DrawHeader(HDC mdc, VARIABLE_BUFFER *bf)
{
	RECT trect;
	HBRUSH hBrush;
	int offset;

	SetRect(&trect, 0, 0, bf->width, bf->header_height);
	hBrush = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
	FillRect(mdc, &trect, hBrush);
	DeleteObject(hBrush);

	SetTextColor(mdc, GetSysColor(COLOR_BTNTEXT));
	SetBkColor(mdc, GetSysColor(COLOR_3DFACE));
	offset = bf->LeftMargin - (bf->pos_x * bf->CharWidth);
	trect.left = offset;
	trect.right	= bf->sep_size;
	DrawText(mdc, bf->header_name, lstrlen(bf->header_name),
		&trect, DT_VCENTER | DT_SINGLELINE | DT_NOCLIP | DT_WORD_ELLIPSIS);

	offset = bf->sep_size + bf->CharWidth - (bf->pos_x * bf->CharWidth);
	trect.left = offset;
	trect.right	= bf->width - bf->sep_size;
	DrawText(mdc, bf->header_value, lstrlen(bf->header_value),
		&trect, DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
}

/*
 * DrawSeparator - 境界線の描画
 */
static void DrawSeparator(HDC mdc, VARIABLE_BUFFER *bf, int height)
{
	HPEN hPen, retPen;
	int offset;

	offset = bf->sep_size - (bf->pos_x * bf->CharWidth);

	hPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
	retPen = (HPEN)SelectObject(mdc, hPen);
	MoveToEx(mdc, offset, 0, NULL);
	LineTo(mdc, offset, height);
	SelectObject(mdc, retPen);
	DeleteObject(hPen);
}

/*
 * DrawTreeIcon - ツリー開閉アイコンの描画
 */
static void DrawTreeIcon(HWND hWnd, HDC mdc, VARIABLE_BUFFER *bf, VALUE_VIEW_INFO *vvi, int top)
{
	HBRUSH hBrush;
	HBRUSH hRetBrush;
	HPEN hPen;
	HPEN hRetPen;
	RECT irect;
	RECT iconRect = getTreeIconRect(mdc, bf, vvi);
	int size = (iconRect.right - iconRect.left) / 2;

	irect.left = iconRect.left + size / 2 - (bf->pos_x * bf->CharWidth);
	irect.right = iconRect.right - size / 2 - (bf->pos_x * bf->CharWidth);
	irect.top = top + size / 2;
	irect.bottom = top + bf->FontHeight - size / 2;
	if (irect.right > bf->sep_size - (bf->pos_x * bf->CharWidth)) {
		return;
	}

	SetPolyFillMode(mdc, WINDING);
	if (vvi->select == TRUE && GetFocus() == hWnd) {
		hBrush = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHTTEXT));
		hPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_HIGHLIGHTTEXT));
	} else {
		hBrush = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
		hPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_HIGHLIGHT));
	}
	hRetBrush = SelectObject(mdc , hBrush);
	hRetPen = SelectObject(mdc , hPen);
	if (vvi->open == TRUE) {
		POINT pt[3] = {{irect.left, irect.top + (irect.bottom - irect.top) / 2},
			{irect.right, irect.top + (irect.bottom - irect.top) / 2},
			{irect.left + (irect.right - irect.left) / 2, irect.bottom}};
		Polygon(mdc, pt, 3);
	} else {
		POINT pt[3] = {{irect.left + (irect.right - irect.left) / 2 - (irect.right - irect.left) / 4, irect.top},
			{irect.right - (irect.right - irect.left) / 4, irect.top + (irect.bottom - irect.top) / 2},
			{irect.left + (irect.right - irect.left) / 2 - (irect.right - irect.left) / 4, irect.bottom}};
		Polygon(mdc, pt, 3);
	}
	SelectObject(mdc , hRetBrush);
	DeleteObject(hBrush);
	SelectObject(mdc , hRetPen);
	DeleteObject(hPen);
}

/*
 * _DrawLine - 1行描画
 */
static void _DrawLine(HWND hWnd, HDC mdc, VARIABLE_BUFFER *bf, VALUE_VIEW_INFO *vvi, int i)
{
	RECT drect;
	SIZE sz;
	TCHAR *p;
	int offset;
	int height;
	int csize;

	if (vvi->select == TRUE) {
		RECT trect;
		HBRUSH hBrush;
		int top = bf->header_height + (i - bf->pos_y) * bf->FontHeight;
		SetRect(&trect, 0, top, bf->width, top + bf->FontHeight);

		if (GetFocus() != hWnd) {
			hBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
		} else {
			hBrush = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
		}
		FillRect(mdc, &trect, hBrush);
		DeleteObject(hBrush);
	}
	// 変数名
	p = createVariableName(vvi);

	offset = bf->LeftMargin - (bf->pos_x * bf->CharWidth);
	height = bf->header_height + (i - bf->pos_y) * bf->FontHeight + (bf->Spacing / 2);
	drect.left = offset;
	drect.right = bf->sep_size - (bf->pos_x * bf->CharWidth);
	drect.top = bf->header_height + (i - bf->pos_y) * bf->FontHeight;
	drect.bottom = drect.top + bf->FontHeight;
	if (vvi->select == TRUE) {
		SetTextColor(mdc, GetSysColor((GetFocus() != hWnd) ? COLOR_BTNTEXT : COLOR_HIGHLIGHTTEXT));
		SetBkColor(mdc, GetSysColor((GetFocus() != hWnd) ? COLOR_BTNFACE : COLOR_HIGHLIGHT));
	} else {
		SetTextColor(mdc, bf->TextColor);
		SetBkColor(mdc, bf->BackClolor);
	}
	DrawText(mdc, p, lstrlen(p),
		&drect, DT_VCENTER | DT_SINGLELINE | DT_NOCLIP | DT_WORD_ELLIPSIS);
	mem_free(&p);
	if (vvi->vi->v->type == TYPE_ARRAY) {
		DrawTreeIcon(hWnd, mdc, bf, vvi, drect.top);
	}

	// 値
	offset = bf->sep_size + bf->CharWidth - (bf->pos_x * bf->CharWidth);
	p = vvi->buf;
	drect.left = offset;
	GetTextExtentPoint32(mdc, p, lstrlen(p), &sz);
	csize = sz.cx;
	if (csize > bf->width) {
		csize = bf->width;
	}
	drect.right = offset + csize;
	if (vvi->select == TRUE) {
		SetTextColor(mdc, GetSysColor((GetFocus() != hWnd) ? COLOR_BTNTEXT : COLOR_HIGHLIGHTTEXT));
		SetBkColor(mdc, GetSysColor((GetFocus() != hWnd) ? COLOR_BTNFACE : COLOR_HIGHLIGHT));
	} else {
		if (vvi->change == TRUE) {
			SetTextColor(mdc, RGB(0xFF, 0x00, 0x00));
		} else {
			SetTextColor(mdc, bf->TextColor);
		}
		SetBkColor(mdc, bf->BackClolor);
	}
	DrawText(mdc, p, lstrlen(p), &drect, DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
}

/*
 * DrawLine - 1行描画
 */
static BOOL DrawLine(HWND hWnd, HDC mdc, VARIABLE_BUFFER *bf, VALUE_VIEW_INFO *vvi, int count, int index)
{
	int i;
	for (i = 0; i < count; i++) {
		if (vvi[i].index == index) {
			_DrawLine(hWnd, mdc, bf, vvi + i, index);
			return TRUE;
		}
		if (vvi[i].child != NULL && vvi[i].open == TRUE) {
			if (DrawLine(hWnd, mdc, bf, vvi[i].child, vvi[i].child_count, index) == TRUE) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

/*
 * initValueList - 変数リストの初期化
 */
static void initValueList(VALUE_VIEW_INFO *vvi, int vvi_count)
{
	int i;
	for (i = 0; i < vvi_count; i++) {
		vvi[i].change = FALSE;
		vvi[i].valid = FALSE;
		if (vvi[i].child != NULL) {
			initValueList(vvi[i].child, vvi[i].child_count);
		}
	}
}

/*
 * findValueList - 変数リストからオリジナル変数と同一の変数を検索
 */
static int findValueList(VALUEINFO *vi, VALUE_VIEW_INFO *vvi, int vvi_count)
{
	int i;
	for (i = 0; i < vvi_count; i++) {
		if (vvi[i].org_vi == vi) {
			return i;
		}
	}
	return -1;
}

/*
 * deselectValueList - 変数リストの選択解除
 */
static void deselectValueList(VALUE_VIEW_INFO *vvi, int vvi_count)
{
	int i;
	for (i = 0; i < vvi_count; i++) {
		vvi[i].select = FALSE;
		if (vvi[i].child != NULL) {
			deselectValueList(vvi[i].child, vvi[i].child_count);
		}
	}
}

/*
 * trimValueList - 変数リストの整形
 */
static void trimValueList(VALUE_VIEW_INFO *vvi, int *count)
{
	int i;
	for (i = *count - 1; i >= 0; i--) {
		if (vvi[i].child != NULL) {
			trimValueList(vvi[i].child, &vvi[i].child_count);
		}
		if (vvi[i].valid == FALSE) {
			FreeValue(vvi[i].vi);
			mem_free(&(vvi[i].name));
			mem_free(&(vvi[i].buf));
			if (vvi[i].child != NULL) {
				freeValueinfo(vvi[i].child, vvi[i].child_count);
				mem_free(&(vvi[i].child));
				vvi[i].child_count = 0;
			}
			if ((*count) - i - 1 > 0) {
				MoveMemory(vvi + i, vvi + i + 1, sizeof(VALUE_VIEW_INFO) * ((*count) - i - 1));
			}
			(*count)--;
		}
	}
	for (i = 0; i < *count; i++) {
		if (vvi[i].name == NULL) {
			// 配列の場合は名前をインデックスとする
			TCHAR tmp[BUF_SIZE];
			wsprintf(tmp, TEXT("[%d]"), i);
			vvi[i].name = (TCHAR *)mem_alloc(sizeof(TCHAR) * (lstrlen(tmp) + 1));
			if (vvi[i].name != NULL) {
				lstrcpy(vvi[i].name, tmp);
			}
		}
	}
}

/*
 * getValueCount - 変数の数を取得
 */
static int getValueCount(VALUEINFO *vi)
{
	int cnt = 0;
	for(; vi != NULL; vi = vi->next){
		cnt++;
	}
	return cnt;
}

/*
 * setValueList - 変数リストの作成
 */
static void setValueList(VALUEINFO *vi, VALUE_VIEW_INFO *vvi, int *vvi_count)
{
	for(; vi != NULL; vi = vi->next){
		TCHAR *name;
		TCHAR *buf;
		// 前回の変数を検索
		int index = findValueList(vi, vvi, *vvi_count);
		if (index == -1) {
			// 新規変数
			index = *vvi_count;
			(*vvi_count)++;
			ZeroMemory(vvi + index, sizeof(VALUE_VIEW_INFO));
			vvi[index].org_vi = vi;
			vvi[index].change = TRUE;
		} else {
			FreeValue(vvi[index].vi);
			if (vvi[index].child != NULL && vi->v->type != TYPE_ARRAY) {
				freeValueinfo(vvi[index].child, vvi[index].child_count);
				mem_free(&(vvi[index].child));
				vvi[index].child_count = 0;
			}
		}
		vvi[index].valid = TRUE;
		// 変数のコピー
		vvi[index].vi = CopyValue(vi);
		if (vvi[index].vi == NULL) {
			return;
		}
		mem_free(&(vvi[index].name));

		name = vvi[index].vi->org_name;
		if (name == NULL) {
			name = vvi[index].vi->name;
		}
		vvi[index].name = alloc_copy(name);
		// 変数内容の作成
		if (vvi[index].vi->v->type == TYPE_ARRAY) {
			int cnt;
			int size = ArrayToStringSize(vvi[index].vi->v->u.array, hex_mode);
			buf = (TCHAR *)mem_alloc(sizeof(TCHAR) * (size + 1));
			if (buf != NULL) {
				ArrayToString(vvi[index].vi->v->u.array, buf, hex_mode);
			}
			cnt = getValueCount(vi->v->u.array);
			if (vvi[index].child == NULL) {
				vvi[index].alloc_count = cnt;
				vvi[index].child = mem_calloc(sizeof(VALUE_VIEW_INFO) * vvi[index].alloc_count);
				vvi[index].child_count = 0;
			} else if (vvi[index].child_count + cnt > vvi[index].alloc_count) {
				VALUE_VIEW_INFO *tmp_list;
				vvi[index].alloc_count = vvi[index].child_count + cnt;
				tmp_list = mem_alloc(sizeof(VALUE_VIEW_INFO) * vvi[index].alloc_count);
				CopyMemory(tmp_list, vvi[index].child, sizeof(VALUE_VIEW_INFO) * vvi[index].child_count);
				mem_free(&(vvi[index].child));
				vvi[index].child = tmp_list;
			}
			setValueList(vi->v->u.array, vvi[index].child, &(vvi[index].child_count));
		} else if (vvi[index].vi->v->type == TYPE_STRING) {
			TCHAR *tmp = reconv_ctrl(vvi[index].vi->v->u.sValue);
			buf = (TCHAR *)mem_alloc(sizeof(TCHAR) * (lstrlen(tmp) + 2 + 1));
			wsprintf(buf, TEXT("\"%s\""), tmp);
			mem_free(&tmp);
		} else if (vvi[index].vi->v->type == TYPE_FLOAT) {
			TCHAR tmp[FLOAT_LENGTH];
			_stprintf_s(tmp, FLOAT_LENGTH, TEXT("%.16f"), vvi[index].vi->v->u.fValue);
			buf = (TCHAR *)mem_alloc(sizeof(TCHAR) * (lstrlen(tmp) + 1));
			if (buf != NULL) {
				lstrcpy(buf, tmp);
			}
		} else {
			TCHAR tmp[BUF_SIZE];
			if (hex_mode == FALSE) {
				wsprintf(tmp, TEXT("%ld"), vvi[index].vi->v->u.iValue);
			} else {
				wsprintf(tmp, TEXT("0x%X"), vvi[index].vi->v->u.iValue);
			}
			buf = (TCHAR *)mem_alloc(sizeof(TCHAR) * (lstrlen(tmp) + 1));
			if (buf != NULL) {
				lstrcpy(buf, tmp);
			}
		}
		if (buf != NULL && lstrlen(buf) > BUF_SIZE + 3) {
			buf[BUF_SIZE + 1] = TEXT('.');
			buf[BUF_SIZE + 2] = TEXT('.');
			buf[BUF_SIZE + 3] = TEXT('.');
			buf[BUF_SIZE + 4] = TEXT('\0');
		}
		if (buf != NULL && vvi[index].buf != NULL && lstrcmp(vvi[index].buf, buf) != 0) {
			// 前回から変化あり
			vvi[index].change = TRUE;
		}
		mem_free(&(vvi[index].buf));
		vvi[index].buf = buf;
	}
}

/*
 * listValueinfo - 変数を配列にコピー
 */
static void listValueinfo(EXECINFO *ei)
{
	VALUE_VIEW_INFO *tmp_list;
	int cnt;

	if (ei == NULL) {
		return;
	}
	listValueinfo(ei->parent);

	cnt = getValueCount(ei->vi);
	if (vi_list == NULL) {
		alloc_count = cnt;
		vi_list = mem_alloc(sizeof(VALUE_VIEW_INFO) * alloc_count);
		vi_list_count = 0;
	} else if (vi_list_count + cnt > alloc_count) {
		alloc_count = vi_list_count + cnt;
		tmp_list = mem_alloc(sizeof(VALUE_VIEW_INFO) * alloc_count);
		CopyMemory(tmp_list, vi_list, sizeof(VALUE_VIEW_INFO) * vi_list_count);
		mem_free(&(vi_list));
		vi_list = tmp_list;
	}
	setValueList(ei->vi, vi_list, &vi_list_count);
}

/*
 * freeValueinfo - 変数リストの解放
 */
static void freeValueinfo(VALUE_VIEW_INFO *vvi, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		FreeValue(vvi[i].vi);
		mem_free(&(vvi[i].name));
		mem_free(&(vvi[i].buf));
		if (vvi[i].child != NULL) {
			freeValueinfo(vvi[i].child, vvi[i].child_count);
			mem_free(&(vvi[i].child));
			vvi[i].child_count = 0;
		}
	}
}

/*
 * setValueIndex - 変数に表示用インデックスを設定
 */
static void setValueIndex(VALUE_VIEW_INFO *vvi, int count, int *index, int indent)
{
	int i;
	for (i = 0; i < count; i++) {
		vvi[i].index = *index;
		vvi[i].indent = indent;
		(*index)++;
		if (vvi[i].child != NULL && vvi[i].open == TRUE) {
			setValueIndex(vvi[i].child, vvi[i].child_count, index, indent + 1);
		}
	}
}

/*
 * getValueDrawCount - 変数の表示数を取得
 */
static int getValueDrawCount(VALUE_VIEW_INFO *vvi, int count)
{
	int cnt = 0;
	int i;
	for (i = 0; i < count; i++) {
		cnt++;
		if (vvi[i].child != NULL && vvi[i].open == TRUE) {
			cnt += getValueDrawCount(vvi[i].child, vvi[i].child_count);
		}
	}
	return cnt;
}

/*
 * indexToValueViewInfo - 表示インデックスに対応する変数表示情報を取得
 */
static VALUE_VIEW_INFO *indexToValueViewInfo(VALUE_VIEW_INFO *vvi, int count, int index)
{
	int i;
	for (i = 0; i < count; i++) {
		if (vvi[i].index == index) {
			return vvi + i;
		}
		if (vvi[i].child != NULL && vvi[i].open == TRUE) {
			VALUE_VIEW_INFO *ret = indexToValueViewInfo(vvi[i].child, vvi[i].child_count, index);
			if (ret != NULL) {
				return ret;
			}
		}
	}
	return NULL;
}

/*
 * getSelectValueViewInfo - 選択中の変数表示情報を取得
 */
static VALUE_VIEW_INFO *getSelectValueViewInfo(VALUE_VIEW_INFO *vvi, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		if (vvi[i].select == TRUE) {
			return vvi + i;
		}
		if (vvi[i].child != NULL && vvi[i].open == TRUE) {
			VALUE_VIEW_INFO *ret = getSelectValueViewInfo(vvi[i].child, vvi[i].child_count);
			if (ret != NULL) {
				return ret;
			}
		}
	}
	return NULL;
}

/*
 * ShowSelectValueViewInfo - 選択中の変数表示情報を表示位置にスクロールする
 */
static void ShowSelectValueViewInfo(HWND hWnd, VARIABLE_BUFFER *bf)
{
	VALUE_VIEW_INFO *vvi = getSelectValueViewInfo(vi_list, vi_list_count);
	RECT rect;
	GetClientRect(hWnd, &rect);
	if (bf->pos_y > vvi->index) {
		bf->pos_y = vvi->index;
		SetScrollPos(hWnd, SB_VERT, bf->pos_y, TRUE);
		InvalidateRect(hWnd, NULL, FALSE);
		UpdateWindow(hWnd);
	} else if (vvi->index >= (bf->pos_y + (rect.bottom / bf->FontHeight) - 1)) {
		bf->pos_y = vvi->index - (rect.bottom / bf->FontHeight) + 2;
		if (bf->pos_y >= bf->view_line) {
			bf->pos_y = bf->view_line - 1;
		}
		SetScrollPos(hWnd, SB_VERT, bf->pos_y, TRUE);
		InvalidateRect(hWnd, NULL, FALSE);
		UpdateWindow(hWnd);
	}
}

/*
 * VariableProc - ウィンドウプロシージャ
 */
static LRESULT CALLBACK VariableProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	VARIABLE_BUFFER *bf;
	HDC hDC;
	HFONT hFont;
	HFONT hRetFont;
	RECT rect;
	POINT apos;
	int i;
	static BOOL sep_move;
	BOOL focus_flag;
#ifdef OP_XP_STYLE
	static FARPROC _OpenThemeData;
	static FARPROC _CloseThemeData;
	static FARPROC _DrawThemeBackground;
#endif	// OP_XP_STYLE

	switch(message)
	{
	case WM_CREATE:
		SetCursor(LoadCursor(NULL, IDC_ARROW));

		bf = mem_calloc(sizeof(VARIABLE_BUFFER));
		if(bf == NULL) return -1;

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

		bf->LeftMargin = 10;
		bf->RightMargin = 5;
		bf->view_line = 0;
		bf->pos_y = 0;
		bf->sep_size = 100;

		hDC = GetDC(hWnd);
		GetClientRect(hWnd, &rect);
		bf->mdc = CreateCompatibleDC(hDC);
		bf->hBmp = CreateCompatibleBitmap(hDC, rect.right, rect.bottom);
		bf->hRetBmp = SelectObject(bf->mdc, bf->hBmp);
		ReleaseDC(hWnd, hDC);

		SetMapMode(bf->mdc, MM_TEXT);
		SetTextCharacterExtra(bf->mdc, 0);
		SetTextJustification(bf->mdc, 0, 0);
		SetTextAlign(bf->mdc, TA_TOP | TA_LEFT);
		SetBkMode(bf->mdc, TRANSPARENT);

		bf->TextColor = GetSysColor(COLOR_WINDOWTEXT);
		bf->BackClolor = GetSysColor(COLOR_WINDOW);
		bf->hBkBrush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));

		//buffer info to window long
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)bf);

		SendMessage(hWnd, WM_VIEW_REFLECT, 0, 0);
		break;

	case WM_DESTROY:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf != NULL){
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

			SelectObject(bf->mdc, bf->hRetBmp);
			DeleteObject(bf->hBmp);
			DeleteDC(bf->mdc);

			DeleteObject(bf->hBkBrush);

			mem_free(&bf);
		}
		freeValueinfo(vi_list, vi_list_count);
		mem_free(&vi_list);
		vi_list_count = 0;
		return DefWindowProc(hWnd, message, wParam, lParam);

	case WM_SETFOCUS:
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_KILLFOCUS:
		InvalidateRect(hWnd, NULL, FALSE);
		UpdateWindow(hWnd);
		break;

	case WM_GETDLGCODE:
		return DLGC_WANTALLKEYS;

	case WM_HSCROLL:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		GetClientRect(hWnd, &rect);
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
			bf->pos_x = (bf->pos_x - (rect.right / bf->CharWidth) > 0) ?
				bf->pos_x - (rect.right / bf->CharWidth) : 0;
			break;

		case SB_PAGERIGHT:
			bf->pos_x = (bf->pos_x + (rect.right / bf->CharWidth) < bf->max_x) ?
				bf->pos_x + (rect.right / bf->CharWidth) : bf->max_x;
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
		SetScrollPos(hWnd, SB_HORZ, bf->pos_x, TRUE);
		ScrollWindowEx(hWnd, (i - bf->pos_x) * bf->CharWidth, 0, NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
		break;

	case WM_VSCROLL:
		if ((bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL) {
			break;
		}
		GetClientRect(hWnd, &rect);
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
			bf->pos_y = (bf->pos_y - ((rect.bottom - bf->header_height) / bf->FontHeight) > 0) ?
				bf->pos_y - ((rect.bottom - bf->header_height) / bf->FontHeight) : 0;
			break;

		case SB_PAGEDOWN:
			bf->pos_y = (bf->pos_y + ((rect.bottom - bf->header_height)/ bf->FontHeight) < bf->max_y) ?
				bf->pos_y + ((rect.bottom - bf->header_height) / bf->FontHeight) : bf->max_y;
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
		SetScrollPos(hWnd, SB_VERT, bf->pos_y, TRUE);
		rect.top = bf->header_height;
		ScrollWindowEx(hWnd, 0, (i - bf->pos_y) * bf->FontHeight, NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
		break;

	case WM_KEYDOWN:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		GetClientRect(hWnd, &rect);
		switch(wParam)
		{
		case TEXT('C'):
			if(GetKeyState(VK_CONTROL) < 0){
				//コピー
				SendMessage(hWnd, WM_COPY, 0, 0);
			}
			break;

		case VK_SPACE:
			if(GetKeyState(VK_CONTROL) < 0){
				// 選択解除
				deselectValueList(vi_list, vi_list_count);
				InvalidateRect(hWnd, NULL, FALSE);
				UpdateWindow(hWnd);
			} else {
				VALUE_VIEW_INFO *vvi = getSelectValueViewInfo(vi_list, vi_list_count);
				if (vvi != NULL && vvi->vi->v->type == TYPE_ARRAY) {
					int index = 0;
					vvi->open = !vvi->open;
					setValueIndex(vi_list, vi_list_count, &index, 0);
					bf->view_line = getValueDrawCount(vi_list, vi_list_count);
					bf->draw_width = getDrawMaxWidth(bf->mdc, bf);
					SetScrollBar(hWnd, bf);
					InvalidateRect(hWnd, NULL, FALSE);
					UpdateWindow(hWnd);
				}
			}
			break;

		case VK_TAB:
			SetFocus(GetNextWindow(hWnd, GW_HWNDNEXT));
			break;

		case VK_HOME:
			SetScrollPos(hWnd, SB_VERT, 0, TRUE);
			InvalidateRect(hWnd, NULL, FALSE);
			UpdateWindow(hWnd);
			break;

		case VK_END:
			//全体の末尾
			SetScrollPos(hWnd, SB_VERT, bf->view_line, TRUE);
			InvalidateRect(hWnd, NULL, FALSE);
			UpdateWindow(hWnd);
			break;

		case VK_PRIOR:
			//Page UP
			break;

		case VK_NEXT:
			//Page Down
			break;

		case VK_UP:
			{
				VALUE_VIEW_INFO *vvi = getSelectValueViewInfo(vi_list, vi_list_count);
				if (vvi == NULL) {
					vvi = indexToValueViewInfo(vi_list, vi_list_count, 0);
					vvi->select = TRUE; 
				} else if (vvi->index > 0) {
					deselectValueList(vi_list, vi_list_count);
					vvi = indexToValueViewInfo(vi_list, vi_list_count, vvi->index - 1);
					vvi->select = TRUE; 
				}
				ShowSelectValueViewInfo(hWnd, bf);
				InvalidateRect(hWnd, NULL, FALSE);
				UpdateWindow(hWnd);
			}
			break;

		case VK_DOWN:
			{
				VALUE_VIEW_INFO *vvi = getSelectValueViewInfo(vi_list, vi_list_count);
				if (vvi == NULL) {
					vvi = indexToValueViewInfo(vi_list, vi_list_count, 0);
					vvi->select = TRUE; 
				} else if (vvi->index < bf->view_line - 1) {
					deselectValueList(vi_list, vi_list_count);
					vvi = indexToValueViewInfo(vi_list, vi_list_count, vvi->index + 1);
					vvi->select = TRUE; 
				}
				ShowSelectValueViewInfo(hWnd, bf);
				InvalidateRect(hWnd, NULL, FALSE);
				UpdateWindow(hWnd);
			}
			break;

		case VK_LEFT:
			i = bf->pos_x;
			bf->pos_x = (bf->pos_x > 0) ? bf->pos_x - 1 : 0;
			SetScrollPos(hWnd, SB_HORZ, bf->pos_x, TRUE);
			ScrollWindowEx(hWnd, (i - bf->pos_x) * bf->CharWidth, 0, NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
			break;

		case VK_RIGHT:
			i = bf->pos_x;
			bf->pos_x = (bf->pos_x < bf->max_x) ? bf->pos_x + 1 : bf->max_x;
			SetScrollPos(hWnd, SB_HORZ, bf->pos_x, TRUE);
			ScrollWindowEx(hWnd, (i - bf->pos_x) * bf->CharWidth, 0, NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
			break;
		}
		break;

	case WM_LBUTTONDOWN:
		focus_flag = TRUE;
		if (GetFocus() != hWnd) {
			SetFocus(hWnd);
			focus_flag = FALSE;
		}
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		GetCursorPos((LPPOINT)&apos);
		GetWindowRect(hWnd, (LPRECT)&rect);
		if (!(apos.x - rect.left - GetSystemMetrics(SM_CXFRAME) + (bf->pos_x * bf->CharWidth) > bf->sep_size - 10 &&
			apos.x - rect.left - GetSystemMetrics(SM_CXFRAME) + (bf->pos_x * bf->CharWidth) < bf->sep_size + 10)) {
			if ((apos.y - rect.top - bf->header_height) >= 0) {
				// 行選択
				RECT iconRect;
				int click_line = (apos.y - rect.top - bf->header_height) / bf->FontHeight + bf->pos_y;
				VALUE_VIEW_INFO *vvi = indexToValueViewInfo(vi_list, vi_list_count, click_line);
				if (vvi != NULL && vvi->vi->v->type == TYPE_ARRAY) {
					iconRect = getTreeIconRect(bf->mdc, bf, vvi);
				}
				if (vvi != NULL && vvi->vi->v->type == TYPE_ARRAY && iconRect.left <= apos.x - rect.left - GetSystemMetrics(SM_CXFRAME) && iconRect.right >= apos.x - rect.left - GetSystemMetrics(SM_CXFRAME)) {
					int index = 0;
					vvi->open = !vvi->open;
					setValueIndex(vi_list, vi_list_count, &index, 0);
					bf->view_line = getValueDrawCount(vi_list, vi_list_count);
					bf->draw_width = getDrawMaxWidth(bf->mdc, bf);
					SetScrollBar(hWnd, bf);
				} else if (vvi == NULL) {
					// 選択解除
					deselectValueList(vi_list, vi_list_count);
				} else if (vvi->select == TRUE) {
					if (focus_flag == TRUE) {
						vvi->select = FALSE;
					}
				} else {
					deselectValueList(vi_list, vi_list_count);
					vvi->select = TRUE; 
				}
				InvalidateRect(hWnd, NULL, FALSE);
				UpdateWindow(hWnd);
			}
			break;
		}
		if (InitializeFrame(hWnd) == FALSE) {
			break;
		}
		sep_move = TRUE;
		SetTimer(hWnd, TIMER_SEP, 1, NULL);
		break;

	case WM_MOUSEMOVE:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		GetCursorPos((LPPOINT)&apos);
		GetWindowRect(hWnd, (LPRECT)&rect);
		if (apos.x - rect.left - GetSystemMetrics(SM_CXFRAME) + (bf->pos_x * bf->CharWidth) > bf->sep_size - 10 &&
			apos.x - rect.left - GetSystemMetrics(SM_CXFRAME) + (bf->pos_x * bf->CharWidth) < bf->sep_size + 10) {
			SetCursor(LoadCursor(NULL, IDC_SIZEWE));
		} else {
			SetCursor(LoadCursor(NULL, IDC_ARROW));
		}
		if (sep_move == TRUE) {
			int ret = DrawVariableFrame(hWnd);
			GetClientRect(hWnd, &rect);
			bf->sep_size = (bf->pos_x * bf->CharWidth) + ret;
			if (bf->sep_size < 50) {
				bf->sep_size = 50;
			}
			SendMessage(hWnd, WM_SIZE, 0, 0);
		}
		break;

	case WM_LBUTTONUP:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		if (sep_move == TRUE) {
			KillTimer(hWnd, TIMER_SEP);
			FreeFrame();
			sep_move = FALSE;

			bf->draw_width = getDrawMaxWidth(bf->mdc, bf);
			SendMessage(hWnd, WM_SIZE, 0, 0);
		}
		break;

	case WM_TIMER:
		switch (wParam) {
		case TIMER_SEP:
			if (GetParent(hWnd) != GetForegroundWindow() || GetAsyncKeyState(VK_ESCAPE) < 0 || GetAsyncKeyState(VK_RBUTTON) < 0) {
				SendMessage(hWnd, WM_LBUTTONUP, 0, 0);
			}
			break;
		}
		break;

	case WM_LBUTTONDBLCLK:
		if (vi_list_count == 0) {
			break;
		}
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		GetCursorPos((LPPOINT)&apos);
		GetWindowRect(hWnd, (LPRECT)&rect);
		if (!(apos.x - rect.left - GetSystemMetrics(SM_CXFRAME) + (bf->pos_x * bf->CharWidth) > bf->sep_size - 10 &&
			apos.x - rect.left - GetSystemMetrics(SM_CXFRAME) + (bf->pos_x * bf->CharWidth) < bf->sep_size + 10)) {
			int click_line = (apos.y - rect.top - bf->header_height) / bf->FontHeight + bf->pos_y;
			VALUE_VIEW_INFO *vvi = indexToValueViewInfo(vi_list, vi_list_count, click_line);
			if (vvi != NULL) {
				deselectValueList(vi_list, vi_list_count);
				vvi->select = TRUE;
				ShowSelectValueViewInfo(hWnd, bf);
				InvalidateRect(hWnd, NULL, FALSE);
				UpdateWindow(hWnd);
				SendMessage(hWnd, WM_COPY, 0, 0);
			}
			break;
		}
		bf->sep_size = getNameMaxWidth(bf->mdc, bf, vi_list, vi_list_count) + bf->CharWidth;
		if (bf->sep_size < 50) {
			bf->sep_size = 50;
		}
		bf->draw_width = getDrawMaxWidth(bf->mdc, bf);
		SendMessage(hWnd, WM_SIZE, 0, 0);
		break;

	case WM_MOUSEWHEEL:
		for(i = 0; i < 3; i++){
			SendMessage(hWnd, WM_VSCROLL, ((short)HIWORD(wParam) > 0) ? SB_LINEUP : SB_LINEDOWN, 0);
		}
		break;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;

			bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
			if(bf == NULL) break;

			hDC = BeginPaint(hWnd, &ps);
			GetClientRect(hWnd, &rect);

			hFont = CreateViewFont(bf->mdc, bf);
			hRetFont = SelectObject(bf->mdc, hFont);

			FillRect(bf->mdc, &rect, bf->hBkBrush);

			i = bf->pos_y + ((ps.rcPaint.top - bf->header_height) / bf->FontHeight);
			if(i >= bf->view_line) i = bf->view_line - 1;
			if (i < 0) {
				i = 0;
			}
			for(; i < bf->view_line && i < bf->pos_y + ((ps.rcPaint.bottom - bf->header_height) / bf->FontHeight) + 1; i++){
				DrawLine(hWnd, bf->mdc, bf, vi_list, vi_list_count, i);
			}
			DrawHeader(bf->mdc, bf);
			DrawSeparator(bf->mdc, bf, rect.bottom);
			BitBlt(hDC, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right, ps.rcPaint.bottom,
				bf->mdc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);

			hFont = SelectObject(bf->mdc, hRetFont);
			DeleteObject(hFont);

			EndPaint(hWnd, &ps);
		}
		break;

#ifdef OP_XP_STYLE
	case WM_NCPAINT:
		{
			HRGN hrgn;
			DWORD stats;
			RECT clip_rect;

			if ((bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->hTheme == NULL) {
				return DefWindowProc(hWnd, message, wParam, lParam);
			}
			// XP用の背景描画
			if (_DrawThemeBackground == NULL) {
				_DrawThemeBackground = GetProcAddress(bf->hModThemes, "DrawThemeBackground");
			}
			if (_DrawThemeBackground == NULL) {
				return DefWindowProc(hWnd, message, wParam, lParam);
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
			hDC = GetDCEx(hWnd, (HRGN)wParam, DCX_WINDOW | DCX_INTERSECTRGN);
			if (hDC == NULL) {
				hDC = GetWindowDC(hWnd);
			}
			GetWindowRect(hWnd, &rect);
			OffsetRect(&rect, -rect.left, -rect.top);
			ExcludeClipRect(hDC, rect.left + GetSystemMetrics(SM_CXEDGE), rect.top + GetSystemMetrics(SM_CYEDGE),
				rect.right - GetSystemMetrics(SM_CXEDGE), rect.bottom - GetSystemMetrics(SM_CYEDGE));
			clip_rect = rect;
			_DrawThemeBackground(bf->hTheme, hDC, EP_EDITTEXT, stats, &rect, &clip_rect);
			ReleaseDC(hWnd, hDC);

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
		if ((bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->hModThemes == NULL) {
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

	case WM_ERASEBKGND:
		return 1;

	case WM_SIZE:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		SetScrollBar(hWnd, bf);

		GetClientRect(hWnd, &rect);
		if(bf->width != rect.right){
			bf->width = rect.right;
		}

		hDC = GetDC(hWnd);
		SelectObject(bf->mdc, bf->hRetBmp);
		DeleteObject(bf->hBmp);
		bf->hBmp = CreateCompatibleBitmap(hDC, rect.right, rect.bottom);
		bf->hRetBmp = SelectObject(bf->mdc, bf->hBmp);
		ReleaseDC(hWnd, hDC);

		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_VIEW_SETVARIABLE:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		if (lParam == 0) {
			freeValueinfo(vi_list, vi_list_count);
			mem_free(&vi_list);
			vi_list_count = 0;
		} else {
			int index = 0;
			initValueList(vi_list, vi_list_count);
			listValueinfo((EXECINFO *)lParam);
			trimValueList(vi_list, &vi_list_count);
			setValueIndex(vi_list, vi_list_count, &index, 0);
		}
		bf->view_line = getValueDrawCount(vi_list, vi_list_count);
		bf->draw_width = getDrawMaxWidth(bf->mdc, bf);
		SetScrollBar(hWnd, bf);
		InvalidateRect(hWnd, NULL, FALSE);
		UpdateWindow(hWnd);
		break;

	case WM_VIEW_GETBUFFERINFO:
		*((VARIABLE_BUFFER **)lParam) = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		break;

	case WM_VIEW_SETCOLOR:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;

		bf->TextColor = ((int)wParam == -1) ? GetSysColor(COLOR_WINDOWTEXT) : (COLORREF)wParam;
		bf->BackClolor = (lParam == -1) ? GetSysColor(COLOR_WINDOW) : (COLORREF)lParam;

		DeleteObject(bf->hBkBrush);
		bf->hBkBrush = CreateSolidBrush(bf->BackClolor);
		InvalidateRect(hWnd, NULL, FALSE);
		UpdateWindow(hWnd);
		break;

	case WM_VIEW_REFLECT:
		{
			TEXTMETRIC tm;

			bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
			if(bf == NULL) break;

			hFont = CreateViewFont(bf->mdc, bf);
			hRetFont = SelectObject(bf->mdc, hFont);
			//Metrics
			GetTextMetrics(bf->mdc, &tm);
			bf->FontHeight = tm.tmHeight + bf->Spacing + tm.tmHeight / 3;
			bf->header_height = bf->FontHeight + (bf->Spacing / 2);
			bf->CharWidth = tm.tmAveCharWidth;
			bf->Fixed = (tm.tmPitchAndFamily & TMPF_FIXED_PITCH) ? FALSE : TRUE;
			bf->LeftMargin = bf->FontHeight;
			SelectObject(bf->mdc, hRetFont);
			DeleteObject(hFont);

			bf->draw_width = getDrawMaxWidth(bf->mdc, bf);
			bf->width = 0;
			SendMessage(hWnd, WM_SIZE, 0, 0);
		}
		break;

	case WM_VIEW_IS_SELECTION:
		if (getSelectValueViewInfo(vi_list, vi_list_count) == NULL) {
			return FALSE;
		}
		return TRUE;

	case WM_SETFONT:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;

		if(GetObject((HGDIOBJ)wParam, sizeof(LOGFONT), &(bf->lf)) == 0){
			break;
		}
		SendMessage(hWnd, WM_VIEW_REFLECT, 0, 0);
		break;

	case WM_COPY:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf != NULL) {
			VALUE_VIEW_INFO *vvi = getSelectValueViewInfo(vi_list, vi_list_count);
			if (vvi != NULL) {
				Variable2Clipboard(hWnd, vvi);
			}
		}
		break;

	case EM_GETFIRSTVISIBLELINE:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		return bf->pos_y;

	case EM_GETLINECOUNT:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		return bf->view_line;

	case WM_VIEW_SET_SEPSIZE:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		bf->sep_size = (long)lParam;
		if (bf->sep_size < 50) {
			bf->sep_size = 50;
		}
		SendMessage(hWnd, WM_SIZE, 0, 0);
		break;

	case WM_VIEW_GET_SEPSIZE:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		return bf->sep_size;

	case WM_VIEW_SET_HEADER_NAME:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		if ((TCHAR *)lParam != NULL) {
			lstrcpy(bf->header_name, (TCHAR *)lParam);
		}
		break;

	case WM_VIEW_SET_HEADER_VALUE:
		bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		if ((TCHAR *)lParam != NULL) {
			lstrcpy(bf->header_value, (TCHAR *)lParam);
		}
		break;

	case WM_VIEW_SET_HEX_MODE:
		hex_mode = (BOOL)wParam;
		SendMessage(hWnd, WM_VIEW_REFLECT, 0, 0);
		break;

	case WM_CONTEXTMENU:
		if ((bf = (VARIABLE_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) != NULL) {
			HMENU hMenu;
			POINT apos;
			WORD lang = PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale()));
			VALUE_VIEW_INFO *vvi = getSelectValueViewInfo(vi_list, vi_list_count);

			// メニューの作成
			hMenu = CreatePopupMenu();
			AppendMenu(hMenu, MF_STRING | ((vvi != NULL) ? 0 : MF_GRAYED), WM_COPY,
				(lang != LANG_JAPANESE) ? TEXT("&Copy") : TEXT("コピー(&C)"));

			// メニューの表示
			GetCursorPos((LPPOINT)&apos);
			i = TrackPopupMenu(hMenu, TPM_TOPALIGN | TPM_RETURNCMD, apos.x, apos.y, 0, hWnd, NULL);
			DestroyMenu(hMenu);
			if (i != 0) {
				SendMessage(hWnd, i, 0, 0);
			}
		}
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

/*
 * RegisterVariableWindow - 1クラスの登録
 */
BOOL RegisterVariableWindow(HINSTANCE hInstance)
{
	WNDCLASS wc;

	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = (WNDPROC)VariableProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = NULL;
	wc.hCursor = NULL;
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName = 0;
	wc.lpszClassName = VARIABLE_WND_CLASS;
	return RegisterClass(&wc);
}
/* End of source */
