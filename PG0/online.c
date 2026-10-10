/*
 * PG0
 *
 * online.c
 *
 * Opening and saving scripts on the online storage of the web version (pg0.jp),
 * with the same dialogs and the same API as the web version.
 */

/* Include Files */
#define _CRT_RAND_S
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <windowsx.h>
#include <tchar.h>
#include <stdlib.h>
#include <stdio.h>
#include <wincrypt.h>

#include "Profile.h"
#include "dpi.h"
#include "script_memory.h"
#include "json.h"
#include "http.h"
#include "online_view.h"
#include "text_diff.h"
#include "diff_view.h"
#include "online.h"
#include "resource.h"

#pragma comment(lib, "crypt32.lib")

/* Define */
#define INI_SECTION						TEXT("ONLINE")
#define APP_NAME						TEXT("PG0")
#define LIST_COUNT						30
#define BUF_SIZE						256
#define PROTECT_SIZE					2048
#define LABEL_SIZE						64
#define RES_BUF_SIZE					1024
#define RES_BUF_COUNT					8

#define WM_HTTP_RESULT					(WM_APP + 60)

#define REQUEST_LIST					1
#define REQUEST_SCRIPT					2
#define REQUEST_REMOVE					3
#define REQUEST_SAVE					4
#define REQUEST_PREVIOUS				5
#define REQUEST_CODE					6

#define RESULT_HISTORY					100

#define IDC_SEARCH						1001
#define IDC_SEARCH_BUTTON				1002
#define IDC_CHIPS						1003
#define IDC_SORT						1004
#define IDC_LIST						1005
#define IDC_FILE						1101
#define IDC_AUTHOR						1102
#define IDC_PASSWORD					1103
#define IDC_MEMO						1104
#define IDC_TAGS						1105
#define IDC_NEW							1106
#define IDC_PRIVATE						1107
#define IDC_LABEL						1110
#define IDC_INPUT						1201
#define IDC_DIFF						1301

#define ID_ITEM_COPY					1
#define ID_ITEM_COPY_AUTORUN			2
#define ID_ITEM_HISTORY					3
#define ID_ITEM_REMOVE					4
#define ID_ITEM_DIFF					5

#define ID_DIFF_COPY					1
#define ID_DIFF_SELECT_ALL				2

// sizes in pixels at 96 DPI
#define DIALOG_MARGIN					20
#define OPEN_WIDTH						640
#define SAVE_WIDTH						380
#define INPUT_WIDTH						320
#define DIFF_WIDTH						900
#define DIFF_MARGIN						12

/* Global Variables */
static HINSTANCE hInst;
static int request_seq;

// settings
static TCHAR server[BUF_SIZE];
static TCHAR uuid[ONLINE_CID_SIZE];
static TCHAR author[BUF_SIZE];
static TCHAR password[BUF_SIZE];
static TCHAR keyword[BUF_SIZE];
static TCHAR list_filter[ONLINE_TAG_SIZE];
static TCHAR list_sort[ONLINE_TAG_SIZE];

// the font and the line numbers of the editor, for the code in the diff
static LOGFONT code_font;
static BOOL code_font_set;
static BOOL code_line_no = TRUE;

// the script opened from or saved to the online storage
typedef struct _CONTENT {
	TCHAR cid[ONLINE_CID_SIZE];
	TCHAR name[BUF_SIZE];
	TCHAR author[BUF_SIZE];
	TCHAR tag[ONLINE_TAG_SIZE];
	BOOL private_mode;
} CONTENT;
static CONTENT current;

// genres of the web version (the same order as its list)
static const TCHAR *tag_ids[] = {
	TEXT("game"), TEXT("graphics"), TEXT("math"), TEXT("algorithm"),
	TEXT("text"), TEXT("study"), TEXT("tool"), TEXT("other")
};
static const UINT tag_names[] = {
	IDS_STRING_ONLINE_TAG_GAME, IDS_STRING_ONLINE_TAG_GRAPHICS, IDS_STRING_ONLINE_TAG_MATH, IDS_STRING_ONLINE_TAG_ALGORITHM,
	IDS_STRING_ONLINE_TAG_TEXT, IDS_STRING_ONLINE_TAG_STUDY, IDS_STRING_ONLINE_TAG_TOOL, IDS_STRING_ONLINE_TAG_OTHER
};
#define TAG_COUNT						((int)(sizeof(tag_ids) / sizeof(tag_ids[0])))
#define TAG_OTHER						(TAG_COUNT - 1)
static TCHAR tag_labels[TAG_COUNT][LABEL_SIZE];

// a removal in flight (several scripts can be removed one after another)
#define MAX_REMOVES						8
typedef struct _REMOVE_REQUEST {
	int id;
	TCHAR cid[ONLINE_CID_SIZE];
} REMOVE_REQUEST;

// the list and history dialogs
typedef struct _OPEN_DATA {
	BOOL history;
	TCHAR cid[ONLINE_CID_SIZE];
	ONLINE_SCRIPT *script;
	CONTENT content;
	HFONT hFont;
	HWND hSearch;
	HWND hSearchButton;
	HWND hChips;
	HWND hSort;
	HWND hList;
	RECT search_frame;
	int list_id;
	int script_id;
	int skip;
	BOOL more;
	BOOL closing;
	REMOVE_REQUEST removes[MAX_REMOVES];
} OPEN_DATA;

// the dialog of the changes from the previous version
typedef struct _DIFF_DATA {
	const TCHAR *title;		// two texts given by the caller are compared (nothing is read online)
	TCHAR cid[ONLINE_CID_SIZE];
	double time;
	double prev_time;
	BOOL has_prev;
	int prev_skip;			// place of the previous version in the history when it is not in the list (-1 for none)
	HFONT hFont;
	HWND hView;
	int prev_id;
	int old_id;
	int new_id;
	TCHAR *old_code;		// NULL when there is no previous version
	TCHAR *new_code;
	BOOL old_ready;
	BOOL new_ready;
	BOOL closing;
} DIFF_DATA;

// the save dialog
typedef struct _SAVE_DATA {
	const TCHAR *code;
	const TCHAR *default_name;
	BOOL pg05_mode;
	int speed;
	HFONT hFont;
	int save_id;
	BOOL post;
	CONTENT content;
	TCHAR password[BUF_SIZE];
} SAVE_DATA;

// the password input dialog
typedef struct _INPUT_DATA {
	HFONT hFont;
	TCHAR *ret;
	int size;
} INPUT_DATA;

/* Local Function Prototypes */
static INT_PTR CALLBACK history_proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

/*
 * res - string of the resource (a few can be used at the same time)
 */
static TCHAR *res(const UINT id)
{
	static TCHAR buf[RES_BUF_COUNT][RES_BUF_SIZE];
	static int index = 0;
	TCHAR *ret = buf[index];

	index = (index + 1) % RES_BUF_COUNT;
	*ret = TEXT('\0');
	LoadString(hInst, id, ret, RES_BUF_SIZE - 1);
	return ret;
}

/*
 * next_request_id - identifier of a request, unique in the process (0 means none)
 */
static int next_request_id(void)
{
	if (++request_seq <= 0) {
		request_seq = 1;
	}
	return request_seq;
}

/*
 * drop_results - free the results that came after the dialog was closed
 */
static void drop_results(const HWND hDlg)
{
	MSG msg;

	while (PeekMessage(&msg, hDlg, WM_HTTP_RESULT, WM_HTTP_RESULT, PM_REMOVE)) {
		http_free_result((HTTP_RESULT *)msg.lParam);
	}
}

/*
 * message - show a message like alert() of the web version
 */
static void message(const HWND hWnd, const TCHAR *msg, const UINT icon)
{
	MessageBox(hWnd, msg, APP_NAME, MB_OK | icon);
}

/*
 * status_message - show the status of a failed request ("statusText(status)")
 */
static void status_message(const HWND hWnd, const HTTP_RESULT *res)
{
	TCHAR buf[HTTP_STATUS_TEXT_SIZE + 32];

	wsprintf(buf, TEXT("%s(%d)"), res->status_text, res->status);
	message(hWnd, buf, MB_ICONEXCLAMATION);
}

/*
 * is_js_space - white space removed by JavaScript's trim()
 */
static BOOL is_js_space(const TCHAR c)
{
	return ((c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0xA0 || c == 0x1680 ||
		(c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
		c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF);
}

/*
 * js_trim - remove the white space at both ends
 */
static void js_trim(TCHAR *str)
{
	TCHAR *p, *r;

	for (p = str; is_js_space(*p); p++);
	for (r = p + lstrlen(p); r > p && is_js_space(*(r - 1)); r--);
	MoveMemory(str, p, sizeof(TCHAR) * (r - p));
	*(str + (r - p)) = TEXT('\0');
}

/*
 * js_lower - lower case like JavaScript's toLowerCase() (free the result with mem_free)
 *            ICU (icu.dll, Windows 10 1703 or later) does the full Unicode case mapping
 *            of JavaScript; without it CharLowerBuff differs for a few hundred characters
 */
typedef int (__cdecl *ICU_STRTOLOWER)(WCHAR *dest, int destCapacity, const WCHAR *src, int srcLength, const char *locale, int *pErrorCode);

static TCHAR *js_lower(const TCHAR *str)
{
	static ICU_STRTOLOWER u_strToLower = NULL;
	static BOOL checked = FALSE;
	TCHAR *ret;
	int len = lstrlen(str);

	if (!checked) {
		HMODULE hIcu = LoadLibrary(TEXT("icu.dll"));
		if (hIcu != NULL) {
			u_strToLower = (ICU_STRTOLOWER)GetProcAddress(hIcu, "u_strToLower");
		}
		checked = TRUE;
	}
	if (u_strToLower != NULL) {
		// a character can become two (U+0130 is "i" and U+0307)
		int err = 0, cap = len * 2 + 1, n;
		if ((ret = mem_alloc(sizeof(TCHAR) * cap)) == NULL) {
			return NULL;
		}
		n = u_strToLower(ret, cap, str, len, "", &err);
		if (err <= 0 && n >= 0 && n < cap) {
			*(ret + n) = TEXT('\0');
			return ret;
		}
		mem_free(&ret);
	}
	if ((ret = alloc_copy(str)) == NULL) {
		return NULL;
	}
	CharLowerBuff(ret, len);
	return ret;
}

/*
 * password_crc - CRC32 of the password sent to the server (pg0_string.crc32 of the web version)
 */
static unsigned int password_crc(const TCHAR *str)
{
	static unsigned int table[256];
	static BOOL init = FALSE;
	TCHAR *buf, *lower, *p;
	unsigned int crc = 0xFFFFFFFF;
	int i, j;

	if (str == NULL || *str == TEXT('\0')) {
		return 0;
	}
	if (!init) {
		for (i = 0; i < 256; i++) {
			unsigned int c = (unsigned int)i;
			for (j = 0; j < 8; j++) {
				c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
			}
			table[i] = c;
		}
		init = TRUE;
	}
	if ((buf = alloc_copy(str)) == NULL) {
		return 0;
	}
	js_trim(buf);
	lower = js_lower(buf);
	mem_free(&buf);
	if ((buf = lower) == NULL) {
		return 0;
	}
	// only the low byte of each UTF-16 code unit counts, as in the web version
	for (p = buf; *p != TEXT('\0'); p++) {
		crc = (crc >> 8) ^ table[(crc ^ (unsigned int)*p) & 0xFF];
	}
	mem_free(&buf);
	return crc ^ 0xFFFFFFFF;
}

/*
 * create_uuid - random UUID (version 4) like crypto.randomUUID()
 */
static void create_uuid(TCHAR *ret)
{
	unsigned char b[16];
	unsigned int r;
	int i;

	for (i = 0; i < 16; i += 4) {
		if (rand_s(&r) != 0) {
			r = GetTickCount() * 2654435761u + i;
		}
		CopyMemory(b + i, &r, 4);
	}
	b[6] = (b[6] & 0x0F) | 0x40;
	b[8] = (b[8] & 0x3F) | 0x80;
	wsprintf(ret, TEXT("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x"),
		b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

/*
 * protect_string - encrypt a string for the current user (DPAPI) in Base64
 */
static BOOL protect_string(const TCHAR *str, TCHAR *ret, const int size)
{
	DATA_BLOB in, out;
	DWORD len = size;
	BOOL result;

	*ret = TEXT('\0');
	if (*str == TEXT('\0')) {
		return TRUE;
	}
	in.pbData = (BYTE *)str;
	in.cbData = sizeof(TCHAR) * lstrlen(str);
	if (CryptProtectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out) == FALSE) {
		return FALSE;
	}
	result = CryptBinaryToString(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, ret, &len);
	LocalFree(out.pbData);
	if (!result) {
		*ret = TEXT('\0');
	}
	return result;
}

/*
 * unprotect_string - decrypt a string encrypted by protect_string
 */
static BOOL unprotect_string(const TCHAR *str, TCHAR *ret, const int size)
{
	DATA_BLOB in, out;
	DWORD len = 0;
	int count;

	*ret = TEXT('\0');
	if (*str == TEXT('\0')) {
		return TRUE;
	}
	if (CryptStringToBinary(str, 0, CRYPT_STRING_BASE64, NULL, &len, NULL, NULL) == FALSE) {
		return FALSE;
	}
	if ((in.pbData = mem_alloc(len)) == NULL) {
		return FALSE;
	}
	in.cbData = len;
	if (CryptStringToBinary(str, 0, CRYPT_STRING_BASE64, in.pbData, &len, NULL, NULL) == FALSE ||
		CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out) == FALSE) {
		mem_free(&in.pbData);
		return FALSE;
	}
	mem_free(&in.pbData);
	count = out.cbData / sizeof(TCHAR);
	if (count >= size) {
		count = size - 1;
	}
	CopyMemory(ret, out.pbData, sizeof(TCHAR) * count);
	*(ret + count) = TEXT('\0');
	LocalFree(out.pbData);
	return TRUE;
}

/*
 * tag_index - index of a genre (-1 when it is unknown)
 */
static int tag_index(const TCHAR *tag)
{
	int i;

	for (i = 0; i < TAG_COUNT; i++) {
		if (lstrcmp(tag_ids[i], tag) == 0) {
			return i;
		}
	}
	return -1;
}

/*
 * filter_index - chip of the list filter (0: all, 1: mine, 2-: genres)
 */
static int filter_index(void)
{
	int i;

	if (lstrcmp(list_filter, TEXT("mine")) == 0) {
		return 1;
	}
	i = tag_index(list_filter);
	return (i >= 0) ? i + 2 : 0;
}

/*
 * is_new_sort - the list is sorted by date
 */
static BOOL is_new_sort(void)
{
	return (lstrcmp(list_sort, TEXT("new")) == 0);
}

/*
 * make_url - URL on the server (free it with mem_free)
 */
static TCHAR *make_url(const TCHAR *path)
{
	return alloc_join(server, path);
}

/*
 * set_clipboard - copy a text to the clipboard
 */
static BOOL set_clipboard(const HWND hWnd, const TCHAR *str)
{
	HGLOBAL hMem;
	TCHAR *p;
	int size = sizeof(TCHAR) * (lstrlen(str) + 1);

	if ((hMem = GlobalAlloc(GMEM_MOVEABLE, size)) == NULL) {
		return FALSE;
	}
	if ((p = GlobalLock(hMem)) == NULL) {
		GlobalFree(hMem);
		return FALSE;
	}
	CopyMemory(p, str, size);
	GlobalUnlock(hMem);
	if (OpenClipboard(hWnd) == FALSE) {
		GlobalFree(hMem);
		return FALSE;
	}
	EmptyClipboard();
	if (SetClipboardData(CF_UNICODETEXT, hMem) == NULL) {
		GlobalFree(hMem);
		CloseClipboard();
		return FALSE;
	}
	CloseClipboard();
	return TRUE;
}

/*
 * format_time - "(date time)" of a time in milliseconds since 1970 (UTC)
 */
static void format_time(const double ms, TCHAR *ret)
{
	ULARGE_INTEGER ul;
	FILETIME ft;
	SYSTEMTIME st, lst;
	TCHAR date[64], time[64];

	*ret = TEXT('\0');
	if (ms <= 0) {
		return;
	}
	ul.QuadPart = (ULONGLONG)ms * 10000 + 116444736000000000ULL;
	ft.dwLowDateTime = ul.LowPart;
	ft.dwHighDateTime = ul.HighPart;
	if (FileTimeToSystemTime(&ft, &st) == FALSE || SystemTimeToTzSpecificLocalTime(NULL, &st, &lst) == FALSE) {
		return;
	}
	GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &lst, NULL, date, 64, NULL);
	GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &lst, NULL, time, 64);
	wsprintf(ret, TEXT("(%s %s)"), date, time);
}

/*
 * to_crlf - copy a text with its line breaks (LF, CR+LF and lone CR) changed to CR+LF (the editor of this program)
 */
static TCHAR *to_crlf(const TCHAR *str)
{
	const TCHAR *p;
	TCHAR *ret, *r;
	int len = 0;

	for (p = str; *p != TEXT('\0'); p++) {
		if (*p == TEXT('\r') && *(p + 1) == TEXT('\n')) {
			continue;
		}
		len += (*p == TEXT('\r') || *p == TEXT('\n')) ? 2 : 1;
	}
	if ((ret = mem_alloc(sizeof(TCHAR) * (len + 1))) == NULL) {
		return NULL;
	}
	for (p = str, r = ret; *p != TEXT('\0'); p++) {
		if (*p == TEXT('\r') && *(p + 1) == TEXT('\n')) {
			continue;
		}
		if (*p == TEXT('\r') || *p == TEXT('\n')) {
			*(r++) = TEXT('\r');
			*(r++) = TEXT('\n');
		} else {
			*(r++) = *p;
		}
	}
	*r = TEXT('\0');
	return ret;
}

/*
 * to_lf - copy a text with its line breaks (CR+LF and lone CR) changed to LF (the web version)
 */
static TCHAR *to_lf(const TCHAR *str)
{
	const TCHAR *p;
	TCHAR *ret, *r;

	if ((ret = mem_alloc(sizeof(TCHAR) * (lstrlen(str) + 1))) == NULL) {
		return NULL;
	}
	for (p = str, r = ret; *p != TEXT('\0'); p++) {
		if (*p == TEXT('\r')) {
			if (*(p + 1) != TEXT('\n')) {
				*(r++) = TEXT('\n');
			}
			continue;
		}
		*(r++) = *p;
	}
	*r = TEXT('\0');
	return ret;
}

/*
 * get_window_text - text of a control (free it with mem_free)
 */
static TCHAR *get_window_text(const HWND hWnd)
{
	TCHAR *buf;
	int len = GetWindowTextLength(hWnd);

	if ((buf = mem_alloc(sizeof(TCHAR) * (len + 1))) == NULL) {
		return NULL;
	}
	GetWindowText(hWnd, buf, len + 1);
	return buf;
}

/*
 * dialog_font - font of the dialogs (the message font of the system)
 */
static HFONT dialog_font(void)
{
	NONCLIENTMETRICS ncm;

	ZeroMemory(&ncm, sizeof(NONCLIENTMETRICS));
	ncm.cbSize = sizeof(NONCLIENTMETRICS);
	SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);
	return CreateFontIndirect(&ncm.lfMessageFont);
}

/*
 * text_height - height of a line of the font
 */
static int text_height(const HWND hWnd, const HFONT hFont)
{
	TEXTMETRIC tm;
	HDC hdc;
	HFONT hRetFont;

	hdc = GetDC(hWnd);
	hRetFont = SelectObject(hdc, hFont);
	GetTextMetrics(hdc, &tm);
	SelectObject(hdc, hRetFont);
	ReleaseDC(hWnd, hdc);
	return tm.tmHeight;
}

/*
 * text_width - width of a text in the font
 */
static int text_width(const HWND hWnd, const HFONT hFont, const TCHAR *str)
{
	HDC hdc;
	HFONT hRetFont;
	SIZE sz = {0, 0};

	hdc = GetDC(hWnd);
	hRetFont = SelectObject(hdc, hFont);
	GetTextExtentPoint32(hdc, str, lstrlen(str), &sz);
	SelectObject(hdc, hRetFont);
	ReleaseDC(hWnd, hdc);
	return sz.cx;
}

/*
 * create_control - create a control of a dialog
 */
static HWND create_control(const HWND hDlg, const TCHAR *cls, const TCHAR *text, const DWORD style, const DWORD ex_style, const int id, const HFONT hFont)
{
	HWND hWnd;

	hWnd = CreateWindowEx(ex_style, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
		hDlg, (HMENU)(INT_PTR)id, hInst, NULL);
	if (hWnd != NULL) {
		SendMessage(hWnd, WM_SETFONT, (WPARAM)hFont, FALSE);
	}
	return hWnd;
}

/*
 * work_area - monitor of a window (rcWork is the area without the taskbar)
 */
static void work_area(const HWND hWnd, MONITORINFO *mi)
{
	ZeroMemory(mi, sizeof(MONITORINFO));
	mi->cbSize = sizeof(MONITORINFO);
	if (GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), mi) == FALSE) {
		SystemParametersInfo(SPI_GETWORKAREA, 0, &mi->rcWork, 0);
	}
}

/*
 * center_window - put a window in the middle of its owner
 */
static void center_window(const HWND hWnd, const int width, const int height)
{
	HWND hOwner = GetWindow(hWnd, GW_OWNER);
	MONITORINFO mi;
	RECT rect;
	int x, y;

	if (hOwner == NULL || IsIconic(hOwner)) {
		hOwner = GetDesktopWindow();
	}
	GetWindowRect(hOwner, &rect);
	work_area(hOwner, &mi);
	x = rect.left + (rect.right - rect.left - width) / 2;
	y = rect.top + (rect.bottom - rect.top - height) / 2;
	if (x + width > mi.rcWork.right) {
		x = mi.rcWork.right - width;
	}
	if (y + height > mi.rcWork.bottom) {
		y = mi.rcWork.bottom - height;
	}
	if (x < mi.rcWork.left) {
		x = mi.rcWork.left;
	}
	if (y < mi.rcWork.top) {
		y = mi.rcWork.top;
	}
	SetWindowPos(hWnd, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

/*
 * set_client_size - size a window so that its client area has the given size, in the middle of its owner
 */
static void set_client_size(const HWND hWnd, const int width, const int height)
{
	RECT rect;

	SetRect(&rect, 0, 0, width, height);
	AdjustWindowRectEx(&rect, (DWORD)GetWindowLongPtr(hWnd, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtr(hWnd, GWL_EXSTYLE));
	center_window(hWnd, rect.right - rect.left, rect.bottom - rect.top);
}

/*
 * dialog_box - show a modal dialog without a template (the controls are made in WM_INITDIALOG)
 */
static INT_PTR dialog_box(const HWND hParent, const DWORD style, const DLGPROC proc, const LPARAM param)
{
	struct {
		DLGTEMPLATE dt;
		WORD menu;
		WORD cls;
		WORD title;
	} tmpl;

	ZeroMemory(&tmpl, sizeof(tmpl));
	tmpl.dt.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | style;
	return DialogBoxIndirectParam(hInst, &tmpl.dt, hParent, proc, param);
}

/*
 * white_ctlcolor - white background of the dialogs (like the web version)
 */
static INT_PTR white_ctlcolor(const UINT msg, const WPARAM wParam)
{
	switch (msg) {
	case WM_CTLCOLORDLG:
		return (INT_PTR)GetStockObject(WHITE_BRUSH);
	case WM_CTLCOLORSTATIC:
		SetBkColor((HDC)wParam, RGB(0xFF, 0xFF, 0xFF));
		return (INT_PTR)GetStockObject(WHITE_BRUSH);
	}
	return 0;
}

/*
 * make_item - list item of a script in the response
 */
static void make_item(const JSON *js, const BOOL history, const BOOL first, ONLINE_ITEM *item, TCHAR *time)
{
	const TCHAR *str;
	JSON *tags, *tag;

	ZeroMemory(item, sizeof(ONLINE_ITEM));
	if ((str = json_get_string(js, TEXT("cid"))) != NULL) {
		lstrcpyn(item->cid, str, ONLINE_CID_SIZE);
	}
	str = json_get_string(js, TEXT("name"));
	item->name = (TCHAR *)((str != NULL) ? str : TEXT(""));
	str = json_get_string(js, TEXT("author"));
	item->author = (TCHAR *)((str != NULL) ? str : TEXT(""));
	item->update_time = json_get_number(js, TEXT("updateTime"), 0);
	format_time(item->update_time, time);
	item->time = time;
	item->private_mode = json_is_true(json_get(js, TEXT("private")));
	if (history) {
		str = json_get_string(js, TEXT("memo"));
		item->memo = (TCHAR *)str;
		item->current = first;
		item->menu = TRUE;
		return;
	}
	item->menu = TRUE;
	tags = json_get(js, TEXT("tags"));
	if (tags != NULL && tags->type == JSON_ARRAY) {
		for (tag = tags->child; tag != NULL && item->tag_count < ONLINE_MAX_TAGS; tag = tag->next) {
			int i;
			if (tag->type != JSON_STRING) {
				continue;
			}
			lstrcpyn(item->tags[item->tag_count], tag->str, ONLINE_TAG_SIZE);
			i = tag_index(tag->str);
			item->tag_labels[item->tag_count] = (i >= 0) ? tag_labels[i] : item->tags[item->tag_count];
			item->tag_count++;
		}
	}
}

/*
 * read_script - take the script from the response of an item
 */
static BOOL read_script(OPEN_DATA *od, const char *body, const TCHAR *cid)
{
	JSON *js, *tags;
	const TCHAR *str;
	ONLINE_SCRIPT *script = od->script;
	CONTENT *content = &od->content;

	if ((js = json_parse(body)) == NULL || js->type != JSON_OBJECT) {
		json_free(js);
		return FALSE;
	}
	str = json_get_string(js, TEXT("code"));
	if ((script->code = to_crlf((str != NULL) ? str : TEXT(""))) == NULL) {
		json_free(js);
		return FALSE;
	}
	str = json_get_string(js, TEXT("type"));
	script->pg05_mode = !(str != NULL && lstrcmp(str, TEXT("PG0")) == 0);
	script->has_speed = (json_get(js, TEXT("speed")) != NULL && json_get(js, TEXT("speed"))->type == JSON_NUMBER);
	script->speed = (int)json_get_number(js, TEXT("speed"), 0);

	// kept in the dialog until it ends with the script (see finish_open)
	ZeroMemory(content, sizeof(CONTENT));
	lstrcpyn(content->cid, cid, ONLINE_CID_SIZE);
	str = json_get_string(js, TEXT("name"));
	lstrcpyn(content->name, (str != NULL) ? str : TEXT(""), BUF_SIZE);
	str = json_get_string(js, TEXT("author"));
	lstrcpyn(content->author, (str != NULL) ? str : TEXT(""), BUF_SIZE);
	content->private_mode = json_is_true(json_get(js, TEXT("private")));
	tags = json_get(js, TEXT("tags"));
	if (tags != NULL && tags->type == JSON_ARRAY && tags->child != NULL && tags->child->type == JSON_STRING) {
		lstrcpyn(content->tag, tags->child->str, ONLINE_TAG_SIZE);
	}
	json_free(js);
	return TRUE;
}

/*
 * end_open - end the list dialog; the results that arrive afterwards are dropped
 */
static void end_open(const HWND hDlg, OPEN_DATA *od, const INT_PTR result)
{
	od->closing = TRUE;
	EndDialog(hDlg, result);
}

/*
 * open_request_list - read the next page of the list
 */
static void open_request_list(const HWND hDlg, OPEN_DATA *od)
{
	TCHAR *url, *path, *enc_keyword, *enc_uuid;
	TCHAR buf[BUF_SIZE * 2];
	int len;

	od->list_id = next_request_id();
	if (od->history) {
		// one more than is shown tells whether older versions remain
		wsprintf(buf, TEXT("/api/script/history/%s?count=%d&skip=%d"), od->cid, LIST_COUNT + 1, od->skip);
		url = make_url(buf);
	} else {
		// what is in the search box, even if the search button has not been pressed (as the web version)
		GetWindowText(od->hSearch, buf, BUF_SIZE);
		enc_keyword = http_encode_component(buf);
		enc_uuid = http_encode_component(uuid);
		len = ((enc_keyword != NULL) ? lstrlen(enc_keyword) : 0) + ((enc_uuid != NULL) ? lstrlen(enc_uuid) : 0) + BUF_SIZE;
		path = (enc_keyword != NULL && enc_uuid != NULL) ? mem_alloc(sizeof(TCHAR) * len) : NULL;
		if (path != NULL) {
			_stprintf_s(path, len, TEXT("/api/script/%s?count=%d&skip=%d&uuid=%s&sort=%s"),
				enc_keyword, LIST_COUNT, od->skip, enc_uuid, (is_new_sort()) ? TEXT("new") : TEXT("popular"));
			if (lstrcmp(list_filter, TEXT("mine")) == 0) {
				lstrcat(path, TEXT("&mine=1"));
			} else if (*list_filter != TEXT('\0')) {
				lstrcat(path, TEXT("&tag="));
				lstrcat(path, list_filter);
			}
		}
		mem_free(&enc_keyword);
		mem_free(&enc_uuid);
		url = (path != NULL) ? make_url(path) : NULL;
		mem_free(&path);
	}
	if (url == NULL || http_request_async(hDlg, WM_HTTP_RESULT, od->list_id, TEXT("GET"), url, NULL, REQUEST_LIST) == FALSE) {
		SendMessage(od->hList, OLM_SETLOADING, FALSE, 0);
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_CONNECTION), MB_ICONEXCLAMATION);
	}
	mem_free(&url);
}

/*
 * open_reload - read the list from the first page
 */
static void open_reload(const HWND hDlg, OPEN_DATA *od)
{
	SendMessage(od->hList, OLM_CLEAR, 0, 0);
	SendMessage(od->hList, OLM_SETLOADING, TRUE, 0);
	od->skip = 0;
	od->more = FALSE;
	open_request_list(hDlg, od);
}

/*
 * open_list_result - add the page of the list
 */
static void open_list_result(const HWND hDlg, OPEN_DATA *od, const HTTP_RESULT *result)
{
	JSON *js = NULL, *item;
	ONLINE_ITEM oi;
	TCHAR time[BUF_SIZE];
	int count = 0, shown, i;

	SendMessage(od->hList, OLM_SETLOADING, FALSE, 0);
	if (result->status == 200) {
		js = json_parse(result->body);
	}
	if (js == NULL || js->type != JSON_ARRAY) {
		json_free(js);
		if (od->history && result->status == 404) {
			message(hDlg, res(IDS_STRING_ONLINE_ERROR_NOT_FOUND), MB_ICONEXCLAMATION);
		} else if (od->history && result->status != 0 && result->status != 200) {
			status_message(hDlg, result);
		} else {
			message(hDlg, res(IDS_STRING_ONLINE_ERROR_CONNECTION), MB_ICONEXCLAMATION);
		}
		// a version may have been read while the message was shown
		if (od->history && !od->closing) {
			end_open(hDlg, od, IDCANCEL);
		}
		return;
	}
	SendMessage(od->hList, OLM_SETMORE, FALSE, 0);
	for (item = js->child; item != NULL; item = item->next) {
		count++;
	}
	// the history is read with one more item than it shows
	od->more = (od->history) ? (count > LIST_COUNT) : (count >= LIST_COUNT);
	shown = (count > LIST_COUNT && od->history) ? LIST_COUNT : count;
	for (item = js->child, i = 0; item != NULL && i < shown; item = item->next, i++) {
		if (item->type != JSON_OBJECT) {
			continue;
		}
		make_item(item, od->history, (SendMessage(od->hList, OLM_GETCOUNT, 0, 0) == 0), &oi, time);
		// the first version has nothing to compare with, so its menu would be empty unless it is also the current one
		if (od->history && !od->more && i == shown - 1 && !oi.current) {
			oi.menu = FALSE;
		}
		SendMessage(od->hList, OLM_ADDITEM, 0, (LPARAM)&oi);
	}
	json_free(js);
	if (od->more) {
		od->skip += shown;
		SendMessage(od->hList, OLM_SETMORE, TRUE, 0);
	}
}

/*
 * open_request_script - read a script (or a version of it) chosen in the list
 */
static void open_request_script(const HWND hDlg, OPEN_DATA *od, const int index)
{
	ONLINE_ITEM *item = (ONLINE_ITEM *)SendMessage(od->hList, OLM_GETITEM, index, 0);
	TCHAR path[BUF_SIZE];
	TCHAR *url;

	// one script at a time
	if (item == NULL || od->script_id != 0) {
		return;
	}
	if (od->history) {
		_stprintf_s(path, BUF_SIZE, TEXT("/api/script/item/%s/%.0f"), od->cid, item->update_time);
	} else {
		wsprintf(path, TEXT("/api/script/item/%s"), item->cid);
		lstrcpyn(od->cid, item->cid, ONLINE_CID_SIZE);
	}
	if ((url = make_url(path)) == NULL) {
		return;
	}
	od->script_id = next_request_id();
	if (http_request_async(hDlg, WM_HTTP_RESULT, od->script_id, TEXT("GET"), url, NULL, REQUEST_SCRIPT) == FALSE) {
		od->script_id = 0;
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_CONNECTION), MB_ICONEXCLAMATION);
	}
	mem_free(&url);
}

/*
 * open_script_result - open the script that was read
 */
static void open_script_result(const HWND hDlg, OPEN_DATA *od, const HTTP_RESULT *result)
{
	od->script_id = 0;
	switch (result->status) {
	case 200:
		if (read_script(od, result->body, od->cid) == TRUE) {
			end_open(hDlg, od, IDOK);
			return;
		}
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_CONNECTION), MB_ICONEXCLAMATION);
		break;
	case 404:
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_NOT_FOUND), MB_ICONEXCLAMATION);
		break;
	case 0:
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_CONNECTION), MB_ICONEXCLAMATION);
		break;
	default:
		status_message(hDlg, result);
		break;
	}
}

/*
 * input_proc - dialog to type the edit password (window.prompt of the web version)
 */
static INT_PTR CALLBACK input_proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	INPUT_DATA *id = (INPUT_DATA *)GetWindowLongPtr(hDlg, DWLP_USER);

	switch (msg) {
	case WM_INITDIALOG:
		{
			HWND hLabel, hEdit, hOk, hCancel;
			int margin = Scale(DIALOG_MARGIN);
			int width = Scale(INPUT_WIDTH);
			int fh, eh, bw, bh, y;

			id = (INPUT_DATA *)lParam;
			SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)id);
			SetWindowText(hDlg, res(IDS_STRING_ONLINE_REMOVE));
			id->hFont = dialog_font();
			fh = text_height(hDlg, id->hFont);
			eh = fh + Scale(8);
			bw = Scale(88);
			bh = fh + Scale(14);

			hLabel = create_control(hDlg, TEXT("STATIC"), res(IDS_STRING_ONLINE_REMOVE_PASSWORD), SS_LEFT, 0, IDC_LABEL, id->hFont);
			hEdit = create_control(hDlg, TEXT("EDIT"), TEXT(""), WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, WS_EX_CLIENTEDGE, IDC_INPUT, id->hFont);
			SendMessage(hEdit, EM_SETLIMITTEXT, id->size - 1, 0);
			hOk = create_control(hDlg, TEXT("BUTTON"), res(IDS_STRING_ONLINE_OK_BUTTON), WS_TABSTOP | BS_DEFPUSHBUTTON, 0, IDOK, id->hFont);
			hCancel = create_control(hDlg, TEXT("BUTTON"), res(IDS_STRING_ONLINE_CANCEL_BUTTON), WS_TABSTOP | BS_PUSHBUTTON, 0, IDCANCEL, id->hFont);
			y = margin;
			MoveWindow(hLabel, margin, y, width, fh, FALSE);
			y += fh + Scale(6);
			MoveWindow(hEdit, margin, y, width, eh, FALSE);
			y += eh + Scale(16);
			MoveWindow(hCancel, margin + width - bw, y, bw, bh, FALSE);
			MoveWindow(hOk, margin + width - bw * 2 - Scale(8), y, bw, bh, FALSE);
			y += bh + margin;
			set_client_size(hDlg, width + margin * 2, y);
			SetFocus(hEdit);
		}
		return FALSE;

	case WM_DESTROY:
		if (id != NULL && id->hFont != NULL) {
			DeleteObject(id->hFont);
			id->hFont = NULL;
		}
		break;

	case WM_CTLCOLORDLG:
	case WM_CTLCOLORSTATIC:
		return white_ctlcolor(msg, wParam);

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			GetDlgItemText(hDlg, IDC_INPUT, id->ret, id->size);
			EndDialog(hDlg, IDOK);
			break;
		case IDCANCEL:
			EndDialog(hDlg, IDCANCEL);
			break;
		}
		break;
	}
	return FALSE;
}

/*
 * open_remove - remove a script from the online storage
 */
static void open_remove(const HWND hDlg, OPEN_DATA *od, const TCHAR *cid)
{
	INPUT_DATA id;
	JSON_WRITER jw;
	REMOVE_REQUEST *rr;
	TCHAR pass[BUF_SIZE];
	TCHAR *url, *path;
	char *body;
	int i;

	ZeroMemory(&id, sizeof(INPUT_DATA));
	*pass = TEXT('\0');
	id.ret = pass;
	id.size = BUF_SIZE;
	if (dialog_box(hDlg, 0, input_proc, (LPARAM)&id) != IDOK) {
		return;
	}
	json_writer_init(&jw);
	json_begin_object(&jw);
	json_add_number(&jw, TEXT("password"), password_crc(pass));
	json_end_object(&jw);
	body = json_writer_to_utf8(&jw);
	json_writer_free(&jw);

	// a free slot, or the oldest one
	for (i = 0; i < MAX_REMOVES && od->removes[i].id != 0; i++);
	rr = &od->removes[(i < MAX_REMOVES) ? i : 0];
	rr->id = next_request_id();
	lstrcpyn(rr->cid, cid, ONLINE_CID_SIZE);
	path = alloc_join(TEXT("/api/script/"), cid);
	url = (path != NULL) ? make_url(path) : NULL;
	if (body == NULL || url == NULL ||
		http_request_async(hDlg, WM_HTTP_RESULT, rr->id, TEXT("DELETE"), url, body, REQUEST_REMOVE) == FALSE) {
		rr->id = 0;
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_CONNECTION), MB_ICONEXCLAMATION);
	}
	mem_free(&path);
	mem_free(&url);
	mem_free(&body);
}

/*
 * open_remove_result - take the removed script out of the list
 */
static void open_remove_result(const HWND hDlg, OPEN_DATA *od, const HTTP_RESULT *result, const TCHAR *cid)
{
	int i;

	switch (result->status) {
	case 200:
		i = (int)SendMessage(od->hList, OLM_FINDCID, 0, (LPARAM)cid);
		if (i >= 0) {
			SendMessage(od->hList, OLM_DELETEITEM, i, 0);
		}
		break;
	case 401:
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_UNAUTHORIZED), MB_ICONEXCLAMATION);
		break;
	case 404:
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_NOT_FOUND), MB_ICONEXCLAMATION);
		break;
	case 0:
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_CONNECTION), MB_ICONEXCLAMATION);
		break;
	default:
		status_message(hDlg, result);
		break;
	}
}

/*
 * diff_end - end the diff dialog; the results that arrive afterwards are dropped
 */
static void diff_end(const HWND hDlg, DIFF_DATA *dd)
{
	dd->closing = TRUE;
	EndDialog(hDlg, IDCANCEL);
}

/*
 * diff_fail - tell why the versions could not be read and end the diff dialog
 */
static void diff_fail(const HWND hDlg, DIFF_DATA *dd, const HTTP_RESULT *result)
{
	if (dd->closing) {
		return;
	}
	// the other requests are dropped while the message is shown
	dd->closing = TRUE;
	if (result == NULL || result->status == 0 || result->status == 200) {
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_CONNECTION), MB_ICONEXCLAMATION);
	} else if (result->status == 404) {
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_NOT_FOUND), MB_ICONEXCLAMATION);
	} else {
		status_message(hDlg, result);
	}
	EndDialog(hDlg, IDCANCEL);
}

/*
 * diff_request_code - read the code of a version
 */
static BOOL diff_request_code(const HWND hDlg, DIFF_DATA *dd, const double time, int *id)
{
	TCHAR path[BUF_SIZE];
	TCHAR *url;
	BOOL ret;

	_stprintf_s(path, BUF_SIZE, TEXT("/api/script/item/%s/%.0f"), dd->cid, time);
	if ((url = make_url(path)) == NULL) {
		return FALSE;
	}
	*id = next_request_id();
	ret = http_request_async(hDlg, WM_HTTP_RESULT, *id, TEXT("GET"), url, NULL, REQUEST_CODE);
	mem_free(&url);
	return ret;
}

/*
 * diff_request_previous - look up the version saved just before the one shown, past the list read so far
 */
static BOOL diff_request_previous(const HWND hDlg, DIFF_DATA *dd)
{
	TCHAR path[BUF_SIZE];
	TCHAR *url;
	BOOL ret;

	wsprintf(path, TEXT("/api/script/history/%s?count=1&skip=%d"), dd->cid, dd->prev_skip);
	if ((url = make_url(path)) == NULL) {
		return FALSE;
	}
	dd->prev_id = next_request_id();
	ret = http_request_async(hDlg, WM_HTTP_RESULT, dd->prev_id, TEXT("GET"), url, NULL, REQUEST_PREVIOUS);
	mem_free(&url);
	return ret;
}

/*
 * diff_show - compare the two versions once both have been read
 */
static void diff_show(const HWND hDlg, DIFF_DATA *dd)
{
	DIFF_RESULT *dr;

	if (!dd->old_ready || !dd->new_ready) {
		return;
	}
	dr = text_diff_compare(dd->old_code, (dd->new_code != NULL) ? dd->new_code : TEXT(""));
	mem_free(&dd->old_code);
	mem_free(&dd->new_code);
	SendMessage(dd->hView, DVM_SETLOADING, FALSE, 0);
	if (dr == NULL) {
		dd->closing = TRUE;
		message(hDlg, TEXT("Alloc error"), MB_ICONERROR);
		EndDialog(hDlg, IDCANCEL);
		return;
	}
	SendMessage(dd->hView, DVM_SETRESULT, 0, (LPARAM)dr);
}

/*
 * diff_previous_result - read the code of the version found before the one shown
 */
static void diff_previous_result(const HWND hDlg, DIFF_DATA *dd, const HTTP_RESULT *result)
{
	JSON *js;

	dd->prev_id = 0;
	if (result->status != 200) {
		diff_fail(hDlg, dd, result);
		return;
	}
	if ((js = json_parse(result->body)) == NULL || js->type != JSON_ARRAY) {
		json_free(js);
		diff_fail(hDlg, dd, NULL);
		return;
	}
	if (js->child != NULL && js->child->type == JSON_OBJECT) {
		dd->prev_time = json_get_number(js->child, TEXT("updateTime"), 0);
		dd->has_prev = TRUE;
	}
	json_free(js);
	if (!dd->has_prev) {
		// with no version before it, every line is shown as added
		dd->old_ready = TRUE;
		diff_show(hDlg, dd);
	} else if (diff_request_code(hDlg, dd, dd->prev_time, &dd->old_id) == FALSE) {
		diff_fail(hDlg, dd, NULL);
	}
}

/*
 * diff_code_result - keep the code of a version that was read
 */
static void diff_code_result(const HWND hDlg, DIFF_DATA *dd, const HTTP_RESULT *result)
{
	JSON *js;
	const TCHAR *str;
	TCHAR *code;

	if (result->status != 200) {
		diff_fail(hDlg, dd, result);
		return;
	}
	if ((js = json_parse(result->body)) == NULL || js->type != JSON_OBJECT) {
		json_free(js);
		diff_fail(hDlg, dd, NULL);
		return;
	}
	str = json_get_string(js, TEXT("code"));
	code = alloc_copy((str != NULL) ? str : TEXT(""));
	json_free(js);
	if (code == NULL) {
		diff_fail(hDlg, dd, NULL);
		return;
	}
	if (result->id == dd->old_id) {
		dd->old_id = 0;
		dd->old_code = code;
		dd->old_ready = TRUE;
	} else {
		dd->new_id = 0;
		dd->new_code = code;
		dd->new_ready = TRUE;
	}
	diff_show(hDlg, dd);
}

/*
 * diff_layout - place the view
 */
static void diff_layout(const HWND hDlg, DIFF_DATA *dd)
{
	RECT client;
	int margin = Scale(DIFF_MARGIN);

	GetClientRect(hDlg, &client);
	MoveWindow(dd->hView, margin, margin, client.right - margin * 2, client.bottom - margin * 2, TRUE);
}

/*
 * diff_init - make the view and read the two versions at the same time
 */
static void diff_init(const HWND hDlg, DIFF_DATA *dd)
{
	MONITORINFO mi;
	int width, height;

	SetWindowText(hDlg, (dd->title != NULL) ? dd->title : res(IDS_STRING_ONLINE_HISTORY_DIFF));
	dd->hFont = (code_font_set) ? CreateFontIndirect(&code_font) : NULL;
	dd->hView = create_control(hDlg, DIFF_VIEW_WND_CLASS, TEXT(""), WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | WS_BORDER, 0, IDC_DIFF, dd->hFont);
	SendMessage(dd->hView, DVM_SETLINENO, code_line_no, 0);
	SendMessage(dd->hView, DVM_SETFOLDTEXT, 0, (LPARAM)res(IDS_STRING_ONLINE_DIFF_FOLD));
	SendMessage(dd->hView, DVM_SETLOADING, TRUE, 0);

	work_area(GetWindow(hDlg, GW_OWNER), &mi);
	width = Scale(DIFF_WIDTH);
	if (width > (mi.rcWork.right - mi.rcWork.left) * 9 / 10) {
		width = (mi.rcWork.right - mi.rcWork.left) * 9 / 10;
	}
	height = (mi.rcWork.bottom - mi.rcWork.top) * 85 / 100;
	center_window(hDlg, width, height);
	diff_layout(hDlg, dd);
	SetFocus(dd->hView);

	if (dd->title != NULL) {
		// both texts are already here
		dd->old_ready = dd->new_ready = TRUE;
		diff_show(hDlg, dd);
		return;
	}
	if (diff_request_code(hDlg, dd, dd->time, &dd->new_id) == FALSE) {
		diff_fail(hDlg, dd, NULL);
		return;
	}
	if (dd->has_prev) {
		if (diff_request_code(hDlg, dd, dd->prev_time, &dd->old_id) == FALSE) {
			diff_fail(hDlg, dd, NULL);
		}
	} else if (dd->prev_skip >= 0) {
		if (diff_request_previous(hDlg, dd) == FALSE) {
			diff_fail(hDlg, dd, NULL);
		}
	} else {
		dd->old_ready = TRUE;
	}
}

/*
 * diff_show_menu - menu of the view (copy, select all)
 */
static void diff_show_menu(const HWND hDlg, DIFF_DATA *dd, const LPARAM lParam)
{
	HMENU hMenu;
	POINT pt;
	int cmd;

	pt.x = GET_X_LPARAM(lParam);
	pt.y = GET_Y_LPARAM(lParam);
	if (pt.x == -1 && pt.y == -1) {
		// opened from the keyboard
		pt.x = pt.y = Scale(DIFF_MARGIN);
		ClientToScreen(dd->hView, &pt);
	}
	if ((hMenu = CreatePopupMenu()) == NULL) {
		return;
	}
	AppendMenu(hMenu, MF_STRING | ((SendMessage(dd->hView, DVM_HASSELECTION, 0, 0)) ? 0 : MF_GRAYED), ID_DIFF_COPY, res(IDS_STRING_ONLINE_DIFF_COPY));
	AppendMenu(hMenu, MF_STRING, ID_DIFF_SELECT_ALL, res(IDS_STRING_ONLINE_DIFF_SELECT_ALL));
	cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
	DestroyMenu(hMenu);
	switch (cmd) {
	case ID_DIFF_COPY:
		SendMessage(dd->hView, WM_COPY, 0, 0);
		break;
	case ID_DIFF_SELECT_ALL:
		SendMessage(dd->hView, DVM_SELECTALL, 0, 0);
		break;
	}
}

/*
 * diff_proc - dialog of the changes from the previous version
 */
static INT_PTR CALLBACK diff_proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	DIFF_DATA *dd = (DIFF_DATA *)GetWindowLongPtr(hDlg, DWLP_USER);

	switch (msg) {
	case WM_INITDIALOG:
		dd = (DIFF_DATA *)lParam;
		SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)dd);
		diff_init(hDlg, dd);
		return FALSE;

	case WM_DESTROY:
		http_cancel(hDlg);
		drop_results(hDlg);
		if (dd != NULL && dd->hFont != NULL) {
			DeleteObject(dd->hFont);
			dd->hFont = NULL;
		}
		break;

	case WM_SIZE:
		if (dd != NULL && dd->hView != NULL) {
			diff_layout(hDlg, dd);
		}
		break;

	case WM_GETMINMAXINFO:
		((MINMAXINFO *)lParam)->ptMinTrackSize.x = Scale(360);
		((MINMAXINFO *)lParam)->ptMinTrackSize.y = Scale(240);
		break;

	case WM_CTLCOLORDLG:
	case WM_CTLCOLORSTATIC:
		return white_ctlcolor(msg, wParam);

	case WM_CONTEXTMENU:
		if (dd != NULL && (HWND)wParam == dd->hView) {
			diff_show_menu(hDlg, dd, lParam);
			return TRUE;
		}
		break;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDCANCEL) {
			diff_end(hDlg, dd);
		}
		break;

	case WM_HTTP_RESULT:
		{
			HTTP_RESULT *result = (HTTP_RESULT *)lParam;
			if (!dd->closing) {
				if (result->param == REQUEST_PREVIOUS && result->id == dd->prev_id) {
					diff_previous_result(hDlg, dd, result);
				} else if (result->param == REQUEST_CODE && (result->id == dd->old_id || result->id == dd->new_id)) {
					diff_code_result(hDlg, dd, result);
				}
			}
			http_free_result(result);
		}
		break;
	}
	return FALSE;
}

/*
 * open_show_diff - show the changes of a version from the one saved just before it
 */
static void open_show_diff(const HWND hDlg, OPEN_DATA *od, const int index)
{
	ONLINE_ITEM *item = (ONLINE_ITEM *)SendMessage(od->hList, OLM_GETITEM, index, 0);
	ONLINE_ITEM *prev = (ONLINE_ITEM *)SendMessage(od->hList, OLM_GETITEM, index + 1, 0);
	DIFF_DATA dd;

	if (item == NULL) {
		return;
	}
	ZeroMemory(&dd, sizeof(DIFF_DATA));
	lstrcpyn(dd.cid, od->cid, ONLINE_CID_SIZE);
	dd.time = item->update_time;
	dd.prev_skip = -1;
	if (prev != NULL) {
		dd.prev_time = prev->update_time;
		dd.has_prev = TRUE;
	} else if (od->more) {
		// the item is the last one read so far
		dd.prev_skip = index + 1;
	}
	dialog_box(hDlg, WS_THICKFRAME | WS_MAXIMIZEBOX, diff_proc, (LPARAM)&dd);
	mem_free(&dd.old_code);
	mem_free(&dd.new_code);
}

/*
 * open_show_menu - menu of an item (copy the URL, revision history, remove, compare with the previous version)
 */
static void open_show_menu(const HWND hDlg, OPEN_DATA *od, const int index, const POINT pt)
{
	ONLINE_ITEM *item = (ONLINE_ITEM *)SendMessage(od->hList, OLM_GETITEM, index, 0);
	TCHAR cid[ONLINE_CID_SIZE];
	TCHAR *url, *tmp;
	HMENU hMenu;
	BOOL copy, diff;
	int cmd;

	if (item == NULL || (hMenu = CreatePopupMenu()) == NULL) {
		return;
	}
	// the item can be removed from the list while the menu is shown (a removal completes)
	lstrcpyn(cid, (od->history) ? od->cid : item->cid, ONLINE_CID_SIZE);
	// the URLs open the current version, so in the history only its item offers them
	copy = (!od->history || item->current);
	// only the first version has no item after it
	diff = (od->history && (index + 1 < (int)SendMessage(od->hList, OLM_GETCOUNT, 0, 0) || od->more));
	item = NULL;
	if (copy) {
		AppendMenu(hMenu, MF_STRING, ID_ITEM_COPY, res(IDS_STRING_ONLINE_COPY));
		AppendMenu(hMenu, MF_STRING, ID_ITEM_COPY_AUTORUN, res(IDS_STRING_ONLINE_COPY_AUTORUN));
	}
	if (!od->history) {
		AppendMenu(hMenu, MF_STRING, ID_ITEM_HISTORY, res(IDS_STRING_ONLINE_HISTORY_TITLE));
		AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
		AppendMenu(hMenu, MF_STRING, ID_ITEM_REMOVE, res(IDS_STRING_ONLINE_REMOVE));
	}
	if (diff) {
		AppendMenu(hMenu, MF_STRING, ID_ITEM_DIFF, res(IDS_STRING_ONLINE_HISTORY_DIFF));
	}
	if (GetMenuItemCount(hMenu) <= 0) {
		DestroyMenu(hMenu);
		return;
	}
	cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
	DestroyMenu(hMenu);

	switch (cmd) {
	case ID_ITEM_COPY:
	case ID_ITEM_COPY_AUTORUN:
		// the URL of the web version that opens (and runs) the script
		tmp = alloc_join(TEXT("/dev/?cid="), cid);
		url = (tmp != NULL) ? make_url(tmp) : NULL;
		mem_free(&tmp);
		if (url != NULL && cmd == ID_ITEM_COPY_AUTORUN) {
			tmp = url;
			url = alloc_join(tmp, TEXT("&run=1"));
			mem_free(&tmp);
		}
		if (url != NULL) {
			set_clipboard(hDlg, url);
			mem_free(&url);
		}
		break;

	case ID_ITEM_HISTORY:
		lstrcpyn(od->cid, cid, ONLINE_CID_SIZE);
		end_open(hDlg, od, RESULT_HISTORY);
		break;

	case ID_ITEM_REMOVE:
		open_remove(hDlg, od, cid);
		break;

	case ID_ITEM_DIFF:
		open_show_diff(hDlg, od, index);
		break;
	}
}

/*
 * open_show_filter - show the list filter on the chips and the list
 */
static void open_show_filter(OPEN_DATA *od)
{
	SendMessage(od->hChips, OCM_SETSEL, filter_index(), 0);
	// the badges of the chosen genre choose the item instead of the same genre again
	SendMessage(od->hList, OLM_SETSELTAG, 0, (LPARAM)((tag_index(list_filter) >= 0) ? list_filter : TEXT("")));
}

/*
 * open_set_filter - change the chip of the list
 */
static void open_set_filter(const HWND hDlg, OPEN_DATA *od, const int index)
{
	if (index <= 0) {
		*list_filter = TEXT('\0');
	} else if (index == 1) {
		lstrcpy(list_filter, TEXT("mine"));
	} else if (index - 2 < TAG_COUNT) {
		lstrcpy(list_filter, tag_ids[index - 2]);
	}
	open_show_filter(od);
	EnableWindow(od->hSort, lstrcmp(list_filter, TEXT("mine")) != 0);
	open_reload(hDlg, od);
}

/*
 * open_layout - place the controls
 */
static void open_layout(const HWND hDlg, OPEN_DATA *od)
{
	RECT client, rect;
	int margin = Scale(DIALOG_MARGIN);
	int fh = text_height(hDlg, od->hFont);
	int y = margin, width, sort_width, sort_height, chips_height, row;

	GetClientRect(hDlg, &client);
	width = client.right - margin * 2;
	if (!od->history) {
		// search box with the button inside a rounded frame
		int height = fh + Scale(12);
		int button = height - Scale(6);
		SetRect(&od->search_frame, margin, y, margin + width, y + height);
		MoveWindow(od->hSearch, margin + Scale(12), y + (height - fh) / 2,
			width - Scale(12) - button - Scale(10), fh, TRUE);
		MoveWindow(od->hSearchButton, margin + width - button - Scale(5), y + (height - button) / 2, button, button, TRUE);
		y += height + Scale(10);

		// genre chips and the sort order
		GetWindowRect(od->hSort, &rect);
		sort_height = rect.bottom - rect.top;
		sort_width = text_width(hDlg, od->hFont, res(IDS_STRING_ONLINE_SORT_POPULAR));
		if (text_width(hDlg, od->hFont, res(IDS_STRING_ONLINE_SORT_NEW)) > sort_width) {
			sort_width = text_width(hDlg, od->hFont, res(IDS_STRING_ONLINE_SORT_NEW));
		}
		sort_width += GetSystemMetrics(SM_CXVSCROLL) + Scale(16);
		chips_height = (int)SendMessage(od->hChips, OCM_GETHEIGHT, 0, 0);
		row = (chips_height > sort_height) ? chips_height : sort_height;
		MoveWindow(od->hChips, margin, y + (row - chips_height) / 2, width - sort_width - Scale(8), chips_height, TRUE);
		MoveWindow(od->hSort, margin + width - sort_width, y + (row - sort_height) / 2, sort_width, sort_height * 8, TRUE);
		y += row + Scale(15);
	}
	MoveWindow(od->hList, margin, y, width, client.bottom - margin - y, TRUE);
	InvalidateRect(hDlg, NULL, TRUE);
}

/*
 * draw_search_button - draw the magnifier of the search button
 */
static void draw_search_button(const DRAWITEMSTRUCT *dis)
{
	HPEN hPen, hRetPen;
	HBRUSH hRetBrush;
	COLORREF color = RGB(0x33, 0x33, 0x33);
	int w = dis->rcItem.right - dis->rcItem.left;
	int h = dis->rcItem.bottom - dis->rcItem.top;
	int r = w * 22 / 100;
	int cx = dis->rcItem.left + w * 44 / 100;
	int cy = dis->rcItem.top + h * 44 / 100;

	FillRect(dis->hDC, &dis->rcItem, GetStockObject(WHITE_BRUSH));
	if (dis->itemState & ODS_SELECTED) {
		color = RGB(0x99, 0x99, 0x99);
	} else if (dis->itemState & ODS_FOCUS) {
		color = RGB(0x70, 0x70, 0x70);
	}
	hPen = CreatePen(PS_SOLID, Scale(2), color);
	hRetPen = SelectObject(dis->hDC, hPen);
	hRetBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
	Ellipse(dis->hDC, cx - r, cy - r, cx + r, cy + r);
	MoveToEx(dis->hDC, cx + r * 7 / 10, cy + r * 7 / 10, NULL);
	LineTo(dis->hDC, dis->rcItem.left + w * 80 / 100, dis->rcItem.top + h * 80 / 100);
	SelectObject(dis->hDC, hRetBrush);
	SelectObject(dis->hDC, hRetPen);
	DeleteObject(hPen);
	if ((dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT)) {
		RECT rect = dis->rcItem;
		DrawFocusRect(dis->hDC, &rect);
	}
}

/*
 * open_init - make the controls of the list dialog
 */
static void open_init(const HWND hDlg, OPEN_DATA *od)
{
	MONITORINFO mi;
	int i, width, height;

	od->hFont = dialog_font();
	SetWindowText(hDlg, res((od->history) ? IDS_STRING_ONLINE_HISTORY_TITLE : IDS_STRING_ONLINE_OPEN_TITLE));
	if (!od->history) {
		od->hSearch = create_control(hDlg, TEXT("EDIT"), keyword, WS_TABSTOP | ES_AUTOHSCROLL, 0, IDC_SEARCH, od->hFont);
		SendMessage(od->hSearch, EM_SETLIMITTEXT, BUF_SIZE - 1, 0);
		od->hSearchButton = create_control(hDlg, TEXT("BUTTON"), res(IDS_STRING_ONLINE_SEARCH), WS_TABSTOP | BS_OWNERDRAW, 0, IDC_SEARCH_BUTTON, od->hFont);
		od->hChips = create_control(hDlg, ONLINE_CHIPS_WND_CLASS, TEXT(""), WS_TABSTOP, 0, IDC_CHIPS, od->hFont);
		od->hSort = create_control(hDlg, TEXT("COMBOBOX"), TEXT(""), WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, IDC_SORT, od->hFont);

		SendMessage(od->hChips, OCM_ADDCHIP, 0, (LPARAM)res(IDS_STRING_ONLINE_FILTER_ALL));
		SendMessage(od->hChips, OCM_ADDCHIP, 0, (LPARAM)res(IDS_STRING_ONLINE_FILTER_MINE));
		for (i = 0; i < TAG_COUNT; i++) {
			SendMessage(od->hChips, OCM_ADDCHIP, 0, (LPARAM)res(tag_names[i]));
		}
		SendMessage(od->hSort, CB_ADDSTRING, 0, (LPARAM)res(IDS_STRING_ONLINE_SORT_POPULAR));
		SendMessage(od->hSort, CB_ADDSTRING, 0, (LPARAM)res(IDS_STRING_ONLINE_SORT_NEW));
		SendMessage(od->hSort, CB_SETCURSEL, (is_new_sort()) ? 1 : 0, 0);
		EnableWindow(od->hSort, lstrcmp(list_filter, TEXT("mine")) != 0);
	}
	od->hList = create_control(hDlg, ONLINE_LIST_WND_CLASS, TEXT(""), WS_TABSTOP | WS_VSCROLL | WS_BORDER, 0, IDC_LIST, od->hFont);
	SendMessage(od->hList, OLM_SETMORETEXT, 0, (LPARAM)res(IDS_STRING_ONLINE_READ_MORE));
	SendMessage(od->hList, OLM_SETCURRENTTEXT, 0, (LPARAM)res(IDS_STRING_ONLINE_HISTORY_CURRENT));

	// as tall as the screen allows, like the web version
	work_area(GetWindow(hDlg, GW_OWNER), &mi);
	width = Scale(OPEN_WIDTH);
	if (width > (mi.rcWork.right - mi.rcWork.left) * 9 / 10) {
		width = (mi.rcWork.right - mi.rcWork.left) * 9 / 10;
	}
	height = (mi.rcWork.bottom - mi.rcWork.top) * 85 / 100;
	center_window(hDlg, width, height);
	open_layout(hDlg, od);
	if (!od->history) {
		open_show_filter(od);
	}
	open_reload(hDlg, od);
	SetFocus(od->hList);
}

/*
 * open_proc - dialog of the online list (history_proc for the revision history)
 */
static INT_PTR CALLBACK open_proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	OPEN_DATA *od = (OPEN_DATA *)GetWindowLongPtr(hDlg, DWLP_USER);

	switch (msg) {
	case WM_INITDIALOG:
		od = (OPEN_DATA *)lParam;
		SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)od);
		open_init(hDlg, od);
		return FALSE;

	case WM_DESTROY:
		http_cancel(hDlg);
		drop_results(hDlg);
		if (od != NULL && od->hFont != NULL) {
			DeleteObject(od->hFont);
			od->hFont = NULL;
		}
		break;

	case WM_SIZE:
		if (od != NULL && od->hList != NULL) {
			open_layout(hDlg, od);
		}
		break;

	case WM_GETMINMAXINFO:
		((MINMAXINFO *)lParam)->ptMinTrackSize.x = Scale(360);
		((MINMAXINFO *)lParam)->ptMinTrackSize.y = Scale(320);
		break;

	case WM_PAINT:
		if (od != NULL && !od->history) {
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hDlg, &ps);
			HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0x99, 0x99, 0x99));
			HPEN hRetPen = SelectObject(hdc, hPen);
			HBRUSH hRetBrush = SelectObject(hdc, GetStockObject(WHITE_BRUSH));
			int r = od->search_frame.bottom - od->search_frame.top;
			RoundRect(hdc, od->search_frame.left, od->search_frame.top, od->search_frame.right, od->search_frame.bottom, r, r);
			SelectObject(hdc, hRetBrush);
			SelectObject(hdc, hRetPen);
			DeleteObject(hPen);
			EndPaint(hDlg, &ps);
			return TRUE;
		}
		break;

	case WM_CTLCOLORDLG:
	case WM_CTLCOLORSTATIC:
		return white_ctlcolor(msg, wParam);

	case WM_DRAWITEM:
		if (wParam == IDC_SEARCH_BUTTON) {
			draw_search_button((DRAWITEMSTRUCT *)lParam);
			return TRUE;
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			// Enter in the search box
			if (od->hSearch != NULL && (GetFocus() == od->hSearch || GetFocus() == od->hSearchButton)) {
				SendMessage(hDlg, WM_COMMAND, MAKEWPARAM(IDC_SEARCH_BUTTON, BN_CLICKED), (LPARAM)od->hSearchButton);
			}
			break;
		case IDCANCEL:
			end_open(hDlg, od, IDCANCEL);
			break;
		case IDC_SEARCH_BUTTON:
			if (HIWORD(wParam) == BN_CLICKED) {
				GetWindowText(od->hSearch, keyword, BUF_SIZE);
				open_reload(hDlg, od);
			}
			break;
		case IDC_SORT:
			if (HIWORD(wParam) == CBN_SELCHANGE) {
				lstrcpy(list_sort, (SendMessage(od->hSort, CB_GETCURSEL, 0, 0) == 1) ? TEXT("new") : TEXT("popular"));
				open_reload(hDlg, od);
			}
			break;
		}
		break;

	case WM_ONLINE_NOTIFY:
		{
			ONLINE_NOTIFY *on = (ONLINE_NOTIFY *)lParam;
			ONLINE_ITEM *item;
			switch (on->code) {
			case OCN_SELECT:
				open_set_filter(hDlg, od, on->index);
				break;
			case OLN_OPEN:
				open_request_script(hDlg, od, on->index);
				break;
			case OLN_MENU:
				open_show_menu(hDlg, od, on->index, on->pt);
				break;
			case OLN_TAG:
				item = (ONLINE_ITEM *)SendMessage(od->hList, OLM_GETITEM, on->index, 0);
				if (item != NULL && on->tag >= 0 && on->tag < item->tag_count) {
					int i = tag_index(item->tags[on->tag]);
					if (i >= 0) {
						open_set_filter(hDlg, od, i + 2);
					}
				}
				break;
			case OLN_MORE:
				open_request_list(hDlg, od);
				break;
			}
		}
		break;

	case WM_HTTP_RESULT:
		{
			HTTP_RESULT *result = (HTTP_RESULT *)lParam;
			int i;
			if (od->closing) {
				http_free_result(result);
				break;
			}
			switch (result->param) {
			case REQUEST_LIST:
				if (result->id == od->list_id) {
					open_list_result(hDlg, od, result);
				}
				break;
			case REQUEST_SCRIPT:
				if (result->id == od->script_id) {
					open_script_result(hDlg, od, result);
				}
				break;
			case REQUEST_REMOVE:
				for (i = 0; i < MAX_REMOVES; i++) {
					if (od->removes[i].id == result->id) {
						od->removes[i].id = 0;
						open_remove_result(hDlg, od, result, od->removes[i].cid);
						break;
					}
				}
				break;
			}
			http_free_result(result);
		}
		break;
	}
	return FALSE;
}

/*
 * history_proc - dialog of the revision history
 */
static INT_PTR CALLBACK history_proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_INITDIALOG) {
		((OPEN_DATA *)lParam)->history = TRUE;
	}
	return open_proc(hDlg, msg, wParam, lParam);
}

/*
 * save_init - make and place the controls of the save dialog
 */
static void save_init(const HWND hDlg, SAVE_DATA *sd)
{
	static const UINT labels[] = {
		IDS_STRING_ONLINE_FILE, IDS_STRING_ONLINE_AUTHOR, IDS_STRING_ONLINE_PASSWORD, IDS_STRING_ONLINE_MEMO, IDS_STRING_ONLINE_TAGS
	};
	static const int ids[] = {IDC_FILE, IDC_AUTHOR, IDC_PASSWORD, IDC_MEMO, IDC_TAGS};
	HWND hWnd, hSave, hCancel, hNew;
	RECT rect;
	int margin = Scale(DIALOG_MARGIN);
	int width = Scale(SAVE_WIDTH);
	int fh, eh, ch, bw, bh, y, i, tags_width;

	sd->hFont = dialog_font();
	SetWindowText(hDlg, res(IDS_STRING_ONLINE_SAVE_TITLE));
	fh = text_height(hDlg, sd->hFont);
	eh = fh + Scale(8);
	ch = fh + Scale(4);
	bw = Scale(88);
	bh = fh + Scale(14);
	y = margin;
	for (i = 0; i < 5; i++) {
		hWnd = create_control(hDlg, TEXT("STATIC"), res(labels[i]), SS_LEFT, 0, IDC_LABEL + i, sd->hFont);
		MoveWindow(hWnd, margin, y, width, fh, FALSE);
		y += fh + Scale(4);
		if (ids[i] == IDC_TAGS) {
			int j;
			hWnd = create_control(hDlg, TEXT("COMBOBOX"), TEXT(""), WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, IDC_TAGS, sd->hFont);
			tags_width = 0;
			for (j = 0; j < TAG_COUNT; j++) {
				int w = text_width(hDlg, sd->hFont, res(tag_names[j]));
				if (w > tags_width) {
					tags_width = w;
				}
				SendMessage(hWnd, CB_ADDSTRING, 0, (LPARAM)res(tag_names[j]));
			}
			tags_width += GetSystemMetrics(SM_CXVSCROLL) + Scale(16);
			MoveWindow(hWnd, margin, y, tags_width, eh * 10, FALSE);
			GetWindowRect(hWnd, &rect);
			y += rect.bottom - rect.top + Scale(12);
		} else {
			hWnd = create_control(hDlg, TEXT("EDIT"), TEXT(""), WS_TABSTOP | ES_AUTOHSCROLL | ((ids[i] == IDC_PASSWORD) ? ES_PASSWORD : 0),
				WS_EX_CLIENTEDGE, ids[i], sd->hFont);
			SendMessage(hWnd, EM_SETLIMITTEXT, BUF_SIZE - 1, 0);
			MoveWindow(hWnd, margin, y, width * 9 / 10, eh, FALSE);
			y += eh + Scale(12);
		}
	}
	// "save as new file" is offered only for a script that is already online
	hNew = create_control(hDlg, TEXT("BUTTON"), res(IDS_STRING_ONLINE_SAVE_NEW), WS_TABSTOP | BS_AUTOCHECKBOX, 0, IDC_NEW, sd->hFont);
	if (*current.cid != TEXT('\0')) {
		MoveWindow(hNew, margin, y, width, ch, FALSE);
		y += ch + Scale(8);
	} else {
		ShowWindow(hNew, SW_HIDE);
	}
	hWnd = create_control(hDlg, TEXT("BUTTON"), res(IDS_STRING_ONLINE_PRIVATE), WS_TABSTOP | BS_AUTOCHECKBOX, 0, IDC_PRIVATE, sd->hFont);
	MoveWindow(hWnd, margin, y, width, ch, FALSE);
	y += ch + Scale(16);
	hSave = create_control(hDlg, TEXT("BUTTON"), res(IDS_STRING_ONLINE_SAVE_BUTTON), WS_TABSTOP | BS_DEFPUSHBUTTON, 0, IDOK, sd->hFont);
	hCancel = create_control(hDlg, TEXT("BUTTON"), res(IDS_STRING_ONLINE_CANCEL_BUTTON), WS_TABSTOP | BS_PUSHBUTTON, 0, IDCANCEL, sd->hFont);
	MoveWindow(hSave, margin, y, bw, bh, FALSE);
	MoveWindow(hCancel, margin + bw + Scale(8), y, bw, bh, FALSE);
	y += bh + margin;
	set_client_size(hDlg, width + margin * 2, y);

	// the values of the last save
	SetDlgItemText(hDlg, IDC_FILE, (*current.name != TEXT('\0')) ? current.name :
		((sd->default_name != NULL) ? sd->default_name : TEXT("")));
	SetDlgItemText(hDlg, IDC_AUTHOR, author);
	SetDlgItemText(hDlg, IDC_PASSWORD, password);
	i = tag_index(current.tag);
	SendDlgItemMessage(hDlg, IDC_TAGS, CB_SETCURSEL, (i >= 0) ? i : TAG_OTHER, 0);
	CheckDlgButton(hDlg, IDC_PRIVATE, (current.private_mode) ? BST_CHECKED : BST_UNCHECKED);
	SetFocus(GetDlgItem(hDlg, IDC_FILE));
	SendDlgItemMessage(hDlg, IDC_FILE, EM_SETSEL, 0, -1);
}

/*
 * save_request - send the script to the online storage
 */
static void save_request(const HWND hDlg, SAVE_DATA *sd)
{
	JSON_WRITER jw;
	TCHAR *name, *auth, *pass, *memo, *code, *url, *path;
	const TCHAR *tags[1];
	char *body;
	int i;

	name = get_window_text(GetDlgItem(hDlg, IDC_FILE));
	auth = get_window_text(GetDlgItem(hDlg, IDC_AUTHOR));
	pass = get_window_text(GetDlgItem(hDlg, IDC_PASSWORD));
	memo = get_window_text(GetDlgItem(hDlg, IDC_MEMO));
	if (name == NULL || auth == NULL || pass == NULL || memo == NULL) {
		goto end;
	}
	js_trim(name);
	js_trim(auth);
	if (*name == TEXT('\0')) {
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_NAME_NOT_ENTERED), MB_ICONEXCLAMATION);
		goto end;
	}
	if (*auth == TEXT('\0')) {
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_AUTHOR_NOT_ENTERED), MB_ICONEXCLAMATION);
		goto end;
	}
	if (*pass == TEXT('\0')) {
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_PASSWORD_NOT_ENTERED), MB_ICONEXCLAMATION);
		goto end;
	}
	i = (int)SendDlgItemMessage(hDlg, IDC_TAGS, CB_GETCURSEL, 0, 0);
	if (i < 0 || i >= TAG_COUNT) {
		i = TAG_OTHER;
	}
	tags[0] = tag_ids[i];

	// what is kept when the save succeeds
	ZeroMemory(&sd->content, sizeof(CONTENT));
	lstrcpyn(sd->content.cid, current.cid, ONLINE_CID_SIZE);
	lstrcpyn(sd->content.name, name, BUF_SIZE);
	lstrcpyn(sd->content.author, auth, BUF_SIZE);
	lstrcpyn(sd->content.tag, tags[0], ONLINE_TAG_SIZE);
	sd->content.private_mode = (IsDlgButtonChecked(hDlg, IDC_PRIVATE) == BST_CHECKED);
	lstrcpyn(sd->password, pass, BUF_SIZE);
	sd->post = (*current.cid == TEXT('\0') || IsDlgButtonChecked(hDlg, IDC_NEW) == BST_CHECKED);

	if ((code = to_lf(sd->code)) == NULL) {
		goto end;
	}
	json_writer_init(&jw);
	json_begin_object(&jw);
	json_add_string(&jw, TEXT("name"), name);
	json_add_string(&jw, TEXT("type"), (sd->pg05_mode) ? TEXT("PG0_5") : TEXT("PG0"));
	json_add_string(&jw, TEXT("author"), auth);
	json_add_number(&jw, TEXT("password"), password_crc(pass));
	json_add_string(&jw, TEXT("memo"), memo);
	json_add_string(&jw, TEXT("uuid"), uuid);
	json_add_string(&jw, TEXT("code"), code);
	json_add_number(&jw, TEXT("speed"), sd->speed);
	json_add_number(&jw, TEXT("private"), (sd->content.private_mode) ? 1 : 0);
	json_add_string_array(&jw, TEXT("tags"), tags, 1);
	json_end_object(&jw);
	body = json_writer_to_utf8(&jw);
	json_writer_free(&jw);
	mem_free(&code);

	if (sd->post) {
		url = make_url(TEXT("/api/script"));
	} else {
		path = alloc_join(TEXT("/api/script/"), current.cid);
		url = (path != NULL) ? make_url(path) : NULL;
		mem_free(&path);
	}
	sd->save_id = next_request_id();
	if (body != NULL && url != NULL &&
		http_request_async(hDlg, WM_HTTP_RESULT, sd->save_id, (sd->post) ? TEXT("POST") : TEXT("PUT"), url, body, REQUEST_SAVE) == TRUE) {
		// a disabled button cannot keep the focus
		if (GetFocus() == GetDlgItem(hDlg, IDOK)) {
			SetFocus(GetDlgItem(hDlg, IDC_FILE));
		}
		EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
	} else {
		sd->save_id = 0;
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_CONNECTION), MB_ICONEXCLAMATION);
	}
	mem_free(&url);
	mem_free(&body);

end:
	mem_free(&name);
	mem_free(&auth);
	mem_free(&pass);
	mem_free(&memo);
}

/*
 * save_result - keep what was saved, or tell why it could not be saved
 */
static void save_result(const HWND hDlg, SAVE_DATA *sd, const HTTP_RESULT *result)
{
	JSON *js;
	const TCHAR *str;

	sd->save_id = 0;
	EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
	switch (result->status) {
	case 200:
		if (sd->post) {
			// the new cid comes in the response; without it the save is not usable
			*sd->content.cid = TEXT('\0');
			if ((js = json_parse(result->body)) != NULL) {
				if ((str = json_get_string(js, TEXT("cid"))) != NULL) {
					lstrcpyn(sd->content.cid, str, ONLINE_CID_SIZE);
				}
				json_free(js);
			}
			if (*sd->content.cid == TEXT('\0')) {
				message(hDlg, res(IDS_STRING_ONLINE_ERROR_CONNECTION), MB_ICONEXCLAMATION);
				break;
			}
		}
		current = sd->content;
		lstrcpyn(author, sd->content.author, BUF_SIZE);
		lstrcpyn(password, sd->password, BUF_SIZE);
		EndDialog(hDlg, IDOK);
		break;
	case 401:
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_UNAUTHORIZED), MB_ICONEXCLAMATION);
		break;
	case 404:
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_NOT_FOUND), MB_ICONEXCLAMATION);
		break;
	case 409:
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_CONFLICT), MB_ICONEXCLAMATION);
		break;
	case 413:
		js = json_parse(result->body);
		str = json_get_string(js, TEXT("type"));
		if (str != NULL && lstrcmp(str, TEXT("name")) == 0) {
			message(hDlg, res(IDS_STRING_ONLINE_ERROR_NAME_TOO_LONG), MB_ICONEXCLAMATION);
		} else if (str != NULL && lstrcmp(str, TEXT("author")) == 0) {
			message(hDlg, res(IDS_STRING_ONLINE_ERROR_AUTHOR_TOO_LONG), MB_ICONEXCLAMATION);
		} else {
			status_message(hDlg, result);
		}
		json_free(js);
		break;
	case 0:
		message(hDlg, res(IDS_STRING_ONLINE_ERROR_CONNECTION), MB_ICONEXCLAMATION);
		break;
	default:
		status_message(hDlg, result);
		break;
	}
}

/*
 * save_proc - dialog to save to the online storage
 */
static INT_PTR CALLBACK save_proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	SAVE_DATA *sd = (SAVE_DATA *)GetWindowLongPtr(hDlg, DWLP_USER);

	switch (msg) {
	case WM_INITDIALOG:
		sd = (SAVE_DATA *)lParam;
		SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)sd);
		save_init(hDlg, sd);
		return FALSE;

	case WM_DESTROY:
		http_cancel(hDlg);
		drop_results(hDlg);
		if (sd != NULL && sd->hFont != NULL) {
			DeleteObject(sd->hFont);
			sd->hFont = NULL;
		}
		break;

	case WM_CTLCOLORDLG:
	case WM_CTLCOLORSTATIC:
		return white_ctlcolor(msg, wParam);

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			if (sd->save_id == 0) {
				save_request(hDlg, sd);
			}
			break;
		case IDCANCEL:
			EndDialog(hDlg, IDCANCEL);
			break;
		case IDC_PRIVATE:
			if (HIWORD(wParam) == BN_CLICKED && IsDlgButtonChecked(hDlg, IDC_PRIVATE) == BST_CHECKED) {
				message(hDlg, res(IDS_STRING_ONLINE_CONFIRM_PRIVATE), MB_ICONINFORMATION);
			}
			break;
		}
		break;

	case WM_HTTP_RESULT:
		{
			HTTP_RESULT *result = (HTTP_RESULT *)lParam;
			if (result->param == REQUEST_SAVE && result->id == sd->save_id) {
				save_result(hDlg, sd, result);
			}
			http_free_result(result);
		}
		break;
	}
	return FALSE;
}

/*
 * ensure_uuid - identifier of this user (the "mine" list and the owner of saved scripts)
 */
static void ensure_uuid(void)
{
	if (*uuid == TEXT('\0')) {
		create_uuid(uuid);
	}
}

/*
 * online_initialize - register the window classes
 */
BOOL online_initialize(const HINSTANCE hInstance)
{
	int i;

	hInst = hInstance;
	lstrcpy(server, ONLINE_DEFAULT_SERVER);
	lstrcpy(list_sort, TEXT("popular"));
	for (i = 0; i < TAG_COUNT; i++) {
		LoadString(hInst, tag_names[i], tag_labels[i], LABEL_SIZE - 1);
	}
	return (online_view_register(hInstance) && diff_view_register(hInstance));
}

/*
 * online_set_code_view - font and line numbers of the code in the diff, taken from the editor
 */
void online_set_code_view(const LOGFONT *font, const BOOL line_no)
{
	code_font = *font;
	code_font_set = TRUE;
	code_line_no = line_no;
}

/*
 * online_get_ini - read the settings
 */
void online_get_ini(const TCHAR *ini_path)
{
	TCHAR buf[PROTECT_SIZE];
	int len;

	// the server can be changed for testing
	profile_get_string(INI_SECTION, TEXT("server"), ONLINE_DEFAULT_SERVER, server, BUF_SIZE - 1, ini_path);
	for (len = lstrlen(server); len > 0 && server[len - 1] == TEXT('/'); len--) {
		server[len - 1] = TEXT('\0');
	}
	if (*server == TEXT('\0')) {
		lstrcpy(server, ONLINE_DEFAULT_SERVER);
	}
	// #import("cid:...") reads from the same server (also in pg0gen.exe started from here)
	SetEnvironmentVariable(ONLINE_SERVER_ENV, server);
	profile_get_string(INI_SECTION, TEXT("uuid"), TEXT(""), uuid, ONLINE_CID_SIZE - 1, ini_path);
	ensure_uuid();
	profile_get_string(INI_SECTION, TEXT("author"), TEXT(""), author, BUF_SIZE - 1, ini_path);
	profile_get_string(INI_SECTION, TEXT("password"), TEXT(""), buf, PROTECT_SIZE - 1, ini_path);
	if (unprotect_string(buf, password, BUF_SIZE) == FALSE) {
		*password = TEXT('\0');
	}
	profile_get_string(INI_SECTION, TEXT("keyword"), TEXT(""), keyword, BUF_SIZE - 1, ini_path);
	profile_get_string(INI_SECTION, TEXT("list_filter"), TEXT(""), list_filter, ONLINE_TAG_SIZE - 1, ini_path);
	if (lstrcmp(list_filter, TEXT("mine")) != 0 && tag_index(list_filter) < 0) {
		*list_filter = TEXT('\0');
	}
	profile_get_string(INI_SECTION, TEXT("list_sort"), TEXT("popular"), list_sort, ONLINE_TAG_SIZE - 1, ini_path);
	if (!is_new_sort()) {
		lstrcpy(list_sort, TEXT("popular"));
	}
}

/*
 * online_put_ini - write the settings
 */
void online_put_ini(const TCHAR *ini_path)
{
	TCHAR buf[PROTECT_SIZE];

	ensure_uuid();
	profile_write_string(INI_SECTION, TEXT("uuid"), uuid, ini_path);
	profile_write_string(INI_SECTION, TEXT("author"), author, ini_path);
	if (protect_string(password, buf, PROTECT_SIZE) == FALSE) {
		*buf = TEXT('\0');
	}
	profile_write_string(INI_SECTION, TEXT("password"), buf, ini_path);
	profile_write_string(INI_SECTION, TEXT("keyword"), keyword, ini_path);
	profile_write_string(INI_SECTION, TEXT("list_filter"), list_filter, ini_path);
	profile_write_string(INI_SECTION, TEXT("list_sort"), list_sort, ini_path);
}

/*
 * online_clear - the editor no longer shows an online script (new or local file)
 */
void online_clear(void)
{
	ZeroMemory(&current, sizeof(CONTENT));
}

/*
 * online_get_name - name of the online script in the editor (NULL when there is none)
 */
const TCHAR *online_get_name(void)
{
	return (*current.cid != TEXT('\0') && *current.name != TEXT('\0')) ? current.name : NULL;
}

/*
 * online_get_cid - cid of the online script in the editor (NULL when there is none)
 */
const TCHAR *online_get_cid(void)
{
	return (*current.cid != TEXT('\0')) ? current.cid : NULL;
}

/*
 * online_export_cid - pass the cid of the online script in the editor to the script about to run
 *                     (lib/net.pg0 meets the copies of the same script with it; removed when there is none)
 */
void online_export_cid(void)
{
	SetEnvironmentVariable(ONLINE_CID_ENV, online_get_cid());
}

/*
 * finish_open - the script read by a list dialog becomes the one in the editor (only when the dialog ended with it)
 */
static BOOL finish_open(OPEN_DATA *od, const INT_PTR ret)
{
	if (ret == IDOK && od->script->code != NULL) {
		current = od->content;
		return TRUE;
	}
	mem_free(&od->script->code);
	return FALSE;
}

/*
 * show_history - choose a version in the revision history of a script and read it
 */
static BOOL show_history(const HWND hWnd, const TCHAR *cid, ONLINE_SCRIPT *script)
{
	OPEN_DATA od;

	ZeroMemory(&od, sizeof(OPEN_DATA));
	od.script = script;
	lstrcpyn(od.cid, cid, ONLINE_CID_SIZE);
	return finish_open(&od, dialog_box(hWnd, WS_THICKFRAME, history_proc, (LPARAM)&od));
}

/*
 * online_open - choose a script on the online storage and read it
 */
BOOL online_open(const HWND hWnd, ONLINE_SCRIPT *script)
{
	OPEN_DATA od;
	INT_PTR ret;

	ZeroMemory(script, sizeof(ONLINE_SCRIPT));
	ZeroMemory(&od, sizeof(OPEN_DATA));
	od.script = script;
	ensure_uuid();
	ret = dialog_box(hWnd, WS_THICKFRAME, open_proc, (LPARAM)&od);
	if (ret == RESULT_HISTORY) {
		return show_history(hWnd, od.cid, script);
	}
	return finish_open(&od, ret);
}

/*
 * online_history - choose a version in the revision history of the online script in the editor
 */
BOOL online_history(const HWND hWnd, ONLINE_SCRIPT *script)
{
	TCHAR cid[ONLINE_CID_SIZE];

	ZeroMemory(script, sizeof(ONLINE_SCRIPT));
	if (*current.cid == TEXT('\0')) {
		return FALSE;
	}
	ensure_uuid();
	lstrcpy(cid, current.cid);
	return show_history(hWnd, cid, script);
}

/*
 * online_save - save the script to the online storage
 */
BOOL online_save(const HWND hWnd, const TCHAR *code, const TCHAR *default_name, const BOOL pg05_mode, const int speed)
{
	SAVE_DATA sd;

	ZeroMemory(&sd, sizeof(SAVE_DATA));
	sd.code = code;
	sd.default_name = default_name;
	sd.pg05_mode = pg05_mode;
	sd.speed = speed;
	ensure_uuid();
	return (dialog_box(hWnd, 0, save_proc, (LPARAM)&sd) == IDOK);
}

/*
 * online_show_diff - show the changes between two texts in the dialog of the revision history
 */
void online_show_diff(const HWND hWnd, const TCHAR *title, const TCHAR *old_code, const TCHAR *new_code)
{
	DIFF_DATA dd;

	ZeroMemory(&dd, sizeof(DIFF_DATA));
	dd.title = title;
	dd.prev_skip = -1;
	dd.old_code = alloc_copy(old_code);
	dd.new_code = alloc_copy(new_code);
	if (dd.old_code != NULL && dd.new_code != NULL) {
		dialog_box(hWnd, WS_THICKFRAME | WS_MAXIMIZEBOX, diff_proc, (LPARAM)&dd);
	}
	mem_free(&dd.old_code);
	mem_free(&dd.new_code);
}
/* End of source */
