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

#include "script_struct.h"
#include "script_memory.h"
#include "script_string.h"
#include "script_utility.h"

/* Define */

/* Global Variables */

/* Local Function Prototypes */

/*
 * _lib_func_istype - 型の判定
 */
int SFUNC _lib_func_istype(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (param == NULL) {
		return -2;
	}
	ret->v->u.iValue = param->v->type;
	ret->v->type = TYPE_INTEGER;
	return 0;
}

/*
 * _lib_func_length - 長さを取得
 */
int SFUNC _lib_func_length(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *tmp;
	TCHAR *str;
	int len = 0;

	if(param == NULL){
		return -2;
	}
	switch (param->v->type) {
	case TYPE_ARRAY:
		for (tmp = param->v->u.array; tmp != NULL; tmp = tmp->next) {
			len++;
		}
		ret->v->u.iValue = len;
		break;
	case TYPE_STRING:
		ret->v->u.iValue = lstrlen(param->v->u.sValue);
		break;
	default:
		str = VariableToString(param);
		ret->v->u.iValue = lstrlen(str);
		mem_free(&str);
		break;
	}
	ret->v->type = TYPE_INTEGER;
	return 0;
}

/*
 * StringToValueList - 文字列を配列に変換
 */
static VALUEINFO *StringToValueList(TCHAR *str)
{
	VALUEINFO *To, Top;

	To = &Top;
	To->next = NULL;
	for (; *str != TEXT('\0'); str++) {
		To = To->next = AllocValue();
		if (To == NULL) {
			return Top.next;
		}
		To->v->u.sValue = mem_alloc(sizeof(TCHAR) * 2);
		*(To->v->u.sValue) = *str;
		*(To->v->u.sValue + 1) = TEXT('\0');
		To->v->type = TYPE_STRING;
	}
	return Top.next;
}

/*
 * _lib_func_array - 配列に変換
 */
int SFUNC _lib_func_array(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR *str;

	if (param == NULL) {
		return -2;
	}
	switch (param->v->type) {
	case TYPE_ARRAY:
		ret->v->u.array = CopyValueList(param->v->u.array);
		break;
	case TYPE_STRING:
		ret->v->u.array = StringToValueList(param->v->u.sValue);
		break;
	default:
		str = VariableToString(param);
		ret->v->u.array = StringToValueList(str);
		mem_free(&str);
		break;
	}
	ret->v->type = TYPE_ARRAY;
	return 0;
}

/*
 * ValueListToString - 配列を文字列に変換
 */
TCHAR *ValueListToString(VALUEINFO *From)
{
	TCHAR *ret, *p;
	VALUEINFO *vi;
	int len = 0;

	for (vi = From; vi != NULL; vi = vi->next) {
		if (vi->v->type == TYPE_ARRAY) {
		} else if (vi->v->type == TYPE_STRING) {
			len += lstrlen(vi->v->u.sValue);
		} else {
			TCHAR *str = VariableToString(vi);
			len += lstrlen(str);
			mem_free(&str);
		}
		len++;
	}
	p = ret = mem_calloc(sizeof(TCHAR) * (len + 1));
	if (ret == NULL) {
		return NULL;
	}
	for (vi = From; vi != NULL; vi = vi->next) {
		if (vi->v->type == TYPE_ARRAY) {
		} else if (vi->v->type == TYPE_STRING) {
			lstrcpy(p, vi->v->u.sValue);
			p += lstrlen(p);
		} else {
			TCHAR *str = VariableToString(vi);
			lstrcpy(p, str);
			p += lstrlen(p);
			mem_free(&str);
		}
	}
	return ret;
}

/*
 * _lib_func_string - 文字列に変換
 */
int SFUNC _lib_func_string(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (param == NULL) {
		return -2;
	}
	if (param->v->type == TYPE_ARRAY) {
		ret->v->u.sValue = ValueListToString(param->v->u.array);
	} else {
		ret->v->u.sValue = VariableToString(param);
	}
	ret->v->type = TYPE_STRING;
	return 0;
}

/*
 * StringToNumber - 文字列を数値に変換
 */
void StringToNumber(TCHAR *buf, VALUEINFO *ret)
{
	TCHAR *p;

	for (p = buf; *p != TEXT('\0') && *p != TEXT('.'); p++);
	if (*p == TEXT('.')) {
		ret->v->u.fValue = _ttof(buf);
		ret->v->type = TYPE_FLOAT;
	} else {
		ret->v->u.iValue = _ttoi(buf);
		ret->v->type = TYPE_INTEGER;
	}
}

/*
 * _lib_func_number - 数値に変換
 */
int SFUNC _lib_func_number(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR *buf;

	if (param == NULL) {
		return -2;
	}
	switch (param->v->type) {
	case TYPE_ARRAY:
		buf = ValueListToString(param->v->u.array);
		StringToNumber(buf, ret);
		mem_free(&buf);
		break;
	case TYPE_STRING:
		StringToNumber(param->v->u.sValue, ret);
		break;
	case TYPE_FLOAT:
		ret->v->u.fValue = param->v->u.fValue;
		ret->v->type = TYPE_FLOAT;
		break;
	default:
		ret->v->u.iValue = param->v->u.iValue;
		ret->v->type = TYPE_INTEGER;
		break;
	}
	return 0;
}

/*
 * _lib_func_int - 整数に変換
 */
int SFUNC _lib_func_int(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR *buf;

	if (param == NULL) {
		return -2;
	}
	switch (param->v->type) {
	case TYPE_ARRAY:
		buf = ValueListToString(param->v->u.array);
		ret->v->u.iValue = _ttoi(buf);
		mem_free(&buf);
		break;
	case TYPE_STRING:
		ret->v->u.iValue = _ttoi(param->v->u.sValue);
		break;
	default:
		ret->v->u.iValue = VariableToInt(param);
		break;
	}
	ret->v->type = TYPE_INTEGER;
	return 0;
}

/*
* indexToChar - 指定インデックスの文字を取得
*/
static TCHAR *indexToChar(TCHAR *p, int index)
{
	for (; *p != TEXT('\0') && index > 0; p++, index--);
	return p;
}

/*
 * _lib_func_code - 1文字目の文字コード取得
 */
int SFUNC _lib_func_code(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *vi = param;
	TCHAR *buf;
	int index = 0;

	if (param == NULL) {
		return -2;
	}
	if ((param = param->next) != NULL) {
		switch (param->v->type) {
		case TYPE_ARRAY:
			buf = ValueListToString(param->v->u.array);
			index = _ttoi(buf);
			mem_free(&buf);
			break;
		case TYPE_STRING:
			index = _ttoi(param->v->u.sValue);
			break;
		default:
			index = VariableToInt(param);
			break;
		}
	}
	switch (vi->v->type) {
	case TYPE_ARRAY:
		buf = ValueListToString(vi->v->u.array);
		ret->v->u.iValue = (int)*indexToChar(buf, index);
		mem_free(&buf);
		break;
	case TYPE_STRING:
		ret->v->u.iValue = (int)*indexToChar(vi->v->u.sValue, index);
		break;
	default:
		buf = VariableToString(vi);
		ret->v->u.iValue = (int)*indexToChar(buf, index);
		mem_free(&buf);
		break;
	}
	ret->v->type = TYPE_INTEGER;
	return 0;
}

/*
 * _lib_func_char - 文字コードを文字に変換
 */
int SFUNC _lib_func_char(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR *buf;
	int code;

	if (param == NULL) {
		return -2;
	}
	switch (param->v->type) {
	case TYPE_ARRAY:
		buf = ValueListToString(param->v->u.array);
		code = _ttoi(buf);
		mem_free(&buf);
		break;
	case TYPE_STRING:
		code = _ttoi(param->v->u.sValue);
		break;
	default:
		code = VariableToInt(param);
		break;
	}
	ret->v->u.sValue = mem_alloc(sizeof(TCHAR) * 2);
	*(ret->v->u.sValue) = (TCHAR)code;
	*(ret->v->u.sValue + 1) = TEXT('\0');
	ret->v->type = TYPE_STRING;
	return 0;
}

/*
 * _lib_func_getkey - 配列のキー名を取得
 * getkey(配列, インデックス) → キー名
 */
int SFUNC _lib_func_getkey(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *vi = param;
	int index;

	if (param == NULL) {
		return -2;
	}
	if ((param = param->next) == NULL) {
		return -2;
	}
	if (vi->v->type != TYPE_ARRAY) {
		ret->v->u.sValue = alloc_copy(TEXT(""));
	} else {
		TCHAR *buf;
		int i;
		switch (param->v->type) {
		case TYPE_ARRAY:
			buf = ValueListToString(param->v->u.array);
			index = _ttoi(buf);
			mem_free(&buf);
			break;
		case TYPE_STRING:
			index = _ttoi(param->v->u.sValue);
			break;
		default:
			index = VariableToInt(param);
			break;
		}
		for (i = 0, vi = vi->v->u.array; i < index && vi != NULL; i++, vi = vi->next);
		if (vi != NULL && vi->name != NULL) {
			ret->v->u.sValue = alloc_copy(vi->name);
		} else {
			ret->v->u.sValue = alloc_copy(TEXT(""));
		}
	}
	ret->v->type = TYPE_STRING;
	return 0;
}

/*
* _lib_func_setkey - 配列のキー名を設定
* setkey(配列, インデックス, キー名)
*/
int SFUNC _lib_func_setkey(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *vi = param;
	TCHAR *buf;
	TCHAR *key = NULL;
	int index;
	int i;

	if (param == NULL) {
		return -2;
	}
	if (vi->v->type != TYPE_ARRAY) {
		return 0;
	}
	if ((param = param->next) == NULL) {
		return -2;
	}
	switch (param->v->type) {
	case TYPE_ARRAY:
		buf = ValueListToString(param->v->u.array);
		index = _ttoi(buf);
		mem_free(&buf);
		break;
	case TYPE_STRING:
		index = _ttoi(param->v->u.sValue);
		break;
	default:
		index = VariableToInt(param);
		break;
	}
	if ((param = param->next) == NULL) {
		return -2;
	}
	for (i = 0, vi = vi->v->u.array; i < index && vi != NULL; i++, vi = vi->next);
	if (vi != NULL) {
		if (param->v->type == TYPE_STRING && *param->v->u.sValue != TEXT('\0')) {
			key = alloc_copy(param->v->u.sValue);
		}
		mem_free(&vi->name);
		mem_free(&vi->org_name);
		vi->name_hash = 0;
		if (key != NULL) {
			vi->name = key;
#ifndef PG0_CMD
			vi->org_name = alloc_copy(key);
#endif
			str_lower(vi->name);
			vi->name_hash = str2hash(vi->name);
		}
	}
	return 0;
}
/* End of source */
