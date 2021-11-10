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

/* Define */
#define PREP_IMPORT				TEXT("import")
#define PREP_LIBRARY			TEXT("library")
#define PREP_OPTION				TEXT("option")

#define IS_SPACE(c)				(c == TEXT(' ') || c == TEXT('\t') || c == TEXT('\r') || c == TEXT('\n'))

/* Global Variables */

/* Local Function Prototypes */
static BOOL LoadLibraryFile(SCRIPTINFO *sci, TCHAR *FileName);

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
	WCHAR *wbuf;
	BYTE *buf;
	DWORD fSizeLow, fSizeHigh;
	DWORD ret;
	DWORD errcode;
	int len;
	int bom = 0;

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
	// BOMをスキップ
	if (ret > 3 && *buf == 0xEF && *(buf + 1) == 0xBB && *(buf + 2) == 0xBF) {
		bom = 3;
	}
	// UTF-8からUTF-16に変換
	len = MultiByteToWideChar(CP_UTF8, 0, buf + bom, -1, NULL, 0);
	if ((wbuf = (WCHAR *)mem_alloc(sizeof(WCHAR) * len)) == NULL) {
		errcode = GetLastError();
		mem_free(&buf);
		SetLastError(errcode);
		return NULL;
	}
	MultiByteToWideChar(CP_UTF8, 0, buf + bom, -1, wbuf, len);
	mem_free(&buf);

#ifdef UNICODE
	return wbuf;
#else
	// UTF-16からASCIIに変換
	len = WideCharToMultiByte(CP_ACP, 0, wbuf, -1, NULL, 0, NULL, NULL);
	if ((buf = (BYTE *)mem_alloc(sizeof(BYTE) * len)) == NULL) {
		errcode = GetLastError();
		mem_free(&wbuf);
		SetLastError(errcode);
		return NULL;
	}
	WideCharToMultiByte(CP_ACP, 0, wbuf, -1, buf, len, NULL, NULL);
	mem_free(&wbuf);
	return buf;
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

	lstrcpy(fpath, path);
	if (*path != TEXT('\0') && *(path + lstrlen(path) - 1) != TEXT('\\')) {
		lstrcat(fpath, TEXT("\\"));
	}
	if (csci->buf != NULL) {
		//リストの作成
		while (csci->next != NULL) {
			csci = csci->next;
			if (csci->name != NULL && str_cmp_i(name, csci->name) == 0 &&
				csci->path != NULL && str_cmp_i(fpath, csci->path) == 0) {
				return csci;
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
	ei.name = name;
	ei.sci = csci;
	csci->tk = ParseSentence(&ei, csci->buf, 0);
	return csci;
}

/*
 * LoadLibraryFile - ライブラリを読み込む
 */
static BOOL LoadLibraryFile(SCRIPTINFO *sci, TCHAR *FileName)
{
	SCRIPTINFO *tsci = sci->sci_top;
	EXECINFO ei;
	LIBRARYINFO *lib, *pl;

	lib = mem_calloc(sizeof(LIBRARYINFO));
	if(lib == NULL){
		ZeroMemory(&ei, sizeof(EXECINFO));
		ei.sci = sci;
		Error(&ei, ERR_ALLOC, FileName, NULL);
		return FALSE;
	}
	//ライブラリの読み込み
	lib->hModul = LoadLibrary(FileName);
	if(lib->hModul == NULL){
		mem_free(&lib);
		return FALSE;
	}
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
BOOL ReadScriptFiles(SCRIPTINFO *sci, TCHAR *path, TCHAR *name)
{
	WIN32_FIND_DATA FindData;
	HANDLE hFindFile;
	TCHAR buf[MAX_PATH + 1];
	TCHAR sPath[MAX_PATH + 1];
	TCHAR *p, *r;

	if (lstrlen(path) + lstrlen(name) >= MAX_PATH) {
		return FALSE;
	}

	// 相対パスを結合
	p = str_cpy(buf, path);
	p = str_cpy(p, name);
	for (p = r = buf; *p != TEXT('\0'); p++) {
#ifdef UNICODE
		if (*p == TEXT('\\')) {
			r = p + 1;
		}
#else
		if (IsDBCSLeadByte(*p) == TRUE && *(p + 1) != TEXT('\0')) {
			p++;
		} else if (*p == TEXT('\\')) {
			r = p + 1;
		}
#endif
	}
	str_cpy_n(sPath, buf, (int)(r - buf));

	hFindFile = FindFirstFile(buf, &FindData);
	if (hFindFile == INVALID_HANDLE_VALUE) {
		return FALSE;
	}
	do {
		if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
			SCRIPTINFO *csci;
			VALUEINFO *rvi = NULL;
			if (sci->path != NULL && str_cmp_i(sci->path, sPath) == 0 &&
				sci->name != NULL && str_cmp_i(sci->name, FindData.cFileName) == 0) {
				// 同一ファイルは読み込まない
				continue;
			}
			csci = ReadScriptFile(sci->sci_top, sPath, FindData.cFileName);
			if (csci == NULL || csci->tk == NULL) {
				return FALSE;
			}
			if (csci->ei == NULL && ExecScript(csci, NULL, &rvi) == -1) {
				FreeValueList(rvi);
				return FALSE;
			}
			FreeValueList(rvi);
		}
	} while (FindNextFile(hFindFile, &FindData) == TRUE);
	FindClose(hFindFile);
	return TRUE;
}

/*
 * Preprocessor - プリプロセッサ
 */
TCHAR *Preprocessor(SCRIPTINFO *sci, TCHAR *path, TCHAR *p)
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
		if (GetCurrentDirectory(MAX_PATH, cdir) != 0) {
			lstrcat(cdir, TEXT("\\"));
		}
		// スクリプトファイルまたはライブラリのインポート
		// 検索順序は以下
		// 1) スクリプトパスからの相対パス(スクリプト)
		// 2) カレントディレクトリからの相対パス(スクリプト)
		// 3) ライブラリ
		if (ReadScriptFiles(sci, path, str) == FALSE &&
			(*cdir == TEXT('\0') || ReadScriptFiles(sci, cdir, str) == FALSE) &&
			LoadLibraryFile(sci, str) == FALSE) {
#ifndef IGNORE_IMPORT_ERROR
			mem_free(&str);
			ZeroMemory(&ei, sizeof(EXECINFO));
			ei.sci = sci;
			Error(&ei, ERR_SCRIPT, t, NULL);
			return NULL;
#endif
		}
	} else if (str_cmp_ni(t, PREP_LIBRARY, lstrlen(PREP_LIBRARY)) == 0) {
		sci->extension = TRUE;
		// ライブラリの読み込み
		if (LoadLibraryFile(sci, str) == FALSE) {
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
