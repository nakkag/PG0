/*
 * PG0
 *
 * script_read.c
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#include <windows.h>
#include <windowsx.h>
#include <tchar.h>

#include "script.h"
#include "script_string.h"
#include "script_memory.h"
#include "script_utility.h"
#include "http.h"

/* Define */
#define PREP_IMPORT				TEXT("import")
#define PREP_LIBRARY			TEXT("library")
#define PREP_OPTION				TEXT("option")

// #import("cid:<cid>") imports a script stored online
#define IMPORT_CID				TEXT("cid:")
#define IMPORT_CID_SIZE			64
#define IMPORT_URL_PATH			TEXT("/api/script/import/")

#define IS_SPACE(c)				(c == TEXT(' ') || c == TEXT('\t') || c == TEXT('\r') || c == TEXT('\n'))
#define IS_CID_CHAR(c)			((c >= TEXT('0') && c <= TEXT('9')) || (c >= TEXT('A') && c <= TEXT('Z')) || \
								(c >= TEXT('a') && c <= TEXT('z')) || c == TEXT('-'))

// result of reading a script for an #import
#define IMPORT_OK				0
#define IMPORT_NOT_FOUND		1
#define IMPORT_ERROR			2
#define IMPORT_CIRCULAR			3

/* Global Variables */

/* Local Function Prototypes */
static TCHAR *Utf8ToText(BYTE *buf);
static BOOL GetScriptFullPath(TCHAR *path, TCHAR *name, TCHAR *full);
static BOOL IsSameScriptFile(SCRIPTINFO *csci, TCHAR *full);
static int ExecImportedScript(SCRIPTINFO *csci);
static int ReadScriptFiles(SCRIPTINFO *sci, TCHAR *path, TCHAR *name);
static TCHAR *DownloadScript(TCHAR *cid);
static int ReadScriptCid(SCRIPTINFO *sci, TCHAR *cid);
static TCHAR *PreprocessorLine(SCRIPTINFO *sci, TCHAR *path, TCHAR *p);
static BOOL IsEmptyScript(TCHAR *buf);
static void GetModuleDir(TCHAR *dir);
static BOOL IsLibraryName(TCHAR *name);
static HANDLE LoadLibraryPath(TCHAR *dir, TCHAR *FileName);
static BOOL LoadLibraryFile(SCRIPTINFO *sci, TCHAR *path, TCHAR *FileName);

/*
 * GetFilePathName - パスからファイル名とディレクトリパスを取得
 */
void GetFilePathName(TCHAR *path, TCHAR *dir, TCHAR *name)
{
	TCHAR *p, *r;

	if (*path == TEXT('\0')) {
		*dir = TEXT('\0');
		*name = TEXT('\0');
		return;
	}

	for (p = r = path; *p != TEXT('\0'); p++) {
#ifdef UNICODE
		if (*p == TEXT('\\') || *p == TEXT('/')) {
			r = p;
		}
#else
		if (IsDBCSLeadByte(*p) == TRUE && *(p + 1) != '\0') {
			p++;
		} else if (*p == '\\' || *p == '/') {
			r = p;
		}
#endif
	}
	if (r != path) {
		lstrcpy(name, r + 1);
		str_cpy_n(dir, path, (int)(r - path));
	} else {
		lstrcpy(name, path);
		GetCurrentDirectory(MAX_PATH + 1, dir);
	}
	if (*(dir + lstrlen(dir)) != TEXT('\\')) {
		lstrcat(dir, TEXT("\\"));
	}
}

/*
 * read_file - ファイルの読み込み
 */
TCHAR *read_file(TCHAR *path)
{
	HANDLE hFile;
	TCHAR *text;
	BYTE *buf;
	DWORD fSizeLow, fSizeHigh;
	DWORD ret;
	DWORD errcode;

	// ファイルを開く
	hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == NULL || hFile == (HANDLE)-1) {
		return NULL;
	}
	if ((fSizeLow = GetFileSize(hFile, &fSizeHigh)) == 0xFFFFFFFF) {
		errcode = GetLastError();
		CloseHandle(hFile);
		SetLastError(errcode);
		return NULL;
	}
	if ((buf = (BYTE *)mem_alloc(fSizeLow + 1)) == NULL) {
		errcode = GetLastError();
		CloseHandle(hFile);
		SetLastError(errcode);
		return NULL;
	}
	// ファイルを読み込む
	if (ReadFile(hFile, buf, fSizeLow, &ret, NULL) == FALSE) {
		errcode = GetLastError();
		mem_free(&buf);
		CloseHandle(hFile);
		SetLastError(errcode);
		return NULL;
	}
	*(buf + ret) = '\0';
	CloseHandle(hFile);
	text = Utf8ToText(buf);
	errcode = GetLastError();
	mem_free(&buf);
	SetLastError(errcode);
	return text;
}

/*
 * Utf8ToText - UTF-8 text (with or without a BOM) to the text of a script
 */
static TCHAR *Utf8ToText(BYTE *buf)
{
	WCHAR *wbuf;
#ifndef UNICODE
	BYTE *abuf;
	DWORD errcode;
#endif
	int len;
	int bom = 0;

	// BOMをスキップ
	if (*buf == 0xEF && *(buf + 1) == 0xBB && *(buf + 2) == 0xBF) {
		bom = 3;
	}
	// UTF-8からUTF-16に変換
	len = MultiByteToWideChar(CP_UTF8, 0, buf + bom, -1, NULL, 0);
	if ((wbuf = (WCHAR *)mem_alloc(sizeof(WCHAR) * len)) == NULL) {
		return NULL;
	}
	MultiByteToWideChar(CP_UTF8, 0, buf + bom, -1, wbuf, len);

#ifdef UNICODE
	return wbuf;
#else
	// UTF-16からASCIIに変換
	len = WideCharToMultiByte(CP_ACP, 0, wbuf, -1, NULL, 0, NULL, NULL);
	if ((abuf = (BYTE *)mem_alloc(sizeof(BYTE) * len)) == NULL) {
		errcode = GetLastError();
		mem_free(&wbuf);
		SetLastError(errcode);
		return NULL;
	}
	WideCharToMultiByte(CP_ACP, 0, wbuf, -1, abuf, len, NULL, NULL);
	mem_free(&wbuf);
	return abuf;
#endif
}

/*
 * ReadScriptFile - スクリプトファイルを読み込む
 */
SCRIPTINFO *ReadScriptFile(SCRIPTINFO *sci, TCHAR *path, TCHAR *name)
{
	SCRIPTINFO *csci = sci;
	EXECINFO ei;
	TCHAR fpath[MAX_PATH + 1];
	TCHAR full[MAX_PATH + 1];
	TCHAR *p;

	// the same file is read once, however its path is written
	if (GetScriptFullPath(path, name, full) == TRUE) {
		for (p = full + lstrlen(full); p > full && *(p - 1) != TEXT('\\'); p--);
		str_cpy_n(fpath, full, (int)(p - full));
		name = p;
	} else {
		*full = TEXT('\0');
		lstrcpy(fpath, path);
		if (*path != TEXT('\0') && *(path + lstrlen(path) - 1) != TEXT('\\')) {
			lstrcat(fpath, TEXT("\\"));
		}
	}
	if (csci->buf != NULL) {
		//リストの作成
		for (;; csci = csci->next) {
			if (*full != TEXT('\0') && IsSameScriptFile(csci, full) == TRUE) {
				return csci;
			}
			if (csci->next == NULL) {
				break;
			}
		}
		csci = csci->next = mem_calloc(sizeof(SCRIPTINFO));
		if (csci == NULL) {
			ZeroMemory(&ei, sizeof(EXECINFO));
			ei.sci = sci;
			Error(&ei, ERR_ALLOC, name, NULL);
			return NULL;
		}
	}
	csci->name = alloc_copy(name);
	csci->path = alloc_copy(fpath);
	csci->buf = NULL;
	csci->next = NULL;
	csci->callback = NULL;
	csci->sci_top = sci->sci_top;
	csci->strict_val_op = sci->strict_val_op;
	csci->strict_val = sci->strict_val_op;
	csci->extension = sci->extension;

	//ファイルの読み込み
	lstrcat(fpath, name);
	csci->buf = read_file(fpath);
	if (csci->buf == NULL) {
		ZeroMemory(&ei, sizeof(EXECINFO));
		ei.sci = csci;
		Error(&ei, ERR_FILEOPEN, fpath, NULL);
		return NULL;
	}
	//構文解析
	ZeroMemory(&ei, sizeof(EXECINFO));
	ei.name = csci->name;
	ei.sci = csci;
	csci->importing = TRUE;
	csci->tk = ParseSentence(&ei, csci->buf, 0);
	csci->importing = FALSE;
	return csci;
}

/*
 * GetScriptFullPath - full path of a script file (the same for every way of writing the path)
 */
static BOOL GetScriptFullPath(TCHAR *path, TCHAR *name, TCHAR *full)
{
	TCHAR buf[MAX_PATH + 1];
	TCHAR *p;
	DWORD len;

	if (name == NULL || *name == TEXT('\0') || lstrlen(path) + lstrlen(name) + 1 >= MAX_PATH) {
		return FALSE;
	}
	p = str_cpy(buf, path);
	if (p > buf && *(p - 1) != TEXT('\\') && *(p - 1) != TEXT('/')) {
		p = str_cpy(p, TEXT("\\"));
	}
	str_cpy(p, name);
	len = GetFullPathName(buf, MAX_PATH + 1, full, NULL);
	return (len > 0 && len <= MAX_PATH);
}

/*
 * IsSameScriptFile - the script was read from the file (full path)
 */
static BOOL IsSameScriptFile(SCRIPTINFO *csci, TCHAR *full)
{
	TCHAR buf[MAX_PATH + 1];

	return (csci->path != NULL && GetScriptFullPath(csci->path, csci->name, buf) == TRUE &&
		str_cmp_i(buf, full) == 0);
}

/*
 * ExecImportedScript - run a script read for an #import (a script imported before is not run again)
 */
static int ExecImportedScript(SCRIPTINFO *csci)
{
	VALUEINFO *rvi = NULL;

	// the scripts are imported while the main script is parsed, so the main script is always being read
	if (csci->importing == TRUE || csci == csci->sci_top) {
		return IMPORT_CIRCULAR;
	}
	if (csci->buf == NULL || csci->prep_error == TRUE || (csci->tk == NULL && IsEmptyScript(csci->buf) == FALSE)) {
		return IMPORT_ERROR;
	}
	if (csci->tk != NULL && csci->ei == NULL && ExecScript(csci, NULL, &rvi) == -1) {
		FreeValueList(rvi);
		return IMPORT_ERROR;
	}
	FreeValueList(rvi);
	return IMPORT_OK;
}

/*
 * IsEmptyScript - the script has no statements (only preprocessor lines, comments and blanks)
 */
static BOOL IsEmptyScript(TCHAR *buf)
{
	TCHAR *p;

	if (buf == NULL) {
		return TRUE;
	}
	for (p = buf; *p != TEXT('\0');) {
		while (IS_SPACE(*p)) {
			p++;
		}
		if (*p == TEXT('\0')) {
			break;
		}
		if (*p != TEXT('#') && !(*p == TEXT('/') && *(p + 1) == TEXT('/'))) {
			return FALSE;
		}
		for (; *p != TEXT('\0') && *p != TEXT('\n'); p++);
	}
	return TRUE;
}

/*
 * GetModuleDir - directory of the running program (with a trailing backslash)
 */
static void GetModuleDir(TCHAR *dir)
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
	if (r == dir) {
		*dir = TEXT('\0');
	} else {
		*(r + 1) = TEXT('\0');
	}
}

/*
 * IsLibraryName - the name is a DLL (".dll")
 */
static BOOL IsLibraryName(TCHAR *name)
{
	int len = lstrlen(name);
	return (len > 4 && str_cmp_i(name + len - 4, TEXT(".dll")) == 0);
}

/*
 * LoadLibraryPath - load a DLL located in a directory
 */
static HANDLE LoadLibraryPath(TCHAR *dir, TCHAR *FileName)
{
	TCHAR buf[MAX_PATH + 1];

	if (dir == NULL || *dir == TEXT('\0') || lstrlen(dir) + lstrlen(FileName) >= MAX_PATH) {
		return NULL;
	}
	lstrcpy(buf, dir);
	lstrcat(buf, FileName);
	if (GetFileAttributes(buf) == INVALID_FILE_ATTRIBUTES) {
		return NULL;
	}
	return LoadLibraryEx(buf, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
}

/*
 * LoadLibraryFile - ライブラリを読み込む
 */
static BOOL LoadLibraryFile(SCRIPTINFO *sci, TCHAR *path, TCHAR *FileName)
{
	SCRIPTINFO *tsci = sci->sci_top;
	EXECINFO ei;
	LIBRARYINFO *lib, *pl;
	HANDLE hModul = NULL;
	TCHAR dir[MAX_PATH + 1];
	BOOL absolute;

	//ライブラリの読み込み
	// search order:
	// 1) directory of the importing script  2) current directory
	// 3) directory of the program  4) default search order
	absolute = (*FileName == TEXT('\\') || *FileName == TEXT('/') ||
		(*FileName != TEXT('\0') && *(FileName + 1) == TEXT(':')));
	if (absolute) {
		hModul = LoadLibraryEx(FileName, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	} else {
		if (path != NULL && *path != TEXT('\0')) {
			hModul = LoadLibraryPath(path, FileName);
		}
		if (hModul == NULL) {
			*dir = TEXT('\0');
			if (GetCurrentDirectory(MAX_PATH, dir) != 0) {
				lstrcat(dir, TEXT("\\"));
				hModul = LoadLibraryPath(dir, FileName);
			}
		}
		if (hModul == NULL) {
			GetModuleDir(dir);
			hModul = LoadLibraryPath(dir, FileName);
		}
		if (hModul == NULL) {
			hModul = LoadLibrary(FileName);
		}
	}
	if (hModul == NULL) {
		return FALSE;
	}
	// already imported by another script
	for (pl = tsci->lib; pl != NULL; pl = pl->next) {
		if (pl->hModul == hModul) {
			FreeLibrary(hModul);
			return TRUE;
		}
	}
	lib = mem_calloc(sizeof(LIBRARYINFO));
	if(lib == NULL){
		FreeLibrary(hModul);
		ZeroMemory(&ei, sizeof(EXECINFO));
		ei.sci = sci;
		Error(&ei, ERR_ALLOC, FileName, NULL);
		return FALSE;
	}
	lib->hModul = hModul;
	if (tsci->lib == NULL) {
		tsci->lib = lib;
	} else {
		for (pl = tsci->lib; pl->next != NULL; pl = pl->next);
		pl->next = lib;
	}
	return TRUE;
}

/*
 * ReadScriptFiles - スクリプトファイルを検索して読み込む
 */
static int ReadScriptFiles(SCRIPTINFO *sci, TCHAR *path, TCHAR *name)
{
	WIN32_FIND_DATA FindData;
	HANDLE hFindFile;
	TCHAR buf[MAX_PATH + 1];
	TCHAR sPath[MAX_PATH + 1];
	TCHAR self[MAX_PATH + 1];
	TCHAR full[MAX_PATH + 1];
	TCHAR *p, *r;
	int ret = IMPORT_OK;

	if (lstrlen(path) + lstrlen(name) >= MAX_PATH) {
		return IMPORT_NOT_FOUND;
	}

	// 相対パスを結合
	p = str_cpy(buf, path);
	p = str_cpy(p, name);
	for (p = r = buf; *p != TEXT('\0'); p++) {
#ifdef UNICODE
		if (*p == TEXT('\\') || *p == TEXT('/')) {
			r = p + 1;
		}
#else
		if (IsDBCSLeadByte(*p) == TRUE && *(p + 1) != TEXT('\0')) {
			p++;
		} else if (*p == TEXT('\\') || *p == TEXT('/')) {
			r = p + 1;
		}
#endif
	}
	str_cpy_n(sPath, buf, (int)(r - buf));

	// a wildcard does not import the importing script itself (naming it is a circular import)
	for (p = name; *p != TEXT('\0') && *p != TEXT('*') && *p != TEXT('?'); p++);
	if (*p == TEXT('\0') || GetScriptFullPath(sci->path, sci->name, self) == FALSE) {
		*self = TEXT('\0');
	}

	hFindFile = FindFirstFile(buf, &FindData);
	if (hFindFile == INVALID_HANDLE_VALUE) {
		return IMPORT_NOT_FOUND;
	}
	do {
		if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
			SCRIPTINFO *csci;
			if (*self != TEXT('\0') && GetScriptFullPath(sPath, FindData.cFileName, full) == TRUE &&
				str_cmp_i(self, full) == 0) {
				// 同一ファイルは読み込まない
				continue;
			}
			csci = ReadScriptFile(sci->sci_top, sPath, FindData.cFileName);
			ret = (csci == NULL) ? IMPORT_ERROR : ExecImportedScript(csci);
			if (ret != IMPORT_OK) {
				break;
			}
		}
	} while (FindNextFile(hFindFile, &FindData) == TRUE);
	FindClose(hFindFile);
	return ret;
}

/*
 * DownloadScript - code of a script stored online (NULL when it can not be read)
 */
static TCHAR *DownloadScript(TCHAR *cid)
{
	HTTP_RESULT *res;
	TCHAR server[BUF_SIZE];
	TCHAR *url, *text = NULL;
	TCHAR *p;
	DWORD len;

	len = GetEnvironmentVariable(ONLINE_SERVER_ENV, server, BUF_SIZE);
	if (len == 0 || len >= BUF_SIZE) {
		lstrcpy(server, ONLINE_DEFAULT_SERVER);
	}
	for (len = lstrlen(server); len > 0 && server[len - 1] == TEXT('/'); len--) {
		server[len - 1] = TEXT('\0');
	}
	url = mem_alloc(sizeof(TCHAR) * (lstrlen(server) + lstrlen(IMPORT_URL_PATH) + lstrlen(cid) + 1));
	if (url == NULL) {
		return NULL;
	}
	p = str_cpy(url, server);
	p = str_cpy(p, IMPORT_URL_PATH);
	str_cpy(p, cid);
	res = http_request(TEXT("GET"), url, NULL);
	mem_free(&url);
	if (res == NULL) {
		return NULL;
	}
	if (res->status == 200 && res->body != NULL) {
		text = Utf8ToText((BYTE *)res->body);
	}
	http_free_result(res);
	return text;
}

/*
 * ReadScriptCid - read a script stored online (#import("cid:...")) and run it
 */
static int ReadScriptCid(SCRIPTINFO *sci, TCHAR *cid)
{
	SCRIPTINFO *tsci = sci->sci_top;
	SCRIPTINFO *csci, *last;
	EXECINFO ei;
	TCHAR name[IMPORT_CID_SIZE + 4 + 1];
	TCHAR *buf, *p;
	int len;

	// the cid (0-9, A-Z, a-z and '-'); the spaces around it are ignored
	for (; IS_SPACE(*cid); cid++);
	for (p = cid; IS_CID_CHAR(*p); p++);
	len = (int)(p - cid);
	for (; IS_SPACE(*p); p++);
	if (len == 0 || len >= IMPORT_CID_SIZE || *p != TEXT('\0')) {
		return IMPORT_ERROR;
	}
	// the script is named "cid:<cid>" and has no path
	lstrcpy(name, IMPORT_CID);
	str_cpy_n(name + lstrlen(name), cid, len);

	// a script that is already imported (or being imported) is not read again
	for (csci = last = tsci; csci != NULL; last = csci, csci = csci->next) {
		if (csci->path == NULL && csci->name != NULL && lstrcmp(csci->name, name) == 0) {
			break;
		}
	}
	if (csci == NULL) {
		buf = DownloadScript(name + lstrlen(IMPORT_CID));
		if (buf == NULL) {
			return IMPORT_ERROR;
		}
		csci = mem_calloc(sizeof(SCRIPTINFO));
		if (csci == NULL || (csci->name = alloc_copy(name)) == NULL) {
			mem_free(&csci);
			mem_free(&buf);
			ZeroMemory(&ei, sizeof(EXECINFO));
			ei.sci = sci;
			Error(&ei, ERR_ALLOC, name, NULL);
			return IMPORT_ERROR;
		}
		csci->buf = buf;
		csci->sci_top = tsci;
		csci->strict_val_op = tsci->strict_val_op;
		csci->strict_val = tsci->strict_val_op;
		csci->extension = tsci->extension;
		last->next = csci;
		//構文解析
		ZeroMemory(&ei, sizeof(EXECINFO));
		ei.name = csci->name;
		ei.sci = csci;
		csci->importing = TRUE;
		csci->tk = ParseSentence(&ei, csci->buf, 0);
		csci->importing = FALSE;
	}
	return ExecImportedScript(csci);
}

/*
 * Preprocessor - プリプロセッサ
 */
TCHAR *Preprocessor(SCRIPTINFO *sci, TCHAR *path, TCHAR *p)
{
	TCHAR *r;

	if ((r = PreprocessorLine(sci, path, p)) == NULL) {
		sci->prep_error = TRUE;
	}
	return r;
}

/*
 * PreprocessorLine - process a preprocessor line (NULL on an error)
 */
static TCHAR *PreprocessorLine(SCRIPTINFO *sci, TCHAR *path, TCHAR *p)
{
	EXECINFO ei;
	TCHAR *str;
	TCHAR *r, *s, *t;

	for (t = p; *p != TEXT('\0') && *p != TEXT('('); p++);
	if (*p == TEXT('\0')) {
		ZeroMemory(&ei, sizeof(EXECINFO));
		ei.sci = sci;
		Error(&ei, ERR_SENTENCE, t, NULL);
		return NULL;
	}
	r = get_pair_brace(p);
	if (r == NULL || *r == TEXT('\0')) {
		ZeroMemory(&ei, sizeof(EXECINFO));
		ei.sci = sci;
		Error(&ei, ERR_PARENTHESES, t, NULL);
		return NULL;
	}
	for (p++; IS_SPACE(*p); p++);
	str = alloc_copy_n(p, (int)(r - p));
	if (str == NULL) {
		ZeroMemory(&ei, sizeof(EXECINFO));
		ei.sci = sci;
		Error(&ei, ERR_ALLOC, t, NULL);
		return NULL;
	}
	if (*str == TEXT('\"')) {
		s = str_skip(str, TEXT('\"'));
		*s = TEXT('\0');
		lstrcpy(str, str + 1);
	} else if (*str == TEXT('\'')) {
		s = str_skip(str, TEXT('\''));
		*s = TEXT('\0');
		lstrcpy(str, str + 1);
	}

	if (str_cmp_ni(t, PREP_IMPORT, lstrlen(PREP_IMPORT)) == 0) {
		sci->extension = TRUE;
		TCHAR cdir[MAX_PATH + 1] = { 0 };
		TCHAR mdir[MAX_PATH + 1] = { 0 };
		int ret = IMPORT_NOT_FOUND;
		if (GetCurrentDirectory(MAX_PATH, cdir) != 0) {
			lstrcat(cdir, TEXT("\\"));
		}
		GetModuleDir(mdir);
		// search order:
		// 1) relative to the script path (script)
		// 2) relative to the current directory (script)
		// 3) relative to the program directory (script)
		// 4) library (a ".dll" name skips the script search)
		// ("cid:<cid>" is a script stored online)
		// a script is read once however many times it is imported, and importing a script
		// that is still being read is a circular import
		// スクリプトファイルまたはライブラリのインポート
		// 検索順序は以下
		// 1) スクリプトパスからの相対パス(スクリプト)
		// 2) カレントディレクトリからの相対パス(スクリプト)
		// 3) ライブラリ
		if (str_cmp_ni(str, IMPORT_CID, lstrlen(IMPORT_CID)) == 0) {
			ret = ReadScriptCid(sci, str + lstrlen(IMPORT_CID));
		} else if (IsLibraryName(str) == FALSE) {
			ret = ReadScriptFiles(sci, path, str);
			if (ret == IMPORT_NOT_FOUND && *cdir != TEXT('\0')) {
				ret = ReadScriptFiles(sci, cdir, str);
			}
			if (ret == IMPORT_NOT_FOUND && *mdir != TEXT('\0')) {
				ret = ReadScriptFiles(sci, mdir, str);
			}
		}
		if (ret == IMPORT_NOT_FOUND) {
			ret = (LoadLibraryFile(sci, path, str) == TRUE) ? IMPORT_OK : IMPORT_ERROR;
		}
		if (ret != IMPORT_OK) {
#ifndef IGNORE_IMPORT_ERROR
			mem_free(&str);
			ZeroMemory(&ei, sizeof(EXECINFO));
			ei.sci = sci;
			Error(&ei, (ret == IMPORT_CIRCULAR) ? ERR_IMPORT_CIRCULAR : ERR_SCRIPT, t, NULL);
			return NULL;
#endif
		}
	} else if (str_cmp_ni(t, PREP_LIBRARY, lstrlen(PREP_LIBRARY)) == 0) {
		sci->extension = TRUE;
		// ライブラリの読み込み
		if (LoadLibraryFile(sci, path, str) == FALSE) {
#ifndef IGNORE_IMPORT_ERROR
			mem_free(&str);
			ZeroMemory(&ei, sizeof(EXECINFO));
			ei.sci = sci;
			Error(&ei, ERR_FILEOPEN, t, NULL);
			return NULL;
#endif
		}
	} else if (str_cmp_ni(t, PREP_OPTION, lstrlen(PREP_OPTION)) == 0) {
		//オプション
		if (str_cmp_i(str, TEXT("pg0.5")) == 0) {
			// PG0.5
			sci->extension = TRUE;
		} else if (str_cmp_i(str, TEXT("strict")) == 0) {
			// 明示的な変数宣言の強制
			sci->strict_val = TRUE;
		} else {
			// 不正な識別子
			mem_free(&str);
			ZeroMemory(&ei, sizeof(EXECINFO));
			ei.sci = sci;
			Error(&ei, ERR_SENTENCE, t, NULL);
			return NULL;
		}
	} else {
		// 不正な識別子
		mem_free(&str);
		ZeroMemory(&ei, sizeof(EXECINFO));
		ei.sci = sci;
		Error(&ei, ERR_SENTENCE, t, NULL);
		return NULL;
	}
	mem_free(&str);
	return (r + 1);
}
/* End of source */
