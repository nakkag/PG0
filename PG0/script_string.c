/*
 * PG0
 *
 * script_string.c
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#include <windows.h>
#include <tchar.h>

#include "script_memory.h"
#include "script_string.h"

/* Define */
#define BUF_SIZE				256

#define ToLower(c)				((c >= TEXT('A') && c <= TEXT('Z')) ? (c - TEXT('A') + TEXT('a')) : c)

/* Global Variables */

/* Local Function Prototypes */

/*
* tchar2char - メモリを確保して char を char に変換する
*/
char *tchar2char(const TCHAR *str)
{
#ifdef UNICODE
	char *cchar;
	int len;

	if (str == NULL) {
		return NULL;
	}
	len = WideCharToMultiByte(CP_ACP, 0, str, -1, NULL, 0, NULL, NULL);
	cchar = (char *)mem_alloc(len + 1);
	if (cchar == NULL) {
		return NULL;
	}
	WideCharToMultiByte(CP_ACP, 0, str, -1, cchar, len, NULL, NULL);
	return cchar;
#else
	return alloc_copy(str);
#endif
}

/*
 * char2tchar - メモリを確保して char を char に変換する
 */
TCHAR *char2tchar(const char *str)
{
#ifdef UNICODE
	TCHAR *tchar;
	int len;

	if (str == NULL) {
		return NULL;
	}
	len = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
	tchar = (TCHAR *)mem_alloc(sizeof(TCHAR) * (len + 1));
	if (tchar == NULL) {
		return NULL;
	}
	MultiByteToWideChar(CP_ACP, 0, str, -1, tchar, len);
	return tchar;
#else
	return alloc_copy(str);
#endif
}

/*
 * str_cpy - 文字列をコピーして最後の文字のアドレスを返す
 */
TCHAR *str_cpy(TCHAR *ret, const TCHAR *buf)
{
	if (buf == NULL) {
		*ret = TEXT('\0');
		return ret;
	}
	while (*(ret++) = *(buf++));
	ret--;
	return ret;
}

/*
 * str_cpy_n - 指定された文字数まで文字列をコピーする
 */
TCHAR *str_cpy_n(TCHAR *ret, TCHAR *buf, const int len)
{
	int i = len;

	if (buf == NULL || len <= 0) {
		*ret = TEXT('\0');
		return ret;
	}
	while ((*(ret++) = *(buf++)) && --i);
	*ret = TEXT('\0');
	if (i != 0) ret--;
	return ret;
}

/*
 * str_cmp_n - ２つの文字列を文字数分比較を行う
 */
int str_cmp_n(const TCHAR *buf1, const TCHAR *buf2, const int len)
{
	int i = 0;

	for (; *buf1 == *buf2 && *buf1 != TEXT('\0') && i < len; i++, buf1++, buf2++);
	return ((i == len) ? 0 : *buf1 - *buf2);
}

/*
 * str_cmp_i - 文字列の大文字小文字を区別しない比較を行う
 */
int str_cmp_i(const TCHAR *buf1, const TCHAR *buf2)
{
	for (; ToLower(*buf1) == ToLower(*buf2) && *buf1 != TEXT('\0'); buf1++, buf2++);
	return ToLower(*buf1) - ToLower(*buf2);
}

/*
 * str_cmp_ni - ２つの文字列を大文字、小文字を区別しないで比較を行う
 */
int str_cmp_ni(const TCHAR *buf1, const TCHAR *buf2, const int len)
{
	int i = 0;

	for (; ToLower(*buf1) == ToLower(*buf2) && *buf1 != TEXT('\0') && i < len; i++, buf1++, buf2++);
	return ((i == len) ? 0 : ToLower(*buf1) - ToLower(*buf2));
}

/*
 * f2a - 実数を10進表記の文字列に変換する
 */
TCHAR *f2a(const double f)
{
	TCHAR *buf;

	buf = mem_alloc(sizeof(TCHAR) * FLOAT_LENGTH);
	if (buf == NULL) {
		return NULL;
	}
	swprintf_s(buf, FLOAT_LENGTH, TEXT("%.16f"), f);
	return buf;
}

/*
 * i2a - 整数を10進表記の文字列に変換する
 */
TCHAR *i2a(const int i)
{
	TCHAR *buf;

	buf = mem_alloc(sizeof(TCHAR) * INT_LENGTH);
	if (buf == NULL) {
		return NULL;
	}
	_ltot_s(i, buf, INT_LENGTH, 10);
	return buf;
}

/*
 * x2d - 16進表記の文字列を数値に変換する
 */
int x2d(const TCHAR *str)
{
	int num = 0;
	int m;

	for (; *str != TEXT('\0'); str++) {
		if (*str >= TEXT('0') && *str <= TEXT('9')) {
			m = *str - TEXT('0');
		} else if (*str >= TEXT('A') && *str <= TEXT('F')) {
			m = *str - TEXT('A') + 10;
		} else if (*str >= TEXT('a') && *str <= TEXT('f')) {
			m = *str - TEXT('a') + 10;
		} else {
			break;
		}
		num = 16 * num + m;
	}
	return num;
}

/*
 * o2d - 8進表記の文字列を数値に変換する
 */
int o2d(const TCHAR *str)
{
	int num = 0;
	int m;

	for (; *str != TEXT('\0'); str++) {
		if (*str >= TEXT('0') && *str <= TEXT('7')) {
			m = *str - TEXT('0');
		} else {
			break;
		}
		num = 8 * num + m;
	}
	return num;
}

/*
 * conv_ctrl - 制御文字を変換
 */
BOOL conv_ctrl(TCHAR *buf)
{
	TCHAR *p, *r, *s;
	TCHAR tmp[10];

	if (buf == NULL) {
		return FALSE;
	}

	p = buf;
	while (*p != TEXT('\0')) {
#ifndef UNICODE
		if (IsDBCSLeadByte((BYTE)*p) == TRUE && *(p + 1) != TEXT('\0')) {
			//2バイトコードをスキップ
			p += 2;
			continue;
		}
#endif
		if (*p == TEXT('\\') && *(p + 1) != TEXT('\0')) {
			//制御文字の変換
			r = p + 1;
			switch (*r) {
			case TEXT('r'):
				//CR
				r++;
				*(p++) = TEXT('\r');
				break;

			case TEXT('n'):
				//LF
				r++;
				*(p++) = TEXT('\n');
				break;

			case TEXT('t'):
				//タブ
				r++;
				*(p++) = TEXT('\t');
				break;

			case TEXT('b'):
				//バックスペース
				r++;
				*(p++) = TEXT('\b');
				break;

			case TEXT('x'):
				//Char (Hex)
				r++;
				if (*r != TEXT('\0')) {
#ifdef UNICODE
					int len = 4;
#else
					int len = 2;
#endif
					for (s = r; (s - r) < len && ((*s >= TEXT('0') && *s <= TEXT('9'))
						|| (*s >= TEXT('A') && *s <= TEXT('F'))
						|| (*s >= TEXT('a') && *s <= TEXT('f'))); s++);
					str_cpy_n(tmp, r, (int)(s - r));
					*(p++) = (TCHAR)x2d(tmp);
					r += s - r;
				}
				break;

			case TEXT('\"'):
				r++;
				*(p++) = TEXT('\"');
				break;

			case TEXT('\''):
				r++;
				*(p++) = TEXT('\'');
				break;

			case TEXT('\\'):
				r++;
				*(p++) = TEXT('\\');
				break;

			default:
				if (*r >= TEXT('0') && *r <= TEXT('7')) {
					//Char (Oct)
#ifdef UNICODE
					int len = 6;
#else
					int len = 3;
#endif
					for (s = r; (s - r) < len && (*s >= TEXT('0') && *s <= TEXT('7')); s++);
					str_cpy_n(tmp, r, (int)(s - r));
					*(p++) = (TCHAR)o2d(tmp);
					r += s - r;
					break;
				} else {
					p++;
				}
				continue;
			}
			lstrcpy(p, r);

		} else {
			p++;
		}
	}
	return TRUE;
}

/*
 * reconv_ctrl - 制御文字を文字に変換
 */
TCHAR *reconv_ctrl(TCHAR *buf)
{
	TCHAR *ret, *p, *r;
	int len;

	for (p = buf, len = 0; *p != TEXT('\0'); p++, len++) {
#ifndef UNICODE
		if (IsDBCSLeadByte((BYTE)*p) == TRUE && *(p + 1) != TEXT('\0')) {
			p++;
			len++;
		} else {
			switch (*p) {
			case TEXT('\r'):
			case TEXT('\n'):
			case TEXT('\t'):
			case TEXT('\b'):
			case TEXT('\"'):
			case TEXT('\\'):
				len += 2;
				break;
			}
		}
#else
		switch (*p) {
		case TEXT('\r'):
		case TEXT('\n'):
		case TEXT('\t'):
		case TEXT('\b'):
		case TEXT('\"'):
		case TEXT('\\'):
			len++;
			break;
		}
#endif
	}
	ret = mem_alloc(sizeof(TCHAR) * (len + 1));
	if (ret == NULL) {
		return NULL;
	}
	for (p = buf, r = ret; *p != TEXT('\0'); p++) {
#ifndef UNICODE
		if (IsDBCSLeadByte((BYTE)*p) == TRUE && *(p + 1) != TEXT('\0')) {
			*(r++) = *(p++);
		} else {
			switch (*p) {
			case TEXT('\r'):
				//CR
				*(r++) = TEXT('\\');
				*(r++) = TEXT('r');
				break;

			case TEXT('\n'):
				//LF
				*(r++) = TEXT('\\');
				*(r++) = TEXT('n');
				break;

			case TEXT('\t'):
				//タブ
				*(r++) = TEXT('\\');
				*(r++) = TEXT('t');
				break;

			case TEXT('\b'):
				//バックスペース
				*(r++) = TEXT('\\');
				*(r++) = TEXT('b');
				break;

			case TEXT('\"'):
				*(r++) = TEXT('\\');
				*(r++) = TEXT('"');
				break;

			case TEXT('\\'):
				*(r++) = TEXT('\\');
				*(r++) = TEXT('\\');
				break;

			default:
				*(r++) = *p;
				break;
			}
		}
#else
		switch (*p) {
		case TEXT('\r'):
			//CR
			*(r++) = TEXT('\\');
			*(r++) = TEXT('r');
			break;

		case TEXT('\n'):
			//LF
			*(r++) = TEXT('\\');
			*(r++) = TEXT('n');
			break;

		case TEXT('\t'):
			//タブ
			*(r++) = TEXT('\\');
			*(r++) = TEXT('t');
			break;

		case TEXT('\b'):
			//バックスペース
			*(r++) = TEXT('\\');
			*(r++) = TEXT('b');
			break;

		case TEXT('\"'):
			*(r++) = TEXT('\\');
			*(r++) = TEXT('"');
			break;

		case TEXT('\\'):
			*(r++) = TEXT('\\');
			*(r++) = TEXT('\\');
			break;

		default:
			*(r++) = *p;
			break;
		}
#endif
	}
	*r = TEXT('\0');
	return ret;
}

/*
 * str_skip - 文字列の終わり位置の取得
 */
TCHAR *str_skip(TCHAR *p, TCHAR end)
{
	//文字列はスキップ
	for (p++; *p != TEXT('\0') && *p != end; p++) {
#ifndef UNICODE
		if (IsDBCSLeadByte((BYTE)*p) == TRUE && *(p + 1) != TEXT('\0')) {
			//2バイトコードをスキップ
			p++;
			if (*p == TEXT('\0')) break;
			continue;
		}
#endif
		if (*p == TEXT('\\')) {
			p++;
			if (*p == TEXT('\0')) {
				break;
			}
		}
	}
	return p;
}

/*
 * GetPairtBrace - 対応する括弧を取得
 */
TCHAR *get_pair_brace(TCHAR *buf)
{
	TCHAR sc, ec;
	int k_cnt = 1;

	sc = *buf;
	switch (sc) {
	case TEXT('{'):
		ec = TEXT('}');
		break;

	case TEXT('('):
		ec = TEXT(')');
		break;

	case TEXT('['):
		ec = TEXT(']');
		break;

	default:
		return NULL;
	}

	for (buf++; *buf != TEXT('\0'); buf++) {
#ifndef UNICODE
		if (IsDBCSLeadByte((BYTE)*buf) == TRUE && *(buf + 1) != TEXT('\0')) {
			//2バイトコードをスキップ
			buf++;
			if (*buf == TEXT('\0')) {
				break;
			}
			continue;
		}
#endif
		if (*buf == TEXT('\"') || *buf == TEXT('\'')) {
			//文字列はスキップ
			buf = str_skip(buf, *buf);
			if (*buf == TEXT('\0')) {
				break;
			}
		}
		if (*buf == TEXT('/') && *(buf + 1) == TEXT('/')) {
			//コメントをスキップ
			for (; *buf != TEXT('\0') && *buf != TEXT('\r') && *buf != TEXT('\n'); buf++);
			if (*buf == TEXT('\0')) {
				break;
			}
		}
		if (*buf == sc) {
			k_cnt++;
		} else if (*buf == ec) {
			k_cnt--;
		}
		if (k_cnt == 0) break;
	}
	return ((k_cnt == 0) ? buf : NULL);
}

/*
 * str_trim - 文字列の前後の空白, Tabを除去する
 */
BOOL str_trim(TCHAR *buf)
{
	TCHAR *p, *r;

	//前後の空白を除いたポインタを取得
	for (p = buf; (*p == TEXT(' ') || *p == TEXT('\t')) && *p != TEXT('\0'); p++);
	for (r = buf + lstrlen(buf) - 1; (*r == TEXT(' ') || *r == TEXT('\t')) && r > p; r--);
	*(r + 1) = TEXT('\0');

	//元の文字列にコピーを行う
	lstrcpy(buf, p);
	return TRUE;
}

/*
 * str_lower - 文字列を小文字に変換
 */
void str_lower(TCHAR *p)
{
	for (; *p != TEXT('\0'); p++) {
		*p = ToLower(*p);
	}
}

/*
 * str2hash - 文字列のハッシュ値を取得
 */
int str2hash(TCHAR *str)
{
	int hash = 0;

	if (str == NULL) return 0;

	for (; *str != TEXT('\0'); str++) {
		hash = ((hash << 1) + *str);
	}
	return hash;
}
/* End of source */
