/*
 * PG0 library
 *
 * string.c
 *
 * String functions (string.pg0), the same API as the web version's string.js.
 */

/* Include Files */
#include <windows.h>
#include <tchar.h>

#include "lib_common.h"

/* Define */

/* Global Variables */

/* Local Function Prototypes */

/*
 * DllMain
 */
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(hinst);
	}
	return TRUE;
}

/*
 * is_space - white space as defined by JavaScript's trim()
 */
static BOOL is_space(TCHAR c)
{
	if ((c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0xA0 || c == 0x1680 ||
		(c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
		c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF) {
		return TRUE;
	}
	return FALSE;
}

/*
 * _lib_func_trim - remove the white space around a string
 */
int SFUNC _lib_func_trim(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR *str, *p, *r;

	if (param == NULL) {
		return -2;
	}
	str = lib_to_string(param);
	if (str == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	for (p = str; *p != TEXT('\0') && is_space(*p); p++);
	for (r = p + lstrlen(p); r > p && is_space(*(r - 1)); r--);
	*r = TEXT('\0');
	lib_set_string(ret, p);
	mem_free(&str);
	return 0;
}

/*
 * js_case - change the case like JavaScript's toLowerCase()/toUpperCase() (frees str, returns a new string)
 *           ICU (icu.dll, Windows 10 1703 or later) does the full Unicode case mapping of JavaScript
 *           (U+00DF to "SS"); without it CharLowerBuff/CharUpperBuff differ for a few hundred characters
 */
typedef int (__cdecl *ICU_STRCASE)(WCHAR *dest, int destCapacity, const WCHAR *src, int srcLength, const char *locale, int *pErrorCode);

static TCHAR *js_case(TCHAR *str, const BOOL upper)
{
	static ICU_STRCASE u_strToLower = NULL, u_strToUpper = NULL;
	static BOOL checked = FALSE;
	ICU_STRCASE func;
	TCHAR *ret;
	int len = lstrlen(str);

	if (!checked) {
		HMODULE hIcu = LoadLibrary(TEXT("icu.dll"));
		if (hIcu != NULL) {
			u_strToLower = (ICU_STRCASE)GetProcAddress(hIcu, "u_strToLower");
			u_strToUpper = (ICU_STRCASE)GetProcAddress(hIcu, "u_strToUpper");
		}
		checked = TRUE;
	}
	func = (upper) ? u_strToUpper : u_strToLower;
	if (func != NULL) {
		// a character can become up to three (U+FB03 to "FFI")
		int err = 0, cap = len * 3 + 1, n;
		if ((ret = mem_alloc(sizeof(TCHAR) * cap)) != NULL) {
			n = func(ret, cap, str, len, "", &err);
			if (err <= 0 && n >= 0 && n < cap) {
				*(ret + n) = TEXT('\0');
				mem_free(&str);
				return ret;
			}
			mem_free(&ret);
		}
	}
	if (upper) {
		CharUpperBuff(str, len);
	} else {
		CharLowerBuff(str, len);
	}
	return str;
}

/*
 * _lib_func_to_lower - convert to lower case
 */
int SFUNC _lib_func_to_lower(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR *str;

	if (param == NULL) {
		return -2;
	}
	str = lib_to_string(param);
	if (str == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	ret->v->u.sValue = js_case(str, FALSE);
	ret->v->type = TYPE_STRING;
	return 0;
}

/*
 * _lib_func_to_upper - convert to upper case
 */
int SFUNC _lib_func_to_upper(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR *str;

	if (param == NULL) {
		return -2;
	}
	str = lib_to_string(param);
	if (str == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	ret->v->u.sValue = js_case(str, TRUE);
	ret->v->type = TYPE_STRING;
	return 0;
}

/*
 * lower_char - lower case of one character
 */
static TCHAR lower_char(TCHAR c)
{
	return (TCHAR)(DWORD_PTR)CharLower((TCHAR *)(DWORD_PTR)(unsigned short)c);
}

/*
 * str_match - wild card match ("*" any string, "?" any character)
 */
static BOOL str_match(const TCHAR *ptn, int plen, const TCHAR *str, int slen, int ip, int is)
{
	if (plen <= ip) {
		return (slen <= is);
	}
	switch (ptn[ip]) {
	case TEXT('*'):
		if (plen <= ip + 1) {
			return TRUE;
		}
		if (str_match(ptn, plen, str, slen, ip + 1, is)) {
			return TRUE;
		}
		while (slen > is) {
			is++;
			if (str_match(ptn, plen, str, slen, ip + 1, is)) {
				return TRUE;
			}
		}
		return FALSE;

	case TEXT('?'):
		return (slen > is) && str_match(ptn, plen, str, slen, ip + 1, is + 1);

	default:
		while (lower_char((ip < plen) ? ptn[ip] : TEXT('\0')) == lower_char((is < slen) ? str[is] : TEXT('\0'))) {
			if (plen <= ip) {
				return TRUE;
			}
			ip++;
			is++;
			if (ip < plen && (ptn[ip] == TEXT('*') || ptn[ip] == TEXT('?'))) {
				return str_match(ptn, plen, str, slen, ip, is);
			}
		}
		return FALSE;
	}
}

/*
 * _lib_func_str_match - compare a string with a wild card pattern
 */
int SFUNC _lib_func_str_match(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR *ptn, *str;
	BOOL m;

	if (lib_param_count(param) < 2) {
		return -2;
	}
	ptn = lib_to_string(param);
	str = lib_to_string(param->next);
	if (ptn == NULL || str == NULL) {
		mem_free(&ptn);
		mem_free(&str);
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	m = str_match(ptn, lstrlen(ptn), str, lstrlen(str), 0, 0);
	mem_free(&ptn);
	mem_free(&str);
	lib_set_int(ret, m ? 1 : 0);
	return 0;
}

/*
 * clamp_index - clamp an index into [0, len] (JavaScript substring rule)
 */
static int clamp_index(double d, int len)
{
	if (!(d > 0)) {
		return 0;
	}
	if (d > len) {
		return len;
	}
	return (int)d;
}

/*
 * _lib_func_substring - part of a string
 */
int SFUNC _lib_func_substring(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR *str;
	double begin, length = -1;
	int len, s, e, tmp;

	if (lib_param_count(param) < 2) {
		return -2;
	}
	str = lib_to_string(param);
	if (str == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	len = lstrlen(str);
	begin = (double)lib_to_int(param->next);
	if (begin < 0) {
		begin = len + begin;
	}
	if (param->next->next != NULL) {
		length = (double)lib_to_int(param->next->next);
	}
	s = clamp_index(begin, len);
	if (length < 0) {
		e = len;
	} else {
		e = clamp_index(begin + length, len);
	}
	if (s > e) {
		tmp = s;
		s = e;
		e = tmp;
	}
	*(str + e) = TEXT('\0');
	lib_set_string(ret, str + s);
	mem_free(&str);
	return 0;
}

/*
 * _lib_func_in_string - position of a string in a string
 */
int SFUNC _lib_func_in_string(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR *str, *search, *p;
	int from = 0, len, slen, i;

	if (lib_param_count(param) < 2) {
		return -2;
	}
	str = lib_to_string(param);
	search = lib_to_string(param->next);
	if (str == NULL || search == NULL) {
		mem_free(&str);
		mem_free(&search);
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	if (param->next->next != NULL) {
		from = lib_to_int(param->next->next);
	}
	len = lstrlen(str);
	slen = lstrlen(search);
	if (from < 0) {
		from = 0;
	}
	if (from > len) {
		from = len;
	}
	lib_set_int(ret, -1);
	if (slen == 0) {
		lib_set_int(ret, from);
	} else {
		for (i = from, p = str + from; i + slen <= len; i++, p++) {
			if (str_cmp_n(p, search, slen) == 0) {
				lib_set_int(ret, i);
				break;
			}
		}
	}
	mem_free(&str);
	mem_free(&search);
	return 0;
}

/*
 * _lib_func_split - split a string by a separator
 */
int SFUNC _lib_func_split(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *top = NULL, *last = NULL;
	TCHAR *str, *search, *p, *r;
	TCHAR one[2];
	int slen;

	if (lib_param_count(param) < 2) {
		return -2;
	}
	str = lib_to_string(param);
	search = lib_to_string(param->next);
	if (str == NULL || search == NULL) {
		mem_free(&str);
		mem_free(&search);
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	slen = lstrlen(search);
	if (slen == 0) {
		one[1] = TEXT('\0');
		for (p = str; *p != TEXT('\0'); p++) {
			one[0] = *p;
			lib_list_append(&top, &last, lib_new_string(NULL, one));
		}
	} else {
		p = str;
		for (;;) {
			for (r = p; *r != TEXT('\0') && str_cmp_n(r, search, slen) != 0; r++);
			if (*r == TEXT('\0')) {
				lib_list_append(&top, &last, lib_new_string(NULL, p));
				break;
			}
			*r = TEXT('\0');
			lib_list_append(&top, &last, lib_new_string(NULL, p));
			p = r + slen;
		}
	}
	mem_free(&str);
	mem_free(&search);
	lib_set_array(ret, top);
	return 0;
}
/* End of source */
