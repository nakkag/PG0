/*
 * PG0gen
 *
 * main.c
 *
 * Copyright (C) 1996-2026 by Nakashima Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 *
 * スクリプトを解析して、解析木をテンプレート (pg0app.exe / pg0appw.exe) の
 * リソースに埋め込んだ実行ファイルを作成する
 */

/* Include Files */
#include <windows.h>
#include <stdio.h>
#include <locale.h>

#include "../PG0/script.h"
#include "../PG0/script_string.h"
#include "../PG0/script_memory.h"
#include "../PG0/script_utility.h"
#include "../PG0/script_image.h"

#pragma comment(lib, "Version.lib")

/* Define */
#define APP_NAME						TEXT("PG0gen")

#define TEMPLATE_CONSOLE				TEXT("pg0app.exe")
#define TEMPLATE_WINDOW					TEXT("pg0appw.exe")

#define BUF_SIZE	256

/* Global Variables */
TCHAR AppDir[MAX_PATH + 1];

/* Local Function Prototypes */
static const TCHAR *msg_text(const TCHAR *jp, const TCHAR *en);

/*
 * msg_text - 言語に応じたメッセージ
 */
static const TCHAR *msg_text(const TCHAR *jp, const TCHAR *en)
{
	WORD lang = PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale()));
	return (lang == LANG_JAPANESE) ? jp : en;
}

/*
 * _lib_func_error - エラー出力
 */
int SFUNC _lib_func_error(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR *str;

	if (param == NULL) {
		return -2;
	}
	if (param->v->type == TYPE_ARRAY) {
		int size = ArrayToStringSize(param->v->u.array, FALSE);
		str = (TCHAR *)mem_alloc(sizeof(TCHAR) * (size + 1));
		if (str != NULL) {
			ArrayToString(param->v->u.array, str, FALSE);
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
	DWORD mode, rmode;
	DWORD size = 0;
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
 * get_module_dir - 実行ファイルのフォルダ (末尾に \)
 */
static void get_module_dir(TCHAR *dir)
{
	TCHAR *p, *r;

	*dir = TEXT('\0');
	if (GetModuleFileName(NULL, dir, MAX_PATH) == 0) {
		*dir = TEXT('\0');
		return;
	}
	for (p = r = dir; *p != TEXT('\0'); p++) {
		if (*p == TEXT('\\') || *p == TEXT('/')) {
			r = p;
		}
	}
	*(r + 1) = TEXT('\0');
}

/*
 * file_name_of - パスのファイル名部分
 */
static const TCHAR *file_name_of(const TCHAR *path)
{
	const TCHAR *p, *r;

	for (p = r = path; *p != TEXT('\0'); p++) {
		if (*p == TEXT('\\') || *p == TEXT('/')) {
			r = p + 1;
		}
	}
	return r;
}

/*
 * is_builtin_library - 実行ファイルに組み込まれているライブラリ (DLL のコピーが不要)
 */
static BOOL is_builtin_library(const TCHAR *name)
{
	static const TCHAR *builtin[] = {
		TEXT("pg0_io.dll"), TEXT("pg0_math.dll"), TEXT("pg0_string.dll"), IMAGE_LIB_SCREEN,
	};
	int i;

	for (i = 0; i < sizeof(builtin) / sizeof(TCHAR *); i++) {
		if (str_cmp_i((TCHAR *)name, (TCHAR *)builtin[i]) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * uses_screen - screen ライブラリを使っている (ウィンドウアプリケーションにする)
 */
static BOOL uses_screen(SCRIPTINFO *sci)
{
	LIBRARYINFO *lib;
	TCHAR path[MAX_PATH + 1];

	for (lib = sci->lib; lib != NULL; lib = lib->next) {
		if (GetModuleFileName(lib->hModul, path, MAX_PATH) != 0 &&
			str_cmp_i((TCHAR *)file_name_of(path), IMAGE_LIB_SCREEN) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * default_output - 出力ファイル名 (スクリプトと同じフォルダに .exe)
 */
static BOOL default_output(TCHAR *out, const TCHAR *dir, const TCHAR *fname)
{
	TCHAR *p, *r;

	if (lstrlen(dir) + lstrlen(fname) + 5 >= MAX_PATH) {
		return FALSE;
	}
	p = str_cpy(out, (TCHAR *)dir);
	if (*dir != TEXT('\0') && *(p - 1) != TEXT('\\')) {
		p = str_cpy(p, TEXT("\\"));
	}
	p = str_cpy(p, (TCHAR *)fname);
	// 拡張子を .exe にする
	for (r = NULL, p--; p >= out && *p != TEXT('\\'); p--) {
		if (*p == TEXT('.')) {
			r = p;
			break;
		}
	}
	if (r == NULL) {
		r = out + lstrlen(out);
	}
	lstrcpy(r, TEXT(".exe"));
	return TRUE;
}

/*
 * copy_libraries - 組み込み以外のライブラリの DLL を出力先にコピーする
 */
static BOOL copy_libraries(SCRIPTINFO *sci, const TCHAR *out)
{
	LIBRARYINFO *lib;
	TCHAR path[MAX_PATH + 1];
	TCHAR dest[MAX_PATH + 1];
	const TCHAR *name;
	int dir_len;

	dir_len = (int)(file_name_of(out) - out);
	for (lib = sci->lib; lib != NULL; lib = lib->next) {
		if (GetModuleFileName(lib->hModul, path, MAX_PATH) == 0) {
			return FALSE;
		}
		name = file_name_of(path);
		if (is_builtin_library(name) == TRUE) {
			continue;
		}
		if (dir_len + lstrlen(name) >= MAX_PATH) {
			return FALSE;
		}
		str_cpy_n(dest, (TCHAR *)out, dir_len);
		lstrcat(dest, name);
		if (str_cmp_i(path, dest) == 0) {
			// 既に出力先にある
			continue;
		}
		if (CopyFile(path, dest, FALSE) == FALSE) {
			_tprintf(msg_text(TEXT("ライブラリのコピーに失敗しました: %s\n"), TEXT("Failed to copy the library: %s\n")), dest);
			return FALSE;
		}
		_tprintf(msg_text(TEXT("ライブラリをコピーしました: %s\n"), TEXT("Library copied: %s\n")), dest);
	}
	return TRUE;
}

/*
 * generate - テンプレートをコピーして解析木を埋め込む
 */
static BOOL generate(SCRIPTINFO *sci, const TCHAR *tmpl, const TCHAR *out)
{
	HANDLE hUpdate;
	BYTE *image;
	DWORD size;

	image = ScriptToImage(sci, &size);
	if (image == NULL) {
		_tprintf(msg_text(TEXT("解析木の作成に失敗しました。\n"), TEXT("Failed to build the parse tree image.\n")));
		return FALSE;
	}
	if (CopyFile(tmpl, out, FALSE) == FALSE) {
		_tprintf(msg_text(TEXT("実行ファイルを作成できません: %s\n"), TEXT("Cannot create the program: %s\n")), out);
		mem_free(&image);
		return FALSE;
	}
	hUpdate = BeginUpdateResource(out, FALSE);
	if (hUpdate == NULL ||
		UpdateResource(hUpdate, RT_RCDATA, IMAGE_RESOURCE_NAME, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), image, size) == FALSE ||
		EndUpdateResource(hUpdate, FALSE) == FALSE) {
		_tprintf(msg_text(TEXT("解析木の埋め込みに失敗しました: %s\n"), TEXT("Failed to embed the parse tree: %s\n")), out);
		DeleteFile(out);
		mem_free(&image);
		return FALSE;
	}
	mem_free(&image);
	return TRUE;
}

/*
 * _tmain - メイン
 */
int _tmain(int argc, TCHAR **argv)
{
	SCRIPTINFO *ScriptInfo;
	TCHAR fname[MAX_PATH];
	TCHAR tmpl[MAX_PATH + 1];
	TCHAR out[MAX_PATH + 1];
	TCHAR *c;
	int i = 1;
	int ret = -1;
	int window = -1;
	BOOL op_pg0 = FALSE;
	BOOL op_strict = FALSE;

	setlocale(LC_CTYPE, "");

	if (argc > i && (*(argv[i]) == TEXT('/') || *(argv[i]) == TEXT('-'))) {
		//help
		for (c = argv[i]; *c != TEXT('\0') && *c != TEXT('?'); c++);
		if (*c != TEXT('\0')) {
			WORD lang = PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale()));
			if (lang == LANG_JAPANESE) {
				_tprintf(TEXT("pg0gen [/pswcv] file.pg0 [out.exe]\n"));
				_tprintf(TEXT("\n"));
				_tprintf(TEXT("  p\t\tPG0 Mode\n"));
				_tprintf(TEXT("  s\t\t変数宣言を強制\n"));
				_tprintf(TEXT("  w\t\tウィンドウアプリケーションにする\n"));
				_tprintf(TEXT("  c\t\tコンソールアプリケーションにする\n"));
				_tprintf(TEXT("         \t(省略時は screen ライブラリを使っていればウィンドウ)\n"));
				_tprintf(TEXT("  v\t\tバージョン表示\n"));
				_tprintf(TEXT("\n"));
				_tprintf(TEXT("  file.pg0\t実行ファイルにするスクリプトファイル\n"));
				_tprintf(TEXT("  out.exe\t出力する実行ファイル (省略時はスクリプトと同じ場所に .exe)\n"));
				_tprintf(TEXT("\n"));
			} else {
				_tprintf(TEXT("pg0gen [/pswcv] file.pg0 [out.exe]\n"));
				_tprintf(TEXT("\n"));
				_tprintf(TEXT("  p\t\tPG0 Mode\n"));
				_tprintf(TEXT("  s\t\tStrict\n"));
				_tprintf(TEXT("  w\t\tWindow application\n"));
				_tprintf(TEXT("  c\t\tConsole application\n"));
				_tprintf(TEXT("         \t(default: window when the screen library is used)\n"));
				_tprintf(TEXT("  v\t\tVersion\n"));
				_tprintf(TEXT("\n"));
				_tprintf(TEXT("  file.pg0\tScript file to build\n"));
				_tprintf(TEXT("  out.exe\tOutput program (default: .exe next to the script)\n"));
				_tprintf(TEXT("\n"));
			}
			return 0;
		}
		//Version
		for (c = argv[i]; *c != '\0' && *c != 'v' && *c != 'V'; c++);
		if (*c != '\0') {
			TCHAR var_msg[BUF_SIZE];
			TCHAR path[MAX_PATH];
			DWORD size;
			lstrcpy(var_msg, APP_NAME);
			GetModuleFileName(NULL, path, sizeof(path) / sizeof(TCHAR));
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
			_tprintf(TEXT("%s\n"), var_msg);
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
		//window
		for (c = argv[i]; *c != '\0' && *c != 'w' && *c != 'W'; c++);
		if (*c != '\0') {
			window = 1;
		}
		//console
		for (c = argv[i]; *c != '\0' && *c != 'c' && *c != 'C'; c++);
		if (*c != '\0') {
			window = 0;
		}
		i++;
	}
	if (argc <= i) {
		_tprintf(msg_text(TEXT("スクリプトファイルを指定してください。(pg0gen /? でヘルプ)\n"),
			TEXT("Specify a script file. (pg0gen /? for help)\n")));
		return -1;
	}

	GetFilePathName(argv[i], AppDir, fname);
	i++;

	//出力ファイル
	if (argc > i) {
		if (lstrlen(argv[i]) >= MAX_PATH) {
			return -1;
		}
		lstrcpy(out, argv[i]);
	} else if (default_output(out, AppDir, fname) == FALSE) {
		return -1;
	}

	//初期化
	InitializeScript();

	//読み込み (解析)
	ScriptInfo = mem_calloc(sizeof(SCRIPTINFO));
	if (ScriptInfo == NULL) {
		return -1;
	}
	InitializeScriptInfo(ScriptInfo, op_strict, !op_pg0);
	ScriptInfo->sci_top = ScriptInfo;
	ReadScriptFile(ScriptInfo, AppDir, fname);
	if (ScriptInfo->tk == NULL) {
		FreeScriptInfo(ScriptInfo);
		EndScript();
#ifdef _DEBUG
		mem_debug();
#endif
		return -1;
	}

	//テンプレート (pg0gen.exe と同じフォルダ)
	if (window < 0) {
		window = uses_screen(ScriptInfo);
	}
	get_module_dir(tmpl);
	if (lstrlen(tmpl) + lstrlen(TEMPLATE_WINDOW) >= MAX_PATH) {
		FreeScriptInfo(ScriptInfo);
		EndScript();
		return -1;
	}
	lstrcat(tmpl, (window == 1) ? TEMPLATE_WINDOW : TEMPLATE_CONSOLE);
	if (GetFileAttributes(tmpl) == INVALID_FILE_ATTRIBUTES) {
		_tprintf(msg_text(TEXT("テンプレートが見つかりません: %s\n"), TEXT("The template was not found: %s\n")), tmpl);
		FreeScriptInfo(ScriptInfo);
		EndScript();
		return -1;
	}

	//生成
	if (generate(ScriptInfo, tmpl, out) == TRUE && copy_libraries(ScriptInfo, out) == TRUE) {
		_tprintf(msg_text(TEXT("%s を作成しました。(%s)\n"), TEXT("%s created. (%s)\n")), out,
			(window == 1) ? msg_text(TEXT("ウィンドウアプリケーション"), TEXT("window application")) :
			msg_text(TEXT("コンソールアプリケーション"), TEXT("console application")));
		ret = 0;
	}
	FreeScriptInfo(ScriptInfo);
	EndScript();
#ifdef _DEBUG
	mem_debug();
#endif
	return ret;
}
/* End of source */
