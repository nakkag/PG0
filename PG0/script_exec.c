/*
 * PG0
 *
 * script_exec.c
 *
 * Copyright (C) 1996-2020 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#include <windows.h>
#include <tchar.h>
#include <malloc.h>

#include "script.h"
#include "script_string.h"
#include "script_memory.h"
#include "script_utility.h"

/* Define */
#define ERR_HEAD				TEXT("Error: ")
#define LIB_FUNC_HEAD			TEXT("_lib_func_")
#define ARGUMENT_ADDRESS		TEXT('&')
#define ARGUMENT_VARIABLE		TEXT("arg")

/* Global Variables */
TCHAR err_jp[][BUF_SIZE] = {
	TEXT("構文エラー"),
	TEXT("演算子が必要です"),
	TEXT(" :変数が定義されていません"),
	TEXT(" :変数の宣言が重複しています"),
	TEXT("配列のインデックスが負の値になっています"),
	TEXT("対応する括弧がありません"),
	TEXT("ブロック「{～}」がありません"),
	TEXT("0 で除算をしました"),
	TEXT("不正な演算子です"),
	TEXT("配列に使えない演算子です"),
	TEXT("メモリの確保に失敗しました"),
	TEXT("引数が少なすぎます"),
	TEXT("ファイルオープンに失敗しました"),
	TEXT("スクリプトまたはライブラリの読み込みに失敗しました"),
	TEXT("関数が見つかりません"),
	TEXT("関数実行中にエラーが発生しました"),
};

TCHAR err_en[][BUF_SIZE] = {
	TEXT("Syntax error"),
	TEXT("Operators are required"),
	TEXT(" :Undefined variable"),
	TEXT(" :Duplicate variable declaration"),
	TEXT("Array index is negative value"),
	TEXT("Unbalanced parentheses"),
	TEXT("Block'{...}' expected"),
	TEXT("Division by zero"),
	TEXT("Illegal operator"),
	TEXT("Illegal operator to array"),
	TEXT("Alloc failed"),
	TEXT("Too few arguments"),
	TEXT("File open error"),
	TEXT("Read error in script or library"),
	TEXT("Function not Found"),
	TEXT("Function error"),
};

/* Local Function Prototypes */
static void SetValue(VALUE *to_v, VALUE *from_v);
static VALUEINFO *IndexToArray(EXECINFO *ei, VALUEINFO *pvi, int index);
static VALUEINFO *GetArrayValue(EXECINFO *ei, VALUEINFO *pvi, VALUE *keyv);
static VALUEINFO *DeclVariable(EXECINFO *ei, TCHAR *name, TCHAR *err);
static VALUEINFO *FindValueInfo(VALUEINFO *vi, TCHAR *name);
static VALUEINFO *AddValueInfo(VALUEINFO **vi_root, TCHAR *name, VALUE *v);

static TOKEN *FindCase(EXECINFO *ei, TOKEN *cu_tk, VALUEINFO *v1);
static VALUEINFO *UnaryCalcValue(EXECINFO *ei, VALUEINFO *vi, int c);
static VALUEINFO *IntegerCalcValue(EXECINFO *ei, VALUEINFO *v1, VALUEINFO *v2, int c);
static VALUEINFO *FloatCalcValue(EXECINFO *ei, VALUEINFO *v1, VALUEINFO *v2, int c);
static VALUEINFO *StringCalcValue(EXECINFO *ei, VALUEINFO *v1, VALUEINFO *v2, int c);
static VALUEINFO *ArrayCalcValue(EXECINFO *ei, VALUEINFO *v1, VALUEINFO *v2, int c);
static VALUEINFO *CalcValue(EXECINFO *ei, VALUEINFO *v1, VALUEINFO *v2, int c);

/*
 * InitializeScript - スクリプトの初期化
 */

void InitializeScript()
{
	//関数テーブルの初期化
	InitFuncAddress();
}

/*
 * EndScript - スクリプトの終了処理
 */
void EndScript()
{
}

/*
 * Error - エラー
 */
void Error(EXECINFO *ei, ERROR_CODE err_id, TCHAR *msg, TCHAR *pre_str)
{
	EXECINFO *pei = ei;
	VALUEINFO *vi, *pvi;
	LIBFUNC StdFunc;
	TCHAR buf[BUF_SIZE];
	TCHAR ErrStr[BUF_SIZE];
	TCHAR *err = TEXT("");
	TCHAR *description = TEXT("");
	TCHAR *err_str;
	TCHAR *p, *r, *s, *t = NULL;
	int line = 0;
	int size;
	int i;
	WORD lang;

	for (; pei->parent != NULL; pei = pei->parent);

	// エラーの取得
	lang = PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale()));
	if (lang == LANG_JAPANESE) {
		err = err_jp[err_id];
	} else {
		err = err_en[err_id];
	}
	//行番号の取得
	if (pei->sci != NULL && pei->sci->buf != NULL) {
		p = NULL;
		if (msg != NULL && msg >= pei->sci->buf && msg <= (pei->sci->buf + lstrlen(pei->sci->buf))) {
			p = msg;
		} else if (ei->err != NULL && ei->err >= pei->sci->buf && ei->err <= (pei->sci->buf + lstrlen(pei->sci->buf))) {
			p = ei->err;
		}
		if (p != NULL) {
			i = 1;
			for (r = pei->sci->buf; r < p; r++) {
				if (*r == TEXT('\n')) {
					i++;
				}
			}
			line = i;
		}
	}
	// エラー説明の取得
	if (msg != NULL && pei->sci != NULL && pei->sci->buf != NULL && msg >= pei->sci->buf && msg <= (pei->sci->buf + lstrlen(pei->sci->buf))) {
		for (s = r = pei->sci->buf; r < msg; r++) {
			if (*r == TEXT('\n')) {
				s = r;
			}
		}
		for (; *s == TEXT('\r') || *s == TEXT('\n') || *s == TEXT('\t') || *s == TEXT(' '); s++);
		for (r = s; *r != TEXT('\0') && *r != TEXT('\r') && *r != TEXT('\n'); r++);
		t = alloc_copy_n(s, (int)(r - s));
		description = t;
	} else if (msg != NULL) {
		description = msg;
	}

	// エラーメッセージのサイズ計算
	size = lstrlen(ERR_HEAD);
	if (pei->sci != NULL && pei->sci->name != NULL) {
		size += lstrlen(TEXT("["));
		size += lstrlen(pei->sci->name);
		size += lstrlen(TEXT("]: "));
	}
	if (pre_str != NULL) {
		size += lstrlen(pre_str);
	}
	size += lstrlen(err);
	size += lstrlen(TEXT("("));
	size += wsprintf(buf, TEXT("%u"), line);
	size += lstrlen(TEXT("): "));
	size += lstrlen(description);

	// エラーメッセージの構築
	err_str = mem_alloc(sizeof(TCHAR) * (size + 1));
	if (err_str == NULL) {
		return;
	}
	p = str_cpy(err_str, ERR_HEAD);
	if (pei->sci != NULL && pei->sci->name != NULL) {
		p = str_cpy(p, TEXT("["));
		p = str_cpy(p, pei->sci->name);
		p = str_cpy(p, TEXT("]: "));
	}
	if (pre_str != NULL) {
		p = str_cpy(p, pre_str);
	}
	p = str_cpy(p, err);
	p = str_cpy(p, TEXT("("));
	p = str_cpy(p, buf);
	p = str_cpy(p, TEXT("): "));
	p = str_cpy(p, description);
	if (t != NULL) {
		mem_free(&t);
	}

	//エラーメッセージの出力
	StdFunc = GetFuncAddress(LIB_FUNC_HEAD TEXT("error"));
	if (StdFunc == NULL) {
		mem_free(&err_str);
		return;
	}
	vi = AllocValue();
	if(vi == NULL){
		mem_free(&err_str);
		return;
	}
	pvi = AllocValue();
	if(pvi == NULL){
		FreeValue(vi);
		mem_free(&err_str);
		return;
	}
	pvi->v->u.sValue = err_str;
	pvi->v->type = TYPE_STRING;
	if (pei->sci != NULL && pei->sci == pei->sci->sci_top) {
		pvi->next = AllocValue();
		if (pvi->next == NULL) {
			FreeValue(vi);
			FreeValueList(pvi);
			return;
		}
		pvi->next->v->u.iValue = line;
		pvi->next->v->type = TYPE_INTEGER;
	}

	*ErrStr = TEXT('\0');
	StdFunc(pei, pvi, &vi, ErrStr);
	FreeValue(vi);
	FreeValueList(pvi);
}

/*
 * FreeToken - トークンの解放
 */
void FreeToken(TOKEN *tk)
{
	while (tk != NULL) {
		TOKEN *tmptk = tk->next;
		FreeToken(tk->target);
		mem_free(&(tk->buf));
		mem_free(&tk);
		tk = tmptk;
	}
}

/*
 * FreeFuncAddrList - 関数アドレスリストの解放
 */
void FreeFuncAddrList(FUNCADDRINFO *fa)
{
	while (fa != NULL) {
		FUNCADDRINFO *tmpfa = fa->next;
		mem_free(&(fa->name));
		mem_free(&fa);
		fa = tmpfa;
	}
}

/*
 * FreeExecInfo - 実行情報の解放
 */
void FreeExecInfo(EXECINFO *ei)
{
	if (ei == NULL) return;
	FreeValueList(ei->vi);
	ei->vi = NULL;
	FreeFuncAddrList(ei->funcaddr);
	ei->funcaddr = NULL;
	FreeValueList(ei->inc_vi);
	ei->inc_vi = NULL;
	FreeValueList(ei->dec_vi);
	ei->dec_vi = NULL;
	ei->name = NULL;
}

/*
 * FreeFuncInfo - 関数情報の解放
 */
void FreeFuncInfo(FUNCINFO *fi)
{
	if (fi == NULL) return;
	FreeFuncInfo(fi->next);

	mem_free(&(fi->name));
	mem_free(&fi);
}

/*
 * FreeLibInfo - ライブラリ情報の解放
 */
void FreeLibInfo(LIBRARYINFO *lib)
{
	if (lib == NULL) return;
	FreeLibInfo(lib->next);

	FreeLibrary(lib->hModul);
	mem_free(&lib);
}

/*
 * FreeScriptInfo - スクリプト情報の解放
 */
void FreeScriptInfo(SCRIPTINFO *sci)
{
	if (sci == NULL) return;
	FreeScriptInfo(sci->next);

	mem_free(&sci->name);
	mem_free(&sci->path);
	mem_free(&sci->buf);

	FreeToken(sci->tk);
	FreeFuncInfo(sci->fi);
	FreeLibInfo(sci->lib);
	FreeExecInfo(sci->ei);
	mem_free(&sci->ei);

	mem_free(&sci);
}

/*
 * InitializeScriptInfo - スクリプト情報の初期化
 */
void InitializeScriptInfo(SCRIPTINFO *sci, BOOL op_exp, BOOL op_extension)
{
	ZeroMemory(sci, sizeof(SCRIPTINFO));
	sci->sci_top = sci;
	sci->strict_val_op = op_exp;
	sci->strict_val = op_exp;
	sci->extension = op_extension;
}

/*
 * SetValue - 値の設定
 */
static void SetValue(VALUE *to_v, VALUE *from_v)
{
	VALUE tmp_v = *to_v;
	VALUE_TYPE type = from_v->type;

	switch (from_v->type) {
	case TYPE_ARRAY:
		// 配列
		to_v->u.array = CopyValueList(from_v->u.array);
		break;
	case TYPE_STRING:
		// 文字列
		to_v->u.sValue = alloc_copy(from_v->u.sValue);
		break;
	case TYPE_FLOAT:
		// 実数
		if (from_v->u.fValue == (double)((int)from_v->u.fValue)) {
			to_v->u.iValue = (int)from_v->u.fValue;
			type = TYPE_INTEGER;
		} else {
			to_v->u.fValue = from_v->u.fValue;
		}
		break;
	default:
		// 整数
		to_v->u.iValue = from_v->u.iValue;
		break;
	}
	to_v->type = type;

	if (tmp_v.type == TYPE_ARRAY) {
		FreeValueList(tmp_v.u.array);
	} else if (tmp_v.type == TYPE_STRING) {
		mem_free(&tmp_v.u.sValue);
	}
}

/*
 * IndexToArray - インデックスで配列要素を取得
 */
static VALUEINFO *IndexToArray(EXECINFO *ei, VALUEINFO *pvi, int index)
{
	VALUEINFO *vi = pvi;
	VALUEINFO *topvi;
	int i = 0;

	if (index < 0) {
		//不正なインデックス
		Error(ei, ERR_INDEX, ei->err, NULL);
		return NULL;
	}

	if (vi->v->type != TYPE_ARRAY) {
		if (vi->v->type == TYPE_STRING) {
			mem_free(&vi->v->u.sValue);
		}
		// 0 番目の追加
		vi->v->type = TYPE_ARRAY;
		vi = vi->v->u.array = AllocValue();
		if (vi == NULL) {
			Error(ei, ERR_ALLOC, ei->err, NULL);
			return NULL;
		}
		i = 1;
	}
	else if (vi->v->u.array == NULL) {
		vi->v->u.array = AllocValue();
		vi = vi->v->u.array;
		i++;
	} else {
		VALUEINFO *tmpvi = NULL;
		//位置検索
		for (vi = vi->v->u.array; vi != NULL; vi = vi->next, i++) {
			if (i == index) {
				return vi;
			}
			tmpvi = vi;
		}
		vi = tmpvi;
		if (vi == NULL) {
			Error(ei, ERR_INDEX, ei->err, NULL);
			return NULL;
		}
	}
	//延長
	topvi = vi;
	for (; i <= index; i++) {
		vi = vi->next = AllocValue();
		if (vi == NULL) {
			FreeValueList(topvi->next);
			topvi->next = NULL;
			Error(ei, ERR_ALLOC, ei->err, NULL);
			return NULL;
		}
	}
	return vi;
}

/*
 * GetArrayValue - 配列の取得
 */
static VALUEINFO *GetArrayValue(EXECINFO *ei, VALUEINFO *pvi, VALUE *keyv)
{
	VALUEINFO *vi = pvi;
	VALUEINFO *kvi = NULL;
	TCHAR *key;
	int name_hash;
	int i = 0;

	if (keyv->type != TYPE_STRING) {
		//インデックスで参照
		return IndexToArray(ei, pvi, GetValueInt(keyv));
	}

	//キーで参照
	key = keyv->u.sValue;
	TCHAR* tmp_key = _alloca(sizeof(TCHAR) * (lstrlen(key) + 1));
	if (tmp_key == NULL) {
		return NULL;
	}
	lstrcpy(tmp_key, key);
	str_lower(tmp_key);
	name_hash = str2hash(tmp_key);

	if (vi->v->type != TYPE_ARRAY) {
		if (vi->v->type == TYPE_STRING) {
			mem_free(&vi->v->u.sValue);
		}
		//新規追加
		vi->v->type = TYPE_ARRAY;
		vi = vi->v->u.array = AllocValue();
		if (vi == NULL) {
			Error(ei, ERR_ALLOC, ei->err, NULL);
			return NULL;
		}
		vi->name = alloc_copy(tmp_key);
#ifndef PG0_CMD
		vi->org_name = alloc_copy(key);
#endif
		vi->name_hash = name_hash;
		return vi;
	}

	//検索
	for (vi = vi->v->u.array; vi != NULL; vi = vi->next) {
		if (vi->name != NULL &&
			name_hash == vi->name_hash && lstrcmp(tmp_key, vi->name) == 0) {
			break;
		}
		kvi = vi;
	}
	if (vi == NULL) {
		//キーを追加
		vi = kvi->next = AllocValue();
		if (vi == NULL) {
			Error(ei, ERR_ALLOC, ei->err, NULL);
			return NULL;
		}
		vi->name = alloc_copy(tmp_key);
#ifndef PG0_CMD
		vi->org_name = alloc_copy(key);
#endif
		if (vi->name != NULL) {
			str_lower(vi->name);
		}
		vi->name_hash = name_hash;
	}
	return vi;
}

/*
 * GetVariable - 変数を取得
 */
VALUEINFO *GetVariable(EXECINFO *ei, TCHAR *name)
{
	VALUEINFO *vi = NULL;

	//変数の検索
	for (; ei != NULL; ei = ei->parent) {
		vi = FindValueInfo(ei->vi, name);
		if (vi != NULL) {
			break;
		}
	}
	return vi;
}

/*
 * SetVariable - 変数を設定
 */
BOOL SetVariable(EXECINFO *ei, TCHAR *name, VALUE *v)
{
	VALUEINFO *vi;

	//変数の設定
	vi = GetVariable(ei, name);
	if (vi != NULL) {
		SetValue(vi->v, v);
		return TRUE;
	}
	return ((AddValueInfo(&(ei->vi), name, v) == NULL) ? FALSE : TRUE);
}

/*
 * DeclVariable - 変数定義
 */
static VALUEINFO *DeclVariable(EXECINFO *ei, TCHAR *name, TCHAR *err)
{
	VALUEINFO *ret;

	if (*name == ARGUMENT_ADDRESS) {
		Error(ei, ERR_SENTENCE, err, NULL);
		return NULL;
	}
	if (FindValueInfo(ei->vi, name) != NULL) {
		//変数の重複定義
		Error(ei, ERR_DECLARE, err, name);
		return NULL;
	}
	ret = AddValueInfo(&(ei->vi), name, NULL);
	if (ret == NULL) {
		Error(ei, ERR_ALLOC, err, NULL);
		return NULL;
	}
	return ret;
}

/*
 * FindValueInfo - 変数の検索
 */
static VALUEINFO *FindValueInfo(VALUEINFO *vi, TCHAR *name)
{
	int name_hash;
	TCHAR *tmp_name;

	tmp_name = _alloca(sizeof(TCHAR) * (lstrlen(name) + 1));
	if (tmp_name == NULL) {
		return NULL;
	}
	lstrcpy(tmp_name, name);
	str_lower(tmp_name);
	name_hash = str2hash(tmp_name);
	for (; vi != NULL; vi = vi->next) {
		if (name_hash == vi->name_hash && lstrcmp(tmp_name, vi->name) == 0) {
			return vi;
		}
	}
	return NULL;
}

/*
 * AddValueInfo - 変数の追加
 */
static VALUEINFO *AddValueInfo(VALUEINFO **vi_root, TCHAR *name, VALUE *v)
{
	VALUEINFO *vi;

	if (*vi_root == NULL) {
		vi = *vi_root = AllocValue();
	} else {
		for (vi = *vi_root; vi->next != NULL; vi = vi->next);
		vi = vi->next = AllocValue();
	}
	if (vi == NULL) {
		return NULL;
	}
	vi->name = alloc_copy(name);
	if (vi->name != NULL) {
		str_lower(vi->name);
	}
	vi->name_hash = str2hash(vi->name);
#ifndef PG0_CMD
	vi->org_name = alloc_copy(name);
#endif
	if (v == NULL) {
		vi->v->u.iValue = 0;
		vi->v->type = TYPE_INTEGER;
	} else {
		SetValue(vi->v, v);
	}
	return vi;
}

/*
 * FindCase - case 項目の検索
 */
static TOKEN *FindCase(EXECINFO *ei, TOKEN *cu_tk, VALUEINFO *v1)
{
	VALUEINFO *vi, *v2;
	TOKEN *tmp_tk;
	BOOL cp;

	//case 項目の検索
	for (tmp_tk = cu_tk->target; tmp_tk != NULL; tmp_tk = tmp_tk->next) {
		if (tmp_tk->sym_type != SYM_CASE) {
			continue;
		}
		v2 = NULL;
		ei->to_tk = SYM_LABELEND;
		if (ExecSentense(ei, tmp_tk->next, NULL, &v2) == -1) {
			return NULL;
		}
		ei->to_tk = 0;

		//スタックの内容を取り出す
		if (v2 == NULL) {
			return NULL;
		}

		//比較
		vi = CalcValue(ei, v2, v1, SYM_EQEQ);
		FreeValue(v2);
		if (vi == NULL) {
			return NULL;
		}
		cp = GetValueBoolean(vi->v);
		FreeValue(vi);
		if (cp != FALSE) {
			//マッチ
			break;
		}
	}
	//default の検索
	if (tmp_tk == NULL) {
		for (tmp_tk = cu_tk->target; tmp_tk != NULL &&
			tmp_tk->sym_type != SYM_DEFAULT; tmp_tk = tmp_tk->next);
	}
	return tmp_tk;
}

/*
* UnaryCalcValueFloat - 単項演算子の計算(Float)
*/
static VALUEINFO *UnaryCalcValueFloat(EXECINFO *ei, VALUEINFO *vi, int c)
{
	VALUEINFO *vret;
	double f;

	f = GetValueFloat(vi->v);
	switch (c) {
	case SYM_NOT:
		f = !f;
		break;

	case SYM_PLUS:
		f = +f;
		break;

	case SYM_MINS:
		f = -f;
		break;

	case SYM_BITNOT:
		f = ~(int)f;
		break;

	case SYM_INC:
		f++;
		vi->v->u.fValue = f;
		break;

	case SYM_DEC:
		f--;
		vi->v->u.fValue = f;
		break;

	case SYM_BINC:
	case SYM_BDEC:
		if (vi == vi->v->vi) {
			return vi;
		}
		vret = AllocValue();
		if (vret == NULL) {
			Error(ei, ERR_ALLOC, ei->err, NULL);
			return NULL;
		}
		mem_free(&vret->v);
		vret->v = vi->v;

		switch (c) {
		case SYM_BINC:
			vret->next = ei->inc_vi;
			ei->inc_vi = vret;
			break;
		case SYM_BDEC:
			vret->next = ei->dec_vi;
			ei->dec_vi = vret;
			break;
		}
		return vi;
	}
	vret = AllocValue();
	if (vret == NULL) {
		Error(ei, ERR_ALLOC, ei->err, NULL);
		return NULL;
	}
	if (f == (double)((int)f)) {
		vret->v->u.iValue = (int)f;
		vret->v->type = TYPE_INTEGER;
	} else {
		vret->v->u.fValue = f;
		vret->v->type = TYPE_FLOAT;
	}
	return vret;
}

/*
 * UnaryCalcValue - 単項演算子の計算
 */
static VALUEINFO *UnaryCalcValue(EXECINFO *ei, VALUEINFO *vi, int c)
{
	VALUEINFO *vret;
	int i;

	i = GetValueInt(vi->v);
	switch (c) {
	case SYM_NOT:
		i = !i;
		break;

	case SYM_PLUS:
		i = +i;
		break;

	case SYM_MINS:
		i = -i;
		break;

	case SYM_BITNOT:
		i = ~i;
		break;

	case SYM_INC:
		i++;
		vi->v->u.iValue = i;
		break;

	case SYM_DEC:
		i--;
		vi->v->u.iValue = i;
		break;

	case SYM_BINC:
	case SYM_BDEC:
		if (vi == vi->v->vi) {
			return vi;
		}
		vret = AllocValue();
		if (vret == NULL) {
			Error(ei, ERR_ALLOC, ei->err, NULL);
			return NULL;
		}
		mem_free(&vret->v);
		vret->v = vi->v;

		switch (c) {
		case SYM_BINC:
			vret->next = ei->inc_vi;
			ei->inc_vi = vret;
			break;
		case SYM_BDEC:
			vret->next = ei->dec_vi;
			ei->dec_vi = vret;
			break;
		}
		return vi;
	}
	vret = AllocValue();
	if (vret == NULL) {
		Error(ei, ERR_ALLOC, ei->err, NULL);
		return NULL;
	}
	vret->v->u.iValue = i;
	vret->v->type = TYPE_INTEGER;
	return vret;
}

/*
 * UnaryCalcString - 単項演算子の計算(String)
 */
static VALUEINFO* UnaryCalcString(EXECINFO* ei, VALUEINFO* vi, int c)
{
	VALUEINFO* vret;
	int i;

	i = 0;
	switch (c) {
	case SYM_NOT:
		if (vi->v->u.sValue[0] == TEXT('\0')) {
			i = 1;
		}
		else {
			i = 0;
		}
		break;
	}
	vret = AllocValue();
	if (vret == NULL) {
		Error(ei, ERR_ALLOC, ei->err, NULL);
		return NULL;
	}
	vret->v->u.iValue = i;
	vret->v->type = TYPE_INTEGER;
	return vret;
}

/*
 * IntegerCalcValue - 整数の計算
 */
static VALUEINFO *IntegerCalcValue(EXECINFO *ei, VALUEINFO *v1, VALUEINFO *v2, int c)
{
	VALUEINFO *vret;
	int i = 0, j;

	i = v1->v->u.iValue;
	j = v2->v->u.iValue;
	switch (c) {
	case SYM_DIV:
	case SYM_MOD:
		if (j == 0) {
			Error(ei, ERR_DIVZERO, ei->err, NULL);
			return NULL;
		}
		if (c == SYM_DIV) {
			if (ei->sci->extension == TRUE && (i % j) != 0) {
				return FloatCalcValue(ei, v1, v2, c);
			}
			i /= j;
		} else {
			i %= j;
		}
		break;

	case SYM_MULTI: i *= j; break;
	case SYM_ADD: i += j; break;
	case SYM_SUB: i -= j; break;
	case SYM_EQEQ: i = i == j; break;
	case SYM_NTEQ: i = i != j; break;
	case SYM_LEFTEQ: i = i <= j; break;
	case SYM_LEFT: i = i < j; break;
	case SYM_RIGHTEQ: i = i >= j; break;
	case SYM_RIGHT: i = i > j; break;
	case SYM_AND: i &= j; break;
	case SYM_OR: i |= j; break;
	case SYM_XOR: i ^= j; break;
	case SYM_LEFTSHIFT: i = (int)i << j; break;
	case SYM_RIGHTSHIFT: i = (int)i >> j; break;
	case SYM_LEFTSHIFT_LOGICAL: i = (unsigned int)i << j; break;
	case SYM_RIGHTSHIFT_LOGICAL: i = (unsigned int)i >> j; break;

	default:
		Error(ei, ERR_OPERATOR, ei->err, NULL);
		return NULL;
	}

	vret = AllocValue();
	if (vret == NULL) {
		Error(ei, ERR_ALLOC, ei->err, NULL);
		return NULL;
	}
	vret->v->u.iValue = i;
	vret->v->type = TYPE_INTEGER;
	return vret;
}

/*
 * FloatCalcValue - 実数の計算
 */
static VALUEINFO *FloatCalcValue(EXECINFO *ei, VALUEINFO *v1, VALUEINFO *v2, int c)
{
	VALUEINFO *vret;
	double i = 0, j;

	if (v1->v->type == TYPE_INTEGER) {
		i = (double)v1->v->u.iValue;
	} else {
		i = v1->v->u.fValue;
	}
	if (v2->v->type == TYPE_INTEGER) {
		j = (double)v2->v->u.iValue;
	} else {
		j = v2->v->u.fValue;
	}
	switch (c) {
	case SYM_DIV:
	case SYM_MOD:
		if (j == 0) {
			Error(ei, ERR_DIVZERO, ei->err, NULL);
			return NULL;
		}
		if (c == SYM_DIV) {
			i /= j;
		} else {
			i = (int)i % (int)j;
		}
		break;

	case SYM_MULTI: i *= j; break;
	case SYM_ADD: i += j; break;
	case SYM_SUB: i -= j; break;
	case SYM_EQEQ: i = i == j; break;
	case SYM_NTEQ: i = i != j; break;
	case SYM_LEFTEQ: i = i <= j; break;
	case SYM_LEFT: i = i < j; break;
	case SYM_RIGHTEQ: i = i >= j; break;
	case SYM_RIGHT: i = i > j; break;
	case SYM_AND: i = (int)i & (int)j; break;
	case SYM_OR: i = (int)i | (int)j; break;
	case SYM_XOR: i = (int)i ^ (int)j; break;
	case SYM_LEFTSHIFT: i = (int)i << (int)j; break;
	case SYM_RIGHTSHIFT: i = (int)i >> (int)j; break;
	case SYM_LEFTSHIFT_LOGICAL: i = (unsigned int)i << (int)j; break;
	case SYM_RIGHTSHIFT_LOGICAL: i = (unsigned int)i >> (int)j; break;

	default:
		Error(ei, ERR_OPERATOR, ei->err, NULL);
		return NULL;
	}

	vret = AllocValue();
	if (vret == NULL) {
		Error(ei, ERR_ALLOC, ei->err, NULL);
		return NULL;
	}
	if (i == (double)((int)i)) {
		vret->v->u.iValue = (int)i;
		vret->v->type = TYPE_INTEGER;
	} else {
		vret->v->u.fValue = i;
		vret->v->type = TYPE_FLOAT;
	}
	return vret;
}

/*
 * StringCalcValue - 文字列型の計算
 */
static VALUEINFO *StringCalcValue(EXECINFO *ei, VALUEINFO *v1, VALUEINFO *v2, int c)
{
	VALUEINFO *vret;
	TCHAR *p, *s;
	TCHAR *p1, *p2;
	TCHAR *f1 = NULL, *f2 = NULL;
	int i = 0;

	if (v1->v->type == TYPE_FLOAT) {
		f1 = p1 = f2a(v1->v->u.fValue);
	} else if (v1->v->type == TYPE_INTEGER) {
		f1 = p1 = i2a(v1->v->u.iValue);
	} else {
		p1 = v1->v->u.sValue;
	}
	if (v2->v->type == TYPE_FLOAT) {
		f2 = p2 = f2a(v2->v->u.fValue);
	} else if (v2->v->type == TYPE_INTEGER) {
		f2 = p2 = i2a(v2->v->u.iValue);
	} else {
		p2 = v2->v->u.sValue;
	}

	switch (c) {
	case SYM_ADD:
		//文字列結合
		p = mem_alloc(sizeof(TCHAR) * (lstrlen(p1) + lstrlen(p2) + 1));
		if (p == NULL) {
			Error(ei, ERR_ALLOC, ei->err, NULL);
			mem_free(&f1);
			mem_free(&f2);
			return NULL;
		}
		s = str_cpy(p, p1);
		s = str_cpy(s, p2);

		vret = AllocValue();
		if (vret == NULL) {
			Error(ei, ERR_ALLOC, ei->err, NULL);
			mem_free(&f1);
			mem_free(&f2);
			mem_free(&p);
			return NULL;
		}
		vret->v->u.sValue = p;
		vret->v->type = TYPE_STRING;
		mem_free(&f1);
		mem_free(&f2);
		return vret;

	case SYM_EQEQ:
		i = (*p1 == *p2 && lstrcmp(p1, p2) == 0) ? 1 : 0;
		break;

	case SYM_NTEQ:
		i = (*p1 == *p2 && lstrcmp(p1, p2) == 0) ? 0 : 1;
		break;

	default:
		Error(ei, ERR_OPERATOR, ei->err, NULL);
		mem_free(&f1);
		mem_free(&f2);
		return NULL;
	}
	mem_free(&f1);
	mem_free(&f2);

	vret = AllocValue();
	if (vret == NULL) {
		Error(ei, ERR_ALLOC, ei->err, NULL);
		return NULL;
	}
	vret->v->u.iValue = i;
	vret->v->type = TYPE_INTEGER;
	return vret;
}

/*
 * ArrayCalcValue - 配列の計算
 */
static VALUEINFO *ArrayCalcValue(EXECINFO *ei, VALUEINFO *v1, VALUEINFO *v2, int c)
{
	VALUEINFO *vret;
	VALUEINFO *vi;
	int result = 0;

	switch (c) {
	case SYM_ADD:
		//配列の連結
		vret = AllocValue();
		if (vret == NULL) {
			Error(ei, ERR_ALLOC, ei->err, NULL);
			return NULL;
		}
		if (v1->v->type == TYPE_ARRAY) {
			vret->v->u.array = CopyValueList(v1->v->u.array);
			vret->v->type = TYPE_ARRAY;
			if (v2->v->type == TYPE_ARRAY) {
				//連結
				for (vi = vret->v->u.array; vi->next != NULL; vi = vi->next);
				vi->next = CopyValueList(v2->v->u.array);
			}
		} else {
			vret->v->u.array = CopyValueList(v2->v->u.array);
			vret->v->type = TYPE_ARRAY;
		}
		return vret;

	case SYM_EQEQ:
	case SYM_NTEQ:
		//配列の内容比較
		if (v1->v->type == TYPE_ARRAY && v2->v->type == TYPE_ARRAY) {
			for (v1 = v1->v->u.array, v2 = v2->v->u.array; v1 != NULL && v2 != NULL; v1 = v1->next, v2 = v2->next) {
				BOOL cp;
				vi = CalcValue(ei, v1, v2, SYM_EQEQ);
				cp = GetValueBoolean(vi->v);
				FreeValue(vi);
				if (cp == FALSE) {
					break;
				}
			}
		}
		result = (v1 == NULL && v2 == NULL) ? 1 : 0;
		if (c == SYM_NTEQ) {
			result = !result;
		}
		break;

	default:
		Error(ei, ERR_ARRAYOPERATOR, ei->err, NULL);
		return NULL;
	}

	vret = AllocValue();
	if (vret == NULL) {
		Error(ei, ERR_ALLOC, ei->err, NULL);
		return NULL;
	}
	vret->v->u.iValue = result;
	vret->v->type = TYPE_INTEGER;
	return vret;
}

/*
 * CalcValue - 二項演算子の計算
 */
static VALUEINFO *CalcValue(EXECINFO *ei, VALUEINFO *v1, VALUEINFO *v2, int c)
{
	if (v1->v->type == TYPE_ARRAY || v2->v->type == TYPE_ARRAY) {
		// 配列の計算
		return ArrayCalcValue(ei, v1, v2, c);
	} else if (v1->v->type == TYPE_STRING || v2->v->type == TYPE_STRING) {
		// 文字列型の計算
		return StringCalcValue(ei, v1, v2, c);
	} else if (v1->v->type == TYPE_FLOAT || v2->v->type == TYPE_FLOAT) {
		// 実数の計算
		return FloatCalcValue(ei, v1, v2, c);
	} else {
		// 整数の計算
		return IntegerCalcValue(ei, v1, v2, c);
	}
}

/*
 * PostfixValue - 後置式の計算
 */
static BOOL PostfixValue(EXECINFO *ei)
{
	VALUEINFO *vi;

	while (ei->inc_vi != NULL) {
		vi = ei->inc_vi;
		ei->inc_vi = ei->inc_vi->next;
		if (vi->v->type == TYPE_FLOAT) {
			vi->v->u.fValue = GetValueFloat(vi->v) + 1;
		} else if (vi->v->type == TYPE_INTEGER) {
			vi->v->u.iValue = GetValueInt(vi->v) + 1;
		} else {
			FreeValue(vi);
			Error(ei, ERR_OPERATOR, ei->err, NULL);
			return FALSE;
		}
		FreeValue(vi);
	}
	while (ei->dec_vi != NULL) {
		vi = ei->dec_vi;
		ei->dec_vi = ei->dec_vi->next;
		if (vi->v->type == TYPE_FLOAT) {
			vi->v->u.fValue = GetValueFloat(vi->v) - 1;
		} else if (vi->v->type == TYPE_INTEGER) {
			vi->v->u.iValue = GetValueInt(vi->v) - 1;
		} else {
			FreeValue(vi);
			Error(ei, ERR_OPERATOR, ei->err, NULL);
			return FALSE;
		}
		FreeValue(vi);
	}
	return TRUE;
}

/*
 * ExecSentense - 解析木の実行
 */
int ExecSentense(EXECINFO *ei, TOKEN *cu_tk, VALUEINFO **retvi, VALUEINFO **retstack)
{
	EXECINFO cei;
	VALUEINFO *vi, *v1, *v2;
	VALUEINFO *stack = NULL;
	TOKEN *tmp_tk;
	int RetSt = RET_SUCCESS;
	BOOL cp;

	while (cu_tk != NULL && cu_tk->sym_type != ei->to_tk) {
		ei->err = cu_tk->err;
		if (ei->sci->callback != NULL) {
			if (ei->sci->callback(ei, cu_tk) != 0) {
				RetSt = RET_EXIT;
				break;
			}
		}

		switch (cu_tk->sym_type) {
		case SYM_BOPEN:
		case SYM_BOPEN_PRIMARY:
			if (PostfixValue(ei) == FALSE) {
				RetSt = RET_ERROR;
				break;
			}
			ZeroMemory(&cei, sizeof(EXECINFO));
			cei.sci = ei->sci;
			cei.parent = ei;
			vi = NULL;
			RetSt = ExecSentense(&cei, cu_tk->target, retvi, &vi);
			if (RetSt == RET_BREAK || RetSt == RET_CONTINUE) {
				ei->err = cei.err;
			}
			//実行後のスタックの内容を配列に設定
			v1 = AllocValue();
			if (v1 == NULL) {
				Error(ei, ERR_ALLOC, ei->err, NULL);
				RetSt = RET_ERROR;
				FreeValueList(vi);
				FreeExecInfo(&cei);
				break;
			}
			if (vi != NULL) {
				v1->v->u.array = CopyValueList(vi);
			}
			v1->v->type = TYPE_ARRAY;
			v1->next = stack;
			stack = v1;
			FreeValueList(vi);
			FreeExecInfo(&cei);
			break;

		case SYM_BCLOSE:
		case SYM_DAMMY:
			break;

		case SYM_LINEEND:
			if (PostfixValue(ei) == FALSE) {
				RetSt = RET_ERROR;
			}
			if (ei->line_mode == FALSE) {
				//スタックを解放する
				FreeValueList(stack);
				stack = NULL;
			}
			break;

		case SYM_LINESEP:
			if (PostfixValue(ei) == FALSE) {
				RetSt = RET_ERROR;
			}
			//スタックを解放する
			FreeValueList(stack);
			stack = NULL;
			break;

		case SYM_WORDEND:
			if (stack == NULL) {
				//スタックに空の値を追加
				stack = AllocValue();
				if (stack == NULL) {
					Error(ei, ERR_ALLOC, ei->err, NULL);
					RetSt = RET_ERROR;
					break;
				}
			}
			if (cu_tk->next != NULL && cu_tk->next->sym_type == SYM_WORDEND) {
				//スタックに空の値を追加
				vi = AllocValue();
				if (vi == NULL) {
					Error(ei, ERR_ALLOC, ei->err, NULL);
					RetSt = RET_ERROR;
					break;
				}
				vi->next = stack;
				stack = vi;
			}
			break;

		case SYM_JUMP:
			//移動
			cu_tk = cu_tk->link;
			if (ei->sci->callback != NULL) {
				if (ei->sci->callback(ei, cu_tk) != 0) {
					RetSt = RET_EXIT;
					break;
				}
			}
			break;

		case SYM_JZE:
		case SYM_JNZ:
			if (stack == NULL) {
				Error(ei, ERR_SENTENCE, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			cp = GetValueBoolean(stack->v);
			if ((cu_tk->sym_type == SYM_JZE && cp == FALSE) ||
				(cu_tk->sym_type == SYM_JNZ && cp != FALSE)) {
				// スタックに結果を積む
				vi = AllocValue();
				if (vi == NULL) {
					Error(ei, ERR_ALLOC, ei->err, NULL);
					RetSt = RET_ERROR;
					break;
				}
				vi->v->u.iValue = (cp == FALSE) ? 0 : 1;
				vi->v->type = TYPE_INTEGER;
				vi->next = stack->next;
				// 元の値は解放
				FreeValue(stack);
				stack = vi;
				// 式のスキップ
				cu_tk = cu_tk->link;
				if (ei->sci->callback != NULL) {
					if (ei->sci->callback(ei, cu_tk) != 0) {
						RetSt = RET_EXIT;
						break;
					}
				}
			}
			break;

		case SYM_RETURN:
		case SYM_EXIT:
			if (cu_tk->sym_type == SYM_EXIT) {
				RetSt = RET_EXIT;
			} else {
				RetSt = RET_RETURN;
			}
			if (retvi == NULL) {
				break;
			}
			if (stack == NULL) {
				break;
			}
			//戻り値
			*retvi = AllocValue();
			if (*retvi == NULL) {
				Error(ei, ERR_ALLOC, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			vi = stack;
			stack = stack->next;
			SetValue((*retvi)->v, vi->v);
			FreeValue(vi);
			break;

		case SYM_BREAK:
			//中断
			RetSt = RET_BREAK;
			break;

		case SYM_CONTINUE:
			//継続
			RetSt = RET_CONTINUE;
			break;

		case SYM_CASE:
		case SYM_DEFAULT:
			for (; cu_tk->next != NULL && cu_tk->sym_type != SYM_LABELEND; cu_tk = cu_tk->next);
			break;

		case SYM_CMP:
			//比較
			if (stack == NULL) {
				cu_tk = cu_tk->next;
				break;
			}
			//比較結果
			vi = stack;
			stack = stack->next;
			cp = GetValueBoolean(vi->v);
			FreeValue(vi);
			if (cp != FALSE) {
				//真
				//jump ELSE をスキップ
				cu_tk = cu_tk->next;
			}
			break;

		case SYM_CMPSTART:
		case SYM_ELSE:
		case SYM_CMPEND:
			break;

		case SYM_SWITCH:
			//多岐分岐
			if (stack == NULL) {
				Error(ei, ERR_SENTENCE, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			vi = stack;
			stack = stack->next;

			//case 項目の検索
			cu_tk = cu_tk->next;
			tmp_tk = FindCase(ei, cu_tk, vi);
			FreeValue(vi);
			if (tmp_tk == NULL) {
				//マッチする case 項目が無い
				break;
			}
			//文の実行
			ZeroMemory(&cei, sizeof(EXECINFO));
			cei.sci = ei->sci;
			cei.parent = ei;
			RetSt = ExecSentense(&cei, tmp_tk, retvi, NULL);
			FreeExecInfo(&cei);
			if (RetSt != RET_SUCCESS && RetSt != RET_BREAK) {
				break;
			}
			RetSt = RET_SUCCESS;
			break;

		case SYM_LOOP:
			//繰り返し
			if (stack != NULL) {
				//比較結果
				vi = stack;
				stack = stack->next;
				cp = GetValueBoolean(vi->v);
				FreeValue(vi);
				if (cp == FALSE) {
					//jump LOOPEND
					break;
				}
			}
			//ループ対象処理
			RetSt = ExecSentense(ei, cu_tk->target, retvi, NULL);
			if (RetSt != RET_SUCCESS && RetSt != RET_BREAK && RetSt != RET_CONTINUE) {
				break;
			}
			if (RetSt == RET_BREAK) {
				//ループを中断
				for (; cu_tk->next != NULL && cu_tk->sym_type != SYM_LOOPEND; cu_tk = cu_tk->next);
				RetSt = RET_SUCCESS;
				break;
			}
			//継続
			cu_tk = cu_tk->next;
			RetSt = RET_SUCCESS;
			//jump LOOPSTART
			break;

		case SYM_LOOPEND:
		case SYM_LOOPSTART:
			break;

		case SYM_ARGSTART:
			//引数の開始位置
			vi = AllocValue();
			if (vi == NULL) {
				Error(ei, ERR_ALLOC, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			// 引数開始位置は値を空にして識別
			mem_free(&vi->v);
			vi->next = stack;
			stack = vi;
			break;

		case SYM_FUNC:
			//関数呼び出し
			if (stack == NULL) {
				Error(ei, ERR_SENTENCE, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			//引数の取得
			v1 = NULL;
			while (stack != NULL && stack->v != NULL) {
				vi = stack;
				stack = stack->next;
				vi->next = v1;
				v1 = vi;
			}
			//引数開始位置の解放
			vi = stack;
			stack = stack->next;
			FreeValue(vi);

			v2 = ExecFunction(ei, cu_tk->buf, v1);
			FreeValueList(v1);
			if (v2 == NULL || v2 == (VALUEINFO *)-1) {
				RetSt = RET_ERROR;
				break;
			}
			if (ei->exit == TRUE) {
				RetSt = RET_EXIT;
				if (retvi != NULL) {
					//戻り値
					*retvi = v2;
					break;
				}
			}
			//戻り値をスタックに積む
			v2->next = stack;
			stack = v2;
			break;

		case SYM_DECLVARIABLE:
			//変数定義
			v1 = DeclVariable(ei, cu_tk->buf, ei->err);
			if (v1 == NULL) {
				RetSt = RET_ERROR;
				break;
			}

			vi = AllocValue();
			if (vi == NULL) {
				Error(ei, ERR_ALLOC, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			mem_free(&vi->v);
			vi->v = v1->v;
			vi->next = stack;
			stack = vi;
			break;

		case SYM_VARIABLE:
			//変数取得
			v1 = GetVariable(ei, cu_tk->buf);
			if (v1 == NULL) {
				if (ei->sci->strict_val == TRUE) {
					//変数が未定義
					Error(ei, ERR_NOTDECLARE, ei->err, cu_tk->buf);
					RetSt = RET_ERROR;
					break;
				}
				//変数を定義
				v1 = DeclVariable(ei, cu_tk->buf, ei->err);
				if (v1 == NULL) {
					RetSt = RET_ERROR;
					break;
				}
			}
			vi = AllocValue();
			if (vi == NULL) {
				Error(ei, ERR_ALLOC, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			if (retstack != NULL) {
				vi->name = alloc_copy(v1->name);
				vi->name_hash = v1->name_hash;
			}
			mem_free(&vi->v);
			vi->v = v1->v;
			vi->next = stack;
			stack = vi;
			break;

		case SYM_ARRAY:
			//配列
			if (stack == NULL || stack->next == NULL) {
				Error(ei, ERR_SENTENCE, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			v1 = stack;
			stack = stack->next;
			v2 = stack;
			stack = stack->next;

			vi = GetArrayValue(ei, v2->v->vi, v1->v);
			if (vi == NULL) {
				FreeValue(v1);
				FreeValue(v2);
				RetSt = RET_ERROR;
				break;
			}
			FreeValue(v1);

			v1 = AllocValue();
			if (v1 == NULL) {
				FreeValue(v2);
				Error(ei, ERR_ALLOC, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			if (v2->v != NULL && v2->v->vi == v2) {
				//定数の場合はコピー
				SetValue(v1->v, vi->v);
			} else {
				//変数内の値の参照
				mem_free(&v1->v);
				v1->v = vi->v;
			}
			v1->next = stack;
			stack = v1;
			FreeValue(v2);
			break;

		case SYM_CONST_INT:
			// 定数(整数)
			vi = AllocValue();
			if (vi == NULL) {
				Error(ei, ERR_ALLOC, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			vi->v->u.iValue = cu_tk->i;
			vi->v->type = TYPE_INTEGER;

			vi->next = stack;
			stack = vi;
			break;

		case SYM_CONST_FLOAT:
			// 定数(実数)
			vi = AllocValue();
			if (vi == NULL) {
				Error(ei, ERR_ALLOC, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			vi->v->u.fValue = cu_tk->f;
			vi->v->type = TYPE_FLOAT;

			vi->next = stack;
			stack = vi;
			break;

		case SYM_CONST_STRING:
			//文字列定数
			vi = AllocValue();
			if (vi == NULL) {
				Error(ei, ERR_ALLOC, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			vi->v->u.sValue = alloc_copy(cu_tk->buf);
			conv_ctrl(vi->v->u.sValue);
			if (vi->v->u.sValue == NULL) {
				FreeValue(vi);
				RetSt = RET_ERROR;
				break;
			}
			vi->v->type = TYPE_STRING;
			vi->next = stack;
			stack = vi;
			break;

		case SYM_NOT:
			//単項演算子
			if (stack == NULL) {
				Error(ei, ERR_SENTENCE, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			vi = stack;
			stack = stack->next;
			if (vi->v->type == TYPE_FLOAT) {
				v1 = UnaryCalcValueFloat(ei, vi, cu_tk->sym_type);
			}
			else if (vi->v->type == TYPE_INTEGER) {
				v1 = UnaryCalcValue(ei, vi, cu_tk->sym_type);
			}
			else if (vi->v->type == TYPE_STRING) {
				v1 = UnaryCalcString(ei, vi, cu_tk->sym_type);
			}
			else {
				FreeValue(vi);
				Error(ei, ERR_OPERATOR, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			if (v1 != vi) {
				FreeValue(vi);
			}
			if (v1 == NULL) {
				RetSt = RET_ERROR;
				break;
			}
			v1->next = stack;
			stack = v1;
			break;

		case SYM_BITNOT:
		case SYM_PLUS:
		case SYM_MINS:
		case SYM_INC:
		case SYM_DEC:
		case SYM_BINC:
		case SYM_BDEC:
			//単項演算子
			if (stack == NULL) {
				Error(ei, ERR_SENTENCE, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			vi = stack;
			stack = stack->next;
			if (vi->v->type == TYPE_FLOAT) {
				v1 = UnaryCalcValueFloat(ei, vi, cu_tk->sym_type);
			} else if (vi->v->type == TYPE_INTEGER) {
				v1 = UnaryCalcValue(ei, vi, cu_tk->sym_type);
			} else {
				FreeValue(vi);
				Error(ei, ERR_OPERATOR, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			if (v1 != vi) {
				FreeValue(vi);
			}
			if (v1 == NULL) {
				RetSt = RET_ERROR;
				break;
			}
			v1->next = stack;
			stack = v1;
			break;

		case SYM_EQ:
			//代入演算子
			if (stack == NULL || stack->next == NULL) {
				Error(ei, ERR_SENTENCE, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			//右辺
			v1 = stack;
			stack = stack->next;
			//左辺
			v2 = stack;
			stack = stack->next;

			//変数に値を設定
			SetValue(v2->v, v1->v);
			if (retstack != NULL) {
				mem_free(&v1->name);
				v1->name = alloc_copy(v2->name);
				v1->name_hash = v2->name_hash;
			}
			//代入した値をスタックに積む
			v1->next = stack;
			stack = v1;
			FreeValue(v2);
			break;

		case SYM_COMP_EQ:
			// 複合代入演算子
			if (stack == NULL || stack->next == NULL) {
				Error(ei, ERR_SENTENCE, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			// スタックに左辺のコピーを追加
			v1 = stack->next;
			vi = AllocValue();
			SetValue(vi->v, v1->v);
			if (retstack != NULL) {
				vi->name = alloc_copy(v1->name);
				vi->name_hash = v1->name_hash;
			}
			vi->next = stack->next;
			stack->next = vi;
			break;

		case SYM_LABELEND:
			if (stack == NULL || stack->next == NULL) {
				Error(ei, ERR_SENTENCE, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			// value
			v1 = stack;
			stack = stack->next;
			// key
			v2 = stack;
			stack = stack->next;
			if (v2->v->type == TYPE_STRING && v2->v->u.sValue != NULL && *v2->v->u.sValue != TEXT('\0')) {
				mem_free(&v1->name);
				mem_free(&v1->org_name);
				v1->name = alloc_copy(v2->v->u.sValue);
				v1->name_hash = str2hash(v1->name);
#ifndef PG0_CMD
				v1->org_name = alloc_copy(v2->v->u.sValue);
#endif
			}
			v1->next = stack;
			stack = v1;
			FreeValue(v2);
			break;

		case SYM_CPAND:
		case SYM_CPOR:
			// 論理演算子
			if (stack == NULL || stack->next == NULL) {
				Error(ei, ERR_SENTENCE, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			v1 = stack;
			stack = stack->next;
			v2 = stack;
			stack = stack->next;
			if (cu_tk->sym_type == SYM_CPAND) {
				cp = GetValueBoolean(v1->v) && GetValueBoolean(v2->v);
			} else {
				cp = GetValueBoolean(v1->v) || GetValueBoolean(v2->v);
			}
			FreeValue(v1);
			FreeValue(v2);

			vi = AllocValue();
			if (vi == NULL) {
				Error(ei, ERR_ALLOC, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			vi->v->u.iValue = (cp == FALSE) ? 0 : 1;
			vi->v->type = TYPE_INTEGER;
			vi->next = stack;
			stack = vi;
			break;

		default:
			//二項演算子
			if (stack == NULL || stack->next == NULL) {
				Error(ei, ERR_SENTENCE, ei->err, NULL);
				RetSt = RET_ERROR;
				break;
			}
			v1 = stack;
			stack = stack->next;
			v2 = stack;
			stack = stack->next;

			vi = CalcValue(ei, v2, v1, cu_tk->sym_type);
			if (vi == NULL) {
				FreeValue(v1);
				FreeValue(v2);
				RetSt = RET_ERROR;
				break;
			}
			FreeValue(v1);
			FreeValue(v2);

			//演算結果をスタックに積む
			vi->next = stack;
			stack = vi;
			break;
		}
		if (RetSt != RET_SUCCESS) {
			break;
		}
		cu_tk = cu_tk->next;
	}

	if (PostfixValue(ei) == FALSE) {
		RetSt = RET_ERROR;
	}
	if (retstack != NULL) {
		//スタックを返す
		*retstack = NULL;
		while (stack != NULL) {
			vi = stack;
			stack = stack->next;
			vi->next = *retstack;
			*retstack = vi;
		}
	} else {
		//スタックを解放する
		FreeValueList(stack);
	}
	return RetSt;
}

/*
 * ExpandArgument - 引数の展開
 */
static TOKEN *ExpandArgument(EXECINFO *ei, TOKEN *tk, VALUEINFO *param)
{
	VALUEINFO *vi;
	BOOL eq = FALSE;

	while (tk != NULL && tk->sym_type != SYM_FUNC && param != NULL) {
		if (tk->sym_type != SYM_DECLVARIABLE) {
			Error(ei, ERR_SENTENCE, tk->err, NULL);
			return NULL;
		}
		//変数に値を設定
		if (*tk->buf == ARGUMENT_ADDRESS) {
			vi = DeclVariable(ei, tk->buf + 1, tk->err);
			if (vi == NULL) {
				return NULL;
			}
			mem_free(&vi->v);
			vi->v = param->v;
		} else {
			vi = DeclVariable(ei, tk->buf, tk->err);
			if (vi == NULL) {
				return NULL;
			}
			SetValue(vi->v, param->v);
		}
		param = param->next;
		while (tk->sym_type != SYM_FUNC && tk->sym_type != SYM_WORDEND) {
			tk = tk->next;
		}
		if (tk->sym_type == SYM_WORDEND) {
			tk = tk->next;
		}
	}

	if (tk != NULL && tk->sym_type != SYM_FUNC) {
		//未処理の引数を実行
		ei->decl = TRUE;
		ei->to_tk = SYM_FUNC;
		if (ExecSentense(ei, tk, NULL, NULL) == -1) {
			return NULL;
		}
		ei->decl = FALSE;
		ei->to_tk = 0;

		//引数のスキップ
		for (; tk != NULL && tk->sym_type != SYM_FUNC; tk = tk->next) {
			switch (tk->sym_type) {
			case SYM_EQ:
				eq = TRUE;
				break;

			case SYM_WORDEND:
				if (eq == FALSE) {
					//デフォルト値が無い
					Error(ei, ERR_ARGUMENTCNT, tk->err, NULL);
					return NULL;
				}
				eq = FALSE;
				break;
			}
		}
		if (eq == FALSE) {
			//デフォルト値が無い
			Error(ei, ERR_ARGUMENTCNT, tk->err, NULL);
			return NULL;
		}
	}
	return tk;
}

/*
 * ExecNameFunction - 名前の一致する関数の実行
 */
static VALUEINFO *ExecNameFunction(EXECINFO *ei, FUNCINFO *fi, VALUEINFO *param)
{
	EXECINFO cei;
	EXECINFO* top;
	VALUEINFO *vret = NULL;
	TOKEN *tk;
	int ret;

	ZeroMemory(&cei, sizeof(EXECINFO));
	cei.name = fi->name;
	cei.sci = ei->sci;
	for (top = ei; top->parent != NULL; top = top->parent);
	cei.parent = top;

	//引数の展開
	tk = ExpandArgument(&cei, fi->tk->next, param);
	if (tk == NULL || tk->target == NULL) {
		FreeExecInfo(&cei);
		return AllocValue();
	}

	//実行
	vret = NULL;
	ret = ExecSentense(&cei, tk->target, &vret, NULL);
	if (ret == RET_BREAK || ret == RET_CONTINUE) {
		Error(&cei, ERR_SENTENCE, cei.err, NULL);
		ret = RET_ERROR;
	}
	if (ret == RET_ERROR) {
		FreeExecInfo(&cei);
		FreeValue(vret);
		return (VALUEINFO *)RET_ERROR;
	}
	if (ret == RET_EXIT) {
		ei->exit = TRUE;
	}
	FreeExecInfo(&cei);
	if (vret == NULL) {
		return AllocValue();
	}
	return vret;
}

/*
 * ExecLibFunction - ライブラリ関数の実行
 */
static VALUEINFO *ExecLibFunction(EXECINFO *ei, LIBFUNC StdFunc, TCHAR *name, VALUEINFO *param)
{
	VALUEINFO *vret = NULL;
	TCHAR ErrStr[BUF_SIZE];
	int ret;

	vret = AllocValue();
	if (vret == NULL) {
		Error(ei, ERR_ALLOC, ei->err, NULL);
		return (VALUEINFO *)RET_ERROR;
	}
	*ErrStr = TEXT('\0');
	ret = StdFunc(ei, param, vret, ErrStr);
	if (ret < 0) {
		switch (ret) {
		case -1:
			Error(ei, ERR_FUNCTION_EXEC, ErrStr, NULL);
			break;

		case -2:
			Error(ei, ERR_ARGUMENTCNT, ei->err, NULL);
			break;
		}
		FreeValue(vret);
		return (VALUEINFO *)RET_ERROR;
	}
	return vret;
}

/*
 * SetFuncAddrList - 関数のアドレスを保存
 */
static BOOL SetFuncAddrList(EXECINFO *ei, TCHAR *Name, int name_hash, FUNCTION_TYPE func_type, void *addr) {
	FUNCADDRINFO *funcaddr = mem_calloc(sizeof(FUNCADDRINFO));
	if (funcaddr == NULL) {
		Error(ei, ERR_ALLOC, ei->err, NULL);
		return FALSE;
	}
	if (ei->funcaddr == NULL) {
		ei->funcaddr = funcaddr;
	} else {
		FUNCADDRINFO *fa;
		for (fa = ei->funcaddr; fa->next != NULL; fa = fa->next);
		fa->next = funcaddr;
	}
	funcaddr->name = alloc_copy(Name);
	funcaddr->name_hash = name_hash;
	funcaddr->addr = addr;
	funcaddr->func_type = func_type;
	funcaddr->ei = ei;
	return TRUE;
}

/*
 * ExecFunction - 関数の実行
 */
VALUEINFO *ExecFunction(EXECINFO *ei, TCHAR *name, VALUEINFO *param)
{
	SCRIPTINFO *csci = ei->sci;
	SCRIPTINFO *tmp_sci;
	VALUEINFO *vret = NULL;
	FUNCINFO *fi = ei->sci->fi;
	LIBRARYINFO *lib;
	LIBFUNC lib_func = NULL;
	TCHAR *r;
	char *cr;
	int name_hash;

	// アドレスが解決済みの関数を検索して実行
	name_hash = str2hash(name);
	if (ei->funcaddr != NULL) {
		FUNCADDRINFO *fa;
		for (fa = ei->funcaddr; fa != NULL; fa = fa->next) {
			if (name_hash == fa->name_hash && lstrcmp(name, fa->name) == 0) {
				switch (fa->func_type) {
				case FUNC_SCRIPT:
					vret = ExecNameFunction(fa->ei, (FUNCINFO *)fa->addr, param);
					break;

				case FUNC_LIBRARY:
					vret = ExecLibFunction(fa->ei, (LIBFUNC)fa->addr, name, param);
					break;
				}
				return vret;
			}
		}
	}

	// ユーザ関数
	for (; fi != NULL; fi = fi->next) {
		if (name_hash == fi->name_hash && lstrcmp(name, fi->name) == 0) {
			// アドレスの退避
			SetFuncAddrList(ei, name, name_hash, FUNC_SCRIPT, (void *)fi);
			return ExecNameFunction(ei, fi, param);
		}
	}

	// 別スクリプトのユーザ関数
	for (tmp_sci = csci->sci_top; tmp_sci != NULL; tmp_sci = tmp_sci->next) {
		for (fi = tmp_sci->fi; fi != NULL; fi = fi->next) {
			if (name_hash == fi->name_hash && lstrcmp(name, fi->name) == 0) {
				// アドレスの退避
				SetFuncAddrList(tmp_sci->ei, name, name_hash, FUNC_SCRIPT, (void *)fi);
				return ExecNameFunction(tmp_sci->ei, fi, param);
			}
		}
	}

	// ライブラリ関数
	r = alloc_join(LIB_FUNC_HEAD, name);
	cr = tchar2char(r);
	for (lib = csci->sci_top->lib; lib_func == NULL && lib != NULL; lib = lib->next) {
		// ライブラリの検索
		lib_func = (LIBFUNC)GetProcAddress(lib->hModul, cr);
	}
	mem_free(&cr);
	if (lib_func == NULL) {
		// 標準関数の検索
		lib_func = GetFuncAddress(r);
	}
	mem_free(&r);
	if (lib_func == NULL) {
		Error(ei, ERR_FUNCTION, ei->err, NULL);
		return (VALUEINFO *)RET_ERROR;
	}
	// アドレスの退避
	SetFuncAddrList(ei, name, name_hash, FUNC_LIBRARY, (void *)lib_func);
	return ExecLibFunction(ei, lib_func, name, param);
}

/*
 * ExecScript - スクリプトの実行
 */
int ExecScript(SCRIPTINFO *sci, VALUEINFO *arg_vi, VALUEINFO **ret_vi)
{
	EXECINFO *ei;
	int ret;

	ei = mem_calloc(sizeof(EXECINFO));
	ei->name = sci->name;
	ei->sci = sci;
	ei->vi = arg_vi;
	ret = ExecSentense(ei, sci->tk, ret_vi, NULL);
	if (ret == RET_BREAK || ret == RET_CONTINUE) {
		Error(ei, ERR_SENTENCE, ei->err, NULL);
		ret = RET_ERROR;
	}
	if (ret == RET_ERROR) {
		FreeExecInfo(ei);
		mem_free(&ei);
		return -1;
	}
	if (sci->callback != NULL) {
		sci->callback(ei, NULL);
	}
	sci->ei = ei;
	return 0;
}
/* End of source */
