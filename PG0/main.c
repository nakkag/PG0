/*
 * PG0
 *
 * main.c
 *
 * Copyright (C) 1996-2021 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef	_INC_OLE
#include <shlobj.h>
#include <commctrl.h>
#include <tchar.h>

#include "Profile.h"
#include "toolbar.h"
#include "nEdit.h"
#include "variable_view.h"
#include "console_view.h"
#include "frame.h"
#include "dpi.h"
#include "online.h"

#include "script.h"
#include "script_string.h"
#include "script_memory.h"
#include "script_utility.h"

#include "resource.h"

#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "Version.lib")

/* Define */
#define MAIN_WND_CLASS					TEXT("PG0EditMainWndClass")
#define APP_NAME						TEXT("PG0")
#define WINDOW_TITLE_EX					TEXT("PG0.5")

#define IDC_EDIT_EDIT					1
#define IDC_EDIT_CONSOLE				2
#define IDC_EDIT_VARIABLE				3

#define BUF_SIZE						256
#define SEP_SIZE						Scale(4)

#define EXEC_SPEED_NO_WAIT				0
#define EXEC_SPEED_HIGH					1
#define EXEC_SPEED_MID					250
#define EXEC_SPEED_LOW					500

// タイマーID
#define TIMER_SEP						1
#define TIMER_CONFIRM_TUTORIAL			2
#define TIMER_FILE_WATCH				3

// 開いているファイルの変更を確認する間隔 (ミリ秒)
#define FILE_WATCH_INTERVAL				500

/* Global Variables */
static HINSTANCE hInst;
static TCHAR file_path[MAX_PATH];
static TCHAR cmd_line[BUF_SIZE];
static TCHAR window_title[BUF_SIZE];
static LOGFONT lf;
static HWND hEdit;
static HWND hConsoleView;
static HWND hVariableView;
static HWND hFocusWnd;
static BOOL first_io;
static UINT uFindMsg;
static HWND hFindWnd;

typedef struct _OPTION_DATA {
	int sep_size_vertical;
	int sep_size_horizon;
	RECT window_rect;
	int window_state;

	int confirm_tutorial;

	int pg05_mode;
	int hex_mode;
	int strict_val;
	int line_no;
} OPTION_DATA;
static OPTION_DATA op;

// ファイルの更新日時とサイズ
typedef struct _FILE_STAMP {
	FILETIME time;
	DWORD size_high;
	DWORD size_low;
} FILE_STAMP;
// 開いているファイルを読み込み、保存した時点の状態 (ほかのプログラムによる変更の検出用)
static FILE_STAMP file_stamp;
static BOOL file_stamp_valid;
// 変更を検出した時点の状態 (書き込みの完了を待つ)
static FILE_STAMP file_pending;
static BOOL file_pending_valid;
static BOOL file_checking;
// ほかのプログラムによる変更を読み込む前の内容 (変更の表示用)
static TCHAR *reload_base_text;

static HANDLE hThread;
static int exec_line = -1;
typedef struct _EXEC_DATA {
	BOOL exec_flag;
	BOOL step_flag;
	BOOL step_next_flag;
	BOOL stop_flag;
	int exec_speed;
	int stop_line;
} EXEC_DATA;
static EXEC_DATA ed;
static CRITICAL_SECTION cs;

/* Local Function Prototypes */
static int SFUNC Callback(EXECINFO *ei, TOKEN *cu_tk);
static TCHAR *GetTimeString(TCHAR *ret);
static void OutputTime(HWND hWnd);
static unsigned int CALLBACK StartScript(const HWND hWnd);
static void ExecScriptThread(const HWND hWnd);

static BOOL GetErrorMessage(const int err_code, TCHAR *err_str);
static TCHAR *GetResMessage(const UINT id);
static HFONT SelectFont(const HWND hWnd);
static void GetIni(const HWND hWnd, const TCHAR *path);
static void PutIni(const HWND hWnd, const TCHAR *path);
static BOOL ReadScriptfile(const HWND hWnd, TCHAR *path);
static BOOL GetFileStamp(const TCHAR *path, FILE_STAMP *fs);
static void SetFileStamp(void);
static int FindChangedLine(const TCHAR *old_text, const TCHAR *new_text);
static void CheckFileChange(const HWND hWnd);
static void ShowReloadDiff(const HWND hWnd);
static BOOL SaveFile(const HWND hWnd, TCHAR *path);
static BOOL SaveConfirm(const HWND hWnd);
static void SetTitle(const HWND hWnd);
static UINT SpeedMenuId(const int speed);
static BOOL WriteScriptFile(const HWND hWnd, const TCHAR *path);
static BOOL GenerateExe(const HWND hWnd);
static void SetEnableWindow(const HWND hWnd);
static void Resize(const HWND hWnd);
static LRESULT CALLBACK MainProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static BOOL InitApplication(HINSTANCE hInstance);
static HWND InitInstance(HINSTANCE hInstance, int CmdShow);

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
	str = VariableToString(param);
	if ((param = param->next) != NULL) {
		line = VariableToInt(param);
	}

	OutputTime(hConsoleView);
	SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT(" \x03")TEXT("04"));
	SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)str);
	mem_free(&str);
	if (line > 0) {
		SendMessage(hEdit, WM_SET_ERROR_LINE, 0, (LPARAM)line - 1);
	}
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

	if (param->v->type == TYPE_ARRAY) {
		int size = ArrayToStringSize(param->v->u.array, op.hex_mode);
		str = (TCHAR *)mem_alloc(sizeof(TCHAR) * (size + 1));
		if (str != NULL) {
			ArrayToString(param->v->u.array, str, op.hex_mode);
		}
	} else {
		str = VariableToString(param);
	}
	if (first_io == TRUE && SendMessage(hConsoleView, WM_GETTEXTLENGTH, 0, 0) > 0) {
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT("\r\n"));
		first_io = FALSE;
	}
	SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)str);
	mem_free(&str);
	return 0;
}

/*
 * _lib_func_input - 標準入力
 */
int SFUNC _lib_func_input(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (first_io == TRUE && SendMessage(hConsoleView, WM_GETTEXTLENGTH, 0, 0) > 0) {
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT("\r\n"));
		first_io = FALSE;
	}
	SendMessage(hConsoleView, WM_VIEW_SETINPUTMODE, (WPARAM)TRUE, 0);
	hFocusWnd = hConsoleView;
	SendMessage(GetParent(hConsoleView), WM_SETFOCUS, 0, 0);
	while (SendMessage(hConsoleView, WM_VIEW_GETINPUTMODE, 0, 0) == TRUE) {
		EnterCriticalSection(&cs);
		if (ed.stop_flag == TRUE) {
			LeaveCriticalSection(&cs);
			return 0;
		}
		LeaveCriticalSection(&cs);
		Sleep(100);
	}
	ret->v->u.sValue = alloc_copy((TCHAR *)SendMessage(hConsoleView, WM_VIEW_GETINPUTSTRING, 0, 0));
	ret->v->type = TYPE_STRING;
	return 0;
}

/*
 * Callback - 実行時のコールバック
 */
static int SFUNC Callback(EXECINFO *ei, TOKEN *cu_tk)
{
	EXEC_DATA _ed;
	EnterCriticalSection(&cs);
	_ed = ed;
	LeaveCriticalSection(&cs);
	if (_ed.stop_flag == TRUE) {
		if (_ed.exec_speed <= 0 && _ed.step_flag == FALSE) {
			// 変数のみ更新
			SendMessage(hVariableView, WM_VIEW_SETVARIABLE, 0, (LPARAM)ei);
		}
		return -1;
	}
	if (cu_tk == NULL) {
		// 変数のみ更新
		SendMessage(hVariableView, WM_VIEW_SETVARIABLE, 0, (LPARAM)ei);
		return 0;
	}

	if (_ed.stop_line >= 0) {
		// カーソル行まで実行
		if (cu_tk->line < 0 || exec_line == cu_tk->line) {
			return 0;
		}
		if (_ed.stop_line != cu_tk->line) {
			exec_line = cu_tk->line;
			return 0;
		}
		EnterCriticalSection(&cs);
		ed.stop_line = -1;
		ed.step_flag = TRUE;
		ed.step_next_flag = FALSE;
		_ed = ed;
		LeaveCriticalSection(&cs);
	}
	if (_ed.exec_speed <= 0 && _ed.step_flag == FALSE) {
		// 実行のみ
		exec_line = cu_tk->line;
		return 0;
	}
	if (exec_line != -1) {
		if (cu_tk->line < 0 || exec_line == cu_tk->line) {
			// 同一行は更新しない
			return 0;
		}
		// 変数の更新
		SendMessage(hVariableView, WM_VIEW_SETVARIABLE, 0, (LPARAM)ei);
	}
	// 実行している行の更新
	exec_line = cu_tk->line;
	if (exec_line == -1) {
		return 0;
	}
	SendMessage(hEdit, WM_SET_HIGHLIGHT_LINE, 0, (LPARAM)exec_line);
	if (_ed.step_flag == TRUE) {
		// ステップ実行
		while (1) {
			EnterCriticalSection(&cs);
			if (ed.step_next_flag == TRUE) {
				LeaveCriticalSection(&cs);
				break;
			}
			LeaveCriticalSection(&cs);
			Sleep(100);
		}
		EnterCriticalSection(&cs);
		ed.step_next_flag = FALSE;
		LeaveCriticalSection(&cs);
	} else {
		Sleep(_ed.exec_speed);
	}
	return 0;
}

/*
 * GetDateTimeString - 時間を書式化した文字列の取得
 */
static TCHAR *GetTimeString(TCHAR *ret)
{
	GetTimeFormat(0, TIME_NOSECONDS, NULL, NULL, ret, BUF_SIZE - 1);
	return ret;
}

/*
 * OutputTime - 時間出力
 */
static void OutputTime(HWND hWnd)
{
	TCHAR time_str[BUF_SIZE];
	if (SendMessage(hWnd, WM_GETTEXTLENGTH, 0, 0) > 0) {
		SendMessage(hWnd, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT("\r\n"));
	}
	SendMessage(hWnd, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT("\x03")TEXT("14"));
	SendMessage(hWnd, WM_VIEW_ADDTEXT, 0, (LPARAM)GetTimeString(time_str));
}

/*
 * DebugToken - 解析木のデバッグ表示
 */
#ifdef _DEBUG_TOKEN
static void DebugToken(TOKEN *tk, int lv)
{
	TCHAR buf[BUF_SIZE];
	int i;

	if(tk == NULL) return;

	SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT("\r\n"));
	if (tk->line >= 0) {
		wsprintf(buf, TEXT("%d:\t"), tk->line + 1);
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)buf);
	} else {
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT("\t"));
	}
	for(i = 1; i < lv; i++){
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT("  "));
	}
	if (tk->sym_type == SYM_CONST_INT) {
		wsprintf(buf, TEXT("%ld"), tk->i);
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)buf);
	} else if (tk->sym_type == SYM_CONST_FLOAT) {
		wsprintf(buf, TEXT("%.16f"), tk->f);
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)buf);
	}else if(tk->buf != NULL){
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)tk->buf);
	}

	DebugToken(tk->target, lv + 1);
	DebugToken(tk->next, lv);
}
#endif

/*
 * StartScript - スクリプトの実行
 */
static unsigned int CALLBACK StartScript(const HWND hWnd)
{
	SCRIPTINFO *sci;
	EXECINFO ei;
	VALUEINFO *rvi;
	TCHAR *buf;
	TCHAR path[MAX_PATH + 1], name[MAX_PATH + 1];
	int len;
	int ret;

	SendMessage(hEdit, WM_SET_ERROR_LINE, 0, (LPARAM)-1);
	len = (int)SendMessage(hEdit, WM_GETTEXTLENGTH, 0, 0);
	if (len == 0) {
		hThread = NULL;
		return 0;
	}
	if ((buf = (TCHAR *)mem_alloc(sizeof(TCHAR) * (len + 1))) == NULL) {
		MessageBox(hWnd, TEXT("Alloc error"), window_title, MB_ICONERROR);
		hThread = NULL;
		return -1;
	}
	SendMessage(hEdit, WM_GETTEXT, len + 1, (LPARAM)buf);
	*(buf + len) = TEXT('\0');

	SendMessage(hConsoleView, WM_VIEW_INITINPUTMODE, 0, 0);
	SendMessage(hVariableView, WM_VIEW_SETVARIABLE, 0, 0);
	if (SendMessage(hConsoleView, WM_GETTEXTLENGTH, 0, 0) > 0) {
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT("\r\n--"));
	}
	EnterCriticalSection(&cs);
	ed.exec_flag = TRUE;
	ed.stop_flag = FALSE;
	LeaveCriticalSection(&cs);

	sci = mem_alloc(sizeof(SCRIPTINFO));
	if(sci == NULL){
		mem_free(&buf);
		MessageBox(hWnd, TEXT("Alloc error"), window_title, MB_ICONERROR);
		EnterCriticalSection(&cs);
		ed.exec_flag = FALSE;
		LeaveCriticalSection(&cs);
		hThread = NULL;
		return -1;
	}
#ifdef PG05
	InitializeScriptInfo(sci, (op.strict_val == 0) ? FALSE : TRUE, TRUE);
#else
	InitializeScriptInfo(sci, (op.strict_val == 0) ? FALSE : TRUE, (op.pg05_mode == 0) ? FALSE : TRUE);
#endif
	sci->buf = buf;
	GetFilePathName(file_path, path, name);
	if (*path != TEXT('\0')) {
		SetCurrentDirectory(path);
		sci->path = alloc_copy(path);
	}
	if (*name != TEXT('\0')) {
		SetCurrentDirectory(path);
		sci->name = alloc_copy(name);
	}

	SetEnableWindow(hWnd);
	// 開始メッセージ
	OutputTime(hConsoleView);
	SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT(" \x03")TEXT("12"));
	SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)GetResMessage(IDS_STRING_CONSOLE_START));
	first_io = TRUE;
	exec_line = -1;

	//構文解析
	ZeroMemory(&ei, sizeof(EXECINFO));
	ei.sci = sci;
	sci->tk = ParseSentence(&ei, sci->buf, 0);
	if (sci->tk == NULL) {
		FreeScriptInfo(sci);
		EnterCriticalSection(&cs);
		ed.exec_flag = FALSE;
		LeaveCriticalSection(&cs);
		hThread = NULL;

		// 終了メッセージ
		OutputTime(hConsoleView);
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT(" \x03")TEXT("12"));
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)GetResMessage(IDS_STRING_CONSOLE_END));
		SetEnableWindow(hWnd);
		return 0;
	}
#ifdef _DEBUG_TOKEN
	DebugToken(sci->tk, 1);
#endif

	sci->callback = Callback;
	rvi = NULL;
	ret = ExecScript(sci, NULL, &rvi);
	OutputTime(hConsoleView);
	SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT(" \x03")TEXT("12"));
	EnterCriticalSection(&cs);
	if (ed.stop_flag == FALSE) {
		LeaveCriticalSection(&cs);
		// 終了メッセージ
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)GetResMessage(IDS_STRING_CONSOLE_END));
	} else {
		LeaveCriticalSection(&cs);
		// 停止メッセージ
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)GetResMessage(IDS_STRING_CONSOLE_STOP));
	}
	EnterCriticalSection(&cs);
	if(ret != -1 && ed.stop_flag == FALSE && rvi != NULL && rvi->v != NULL){
		TCHAR msg[BUF_SIZE];
		// 戻り値の出力
		LeaveCriticalSection(&cs);
		OutputTime(hConsoleView);
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT(" \x03")TEXT("12"));
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)GetResMessage(IDS_STRING_CONSOLE_RESULT));
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT("\x03")TEXT(""));
		switch (rvi->v->type) {
		case TYPE_ARRAY:
			{
				int size = ArrayToStringSize(rvi->v->u.array, op.hex_mode);
				TCHAR *str = (TCHAR *)mem_alloc(sizeof(TCHAR) * (size + 1));
				if (str != NULL) {
					ArrayToString(rvi->v->u.array, str, op.hex_mode);
					SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)str);
					mem_free(&str);
				}
			}
			break;
		case TYPE_STRING:
			SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT("\""));
			SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)rvi->v->u.sValue);
			SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT("\""));
			break;
		case TYPE_FLOAT:
		{
			TCHAR tmp[FLOAT_LENGTH];
			_stprintf_s(tmp, FLOAT_LENGTH, TEXT("%.16f"), rvi->v->u.fValue);
			SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)tmp);
		}
			break;
		default:
			if (op.hex_mode == FALSE) {
				wsprintf(msg, TEXT("%ld"), rvi->v->u.iValue);
			} else {
				wsprintf(msg, TEXT("0x%X"), rvi->v->u.iValue);
			}
			SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)msg);
			break;
		}
	} else {
		LeaveCriticalSection(&cs);
	}
	FreeValueList(rvi);
	FreeScriptInfo(sci);
	EndScript();
	SendMessage(hEdit, WM_SET_HIGHLIGHT_LINE, 0, (LPARAM)-1);
	EnterCriticalSection(&cs);
	ed.exec_flag = FALSE;
	LeaveCriticalSection(&cs);
	SetEnableWindow(hWnd);
	hThread = NULL;
	return 0;
}

/*
 * ExecScriptThread - スクリプト実行スレッド
 */
static void ExecScriptThread(const HWND hWnd)
{
	unsigned int thId = -1;
	if (hThread != NULL) {
		return;
	}
	hThread = CreateThread(NULL, 0, StartScript, (void *)hWnd, 0, (unsigned *)&thId);
}

/*
 * GetErrorMessage - エラー値からメッセージを取得
 */
static BOOL GetErrorMessage(const int err_code, TCHAR *err_str)
{
	if (err_str == NULL) {
		return FALSE;
	}
	*err_str = TEXT('\0');
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err_code, 0, err_str, BUF_SIZE - 1, NULL);
	return TRUE;
}

/*
 * GetResMessage - リソースからメッセージを取得
 */
static TCHAR *GetResMessage(const UINT id)
{
	static TCHAR buf[BUF_SIZE];

	*buf = TEXT('\0');
	LoadString(hInst, id, buf, BUF_SIZE - 1);
	return buf;
}

/*
 * SelectFont - フォントの選択
 */
static HFONT SelectFont(const HWND hWnd)
{
	CHOOSEFONT cf;

	// フォント選択ダイアログを表示
	ZeroMemory(&cf, sizeof(CHOOSEFONT));
	cf.lStructSize = sizeof(CHOOSEFONT);
	cf.hwndOwner = hWnd;
	cf.lpLogFont = &lf;
	cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
	cf.nFontType = SCREEN_FONTTYPE;
	if (ChooseFont(&cf) == FALSE) {
		return NULL;
	}
	if (*(lf.lfFaceName) == TEXT('\0')) {
		lf.lfPitchAndFamily = FIXED_PITCH;
	}
	return CreateFontIndirect((CONST LOGFONT *)&lf);
}

/*
 * GetIni - 設定読み込み
 */
static void GetIni(const HWND hWnd, const TCHAR *ini_path)
{
	HFONT hFont;
	HDC hdc;
	int sep_size_variable;

	profile_initialize(ini_path, TRUE);

	op.window_rect.left = profile_get_int(TEXT("WINDOW"), TEXT("left"), CW_USEDEFAULT, ini_path);
	op.window_rect.top = profile_get_int(TEXT("WINDOW"), TEXT("top"), CW_USEDEFAULT, ini_path);
	op.window_rect.right = profile_get_int(TEXT("WINDOW"), TEXT("right"), CW_USEDEFAULT, ini_path);
	op.window_rect.bottom = profile_get_int(TEXT("WINDOW"), TEXT("bottom"), CW_USEDEFAULT, ini_path);
	ScaleRect(&(op.window_rect));
	if ((op.window_rect.left < 0 || op.window_rect.left > GetSystemMetrics(SM_CXSCREEN)) && op.window_rect.left != CW_USEDEFAULT) {
		op.window_rect.left = 0;
	}
	if ((op.window_rect.top < 0 || op.window_rect.top > GetSystemMetrics(SM_CYSCREEN)) && op.window_rect.top != CW_USEDEFAULT) {
		op.window_rect.top = 0;
	}
	if (op.window_rect.right < 200 && op.window_rect.right != CW_USEDEFAULT) {
		op.window_rect.right = Scale(800);
	}
	if (op.window_rect.bottom < 100 && op.window_rect.bottom != CW_USEDEFAULT) {
		op.window_rect.bottom = Scale(600);
	}
	op.sep_size_vertical = Scale(profile_get_int(TEXT("WINDOW"), TEXT("sep_size_vertical"), 400, ini_path));
	op.sep_size_horizon = Scale(profile_get_int(TEXT("WINDOW"), TEXT("sep_size_horizon"), 120, ini_path));
	sep_size_variable = Scale(profile_get_int(TEXT("WINDOW"), TEXT("sep_size_variable"), 100, ini_path));
	SendMessage(hVariableView, WM_VIEW_SET_SEPSIZE, 0, sep_size_variable);
	if (op.window_rect.left != CW_USEDEFAULT && op.window_rect.top != CW_USEDEFAULT &&
		op.window_rect.right != CW_USEDEFAULT && op.window_rect.bottom != CW_USEDEFAULT) {
		SetWindowPos(hWnd, HWND_TOP, op.window_rect.left, op.window_rect.top, op.window_rect.right, op.window_rect.bottom, SWP_HIDEWINDOW);
	}
	op.window_state = profile_get_int(TEXT("WINDOW"), TEXT("window_state"), SW_SHOWDEFAULT, ini_path);
#ifdef NO_TUTORIAL
	op.confirm_tutorial = profile_get_int(TEXT("WINDOW"), TEXT("confirm_tutorial"), 0, ini_path);
#else
	op.confirm_tutorial = profile_get_int(TEXT("WINDOW"), TEXT("confirm_tutorial"), 1, ini_path);
#endif
	op.line_no = profile_get_int(TEXT("WINDOW"), TEXT("line_no"), 1, ini_path);
	SendMessage(hEdit, WM_SHOW_LINE_NO, (WPARAM)(op.line_no == 0) ? FALSE : TRUE, 0);
	CheckMenuItem(GetMenu(hWnd), ID_MENUITEM_LINE_NO, (op.line_no == 0) ? MF_UNCHECKED : MF_CHECKED);

	profile_get_string(TEXT("FONT"), TEXT("name"), TEXT(""), lf.lfFaceName, LF_FACESIZE - 1, ini_path);
	lf.lfHeight = Scale(profile_get_int(TEXT("FONT"), TEXT("height"), -16, ini_path));
	lf.lfWeight = profile_get_int(TEXT("FONT"), TEXT("weight"), 400, ini_path);
	lf.lfItalic = profile_get_int(TEXT("FONT"), TEXT("italic"), 0, ini_path);
	hdc = GetDC(hWnd);
	lf.lfCharSet = profile_get_int(TEXT("FONT"), TEXT("charset"), GetTextCharset(hdc), ini_path);
	ReleaseDC(hWnd, hdc);
	lf.lfPitchAndFamily = FIXED_PITCH;
	if ((hFont = CreateFontIndirect((CONST LOGFONT *)&lf)) != NULL) {
		SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
		SendMessage(hConsoleView, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
		SendMessage(hVariableView, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
		DeleteObject(hFont);
	}

	EnterCriticalSection(&cs);
	ed.exec_speed = profile_get_int(TEXT("EXEC"), TEXT("exec_speed"), EXEC_SPEED_MID, ini_path);
	switch (ed.exec_speed) {
	case EXEC_SPEED_NO_WAIT:
		CheckMenuRadioItem(GetMenu(hWnd), ID_MENUITEM_NO_WAIT, ID_MENUITEM_SPEED_LOW, ID_MENUITEM_NO_WAIT, MF_BYCOMMAND);
		break;
	case EXEC_SPEED_HIGH:
		CheckMenuRadioItem(GetMenu(hWnd), ID_MENUITEM_NO_WAIT, ID_MENUITEM_SPEED_LOW, ID_MENUITEM_SPEED_HIGH, MF_BYCOMMAND);
		break;
	case EXEC_SPEED_MID:
		CheckMenuRadioItem(GetMenu(hWnd), ID_MENUITEM_NO_WAIT, ID_MENUITEM_SPEED_LOW, ID_MENUITEM_SPEED_MID, MF_BYCOMMAND);
		break;
	case EXEC_SPEED_LOW:
		CheckMenuRadioItem(GetMenu(hWnd), ID_MENUITEM_NO_WAIT, ID_MENUITEM_SPEED_LOW, ID_MENUITEM_SPEED_LOW, MF_BYCOMMAND);
		break;
	}
	LeaveCriticalSection(&cs);
	op.strict_val = profile_get_int(TEXT("EXEC"), TEXT("strict_val"), 0, ini_path);
#ifdef PG05
	op.pg05_mode = profile_get_int(TEXT("EXEC"), TEXT("pg0.5_mode"), 1, ini_path);
	SendMessage(hEdit, WM_SET_EXTENSION, TRUE, 0);
#else
	op.pg05_mode = profile_get_int(TEXT("EXEC"), TEXT("pg0.5_mode"), 0, ini_path);
	SendMessage(hEdit, WM_SET_EXTENSION, (WPARAM)(op.pg05_mode == 0) ? FALSE : TRUE, 0);
#endif
	if (op.pg05_mode != 0) {
		lstrcpy(window_title, WINDOW_TITLE_EX);
		SetWindowText(hWnd, window_title);
		CheckMenuItem(GetMenu(hWnd), ID_MENUITEM_PG05, MF_CHECKED);
	}
	op.hex_mode = profile_get_int(TEXT("EXEC"), TEXT("hex_mode"), 0, ini_path);
	SendMessage(hVariableView, WM_VIEW_SET_HEX_MODE, (WPARAM)(op.hex_mode == 0) ? FALSE : TRUE, 0);
	online_get_ini(ini_path);

	profile_free();
}

/*
 * PutIni - 設定書き込み
 */
static void PutIni(const HWND hWnd, const TCHAR *ini_path)
{
	int sep_size_variable;

	profile_initialize(ini_path, TRUE);

	if (op.window_rect.left != CW_USEDEFAULT && op.window_rect.top != CW_USEDEFAULT &&
		op.window_rect.right != CW_USEDEFAULT && op.window_rect.bottom != CW_USEDEFAULT) {
		profile_write_int(TEXT("WINDOW"), TEXT("left"), UnScale(op.window_rect.left), ini_path);
		profile_write_int(TEXT("WINDOW"), TEXT("top"), UnScale(op.window_rect.top), ini_path);
		profile_write_int(TEXT("WINDOW"), TEXT("right"), UnScale(op.window_rect.right), ini_path);
		profile_write_int(TEXT("WINDOW"), TEXT("bottom"), UnScale(op.window_rect.bottom), ini_path);
	}
	profile_write_int(TEXT("WINDOW"), TEXT("window_state"), op.window_state, ini_path);
	profile_write_int(TEXT("WINDOW"), TEXT("sep_size_vertical"), UnScale(op.sep_size_vertical), ini_path);
	profile_write_int(TEXT("WINDOW"), TEXT("sep_size_horizon"), UnScale(op.sep_size_horizon), ini_path);
	sep_size_variable = (int)SendMessage(hVariableView, WM_VIEW_GET_SEPSIZE, 0, 0);
	profile_write_int(TEXT("WINDOW"), TEXT("sep_size_variable"), UnScale(sep_size_variable), ini_path);
	profile_write_int(TEXT("WINDOW"), TEXT("confirm_tutorial"), op.confirm_tutorial, ini_path);
	profile_write_int(TEXT("WINDOW"), TEXT("line_no"), op.line_no, ini_path);

	profile_write_string(TEXT("FONT"), TEXT("name"), lf.lfFaceName, ini_path);
	profile_write_int(TEXT("FONT"), TEXT("height"), UnScale(lf.lfHeight), ini_path);
	profile_write_int(TEXT("FONT"), TEXT("weight"), lf.lfWeight, ini_path);
	profile_write_int(TEXT("FONT"), TEXT("italic"), lf.lfItalic, ini_path);
	profile_write_int(TEXT("FONT"), TEXT("charset"), lf.lfCharSet, ini_path);

	EnterCriticalSection(&cs);
	profile_write_int(TEXT("EXEC"), TEXT("exec_speed"), ed.exec_speed, ini_path);
	LeaveCriticalSection(&cs);
	profile_write_int(TEXT("EXEC"), TEXT("strict_val"), op.strict_val, ini_path);
	profile_write_int(TEXT("EXEC"), TEXT("pg0.5_mode"), op.pg05_mode, ini_path);
	profile_write_int(TEXT("EXEC"), TEXT("hex_mode"), op.hex_mode, ini_path);
	online_put_ini(ini_path);

	profile_flush(ini_path);
	profile_free();
}

/*
 * ReadScriptfile - ファイルの読み込み
 */
static BOOL ReadScriptfile(const HWND hWnd, TCHAR *path)
{
	OPENFILENAME of;
	TCHAR *buf;
	TCHAR err_str[BUF_SIZE];

	if (*path == TEXT('\0')) {
		lstrcpy(path, TEXT("*.pg0"));
		ZeroMemory(&of, sizeof(OPENFILENAME));
		of.lStructSize = sizeof(OPENFILENAME);
		of.hwndOwner = hWnd;
		of.lpstrFilter = TEXT("*.pg0\0*.pg0\0*.*\0*.*\0\0");
		of.lpstrTitle = GetResMessage(IDS_STRING_OPEN_TITLE);
		of.lpstrFile = path;
		of.nMaxFile = MAX_PATH - 1;
		of.lpstrDefExt = TEXT("pg0");
		of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
		if (GetOpenFileName((LPOPENFILENAME)&of) == FALSE) {
			return FALSE;
		}
	}
	// ファイルを読み込む
	buf = read_file(path);
	if (buf == NULL) {
		GetErrorMessage(GetLastError(), err_str);
		MessageBox(hWnd, err_str, window_title, MB_ICONERROR);
		return FALSE;
	}
	SendMessage(hEdit, WM_SETMEM, sizeof(TCHAR) * lstrlen(buf), (LPARAM)buf);
	mem_free(&buf);
	// EDIT の変更フラグを除去する
	SendMessage(hEdit, EM_SETMODIFY, (WPARAM)FALSE, 0);
	return TRUE;
}

/*
 * GetFileStamp - ファイルの更新日時とサイズを取得
 */
static BOOL GetFileStamp(const TCHAR *path, FILE_STAMP *fs)
{
	WIN32_FILE_ATTRIBUTE_DATA fad;

	if (GetFileAttributesEx(path, GetFileExInfoStandard, &fad) == FALSE) {
		return FALSE;
	}
	fs->time = fad.ftLastWriteTime;
	fs->size_high = fad.nFileSizeHigh;
	fs->size_low = fad.nFileSizeLow;
	return TRUE;
}

/*
 * SetFileStamp - 開いているファイルの現在の状態を記録する (読み込み、保存、新規作成の後に呼ぶ)
 * 以降のほかのプログラムによる変更は、この状態との違いで検出する
 */
static void SetFileStamp(void)
{
	file_stamp_valid = (*file_path != TEXT('\0')) ? GetFileStamp(file_path, &file_stamp) : FALSE;
	file_pending_valid = FALSE;
	// 変更の表示は、利用者が開いた、保存した内容からの変更とする
	mem_free(&reload_base_text);
}

/*
 * FindChangedLine - 最初に内容が違う行 (改行コードの違いは無視する)
 * 違いが無い場合は -1 を返す
 */
static int FindChangedLine(const TCHAR *old_text, const TCHAR *new_text)
{
	const TCHAR *p = old_text, *r = new_text;
	const TCHAR *pe, *re;
	int line;

	for (line = 0; ; line++) {
		for (pe = p; *pe != TEXT('\0') && *pe != TEXT('\r') && *pe != TEXT('\n'); pe++);
		for (re = r; *re != TEXT('\0') && *re != TEXT('\r') && *re != TEXT('\n'); re++);
		if (pe - p != re - r || str_cmp_n(p, r, (int)(pe - p)) != 0) {
			return line;
		}
		if (*pe == TEXT('\0') || *re == TEXT('\0')) {
			return (*pe == TEXT('\0') && *re == TEXT('\0')) ? -1 : line;
		}
		p = pe + ((*pe == TEXT('\r') && *(pe + 1) == TEXT('\n')) ? 2 : 1);
		r = re + ((*re == TEXT('\r') && *(re + 1) == TEXT('\n')) ? 2 : 1);
	}
}

/*
 * CheckFileChange - 開いているファイルがほかのプログラム (AI エージェントなど) で変更されていたら読み込み直す
 * 編集中の内容がある場合は、読み込み直すかを確認する
 */
static void CheckFileChange(const HWND hWnd)
{
	FILE_STAMP fs;
	TCHAR *old_text, *new_text;
	BOOL exec;
	int len, line, pos, ret;

	if (file_checking == TRUE || file_stamp_valid == FALSE || *file_path == TEXT('\0') ||
		IsWindowEnabled(hWnd) == FALSE) {
		return;
	}
	// 実行中は終了してから確認する
	EnterCriticalSection(&cs);
	exec = ed.exec_flag;
	LeaveCriticalSection(&cs);
	if (exec == TRUE) {
		return;
	}
	// 削除された場合は何もしない
	if (GetFileStamp(file_path, &fs) == FALSE ||
		(CompareFileTime(&fs.time, &file_stamp.time) == 0 && fs.size_high == file_stamp.size_high && fs.size_low == file_stamp.size_low)) {
		file_pending_valid = FALSE;
		return;
	}
	// 書き込みの途中で読み込まないように、次の確認まで同じ状態が続いてから読み込む
	if (file_pending_valid == FALSE ||
		CompareFileTime(&fs.time, &file_pending.time) != 0 || fs.size_high != file_pending.size_high || fs.size_low != file_pending.size_low) {
		file_pending = fs;
		file_pending_valid = TRUE;
		return;
	}
	file_pending_valid = FALSE;
	if ((new_text = read_file(file_path)) == NULL) {
		return;
	}
	file_stamp = fs;

	// 内容が同じ場合は読み込み直さない
	len = (int)SendMessage(hEdit, WM_GETTEXTLENGTH, 0, 0);
	if ((old_text = (TCHAR *)mem_alloc(sizeof(TCHAR) * (len + 1))) == NULL) {
		mem_free(&new_text);
		return;
	}
	SendMessage(hEdit, WM_GETTEXT, len + 1, (LPARAM)old_text);
	*(old_text + len) = TEXT('\0');
	if ((line = FindChangedLine(old_text, new_text)) < 0) {
		mem_free(&old_text);
		mem_free(&new_text);
		return;
	}
	if (SendMessage(hEdit, EM_GETMODIFY, 0, 0) == TRUE) {
		// 確認の表示中は次の確認を行わない
		file_checking = TRUE;
		ret = MessageBox(hWnd, GetResMessage(IDS_STRING_MSG_FILE_CHANGED), window_title, MB_ICONEXCLAMATION | MB_YESNO);
		file_checking = FALSE;
		if (ret != IDYES) {
			mem_free(&old_text);
			mem_free(&new_text);
			return;
		}
	}

	// 読み込み直して、最初に変更された行を表示する
	SendMessage(hEdit, WM_SETMEM, sizeof(TCHAR) * lstrlen(new_text), (LPARAM)new_text);
	SendMessage(hEdit, EM_SETMODIFY, (WPARAM)FALSE, 0);
	if ((pos = (int)SendMessage(hEdit, EM_LINEINDEX, line, 0)) >= 0) {
		SendMessage(hEdit, EM_SETSEL, pos, pos);
		SendMessage(hEdit, EM_SCROLLCARET, 0, 0);
	}
	mem_free(&new_text);
	// 続けて変更された場合も、最初の変更の前の内容からの変更を表示できるようにする
	if (reload_base_text == NULL) {
		reload_base_text = old_text;
	} else {
		mem_free(&old_text);
	}
	if (SendMessage(hConsoleView, WM_GETTEXTLENGTH, 0, 0) > 0) {
		// 実行時と同じ区切り線
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT("\r\n--"));
	}
	OutputTime(hConsoleView);
	SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT(" \x03")TEXT("12"));
	SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)GetResMessage(IDS_STRING_CONSOLE_RELOAD));
}

/*
 * ShowReloadDiff - ほかのプログラムによる変更を、変更履歴と同じ差分の画面で表示する
 */
static void ShowReloadDiff(const HWND hWnd)
{
	TCHAR title[BUF_SIZE];
	TCHAR *text;
	int len;

	if (reload_base_text == NULL) {
		return;
	}
	len = (int)SendMessage(hEdit, WM_GETTEXTLENGTH, 0, 0);
	if ((text = (TCHAR *)mem_alloc(sizeof(TCHAR) * (len + 1))) == NULL) {
		MessageBox(hWnd, TEXT("Alloc error"), window_title, MB_ICONERROR);
		return;
	}
	SendMessage(hEdit, WM_GETTEXT, len + 1, (LPARAM)text);
	*(text + len) = TEXT('\0');
	lstrcpy(title, GetResMessage(IDS_STRING_RELOAD_DIFF_TITLE));
	online_set_code_view(&lf, (op.line_no != 0));
	online_show_diff(hWnd, title, reload_base_text, text);
	mem_free(&text);
}

/*
 * SaveFile - ファイルの保存
 */
static BOOL SaveFile(const HWND hWnd, TCHAR *path)
{
	OPENFILENAME of;

	if (*path == TEXT('\0')) {
		lstrcpy(path, TEXT(""));
		ZeroMemory(&of, sizeof(OPENFILENAME));
		of.lStructSize = sizeof(OPENFILENAME);
		of.hwndOwner = hWnd;
		of.lpstrFilter = TEXT("*.pg0\0*.pg0\0*.*\0*.*\0\0");
		of.lpstrTitle = GetResMessage(IDS_STRING_SAVE_TITLE);
		of.lpstrFile = path;
		of.nMaxFile = MAX_PATH - 1;
		of.lpstrDefExt = TEXT("pg0");
		of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
		if (GetSaveFileName((LPOPENFILENAME)&of) == FALSE) {
			return FALSE;
		}
	}
	if (WriteScriptFile(hWnd, path) == FALSE) {
		return FALSE;
	}
	// EDIT の変更フラグを除去する
	SendMessage(hEdit, EM_SETMODIFY, (WPARAM)FALSE, 0);
	return TRUE;
}

/*
 * WriteScriptFile - 編集中のスクリプトをファイルに書き込む (UTF-8)
 */
static BOOL WriteScriptFile(const HWND hWnd, const TCHAR *path)
{
	HANDLE hFile;
	WCHAR *wbuf;
	TCHAR *buf;
	BYTE *cbuf;
	TCHAR err_str[BUF_SIZE];
	DWORD ret;
	int len;

	// ファイルを開く
	hFile = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == NULL || hFile == (HANDLE)-1) {
		GetErrorMessage(GetLastError(), err_str);
		MessageBox(hWnd, err_str, window_title, MB_ICONERROR);
		return FALSE;
	}

	len = (int)SendMessage(hEdit, WM_GETTEXTLENGTH, 0, 0);
	if ((buf = (TCHAR *)mem_alloc(sizeof(TCHAR) * (len + 1))) == NULL) {
		GetErrorMessage(GetLastError(), err_str);
		CloseHandle(hFile);
		MessageBox(hWnd, err_str, window_title, MB_ICONERROR);
		return FALSE;
	}
	SendMessage(hEdit, WM_GETTEXT, len + 1, (LPARAM)buf);
	
	// ファイルに書き込む
#ifdef UNICODE
	wbuf = buf;
#else
	// ASCIIからUTF-16に変換
	len = MultiByteToWideChar(CP_ACP, 0, buf, -1, NULL, 0);
	if ((wbuf = (WCHAR *)mem_alloc(sizeof(WCHAR) * len)) == NULL) {
		GetErrorMessage(GetLastError(), err_str);
		mem_free(&buf);
		CloseHandle(hFile);
		MessageBox(hWnd, err_str, window_title, MB_ICONERROR);
		return FALSE;
	}
	MultiByteToWideChar(CP_ACP, 0, buf, -1, wbuf, len);
	mem_free(&buf);
#endif
	// UTF-16からUTF-8に変換
	len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
	if ((cbuf = (BYTE *)mem_alloc(sizeof(BYTE) * len)) == NULL) {
		GetErrorMessage(GetLastError(), err_str);
		mem_free(&wbuf);
		CloseHandle(hFile);
		MessageBox(hWnd, err_str, window_title, MB_ICONERROR);
		return FALSE;
	}
	WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, cbuf, len, NULL, NULL);
	if (WriteFile(hFile, cbuf, sizeof(BYTE) * (len - 1), &ret, NULL) == FALSE) {
		GetErrorMessage(GetLastError(), err_str);
		mem_free(&cbuf);
		mem_free(&wbuf);
		CloseHandle(hFile);
		MessageBox(hWnd, err_str, window_title, MB_ICONERROR);
		return FALSE;
	}
	mem_free(&cbuf);
	mem_free(&wbuf);
	CloseHandle(hFile);
	return TRUE;
}

/*
 * SaveConfirm - 変更の保存確認
 */
static BOOL SaveConfirm(const HWND hWnd)
{
	if (SendMessage(hEdit, EM_GETMODIFY, 0, 0) == TRUE) {
		switch (MessageBox(hWnd, GetResMessage(IDS_STRING_MSG_MODIFY), window_title, MB_ICONEXCLAMATION | MB_YESNOCANCEL)) {
		case IDYES:
			SendMessage(hWnd, WM_COMMAND, ID_MENUITEM_SAVE, 0);
			if (SendMessage(hEdit, EM_GETMODIFY, 0, 0) == TRUE) {
				return FALSE;
			}
			break;

		case IDNO:
			break;

		case IDCANCEL:
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * SetTitle - show the file path (or the name of the online script) in the title
 */
static void SetTitle(const HWND hWnd)
{
	TCHAR buf[BUF_SIZE + MAX_PATH];
	const TCHAR *name = (*file_path != TEXT('\0')) ? file_path : online_get_name();

	if (name == NULL) {
		SetWindowText(hWnd, window_title);
		return;
	}
	wsprintf(buf, TEXT("%s - [%s]"), window_title, name);
	SetWindowText(hWnd, buf);
}

/*
 * SpeedMenuId - menu item of the execution speed nearest to a speed of the web version
 */
static UINT SpeedMenuId(const int speed)
{
	if (speed <= EXEC_SPEED_NO_WAIT) {
		return ID_MENUITEM_NO_WAIT;
	}
	if (speed < (EXEC_SPEED_HIGH + EXEC_SPEED_MID) / 2) {
		return ID_MENUITEM_SPEED_HIGH;
	}
	if (speed < (EXEC_SPEED_MID + EXEC_SPEED_LOW) / 2) {
		return ID_MENUITEM_SPEED_MID;
	}
	return ID_MENUITEM_SPEED_LOW;
}

/*
 * ReplaceExtension - 拡張子を置き換える
 */
static void ReplaceExtension(TCHAR *path, const TCHAR *ext)
{
	TCHAR *p, *r;

	for (p = r = path; *p != TEXT('\0'); p++) {
		if (*p == TEXT('\\') || *p == TEXT('/')) {
			r = NULL;
		} else if (*p == TEXT('.')) {
			r = p;
		}
	}
	if (r == NULL || r == path) {
		r = p;
	}
	lstrcpy(r, ext);
}

/*
 * GenerateExe - 編集中のスクリプトを一時ファイルに書き出し、pg0gen.exe で実行ファイルを作成する
 */
static BOOL GenerateExe(const HWND hWnd)
{
	OPENFILENAME of;
	SECURITY_ATTRIBUTES sa;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	HANDLE hRead = NULL, hWrite = NULL;
	TCHAR path[MAX_PATH + 1], name[MAX_PATH + 1];
	TCHAR out[MAX_PATH + 1];
	TCHAR tmp[MAX_PATH + 1];
	TCHAR gen[MAX_PATH + 1];
	TCHAR *cmd, *result = NULL, *msg, *p;
	char *cbuf = NULL, *tmpbuf;
	DWORD size, len = 0, code = 1;
	BOOL ret;

	if (SendMessage(hEdit, WM_GETTEXTLENGTH, 0, 0) == 0) {
		return FALSE;
	}

	// 出力ファイルの選択 (既定はスクリプトと同じ場所の .exe)
	GetFilePathName(file_path, path, name);
	if (*name != TEXT('\0') && lstrlen(path) + lstrlen(name) + 5 < MAX_PATH) {
		lstrcpy(out, path);
		if (*path != TEXT('\0') && *(path + lstrlen(path) - 1) != TEXT('\\')) {
			lstrcat(out, TEXT("\\"));
		}
		lstrcat(out, name);
		ReplaceExtension(out, TEXT(".exe"));
	} else {
		lstrcpy(out, TEXT("program.exe"));
	}
	ZeroMemory(&of, sizeof(OPENFILENAME));
	of.lStructSize = sizeof(OPENFILENAME);
	of.hwndOwner = hWnd;
	of.lpstrFilter = TEXT("*.exe\0*.exe\0*.*\0*.*\0\0");
	of.lpstrTitle = GetResMessage(IDS_STRING_GEN_EXE_TITLE);
	of.lpstrFile = out;
	// スクリプトのフォルダで開く (lpstrInitialDir が NULL だと前回の場所が優先される)
	of.lpstrInitialDir = (*path != TEXT('\0')) ? path : NULL;
	of.nMaxFile = MAX_PATH - 1;
	of.lpstrDefExt = TEXT("exe");
	of.Flags = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
	if (GetSaveFileName((LPOPENFILENAME)&of) == FALSE) {
		return FALSE;
	}

	// スクリプトを一時ファイルに書き出す (ファイル名はエラー表示に使われるのでスクリプトの名前にする)
	if (*name == TEXT('\0')) {
		GetFilePathName(out, tmp, name);
		ReplaceExtension(name, TEXT(".pg0"));
	}
	if (GetTempPath(MAX_PATH, tmp) == 0 || lstrlen(tmp) + lstrlen(name) + 4 >= MAX_PATH) {
		return FALSE;
	}
	lstrcat(tmp, TEXT("pg0\\"));
	CreateDirectory(tmp, NULL);
	lstrcat(tmp, name);
	if (WriteScriptFile(hWnd, tmp) == FALSE) {
		return FALSE;
	}

	// pg0.exe と同じフォルダの pg0gen.exe を実行する (#import はスクリプトのフォルダを基準にする)
	if (GetModuleFileName(NULL, gen, MAX_PATH) == 0) {
		DeleteFile(tmp);
		return FALSE;
	}
	for (p = gen + lstrlen(gen); p > gen && *(p - 1) != TEXT('\\'); p--);
	if (p - gen + lstrlen(TEXT("pg0gen.exe")) >= MAX_PATH) {
		DeleteFile(tmp);
		return FALSE;
	}
	lstrcpy(p, TEXT("pg0gen.exe"));
	cmd = mem_alloc(sizeof(TCHAR) * (lstrlen(gen) + lstrlen(tmp) + lstrlen(out) + BUF_SIZE));
	if (cmd == NULL) {
		DeleteFile(tmp);
		MessageBox(hWnd, TEXT("Alloc error"), window_title, MB_ICONERROR);
		return FALSE;
	}
	wsprintf(cmd, TEXT("\"%s\" /%s%s \"%s\" \"%s\""), gen,
#ifdef PG05
		TEXT(""),
#else
		(op.pg05_mode == 0) ? TEXT("p") : TEXT(""),
#endif
		(op.strict_val == 0) ? TEXT("") : TEXT("s"), tmp, out);

	// 出力をパイプで受け取る
	ZeroMemory(&sa, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	if (CreatePipe(&hRead, &hWrite, &sa, 0) == FALSE) {
		mem_free(&cmd);
		DeleteFile(tmp);
		return FALSE;
	}
	SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	si.hStdOutput = hWrite;
	si.hStdError = hWrite;
	ZeroMemory(&pi, sizeof(pi));
	ret = CreateProcess(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL,
		(*path != TEXT('\0')) ? path : NULL, &si, &pi);
	mem_free(&cmd);
	CloseHandle(hWrite);
	if (ret == FALSE) {
		CloseHandle(hRead);
		DeleteFile(tmp);
		msg = mem_alloc(sizeof(TCHAR) * (lstrlen(gen) + BUF_SIZE));
		if (msg != NULL) {
			wsprintf(msg, GetResMessage(IDS_STRING_GEN_EXE_ERROR_PG0GEN), gen);
			MessageBox(hWnd, msg, window_title, MB_ICONERROR);
			mem_free(&msg);
		}
		return FALSE;
	}
	while (1) {
		tmpbuf = mem_alloc(len + BUF_SIZE + 1);
		if (tmpbuf == NULL) {
			break;
		}
		if (cbuf != NULL) {
			CopyMemory(tmpbuf, cbuf, len);
			mem_free(&cbuf);
		}
		cbuf = tmpbuf;
		if (ReadFile(hRead, cbuf + len, BUF_SIZE, &size, NULL) == FALSE || size == 0) {
			break;
		}
		len += size;
	}
	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	CloseHandle(hRead);
	DeleteFile(tmp);

	// 出力 (pg0gen.exe のコードページ) を文字列にする
	if (cbuf != NULL) {
		*(cbuf + len) = '\0';
		size = MultiByteToWideChar(CP_ACP, 0, cbuf, -1, NULL, 0);
		result = mem_alloc(sizeof(TCHAR) * (size + 1));
		if (result != NULL) {
			MultiByteToWideChar(CP_ACP, 0, cbuf, -1, result, size);
			for (p = result + lstrlen(result); p > result && (*(p - 1) == TEXT('\r') || *(p - 1) == TEXT('\n')); p--);
			*p = TEXT('\0');
		}
		mem_free(&cbuf);
	}
	if (result == NULL) {
		result = alloc_copy(TEXT(""));
	}

	// 出力を実行結果に表示する
	if (result != NULL && *result != TEXT('\0')) {
		if (SendMessage(hConsoleView, WM_GETTEXTLENGTH, 0, 0) > 0) {
			// 実行時と同じ区切り線
			SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT("\r\n--"));
		}
		OutputTime(hConsoleView);
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)((code == 0) ? TEXT(" \x03")TEXT("12") : TEXT(" \x03")TEXT("04")));
		SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)result);
	}
	msg = mem_alloc(sizeof(TCHAR) * (((result != NULL) ? lstrlen(result) : 0) + lstrlen(out) + BUF_SIZE));
	if (msg != NULL) {
		if (code == 0) {
			wsprintf(msg, GetResMessage(IDS_STRING_GEN_EXE_DONE), out);
			MessageBox(hWnd, msg, window_title, MB_ICONINFORMATION);
		} else {
			wsprintf(msg, GetResMessage(IDS_STRING_GEN_EXE_ERROR), (result != NULL) ? result : TEXT(""));
			MessageBox(hWnd, msg, window_title, MB_ICONERROR);
		}
		mem_free(&msg);
	}
	mem_free(&result);
	return (code == 0);
}

/*
 * SetEnableWindow - メニュー項目の使用可能､ 使用不能を設定
 */
static void SetEnableWindow(const HWND hWnd)
{
	EnterCriticalSection(&cs);
	if (ed.exec_flag == TRUE) {
		SendMessage(hEdit, EM_SETREADONLY, TRUE, 0);
		EnableMenuItem(GetMenu(hWnd), ID_MENUITEM_STOP, MF_ENABLED);
		SendDlgItemMessage(hWnd, IDR_TOOLBAR, TB_ENABLEBUTTON, ID_MENUITEM_STOP, (LPARAM)MAKELONG(TRUE, 0));
	} else {
		SendMessage(hEdit, EM_SETREADONLY, FALSE, 0);
		EnableMenuItem(GetMenu(hWnd), ID_MENUITEM_STOP, MF_GRAYED);
		SendDlgItemMessage(hWnd, IDR_TOOLBAR, TB_ENABLEBUTTON, ID_MENUITEM_STOP, (LPARAM)MAKELONG(FALSE, 0));
	}
	LeaveCriticalSection(&cs);
}

/*
 * Resize - リサイズ
 */
static void Resize(const HWND hWnd)
{
	RECT rect;
	RECT toolbar_rect;
	int toolbar_size = 0;

	GetClientRect(hWnd, &rect);
	GetClientRect(GetDlgItem(hWnd, IDR_TOOLBAR), (LPRECT)&toolbar_rect);
	toolbar_size = (toolbar_rect.bottom - toolbar_rect.top);
	if (rect.right - op.sep_size_vertical <= Scale(10)) {
		op.sep_size_vertical = rect.right - Scale(10);
	}
	if (op.sep_size_vertical <= Scale(10)) {
		op.sep_size_vertical = Scale(10);
	}
	if (rect.bottom - op.sep_size_horizon <= toolbar_rect.top + toolbar_size) {
		op.sep_size_horizon = rect.bottom - Scale(10) - toolbar_size;
	}
	if (op.sep_size_horizon <= Scale(10)) {
		op.sep_size_horizon = Scale(10);
	}
	MoveWindow(hEdit,
		0, toolbar_size,
		rect.right - op.sep_size_vertical, rect.bottom - op.sep_size_horizon - toolbar_size,
		TRUE);
	MoveWindow(hVariableView,
		rect.right - op.sep_size_vertical + SEP_SIZE,
		toolbar_size, op.sep_size_vertical - SEP_SIZE, rect.bottom - op.sep_size_horizon - toolbar_size,
		TRUE);
	MoveWindow(hConsoleView,
		0, rect.bottom - op.sep_size_horizon + SEP_SIZE,
		rect.right, op.sep_size_horizon - SEP_SIZE,
		TRUE);
	UpdateWindow(hWnd);
}

/*
 * MainProc - メインウィンドウプロシージャ
 */
static LRESULT CALLBACK MainProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static TCHAR ini_path[MAX_PATH];
	static BOOL sep_move_vertical, sep_move_horizon;
	static FINDREPLACE fr;
	static TCHAR findBuf[BUF_SIZE];
	TCHAR buf[BUF_SIZE];
	RECT rect;

	switch (msg) {
	case WM_CREATE:
		lstrcpy(window_title, APP_NAME);
		ZeroMemory(&ed, sizeof(EXEC_DATA));
		ed.stop_line = -1;
		//同期オブジェクト
		InitializeCriticalSection(&cs);

		// 検索用メッセージの登録
		uFindMsg = RegisterWindowMessage(FINDMSGSTRING);
		fr.lpstrFindWhat = NULL;

		// ツールバー
		toolbar_create(hInst, hWnd, IDR_TOOLBAR);

		// EDIT作成
		hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, NEDIT_WND_CLASS, TEXT(""),
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_NOHIDESEL,
			0, 0, 0, 0,
			hWnd, (HMENU)IDC_EDIT_EDIT, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

		hVariableView = CreateWindowEx(WS_EX_CLIENTEDGE, VARIABLE_WND_CLASS, TEXT(""),
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_HSCROLL,
			0, 0, 0, 0,
			hWnd, (HMENU)IDC_EDIT_VARIABLE, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
		SendMessage(hVariableView, EM_LIMITTEXT, 0, 0);
		SendMessage(hVariableView, WM_VIEW_SET_HEADER_NAME, 0, (LPARAM)GetResMessage(IDS_STRING_HEADER_NAME));
		SendMessage(hVariableView, WM_VIEW_SET_HEADER_VALUE, 0, (LPARAM)GetResMessage(IDS_STRING_HEADER_VALUE));

		hConsoleView = CreateWindowEx(WS_EX_CLIENTEDGE, CONSOLE_WND_CLASS, TEXT(""),
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_VSCROLL,
			0, 0, 0, 0,
			hWnd, (HMENU)IDC_EDIT_CONSOLE, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
		SendMessage(hConsoleView, EM_LIMITTEXT, 0, 0);

		SetEnableWindow(hWnd);
		hFocusWnd = hEdit;

		// INIファイル
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, ini_path))) {
			lstrcat(ini_path, TEXT("\\pg0"));
			CreateDirectory(ini_path, NULL);
			lstrcat(ini_path, TEXT("\\pg0.ini"));
			GetIni(hWnd, ini_path);
		}

		// コマンドライン
		if (*cmd_line != TEXT('\0') && ReadScriptfile(hWnd, cmd_line) == TRUE) {
			lstrcpy(file_path, cmd_line);
			SetFileStamp();
			online_clear();
			wsprintf(buf, TEXT("%s - [%s]"), window_title, file_path);
			SetWindowText(hWnd, buf);
			SendMessage(hVariableView, WM_VIEW_SETVARIABLE, 0, 0);
		}
		if (op.confirm_tutorial == 1) {
			SetTimer(hWnd, TIMER_CONFIRM_TUTORIAL, 1, NULL);
		}
		SetTimer(hWnd, TIMER_FILE_WATCH, FILE_WATCH_INTERVAL, NULL);
		break;

	case WM_ACTIVATE:
		if (LOWORD(wParam) == WA_INACTIVE) {
			if (IsIconic(hWnd) == 0 && GetFocus() != NULL) {
				hFocusWnd = GetFocus();
			}
		}
		break;

	case WM_SETFOCUS:
		SetFocus(hFocusWnd);
		break;

	case WM_LBUTTONDOWN:
		// 境界の移動
		if (GetForegroundWindow() != hWnd) {
			break;
		}
		if (sep_move_vertical == FALSE && sep_move_horizon == FALSE) {
			POINT apos;
			if (InitializeFrame(hWnd) == FALSE) {
				break;
			}
			GetCursorPos((LPPOINT)&apos);
			GetWindowRect(hConsoleView, (LPRECT)&rect);
			if (apos.y < rect.top - 5) {
				sep_move_vertical = TRUE;
			} else {
				sep_move_horizon = TRUE;
			}
			SetTimer(hWnd, TIMER_SEP, 1, NULL);
		}
		break;

	case WM_MOUSEMOVE:
		if (sep_move_vertical == TRUE) {
			int ret = DrawVerticalFrame(hWnd, hEdit);
			GetClientRect(hWnd, &rect);
			op.sep_size_vertical = rect.right - ret;
			Resize(hWnd);
		} else if (sep_move_horizon == TRUE) {
			RECT toolbar_rect;
			int ret = DrawHorizonFrame(hWnd, hConsoleView);
			GetClientRect(hWnd, &rect);
			GetClientRect(GetDlgItem(hWnd, IDR_TOOLBAR), (LPRECT)&toolbar_rect);
			op.sep_size_horizon = rect.bottom - ret + (toolbar_rect.bottom - toolbar_rect.top);
			Resize(hWnd);
		}
		break;

	case WM_LBUTTONUP:
		if (sep_move_vertical == TRUE || sep_move_horizon == TRUE) {
			KillTimer(hWnd, TIMER_SEP);
			FreeFrame();
			sep_move_vertical = FALSE;
			sep_move_horizon = FALSE;
		}
		break;

	case WM_TIMER:
		switch (wParam) {
		// 境界の移動
		case TIMER_SEP:
			if (hWnd != GetForegroundWindow() || GetAsyncKeyState(VK_ESCAPE) < 0 || GetAsyncKeyState(VK_RBUTTON) < 0) {
				SendMessage(hWnd, WM_LBUTTONUP, 0, 0);
			}
			break;
		case TIMER_CONFIRM_TUTORIAL:
			KillTimer(hWnd, TIMER_CONFIRM_TUTORIAL);
			{
				int ret = MessageBox(hWnd, GetResMessage(IDS_STRING_CONFIRM_TUTORIAL), APP_NAME, MB_ICONINFORMATION | MB_YESNOCANCEL);
				if (ret == IDYES) {
					SendMessage(hWnd, WM_COMMAND, ID_MENUITEM_TUTORIAL, 0);
				} else if (ret == IDNO) {
					op.confirm_tutorial = 0;
				}
			}
			break;
		case TIMER_FILE_WATCH:
			CheckFileChange(hWnd);
			break;
		}
		break;

	case WM_SIZE:
		if (IsIconic(hWnd) == 0) {
			op.window_state = (IsZoomed(hWnd) == 0) ? SW_SHOWDEFAULT : SW_MAXIMIZE;
			SendMessage(GetDlgItem(hWnd, IDR_TOOLBAR), WM_SIZE, wParam, lParam);
			Resize(hWnd);
		}
		break;

	case WM_EXITSIZEMOVE:
		// サイズ変更完了
		if (IsWindowVisible(hWnd) != 0 && IsIconic(hWnd) == 0 && IsZoomed(hWnd) == 0) {
			GetWindowRect(hWnd, (LPRECT)&op.window_rect);
			op.window_rect.right -= op.window_rect.left;
			op.window_rect.bottom -= op.window_rect.top;
		}
		break;

	case WM_QUERYENDSESSION:
		if (SaveConfirm(hWnd) == FALSE) {
			return FALSE;
		}
		PutIni(hWnd, ini_path);
		return TRUE;

	case WM_ENDSESSION:
		if ((BOOL)wParam == FALSE) {
			return 0;
		}
		DestroyWindow(hWnd);
		return 0;

	case WM_CLOSE:
		if (SaveConfirm(hWnd) == FALSE) {
			return 0;
		}
		PutIni(hWnd, ini_path);
		KillTimer(hWnd, TIMER_FILE_WATCH);
		mem_free(&reload_base_text);
		EnterCriticalSection(&cs);
		ed.stop_flag = TRUE;
		LeaveCriticalSection(&cs);

		DestroyWindow(hEdit);
		DestroyWindow(hVariableView);
		DestroyWindow(hConsoleView);
#ifdef _DEBUG
		mem_debug();
#endif
		DestroyWindow(hWnd);
		break;

	case WM_DESTROY:
		if (hThread != NULL) {
			TerminateThread(hThread, 0);
		}
		DeleteCriticalSection(&cs);
		PostQuitMessage(0);
		break;

	case WM_DROPFILES:
		EnterCriticalSection(&cs);
		if (ed.exec_flag == TRUE) {
			LeaveCriticalSection(&cs);
			break;
		}
		LeaveCriticalSection(&cs);
		if (SaveConfirm(hWnd) == FALSE) {
			break;
		}
		DragQueryFile((HANDLE)wParam, 0, buf, BUF_SIZE - 1);
		if (ReadScriptfile(hWnd, buf) == TRUE) {
			lstrcpy(file_path, buf);
			SetFileStamp();
			online_clear();
			wsprintf(buf, TEXT("%s - [%s]"), window_title, file_path);
			SetWindowText(hWnd, buf);
			SendMessage(hVariableView, WM_VIEW_SETVARIABLE, 0, 0);
		}
		DragFinish((HANDLE)wParam);
		break;

	case WM_INITMENUPOPUP:
		// the revision history needs a script opened from or saved to the online storage
		if (LOWORD(lParam) == 0) {
			EnableMenuItem((HMENU)wParam, ID_MENUITEM_ONLINE_HISTORY, (online_get_cid() != NULL) ? MF_ENABLED : MF_GRAYED);
			EnableMenuItem((HMENU)wParam, ID_MENUITEM_RELOAD_DIFF, (reload_base_text != NULL) ? MF_ENABLED : MF_GRAYED);
		}
		if (LOWORD(lParam) == 1) {
			DWORD st = 0, en = 0;
			if (GetFocus() == hVariableView) {
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_UNDO, MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_REDO, MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_CUT, MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_COPY, (SendMessage(hVariableView, WM_VIEW_IS_SELECTION, 0, 0) == TRUE) ? MF_ENABLED : MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_PASTE, MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_DELETE, MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_SELECT_ALL, MF_GRAYED);
			} else if (GetFocus() == hConsoleView) {
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_UNDO, MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_REDO, MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_CUT, MF_GRAYED);
				SendMessage(hConsoleView, EM_GETSEL, (WPARAM)&st, (LPARAM)&en);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_COPY, (st != en) ? MF_ENABLED : MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_PASTE, MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_DELETE, MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_SELECT_ALL, MF_ENABLED);
			} else {
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_UNDO, (SendMessage(hEdit, EM_CANUNDO, 0, 0) == TRUE) ? MF_ENABLED : MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_REDO, (SendMessage(hEdit, EM_CANREDO, 0, 0) == TRUE) ? MF_ENABLED : MF_GRAYED);
				SendMessage(hEdit, EM_GETSEL, (WPARAM)&st, (LPARAM)&en);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_CUT, (st != en) ? MF_ENABLED : MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_COPY, (st != en) ? MF_ENABLED : MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_PASTE, MF_ENABLED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_DELETE, (st != en) ? MF_ENABLED : MF_GRAYED);
				EnableMenuItem((HMENU)wParam, ID_MENUITEM_SELECT_ALL, MF_ENABLED);
			}
		}
		break;

	case WM_NOTIFY:
		// ツールバー
		if (((NMHDR *)lParam)->code == TTN_NEEDTEXT) {
			((TOOLTIPTEXT*)lParam)->hinst = hInst;
			((TOOLTIPTEXT*)lParam)->lpszText = MAKEINTRESOURCE(((NMHDR *)lParam)->idFrom);
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_MENUITEM_NEW:
			EnterCriticalSection(&cs);
			if (ed.exec_flag == TRUE) {
				LeaveCriticalSection(&cs);
				break;
			}
			LeaveCriticalSection(&cs);
			if (SaveConfirm(hWnd) == FALSE) {
				break;
			}
			*file_path = TEXT('\0');
			SetFileStamp();
			SendMessage(hEdit, WM_SETTEXT, 0, (LPARAM)TEXT(""));
			online_clear();
			SetWindowText(hWnd, window_title);
			SendMessage(hVariableView, WM_VIEW_SETVARIABLE, 0, 0);
			break;

		case ID_MENUITEM_OPEN:
			EnterCriticalSection(&cs);
			if (ed.exec_flag == TRUE) {
				LeaveCriticalSection(&cs);
				break;
			}
			LeaveCriticalSection(&cs);
			if (SaveConfirm(hWnd) == FALSE) {
				break;
			}
			*buf = TEXT('\0');
			if (ReadScriptfile(hWnd, buf) == TRUE) {
				lstrcpy(file_path, buf);
				SetFileStamp();
				online_clear();
				wsprintf(buf, TEXT("%s - [%s]"), window_title, file_path);
				SetWindowText(hWnd, buf);
				SendMessage(hVariableView, WM_VIEW_SETVARIABLE, 0, 0);
			}
			break;

		case ID_MENUITEM_SAVE:
			if (SaveFile(hWnd, file_path) == TRUE) {
				SetFileStamp();
			}
			break;

		case ID_MENUITEM_SAVEAS:
			*buf = TEXT('\0');
			if (SaveFile(hWnd, buf) == TRUE) {
				lstrcpy(file_path, buf);
				SetFileStamp();
				wsprintf(buf, TEXT("%s - [%s]"), window_title, file_path);
				SetWindowText(hWnd, buf);
			}
			break;

		case ID_MENUITEM_GEN_EXE:
			EnterCriticalSection(&cs);
			if (ed.exec_flag == TRUE) {
				LeaveCriticalSection(&cs);
				break;
			}
			LeaveCriticalSection(&cs);
			GenerateExe(hWnd);
			break;

		case ID_MENUITEM_RELOAD_DIFF:
			ShowReloadDiff(hWnd);
			break;

		case ID_MENUITEM_ONLINE_OPEN:
		case ID_MENUITEM_ONLINE_HISTORY:
			EnterCriticalSection(&cs);
			if (ed.exec_flag == TRUE) {
				LeaveCriticalSection(&cs);
				break;
			}
			LeaveCriticalSection(&cs);
			if (LOWORD(wParam) == ID_MENUITEM_ONLINE_HISTORY && online_get_cid() == NULL) {
				break;
			}
			if (SaveConfirm(hWnd) == FALSE) {
				break;
			}
			{
				ONLINE_SCRIPT script;
				BOOL ret;

				// the diff in the revision history shows the code as the editor does
				online_set_code_view(&lf, (op.line_no != 0));
				if (LOWORD(wParam) == ID_MENUITEM_ONLINE_HISTORY) {
					ret = online_history(hWnd, &script);
				} else {
					ret = online_open(hWnd, &script);
				}
				if (ret == TRUE) {
					*file_path = TEXT('\0');
					SetFileStamp();
					SendMessage(hEdit, WM_SETMEM, sizeof(TCHAR) * lstrlen(script.code), (LPARAM)script.code);
					mem_free(&script.code);
					SendMessage(hEdit, EM_SETMODIFY, (WPARAM)FALSE, 0);
					// the execution mode and speed saved with the script, as the web version does
					if ((op.pg05_mode != 0) != script.pg05_mode) {
						SendMessage(hWnd, WM_COMMAND, ID_MENUITEM_PG05, 0);
					}
					if (script.has_speed == TRUE) {
						SendMessage(hWnd, WM_COMMAND, SpeedMenuId(script.speed), 0);
					}
					SetTitle(hWnd);
					SendMessage(hVariableView, WM_VIEW_SETVARIABLE, 0, 0);
				}
			}
			break;

		case ID_MENUITEM_ONLINE_SAVE:
			{
				TCHAR *text, *p;
				TCHAR *name = NULL;
				int len, speed;

				len = (int)SendMessage(hEdit, WM_GETTEXTLENGTH, 0, 0);
				if ((text = (TCHAR *)mem_alloc(sizeof(TCHAR) * (len + 1))) == NULL) {
					MessageBox(hWnd, TEXT("Alloc error"), window_title, MB_ICONERROR);
					break;
				}
				SendMessage(hEdit, WM_GETTEXT, len + 1, (LPARAM)text);
				// the name of the local file is offered, as the web version does
				for (p = file_path; *p != TEXT('\0'); p++) {
					if (*p == TEXT('\\') || *p == TEXT('/')) {
						name = p + 1;
					}
				}
				if (name == NULL && *file_path != TEXT('\0')) {
					name = file_path;
				}
				EnterCriticalSection(&cs);
				speed = ed.exec_speed;
				LeaveCriticalSection(&cs);
				if (online_save(hWnd, text, name, (op.pg05_mode != 0), speed) == TRUE) {
					// a script read from a local file is still to be saved to the file
					if (*file_path == TEXT('\0')) {
						SendMessage(hEdit, EM_SETMODIFY, (WPARAM)FALSE, 0);
					}
					SetTitle(hWnd);
				}
				mem_free(&text);
			}
			break;

		case ID_MENUITEM_FONT:
			{
				HFONT hFont;

				if ((hFont = SelectFont(hWnd)) != NULL) {
					SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
					SendMessage(hConsoleView, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
					SendMessage(hVariableView, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
					DeleteObject(hFont);
				}
			}
			break;

		case ID_MENUITEM_LINE_NO:
			op.line_no = !op.line_no;
			SendMessage(hEdit, WM_SHOW_LINE_NO, (WPARAM)(op.line_no == 0) ? FALSE : TRUE, 0);
			CheckMenuItem(GetMenu(hWnd), ID_MENUITEM_LINE_NO, (op.line_no == 0) ? MF_UNCHECKED : MF_CHECKED);
			break;

		case ID_MENUITEM_PG05:
			op.pg05_mode = !op.pg05_mode;
			SendMessage(hEdit, WM_SET_EXTENSION, (WPARAM)(op.pg05_mode == 0) ? FALSE : TRUE, 0);
			if (op.pg05_mode == 0) {
				CheckMenuItem(GetMenu(hWnd), ID_MENUITEM_PG05, MF_UNCHECKED);
				lstrcpy(window_title, APP_NAME);
			} else {
				CheckMenuItem(GetMenu(hWnd), ID_MENUITEM_PG05, MF_CHECKED);
				lstrcpy(window_title, WINDOW_TITLE_EX);
			}
			SetTitle(hWnd);
			break;

		case ID_MENUITEM_CLOSE:
			SendMessage(hWnd, WM_CLOSE, 0, 0);
			break;

		case ID_MENUITEM_UNDO:
			SendMessage(GetFocus(), EM_UNDO, 0, 0);
			break;
		
		case ID_MENUITEM_REDO:
			SendMessage(GetFocus(), EM_REDO, 0, 0);
			break;

		case ID_MENUITEM_CUT:
			SendMessage(GetFocus(), WM_CUT, 0, 0);
			break;

		case ID_MENUITEM_COPY:
			SendMessage(GetFocus(), WM_COPY, 0, 0);
			break;

		case ID_MENUITEM_PASTE:
			SendMessage(GetFocus(), WM_PASTE, 0, 0);
			break;

		case ID_MENUITEM_DELETE:
			SendMessage(GetFocus(), WM_CLEAR, 0, 0);
			break;

		case ID_MENUITEM_FIND:
			if (hFindWnd != NULL) {
				SetForegroundWindow(hFindWnd);
			} else {
				DWORD st = 0, en = 0;
				SendMessage(hEdit, EM_GETSEL, (WPARAM)&st, (LPARAM)&en);
				if (st != en) {
					TCHAR *buf;
					int len = (int)SendMessage(hEdit, WM_GETTEXTLENGTH, 0, 0);
					if ((buf = (TCHAR *)mem_alloc(sizeof(TCHAR) * (len + 1))) == NULL) {
						MessageBox(hWnd, TEXT("Alloc error"), window_title, MB_ICONERROR);
						break;
					}
					SendMessage(hEdit, WM_GETTEXT, len + 1, (LPARAM)buf);
					*(buf + len) = TEXT('\0');
					lstrcpyn(findBuf, buf + st, en - st + 1);
					mem_free(&buf);
				}
				fr.lStructSize = sizeof(FINDREPLACE);
				fr.hwndOwner = hWnd;
				fr.Flags = FR_HIDEUPDOWN | FR_HIDEWHOLEWORD;
				fr.lpstrFindWhat = findBuf;
				fr.wFindWhatLen = BUF_SIZE;
				hFindWnd = FindText(&fr);
			}
			break;

		case ID_MENUITEM_FINDNEXT:
			if (fr.lpstrFindWhat != NULL) {
				SendMessage(hEdit, WM_FINDTEXT, (fr.Flags & FR_MATCHCASE) ? 1 : 0, (LPARAM)fr.lpstrFindWhat);
			}
			break;

		case ID_MENUITEM_SELECT_ALL:
			SendMessage(GetFocus(), EM_SETSEL, 0, -1);
			SendMessage(GetFocus(), EM_SCROLLCARET, 0, 0);
			break;

		case ID_MENUITEM_EXEC:
			EnterCriticalSection(&cs);
			ed.stop_line = -1;
			if (ed.exec_flag == TRUE) {
				if (ed.step_flag == TRUE) {
					ed.step_flag = FALSE;
					ed.step_next_flag = TRUE;
				}
				if (ed.exec_speed == 0) {
					SendMessage(hEdit, WM_SET_HIGHLIGHT_LINE, 0, (LPARAM)-1);
				}
				LeaveCriticalSection(&cs);
				break;
			}
			ed.step_flag = FALSE;
			LeaveCriticalSection(&cs);
			ExecScriptThread(hWnd);
			break;

		case ID_MENUITEM_STEP:
			EnterCriticalSection(&cs);
			ed.stop_line = -1;
			if (ed.exec_flag == TRUE) {
				if (ed.step_flag == FALSE) {
					ed.step_flag = TRUE;
					ed.step_next_flag = FALSE;
				} else {
					ed.step_next_flag = TRUE;
				}
				LeaveCriticalSection(&cs);
				break;
			}
			ed.step_flag = TRUE;
			ed.step_next_flag = FALSE;
			LeaveCriticalSection(&cs);
			ExecScriptThread(hWnd);
			break;

		case ID_MENUITEM_RUN_TO_CURSOR:
			EnterCriticalSection(&cs);
			ed.stop_line = (int)SendMessage(hEdit, EM_LINEFROMCHAR, (WPARAM)-1, 0);
			if (ed.exec_flag == TRUE) {
				if (ed.step_flag == TRUE) {
					ed.step_flag = FALSE;
					ed.step_next_flag = TRUE;
				}
				LeaveCriticalSection(&cs);
				break;
			}
			ed.step_flag = TRUE;
			ed.step_next_flag = FALSE;
			LeaveCriticalSection(&cs);
			ExecScriptThread(hWnd);
			break;

		case ID_MENUITEM_STOP:
			EnterCriticalSection(&cs);
			if (ed.stop_flag == TRUE) {
				if (hThread != NULL) {
					TerminateThread(hThread, 0);
					hThread = NULL;
				}
				SendMessage(hEdit, WM_SET_HIGHLIGHT_LINE, 0, (LPARAM)-1);
				ed.exec_flag = FALSE;
				SetEnableWindow(hWnd);
				LeaveCriticalSection(&cs);
				OutputTime(hConsoleView);
				SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)TEXT(" \x03")TEXT("12"));
				SendMessage(hConsoleView, WM_VIEW_ADDTEXT, 0, (LPARAM)GetResMessage(IDS_STRING_CONSOLE_STOP));
				break;
			}
			ed.stop_flag = TRUE;
			if (ed.step_flag == TRUE) {
				ed.step_next_flag = TRUE;
			}
			LeaveCriticalSection(&cs);
			break;

		case ID_MENUITEM_CLEAR:
			SendMessage(hConsoleView, WM_SETTEXT, 0, (LPARAM)TEXT(""));
			EnterCriticalSection(&cs);
			if (ed.exec_flag == TRUE) {
				LeaveCriticalSection(&cs);
				break;
			}
			LeaveCriticalSection(&cs);
			SendMessage(hVariableView, WM_VIEW_SETVARIABLE, 0, 0);
			break;

		case ID_MENUITEM_NO_WAIT:
			EnterCriticalSection(&cs);
			ed.exec_speed = EXEC_SPEED_NO_WAIT;
			CheckMenuRadioItem(GetMenu(hWnd), ID_MENUITEM_NO_WAIT, ID_MENUITEM_SPEED_LOW, ID_MENUITEM_NO_WAIT, MF_BYCOMMAND);
			if (ed.exec_flag == TRUE) {
				SendMessage(hEdit, WM_SET_HIGHLIGHT_LINE, 0, (LPARAM)-1);
			}
			LeaveCriticalSection(&cs);
			break;

		case ID_MENUITEM_SPEED_HIGH:
			EnterCriticalSection(&cs);
			ed.exec_speed = EXEC_SPEED_HIGH;
			CheckMenuRadioItem(GetMenu(hWnd), ID_MENUITEM_NO_WAIT, ID_MENUITEM_SPEED_LOW, ID_MENUITEM_SPEED_HIGH, MF_BYCOMMAND);
			LeaveCriticalSection(&cs);
			break;

		case ID_MENUITEM_SPEED_MID:
			EnterCriticalSection(&cs);
			ed.exec_speed = EXEC_SPEED_MID;
			CheckMenuRadioItem(GetMenu(hWnd), ID_MENUITEM_NO_WAIT, ID_MENUITEM_SPEED_LOW, ID_MENUITEM_SPEED_MID, MF_BYCOMMAND);
			LeaveCriticalSection(&cs);
			break;

		case ID_MENUITEM_SPEED_LOW:
			EnterCriticalSection(&cs);
			ed.exec_speed = EXEC_SPEED_LOW;
			CheckMenuRadioItem(GetMenu(hWnd), ID_MENUITEM_NO_WAIT, ID_MENUITEM_SPEED_LOW, ID_MENUITEM_SPEED_LOW, MF_BYCOMMAND);
			LeaveCriticalSection(&cs);
			break;

		case ID_MENUITEM_TUTORIAL:
			{
				TCHAR app_path[MAX_PATH];
				TCHAR *p, *r;
				WORD lang;

				lang = PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale()));
				if (lang == LANG_JAPANESE) {
					// アプリケーションのパスを取得
					GetModuleFileName(hInst, app_path, MAX_PATH - 1);
					for (p = r = app_path; *p != TEXT('\0'); p++) {
#ifndef UNICODE
						if (IsDBCSLeadByte((BYTE)*p) == TRUE) {
							p++;
							continue;
						}
#endif	// UNICODE
						if (*p == TEXT('\\') || *p == TEXT('/')) {
							r = p;
						}
					}
					*r = TEXT('\0');
					if (ShellExecute(NULL, TEXT("open"), TEXT("tutorial.html"), NULL, app_path, SW_SHOWNORMAL) <= (HINSTANCE)32) {
						lang = !LANG_JAPANESE;
					}
				}
				if (lang != LANG_JAPANESE) {
					ShellExecute(NULL, TEXT("open"), TEXT("https://nakka.com/soft/pg0/tutorial/index_eng.html"), NULL, NULL, SW_SHOWNORMAL);
				}
			}
			break;

		case ID_MENUITEM_ABOUT:
			// バージョン情報
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
				lstrcat(var_msg, TEXT("\nCopyright (C) 1996-2026 by Ohno Tomoaki. All rights reserved.\n\n")
					TEXT("WEB SITE: https://nakka.com/\nE-MAIL: nakka@nakka.com"));
				MessageBox(hWnd, var_msg, TEXT("About"), MB_OK | MB_ICONINFORMATION);
			}
			break;
		}
		break;

	default:
		if (msg == uFindMsg) {
			if ((fr.Flags & FR_DIALOGTERM) == 0) {
				SendMessage(hEdit, WM_FINDTEXT, (fr.Flags & FR_MATCHCASE) ? 1 : 0, (LPARAM)fr.lpstrFindWhat);
			} else{
				hFindWnd = NULL;
			}
			break;
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

/*
 * InitApplication - ウィンドウクラスの登録
 */
static BOOL InitApplication(HINSTANCE hInstance)
{
	WNDCLASS wc;

	wc.style = 0;
	wc.hCursor = LoadCursor(NULL, IDC_SIZEALL);
	wc.lpszMenuName = MAKEINTRESOURCE(IDR_MENU);
	wc.lpfnWndProc = (WNDPROC)MainProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON_MAIN));
	wc.hbrBackground = (HBRUSH)COLOR_BTNSHADOW;
	wc.lpszClassName = MAIN_WND_CLASS;
	// ウィンドウクラスの登録
	return RegisterClass(&wc);
}

/*
 * InitInstance - ウィンドウの作成
 */
static HWND InitInstance(HINSTANCE hInstance, int CmdShow)
{
	HWND hWnd =  NULL;

	// ウィンドウの作成
	hWnd = CreateWindowEx(WS_EX_ACCEPTFILES,
		MAIN_WND_CLASS,
		APP_NAME,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		NULL, NULL, hInstance, NULL);
	if (hWnd == NULL) {
		return NULL;
	}

	// ウィンドウの表示
	ShowWindow(hWnd, (op.window_state == SW_MAXIMIZE) ? SW_MAXIMIZE : CmdShow);
	UpdateWindow(hWnd);
	return hWnd;
}

/*
 * _tWinMain - メイン
 */
int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
	MSG msg;
	HANDLE hAccel;
	HWND hWnd;

	hInst = hInstance;

    if (*lpCmdLine == TEXT('\"')) {
		TCHAR *p;
		lstrcpy(cmd_line, lpCmdLine + 1);
		for (p = cmd_line; *p != TEXT('\0') && *p != TEXT('\"'); p++)
			;
		if (*p == TEXT('\"')) {
			*p = TEXT('\0');
		}
	} else {
		lstrcpy(cmd_line, lpCmdLine);
	}

	// DPIの初期化
	InitDpi();

	// スクリプトの初期化
	InitializeScript();

	// ウィンドウクラスの登録
	if (RegisterNedit(hInstance) == FALSE) {
		return 0;
	}
	if (RegisterVariableWindow(hInstance) == FALSE) {
		return 0;
	}
	if (RegisterConsoleWindow(hInstance) == FALSE) {
		return 0;
	}
	if (online_initialize(hInstance) == FALSE) {
		return 0;
	}
	if (InitApplication(hInstance) == FALSE) {
		return 0;
	}
	// ウィンドウの作成
	if ((hWnd = InitInstance(hInstance, nCmdShow)) == NULL) {
		return 0;
	}

	// リソースからアクセラレータをロード
	hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR));
	// ウィンドウメッセージ処理
	while (GetMessage(&msg, NULL, 0, 0)) {
		if (hFindWnd != NULL && IsDialogMessage(hFindWnd, &msg)) {
			continue;
		}
		if (!TranslateAccelerator(hWnd, hAccel, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (int)msg.wParam;
}
/* End of source */
