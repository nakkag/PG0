/*
 * PG0 library
 *
 * io.c
 *
 * Input/output functions (io.pg0), the same API as the web version's io.js.
 * Saved values are kept in %LOCALAPPDATA%\pg0\values\<script>.dat.
 */

/* Include Files */
#include <windows.h>
#include <tchar.h>
#include <math.h>
#include <float.h>

#include "lib_common.h"

#pragma comment(lib, "winmm.lib")

/* Define */
#define VALUE_DIR				TEXT("values")
#define VALUE_EXT				TEXT(".dat")
#define COMMON_ID				TEXT("common")
#define NAME_ID					TEXT("name_")

/* Struct */
typedef struct _RECORD {
	TCHAR *key;
	TCHAR *value;
	struct _RECORD *next;
} RECORD;

#define MAX_DEPTH				64
#define MAX_DIGITS				9
#define SHARE_RETRY				20
#define SHARE_WAIT				25

/* Global Variables */
// the store could not be read because another instance holds it (saving must not overwrite it)
static BOOL store_busy = FALSE;

/* Local Function Prototypes */

#ifndef PG0_STATIC_LIB
/*
 * DllMain
 */
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(hinst);
	}
	return TRUE;
}
#endif

/*
 * console_host - the program is a console program (its stdout adds the CR itself)
 */
static BOOL console_host(void)
{
	static int console = -1;

	if (console < 0) {
		IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)GetModuleHandle(NULL);
		IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)dos + dos->e_lfanew);
		console = (nt->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI);
	}
	return console;
}

/*
 * _lib_func_println - print a line
 */
int SFUNC _lib_func_println(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR *str, *line;

	if (param == NULL) {
		return -2;
	}
	str = lib_to_display_string(param);
	if (str == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	line = alloc_join(str, console_host() ? TEXT("\n") : TEXT("\r\n"));
	mem_free(&str);
	if (line == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	lib_print(ei, line);
	mem_free(&line);
	return 0;
}

/*
 * _lib_func_wait - pause the program (the stop button interrupts it)
 */
int SFUNC _lib_func_wait(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	double time;

	if (param == NULL) {
		return -2;
	}
	time = floor(lib_to_float(param));
	if (_isnan(time) || time <= 0) {
		return 0;
	}
	// a 1ms timer only while waiting
	timeBeginPeriod(1);
	lib_sleep(ei, time);
	timeEndPeriod(1);
	return 0;
}

/*
 * get_io_id - identifier of the value store for the running script
 */
static void get_io_id(EXECINFO *ei, TCHAR *id, int size)
{
	SCRIPTINFO *sci = NULL;
	TCHAR *p;

	if (ei != NULL && ei->sci != NULL) {
		sci = (ei->sci->sci_top != NULL) ? ei->sci->sci_top : ei->sci;
	}
	if (sci == NULL || sci->name == NULL || *sci->name == TEXT('\0')) {
		lstrcpyn(id, COMMON_ID, size);
		return;
	}
	lstrcpyn(id, NAME_ID, size);
	lstrcpyn(id + lstrlen(id), sci->name, size - lstrlen(id));
	for (p = id; *p != TEXT('\0'); p++) {
		if (*p == TEXT('\\') || *p == TEXT('/') || *p == TEXT(':') || *p == TEXT('*') ||
			*p == TEXT('?') || *p == TEXT('"') || *p == TEXT('<') || *p == TEXT('>') || *p == TEXT('|')) {
			*p = TEXT('_');
		}
	}
}

/*
 * get_store_path - file of the value store
 */
static BOOL get_store_path(EXECINFO *ei, TCHAR *path)
{
	TCHAR id[MAX_PATH];

	if (!lib_get_app_data_dir(path, MAX_PATH + 1)) {
		return FALSE;
	}
	lstrcat(path, TEXT("\\"));
	lstrcat(path, VALUE_DIR);
	CreateDirectory(path, NULL);
	get_io_id(ei, id, MAX_PATH - lstrlen(path) - lstrlen(VALUE_EXT) - 2);
	lstrcat(path, TEXT("\\"));
	lstrcat(path, id);
	lstrcat(path, VALUE_EXT);
	return TRUE;
}

/*
 * grow_buf - make room for more characters
 */
static BOOL grow_buf(TCHAR **buf, int *size, int need)
{
	TCHAR *nb;

	if (*buf != NULL && need < *size) {
		return TRUE;
	}
	*size = (need + 256) * 2;
	if (*buf == NULL) {
		nb = mem_alloc(sizeof(TCHAR) * (*size));
	} else {
		nb = mem_realloc(*buf, sizeof(TCHAR) * (*size));
	}
	if (nb == NULL) {
		return FALSE;
	}
	*buf = nb;
	return TRUE;
}

/*
 * escape_append - append a string, escaping the record separators
 */
static void escape_append(TCHAR **buf, int *len, int *size, const TCHAR *str)
{
	const TCHAR *p;
	TCHAR tmp[3];

	if (!grow_buf(buf, size, *len + 1)) {
		return;
	}
	(*buf)[*len] = TEXT('\0');
	for (p = str; *p != TEXT('\0'); p++) {
		tmp[0] = *p;
		tmp[1] = TEXT('\0');
		switch (*p) {
		case TEXT('\\'):
			lstrcpy(tmp, TEXT("\\\\"));
			break;
		case TEXT('\t'):
			lstrcpy(tmp, TEXT("\\t"));
			break;
		case TEXT('\r'):
			lstrcpy(tmp, TEXT("\\r"));
			break;
		case TEXT('\n'):
			lstrcpy(tmp, TEXT("\\n"));
			break;
		}
		if (!grow_buf(buf, size, *len + 3)) {
			return;
		}
		lstrcpy(*buf + *len, tmp);
		*len += lstrlen(tmp);
	}
}

/*
 * unescape - decode an escaped field in place
 */
static void unescape(TCHAR *str)
{
	TCHAR *p, *r;

	for (p = r = str; *p != TEXT('\0'); p++, r++) {
		if (*p == TEXT('\\') && *(p + 1) != TEXT('\0')) {
			p++;
			switch (*p) {
			case TEXT('t'):
				*r = TEXT('\t');
				break;
			case TEXT('r'):
				*r = TEXT('\r');
				break;
			case TEXT('n'):
				*r = TEXT('\n');
				break;
			default:
				*r = *p;
				break;
			}
		} else {
			*r = *p;
		}
	}
	*r = TEXT('\0');
}

/*
 * serialize - value to text
 *   i<int>;  f<float>;  s<len>:<chars>  a<count>:<elements>
 *   an element is an optional n<len>:<name> followed by a value
 */
static void serialize(VALUEINFO *vi, TCHAR **buf, int *len, int *size);

static void append_text(TCHAR **buf, int *len, int *size, const TCHAR *str)
{
	int l = lstrlen(str);
	if (!grow_buf(buf, size, *len + l + 1)) {
		return;
	}
	lstrcpy(*buf + *len, str);
	*len += l;
}

static void serialize_list(VALUEINFO *list, TCHAR **buf, int *len, int *size)
{
	VALUEINFO *vi;
	TCHAR tmp[64];
	int cnt = 0;

	for (vi = list; vi != NULL; vi = vi->next) {
		cnt++;
	}
	wsprintf(tmp, TEXT("a%d:"), cnt);
	append_text(buf, len, size, tmp);
	for (vi = list; vi != NULL; vi = vi->next) {
		const TCHAR *name = (vi->org_name != NULL) ? vi->org_name : vi->name;
		if (name != NULL && *name != TEXT('\0')) {
			wsprintf(tmp, TEXT("n%d:"), lstrlen(name));
			append_text(buf, len, size, tmp);
			append_text(buf, len, size, name);
		}
		serialize(vi, buf, len, size);
	}
}

static void serialize(VALUEINFO *vi, TCHAR **buf, int *len, int *size)
{
	TCHAR tmp[FLOAT_LENGTH + 4];

	if (vi == NULL || vi->v == NULL) {
		append_text(buf, len, size, TEXT("i0;"));
		return;
	}
	switch (vi->v->type) {
	case TYPE_ARRAY:
		serialize_list(vi->v->u.array, buf, len, size);
		break;
	case TYPE_STRING:
		wsprintf(tmp, TEXT("s%d:"), lstrlen(vi->v->u.sValue));
		append_text(buf, len, size, tmp);
		append_text(buf, len, size, vi->v->u.sValue);
		break;
	case TYPE_FLOAT:
		_stprintf_s(tmp, FLOAT_LENGTH + 4, TEXT("f%.17g;"), vi->v->u.fValue);
		append_text(buf, len, size, tmp);
		break;
	default:
		wsprintf(tmp, TEXT("i%d;"), vi->v->u.iValue);
		append_text(buf, len, size, tmp);
		break;
	}
}

/*
 * deserialize - text to value (returns the position after the value)
 */
static const TCHAR *deserialize(const TCHAR *p, VALUEINFO *vi, const int depth);

/*
 * read_length - "<digits>:" of a string or array (-1 when it is not a valid length)
 */
static const TCHAR *read_length(const TCHAR *p, int *len)
{
	int digits = 0;

	*len = 0;
	while (*p >= TEXT('0') && *p <= TEXT('9')) {
		if (++digits > MAX_DIGITS) {
			*len = -1;
		} else if (*len >= 0) {
			*len = *len * 10 + (*p - TEXT('0'));
		}
		p++;
	}
	if (digits == 0 || *p != TEXT(':')) {
		*len = -1;
		return p;
	}
	return p + 1;
}

static const TCHAR *deserialize(const TCHAR *p, VALUEINFO *vi, const int depth)
{
	VALUEINFO *top = NULL, *last = NULL, *e;
	TCHAR *tmp;
	int len, cnt, i;

	if (p == NULL || depth > MAX_DEPTH) {
		return NULL;
	}
	switch (*p) {
	case TEXT('i'):
		lib_set_int(vi, _ttoi(p + 1));
		for (p++; *p != TEXT('\0') && *p != TEXT(';'); p++);
		return (*p == TEXT(';')) ? p + 1 : p;

	case TEXT('f'):
		lib_set_float(vi, _tcstod(p + 1, NULL));
		for (p++; *p != TEXT('\0') && *p != TEXT(';'); p++);
		return (*p == TEXT(';')) ? p + 1 : p;

	case TEXT('s'):
		// a corrupt file must not make a length beyond the text
		p = read_length(p + 1, &len);
		if (len < 0 || lstrlen(p) < len) {
			return NULL;
		}
		tmp = alloc_copy_n((TCHAR *)p, len);
		if (tmp == NULL) {
			return NULL;
		}
		vi->v->u.sValue = tmp;
		vi->v->type = TYPE_STRING;
		return p + len;

	case TEXT('a'):
		p = read_length(p + 1, &cnt);
		if (cnt < 0) {
			return NULL;
		}
		for (i = 0; i < cnt && p != NULL && *p != TEXT('\0'); i++) {
			e = AllocValue();
			if (e == NULL) {
				break;
			}
			if (*p == TEXT('n')) {
				p = read_length(p + 1, &len);
				if (len < 0 || lstrlen(p) < len) {
					FreeValueList(e);
					p = NULL;
					break;
				}
				tmp = alloc_copy_n((TCHAR *)p, len);
				if (tmp != NULL) {
					lib_set_name(e, tmp);
					mem_free(&tmp);
				}
				p += len;
			}
			p = deserialize(p, e, depth + 1);
			lib_list_append(&top, &last, e);
		}
		lib_set_array(vi, top);
		return p;
	}
	return NULL;
}

/*
 * load_records - read the value store
 */
static RECORD *load_records(const TCHAR *path)
{
	RECORD *top = NULL, *last = NULL, *rec;
	HANDLE hFile;
	DWORD size, read;
	BYTE *raw;
	TCHAR *buf, *p, *r, *tab;

	int retry;

	// another instance may be replacing the file at this moment
	store_busy = FALSE;
	for (retry = 0; ; retry++) {
		hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION || retry >= SHARE_RETRY) {
			break;
		}
		Sleep(SHARE_WAIT);
	}
	if (hFile == INVALID_HANDLE_VALUE) {
		store_busy = (GetLastError() == ERROR_SHARING_VIOLATION);
		return NULL;
	}
	size = GetFileSize(hFile, NULL);
	if (size == 0xFFFFFFFF || size < 2) {
		CloseHandle(hFile);
		return NULL;
	}
	// whole characters only
	size &= ~(DWORD)1;
	raw = mem_alloc(size + sizeof(TCHAR));
	if (raw == NULL) {
		CloseHandle(hFile);
		return NULL;
	}
	if (!ReadFile(hFile, raw, size, &read, NULL)) {
		read = 0;
	}
	CloseHandle(hFile);
	read &= ~(DWORD)1;
	*(TCHAR *)(raw + read) = TEXT('\0');
	buf = (TCHAR *)raw;
	if (*buf == 0xFEFF) {
		buf++;
	}
	for (p = buf; *p != TEXT('\0'); p = r) {
		for (r = p; *r != TEXT('\0') && *r != TEXT('\n'); r++);
		if (*r == TEXT('\n')) {
			*r = TEXT('\0');
			r++;
		}
		for (tab = p; *tab != TEXT('\0') && *tab != TEXT('\t'); tab++);
		if (*tab != TEXT('\t')) {
			continue;
		}
		*tab = TEXT('\0');
		rec = mem_calloc(sizeof(RECORD));
		if (rec == NULL) {
			break;
		}
		unescape(p);
		unescape(tab + 1);
		rec->key = alloc_copy(p);
		rec->value = alloc_copy(tab + 1);
		if (top == NULL) {
			top = rec;
		} else {
			last->next = rec;
		}
		last = rec;
	}
	mem_free(&raw);
	return top;
}

/*
 * save_records - write the value store (a temporary file replaces the old one, so a crash
 *                or a second instance never leaves it half written)
 */
static BOOL save_records(const TCHAR *path, RECORD *top)
{
	RECORD *rec;
	HANDLE hFile;
	DWORD written;
	TCHAR *buf = NULL;
	TCHAR tmp_path[MAX_PATH + 8];
	int len = 0, size = 0, retry;
	TCHAR bom = 0xFEFF;
	BOOL ok;

	if (top == NULL) {
		return (DeleteFile(path) || GetLastError() == ERROR_FILE_NOT_FOUND);
	}
	for (rec = top; rec != NULL; rec = rec->next) {
		escape_append(&buf, &len, &size, rec->key);
		append_text(&buf, &len, &size, TEXT("\t"));
		escape_append(&buf, &len, &size, rec->value);
		append_text(&buf, &len, &size, TEXT("\n"));
	}
	if (buf == NULL) {
		return FALSE;
	}
	wsprintf(tmp_path, TEXT("%s.tmp"), path);
	hFile = CreateFile(tmp_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		mem_free(&buf);
		return FALSE;
	}
	ok = WriteFile(hFile, &bom, sizeof(TCHAR), &written, NULL) && written == sizeof(TCHAR) &&
		WriteFile(hFile, buf, sizeof(TCHAR) * len, &written, NULL) && written == sizeof(TCHAR) * len;
	CloseHandle(hFile);
	mem_free(&buf);
	// a reader of the old file makes the replacement fail for a moment
	for (retry = 0; ok; retry++) {
		if (MoveFileEx(tmp_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			return TRUE;
		}
		if (GetLastError() != ERROR_SHARING_VIOLATION || retry >= SHARE_RETRY) {
			break;
		}
		Sleep(SHARE_WAIT);
	}
	DeleteFile(tmp_path);
	return FALSE;
}

/*
 * lock_store - one instance at a time reads, changes and writes a store
 *              (two scripts with the same name can run at the same time)
 */
static HANDLE lock_store(const TCHAR *path)
{
	TCHAR name[MAX_PATH + 16];
	TCHAR *p;
	HANDLE hMutex;

	wsprintf(name, TEXT("Local\\pg0_values_%s"), path);
	for (p = name + 6; *p != TEXT('\0'); p++) {
		if (*p == TEXT('\\') || *p == TEXT('/')) {
			*p = TEXT('_');
		}
	}
	CharLowerBuff(name + 6, lstrlen(name + 6));
	hMutex = CreateMutex(NULL, FALSE, name);
	if (hMutex == NULL) {
		return NULL;
	}
	if (WaitForSingleObject(hMutex, 5000) == WAIT_FAILED) {
		CloseHandle(hMutex);
		return NULL;
	}
	return hMutex;
}

/*
 * unlock_store
 */
static void unlock_store(HANDLE hMutex)
{
	if (hMutex != NULL) {
		ReleaseMutex(hMutex);
		CloseHandle(hMutex);
	}
}

/*
 * free_records
 */
static void free_records(RECORD *top)
{
	RECORD *next;
	while (top != NULL) {
		next = top->next;
		mem_free(&top->key);
		mem_free(&top->value);
		mem_free(&top);
		top = next;
	}
}

/*
 * find_record
 */
static RECORD *find_record(RECORD *top, const TCHAR *key)
{
	// exact comparison like the web version (lstrcmp ignores some characters)
	for (; top != NULL; top = top->next) {
		if (_tcscmp(top->key, key) == 0) {
			return top;
		}
	}
	return NULL;
}

/*
 * remove_record
 */
static RECORD *remove_record(RECORD *top, const TCHAR *key)
{
	RECORD *rec, *prev = NULL;

	for (rec = top; rec != NULL; prev = rec, rec = rec->next) {
		if (_tcscmp(rec->key, key) == 0) {
			if (prev == NULL) {
				top = rec->next;
			} else {
				prev->next = rec->next;
			}
			rec->next = NULL;
			free_records(rec);
			break;
		}
	}
	return top;
}

/*
 * _lib_func_savevalue - save a value
 */
int SFUNC _lib_func_savevalue(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	RECORD *top, *rec;
	HANDLE hLock;
	TCHAR path[MAX_PATH + 1];
	TCHAR *key, *value = NULL;
	int len = 0, size = 0;
	BOOL saved;

	if (lib_param_count(param) < 2) {
		return -2;
	}
	if (!get_store_path(ei, path)) {
		lstrcpy(ErrStr, TEXT("Storage error"));
		return -1;
	}
	key = lib_to_display_string(param);
	serialize(param->next, &value, &len, &size);
	if (key == NULL || value == NULL) {
		mem_free(&key);
		mem_free(&value);
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	hLock = lock_store(path);
	top = load_records(path);
	if (store_busy) {
		unlock_store(hLock);
		mem_free(&key);
		mem_free(&value);
		lstrcpy(ErrStr, TEXT("Storage error"));
		return -1;
	}
	rec = find_record(top, key);
	if (rec != NULL) {
		mem_free(&rec->value);
		rec->value = value;
		mem_free(&key);
	} else {
		rec = mem_calloc(sizeof(RECORD));
		if (rec == NULL) {
			unlock_store(hLock);
			free_records(top);
			mem_free(&key);
			mem_free(&value);
			lstrcpy(ErrStr, LIB_ERR_ALLOC);
			return -1;
		}
		rec->key = key;
		rec->value = value;
		rec->next = top;
		top = rec;
	}
	saved = save_records(path, top);
	unlock_store(hLock);
	free_records(top);
	if (!saved) {
		lstrcpy(ErrStr, TEXT("Storage error"));
		return -1;
	}
	return 0;
}

/*
 * _lib_func_loadvalue - load a saved value
 */
int SFUNC _lib_func_loadvalue(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	RECORD *top, *rec;
	TCHAR path[MAX_PATH + 1];
	TCHAR *key;

	if (param == NULL) {
		return -2;
	}
	if (!get_store_path(ei, path)) {
		return 0;
	}
	key = lib_to_display_string(param);
	if (key == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	top = load_records(path);
	rec = find_record(top, key);
	if (rec != NULL) {
		deserialize(rec->value, ret, 0);
	}
	free_records(top);
	mem_free(&key);
	return 0;
}

/*
 * _lib_func_removevalue - remove a saved value
 */
int SFUNC _lib_func_removevalue(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	RECORD *top;
	HANDLE hLock;
	TCHAR path[MAX_PATH + 1];
	TCHAR *key;
	BOOL saved;

	if (param == NULL) {
		return -2;
	}
	if (!get_store_path(ei, path)) {
		return 0;
	}
	key = lib_to_display_string(param);
	if (key == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	hLock = lock_store(path);
	top = load_records(path);
	if (store_busy) {
		unlock_store(hLock);
		mem_free(&key);
		lstrcpy(ErrStr, TEXT("Storage error"));
		return -1;
	}
	top = remove_record(top, key);
	saved = save_records(path, top);
	unlock_store(hLock);
	free_records(top);
	mem_free(&key);
	if (!saved) {
		lstrcpy(ErrStr, TEXT("Storage error"));
		return -1;
	}
	return 0;
}

/*
 * _lib_func_get_clipboard - text of the clipboard
 */
int SFUNC _lib_func_get_clipboard(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	HANDLE hMem;
	TCHAR *p;

	lib_set_string(ret, TEXT(""));
	if (!OpenClipboard(NULL)) {
		return 0;
	}
	hMem = GetClipboardData(CF_UNICODETEXT);
	if (hMem != NULL) {
		p = GlobalLock(hMem);
		if (p != NULL) {
			// another program may have put text without a terminator
			SIZE_T max = GlobalSize(hMem) / sizeof(TCHAR);
			SIZE_T len;
			TCHAR *tmp;
			for (len = 0; len < max && p[len] != TEXT('\0'); len++);
			if ((tmp = alloc_copy_n(p, (int)len)) != NULL) {
				mem_free(&ret->v->u.sValue);
				lib_set_string(ret, tmp);
				mem_free(&tmp);
			}
			GlobalUnlock(hMem);
		}
	}
	CloseClipboard();
	return 0;
}

/*
 * _lib_func_set_clipboard - put text on the clipboard
 */
int SFUNC _lib_func_set_clipboard(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	HANDLE hMem;
	TCHAR *str, *p;
	int len;

	if (param == NULL) {
		return -2;
	}
	lib_set_int(ret, 0);
	str = lib_to_string(param);
	if (str == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	len = lstrlen(str) + 1;
	hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(TCHAR) * len);
	if (hMem == NULL) {
		mem_free(&str);
		return 0;
	}
	p = GlobalLock(hMem);
	if (p == NULL) {
		GlobalFree(hMem);
		mem_free(&str);
		return 0;
	}
	lstrcpy(p, str);
	GlobalUnlock(hMem);
	mem_free(&str);
	if (!OpenClipboard(NULL)) {
		GlobalFree(hMem);
		return 0;
	}
	EmptyClipboard();
	if (SetClipboardData(CF_UNICODETEXT, hMem) != NULL) {
		lib_set_int(ret, 1);
	} else {
		GlobalFree(hMem);
	}
	CloseClipboard();
	return 0;
}
/* End of source */
