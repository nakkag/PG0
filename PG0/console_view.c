/*
 * PG0
 *
 * console_view.c
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#include <windows.h>
#ifdef OP_XP_STYLE
#include <uxtheme.h>
#include <vssym32.h>
#endif	// OP_XP_STYLE

#include "console_view.h"
#include "script_memory.h"

/* Define */
#define MAKEBYTE(a, b)			((a & 0xF) | (b << 4))
#define HICOLOR(c)				((c >> 4) & 0xF)

#ifdef UNICODE
#define a2i						_wtoi
#else
#define a2i						atoi
#endif
#define LOCOLOR(c)				(c & 0xF)

/* ホイールメッセージ */
#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL			0x020A
#endif
// XPテーマ変更通知
#ifndef WM_THEMECHANGED
#define WM_THEMECHANGED			0x031A
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

/* Global Variables */
typedef struct _PUTINFO {
	BOOL sel;
	BOOL bold;
	BOOL underline;
	BOOL reverse;
	BOOL italic;
	COLORREF txtColor;
	COLORREF backColor;
	COLORREF rtxtColor;
	COLORREF rbackColor;
} PUTINFO;

/* Local Function Prototypes */
static TCHAR *TStrCpy(TCHAR *ret, const TCHAR *buf);
static HFONT CreateViewFont(const HDC hdc, const VIEW_BUFFER *bf,
							const BOOL bold, const BOOL underline, const BOOL italic);
static BOOL Text2Clipboard(const HWND hWnd, const TCHAR *st, const TCHAR *en);
static void SetScrollBar(HWND hWnd, VIEW_BUFFER *bf, int bottom);
static BOOL AllocLine(VIEW_BUFFER *bf);
static BOOL CountLine(HWND hWnd, VIEW_BUFFER *bf, TCHAR *st);
static void ReCountLine(VIEW_BUFFER *bf, int ln);
static BOOL AddString(HWND hWnd, VIEW_BUFFER *bf, TCHAR *str);
static void DrawLine(HWND hWnd, HDC mdc, VIEW_BUFFER *bf, int i, PUTINFO *pi);
static int Char2Caret(HWND hWnd, HDC mdc, VIEW_BUFFER *bf, int i, PUTINFO *pi, TCHAR *cp);
static TCHAR *Point2Caret(HWND hWnd, VIEW_BUFFER *bf, int x, int y);
static void GetCaretToken(VIEW_BUFFER *bf, TCHAR **st, TCHAR **en);
static void RefreshLine(HWND hWnd, VIEW_BUFFER *bf, TCHAR *p, TCHAR *r);

/*
 * TStrCpy - 文字列をコピーして最後の文字のアドレスを返す
 */
static TCHAR *TStrCpy(TCHAR *ret, const TCHAR *buf)
{
	if(buf == NULL){
		*ret = TEXT('\0');
		return ret;
	}
	while(*(ret++) = *(buf++));
	ret--;
	return ret;
}

/*
 * CreateViewFont - フォントを作成する
 */
static HFONT CreateViewFont(const HDC hdc, const VIEW_BUFFER *bf,
							const BOOL bold, const BOOL underline, const BOOL italic)
{
	LOGFONT lf;

	CopyMemory(&lf, &(bf->lf), sizeof(LOGFONT));
	lf.lfWeight = (bold == TRUE) ? 700 : 0;
	lf.lfItalic = italic;
	lf.lfUnderline = underline;
	return CreateFontIndirect((CONST LOGFONT *)&lf);
}

/*
 * Text2Clipboard - 文字列をクリップボードに設定
 */
static BOOL Text2Clipboard(const HWND hWnd, const TCHAR *st, const TCHAR *en)
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

	if((hMem = GlobalAlloc(GHND, sizeof(TCHAR) * (en - st + 1))) == NULL){
		CloseClipboard();
		return FALSE;
	}
	if((buf = GlobalLock(hMem)) == NULL){
		GlobalFree(hMem);
		CloseClipboard();
		return FALSE;
	}
	lstrcpyn(buf, st, en - st + 1);
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
 * SetScrollBar - スクロールバーの設定
 */
static void SetScrollBar(HWND hWnd, VIEW_BUFFER *bf, int bottom)
{
	SCROLLINFO si;

	if((bottom / bf->FontHeight) < bf->view_line){
		bf->max = bf->view_line - (bottom / bf->FontHeight);
		EnableScrollBar(hWnd, SB_VERT, ESB_ENABLE_BOTH);

		ZeroMemory(&si, sizeof(SCROLLINFO));
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_POS | SIF_RANGE | SIF_PAGE;
		si.nPage = bottom / bf->FontHeight;
		si.nMax = bf->view_line - 1;
		si.nPos = bf->pos;
		SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
	}else{
		bf->max = 0;
		SetScrollRange(hWnd, SB_VERT, 0, bottom / bf->FontHeight + 1, TRUE);
		EnableScrollBar(hWnd, SB_VERT, ESB_DISABLE_BOTH);
	}
}

/*
 * GetColor - 色の取得
 */
static COLORREF GetColor(char c, COLORREF def)
{
	switch(c)
	{
	case 0: return RGB(255, 255, 255);
	case 1: return RGB(0, 0, 0);
	case 2: return RGB(0, 0, 128);
	case 3: return RGB(0, 128, 0);
	case 4: return RGB(255, 0, 0);
	case 5: return RGB(128, 0, 0);
	case 6: return RGB(128, 0, 128);
	case 7: return RGB(255, 128, 0);
	case 8: return RGB(255, 255, 0);
	case 9: return RGB(0, 255, 0);
	case 10: return RGB(0, 128, 128);
	case 11: return RGB(0, 255, 255);
	case 12: return RGB(0, 0, 255);
	case 13: return RGB(255, 0, 255);
	case 14: return RGB(128, 128, 128);
	case 15: return RGB(192, 192, 192);
	}
	return def;
}

/*
 * SetColor - 色の設定
 */
static TCHAR *SetColor(TCHAR *p, char *ret, char *style)
{
	TCHAR num[3];
	unsigned char tcol = 0, bcol = 0;

	//text color
	num[0] = num[1] = num[2] = TEXT('\0');
	if(*p >= TEXT('0') && *p <= TEXT('9')){
		num[0] = *(p++);
		if(*p >= TEXT('0') && *p <= TEXT('9')){
			num[1] = *(p++);
		}
	}
	if(num[0] == TEXT('\0')){
		//return color
		*style |= STYLE_RETURNCOLOR;
	}else{
		*style |= STYLE_TEXTCOLOR;
		tcol = a2i(num) % 16;
		if(*p == TEXT(',')){
			p++;
			//back color
			num[0] = num[1] = num[2] = TEXT('\0');
			if(*p >= TEXT('0') && *p <= TEXT('9')){
				num[0] = *(p++);
				if(*p >= TEXT('0') && *p <= TEXT('9')){
					num[1] = *(p++);
				}
			}
			if(num[0] != TEXT('\0')){
				*style |= STYLE_BACKCOLOR;
				bcol = a2i(num) % 16;
			}
		}
		*ret = MAKEBYTE(tcol, bcol);
	}
	return p;
}

/*
 * AllocLine - 行情報の確保
 */
static BOOL AllocLine(VIEW_BUFFER *bf)
{
	int *mi;

	bf->linesize += RESERVE_LINE;
	mi = mem_realloc(bf->line, sizeof(int) * bf->linesize);
	if(mi == NULL){
		return FALSE;
	}
	bf->line = mi;
	
	mi = mem_realloc(bf->linelen, sizeof(int) * bf->linesize);
	if(mi == NULL){
		return FALSE;
	}
	bf->linelen = mi;
	return TRUE;
}

/*
 * CountLine - 行情報の設定
 */
static BOOL CountLine(HWND hWnd, VIEW_BUFFER *bf, TCHAR *st)
{
	HFONT hFont;
	HFONT hRetFont;
	HFONT hTmpFont;
	RECT rect;
	SIZE sz;
	TCHAR *p;
	int line_size;
	int len = 0;
	int cnt = 0;
	int offset = bf->LeftMargin;
	int width;
	int clen;
	BOOL bold = FALSE;

	hFont = CreateViewFont(bf->mdc, bf, FALSE, FALSE, FALSE);
	hRetFont = SelectObject(bf->mdc, hFont);

	GetClientRect(hWnd, &rect);
	line_size = rect.right - bf->RightMargin;

	for(p = st; *p != TEXT('\0'); p++){
		if(*(bf->style + (p - bf->buf)) & STYLE_BOLD){
			//bold
			bold = (bold == TRUE) ? FALSE : TRUE;

			hFont = CreateViewFont(bf->mdc, bf, bold, FALSE, FALSE);
			hTmpFont = SelectObject(bf->mdc, hFont);
			DeleteObject(hTmpFont);
		}
		if(*p == TEXT('\r')){
		}else if(*p == TEXT('\n')){
			//new line
			*(bf->line + bf->view_line) = p + 1 - bf->buf;
			*(bf->linelen + bf->view_line - 1) = len;
			bf->view_line++;
			if(bf->view_line >= bf->linesize){
				if(AllocLine(bf) == FALSE){
					return FALSE;
				}
			}
			if(bold == TRUE){
				hFont = CreateViewFont(bf->mdc, bf, FALSE, FALSE, FALSE);
				hTmpFont = SelectObject(bf->mdc, hFont);
				DeleteObject(hTmpFont);
			}
			cnt = len = 0;
			offset = bf->LeftMargin;
			bold = FALSE;
		}else if(*p == TEXT('\t')){
			//tab
			offset += (bf->TabStop - (cnt % bf->TabStop)) * bf->CharWidth;
			cnt += bf->TabStop - (cnt % bf->TabStop);
			len++;
			if(offset > line_size){
				*(bf->line + bf->view_line) = p + 1 - bf->buf;
				*(bf->linelen + bf->view_line - 1) = len;
				bf->view_line++;
				if(bf->view_line >= bf->linesize){
					if(AllocLine(bf) == FALSE){
						return FALSE;
					}
				}
				cnt = len = 0;
				offset = bf->LeftMargin;
			}
		}else{
			//char
			clen = (IS_LEAD_TBYTE((TBYTE)*p) == TRUE && *(p + 1) != TEXT('\0')) ? 1 : 0;
			GetTextExtentPoint32(bf->mdc, p, clen + 1, &sz);
			width = sz.cx;
			if(offset + width >= line_size){
				//new line
				*(bf->line + bf->view_line) = p - bf->buf;
				*(bf->linelen + bf->view_line - 1) = len;
				bf->view_line++;
				if(bf->view_line >= bf->linesize){
					if(AllocLine(bf) == FALSE){
						return FALSE;
					}
				}
				cnt = len = clen + 1;
				offset = bf->LeftMargin + width;
			}else{
				len += clen + 1;
				cnt += clen + 1;
				offset += width;
			}
			p += clen;
		}
	}
	*(bf->linelen + bf->view_line - 1) = len;

	hFont = SelectObject(bf->mdc, hRetFont);
	DeleteObject(hFont);

	SetScrollBar(hWnd, bf, rect.bottom);
	return TRUE;
}

/*
 * ReCountLine - 行情報の再設定
 */
static void ReCountLine(VIEW_BUFFER *bf, int ln)
{
	int i, j;

	i = *(bf->line + ln);

	for(j = ln; j < bf->view_line; j++){
		*(bf->line + j) -= i;
	}
	CopyMemory(bf->line, bf->line + ln, sizeof(int) * (bf->view_line - ln));
	CopyMemory(bf->linelen, bf->linelen + ln, sizeof(int) * (bf->view_line - ln));
	bf->view_line -= ln;
	bf->max -= ln;
}

/*
 * AddString - 文字列の追加
 */
static BOOL AddString(HWND hWnd, VIEW_BUFFER *bf, TCHAR *str)
{
	RECT rect;
	TCHAR *p, *r, *s;
	char *cp;
	int len, len2;
	int ln;
	int last_color;
	int scroll_line, draw_line;
	int i, j;
	BOOL scroll, caret;
	BOOL reverse, underline;
	BOOL redraw = FALSE;

	len = bf->len;
	len2 = lstrlen(str);
	scroll = (bf->cp == bf->buf + len) ? TRUE : FALSE;
	caret = (bf->max == 0 || bf->pos == bf->max) ? TRUE : FALSE;
	p = str;

	if(bf->LineLimit > 0){
		//行数制限のチェック
		for(ln = 0, r = str; *r != TEXT('\0'); r++){
			if(*r == TEXT('\n')){
				ln++;
			}
		}
		if(ln >= bf->LineLimit){
			redraw = TRUE;
			scroll = TRUE;
			for(ln = 0, p = str + len2; p > str; p--){
				if(*p == TEXT('\n')){
					ln++;
					if(ln >= bf->LineLimit){
						break;
					}
				}
			}
			for(; *p == TEXT('\r') || *p == TEXT('\n'); p++);
			str = p;
			len2 = lstrlen(str);

			bf->size = len2 + RESERVE_SZIE;

			//buf init
			mem_free(&bf->buf);
			mem_free(&bf->style);
			mem_free(&bf->color);
			bf->buf = mem_alloc(sizeof(TCHAR) * bf->size);
			if(bf->buf == NULL) return FALSE;
			bf->color = mem_alloc(sizeof(char) * bf->size);
			if(bf->color == NULL) return FALSE;
			bf->style = mem_alloc(sizeof(char) * bf->size);
			if(bf->style == NULL) return FALSE;

			ZeroMemory(bf->color, sizeof(char) * bf->size);
			ZeroMemory(bf->style, sizeof(char) * bf->size);
			*bf->buf = TEXT('\0');
			bf->len = 0;
			bf->view_line = 1;
			*bf->line = 0;

			bf->logic_line = bf->LineLimit;

		}else if(ln + bf->logic_line > bf->LineLimit){
			redraw = TRUE;
			for(i = 0, r = bf->buf; *r != TEXT('\0'); r++){
				if(*r == TEXT('\n')){
					i++;
					if(i >= ln){
						break;
					}
				}
			}
			for(; *r == TEXT('\r') || *r == TEXT('\n'); r++);

			for(i = 0; i < bf->view_line - 1 && *(bf->line + i + 1) + bf->buf <= r; i++);
			j = bf->view_line;
			ReCountLine(bf, i);
			j -= bf->view_line;

			bf->cp = bf->cp - (r - bf->buf);
			bf->sp = bf->sp - (r - bf->buf);
			if(bf->cp < bf->buf) bf->cp = bf->buf;
			if(bf->sp < bf->buf) bf->sp = bf->buf;

			CopyMemory(bf->style, bf->style + (r - bf->buf), sizeof(char) * (bf->size - (r - bf->buf)));
			ZeroMemory(bf->style + (bf->size - (r - bf->buf)), sizeof(char) * (r - bf->buf));
			CopyMemory(bf->color, bf->color + (r - bf->buf), sizeof(char) * (bf->size - (r - bf->buf)));
			ZeroMemory(bf->color + (bf->size - (r - bf->buf)), sizeof(char) * (r - bf->buf));

			bf->len -= (r - bf->buf);
			r = TStrCpy(bf->buf, r);
			len = r - bf->buf;

			bf->logic_line = bf->LineLimit;
			
			//スクロール設定
			if(bf->sc_lock != 0){
				bf->pos -= j;
				if(bf->pos < 0) bf->pos = 0;
			}
		}else{
			bf->logic_line += ln;
		}
	}


	if(bf->Limit <= 0){
		// no limit
		if(len + len2 + 1 > bf->size){
			bf->size = len + len2 + RESERVE_SZIE;

			cp = mem_realloc(bf->color, bf->size);
			if(cp == NULL) return FALSE;
			ZeroMemory(cp + len + 1, sizeof(char) * (len2 + RESERVE_SZIE - 1));
			bf->color = (char *)cp;
			
			cp = mem_realloc(bf->style, bf->size);
			if(cp == NULL) return FALSE;
			ZeroMemory(cp + len + 1, sizeof(char) * (len2 + RESERVE_SZIE - 1));
			bf->style = (char *)cp;

			s = mem_realloc(bf->buf, sizeof(TCHAR) * bf->size);
			if(s == NULL) return FALSE;
			bf->cp = s + (bf->cp - bf->buf);
			bf->sp = s + (bf->sp - bf->buf);
			bf->buf = s;
			r = bf->buf + len;
		}else{
			r = bf->buf + len;
		}

	}else if(bf->Limit > 0 && len + len2 >= bf->Limit){
		//文字数制限
		redraw = TRUE;
		if(len2 >= bf->Limit){
			scroll = TRUE;
			p = str + len2 - bf->Limit + 1;
			for(; *p != TEXT('\0') && *p != TEXT('\n'); p++);
			for(; *p == TEXT('\r') || *p == TEXT('\n'); p++);
			r = bf->buf;
			bf->len = 0;
			ZeroMemory(bf->style, sizeof(char) * (bf->Limit));
			ZeroMemory(bf->color, sizeof(char) * (bf->Limit));
			bf->view_line = 1;
			*bf->line = 0;
		}else{
			r = bf->buf + len - bf->Limit + len2 + 1;
			for(; *r != TEXT('\0') && *r != TEXT('\n'); r++);
			for(; *r == TEXT('\r') || *r == TEXT('\n'); r++);
			for(ln = 0; ln < bf->view_line - 1 && *(bf->line + ln + 1) + bf->buf <= r; ln++);

			j = bf->view_line;
			ReCountLine(bf, ln);
			j -= bf->view_line;

			bf->cp = bf->cp - (r - bf->buf);
			bf->sp = bf->sp - (r - bf->buf);
			if(bf->cp < bf->buf) bf->cp = bf->buf;
			if(bf->sp < bf->buf) bf->sp = bf->buf;

			CopyMemory(bf->style, bf->style + (r - bf->buf), sizeof(char) * (bf->Limit - (r - bf->buf)));
			ZeroMemory(bf->style + (bf->Limit - (r - bf->buf)), sizeof(char) * (r - bf->buf));
			CopyMemory(bf->color, bf->color + (r - bf->buf), sizeof(char) * (bf->Limit - (r - bf->buf)));
			ZeroMemory(bf->color + (bf->Limit - (r - bf->buf)), sizeof(char) * (r - bf->buf));

			bf->len -= (r - bf->buf);
			r = TStrCpy(bf->buf, r);

			//スクロール設定
			if(bf->sc_lock != 0){
				bf->pos -= j;
				if(bf->pos < 0) bf->pos = 0;
			}
		}
	}else{
		r = bf->buf + len;
	}

	//copy
	last_color = -1;
	reverse = FALSE;
	underline = FALSE;
	s = r;
	while(*p != TEXT('\0')){
		switch(*p)
		{
		case ORN_COLOR:
			//color
			p = SetColor(p + 1, bf->color + (r - bf->buf), bf->style + (r - bf->buf));
			if(*(bf->style + (r - bf->buf)) & STYLE_RETURNCOLOR){
				last_color = -1;
			}else{
				last_color = *(bf->color + (r - bf->buf));
			}
			continue;

		case ORN_BOLD:
			//bold
			*(bf->style + (r - bf->buf)) ^= STYLE_BOLD;
			p++;
			continue;

		case ORN_UNDERLINE:
			//underline
			underline = (underline == TRUE) ? FALSE : TRUE;
			*(bf->style + (r - bf->buf)) ^= STYLE_UNDERLINE;
			p++;
			continue;

		case ORN_REVERSE:
			//reverse
			reverse = (reverse == TRUE) ? FALSE : TRUE;
			*(bf->style + (r - bf->buf)) ^= STYLE_REVERSE;
			p++;
			continue;

		case ORN_ITALIC:
			//italic
			*(bf->style + (r - bf->buf)) ^= STYLE_ITALIC;
			p++;
			continue;
		}
		if(*p == TEXT('\r') || *p == TEXT('\n')){
			last_color = -1;
			reverse = FALSE;
			underline = FALSE;
		}
		if(IS_LEAD_TBYTE((TBYTE)*p) == TRUE && *(p + 1) != TEXT('\0')){
			*(r++) = *(p++);
		}
		*(r++) = *(p++);
	}
	*r = TEXT('\0');
	bf->len = bf->len + (r - s);

	scroll_line = bf->max;
	draw_line = bf->view_line;
	//行情報の設定
	if(CountLine(hWnd, bf, bf->buf + *(bf->line + bf->view_line - 1)) == FALSE){
		return FALSE;
	}
	scroll_line -= bf->max;
	draw_line -= bf->view_line;

	if(scroll == TRUE && bf->sp == bf->cp){
		bf->sp = bf->cp = bf->buf + bf->len;
		bf->cpx = 0;
	}
	if(caret == TRUE){
		if(bf->sc_lock == 0){
			GetClientRect(hWnd, &rect);
			if(bf->max > 0){
				//追加分をスクロール
				ScrollWindow(hWnd, 0, scroll_line * bf->FontHeight, NULL, &rect);
				rect.top = ((rect.bottom / bf->FontHeight) + draw_line) * bf->FontHeight - bf->FontHeight;
				InvalidateRect(hWnd, &rect, FALSE);
			}else{
				InvalidateRect(hWnd, NULL, FALSE);
			}
			bf->pos = bf->max;
			SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);
		}else{
			InvalidateRect(hWnd, NULL, FALSE);
		}
		UpdateWindow(hWnd);

	}else if(redraw == TRUE){
		InvalidateRect(hWnd, NULL, FALSE);
		UpdateWindow(hWnd);
	}
	return TRUE;
}

/*
 * DrawLine - 1行描画
 */
static void DrawLine(HWND hWnd, HDC mdc, VIEW_BUFFER *bf, int i, PUTINFO *pi)
{
	HFONT hFont;
	HFONT hTmpFont;
	RECT drect;
	SIZE sz;
	TCHAR *p, *s;
	int offset;
	int height;
	int csize;
	int cnt = 0;
	int j, tab;

	s = p = *(bf->line + i) + bf->buf;
	offset = bf->LeftMargin;
	height = (i - bf->pos) * bf->FontHeight + (bf->Spacing / 2);
	drect.top = (i - bf->pos) * bf->FontHeight;
	drect.bottom = drect.top + bf->FontHeight;

	if (*p == TEXT('-') && *(p + 1) == TEXT('-')) {
		RECT rect;
		HPEN hPen, retPen;

		GetClientRect(hWnd, &rect);
		hPen = CreatePen(PS_SOLID, 1, RGB(0xcc, 0xcc, 0xcc));
		retPen = (HPEN)SelectObject(mdc, hPen);
		MoveToEx(mdc , 0 , height + bf->FontHeight / 2 - (bf->Spacing / 2), NULL);
		LineTo(mdc , rect.right - 0, height + bf->FontHeight / 2 - (bf->Spacing / 2));
		SelectObject(mdc, retPen);
		DeleteObject(hPen);
		return;
	}
	for(j = 0; j < *(bf->linelen + i); j++, p++){
		if(*(bf->style + (p - bf->buf)) != 0){
			//change style
#ifdef UNICODE
			GetTextExtentPoint32(mdc, s, p - s, &sz);
			csize = sz.cx;
#else
			if(bf->Fixed == FALSE || pi->bold == TRUE){
				GetTextExtentPoint32(mdc, s, p - s, &sz);
				csize = sz.cx;
			}else{
				csize = (p - s) * bf->CharWidth;
			}
#endif
			drect.left = offset;
			drect.right = offset + csize;
			ExtTextOut(mdc, offset, height, ETO_OPAQUE, &drect, s, p - s, NULL);
			offset += csize;
			s = p;

			if(*(bf->style + (p - bf->buf)) & STYLE_BOLD){
				//bold
				pi->bold = (pi->bold == TRUE) ? FALSE : TRUE;
				hFont = CreateViewFont(mdc, bf, pi->bold, pi->underline, pi->italic);
				hTmpFont = SelectObject(mdc, hFont);
				DeleteObject(hTmpFont);
			}
			if(*(bf->style + (p - bf->buf)) & STYLE_UNDERLINE){
				//underline
				pi->underline = (pi->underline == TRUE) ? FALSE : TRUE;
				hFont = CreateViewFont(mdc, bf, pi->bold, pi->underline, pi->italic);
				hTmpFont = SelectObject(mdc, hFont);
				DeleteObject(hTmpFont);
			}
			if(*(bf->style + (p - bf->buf)) & STYLE_RETURNCOLOR){
				//return color
				pi->txtColor = bf->TextColor;
				pi->backColor = bf->BackClolor;
			}
			if(*(bf->style + (p - bf->buf)) & STYLE_ITALIC){
				//italic
				pi->italic = (pi->italic == TRUE) ? FALSE : TRUE;
				hFont = CreateViewFont(mdc, bf, pi->bold, pi->underline, pi->italic);
				hTmpFont = SelectObject(mdc, hFont);
				DeleteObject(hTmpFont);
			}
			if(*(bf->style + (p - bf->buf)) & STYLE_REVERSE){
				//reverse
				if(pi->reverse == TRUE){
					pi->reverse = FALSE;
					pi->txtColor = pi->rtxtColor;
					pi->backColor = pi->rbackColor;
				}else{
					pi->reverse = TRUE;
					pi->rtxtColor = pi->txtColor;
					pi->rbackColor = pi->backColor;
					pi->txtColor = bf->BackClolor;
					pi->backColor = bf->TextColor;
				}
			}
			if(*(bf->style + (p - bf->buf)) & STYLE_TEXTCOLOR){
				//change text color
				pi->txtColor = GetColor((char)LOCOLOR(*(bf->color + (p - bf->buf))), bf->TextColor);
			}
			if(*(bf->style + (p - bf->buf)) & STYLE_BACKCOLOR){
				//change back color
				pi->backColor = GetColor((char)HICOLOR(*(bf->color + (p - bf->buf))), bf->BackClolor);
			}
			if(pi->sel == FALSE){
				SetTextColor(mdc, pi->txtColor);
				SetBkColor(mdc, pi->backColor);
			}
		}
		if((p >= bf->sp && p < bf->cp) || (p >= bf->cp && p < bf->sp)){
			if(pi->sel == FALSE){
				//select
#ifdef UNICODE
				GetTextExtentPoint32(mdc, s, p - s, &sz);
				csize = sz.cx;
#else
				if(bf->Fixed == FALSE || pi->bold == TRUE){
					GetTextExtentPoint32(mdc, s, p - s, &sz);
					csize = sz.cx;
				}else{
					csize = (p - s) * bf->CharWidth;
				}
#endif
				drect.left = offset;
				drect.right = offset + csize;
				ExtTextOut(mdc, offset, height, ETO_OPAQUE, &drect, s, p - s, NULL);
				offset += csize;
				s = p;

				SetTextColor(mdc, GetSysColor((GetFocus() != hWnd) ? COLOR_BTNTEXT : COLOR_HIGHLIGHTTEXT));
				SetBkColor(mdc, GetSysColor((GetFocus() != hWnd) ? COLOR_BTNFACE : COLOR_HIGHLIGHT));
				pi->sel = TRUE;
			}
		}else if(pi->sel == TRUE){
			//unselect
#ifdef UNICODE
			GetTextExtentPoint32(mdc, s, p - s, &sz);
			csize = sz.cx;
#else
			if (bf->Fixed == FALSE || pi->bold == TRUE) {
				GetTextExtentPoint32(mdc, s, p - s, &sz);
				csize = sz.cx;
			}
			else {
				csize = (p - s) * bf->CharWidth;
			}
#endif
			drect.left = offset;
			drect.right = offset + csize;
			ExtTextOut(mdc, offset, height, ETO_OPAQUE, &drect, s, p - s, NULL);
			offset += csize;
			s = p;

			SetTextColor(mdc, pi->txtColor);
			SetBkColor(mdc, pi->backColor);
			pi->sel = FALSE;
		}
		if(*p == TEXT('\t')){
			RECT trect;
			HBRUSH hBrush;

#ifdef UNICODE
			GetTextExtentPoint32(mdc, s, p - s, &sz);
			csize = sz.cx;
#else
			if(bf->Fixed == FALSE || pi->bold == TRUE){
				GetTextExtentPoint32(mdc, s, p - s, &sz);
				csize = sz.cx;
			}else{
				csize = (p - s) * bf->CharWidth;
			}
#endif
			drect.left = offset;
			drect.right = offset + csize;
			ExtTextOut(mdc, offset, height, ETO_OPAQUE, &drect, s, p - s, NULL);
			offset += csize;

			//tab
			tab = bf->TabStop - (cnt % bf->TabStop);

			trect.left = offset;
			trect.right = trect.left + tab * bf->CharWidth;
			trect.top = height - (bf->Spacing / 2);
			trect.bottom = trect.top + bf->FontHeight;

			hBrush = CreateSolidBrush(GetBkColor(mdc));
			FillRect(mdc, &trect, hBrush);
			DeleteObject(hBrush);

			offset += tab * bf->CharWidth;
			cnt += tab;
			s = p + 1;
			continue;
		}
		if(pi->bold == TRUE){
			GetTextExtentPoint32(mdc, s, p - s, &sz);
			csize = sz.cx;
			drect.left = offset;
			drect.right = offset + csize;
			ExtTextOut(mdc, offset, height, ETO_OPAQUE, &drect, s, p - s, NULL);
			offset += csize;
			s = p;
		}
		if(IS_LEAD_TBYTE((TBYTE)*p) == TRUE && *(p + 1) != TEXT('\0')){
			p++;
			j++;
			cnt++;
		}
		cnt++;
	}
	drect.left = offset;
#ifdef UNICODE
	GetTextExtentPoint32(mdc, s, p - s, &sz);
	drect.right = offset + sz.cx;
#else
	if(bf->Fixed == FALSE || pi->bold == TRUE){
		GetTextExtentPoint32(mdc, s, p - s, &sz);
		drect.right = offset + sz.cx;
	}else{
		drect.right = offset + (p - s) * bf->CharWidth;
	}
#endif
	ExtTextOut(mdc, offset, height, ETO_OPAQUE, &drect, s, p - s, NULL);

	if(*p == TEXT('\r') || *p == TEXT('\n')){
		//初期化
		pi->txtColor = bf->TextColor;
		pi->backColor = bf->BackClolor;
		SetTextColor(mdc, pi->txtColor);
		SetBkColor(mdc, pi->backColor);
		pi->sel = FALSE;
		pi->bold = FALSE;
		pi->underline = FALSE;
		pi->reverse = FALSE;
		pi->italic = FALSE;

		hFont = CreateViewFont(mdc, bf, pi->bold, pi->underline, pi->italic);
		hTmpFont = SelectObject(mdc, hFont);
		DeleteObject(hTmpFont);
	}
}

/*
 * Char2Caret - 文字位置からキャレットの位置取得
 */
static int Char2Caret(HWND hWnd, HDC mdc, VIEW_BUFFER *bf, int i, PUTINFO *pi, TCHAR *cp)
{
	HFONT hFont;
	HFONT hTmpFont;
	SIZE sz;
	TCHAR *p, *s;
	int len;
	int offset;
	int cnt = 0;
	int j;
	BOOL bold = pi->bold;

	s = p = *(bf->line + i) + bf->buf;
	len = *(bf->linelen + i);
	offset = bf->LeftMargin;
	for(j = 0; j < len; j++, p++){
		if(*(bf->style + (p - bf->buf)) & STYLE_BOLD){
			//bold
#ifdef UNICODE
			GetTextExtentPoint32(mdc, s, p - s, &sz);
			offset += sz.cx;
#else
			if(bf->Fixed == FALSE || bold == TRUE){
				GetTextExtentPoint32(mdc, s, p - s, &sz);
				offset += sz.cx;
			}else{
				offset += (p - s) * bf->CharWidth;
			}
#endif
			s = p;

			bold = (bold == TRUE) ? FALSE : TRUE;
			hFont = CreateViewFont(mdc, bf, bold, FALSE, FALSE);
			hTmpFont = SelectObject(mdc, hFont);
			DeleteObject(hTmpFont);
		}
		if(cp != NULL && p >= cp){
			break;
		}
		if(*p == TEXT('\t')){
			//tab
#ifdef UNICODE
			GetTextExtentPoint32(mdc, s, p - s, &sz);
			offset += sz.cx;
#else
			if(bf->Fixed == FALSE || bold == TRUE){
				GetTextExtentPoint32(mdc, s, p - s, &sz);
				offset += sz.cx;
			}else{
				offset += (p - s) * bf->CharWidth;
			}
#endif

			offset += (bf->TabStop - (cnt % bf->TabStop)) * bf->CharWidth;
			cnt += bf->TabStop - (cnt % bf->TabStop);
			s = p + 1;
			continue;
		}
		if(bold == TRUE){
			GetTextExtentPoint32(mdc, s, p - s, &sz);
			offset += sz.cx;
			s = p;
		}
		if(IS_LEAD_TBYTE((TBYTE)*p) == TRUE && *(p + 1) != TEXT('\0')){
			p++;
			j++;
			cnt++;
		}
		cnt++;
	}
#ifdef UNICODE
	GetTextExtentPoint32(mdc, s, p - s, &sz);
	offset += sz.cx;
#else
	if(bf->Fixed == FALSE || bold == TRUE){
		GetTextExtentPoint32(mdc, s, p - s, &sz);
		offset += sz.cx;
	}else{
		offset += (p - s) * bf->CharWidth;
	}
#endif
	hFont = CreateViewFont(mdc, bf, pi->bold, pi->underline, pi->italic);
	hTmpFont = SelectObject(mdc, hFont);
	DeleteObject(hTmpFont);
	return offset;
}

/*
 * Point2Caret - 座標からキャレットの位置取得
 */
static TCHAR *Point2Caret(HWND hWnd, VIEW_BUFFER *bf, int x, int y)
{
	HFONT hFont;
	HFONT hRetFont;
	HFONT hTmpFont;
	RECT rect;
	SIZE sz;
	TCHAR *p, *r = bf->buf;
	int cnt;
	int offset;
	int clen;
	int ci;
	int i, j;
	BOOL bold = FALSE;

	hFont = CreateViewFont(bf->mdc, bf, FALSE, FALSE, FALSE);
	hRetFont = SelectObject(bf->mdc, hFont);

	GetClientRect(hWnd, &rect);
	ci = i = bf->pos + (y / bf->FontHeight);
	if(i < 0){
		return bf->buf;
	}else if(i >= bf->view_line){
		return bf->buf + bf->len;
	}
	for(; i > 0; i--){
		//論理行の先頭に移動
		p = *(bf->line + i) + bf->buf - 1;
		if(*p == '\r' || *p == '\n'){
			break;
		}
	}

	for(; i <= ci; i++){
		cnt = j = 0;
		offset = bf->LeftMargin - (bf->CharWidth / 2);
		for(r = p = *(bf->line + i) + bf->buf; j <= *(bf->linelen + i); p++, j++){
			if(*(bf->style + (p - bf->buf)) & STYLE_BOLD){
				//bold
				bold = (bold == TRUE) ? FALSE : TRUE;
				hFont = CreateViewFont(bf->mdc, bf, bold, FALSE, FALSE);
				hTmpFont = SelectObject(bf->mdc, hFont);
				DeleteObject(hTmpFont);
			}
			if(i == ci && offset > x){
				break;
			}
			r = p;
			if(*p == TEXT('\t')){
				offset += (bf->TabStop - (cnt % bf->TabStop)) * bf->CharWidth;
				cnt += bf->TabStop - (cnt % bf->TabStop);
				continue;
			}
			clen = (IS_LEAD_TBYTE((TBYTE)*p) == TRUE && *(p + 1) != TEXT('\0')) ? 1 : 0;
			GetTextExtentPoint32(bf->mdc, p, clen + 1, &sz);
			offset += sz.cx;
			p += clen;
			j += clen;
			cnt += clen + 1;
		}
	}
	hFont = SelectObject(bf->mdc, hRetFont);
	DeleteObject(hFont);
	return r;
}

/*
 * GetCaretToken - キャレット位置のトークンを取得
 */
static void GetCaretToken(VIEW_BUFFER *bf, TCHAR **st, TCHAR **en)
{
	TCHAR *p, *r;
	int i;

	for(i = 0; i < bf->view_line - 1 && *(bf->line + i + 1) + bf->buf <= bf->cp; i++);
	for(; i > 0; i--){
		//論理行の先頭に移動
		p = *(bf->line + i) + bf->buf - 1;
		if(*p == '\r' || *p == '\n'){
			break;
		}
	}

	p = *(bf->line + i) + bf->buf;
	while(*p != TEXT('\0') && *p != TEXT('\r') && *p != TEXT('\n')){
#ifdef UNICODE
		if(IS_LEAD_TBYTE((TBYTE)*p) == TRUE || WideCharToMultiByte(CP_ACP, 0, p, 1, NULL, 0, NULL, NULL) != 1){
			r = p;
			while(IS_LEAD_TBYTE((TBYTE)*p) == TRUE || WideCharToMultiByte(CP_ACP, 0, p, 1, NULL, 0, NULL, NULL) != 1){
				if (IS_LEAD_TBYTE((TBYTE)*p) == TRUE) {
					p++;
				}
				p++;
			}
#else
		if(IS_LEAD_TBYTE((TBYTE)*p) == TRUE){
			r = p;
			while(IS_LEAD_TBYTE((TBYTE)*p) == TRUE && *(p + 1) != TEXT('\0')){
				p += 2;
			}
#endif
		}else if((*p >= TEXT('A') && *p <= TEXT('Z')) ||
			(*p >= TEXT('a') && *p <= TEXT('z')) ||
			(*p >= TEXT('0') && *p <= TEXT('9')) ||
			*p == TEXT('_')){
			r = p;
			while((*p >= TEXT('A') && *p <= TEXT('Z')) ||
				(*p >= TEXT('a') && *p <= TEXT('z')) ||
				(*p >= TEXT('0') && *p <= TEXT('9')) ||
				*p == TEXT('_')){
				p++;
			}

		}else{
			r = p++;
		}
		if(p > bf->cp){
			*st = r;
			*en = p;
			break;
		}
	}
}

/*
 * RefreshLine - 指定範囲文字列のある行を再描画対象にする
 */
static void RefreshLine(HWND hWnd, VIEW_BUFFER *bf, TCHAR *p, TCHAR *r)
{
	RECT rect;
	int i, j, k;

	for(i = 0; i < bf->view_line - 1 && *(bf->line + i + 1) + bf->buf <= p; i++);
	if(p == r){
		j = i;
	}else{
		for(j = 0; j < bf->view_line - 1 && *(bf->line + j + 1) + bf->buf <= r; j++);
	}
	if(i > j){
		k = i; i = j; j = k;
	}
	i -= bf->pos;
	j -= bf->pos;

	GetClientRect(hWnd, &rect);
	rect.top = i * bf->FontHeight;
	rect.bottom = j * bf->FontHeight + bf->FontHeight;
	InvalidateRect(hWnd, &rect, FALSE);
}

/*
 * SetInputBuf - 入力バッファの設定
 */
static void SetInputBuf(HWND hWnd, VIEW_BUFFER *bf, TCHAR *input_str)
{
	TCHAR *p, *r;
	int len = lstrlen(bf->input_string);

	for (p = input_str, r = bf->input_string + len; *p != TEXT('\0'); p++, r++) {
		if (*p == TEXT('\r') || *p == TEXT('\n')) {
			*r = TEXT('\0');
			AddString(hWnd, bf, bf->input_string);
			AddString(hWnd, bf, TEXT("\r\n"));
			bf->input_mode = FALSE;
			for (; *p == TEXT('\r') || *p == TEXT('\n'); p++);
			lstrcpy(bf->input_buf, p);
			return;
		}
		if (++len > INPUT_SIZE) {
			break;
		}
		*r = *p;
	}
	*r = TEXT('\0');
	AddString(hWnd, bf, bf->input_string);
}

/*
 * ConsoleProc - ウィンドウプロシージャ
 */
static LRESULT CALLBACK ConsoleProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	VIEW_BUFFER *bf;
	HDC hDC;
	HFONT hFont;
	HFONT hRetFont;
	RECT rect;
	POINT pt;
	TCHAR *p, *r;
	TCHAR *oldcp, *oldsp;
	TCHAR in[3];
	char *cp;
	int len;
	int i, j;
#ifdef OP_XP_STYLE
	static FARPROC _OpenThemeData;
	static FARPROC _CloseThemeData;
	static FARPROC _DrawThemeBackground;
#endif	// OP_XP_STYLE
	HIMC hIMC;
	COMPOSITIONFORM cf;

	switch(message)
	{
	case WM_CREATE:
		bf = mem_calloc(sizeof(VIEW_BUFFER));
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

		bf->Limit = 0;
		bf->LineLimit = 0;
		bf->TabStop = 8;
		bf->LeftMargin = 1;
		bf->RightMargin = 1;
		bf->Spacing = 2;

		bf->size = (bf->Limit <= 0) ? RESERVE_SZIE : bf->Limit;
		bf->linesize = RESERVE_LINE;
		bf->view_line = 1;
		bf->logic_line = 1;

		//buf init
		bf->buf = mem_alloc(sizeof(TCHAR) * bf->size);
		if(bf->buf == NULL) return -1;
		*bf->buf = TEXT('\0');
		bf->len = 0;
		bf->color = mem_alloc(sizeof(char) * bf->size);
		if(bf->color == NULL) return -1;
		ZeroMemory(bf->color, sizeof(char) * bf->size);
		bf->style = mem_alloc(sizeof(char) * bf->size);
		if(bf->style == NULL) return -1;
		ZeroMemory(bf->style, sizeof(char) * bf->size);

		//line init
		bf->line = mem_alloc(sizeof(int) * bf->linesize);
		if(bf->line == NULL) return -1;
		ZeroMemory(bf->line, sizeof(int) * bf->linesize);
		bf->linelen = mem_alloc(sizeof(int) * bf->linesize);
		if(bf->linelen == NULL) return -1;
		ZeroMemory(bf->linelen, sizeof(int) * bf->linesize);

		bf->sp = bf->cp = bf->buf;

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
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
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

			mem_free(&bf->buf);
			mem_free(&bf->style);
			mem_free(&bf->color);
			mem_free(&bf->line);
			mem_free(&bf->linelen);
			mem_free(&bf);
		}
		if(GetFocus() == hWnd){
			DestroyCaret();
		}
		return DefWindowProc(hWnd, message, wParam, lParam);

	case WM_SETFOCUS:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		CreateCaret(hWnd, 0, 0, bf->FontHeight);
		InvalidateRect(hWnd, NULL, FALSE);

		ShowCaret(hWnd);
		break;

	case WM_KILLFOCUS:
		HideCaret(hWnd);
		DestroyCaret();

		InvalidateRect(hWnd, NULL, FALSE);
		UpdateWindow(hWnd);
		break;

	case WM_GETDLGCODE:
		return DLGC_WANTALLKEYS;

	case WM_VSCROLL:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		GetClientRect(hWnd, &rect);
		i = bf->pos;
		switch((int)LOWORD(wParam))
		{
		case SB_TOP:
			bf->pos = 0;
			break;

		case SB_BOTTOM:
			bf->pos = bf->max;
			break;

		case SB_LINEDOWN:
			bf->pos = (bf->pos < bf->max) ? bf->pos + 1 : bf->max;
			break;

		case SB_LINEUP:
			bf->pos = (bf->pos > 0) ? bf->pos - 1 : 0;
			break;

		case SB_PAGEDOWN:
			bf->pos = (bf->pos + (rect.bottom / bf->FontHeight) < bf->max) ?
				bf->pos + (rect.bottom / bf->FontHeight) : bf->max;
			break;

		case SB_PAGEUP:
			bf->pos = (bf->pos - (rect.bottom / bf->FontHeight) > 0) ?
				bf->pos - (rect.bottom / bf->FontHeight) : 0;
			break;

		case SB_THUMBPOSITION:
		case SB_THUMBTRACK:
			{
				SCROLLINFO sci;

				ZeroMemory(&sci, sizeof(SCROLLINFO));
				sci.cbSize = sizeof(SCROLLINFO);
				sci.fMask = SIF_TRACKPOS;
				GetScrollInfo(hWnd, SB_VERT, &sci);
				if(sci.nTrackPos > bf->max){
					bf->pos = bf->max;
				}else if(sci.nTrackPos < 0){
					bf->pos = 0;
				}else{
					bf->pos = sci.nTrackPos;
				}
			}
			break;
		}

		SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);

		ScrollWindowEx(hWnd, 0, (i - bf->pos) * bf->FontHeight, NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
		UpdateWindow(hWnd);
		break;

	case WM_KEYDOWN:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		if (bf->input_mode == TRUE) {
			switch (wParam) {
			case TEXT('V'):
				if (GetKeyState(VK_CONTROL) < 0) {
					SendMessage(hWnd, WM_PASTE, 0, 0);
				}
				break;

			case VK_TAB:
				if (GetKeyState(VK_CONTROL) < 0) {
					SetFocus(GetNextWindow(GetWindow(hWnd, GW_HWNDFIRST), GW_HWNDNEXT));
				}
				break;
			}
			break;
		}

		GetClientRect(hWnd, &rect);
		oldcp = bf->cp;
		oldsp = bf->sp;
		for(i = 0; i < bf->view_line - 1 && *(bf->line + i + 1) + bf->buf <= bf->cp; i++);
		switch(wParam)
		{
		case TEXT('A'):
			if(GetKeyState(VK_CONTROL) < 0){
				//全て選択
				bf->sp = bf->buf;
				bf->cp = bf->buf + bf->len;
				bf->pos = bf->max;
				SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);
				InvalidateRect(hWnd, NULL, FALSE);
				UpdateWindow(hWnd);
			}
			break;

		case TEXT('C'):
			if(GetKeyState(VK_CONTROL) < 0){
				//コピー
				SendMessage(hWnd, WM_COPY, 0, 0);
			}
			break;

		case VK_TAB:
			SetFocus(GetNextWindow(GetWindow(hWnd, GW_HWNDFIRST), GW_HWNDNEXT));
			break;

		case VK_HOME:
			if(GetKeyState(VK_CONTROL) < 0){
				//全体の先頭
				bf->cp = bf->buf;
				if(GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
				bf->pos = 0;
				SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);
				InvalidateRect(hWnd, NULL, FALSE);
			}else{
				//論理行頭
				for(; *bf->cp == TEXT('\r') || *bf->cp == TEXT('\n'); bf->cp--);
				for(; bf->cp > bf->buf && *bf->cp != TEXT('\r') && *bf->cp != TEXT('\n'); bf->cp--);
				if(bf->cp > bf->buf) bf->cp++;
				if(GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
				InvalidateRect(hWnd, NULL, FALSE);
			}
			UpdateWindow(hWnd);
			break;

		case VK_END:
			if(GetKeyState(VK_CONTROL) < 0){
				//全体の末尾
				bf->cp = (bf->buf + bf->len);
				if(GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
				bf->pos = bf->max;
				SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);
				InvalidateRect(hWnd, NULL, FALSE);
			}else{
				//論理行末
				for(; *bf->cp != TEXT('\0') && *bf->cp != TEXT('\r') && *bf->cp != TEXT('\n'); bf->cp++);
				if(GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
				InvalidateRect(hWnd, NULL, FALSE);
			}
			UpdateWindow(hWnd);
			break;

		case VK_PRIOR:
			//Page UP
			if(GetCaretPos(&pt) == FALSE) break;
			bf->cp = Point2Caret(hWnd, bf, pt.x, pt.y - rect.bottom);
			if(GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
			SendMessage(hWnd, WM_VSCROLL, SB_PAGEUP, 0);
			InvalidateRect(hWnd, NULL, FALSE);
			UpdateWindow(hWnd);
			break;

		case VK_NEXT:
			//Page Down
			if(GetCaretPos(&pt) == FALSE) break;
			bf->cp = Point2Caret(hWnd, bf, pt.x, pt.y + rect.bottom);
			if(GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
			SendMessage(hWnd, WM_VSCROLL, SB_PAGEDOWN, 0);
			InvalidateRect(hWnd, NULL, FALSE);
			UpdateWindow(hWnd);
			break;

		case VK_UP:
			i--;
			if(i < 0){
				if(GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
				break;
			}
			if(GetCaretPos(&pt) == FALSE) break;
			if(bf->cpx == 0){
				//キャレット位置の退避
				bf->cpx = pt.x;
			}
			bf->cp = Point2Caret(hWnd, bf, bf->cpx, pt.y - bf->FontHeight);
			if(GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
			break;

		case VK_DOWN:
			i++;
			if(i > bf->view_line - 1){
				if(GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
				break;
			}
			if(GetCaretPos(&pt) == FALSE) break;
			if(bf->cpx == 0){
				//キャレット位置の退避
				bf->cpx = pt.x;
			}
			bf->cp = Point2Caret(hWnd, bf, bf->cpx, pt.y + bf->FontHeight);
			if(GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
			break;

		case VK_LEFT:
			bf->cpx = 0;
			if(bf->cp != bf->sp && GetKeyState(VK_SHIFT) >= 0){
				//選択解除
				p = (bf->cp > bf->sp) ? bf->sp : bf->cp;
				bf->cp = bf->sp = p;
				for(i = 0; i < bf->view_line - 1 && *(bf->line + i + 1) + bf->buf <= bf->cp; i++);
				break;
			}
			p = *(bf->line + i) + bf->buf;
			if(bf->cp == p){
				i--;
				if(i < 0){
					if(GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
					break;
				}
			}
			for(j = 0, r = p = *(bf->line + i) + bf->buf; j <= *(bf->linelen + i); p++, j++){
				if(p >= bf->cp) break;
				r = p;
				if(IS_LEAD_TBYTE((TBYTE)*p) == TRUE && *(p + 1) != TEXT('\0')){
					p++;
					j++;
				}
			}
			bf->cp = r;
			if(GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
			break;

		case VK_RIGHT:
			bf->cpx = 0;
			if(bf->cp != bf->sp && GetKeyState(VK_SHIFT) >= 0){
				//選択解除
				p = (bf->cp < bf->sp) ? bf->sp : bf->cp;
				bf->cp = bf->sp = p;
				for(i = 0; i < bf->view_line - 1 && *(bf->line + i + 1) + bf->buf <= bf->cp; i++);
				break;
			}
			if(*bf->cp == TEXT('\0')){
				if(GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
				break;
			}
			if(IS_LEAD_TBYTE((TBYTE)*bf->cp) == TRUE && *(bf->cp + 1) != TEXT('\0')){
				bf->cp += 2;
			}else{
				bf->cp++;
			}
			if(bf->cp - (*(bf->line + i) + bf->buf) >= *(bf->linelen + i) &&
				*bf->cp != TEXT('\0') && *bf->cp != TEXT('\r')){
				i++;
				if(i >= bf->view_line){
					bf->cp = bf->buf + bf->len;
					break;
				}
				bf->cp = *(bf->line + i) + bf->buf;
			}
			if(GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
			break;
		}
		switch(wParam)
		{
		case VK_UP:
		case VK_DOWN:
		case VK_LEFT:
		case VK_RIGHT:
			if(i < bf->pos){
				if(i >= 0){
					ScrollWindowEx(hWnd, 0,
						(bf->pos - i) * bf->FontHeight,
						NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
				}
				bf->pos = i;
				if(bf->pos < 0) bf->pos = 0;
				SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);

			}else if(i >= bf->pos + (rect.bottom / bf->FontHeight)){
				if(i <= bf->view_line - 1){
					ScrollWindowEx(hWnd, 0,
						(bf->pos - (i - (rect.bottom / bf->FontHeight) + 1)) * bf->FontHeight,
						NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
				}
				bf->pos = i - (rect.bottom / bf->FontHeight) + 1;
				if(bf->pos > bf->max) bf->pos = bf->max;
				SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);
			}
			if(oldsp != bf->sp){
				RefreshLine(hWnd, bf, oldcp, oldsp);
			}
			RefreshLine(hWnd, bf, oldcp, bf->cp);
			UpdateWindow(hWnd);
			break;
		}
		break;

	case WM_CHAR:
		if ((bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->input_mode == FALSE) {
			break;
		}
		if (GetKeyState(VK_CONTROL) < 0) {
			break;
		}
		switch (wParam) {
		case VK_RETURN:
			// 改行
			AddString(hWnd, bf, TEXT("\r\n"));
			bf->input_mode = FALSE;
			break;

		case VK_BACK:
			if (lstrlen(bf->input_string) > 0 && bf->len > 0) {
				*(bf->input_string + lstrlen(bf->input_string) - 1) = TEXT('\0');
				bf->len--;
				*(bf->buf + bf->len) = TEXT('\0');
				bf->view_line = 1;
				CountLine(hWnd, bf, bf->buf);
				InvalidateRect(hWnd, NULL, FALSE);
				UpdateWindow(hWnd);
			}
			break;

		case VK_ESCAPE:
			bf->input_mode = FALSE;
			break;

		default:
			in[0] = (TCHAR)wParam;
			in[1] = TEXT('\0');
			AddString(hWnd, bf, in);
			if (lstrlen(bf->input_string) + 1 <= INPUT_SIZE) {
				lstrcat(bf->input_string, in);
			}
			break;
		}
		break;

	case WM_IME_CHAR:
		if ((bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->input_mode == FALSE) {
			break;
		}
		if (GetKeyState(VK_CONTROL) < 0) {
			break;
		}
#ifdef UNICODE
		in[0] = (TCHAR)wParam;
		in[1] = TEXT('\0');
#else
		in[0] = (TCHAR)wParam >> 8;
		in[1] = (TCHAR)wParam & 0xFF;
		in[2] = TEXT('\0');
#endif
		AddString(hWnd, bf, in);
		if (lstrlen(bf->input_string) + lstrlen(in) <= INPUT_SIZE) {
			lstrcat(bf->input_string, in);
		}
		break;

	case WM_IME_STARTCOMPOSITION:
		if ((bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) != NULL) {
			bf->sp = bf->cp = (bf->buf + bf->len);
			bf->pos = bf->max;
			SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);
			InvalidateRect(hWnd, NULL, FALSE);
			UpdateWindow(hWnd);

			hIMC = ImmGetContext(hWnd);
			ImmSetCompositionFont(hIMC, &bf->lf);
			// 位置の設定
			GetCaretPos(&pt);
			cf.dwStyle = CFS_POINT | CFS_RECT;
			cf.ptCurrentPos.x = pt.x;
			cf.ptCurrentPos.y = pt.y + (bf->Spacing / 2);
			GetClientRect(hWnd, &cf.rcArea);
			ImmSetCompositionWindow(hIMC, &cf);
			ImmReleaseContext(hWnd, hIMC);

			HideCaret(hWnd);
		}
		return DefWindowProc(hWnd, message, wParam, lParam);

	case WM_IME_COMPOSITION:
		if ((bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) != NULL &&
			bf->input_mode == TRUE && (lParam & GCS_RESULTSTR)) {
			TCHAR *buf;

			// 確定文字列をバッファに追加
			hIMC = ImmGetContext(hWnd);
			len = ImmGetCompositionString(hIMC, GCS_RESULTSTR, NULL, 0);
			buf = mem_calloc(len + sizeof(TCHAR));
			if (buf != NULL) {
				ImmGetCompositionString(hIMC, GCS_RESULTSTR, buf, len);
				AddString(hWnd, bf, buf);
				if (lstrlen(bf->input_string) + lstrlen(buf) <= INPUT_SIZE) {
					lstrcat(bf->input_string, buf);
				}
				UpdateWindow(hWnd);
				mem_free(&buf);
			}
			// 位置の設定
			GetCaretPos(&pt);
			cf.dwStyle = CFS_POINT | CFS_RECT;
			cf.ptCurrentPos.x = pt.x;
			cf.ptCurrentPos.y = pt.y + (bf->Spacing / 2);
			GetClientRect(hWnd, &cf.rcArea);
			ImmSetCompositionWindow(hIMC, &cf);
			ImmReleaseContext(hWnd, hIMC);
			break;
		}
		return DefWindowProc(hWnd, message, wParam, lParam);

	case WM_IME_ENDCOMPOSITION:
		ShowCaret(hWnd);
		return DefWindowProc(hWnd, message, wParam, lParam);

	case WM_LBUTTONDOWN:
		SetFocus(hWnd);
	case WM_MOUSEMOVE:
	case WM_LBUTTONUP:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		if (bf->input_mode == TRUE) {
			break;
		}
		bf->cpx = 0;
		oldcp = bf->cp;
		oldsp = bf->sp;
		SetCursor(LoadCursor(0, IDC_IBEAM));
		if(message == WM_MOUSEMOVE){
			if(!(wParam & MK_LBUTTON) || bf->mousedown == FALSE) break;
		}else if(message == WM_LBUTTONDOWN){
			SetCapture(hWnd);
			bf->mousedown = TRUE;
		}else if(message == WM_LBUTTONUP){
			if(bf->mousedown == FALSE) break;
			ReleaseCapture();
			bf->mousedown = FALSE;
		}

		i = bf->pos + ((short)HIWORD(lParam) / bf->FontHeight);
		bf->cp = Point2Caret(hWnd, bf, (short)LOWORD(lParam), (short)HIWORD(lParam));

		GetClientRect(hWnd, &rect);
		if(i < bf->pos){
			if(i < 0) i = 0;
			ScrollWindowEx(hWnd, 0,
				(bf->pos - i) * bf->FontHeight,
				NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
			bf->pos = i;
			SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);

		}else if(i >= bf->pos + (rect.bottom / bf->FontHeight) && bf->max != 0){
			if(i >= bf->view_line - 1) i = bf->view_line - 1;
			ScrollWindowEx(hWnd, 0,
				(bf->pos - (i - (rect.bottom / bf->FontHeight) + 1)) * bf->FontHeight,
				NULL, &rect, NULL, NULL, SW_INVALIDATE | SW_ERASE);
			bf->pos = i - (rect.bottom / bf->FontHeight) + 1;
			bf->pos = (bf->pos > 0 && bf->pos < bf->max) ? bf->pos : bf->max;
			SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);
		}
		if(message == WM_LBUTTONDOWN && GetKeyState(VK_SHIFT) >= 0) bf->sp = bf->cp;
		if(oldsp != bf->sp){
			RefreshLine(hWnd, bf, oldcp, oldsp);
		}
		RefreshLine(hWnd, bf, oldcp, bf->cp);
		UpdateWindow(hWnd);
		break;

	case WM_LBUTTONDBLCLK:
		if(!(wParam & MK_LBUTTON)) break;
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL || bf->input_mode == TRUE) break;
		bf->cpx = 0;
		oldcp = bf->cp;
		oldsp = bf->sp;
		bf->sp = bf->cp;
		p = Point2Caret(hWnd, bf, (short)LOWORD(lParam), (short)HIWORD(lParam));
		//select token
		GetCaretToken(bf, &bf->sp, &bf->cp);
		RefreshLine(hWnd, bf, oldcp, oldsp);
		RefreshLine(hWnd, bf, bf->cp, bf->sp);
		UpdateWindow(hWnd);
		break;

	case WM_MOUSEWHEEL:
		for(i = 0; i < 3; i++){
			SendMessage(hWnd, WM_VSCROLL, ((short)HIWORD(wParam) > 0) ? SB_LINEUP : SB_LINEDOWN, 0);
		}
		break;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			PUTINFO pi;

			bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
			if(bf == NULL) break;

			hDC = BeginPaint(hWnd, &ps);
			GetClientRect(hWnd, &rect);

			hFont = CreateViewFont(bf->mdc, bf, FALSE, FALSE, FALSE);
			hRetFont = SelectObject(bf->mdc, hFont);

			FillRect(bf->mdc, &rect, bf->hBkBrush);

			//caret pos
			j = -1;
			if(GetFocus() == hWnd){
				for(j = 0; j < bf->view_line - 1 && *(bf->line + j + 1) + bf->buf <= bf->cp; j++);
			}

			pi.txtColor = bf->TextColor;
			pi.backColor = bf->BackClolor;
			pi.sel = FALSE;
			pi.bold = FALSE;
			pi.underline = FALSE;
			pi.reverse = FALSE;
			pi.italic = FALSE;

			SetTextColor(bf->mdc, pi.txtColor);
			SetBkColor(bf->mdc, pi.backColor);

			i = bf->pos + (ps.rcPaint.top / bf->FontHeight);
			if(i >= bf->view_line) i = bf->view_line - 1;
			for(; i > 0; i--){
				//論理行の先頭に移動
				p = *(bf->line + i) + bf->buf - 1;
				if(*p == '\r' || *p == '\n'){
					break;
				}
			}
			for(; i < bf->view_line && i < bf->pos + (ps.rcPaint.bottom / bf->FontHeight) + 1; i++){
				if(i == j){
					//set caret
					len = Char2Caret(hWnd, bf->mdc, bf, j, &pi, bf->cp);
					SetCaretPos(len, (j - bf->pos) * bf->FontHeight);
					j = -1;
				}
				//draw line
				DrawLine(hWnd, bf->mdc, bf, i, &pi);
			}
			if(j != -1 && GetFocus() == hWnd){
				len = Char2Caret(hWnd, bf->mdc, bf, j, &pi, bf->cp);
				SetCaretPos(len, (j - bf->pos) * bf->FontHeight);
			}

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

			if ((bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->hTheme == NULL) {
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
		if ((bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->hModThemes == NULL) {
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
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;

		i = (bf->max == 0 || bf->pos == bf->max) ? 1 : 0;
		GetClientRect(hWnd, &rect);

		if(bf->width != rect.right){
			bf->width = rect.right;

			//clear line info
			mem_free(&bf->line);
			mem_free(&bf->linelen);

			bf->linesize = RESERVE_LINE;
			bf->line = mem_alloc(sizeof(int) * bf->linesize);
			if(bf->line == NULL) break;
			ZeroMemory(bf->line, sizeof(int) * bf->linesize);
			bf->linelen = mem_alloc(sizeof(int) * bf->linesize);
			if(bf->linelen == NULL) break;
			ZeroMemory(bf->linelen, sizeof(int) * bf->linesize);

			//set line info
			bf->view_line = 1;
			CountLine(hWnd, bf, bf->buf);
		}else{
			SetScrollBar(hWnd, bf, rect.bottom);
		}

		if(i == 1 || bf->pos > bf->max){
			bf->pos = bf->max;
			SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);
		}

		hDC = GetDC(hWnd);
		SelectObject(bf->mdc, bf->hRetBmp);
		DeleteObject(bf->hBmp);
		bf->hBmp = CreateCompatibleBitmap(hDC, rect.right, rect.bottom);
		bf->hRetBmp = SelectObject(bf->mdc, bf->hBmp);
		ReleaseDC(hWnd, hDC);

		InvalidateRect(hWnd, NULL, FALSE);

		// IMEの設定
		hIMC = ImmGetContext(hWnd);
		GetCaretPos(&pt);
		cf.dwStyle = CFS_POINT | CFS_RECT;
		cf.ptCurrentPos.x = pt.x;
		cf.ptCurrentPos.y = pt.y + (bf->Spacing / 2);
		GetClientRect(hWnd, &cf.rcArea);
		ImmSetCompositionWindow(hIMC, &cf);
		ImmReleaseContext(hWnd, hIMC);
		break;

	case WM_SETTEXT:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		if(bf->Limit <= 0){
			mem_free(&bf->buf);
			mem_free(&bf->style);
			mem_free(&bf->color);
			mem_free(&bf->line);
			mem_free(&bf->linelen);

			bf->size = RESERVE_SZIE;
			bf->linesize = RESERVE_LINE;

			//buf init
			bf->buf = mem_alloc(sizeof(TCHAR) * bf->size);
			if(bf->buf == NULL) break;
			bf->color = mem_alloc(sizeof(char) * bf->size);
			if(bf->color == NULL) break;
			bf->style = mem_alloc(sizeof(char) * bf->size);
			if(bf->style == NULL) break;

			//line init
			bf->line = mem_alloc(sizeof(int) * bf->linesize);
			if(bf->line == NULL) break;
			bf->linelen = mem_alloc(sizeof(int) * bf->linesize);
			if(bf->linelen == NULL) break;
		}
		ZeroMemory(bf->color, sizeof(char) * bf->size);
		ZeroMemory(bf->style, sizeof(char) * bf->size);
		*bf->buf = TEXT('\0');
		bf->len = 0;
		*bf->line = 0;
		bf->view_line = 1;
		bf->logic_line = 1;
		bf->pos = bf->max = 0;
		bf->sp = bf->cp = bf->buf;
		bf->cpx = 0;
		return AddString(hWnd, bf, (TCHAR *)lParam);

	case WM_VIEW_ADDTEXT:
		if(lParam == 0) break;
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		return AddString(hWnd, bf, (TCHAR *)lParam);

	case WM_GETTEXT:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		lstrcpyn((TCHAR *)lParam, bf->buf, (int)wParam);
		return lstrlen((TCHAR *)lParam);

	case WM_GETTEXTLENGTH:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		return bf->len;

	case WM_VIEW_GETBUFFERINFO:
		*((VIEW_BUFFER **)lParam) = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		break;

	case WM_VIEW_SETCOLOR:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
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

			bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
			if(bf == NULL) break;

			hFont = CreateViewFont(bf->mdc, bf, FALSE, FALSE, FALSE);
			hRetFont = SelectObject(bf->mdc, hFont);
			//Metrics
			GetTextMetrics(bf->mdc, &tm);
			bf->FontHeight = tm.tmHeight + bf->Spacing;
			bf->CharWidth = tm.tmAveCharWidth;
			bf->Fixed = (tm.tmPitchAndFamily & TMPF_FIXED_PITCH) ? FALSE : TRUE;
			SelectObject(bf->mdc, hRetFont);
			DeleteObject(hFont);

			if(GetFocus() == hWnd){
				DestroyCaret();
				CreateCaret(hWnd, 0, 0, bf->FontHeight);
				ShowCaret(hWnd);
			}
			bf->width = 0;
			SendMessage(hWnd, WM_SIZE, 0, 0);
		}
		break;

	case WM_VIEW_SETSPACING:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		bf->Spacing = ((int)wParam < 0) ? 0 : (int)wParam;
		SendMessage(hWnd, WM_VIEW_REFLECT, 0, 0);
		break;

	case WM_SETFONT:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;

		if(GetObject((HGDIOBJ)wParam, sizeof(LOGFONT), &(bf->lf)) == 0){
			break;
		}
		SendMessage(hWnd, WM_VIEW_REFLECT, 0, 0);
		break;

	case WM_COPY:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		if(bf->cp != bf->sp){
			if(bf->sp > bf->cp){
				Text2Clipboard(hWnd, bf->cp, bf->sp);
			}else{
				Text2Clipboard(hWnd, bf->sp, bf->cp);
			}
		}
		break;

	case WM_PASTE:
		// 貼り付け
		if ((bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) == NULL || bf->input_mode == FALSE) {
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
				SetInputBuf(hWnd, bf, p);
				GlobalUnlock(hclip);
			}
			CloseClipboard();
			SetCursor(old_cursor);
		}
		break;

	case WM_VIEW_SETLINELIMIT:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		bf->LineLimit = (int)wParam;

		if(bf->LineLimit > 0){
			int ln;

			for(ln = 0, p = bf->buf; *p != TEXT('\0'); p++){
				if(*p == TEXT('\n')){
					ln++;
				}
			}
			if(ln > bf->LineLimit){
				for(i = 0, p = bf->buf; *p != TEXT('\0'); p++){
					if(*p == TEXT('\n')){
						i++;
						if(i >= ln){
							break;
						}
					}
				}
				for(; *p == TEXT('\r') || *p == TEXT('\n'); p++);
				for(i = 0; i < bf->view_line - 1 && *(bf->line + i + 1) + bf->buf <= p; i++);
				ReCountLine(bf, i);

				bf->cp = bf->cp - (p - bf->buf);
				bf->sp = bf->sp - (p - bf->buf);
				if(bf->cp < bf->buf) bf->cp = bf->buf;
				if(bf->sp < bf->buf) bf->sp = bf->buf;

				CopyMemory(bf->style, bf->style + (p - bf->buf), sizeof(char) * (bf->len - (p - bf->buf)));
				ZeroMemory(bf->style + (bf->len - (p - bf->buf)), sizeof(char) * (p - bf->buf));
				CopyMemory(bf->color, bf->color + (p - bf->buf), sizeof(char) * (bf->len - (p - bf->buf)));
				ZeroMemory(bf->color + (bf->len - (p - bf->buf)), sizeof(char) * (p - bf->buf));

				bf->len -= (p - bf->buf);
				lstrcpy(bf->buf, p);

				bf->logic_line = bf->LineLimit;
			}else{
				bf->logic_line = ln + 1;
			}
		}
		break;

	case WM_VIEW_SETSCROLLLOCK:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		bf->sc_lock = (int)wParam;
		break;

	case WM_VIEW_INITINPUTMODE:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if (bf == NULL) break;
		bf->input_mode = FALSE;
		*bf->input_string = TEXT('\0');
		*bf->input_buf = TEXT('\0');
		break;

	case WM_VIEW_SETINPUTMODE:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if (bf == NULL) break;
		if (bf->input_mode != (BOOL)wParam) {
			bf->input_mode = (BOOL)wParam;
			if (bf->input_mode == TRUE) {
				*bf->input_string = TEXT('\0');
				SetInputBuf(hWnd, bf, bf->input_buf);
				bf->sp = bf->cp = (bf->buf + bf->len);
				bf->pos = bf->max;
				SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);
				InvalidateRect(hWnd, NULL, FALSE);
				UpdateWindow(hWnd);
			}
		}
		break;

	case WM_VIEW_GETINPUTMODE:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if (bf == NULL) break;
		return bf->input_mode;

	case WM_VIEW_GETINPUTSTRING:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if (bf == NULL) break;
		return (LRESULT)bf->input_string;
		
	case EM_GETFIRSTVISIBLELINE:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		return bf->pos;

	case EM_GETLINE:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		if((int)wParam >= bf->view_line) return 0;
		lstrcpyn((TCHAR *)lParam, bf->buf + *(bf->line + wParam), *(bf->linelen + wParam));
		return *(bf->linelen + wParam);

	case EM_GETLINECOUNT:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		return bf->view_line;

	case EM_GETSEL:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		*((LPDWORD)wParam) = ((bf->sp > bf->cp) ? bf->sp : bf->cp) - bf->buf;
		*((LPDWORD)lParam) = ((bf->sp > bf->cp) ? bf->cp : bf->sp) - bf->buf;
		return MAKELPARAM(*((LPDWORD)wParam), *((LPDWORD)lParam));

	case EM_LIMITTEXT:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		if((int)wParam <= 0){
			bf->Limit = 0;
			break;
		}

		bf->size = bf->Limit = (int)wParam;
		if(bf->len > bf->Limit){
			r = bf->buf + bf->len - bf->Limit + 1;
			for(; *r != TEXT('\0') && *r != TEXT('\n'); r++);
			for(; *r == TEXT('\r') || *r == TEXT('\n'); r++);
		}else{
			r = bf->buf;
		}

		cp = mem_alloc(sizeof(char) * bf->size);
		if(cp != NULL){
			ZeroMemory(cp, sizeof(char) * bf->size);
			CopyMemory(cp, bf->style + (r - bf->buf), sizeof(char) * (lstrlen(r)));
			mem_free(&bf->style);
			bf->style = (char *)cp;
		}

		cp = mem_alloc(sizeof(char) * bf->size);
		if(cp != NULL){
			ZeroMemory(cp, sizeof(char) * bf->size);
			CopyMemory(cp, bf->color + (r - bf->buf), sizeof(char) * (lstrlen(r)));
			mem_free(&bf->color);
			bf->color = (char *)cp;
		}

		p = mem_alloc(sizeof(TCHAR) * bf->size);
		if(p != NULL){
			lstrcpy(p, r);
			mem_free(&bf->buf);
			bf->buf = p;
			bf->len = lstrlen(p);
		}

		mem_free(&bf->line);
		mem_free(&bf->linelen);

		//line init
		bf->linesize = RESERVE_LINE;
		bf->line = mem_alloc(sizeof(int) * bf->linesize);
		if(bf->line == NULL) break;
		*bf->line = 0;
		bf->linelen = mem_alloc(sizeof(int) * bf->linesize);
		if(bf->linelen == NULL) break;
		bf->view_line = 1;
		bf->max = 0;

		//set line
		CountLine(hWnd, bf, bf->buf);

		bf->pos = bf->max;
		bf->sp = bf->cp = bf->buf + bf->len;
		bf->cpx = 0;

		SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);
		InvalidateRect(hWnd, NULL, FALSE);
		UpdateWindow(hWnd);
		break;

	case EM_LINEFROMCHAR:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		p = ((int)wParam == -1) ? bf->cp : bf->buf + wParam;
		for(j = 0; j < bf->view_line - 1 && *(bf->line + j + 1) + bf->buf <= p; j++);
		return j;

	case EM_LINEINDEX:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		if((int)wParam == -1){
			for(j = 0; j < bf->view_line - 1 && *(bf->line + j + 1) + bf->buf <= bf->cp; j++);
		}else{
			if((int)wParam >= bf->view_line) return -1;
			j = (int)wParam;
		}
		return *(bf->line + j);

	case EM_LINELENGTH:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		p = ((int)wParam == -1) ? bf->cp : bf->buf + wParam;
		for(j = 0; j < bf->view_line - 1 && *(bf->line + j + 1) + bf->buf <= p; j++);
		return *(bf->linelen + j);

	case EM_SETTABSTOPS:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		if((int)wParam <= 0){
			i = 8;
		}else{
			i = *((LPWORD)lParam);
		}
		if(i == bf->TabStop){
			return TRUE;
		}
		bf->TabStop = i;
		bf->view_line = 1;
		CountLine(hWnd, bf, bf->buf);
		return TRUE;

	case EM_SCROLL:
		SendMessage(hWnd, WM_VSCROLL, wParam, 0);
		break;

	case EM_SETSEL:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL || bf->input_mode == TRUE) break;
		i = ((int)wParam > lParam) ? (int)wParam : (int)lParam;
		j = ((int)wParam > lParam) ? (int)lParam : (int)wParam;
		len = bf->len;
		if(i < 0) i = 0;
		if(i > len) i = len;
		if(j < 0 || j > len) j = len;
		bf->sp = bf->buf + i;
		bf->cp = bf->buf + j;
		bf->cpx = 0;
		InvalidateRect(hWnd, NULL, FALSE);
		UpdateWindow(hWnd);
		break;

	case EM_SCROLLCARET:
		bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if(bf == NULL) break;
		GetClientRect(hWnd, &rect);
		for(i = 0; i < bf->view_line - 1 && *(bf->line + i + 1) + bf->buf <= bf->cp; i++);
		if(i < bf->pos){
			bf->pos = i;
			if(bf->pos < 0) bf->pos = 0;
			SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);

		}else if(i >= bf->pos + (rect.bottom / bf->FontHeight)){
			bf->pos = i - (rect.bottom / bf->FontHeight) + 1;
			if(bf->pos > bf->max) bf->pos = bf->max;
			SetScrollPos(hWnd, SB_VERT, bf->pos, TRUE);
		}
		InvalidateRect(hWnd, NULL, FALSE);
		UpdateWindow(hWnd);
		return !0;

	case WM_CONTEXTMENU:
		if ((bf = (VIEW_BUFFER *)GetWindowLongPtr(hWnd, GWLP_USERDATA)) != NULL) {
			HMENU hMenu;
			POINT apos;
			DWORD st, en;
			WORD lang;

			SendMessage(hWnd, EM_GETSEL, (WPARAM)&st, (LPARAM)&en);
			lang = PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale()));

			// メニューの作成
			hMenu = CreatePopupMenu();
			AppendMenu(hMenu, MF_STRING | ((st != en) ? 0 : MF_GRAYED), WM_COPY,
				(lang != LANG_JAPANESE) ? TEXT("&Copy") : TEXT("コピー(&C)"));
			if (bf->input_mode == TRUE) {
				AppendMenu(hMenu, MF_STRING, WM_PASTE,
					(lang != LANG_JAPANESE) ? TEXT("&Paste") : TEXT("貼り付け(&P)"));
			}
			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenu, MF_STRING, 1,
				(lang != LANG_JAPANESE) ? TEXT("Select &All") : TEXT("すべて選択(&A)"));
			AppendMenu(hMenu, MF_STRING, 2,
				(lang != LANG_JAPANESE) ? TEXT("C&lear console") : TEXT("実行結果のクリア(&L)"));

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
			case 2:
				SendMessage(hWnd, WM_SETTEXT, 0, (LPARAM)TEXT(""));
				break;
			default:
				SendMessage(hWnd, i, 0, 0);
				break;
			}
		}
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

/*
 * RegisterConsoleWindow - クラスの登録
 */
BOOL RegisterConsoleWindow(HINSTANCE hInstance)
{
	WNDCLASS wc;

	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = (WNDPROC)ConsoleProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = NULL;
	wc.hCursor = LoadCursor(0, IDC_IBEAM);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName = 0;
	wc.lpszClassName = CONSOLE_WND_CLASS;
	return RegisterClass(&wc);
}
/* End of source */
