/*
 * PG0
 *
 * func_std.c
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#include <windows.h>
#include <tchar.h>

#include "../PG0/script_struct.h"
#include "../PG0/script_memory.h"
#include "../PG0/script_string.h"
#include "../PG0/script_utility.h"

/* Define */

/* Global Variables */

/* Local Function Prototypes */

/*
 * _lib_func_sum - 2‚Â‚Ì”’l‚Ì‘«‚µŽZ
 */
int SFUNC _lib_func_sum(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *v1 = param;
	VALUEINFO *v2;
	if (param == NULL) {
		return -2;
	}
	v2 = param->next;
	if (v2 == NULL) {
		return -2;
	}
	if (v1->v->type != TYPE_INTEGER || v2->v->type != TYPE_INTEGER) {
		return 0;
	}
	ret->v->u.iValue = v1->v->u.iValue + v2->v->u.iValue;
	ret->v->type = TYPE_INTEGER;
	return 0;
}

/*
 * _lib_func_tolower - •¶Žš—ñ’†‚Ì‰p‘å•¶Žš‚ð¬•¶Žš‚É•ÏŠ·
 */
int SFUNC _lib_func_tolower(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *vi = param;
	TCHAR *p, *r;

	if (param == NULL) {
		return -2;
	}
	if (vi->v->type != TYPE_STRING) {
		return 0;
	}
	ret->v->u.sValue = mem_alloc(sizeof(TCHAR) * (lstrlen(vi->v->u.sValue) + 1));
	if (ret->v->u.sValue == NULL) {
		return -1;
	}
	for (p = vi->v->u.sValue, r = ret->v->u.sValue; *p != TEXT('\0'); p++, r++) {
		*r = (*p >= TEXT('A') && *p <= TEXT('Z')) ? (*p - TEXT('A') + TEXT('a')) : *p;
	}
	*r = TEXT('\0');
	ret->v->type = TYPE_STRING;
	return 0;
}

/*
 * _lib_func_toupper - •¶Žš—ñ’†‚Ì‰p‘å•¶Žš‚ð‘å•¶Žš‚É•ÏŠ·
 */
int SFUNC _lib_func_toupper(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *vi = param;
	TCHAR *p, *r;

	if (param == NULL) {
		return -2;
	}
	if (vi->v->type != TYPE_STRING) {
		return 0;
	}
	ret->v->u.sValue = mem_alloc(sizeof(TCHAR) * (lstrlen(vi->v->u.sValue) + 1));
	if (ret->v->u.sValue == NULL) {
		return -1;
	}
	for (p = vi->v->u.sValue, r = ret->v->u.sValue; *p != TEXT('\0'); p++, r++) {
		*r = (*p >= TEXT('a') && *p <= TEXT('z')) ? (*p - TEXT('a') + TEXT('A')) : *p;
	}
	*r = TEXT('\0');
	ret->v->type = TYPE_STRING;
	return 0;
}
/* End of source */
