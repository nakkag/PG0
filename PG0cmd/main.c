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
#include <stdlib.h>
#include <locale.h>
#include <float.h>
#include <io.h>
#include <fcntl.h>

#include "../PG0/script.h"
#include "../PG0/script_string.h"
#include "../PG0/script_memory.h"
#include "../PG0/script_utility.h"

#pragma comment(lib, "Version.lib")

/* Define */
#define APP_NAME						TEXT("PG0cmd")

#define BUF_SIZE	256
#define LINE_SIZE	32768

// 終了コード
#define EXIT_CODE_OK					0
#define EXIT_CODE_ERROR					1	// 構文エラー、実行時エラー、ファイルやオプションの誤り
#define EXIT_CODE_LIMIT					2	// 実行時間、ステップ数の上限で停止

// 実行の状態
#define RUN_OK						0
#define RUN_ERROR					1
#define RUN_TIMEOUT					2
#define RUN_STEP_LIMIT				3

// JSON に入れる出力の上限 (文字数)
#define OUTPUT_MAX						1000000
// 実行時間の上限を過ぎても止まらないスクリプトを強制終了するまでの猶予 (ミリ秒)
#define WATCHDOG_GRACE					3000
// 実行時間を確認する間隔 (ステップ数)
#define TIME_CHECK_STEPS				256

// 画面ライブラリ
#define SCREEN_LIBRARY					TEXT("pg0_screen.dll")
#define SCREEN_HEADLESS_ENV				TEXT("PG0_SCREEN_HEADLESS")
#define SCREEN_TOUCH_MOVE				0
#define SCREEN_TOUCH_DOWN				1
#define SCREEN_TOUCH_UP					2
#define SCREEN_KEY_TIME					100		// key のキーを押している時間の既定値 (ミリ秒)
#define SCREEN_TAP_TIME					50		// tap のタッチしている時間 (ミリ秒)
#define SCREEN_DRAG_TIME				300		// drag の移動時間の既定値 (ミリ秒)
#define SCREEN_DRAG_INTERVAL			10		// drag の移動の間隔 (ミリ秒)
#define SCREEN_SHOT_MAX					32		// 結果に載せる画像の数の上限

/* Struct */
// 伸長する文字列
typedef struct _TEXTBUF {
	TCHAR *buf;
	int len;
	int size;
	BOOL truncated;
} TEXTBUF;

// 画面の操作
typedef enum {
	ACT_WAIT = 0,
	ACT_KEY,
	ACT_KEYDOWN,
	ACT_KEYUP,
	ACT_TAP,
	ACT_PRESS,
	ACT_DRAG,
	ACT_MOVE,
	ACT_SCREEN,
} ACTION_TYPE;

typedef struct _ACTION {
	ACTION_TYPE type;
	TCHAR *str;				// キー名、画像のパス
	double x1, y1, x2, y2;
	DWORD ms;
	struct _ACTION *next;
} ACTION;

// 画面ライブラリの関数
typedef BOOL (SFUNC *SCREEN_SIZE)(int *w, int *h);
typedef BOOL (SFUNC *SCREEN_SAVE)(const TCHAR *path);
typedef void (SFUNC *SCREEN_KEY)(const TCHAR *name, BOOL down);
typedef void (SFUNC *SCREEN_TOUCH)(int action, double x, double y);

/* Global Variables */
TCHAR AppDir[MAX_PATH + 1];
BOOL op_pg0 = FALSE;
BOOL op_hex = FALSE;
static BOOL op_strict = FALSE;
static BOOL op_check = FALSE;
static BOOL op_json = FALSE;
static BOOL op_utf8 = FALSE;
static BOOL op_headless = FALSE;
static DWORD op_timeout = 0;
static unsigned __int64 op_max_steps = 0;
static TCHAR op_screenshot[MAX_PATH + 1];
static TCHAR op_screen_input[MAX_PATH + 1];
static TCHAR op_input[MAX_PATH + 1];

// 実行の結果 (cs で保護する)
static CRITICAL_SECTION cs;
static TEXTBUF out_buf;
static TEXTBUF err_buf;
static TCHAR *err_message = NULL;
static int err_line = 0;
static int engine_error_count = 0;
static BOOL finished = FALSE;
static TCHAR screenshots[SCREEN_SHOT_MAX][MAX_PATH + 1];
static int screenshot_count = 0;

static BOOL exec_phase = FALSE;
static BOOL mode_pg05 = FALSE;
static volatile LONG stop_status = RUN_OK;
static unsigned __int64 steps = 0;
static DWORD start_tick = 0;
static HANDLE hDone = NULL;

static TCHAR *input_text = NULL;
static TCHAR *input_pos = NULL;
static int input_lines = 0;
static BOOL input_first = TRUE;

static ACTION *actions = NULL;

/* Local Function Prototypes */
static BOOL text_add(TEXTBUF *tb, const TCHAR *str, const int max);
static void text_add_json_string(TEXTBUF *tb, const TCHAR *str);
static void text_add_json_value(TEXTBUF *tb, VALUEINFO *vi);
static void text_add_json_array(TEXTBUF *tb, VALUEINFO *vi);
static void write_utf8(const TCHAR *str);
static void write_result(const int status, VALUEINFO *rvi, VALUEINFO *vars, const BOOL has_vars);
static void write_check_result(const BOOL ok);
static HMODULE screen_module(void);
static BOOL screen_save(const TCHAR *path);
static void free_actions(void);
static BOOL read_actions(const TCHAR *path);
static DWORD WINAPI action_thread(LPVOID param);
static DWORD WINAPI watchdog_thread(LPVOID param);
static int SFUNC Callback(EXECINFO *ei, TOKEN *cu_tk);
static void FormatValue(VALUEINFO *vi);
static void PrintValue(VALUEINFO *vi);
static void LineExecLoop();
static void ShowHelp(void);
static void ShowVersion(void);
static int ExecFile(TCHAR *path, int argc, TCHAR **argv);

/*
 * text_add - 文字列の追加 (max > 0 の場合は max 文字で打ち切る)
 */
static BOOL text_add(TEXTBUF *tb, const TCHAR *str, const int max)
{
	TCHAR *tmp;
	int len = lstrlen(str);
	int size;

	if (max > 0 && tb->len + len > max) {
		len = max - tb->len;
		tb->truncated = TRUE;
		if (len <= 0) {
			return FALSE;
		}
	}
	if (tb->len + len + 1 > tb->size) {
		size = (tb->size == 0) ? BUF_SIZE : tb->size;
		while (size < tb->len + len + 1) {
			size *= 2;
		}
		tmp = mem_alloc(sizeof(TCHAR) * size);
		if (tmp == NULL) {
			return FALSE;
		}
		if (tb->buf != NULL) {
			CopyMemory(tmp, tb->buf, sizeof(TCHAR) * tb->len);
			mem_free(&tb->buf);
		}
		tb->buf = tmp;
		tb->size = size;
	}
	CopyMemory(tb->buf + tb->len, str, sizeof(TCHAR) * len);
	tb->len += len;
	*(tb->buf + tb->len) = TEXT('\0');
	return TRUE;
}

/*
 * text_add_json_string - JSON の文字列の追加
 */
static void text_add_json_string(TEXTBUF *tb, const TCHAR *str)
{
	TCHAR buf[BUF_SIZE];
	int i = 0;

	text_add(tb, TEXT("\""), 0);
	for (; str != NULL && *str != TEXT('\0'); str++) {
		if (i >= BUF_SIZE - 8) {
			buf[i] = TEXT('\0');
			text_add(tb, buf, 0);
			i = 0;
		}
		switch (*str) {
		case TEXT('\"'): buf[i++] = TEXT('\\'); buf[i++] = TEXT('\"'); break;
		case TEXT('\\'): buf[i++] = TEXT('\\'); buf[i++] = TEXT('\\'); break;
		case TEXT('\n'): buf[i++] = TEXT('\\'); buf[i++] = TEXT('n'); break;
		case TEXT('\r'): buf[i++] = TEXT('\\'); buf[i++] = TEXT('r'); break;
		case TEXT('\t'): buf[i++] = TEXT('\\'); buf[i++] = TEXT('t'); break;
		default:
			if ((unsigned int)*str < 0x20) {
				i += wsprintf(buf + i, TEXT("\\u%04x"), (unsigned int)*str);
			} else {
				buf[i++] = *str;
			}
			break;
		}
	}
	buf[i] = TEXT('\0');
	text_add(tb, buf, 0);
	text_add(tb, TEXT("\""), 0);
}

/*
 * text_add_json_value - 値を JSON にして追加
 */
static void text_add_json_value(TEXTBUF *tb, VALUEINFO *vi)
{
	TCHAR buf[BUF_SIZE];
	int i;

	if (vi == NULL || vi->v == NULL) {
		text_add(tb, TEXT("null"), 0);
		return;
	}
	switch (vi->v->type) {
	case TYPE_ARRAY:
		text_add_json_array(tb, vi->v->u.array);
		break;
	case TYPE_STRING:
		text_add_json_string(tb, vi->v->u.sValue);
		break;
	case TYPE_FLOAT:
		if (_finite(vi->v->u.fValue) == 0) {
			// JSON には無限大と非数が無い
			text_add(tb, TEXT("null"), 0);
			break;
		}
		// 元の値に戻せる最も短い桁数にする
		for (i = 15; i <= 17; i++) {
			_stprintf_s(buf, BUF_SIZE, TEXT("%.*g"), i, vi->v->u.fValue);
			if (_tcstod(buf, NULL) == vi->v->u.fValue) {
				break;
			}
		}
		text_add(tb, buf, 0);
		break;
	default:
		wsprintf(buf, TEXT("%d"), vi->v->u.iValue);
		text_add(tb, buf, 0);
		break;
	}
}

/*
 * text_add_json_array - 配列を JSON にして追加
 * キーの付いた要素が無ければ配列、あればオブジェクト (キーの無い要素はインデックスがキー) にする
 */
static void text_add_json_array(TEXTBUF *tb, VALUEINFO *vi)
{
	VALUEINFO *tvi;
	TCHAR buf[BUF_SIZE];
	BOOL keyed = FALSE;
	int i;

	for (tvi = vi; tvi != NULL; tvi = tvi->next) {
		if (tvi->name != NULL) {
			keyed = TRUE;
			break;
		}
	}
	text_add(tb, (keyed == TRUE) ? TEXT("{") : TEXT("["), 0);
	for (tvi = vi, i = 0; tvi != NULL; tvi = tvi->next, i++) {
		if (i > 0) {
			text_add(tb, TEXT(","), 0);
		}
		if (keyed == TRUE) {
			if (tvi->org_name != NULL) {
				text_add_json_string(tb, tvi->org_name);
			} else if (tvi->name != NULL) {
				text_add_json_string(tb, tvi->name);
			} else {
				wsprintf(buf, TEXT("\"%d\""), i);
				text_add(tb, buf, 0);
			}
			text_add(tb, TEXT(":"), 0);
		}
		text_add_json_value(tb, tvi);
	}
	text_add(tb, (keyed == TRUE) ? TEXT("}") : TEXT("]"), 0);
}

/*
 * write_utf8 - 標準出力に UTF-8 で出力
 */
static void write_utf8(const TCHAR *str)
{
	char *cbuf;
	int len;

	len = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
	if (len <= 0 || (cbuf = mem_alloc(len)) == NULL) {
		return;
	}
	WideCharToMultiByte(CP_UTF8, 0, str, -1, cbuf, len, NULL, NULL);
	fflush(stdout);
	_setmode(_fileno(stdout), _O_BINARY);
	fwrite(cbuf, 1, len - 1, stdout);
	fflush(stdout);
	mem_free(&cbuf);
}

/*
 * write_result - 実行の結果を JSON で出力
 * has_vars が FALSE の場合は変数を取り出せない (構文エラー、強制終了)
 */
static void write_result(const int status, VALUEINFO *rvi, VALUEINFO *vars, const BOOL has_vars)
{
	static const TCHAR *status_name[] = {TEXT("ok"), TEXT("error"), TEXT("timeout"), TEXT("step_limit")};
	static const TCHAR *type_name[] = {TEXT("integer"), TEXT("float"), TEXT("string"), TEXT("array")};
	SCREEN_SIZE screen_size;
	HMODULE hModule;
	TEXTBUF tb;
	TCHAR buf[BUF_SIZE];
	int w = 0, h = 0;
	int i;

	ZeroMemory(&tb, sizeof(TEXTBUF));
	text_add(&tb, TEXT("{\"status\":"), 0);
	text_add_json_string(&tb, status_name[status]);
	text_add(&tb, TEXT(",\"mode\":"), 0);
	text_add_json_string(&tb, (mode_pg05 == TRUE) ? TEXT("PG0.5") : TEXT("PG0"));
	text_add(&tb, TEXT(",\"output\":"), 0);
	text_add_json_string(&tb, (out_buf.buf != NULL) ? out_buf.buf : TEXT(""));
	text_add(&tb, TEXT(",\"output_truncated\":"), 0);
	text_add(&tb, (out_buf.truncated == TRUE) ? TEXT("true") : TEXT("false"), 0);
	text_add(&tb, TEXT(",\"error_output\":"), 0);
	text_add_json_string(&tb, (err_buf.buf != NULL) ? err_buf.buf : TEXT(""));

	// 戻り値
	text_add(&tb, TEXT(",\"result\":"), 0);
	if (status == RUN_OK && rvi != NULL && rvi->v != NULL) {
		text_add_json_value(&tb, rvi);
		text_add(&tb, TEXT(",\"result_type\":"), 0);
		text_add_json_string(&tb, type_name[rvi->v->type]);
	} else {
		text_add(&tb, TEXT("null,\"result_type\":null"), 0);
	}

	// エラー
	text_add(&tb, TEXT(",\"error\":"), 0);
	if (status == RUN_ERROR) {
		text_add(&tb, TEXT("{\"message\":"), 0);
		text_add_json_string(&tb, (err_message != NULL) ? err_message : TEXT(""));
		if (err_line > 0) {
			wsprintf(buf, TEXT(",\"line\":%d"), err_line);
		} else {
			lstrcpy(buf, TEXT(",\"line\":null"));
		}
		text_add(&tb, buf, 0);
		text_add(&tb, TEXT(",\"phase\":"), 0);
		text_add_json_string(&tb, (exec_phase == TRUE) ? TEXT("runtime") : TEXT("parse"));
		text_add(&tb, TEXT("}"), 0);
	} else {
		text_add(&tb, TEXT("null"), 0);
	}

	// グローバル変数
	text_add(&tb, TEXT(",\"variables\":"), 0);
	if (has_vars == TRUE) {
		VALUEINFO *vi;
		text_add(&tb, TEXT("{"), 0);
		for (vi = vars, i = 0; vi != NULL; vi = vi->next) {
			if (vi->name == NULL) {
				continue;
			}
			if (i++ > 0) {
				text_add(&tb, TEXT(","), 0);
			}
			text_add_json_string(&tb, (vi->org_name != NULL) ? vi->org_name : vi->name);
			text_add(&tb, TEXT(":"), 0);
			text_add_json_value(&tb, vi);
		}
		text_add(&tb, TEXT("}"), 0);
	} else {
		text_add(&tb, TEXT("null"), 0);
	}

	// 画面
	text_add(&tb, TEXT(",\"screen\":"), 0);
	hModule = screen_module();
	screen_size = (hModule != NULL) ? (SCREEN_SIZE)GetProcAddress(hModule, "_lib_screen_size") : NULL;
	if (screen_size != NULL && screen_size(&w, &h) == TRUE) {
		wsprintf(buf, TEXT("{\"width\":%d,\"height\":%d,\"screenshots\":["), w, h);
		text_add(&tb, buf, 0);
		for (i = 0; i < screenshot_count; i++) {
			if (i > 0) {
				text_add(&tb, TEXT(","), 0);
			}
			text_add_json_string(&tb, screenshots[i]);
		}
		text_add(&tb, TEXT("]}"), 0);
	} else {
		text_add(&tb, TEXT("null"), 0);
	}

	_stprintf_s(buf, BUF_SIZE, TEXT(",\"stats\":{\"steps\":%I64u,\"elapsed_ms\":%lu,\"input_lines_used\":%d}}\n"),
		steps, (exec_phase == TRUE) ? (GetTickCount() - start_tick) : 0, input_lines);
	text_add(&tb, buf, 0);
	if (tb.buf != NULL) {
		write_utf8(tb.buf);
	}
	mem_free(&tb.buf);
}

/*
 * write_check_result - 構文チェックの結果を JSON で出力
 */
static void write_check_result(const BOOL ok)
{
	TEXTBUF tb;
	TCHAR buf[BUF_SIZE];

	ZeroMemory(&tb, sizeof(TEXTBUF));
	text_add(&tb, (ok == TRUE) ? TEXT("{\"ok\":true") : TEXT("{\"ok\":false"), 0);
	text_add(&tb, TEXT(",\"mode\":"), 0);
	text_add_json_string(&tb, (mode_pg05 == TRUE) ? TEXT("PG0.5") : TEXT("PG0"));
	text_add(&tb, TEXT(",\"error\":"), 0);
	if (ok == FALSE) {
		text_add(&tb, TEXT("{\"message\":"), 0);
		text_add_json_string(&tb, (err_message != NULL) ? err_message : TEXT(""));
		if (err_line > 0) {
			wsprintf(buf, TEXT(",\"line\":%d"), err_line);
		} else {
			lstrcpy(buf, TEXT(",\"line\":null"));
		}
		text_add(&tb, buf, 0);
		text_add(&tb, TEXT(",\"phase\":\"parse\"}"), 0);
	} else {
		text_add(&tb, TEXT("null"), 0);
	}
	text_add(&tb, TEXT("}\n"), 0);
	if (tb.buf != NULL) {
		write_utf8(tb.buf);
	}
	mem_free(&tb.buf);
}

/*
 * screen_module - 読み込まれている画面ライブラリ
 */
static HMODULE screen_module(void)
{
	return GetModuleHandle(SCREEN_LIBRARY);
}

/*
 * screen_save - 画面を PNG ファイルに保存 (startScreen() の前は保存しない)
 */
static BOOL screen_save(const TCHAR *path)
{
	SCREEN_SAVE func;
	HMODULE hModule = screen_module();
	int i;

	if (hModule == NULL || (func = (SCREEN_SAVE)GetProcAddress(hModule, "_lib_screen_save")) == NULL ||
		func(path) == FALSE) {
		return FALSE;
	}
	EnterCriticalSection(&cs);
	for (i = 0; i < screenshot_count; i++) {
		if (lstrcmpi(screenshots[i], path) == 0) {
			break;
		}
	}
	if (i == screenshot_count && screenshot_count < SCREEN_SHOT_MAX) {
		lstrcpy(screenshots[screenshot_count++], path);
	}
	LeaveCriticalSection(&cs);
	return TRUE;
}

/*
 * free_actions - 画面の操作の解放
 */
static void free_actions(void)
{
	ACTION *act;

	while (actions != NULL) {
		act = actions->next;
		mem_free(&actions->str);
		mem_free(&actions);
		actions = act;
	}
}

/*
 * next_word - 空白で区切られた次の語 (無い場合は NULL)
 */
static TCHAR *next_word(TCHAR **p)
{
	TCHAR *r;

	for (; **p == TEXT(' ') || **p == TEXT('\t'); (*p)++);
	if (**p == TEXT('\0')) {
		return NULL;
	}
	r = *p;
	for (; **p != TEXT('\0') && **p != TEXT(' ') && **p != TEXT('\t'); (*p)++);
	if (**p != TEXT('\0')) {
		*((*p)++) = TEXT('\0');
	}
	return r;
}

/*
 * next_number - 次の語を数値にする
 */
static BOOL next_number(TCHAR **p, double *num)
{
	TCHAR *word, *end;

	if ((word = next_word(p)) == NULL) {
		return FALSE;
	}
	*num = _tcstod(word, &end);
	return (end != word && *end == TEXT('\0') && _finite(*num) != 0);
}

/*
 * next_time - 次の語を時間 (ミリ秒) にする
 */
static BOOL next_time(TCHAR **p, DWORD *ms)
{
	double num;

	if (next_number(p, &num) == FALSE || num < 0 || num > 3600000) {
		return FALSE;
	}
	*ms = (DWORD)num;
	return TRUE;
}

/*
 * parse_action - 画面の操作の 1 行を解析
 */
static ACTION *parse_action(TCHAR *line)
{
	ACTION *act;
	TCHAR *p = line;
	TCHAR *word;
	TCHAR full[MAX_PATH + 1];
	BOOL ret = TRUE;

	if ((word = next_word(&p)) == NULL || (act = mem_calloc(sizeof(ACTION))) == NULL) {
		return NULL;
	}
	if (lstrcmpi(word, TEXT("wait")) == 0) {
		act->type = ACT_WAIT;
		ret = next_time(&p, &act->ms);
	} else if (lstrcmpi(word, TEXT("key")) == 0 || lstrcmpi(word, TEXT("keydown")) == 0 || lstrcmpi(word, TEXT("keyup")) == 0) {
		act->type = (lstrcmpi(word, TEXT("key")) == 0) ? ACT_KEY : ((lstrcmpi(word, TEXT("keydown")) == 0) ? ACT_KEYDOWN : ACT_KEYUP);
		act->ms = SCREEN_KEY_TIME;
		if ((word = next_word(&p)) == NULL) {
			ret = FALSE;
		} else {
			// 空白は語の区切りなので Space と書く
			act->str = alloc_copy((lstrcmpi(word, TEXT("Space")) == 0) ? TEXT(" ") : word);
			for (; *p == TEXT(' ') || *p == TEXT('\t'); p++);
			if (act->type == ACT_KEY && *p != TEXT('\0')) {
				ret = next_time(&p, &act->ms);
			}
		}
	} else if (lstrcmpi(word, TEXT("tap")) == 0) {
		act->type = ACT_TAP;
		act->ms = SCREEN_TAP_TIME;
		ret = (next_number(&p, &act->x1) && next_number(&p, &act->y1));
	} else if (lstrcmpi(word, TEXT("press")) == 0) {
		act->type = ACT_PRESS;
		ret = (next_number(&p, &act->x1) && next_number(&p, &act->y1) && next_time(&p, &act->ms));
	} else if (lstrcmpi(word, TEXT("drag")) == 0) {
		act->type = ACT_DRAG;
		act->ms = SCREEN_DRAG_TIME;
		ret = (next_number(&p, &act->x1) && next_number(&p, &act->y1) &&
			next_number(&p, &act->x2) && next_number(&p, &act->y2));
		for (; *p == TEXT(' ') || *p == TEXT('\t'); p++);
		if (ret == TRUE && *p != TEXT('\0')) {
			ret = next_time(&p, &act->ms);
		}
	} else if (lstrcmpi(word, TEXT("move")) == 0) {
		act->type = ACT_MOVE;
		ret = (next_number(&p, &act->x1) && next_number(&p, &act->y1));
	} else if (lstrcmpi(word, TEXT("screen")) == 0) {
		// 画像のパスは空白を含められるように行末までとする
		act->type = ACT_SCREEN;
		for (; *p == TEXT(' ') || *p == TEXT('\t'); p++);
		if (*p == TEXT('\0') || GetFullPathName(p, MAX_PATH + 1, full, NULL) == 0) {
			ret = FALSE;
		} else {
			act->str = alloc_copy(full);
			p += lstrlen(p);
		}
	} else {
		ret = FALSE;
	}
	for (; *p == TEXT(' ') || *p == TEXT('\t'); p++);
	if (ret == FALSE || *p != TEXT('\0')) {
		mem_free(&act->str);
		mem_free(&act);
		return NULL;
	}
	return act;
}

/*
 * read_actions - 画面の操作を書いたファイルを読み込む
 */
static BOOL read_actions(const TCHAR *path)
{
	ACTION *act, *last = NULL;
	TCHAR *text, *p, *r, *line;
	int no = 0;

	if ((text = read_file((TCHAR *)path)) == NULL) {
		_ftprintf(stderr, TEXT("%s: cannot read the file: %s\n"), APP_NAME, path);
		return FALSE;
	}
	for (p = text; *p != TEXT('\0'); p = r) {
		for (r = p; *r != TEXT('\0') && *r != TEXT('\n'); r++);
		line = alloc_copy_n(p, (int)(r - p));
		if (*r != TEXT('\0')) {
			r++;
		}
		no++;
		if (line == NULL) {
			continue;
		}
		// 行末の空白と改行を取り除く
		for (p = line + lstrlen(line); p > line && (*(p - 1) == TEXT('\r') || *(p - 1) == TEXT(' ') || *(p - 1) == TEXT('\t')); p--);
		*p = TEXT('\0');
		for (p = line; *p == TEXT(' ') || *p == TEXT('\t'); p++);
		if (*p == TEXT('\0') || *p == TEXT('#') || (*p == TEXT('/') && *(p + 1) == TEXT('/'))) {
			// 空行とコメント
			mem_free(&line);
			continue;
		}
		if ((act = parse_action(p)) == NULL) {
			_ftprintf(stderr, TEXT("%s: invalid operation: %s(%d)\n"), APP_NAME, path, no);
			mem_free(&line);
			mem_free(&text);
			free_actions();
			return FALSE;
		}
		mem_free(&line);
		if (last == NULL) {
			actions = act;
		} else {
			last->next = act;
		}
		last = act;
	}
	mem_free(&text);
	return TRUE;
}

/*
 * action_thread - 画面の操作を順番に行う (startScreen() が呼ばれてから始める)
 */
static DWORD WINAPI action_thread(LPVOID param)
{
	SCREEN_SIZE screen_size = NULL;
	SCREEN_KEY screen_key;
	SCREEN_TOUCH screen_touch;
	HMODULE hModule = NULL;
	ACTION *act;
	DWORD st, now;
	int w, h;

	// 画面の開始を待つ
	for (;;) {
		if (screen_size == NULL && (hModule = screen_module()) != NULL) {
			screen_size = (SCREEN_SIZE)GetProcAddress(hModule, "_lib_screen_size");
		}
		if (screen_size != NULL && screen_size(&w, &h) == TRUE) {
			break;
		}
		if (WaitForSingleObject(hDone, 10) != WAIT_TIMEOUT) {
			return 0;
		}
	}
	screen_key = (SCREEN_KEY)GetProcAddress(hModule, "_lib_screen_key");
	screen_touch = (SCREEN_TOUCH)GetProcAddress(hModule, "_lib_screen_touch");
	if (screen_key == NULL || screen_touch == NULL) {
		return 0;
	}

	for (act = actions; act != NULL; act = act->next) {
		switch (act->type) {
		case ACT_WAIT:
			if (WaitForSingleObject(hDone, act->ms) != WAIT_TIMEOUT) {
				return 0;
			}
			break;

		case ACT_KEY:
			screen_key(act->str, TRUE);
			if (WaitForSingleObject(hDone, act->ms) != WAIT_TIMEOUT) {
				return 0;
			}
			screen_key(act->str, FALSE);
			break;

		case ACT_KEYDOWN:
			screen_key(act->str, TRUE);
			break;

		case ACT_KEYUP:
			screen_key(act->str, FALSE);
			break;

		case ACT_TAP:
		case ACT_PRESS:
			screen_touch(SCREEN_TOUCH_DOWN, act->x1, act->y1);
			if (WaitForSingleObject(hDone, act->ms) != WAIT_TIMEOUT) {
				return 0;
			}
			screen_touch(SCREEN_TOUCH_UP, act->x1, act->y1);
			break;

		case ACT_DRAG:
			screen_touch(SCREEN_TOUCH_DOWN, act->x1, act->y1);
			st = GetTickCount();
			for (;;) {
				if (WaitForSingleObject(hDone, SCREEN_DRAG_INTERVAL) != WAIT_TIMEOUT) {
					return 0;
				}
				now = GetTickCount() - st;
				if (now >= act->ms) {
					break;
				}
				screen_touch(SCREEN_TOUCH_MOVE,
					act->x1 + (act->x2 - act->x1) * now / act->ms,
					act->y1 + (act->y2 - act->y1) * now / act->ms);
			}
			screen_touch(SCREEN_TOUCH_MOVE, act->x2, act->y2);
			screen_touch(SCREEN_TOUCH_UP, act->x2, act->y2);
			break;

		case ACT_MOVE:
			screen_touch(SCREEN_TOUCH_MOVE, act->x1, act->y1);
			break;

		case ACT_SCREEN:
			screen_save(act->str);
			break;
		}
	}
	return 0;
}

/*
 * watchdog_thread - 実行時間の上限を過ぎても止まらないスクリプトを強制終了する
 * (取り込んだスクリプトの中の無限ループや入力待ちは、コールバックでは止められない)
 */
static DWORD WINAPI watchdog_thread(LPVOID param)
{
	if (WaitForSingleObject(hDone, op_timeout + WATCHDOG_GRACE) != WAIT_TIMEOUT) {
		return 0;
	}
	if (*op_screenshot != TEXT('\0')) {
		screen_save(op_screenshot);
	}
	EnterCriticalSection(&cs);
	if (finished == TRUE) {
		LeaveCriticalSection(&cs);
		return 0;
	}
	if (op_json == TRUE) {
		write_result(RUN_TIMEOUT, NULL, NULL, FALSE);
	} else {
		fflush(stdout);
	}
	// cs を保持したまま終了する (実行スレッドに結果を出力させない)
	ExitProcess(EXIT_CODE_LIMIT);
	return 0;
}

/*
 * Callback - 実行時のコールバック (0 以外を返すと実行を止める)
 * cu_tk が NULL の場合はライブラリからの停止の確認
 */
static int SFUNC Callback(EXECINFO *ei, TOKEN *cu_tk)
{
	if (stop_status != RUN_OK) {
		return -1;
	}
	if (cu_tk != NULL) {
		steps++;
		if (op_max_steps > 0 && steps > op_max_steps) {
			stop_status = RUN_STEP_LIMIT;
			return -1;
		}
		if ((steps % TIME_CHECK_STEPS) != 0) {
			return 0;
		}
	}
	if (op_timeout > 0 && GetTickCount() - start_tick >= op_timeout) {
		stop_status = RUN_TIMEOUT;
		return -1;
	}
	return 0;
}

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
	if (str == NULL) {
		return 0;
	}
	if (param->next != NULL) {
		line = VariableToInt(param->next);
	}
	EnterCriticalSection(&cs);
	// インタプリタが出したエラー (最初の 1 つが停止の原因)
	if (str_cmp_n(str, TEXT("Error: "), lstrlen(TEXT("Error: "))) == 0) {
		if (engine_error_count++ == 0) {
			mem_free(&err_message);
			err_message = alloc_copy(str);
			err_line = line;
		}
	}
	if (op_json == TRUE) {
		text_add(&err_buf, str, OUTPUT_MAX);
		text_add(&err_buf, TEXT("\n"), OUTPUT_MAX);
	} else {
		_tprintf(TEXT("%s\n"), str);
	}
	LeaveCriticalSection(&cs);
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
	if (param->v->type == TYPE_ARRAY) {
		// 配列は pg0.exe と同じ形式で出力する
		int size = ArrayToStringSize(param->v->u.array, op_hex);
		str = (TCHAR *)mem_alloc(sizeof(TCHAR) * (size + 1));
		if (str != NULL) {
			ArrayToString(param->v->u.array, str, op_hex);
		}
	} else {
		str = VariableToString(param);
	}
	if (str == NULL) {
		return 0;
	}
	EnterCriticalSection(&cs);
	if (op_json == TRUE) {
		text_add(&out_buf, str, OUTPUT_MAX);
	} else {
		_tprintf(TEXT("%s"), str);
	}
	LeaveCriticalSection(&cs);
	mem_free(&str);
	return 0;
}

/*
 * _lib_func_input - 標準入力
 * 入力のファイル (--input)、リダイレクトされた標準入力、コンソールの順に 1 行を読み取る
 * ファイルやリダイレクトの入力が尽きた場合は、空行と区別できるように整数の 0 を返す
 */
int SFUNC _lib_func_input(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	HANDLE ih;
	TCHAR *buf, *p;
	DWORD mode, rmode;
	DWORD size = 0;
	BOOL eof = FALSE;
	int len = LINE_SIZE;

	buf = mem_alloc(sizeof(TCHAR) * (len + 1));
	if (buf == NULL) {
		return -1;
	}
	*buf = TEXT('\0');
	ih = GetStdHandle(STD_INPUT_HANDLE);
	if (input_text != NULL) {
		// 入力のファイルから 1 行
		if (*input_pos != TEXT('\0')) {
			for (p = input_pos; *p != TEXT('\0') && *p != TEXT('\n'); p++);
			lstrcpyn(buf, input_pos, (int)min(p - input_pos, len) + 1);
			input_pos = (*p != TEXT('\0')) ? p + 1 : p;
			input_lines++;
		} else {
			eof = TRUE;
		}
	} else if (GetConsoleMode(ih, &mode) == 0) {
		// リダイレクトされた標準入力から 1 行
		if (_fgetts(buf, len, stdin) != NULL) {
			input_lines++;
		} else {
			eof = TRUE;
		}
	} else {
		//標準入力から文字列を読み取る
		rmode = mode;
		//行入力モードに設定
		mode |= ENABLE_LINE_INPUT;
		SetConsoleMode(ih, mode);
		ReadConsole(ih, buf, len, &size, NULL);
		SetConsoleMode(ih, rmode);
		*(buf + size) = TEXT('\0');
		input_lines++;
	}
	if (eof == TRUE) {
		mem_free(&buf);
		return 0;
	}
	p = buf;
	if (input_first == TRUE && *p == 0xFEFF) {
		// BOM をスキップ
		p++;
	}
	input_first = FALSE;
	if (p != buf) {
		MoveMemory(buf, p, sizeof(TCHAR) * (lstrlen(p) + 1));
	}
	for (p = buf; *p != TEXT('\0') && *p != TEXT('\r') && *p != TEXT('\n'); p++);
	*p = TEXT('\0');

	ret->v->u.sValue = buf;
	ret->v->type = TYPE_STRING;
	return 0;
}

/*
 * FormatValue - 値の出力
 */
static void FormatValue(VALUEINFO *vi) {
	if (op_hex == TRUE) {
		_tprintf(TEXT("0x%X"), vi->v->u.iValue);
	} else {
		_tprintf(TEXT("%d"), vi->v->u.iValue);
	}
}

/*
 * PrintValue - 値を 1 行で出力
 */
static void PrintValue(VALUEINFO *vi)
{
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
				PrintValue(vi);
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
 * ShowHelp - 使い方の表示
 */
static void ShowHelp(void)
{
	WORD lang = PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale()));

	_tprintf(TEXT("pg0cmd [-psxvcj] [--option...] [file.pg0] [arg1[ arg2...]]\n"));
	_tprintf(TEXT("\n"));
	if (lang == LANG_JAPANESE) {
		_tprintf(TEXT("  -p\t\t\tPG0 Mode\n"));
		_tprintf(TEXT("  -s\t\t\t変数宣言を強制 (通常実行時)\n"));
		_tprintf(TEXT("  -x\t\t\t結果を16進数で表示\n"));
		_tprintf(TEXT("  -v, --version\t\tバージョン表示\n"));
		_tprintf(TEXT("  -c, --check\t\t構文チェックのみ行う (実行しない)\n"));
		_tprintf(TEXT("  -j, --json\t\t結果を JSON (UTF-8) で出力\n"));
		_tprintf(TEXT("  --utf8\t\t入出力を UTF-8 にする\n"));
		_tprintf(TEXT("  --timeout=ms\t\t実行時間の上限 (ミリ秒)\n"));
		_tprintf(TEXT("  --max-steps=n\t\t実行ステップ数の上限\n"));
		_tprintf(TEXT("  --input=file\t\tinput() が 1 行ずつ読み取るファイル (UTF-8)\n"));
		_tprintf(TEXT("  --headless\t\t画面ライブラリのウィンドウを表示せず、音も鳴らさない\n"));
		_tprintf(TEXT("  --screenshot=file\t終了時の画面ライブラリの画面を PNG で保存\n"));
		_tprintf(TEXT("  --screen-input=file\t画面ライブラリへのキー、タッチの操作を書いたファイル\n"));
		_tprintf(TEXT("\n"));
		_tprintf(TEXT("  file.pg0\t\t実行するスクリプトファイル\n"));
		_tprintf(TEXT("         \t\tファイル名の指定が無い場合はライン実行を行う\n"));
		_tprintf(TEXT("\n"));
		_tprintf(TEXT("  arg1\t\t\tスクリプトに渡す引数\n"));
		_tprintf(TEXT("         \t\targvで引数の配列、argcで引数の数\n"));
		_tprintf(TEXT("\n"));
		_tprintf(TEXT("  終了コード\t\t0: 正常終了  1: エラー  2: 実行時間、ステップ数の上限で停止\n"));
		_tprintf(TEXT("\n"));
		_tprintf(TEXT("  操作のファイル\twait ms / key name [ms] / keydown name / keyup name /\n"));
		_tprintf(TEXT("         \t\ttap x y / press x y ms / drag x1 y1 x2 y2 [ms] / move x y /\n"));
		_tprintf(TEXT("         \t\tscreen file.png (startScreen() が呼ばれてから始まる)\n"));
	} else {
		_tprintf(TEXT("  -p\t\t\tPG0 Mode\n"));
		_tprintf(TEXT("  -s\t\t\tStrict\n"));
		_tprintf(TEXT("  -x\t\t\tHex result\n"));
		_tprintf(TEXT("  -v, --version\t\tVersion\n"));
		_tprintf(TEXT("  -c, --check\t\tSyntax check only (the script is not run)\n"));
		_tprintf(TEXT("  -j, --json\t\tOutput the result as JSON (UTF-8)\n"));
		_tprintf(TEXT("  --utf8\t\tUTF-8 input and output\n"));
		_tprintf(TEXT("  --timeout=ms\t\tTime limit (milliseconds)\n"));
		_tprintf(TEXT("  --max-steps=n\t\tLimit of executed steps\n"));
		_tprintf(TEXT("  --input=file\t\tFile read line by line by input() (UTF-8)\n"));
		_tprintf(TEXT("  --headless\t\tNo window and no sound for the screen library\n"));
		_tprintf(TEXT("  --screenshot=file\tSave the screen of the screen library as PNG at the end\n"));
		_tprintf(TEXT("  --screen-input=file\tFile of key and touch operations for the screen library\n"));
		_tprintf(TEXT("\n"));
		_tprintf(TEXT("  file.pg0\t\tExecution script file\n"));
		_tprintf(TEXT("\n"));
		_tprintf(TEXT("  arg1\t\t\tCommand line\n"));
		_tprintf(TEXT("\n"));
		_tprintf(TEXT("  Exit code\t\t0: OK  1: Error  2: Stopped by the time or step limit\n"));
		_tprintf(TEXT("\n"));
		_tprintf(TEXT("  Operations\t\twait ms / key name [ms] / keydown name / keyup name /\n"));
		_tprintf(TEXT("         \t\ttap x y / press x y ms / drag x1 y1 x2 y2 [ms] / move x y /\n"));
		_tprintf(TEXT("         \t\tscreen file.png (started when startScreen() is called)\n"));
	}
	_tprintf(TEXT("\n"));
}

/*
 * ShowVersion - バージョンの表示
 */
static void ShowVersion(void)
{
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
	_tprintf(TEXT("%s"), var_msg);
}

/*
 * ExecFile - スクリプトファイルの構文チェック、実行
 */
static int ExecFile(TCHAR *path, int argc, TCHAR **argv)
{
	SCRIPTINFO *ScriptInfo;
	EXECINFO *ei;
	VALUEINFO *vi, *pvi = NULL;
	VALUEINFO *rvi = NULL;
	HANDLE hAction = NULL;
	HANDLE hWatchdog = NULL;
	TCHAR fname[MAX_PATH];
	DWORD thId;
	int status = RUN_OK;
	int ret;
	int i;

	GetFilePathName(path, AppDir, fname);

	if (*op_input != TEXT('\0')) {
		// input() の入力
		input_text = input_pos = read_file(op_input);
		if (input_text == NULL) {
			_ftprintf(stderr, TEXT("%s: cannot read the file: %s\n"), APP_NAME, op_input);
			return EXIT_CODE_ERROR;
		}
	}
	if (*op_screen_input != TEXT('\0') && read_actions(op_screen_input) == FALSE) {
		mem_free(&input_text);
		return EXIT_CODE_ERROR;
	}
	if (op_headless == TRUE) {
		SetEnvironmentVariable(SCREEN_HEADLESS_ENV, TEXT("1"));
	}

	//初期化
	InitializeScript();

	//読み込み
	ScriptInfo = mem_calloc(sizeof(SCRIPTINFO));
	if (ScriptInfo == NULL) {
		return EXIT_CODE_ERROR;
	}
	InitializeScriptInfo(ScriptInfo, op_strict, !op_pg0);
	ScriptInfo->sci_top = ScriptInfo;
	ReadScriptFile(ScriptInfo, AppDir, fname);
	mode_pg05 = (ScriptInfo->extension == FALSE) ? FALSE : TRUE;
	if (engine_error_count > 0 || ScriptInfo->prep_error == TRUE || ScriptInfo->buf == NULL) {
		// 構文エラー
		if (op_json == TRUE) {
			if (op_check == TRUE) {
				write_check_result(FALSE);
			} else {
				write_result(RUN_ERROR, NULL, NULL, FALSE);
			}
		}
		FreeScriptInfo(ScriptInfo);
		EndScript();
		mem_free(&input_text);
		free_actions();
		return EXIT_CODE_ERROR;
	}
	if (op_check == TRUE) {
		// 構文チェックのみ
		if (op_json == TRUE) {
			write_check_result(TRUE);
		}
		FreeScriptInfo(ScriptInfo);
		EndScript();
		mem_free(&input_text);
		free_actions();
		return EXIT_CODE_OK;
	}

	//引数
	if (argc > 0) {
		// argv
		pvi = AllocValue();
		if (pvi == NULL) {
			return EXIT_CODE_ERROR;
		}
		pvi->name = alloc_copy(TEXT("argv"));
		pvi->name_hash = str2hash(TEXT("argv"));
		pvi->v->type = TYPE_ARRAY;
		vi = pvi->v->u.array = StringToVariable(NULL, argv[0]);
		for (i = 1; i < argc; i++) {
			vi = vi->next = StringToVariable(NULL, argv[i]);
		}
		// argc
		pvi->next = AllocValue();
		if (pvi->next == NULL) {
			return EXIT_CODE_ERROR;
		}
		pvi->next->name = alloc_copy(TEXT("argc"));
		pvi->next->name_hash = str2hash(TEXT("argc"));
		pvi->next->v->u.iValue = argc;
		pvi->next->v->type = TYPE_INTEGER;
	}

	// 実行の制限と画面の操作
	exec_phase = TRUE;
	start_tick = GetTickCount();
	if (op_json == TRUE || op_timeout > 0 || op_max_steps > 0) {
		ScriptInfo->callback = Callback;
	}
	hDone = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (hDone != NULL && actions != NULL) {
		hAction = CreateThread(NULL, 0, action_thread, NULL, 0, &thId);
	}
	if (hDone != NULL && op_timeout > 0) {
		hWatchdog = CreateThread(NULL, 0, watchdog_thread, NULL, 0, &thId);
	}

	// 実行 (エラーで止まった場合も変数を取り出せるように、ExecScript を使わずに実行情報を保持する)
	ei = mem_calloc(sizeof(EXECINFO));
	if (ei == NULL) {
		return EXIT_CODE_ERROR;
	}
	ei->name = ScriptInfo->name;
	ei->sci = ScriptInfo;
	ei->vi = pvi;
	ret = (ScriptInfo->tk != NULL) ? ExecSentense(ei, ScriptInfo->tk, &rvi, NULL) : RET_SUCCESS;
	if (ret == RET_BREAK || ret == RET_CONTINUE) {
		Error(ei, ERR_SENTENCE, ei->err, NULL);
		ret = RET_ERROR;
	}
	ScriptInfo->ei = ei;
	if (ret == RET_ERROR || engine_error_count > 0) {
		// エラーを出しても実行が続く場合がある (関数の引数の不足など) ため、エラーの有無でも判定する
		status = RUN_ERROR;
	} else if (stop_status != RUN_OK) {
		status = stop_status;
	}

	// 画面の操作を止めて、終了時の画面を保存する
	if (hDone != NULL) {
		SetEvent(hDone);
	}
	if (hAction != NULL) {
		WaitForSingleObject(hAction, INFINITE);
		CloseHandle(hAction);
	}
	if (*op_screenshot != TEXT('\0')) {
		screen_save(op_screenshot);
	}

	// 結果の出力 (強制終了と同時に出力しない)
	EnterCriticalSection(&cs);
	finished = TRUE;
	LeaveCriticalSection(&cs);
	if (hWatchdog != NULL) {
		WaitForSingleObject(hWatchdog, INFINITE);
		CloseHandle(hWatchdog);
	}
	if (hDone != NULL) {
		CloseHandle(hDone);
		hDone = NULL;
	}
	if (op_json == TRUE) {
		write_result(status, rvi, ei->vi, TRUE);
	} else if (status == RUN_OK && rvi != NULL && rvi->v != NULL) {
		PrintValue(rvi);
	}
	FreeValueList(rvi);
	FreeScriptInfo(ScriptInfo);
	EndScript();
	mem_free(&input_text);
	free_actions();
	switch (status) {
	case RUN_OK:
		return EXIT_CODE_OK;
	case RUN_ERROR:
		return EXIT_CODE_ERROR;
	}
	return EXIT_CODE_LIMIT;
}

/*
 * option_value - "--name=value" の値 (名前が違う場合は NULL)
 */
static TCHAR *option_value(TCHAR *arg, const TCHAR *name)
{
	int len = lstrlen(name);

	if (str_cmp_n(arg, name, len) != 0 || *(arg + len) != TEXT('=')) {
		return NULL;
	}
	return arg + len + 1;
}

/*
 * option_number - オプションの数値 (0 以上の整数)
 */
static BOOL option_number(const TCHAR *str, unsigned __int64 *num)
{
	const TCHAR *p;

	if (*str == TEXT('\0')) {
		return FALSE;
	}
	*num = 0;
	for (p = str; *p != TEXT('\0'); p++) {
		if (*p < TEXT('0') || *p > TEXT('9') || p - str >= 18) {
			return FALSE;
		}
		*num = *num * 10 + (*p - TEXT('0'));
	}
	return TRUE;
}

/*
 * option_path - オプションのファイル名をフルパスにする
 */
static BOOL option_path(const TCHAR *str, TCHAR *path)
{
	DWORD len;

	if (*str == TEXT('\0')) {
		return FALSE;
	}
	len = GetFullPathName(str, MAX_PATH + 1, path, NULL);
	return (len > 0 && len <= MAX_PATH);
}

/*
 * is_option_letters - 1 文字のオプションを並べたもの ("/psx", "-cj" など)
 */
static BOOL is_option_letters(const TCHAR *arg)
{
	const TCHAR *p;

	if ((*arg != TEXT('/') && *arg != TEXT('-')) || *(arg + 1) == TEXT('\0')) {
		return FALSE;
	}
	for (p = arg + 1; *p != TEXT('\0'); p++) {
		switch (*p) {
		case TEXT('?'):
		case TEXT('p'): case TEXT('P'):
		case TEXT('s'): case TEXT('S'):
		case TEXT('x'): case TEXT('X'):
		case TEXT('v'): case TEXT('V'):
		case TEXT('c'): case TEXT('C'):
		case TEXT('j'): case TEXT('J'):
			break;
		default:
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * _tmain - メイン
 */
int _tmain(int argc, TCHAR **argv)
{
	TCHAR *c, *value;
	unsigned __int64 num;
	DWORD mode;
	BOOL op_help = FALSE;
	BOOL op_version = FALSE;
	BOOL op_error = FALSE;
	int i;
	int ret;

	setlocale(LC_CTYPE, "");
	InitializeCriticalSection(&cs);

	for (i = 1; i < argc; i++) {
		if (*(argv[i]) == TEXT('-') && *(argv[i] + 1) == TEXT('-')) {
			// 名前のオプション
			if (*(argv[i] + 2) == TEXT('\0')) {
				// 以降はオプションとして扱わない
				i++;
				break;
			} else if (lstrcmp(argv[i], TEXT("--help")) == 0) {
				op_help = TRUE;
			} else if (lstrcmp(argv[i], TEXT("--version")) == 0) {
				op_version = TRUE;
			} else if (lstrcmp(argv[i], TEXT("--check")) == 0) {
				op_check = TRUE;
			} else if (lstrcmp(argv[i], TEXT("--json")) == 0) {
				op_json = TRUE;
			} else if (lstrcmp(argv[i], TEXT("--utf8")) == 0) {
				op_utf8 = TRUE;
			} else if (lstrcmp(argv[i], TEXT("--headless")) == 0) {
				op_headless = TRUE;
			} else if ((value = option_value(argv[i], TEXT("--timeout"))) != NULL) {
				if (option_number(value, &num) == FALSE || num > 0x7FFFFFFF) {
					op_error = TRUE;
				}
				op_timeout = (DWORD)num;
			} else if ((value = option_value(argv[i], TEXT("--max-steps"))) != NULL) {
				if (option_number(value, &op_max_steps) == FALSE) {
					op_error = TRUE;
				}
			} else if ((value = option_value(argv[i], TEXT("--input"))) != NULL) {
				if (option_path(value, op_input) == FALSE) {
					op_error = TRUE;
				}
			} else if ((value = option_value(argv[i], TEXT("--screenshot"))) != NULL) {
				if (option_path(value, op_screenshot) == FALSE) {
					op_error = TRUE;
				}
			} else if ((value = option_value(argv[i], TEXT("--screen-input"))) != NULL) {
				if (option_path(value, op_screen_input) == FALSE) {
					op_error = TRUE;
				}
			} else {
				op_error = TRUE;
			}
			if (op_error == TRUE) {
				_ftprintf(stderr, TEXT("%s: invalid option: %s\n"), APP_NAME, argv[i]);
				DeleteCriticalSection(&cs);
				return EXIT_CODE_ERROR;
			}
			continue;
		}
		if (is_option_letters(argv[i]) == FALSE) {
			break;
		}
		for (c = argv[i] + 1; *c != TEXT('\0'); c++) {
			switch (*c) {
			case TEXT('?'):
				op_help = TRUE;
				break;
			case TEXT('v'): case TEXT('V'):
				op_version = TRUE;
				break;
			case TEXT('p'): case TEXT('P'):
				op_pg0 = TRUE;
				break;
			case TEXT('s'): case TEXT('S'):
				op_strict = TRUE;
				break;
			case TEXT('x'): case TEXT('X'):
				op_hex = TRUE;
				break;
			case TEXT('c'): case TEXT('C'):
				op_check = TRUE;
				break;
			case TEXT('j'): case TEXT('J'):
				op_json = TRUE;
				break;
			}
		}
	}

	// UTF-8 での入出力 (JSON は常に UTF-8)
	if (op_utf8 == TRUE || op_json == TRUE) {
		if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) == 0) {
			_setmode(_fileno(stdin), _O_U8TEXT);
		}
		if (op_json == FALSE) {
			_setmode(_fileno(stdout), _O_U8TEXT);
		}
	}

	if (op_help == TRUE) {
		ShowHelp();
		DeleteCriticalSection(&cs);
		return EXIT_CODE_OK;
	}
	if (op_version == TRUE) {
		ShowVersion();
		DeleteCriticalSection(&cs);
		return EXIT_CODE_OK;
	}

	if (argc <= i) {
		if (op_check == TRUE || op_json == TRUE) {
			// 構文チェックと JSON 出力にはスクリプトファイルが必要
			_ftprintf(stderr, TEXT("%s: a script file is required\n"), APP_NAME);
			DeleteCriticalSection(&cs);
			return EXIT_CODE_ERROR;
		}
		//1行実行モード
		InitializeScript();
		LineExecLoop();
		EndScript();
#ifdef _DEBUG
		mem_debug();
#endif
		DeleteCriticalSection(&cs);
		return EXIT_CODE_OK;
	}

	ret = ExecFile(argv[i], argc - i - 1, argv + i + 1);
	mem_free(&out_buf.buf);
	mem_free(&err_buf.buf);
	mem_free(&err_message);
#ifdef _DEBUG
	mem_debug();
#endif
	DeleteCriticalSection(&cs);
	return ret;
}
/* End of source */
