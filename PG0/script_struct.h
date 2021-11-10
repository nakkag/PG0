/*
 * PG0
 *
 * script_struct.h
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef SCRIPT_STRUCT_H
#define SCRIPT_STRUCT_H

/* Include Files */
#include <windows.h>
#include <tchar.h>

/* Define */
#define SFUNC					__stdcall
typedef int (SFUNC *LIBFUNC)();

/* Struct */
// エラータイプ
typedef enum {
	ERR_SENTENCE = 0,
	ERR_SENTENCE_PREV,
	ERR_NOTDECLARE,
	ERR_DECLARE,
	ERR_INDEX,
	ERR_PARENTHESES,
	ERR_BLOCK,
	ERR_DIVZERO,
	ERR_OPERATOR,
	ERR_ARRAYOPERATOR,
	ERR_ALLOC,
	ERR_ARGUMENTCNT,
	ERR_FILEOPEN,
	ERR_SCRIPT,
	ERR_FUNCTION,
	ERR_FUNCTION_EXEC,
} ERROR_CODE;

// 戻り値タイプ
typedef enum {
	RET_RETURN = -3,
	RET_EXIT = -2,
	RET_ERROR = -1,
	RET_SUCCESS = 0,
	RET_BREAK,
	RET_CONTINUE
} RET_TYPE;

// 関数タイプ
typedef enum {
	FUNC_NONE = 0,
	FUNC_SCRIPT,
	FUNC_LIBRARY
} FUNCTION_TYPE;

// 変数タイプ
typedef enum {
	TYPE_INTEGER = 0,
	TYPE_FLOAT,
	TYPE_STRING,
	TYPE_ARRAY
} VALUE_TYPE;

//シンボル
typedef enum {
	SYM_NONE = 0,
	SYM_EOF,				// '\0'
	SYM_COMMENT,			// //
	SYM_PREP,				// #
	SYM_LINEEND,			// \n
	SYM_LINESEP,			// ;
	SYM_WORDEND,			// ,

	SYM_BOPEN,				// {
	SYM_BOPEN_PRIMARY,		// {
	SYM_BCLOSE,				// }

	SYM_OPEN,				// (
	SYM_CLOSE,				// )

	SYM_ARRAYOPEN,			// [
	SYM_ARRAYCLOSE,			// ]

	SYM_EQ,					// =

	SYM_CPAND,				// &&
	SYM_CPOR,				// ||

	SYM_LEFT,				// <
	SYM_LEFTEQ,				// <=

	SYM_RIGHT,				// >
	SYM_RIGHTEQ,			// >=

	SYM_EQEQ,				// ==
	SYM_NTEQ,				// !=

	SYM_ADD,				// +
	SYM_SUB,				// -

	SYM_MULTI,				// *
	SYM_DIV,				// /
	SYM_MOD,				// %

	SYM_NOT,				// !
	SYM_PLUS,				// +
	SYM_MINS,				// -

	SYM_CONST_INT,			// 数値定数
	SYM_DECLVARIABLE,		// 変数定義
	SYM_VARIABLE,			// 変数
	SYM_ARRAY,				// 配列

	//予約語
	SYM_VAR,				// var
	SYM_IF,					// if
	SYM_ELSE,				// else
	SYM_WHILE,				// while
	SYM_EXIT,				// exit

	SYM_JUMP,				// jump
	SYM_JZE,				// jump Zero
	SYM_JNZ,				// jump Not Zero

	SYM_CMP,				// Compare
	SYM_CMPSTART,			// Compare start
	SYM_CMPEND,				// Compare end
	SYM_LOOP,				// loop
	SYM_LOOPSTART,			// Loop start
	SYM_LOOPEND,			// Loop end

	// Extension
	SYM_LABELEND,			// :
	SYM_AND,				// &
	SYM_OR,					// |
	SYM_XOR,				// ^
	SYM_LEFTSHIFT,			// <<
	SYM_RIGHTSHIFT,			// >>
	SYM_LEFTSHIFT_LOGICAL,	// <<<
	SYM_RIGHTSHIFT_LOGICAL,	// >>>
	SYM_BITNOT,				// ~

	SYM_COMP_EQ,			// += -= *= /= %= &= |= ^= <<= >>= <<<= >>>=

	SYM_INC,				// ++
	SYM_DEC,				// --
	SYM_BINC,				// ++ (後置)
	SYM_BDEC,				// -- (後置)

	SYM_CONST_FLOAT,		// 定数(実数)
	SYM_CONST_STRING,		// 文字列定数

	SYM_FOR,				// for
	SYM_DO,					// do
	SYM_BREAK,				// break
	SYM_CONTINUE,			// continue
	SYM_SWITCH,				// switch
	SYM_CASE,				// case
	SYM_DEFAULT,			// default
	SYM_DAMMY,				// Dammy

	SYM_FUNCSTART,			// Function start
	SYM_FUNCEND,			// Function end
	SYM_FUNC,				// Function name
	SYM_ARGSTART,			// Argument start
	SYM_RETURN,				// return
} SYM_TYPE;

//トークン
typedef struct _TOKEN {
	SYM_TYPE sym_type;
	TCHAR *buf;
	int i;
	double f;

	//エラー位置 (link)
	TCHAR *err;

	//次のトークン
	struct _TOKEN *next;
	//ブロック
	struct _TOKEN *target;
	//SYM_JUMPのリンク先
	struct _TOKEN *link;

	// 行番号
	int line;
} TOKEN;

// 値
typedef struct _VALUE {
	union {
		struct _VALUEINFO *array;
		TCHAR *sValue;
		int iValue;
		double fValue;
	} u;
	VALUE_TYPE type;
	// 本体
	struct _VALUEINFO *vi;
} VALUE;

// 値情報
typedef struct _VALUEINFO {
	// 名前
	TCHAR *name;
	TCHAR *org_name;
	int name_hash;

	// 値
	struct _VALUE *v;

	//リスト
	struct _VALUEINFO *next;
} VALUEINFO;

// 関数アドレス情報
typedef struct _FUNCADDRINFO {
	//関数名
	TCHAR *name;
	int name_hash;

	void *addr;
	FUNCTION_TYPE func_type;
	struct _EXECINFO *ei;

	struct _FUNCADDRINFO *next;
} FUNCADDRINFO;

//実行情報
typedef struct _EXECINFO {
	TCHAR *name;
	TCHAR *err;

	//変数宣言フラグ
	BOOL decl;
	//位置指定実行
	SYM_TYPE to_tk;

	//スクリプト情報
	struct _SCRIPTINFO *sci;
	//ローカル変数
	struct _VALUEINFO *vi;
	// 関数アドレス情報
	struct _FUNCADDRINFO *funcaddr;
	//親
	struct _EXECINFO *parent;

	//後置
	struct _VALUEINFO *inc_vi;
	struct _VALUEINFO *dec_vi;

	// EXITフラグ
	BOOL exit;
	BOOL line_mode;
} EXECINFO;

//関数情報
typedef struct _FUNCINFO {
	//関数名
	TCHAR *name;
	int name_hash;
	//解析木 (link)
	struct _TOKEN *tk;

	struct _FUNCINFO *next;
} FUNCINFO;

//ライブラリ情報
typedef struct _LIBRARYINFO {
	HANDLE hModul;

	struct _LIBRARYINFO *next;
} LIBRARYINFO;

//スクリプト情報
typedef struct _SCRIPTINFO {
	//ファイル名
	TCHAR *name;
	TCHAR *path;
	TCHAR *buf;

	//オプション
	BOOL strict_val_op;
	BOOL strict_val;
	BOOL extension;

	//解析木
	struct _TOKEN *tk;
	//関数リスト
	struct _FUNCINFO *fi;
	//ライブラリ
	struct _LIBRARYINFO *lib;
	//実行情報
	struct _EXECINFO *ei;
	//コールバック
	LIBFUNC callback;
	//スクリプト情報のリスト
	struct _SCRIPTINFO *sci_top;
	struct _SCRIPTINFO *next;

	long param1;
	long param2;
} SCRIPTINFO;

#endif
/* End of source */
