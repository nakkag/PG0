/*
 * PG0
 *
 * script_utility.c
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#include <windows.h>

#include "script.h"
#include "script_memory.h"
#include "script_string.h"
#include "script_utility.h"

/* Define */

/* Global Variables */

/* Local Function Prototypes */

/*
 * AllocValue - 変数の確保
 */
VALUEINFO *AllocValue()
{
	VALUEINFO *vi;

	vi = mem_calloc(sizeof(VALUEINFO));
	if (vi == NULL) {
		return NULL;
	}
	vi->v = mem_calloc(sizeof(VALUE));
	if (vi->v == NULL) {
		mem_free(&vi);
		return NULL;
	}
	vi->v->vi = vi;
	return vi;
}

/*
 * FreeValue - 変数の解放
 */
void FreeValue(VALUEINFO *vi)
{
	if (vi == NULL || vi == (VALUEINFO *)RET_ERROR || vi == (VALUEINFO *)RET_EXIT) {
		return;
	}
	mem_free(&(vi->name));
	mem_free(&(vi->org_name));
	if (vi->v != NULL && vi->v->vi == vi) {
		if (vi->v->type == TYPE_ARRAY) {
			FreeValueList(vi->v->u.array);
		} else if (vi->v->type == TYPE_STRING) {
			mem_free(&(vi->v->u.sValue));
		}
		mem_free(&(vi->v));
	}
	mem_free(&vi);
}

/*
 * FreeValueList - 変数リストの解放
 */
void FreeValueList(VALUEINFO *vi)
{
	VALUEINFO *tmpvi;

	if (vi == (VALUEINFO *)-1) {
		return;
	}
	while (vi != NULL) {
		tmpvi = vi->next;
		FreeValue(vi);
		vi = tmpvi;
	}
}

/*
 * CopyValueList - 変数リストのコピー
 */
VALUEINFO *CopyValueList(VALUEINFO *From)
{
	VALUEINFO *To, Top;

	To = &Top;
	To->next = NULL;
	for (; From != NULL; From = From->next) {
		if (From->v == NULL) {
			continue;
		}
		To = To->next = AllocValue();
		if (To == NULL) {
			return Top.next;
		}
		if (From->name != NULL) {
			To->name = alloc_copy(From->name);
			To->name_hash = From->name_hash;
		}
#ifndef PG0_CMD
		if (From->org_name != NULL) {
			To->org_name = alloc_copy(From->org_name);
		}
#endif
		switch (From->v->type) {
		case TYPE_ARRAY:
			To->v->u.array = CopyValueList(From->v->u.array);
			break;
		case TYPE_STRING:
			To->v->u.sValue = alloc_copy(From->v->u.sValue);
			break;
		case TYPE_FLOAT:
			To->v->u.fValue = From->v->u.fValue;
			break;
		default:
			To->v->u.iValue = From->v->u.iValue;
			break;
		}
		To->v->type = From->v->type;
	}
	return Top.next;
}

/*
 * CopyValue - 変数のコピー
 */
VALUEINFO *CopyValue(VALUEINFO *From)
{
	VALUEINFO *To;

	To = AllocValue();
	To->next = NULL;
	if (From->name != NULL) {
		To->name = alloc_copy(From->name);
		To->name_hash = From->name_hash;
	}
#ifndef PG0_CMD
	if (From->org_name != NULL) {
		To->org_name = alloc_copy(From->org_name);
	}
#endif
	switch (From->v->type) {
	case TYPE_ARRAY:
		To->v->u.array = CopyValueList(From->v->u.array);
		break;
	case TYPE_STRING:
		To->v->u.sValue = alloc_copy(From->v->u.sValue);
		break;
	case TYPE_FLOAT:
		To->v->u.fValue = From->v->u.fValue;
		break;
	default:
		To->v->u.iValue = From->v->u.iValue;
		break;
	}
	To->v->type = From->v->type;
	return To;
}

/*
 * GetValueInt - 整数の取得
 */
int GetValueInt(VALUE *v)
{
	if (v->type == TYPE_FLOAT) {
		return (int)v->u.fValue;
	}
	if (v->type != TYPE_INTEGER) {
		return 0;
	}
	return v->u.iValue;
}

/*
 * GetValueString - 文字列の取得
 */
TCHAR *GetValueString(VALUE *v)
{
	TCHAR *buf;

	switch (v->type) {
	case TYPE_ARRAY:
		buf = alloc_copy(TEXT(""));
		break;
	case TYPE_STRING:
		buf = alloc_copy(v->u.sValue);
		break;
	case TYPE_FLOAT:
		buf = f2a(v->u.fValue);
		break;
	default:
		buf = i2a(v->u.iValue);
		break;
	}
	return buf;
}

/*
 * GetValueFloat - 実数の取得
 */
double GetValueFloat(VALUE *v)
{
	if (v->type == TYPE_INTEGER) {
		return (double)v->u.iValue;
	}
	if (v->type != TYPE_FLOAT) {
		return 0;
	}
	return v->u.fValue;
}

/*
 * GetValueBoolean - 真偽の取得
 * 0, 0.0, ""を偽とする
 */
BOOL GetValueBoolean(VALUE *v)
{
	switch (v->type) {
	case TYPE_STRING:
		if (*v->u.sValue == TEXT('\0')) {
			return FALSE;
		}
		break;
	case TYPE_FLOAT:
		if (v->u.fValue == 0.0) {
			return FALSE;
		}
		break;
	default:
		if (v->u.iValue == 0) {
			return FALSE;
		}
		break;
	}
	return TRUE;
}

/*
 * IntToVariable - 数値を変数に変換
 */
VALUEINFO *IntToVariable(TCHAR *name, const int i)
{
	VALUEINFO *vi;

	vi = AllocValue();
	if(vi == NULL){
		return NULL;
	}
	vi->name = alloc_copy(name);
	if (vi->name != NULL) {
		str_lower(vi->name);
	}
	vi->name_hash = str2hash(name);
	vi->name_hash = str2hash(name);
	vi->v->u.iValue = i;
	vi->v->type = TYPE_INTEGER;
	return vi;
}

/*
 * VariableToInt - 変数を数値に変換
 */
int VariableToInt(VALUEINFO *vi)
{
	if(vi == NULL || vi->v == NULL){
		return 0;
	}
	return GetValueInt(vi->v);
}

/*
 * StringToVariable - 文字列を変数に変換
 */
VALUEINFO *StringToVariable(TCHAR *name, const TCHAR *buf)
{
	VALUEINFO *vi;

	vi = AllocValue();
	if(vi == NULL){
		return NULL;
	}
	vi->name = alloc_copy(name);
	if (vi->name != NULL) {
		str_lower(vi->name);
	}
	vi->name_hash = str2hash(name);
	vi->org_name = alloc_copy(name);
	vi->v->u.sValue = alloc_copy(buf);
	vi->v->type = TYPE_STRING;
	return vi;
}

/*
 * VariableToString - 変数を文字列に変換
 */
TCHAR *VariableToString(VALUEINFO *vi)
{
	if(vi == NULL || vi->v == NULL){
		return alloc_copy(TEXT(""));
	}
	return GetValueString(vi->v);
}

/*
 * _ArrayToStringSize - 配列の出力サイズ取得
 */
int _ArrayToStringSize(VALUEINFO *vi, BOOL hex_mode)
{
	TCHAR buf[BUF_SIZE];
	BOOL first = TRUE;
	int ret = 0;
	for(; vi != NULL; vi = vi->next){
		if (first != TRUE) {
			ret++;
		}
		first = FALSE;
		if (vi->org_name != NULL) {
			ret += lstrlen(vi->org_name) + 3;
		} else if (vi->name != NULL) {
			ret += lstrlen(vi->name) + 3;
		}
		if (vi->v->type == TYPE_ARRAY) {
			ret += _ArrayToStringSize(vi->v->u.array, hex_mode) + 2;
		} else if (vi->v->type == TYPE_STRING) {
			TCHAR *tmp = reconv_ctrl(vi->v->u.sValue);
			ret += lstrlen(tmp) + 2;
			mem_free(&tmp);
		} else if (vi->v->type == TYPE_FLOAT) {
			TCHAR tmp[FLOAT_LENGTH];
			swprintf_s(tmp, FLOAT_LENGTH, TEXT("%.16f"), vi->v->u.fValue);
			ret += lstrlen(tmp);
		} else {
			if (hex_mode == FALSE) {
				wsprintf(buf, TEXT("%ld"), vi->v->u.iValue);
			} else {
				wsprintf(buf, TEXT("0x%X"), vi->v->u.iValue);
			}
			ret += lstrlen(buf);
		}
	}
	return ret;
}
int ArrayToStringSize(VALUEINFO *vi, BOOL hex_mode)
{
	return _ArrayToStringSize(vi, hex_mode) + 2;
}

/*
 * _ArrayToString - 配列の出力
 */
static void _ArrayToString(VALUEINFO *vi, TCHAR *buf, BOOL hex_mode)
{
	TCHAR tmp[BUF_SIZE];
	BOOL first = TRUE;
	for(; vi != NULL; vi = vi->next){
		if (first != TRUE) {
			lstrcat(buf, TEXT(","));
		}
		first = FALSE;

		if (vi->org_name != NULL) {
			lstrcat(buf, TEXT("\""));
			lstrcat(buf, vi->org_name);
			lstrcat(buf, TEXT("\":"));
		} else if (vi->name != NULL) {
			lstrcat(buf, TEXT("\""));
			lstrcat(buf, vi->name);
			lstrcat(buf, TEXT("\":"));
		}
		if (vi->v->type == TYPE_ARRAY) {
			lstrcat(buf, TEXT("{"));
			_ArrayToString(vi->v->u.array, buf, hex_mode);
			lstrcat(buf, TEXT("}"));
		} else if (vi->v->type == TYPE_STRING) {
			TCHAR *tmp = reconv_ctrl(vi->v->u.sValue);
			lstrcat(buf, TEXT("\""));
			lstrcat(buf, tmp);
			lstrcat(buf, TEXT("\""));
			mem_free(&tmp);
		} else if (vi->v->type == TYPE_FLOAT) {
			TCHAR ftmp[FLOAT_LENGTH];
			_stprintf_s(ftmp, FLOAT_LENGTH, TEXT("%.16f"), vi->v->u.fValue);
			lstrcat(buf, ftmp);
		} else {
			if (hex_mode == FALSE) {
				wsprintf(tmp, TEXT("%ld"), vi->v->u.iValue);
			} else {
				wsprintf(tmp, TEXT("0x%X"), vi->v->u.iValue);
			}
			lstrcat(buf, tmp);
		}
	}
}
void ArrayToString(VALUEINFO *vi, TCHAR *buf, BOOL hex_mode)
{
	lstrcpy(buf, TEXT("{"));
	_ArrayToString(vi, buf, hex_mode);
	lstrcat(buf, TEXT("}"));
}
/* End of source */
