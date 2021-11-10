/*
 * PG0cmd
 *
 * main.c
 *
 * Copyright (C) 1996-2019 by Nakashima Tomoaki. All rights reserved.
 *		http://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#include <windows.h>
#include <stdio.h>
#include <locale.h>

#include "../PG0/script.h"
#include "../PG0/script_string.h"
#include "../PG0/script_memory.h"
#include "../PG0/script_utility.h"

#pragma comment(lib, "Version.lib")

/* Define */
#define APP_NAME						TEXT("PG0cmd")

#define BUF_SIZE	256
#define LINE_SIZE	32768

/* Global Variables */
TCHAR AppDir[MAX_PATH + 1];
BOOL op_pg0 = FALSE;
BOOL op_hex = FALSE;

/* Local Function Prototypes */

/*
 * _lib_func_error - エラー出力
 */
int SFUNC _lib_func_error(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR *str;
	int line = 0;

	if (param == NULL) {
		return -2;
	}
	if (param->v->type == TYPE_ARRAY) {
		int size = ArrayToStringSize(param->v->u.array, op_hex);
		str = (TCHAR *)mem_alloc(sizeof(TCHAR) * (size + 1));
		if (str != NULL) {
			ArrayToString(param->v->u.array, str, op_hex);
		}
	} else {
		str = VariableToString(param);
	}
	_tprintf(TEXT("%s\n"), str);
	mem_free(&str);
	return 0;
}

/*
 * _lib_func_print - 標準出力
 */
int SFUNC _lib_func_print(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR *str;

	if (param == NULL) {
		return -2;
	}
	str = VariableToString(param);
	_tprintf(TEXT("%s"), str);
	mem_free(&str);
	return 0;
}

/*
 * _lib_func_input - 標準入力
 */
int SFUNC _lib_func_input(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	HANDLE ih;
	TCHAR *buf, *p;
	int mode, rmode;
	int size;
	int len = 1024;

	//標準入力から文字列を読み取る
	ih = GetStdHandle(STD_INPUT_HANDLE);
	GetConsoleMode(ih, &mode);
	rmode = mode;
	//行入力モードに設定
	mode |= ENABLE_LINE_INPUT;
	SetConsoleMode(ih, mode);
	buf = mem_alloc(sizeof(TCHAR) * (len + 1));
	ReadConsole(ih, buf, len, &size, NULL);
	SetConsoleMode(ih, rmode);

	*(buf + size) = TEXT('\0');
	for (p = buf; *p != TEXT('\0') && *p != TEXT('\r') && *p != TEXT('\n'); p++);
	*p = TEXT('\0');

	ret->v->u.sValue = buf;
	ret->v->type = TYPE_STRING;
	return 0;
}

/*
 * FormatValue - 値の出力
 */
void FormatValue(VALUEINFO *vi) {
	if (op_hex == TRUE) {
		_tprintf(TEXT("0x%X"), vi->v->u.iValue);
	} else {
		_tprintf(TEXT("%d"), vi->v->u.iValue);
	}
}

/*
 * LineExecLoop - １行入力し解析、実行を行う
 */
static void LineExecLoop()
{
	SCRIPTINFO *sci;
	EXECINFO ei;
	VALUEINFO *vi;
	VALUEINFO *svi = NULL;
	TOKEN *tk;
	TCHAR buf[LINE_SIZE];
	int ret = 0;

	sci = mem_alloc(sizeof(SCRIPTINFO));
	InitializeScriptInfo(sci, FALSE, !op_pg0);

	ZeroMemory(&ei, sizeof(EXECINFO));
	ei.name = TEXT("stdin");
	ei.sci = sci;
	ei.line_mode = TRUE;

	while (_fgetts(buf, LINE_SIZE - 1, stdin) != NULL) {
		//解析
		sci->buf = buf;
		tk = ParseSentence(&ei, buf, 0);
		if (tk == NULL) {
			continue;
		}
		//実行
		svi = NULL;
		ret = ExecSentense(&ei, tk, NULL, &svi);
		FreeToken(tk);
		if (ret == RET_EXIT || ret == RET_RETURN) {
			FreeValueList(svi);
			break;
		}
		if (ret != RET_ERROR) {
			vi = svi;
			while (vi != NULL) {
				switch (vi->v->type) {
				case TYPE_ARRAY:
				{
					int size = ArrayToStringSize(vi->v->u.array, op_hex);
					TCHAR *str = (TCHAR *)mem_alloc(sizeof(TCHAR) * (size + 1));
					if (str != NULL) {
						ArrayToString(vi->v->u.array, str, op_hex);
						_tprintf(TEXT("%s"), str);
						mem_free(&str);
					}
				}
					break;
				case TYPE_STRING:
					_tprintf(TEXT("%s"), vi->v->u.sValue);
					break;
				case TYPE_FLOAT:
					_tprintf(TEXT("%.16f"), vi->v->u.fValue);
					break;
				default:
					FormatValue(vi);
					break;
				}
				_tprintf(TEXT("\n"));
				vi = vi->next;
			}
		}
		FreeValueList(svi);
	}
	FreeExecInfo(&ei);
	sci->buf = NULL;
	FreeScriptInfo(sci);
}

/*
 * _tmain - メイン
 */
int _tmain(int argc, TCHAR **argv)
{
	SCRIPTINFO *ScriptInfo;
	VALUEINFO *vi, *pvi;
	VALUEINFO *rvi = NULL;
	TCHAR fname[MAX_PATH];
	int i = 1;
	int ret;
	TCHAR *c;
	BOOL op_strict = FALSE;

	setlocale(LC_CTYPE, "");

	if (argc > i && (*(argv[i]) == TEXT('/') || *(argv[i]) == TEXT('-'))) {
		//help
		for (c = argv[i]; *c != TEXT('\0') && *c != TEXT('?'); c++);
		if (*c != TEXT('\0')) {
			WORD lang = PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale()));
			if (lang == LANG_JAPANESE) {
				_tprintf(TEXT("pg0cmd [/psxv] [file.pg0] [arg1[ arg2...]]\n"));
				_tprintf(TEXT("\n"));
				_tprintf(TEXT("  p\t\tPG0 Mode\n"));
				_tprintf(TEXT("  s\t\t変数宣言を強制 (通常実行時)\n"));
				_tprintf(TEXT("  x\t\t結果を16進数で表示\n"));
				_tprintf(TEXT("  v\t\tバージョン表示\n"));
				_tprintf(TEXT("\n"));
				_tprintf(TEXT("  file.pg0\t実行するスクリプトファイル\n"));
				_tprintf(TEXT("         \tファイル名の指定が無い場合はライン実行を行う\n"));
				_tprintf(TEXT("\n"));
				_tprintf(TEXT("  arg1\t\tスクリプトに渡す引数\n"));
				_tprintf(TEXT("         \targvで引数の配列、argcで引数の数\n"));
				_tprintf(TEXT("\n"));
			} else {
				_tprintf(TEXT("pg0cmd [/psxv] [file.pg0] [arg1[ arg2...]]\n"));
				_tprintf(TEXT("\n"));
				_tprintf(TEXT("  p\t\tPG0 Mode\n"));
				_tprintf(TEXT("  s\t\tStrict\n"));
				_tprintf(TEXT("  x\t\tHex result\n"));
				_tprintf(TEXT("  v\t\tVersion\n"));
				_tprintf(TEXT("\n"));
				_tprintf(TEXT("  file.pg0\tExecution script file\n"));
				_tprintf(TEXT("\n"));
				_tprintf(TEXT("  arg1\t\tCommand line\n"));
				_tprintf(TEXT("\n"));
			}
			return 0;
		}
		//Version
		for (c = argv[i]; *c != '\0' && *c != 'v' && *c != 'V'; c++);
		if (*c != '\0') {
			{
				TCHAR var_msg[BUF_SIZE];
				TCHAR path[MAX_PATH];
				DWORD size;
				lstrcpy(var_msg, APP_NAME);
				GetModuleFileName(NULL, path, sizeof(path));
				size = GetFileVersionInfoSize(path, NULL);
				if (size) {
					VS_FIXEDFILEINFO *FileInfo;
					UINT len;
					BYTE *buf = mem_alloc(size);
					if (buf != NULL) {
						GetFileVersionInfo(path, 0, size, buf);
						VerQueryValue(buf, TEXT("\\"), &FileInfo, &len);
						wsprintf(var_msg + lstrlen(var_msg), TEXT(" Ver %d.%d.%d"),
							HIWORD(FileInfo->dwFileVersionMS),
							LOWORD(FileInfo->dwFileVersionMS),
							HIWORD(FileInfo->dwFileVersionLS));
						mem_free(&buf);
					}
				}
				_tprintf(var_msg);
			}
			return 0;
		}
		//PG0
		for (c = argv[i]; *c != '\0' && *c != 'p' && *c != 'P'; c++);
		if (*c != '\0') {
			op_pg0 = TRUE;
		}
		//strict
		for (c = argv[i]; *c != '\0' && *c != 's' && *c != 'S'; c++);
		if (*c != '\0') {
			op_strict = TRUE;
		}
		//hex
		for (c = argv[i]; *c != '\0' && *c != 'x' && *c != 'X'; c++);
		if (*c != '\0') {
			op_hex = TRUE;
		}
		i++;
	}

	if (argc <= i) {
		//1行実行モード
		InitializeScript();
		LineExecLoop();
		EndScript();
#ifdef _DEBUG
		mem_debug();
#endif
		return 0;
	}

	GetFilePathName(argv[i], AppDir, fname);
	i++;

	//初期化
	InitializeScript();

	//読み込み
	ScriptInfo = mem_calloc(sizeof(SCRIPTINFO));
	if (ScriptInfo == NULL) {
		return -1;
	}
	InitializeScriptInfo(ScriptInfo, op_strict, !op_pg0);
	ScriptInfo->sci_top = ScriptInfo;
	ReadScriptFile(ScriptInfo, AppDir, fname);
	if (ScriptInfo->tk == NULL) {
		FreeScriptInfo(ScriptInfo);
#ifdef _DEBUG
		mem_debug();
#endif
		return -1;
	}

	//引数
	if (argc <= i) {
		pvi = NULL;
	} else {
		int arg_cnt = argc - i;
		// argv
		pvi = AllocValue();
		if (pvi == NULL) {
			return -1;
		}
		pvi->name = alloc_copy(TEXT("argv"));
		pvi->name_hash = str2hash(TEXT("argv"));
		pvi->v->type = TYPE_ARRAY;
		vi = pvi->v->u.array = StringToVariable(NULL, argv[i]);
		for (i++; i < argc; i++) {
			vi = vi->next = StringToVariable(NULL, argv[i]);
		}
		// argc
		pvi->next = AllocValue();
		if (pvi->next == NULL) {
			return -1;
		}
		pvi->next->name = alloc_copy(TEXT("argc"));
		pvi->next->name_hash = str2hash(TEXT("argc"));
		pvi->next->v->u.iValue = arg_cnt;
		pvi->next->v->type = TYPE_INTEGER;
	}
	//戻り値の確保
	rvi = NULL;
	ret = ExecScript(ScriptInfo, pvi, &rvi);
	if (ret != -1 && rvi != NULL && rvi->v != NULL) {
		switch (rvi->v->type) {
		case TYPE_ARRAY:
		{
			int size = ArrayToStringSize(rvi->v->u.array, op_hex);
			TCHAR *str = (TCHAR *)mem_alloc(sizeof(TCHAR) * (size + 1));
			if (str != NULL) {
				ArrayToString(rvi->v->u.array, str, op_hex);
				_tprintf(TEXT("%s"), str);
				mem_free(&str);
			}
		}
			break;
		case TYPE_STRING:
			_tprintf(TEXT("%s"), rvi->v->u.sValue);
			break;
		case TYPE_FLOAT:
			_tprintf(TEXT("%.16f"), rvi->v->u.fValue);
			break;
		default:
			FormatValue(rvi);
			break;
		}
		_tprintf(TEXT("\n"));
	}
	FreeValueList(rvi);
	FreeScriptInfo(ScriptInfo);
	EndScript();
#ifdef _DEBUG
	mem_debug();
#endif
	return 0;
}