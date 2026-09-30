/*
 * PG0app
 *
 * main.c
 *
 * Copyright (C) 1996-2026 by Nakashima Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 *
 * pg0gen.exe が解析木をリソースに埋め込んで実行ファイルにするテンプレート
 *   pg0app.exe  : コンソールアプリケーション
 *   pg0appw.exe : ウィンドウアプリケーション (PG0_APP_WINDOW、screen ライブラリ用)
 * io / math / string / screen ライブラリは組み込み (lib_static.c)、
 * それ以外のライブラリは実行ファイルと同じフォルダの DLL を読み込む
 */

/* Include Files */
#include <windows.h>
#include <stdio.h>
#include <locale.h>
#include <shellapi.h>

#include "../PG0/script.h"
#include "../PG0/script_string.h"
#include "../PG0/script_memory.h"
#include "../PG0/script_utility.h"
#include "../PG0/script_image.h"
#include "../pg0lib/lib_static.h"

/* Define */
#define DEFAULT_APP_NAME				TEXT("PG0")

#define BUF_SIZE	256

/* Global Variables */
// ウィンドウ版: 親プロセスのコンソールに接続できたら標準出力を使う
static BOOL console = TRUE;

/* Local Function Prototypes */
static const TCHAR *msg_text(const TCHAR *jp, const TCHAR *en);
static const TCHAR *app_name(void);

/*
 * app_name - 実行ファイルの名前 (.exe を除く)
 */
static const TCHAR *app_name(void)
{
	static TCHAR name[MAX_PATH + 1] = { 0 };
	TCHAR path[MAX_PATH + 1];
	TCHAR *p, *r, *ext = NULL;

	if (*name != TEXT('\0')) {
		return name;
	}
	if (GetModuleFileName(NULL, path, MAX_PATH) == 0) {
		return DEFAULT_APP_NAME;
	}
	for (p = r = path; *p != TEXT('\0'); p++) {
		if (*p == TEXT('\\') || *p == TEXT('/')) {
			r = p + 1;
			ext = NULL;
		} else if (*p == TEXT('.')) {
			ext = p;
		}
	}
	if (ext != NULL && ext != r) {
		*ext = TEXT('\0');
	}
	if (*r == TEXT('\0')) {
		return DEFAULT_APP_NAME;
	}
	lstrcpy(name, r);
	return name;
}
static void message(const TCHAR *str);
static void output(const TCHAR *str);

/*
 * msg_text - 言語に応じたメッセージ
 */
static const TCHAR *msg_text(const TCHAR *jp, const TCHAR *en)
{
	WORD lang = PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale()));
	return (lang == LANG_JAPANESE) ? jp : en;
}

/*
 * message - エラーメッセージの表示
 */
static void message(const TCHAR *str)
{
	if (console == TRUE) {
		_tprintf(TEXT("%s\n"), str);
	}
#ifdef PG0_APP_WINDOW
	MessageBox(NULL, str, app_name(), MB_OK | MB_ICONERROR);
#endif
}

/*
 * output - 標準出力
 */
static void output(const TCHAR *str)
{
	if (console == TRUE) {
		_tprintf(TEXT("%s"), str);
	} else {
		OutputDebugString(str);
	}
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
	if (str != NULL) {
		message(str);
	}
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
	if (str != NULL) {
		output(str);
	}
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

	buf = mem_alloc(sizeof(TCHAR) * (len + 1));
	if (buf == NULL) {
		return -1;
	}
	*buf = TEXT('\0');
	if (console == TRUE) {
		//標準入力から文字列を読み取る
#ifdef PG0_APP_WINDOW
		ih = CreateFile(TEXT("CONIN$"), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, 0, NULL);
#else
		ih = GetStdHandle(STD_INPUT_HANDLE);
#endif
		if (ih != INVALID_HANDLE_VALUE && ih != NULL) {
			GetConsoleMode(ih, &mode);
			rmode = mode;
			//行入力モードに設定
			mode |= ENABLE_LINE_INPUT;
			SetConsoleMode(ih, mode);
			ReadConsole(ih, buf, len, &size, NULL);
			SetConsoleMode(ih, rmode);
#ifdef PG0_APP_WINDOW
			CloseHandle(ih);
#endif
		}
		*(buf + size) = TEXT('\0');
		for (p = buf; *p != TEXT('\0') && *p != TEXT('\r') && *p != TEXT('\n'); p++);
		*p = TEXT('\0');
	}
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
 * load_libraries - 組み込み以外のライブラリを DLL として読み込む
 */
static BOOL load_libraries(SCRIPTINFO *sci, const BYTE *image, DWORD size)
{
	LIBRARYINFO *lib, *pl;
	HMODULE hModule;
	TCHAR name[MAX_PATH + 1];
	TCHAR dir[MAX_PATH + 1];
	TCHAR path[MAX_PATH + 1];
	TCHAR buf[MAX_PATH + BUF_SIZE];
	int count, i;

	count = ImageLibraryCount(image, size);
	if (count < 0) {
		return FALSE;
	}
	get_module_dir(dir);
	for (i = 0; i < count; i++) {
		if (ImageLibraryName(image, size, i, name, MAX_PATH) == FALSE) {
			return FALSE;
		}
		if (lib_static_is_builtin(name) == TRUE) {
			continue;
		}
		// 実行ファイルのフォルダ、その lib フォルダ、通常の検索順
		hModule = NULL;
		if (lstrlen(dir) + lstrlen(name) + 4 < MAX_PATH) {
			wsprintf(path, TEXT("%s%s"), dir, name);
			hModule = LoadLibraryEx(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
			if (hModule == NULL) {
				wsprintf(path, TEXT("%slib\\%s"), dir, name);
				hModule = LoadLibraryEx(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
			}
		}
		if (hModule == NULL) {
			hModule = LoadLibrary(name);
		}
		if (hModule == NULL) {
			wsprintf(buf, msg_text(TEXT("ライブラリが読み込めません: %s"), TEXT("The library could not be loaded: %s")), name);
			message(buf);
			return FALSE;
		}
		lib = mem_calloc(sizeof(LIBRARYINFO));
		if (lib == NULL) {
			FreeLibrary(hModule);
			return FALSE;
		}
		lib->hModul = hModule;
		if (sci->lib == NULL) {
			sci->lib = lib;
		} else {
			for (pl = sci->lib; pl->next != NULL; pl = pl->next);
			pl->next = lib;
		}
	}
	return TRUE;
}

/*
 * exec_imports - インポートされたスクリプトを生成時と同じ順序で実行する
 */
static BOOL exec_imports(SCRIPTINFO *sci)
{
	SCRIPTINFO **order, *csci;
	VALUEINFO *rvi;
	int count, i, j;

	for (count = 0, csci = sci->next; csci != NULL; csci = csci->next) {
		if (csci->tk != NULL && csci->exec_seq > 0) {
			count++;
		}
	}
	if (count == 0) {
		return TRUE;
	}
	order = mem_calloc(sizeof(SCRIPTINFO *) * count);
	if (order == NULL) {
		return FALSE;
	}
	// exec_seq の昇順 (実行すると exec_seq が書き換わるので先に並べる)
	for (i = 0, csci = sci->next; csci != NULL; csci = csci->next) {
		if (csci->tk == NULL || csci->exec_seq <= 0) {
			continue;
		}
		for (j = i; j > 0 && (*(order + j - 1))->exec_seq > csci->exec_seq; j--) {
			*(order + j) = *(order + j - 1);
		}
		*(order + j) = csci;
		i++;
	}
	for (i = 0; i < count; i++) {
		rvi = NULL;
		if (ExecScript(*(order + i), NULL, &rvi) == -1) {
			FreeValueList(rvi);
			mem_free((void **)&order);
			return FALSE;
		}
		FreeValueList(rvi);
	}
	mem_free((void **)&order);
	return TRUE;
}

/*
 * make_args - コマンドライン引数を argv, argc にする
 */
static VALUEINFO *make_args(int argc, TCHAR **argv, int i)
{
	VALUEINFO *pvi, *vi;
	int arg_cnt = argc - i;

	if (arg_cnt <= 0) {
		return NULL;
	}
	// argv
	pvi = AllocValue();
	if (pvi == NULL) {
		return NULL;
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
		FreeValueList(pvi);
		return NULL;
	}
	pvi->next->name = alloc_copy(TEXT("argc"));
	pvi->next->name_hash = str2hash(TEXT("argc"));
	pvi->next->v->u.iValue = arg_cnt;
	pvi->next->v->type = TYPE_INTEGER;
	return pvi;
}

/*
 * print_result - スクリプトの戻り値の出力
 */
static void print_result(VALUEINFO *rvi)
{
	if (console == FALSE || rvi == NULL || rvi->v == NULL) {
		return;
	}
	switch (rvi->v->type) {
	case TYPE_ARRAY:
	{
		int size = ArrayToStringSize(rvi->v->u.array, FALSE);
		TCHAR *str = (TCHAR *)mem_alloc(sizeof(TCHAR) * (size + 1));
		if (str != NULL) {
			ArrayToString(rvi->v->u.array, str, FALSE);
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
		_tprintf(TEXT("%d"), rvi->v->u.iValue);
		break;
	}
	_tprintf(TEXT("\n"));
}

/*
 * run - リソースの解析木を実行する
 */
static int run(int argc, TCHAR **argv)
{
	SCRIPTINFO *sci;
	VALUEINFO *rvi = NULL;
	HRSRC hRes;
	HGLOBAL hGlobal;
	const BYTE *image;
	DWORD size;
	int ret = -1;

	//初期化
	InitializeScript();
	if (lib_static_init() == FALSE) {
		EndScript();
		return -1;
	}

	//リソースの解析木
	hRes = FindResourceEx(NULL, RT_RCDATA, IMAGE_RESOURCE_NAME, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
	if (hRes == NULL || (hGlobal = LoadResource(NULL, hRes)) == NULL ||
		(image = LockResource(hGlobal)) == NULL || (size = SizeofResource(NULL, hRes)) == 0) {
		message(msg_text(TEXT("スクリプトが埋め込まれていません。pg0gen.exe で作成した実行ファイルを実行してください。"),
			TEXT("No script is embedded. Run a program created by pg0gen.exe.")));
		lib_static_term();
		EndScript();
		return -1;
	}
	sci = ImageToScript(image, size);
	if (sci == NULL) {
		message(msg_text(TEXT("埋め込まれたスクリプトが不正です。"), TEXT("The embedded script is invalid.")));
		lib_static_term();
		EndScript();
		return -1;
	}

	//ライブラリ、インポートしたスクリプト、本体の順に実行
	if (load_libraries(sci, image, size) == TRUE && exec_imports(sci) == TRUE) {
		ret = ExecScript(sci, make_args(argc, argv, 1), &rvi);
		if (ret != -1) {
			print_result(rvi);
		}
	}
	FreeValueList(rvi);
	FreeScriptInfo(sci);
	lib_static_term();
	EndScript();
#ifdef _DEBUG
	mem_debug();
#endif
	return (ret == -1) ? -1 : 0;
}

#ifdef PG0_APP_WINDOW
/*
 * attach_console - コンソールから起動されたときはそのコンソールに出力する
 */
static BOOL attach_console(void)
{
	FILE *fp;

	if (AttachConsole(ATTACH_PARENT_PROCESS) == FALSE) {
		return FALSE;
	}
	freopen_s(&fp, "CONOUT$", "w", stdout);
	freopen_s(&fp, "CONOUT$", "w", stderr);
	return TRUE;
}

/*
 * _tWinMain - メイン (ウィンドウアプリケーション)
 */
int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
	LPWSTR *argv;
	int argc = 0;
	int ret;

	console = attach_console();
	setlocale(LC_CTYPE, "");
	argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv == NULL) {
		return -1;
	}
	ret = run(argc, argv);
	LocalFree(argv);
	return ret;
}
#else
/*
 * _tmain - メイン (コンソールアプリケーション)
 */
int _tmain(int argc, TCHAR **argv)
{
	setlocale(LC_CTYPE, "");
	return run(argc, argv);
}
#endif
/* End of source */
