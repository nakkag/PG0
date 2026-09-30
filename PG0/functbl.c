/*
 * PG0
 *
 * functbl.c
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#include <windows.h>
#include <tchar.h>

#include "script.h"
#include "script_string.h"
#include "script_memory.h"

/* Local Function Prototypes */
//main.c
int SFUNC _lib_func_error(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr);
int SFUNC _lib_func_print(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr);
int SFUNC _lib_func_input(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr);

//func_std.c
int SFUNC _lib_func_istype(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr);
int SFUNC _lib_func_length(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr);
int SFUNC _lib_func_array(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr);
int SFUNC _lib_func_string(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr);
int SFUNC _lib_func_number(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr);
int SFUNC _lib_func_int(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr);
int SFUNC _lib_func_code(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr);
int SFUNC _lib_func_char(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr);
int SFUNC _lib_func_getkey(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr);
int SFUNC _lib_func_setkey(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr);

/* Global Variables */
typedef struct _FUNCTBL {
	TCHAR *name;
	int name_hash;
	LIBFUNC func;
} FUNCTBL;

// tables added by AddFuncTable (built-in libraries)
typedef struct _FUNCTBLLIST {
	FUNCTBL *tbl;
	int count;
	struct _FUNCTBLLIST *next;
} FUNCTBLLIST;
static FUNCTBLLIST *ext_tbl = NULL;

FUNCTBL ft[] = {
	TEXT("_lib_func_error"), 0, _lib_func_error,
	TEXT("_lib_func_print"), 0, _lib_func_print,
	TEXT("_lib_func_input"), 0, _lib_func_input,

	TEXT("_lib_func_istype"), 0, _lib_func_istype,
	TEXT("_lib_func_length"), 0, _lib_func_length,
	TEXT("_lib_func_array"), 0, _lib_func_array,
	TEXT("_lib_func_string"), 0, _lib_func_string,
	TEXT("_lib_func_number"), 0, _lib_func_number,
	TEXT("_lib_func_int"), 0, _lib_func_int,
	TEXT("_lib_func_code"), 0, _lib_func_code,
	TEXT("_lib_func_char"), 0, _lib_func_char,
	TEXT("_lib_func_getkey"), 0, _lib_func_getkey,
	TEXT("_lib_func_setkey"), 0, _lib_func_setkey,
};

/*
 * InitFuncAddress - 関数テーブルの初期化
 */
void InitFuncAddress()
{
	int i;

	for(i = 0; i < sizeof(ft) / sizeof(FUNCTBL); i++){
		(ft + i)->name_hash = str2hash((ft + i)->name);
	}
}

/*
 * GetFuncAddress - 関数名からアドレスを取得
 */
LIBFUNC GetFuncAddress(TCHAR *FuncName)
{
	FUNCTBLLIST *list;
	int name_hash;
	int i;

	name_hash = str2hash(FuncName);
	for(i = 0; i < sizeof(ft) / sizeof(FUNCTBL); i++){
		if((ft + i)->name_hash == name_hash && lstrcmp((ft + i)->name, FuncName) == 0){
			return (ft + i)->func;
		}
	}
	for (list = ext_tbl; list != NULL; list = list->next) {
		for (i = 0; i < list->count; i++) {
			if ((list->tbl + i)->name_hash == name_hash && lstrcmp((list->tbl + i)->name, FuncName) == 0) {
				return (list->tbl + i)->func;
			}
		}
	}
	return NULL;
}

/*
 * AddFuncTable - add the function table of a built-in library
 */
BOOL AddFuncTable(const LIBFUNCTBL *tbl, int count)
{
	FUNCTBLLIST *list, *pl;
	int i;

	list = mem_calloc(sizeof(FUNCTBLLIST));
	if (list == NULL) {
		return FALSE;
	}
	list->tbl = mem_calloc(sizeof(FUNCTBL) * count);
	if (list->tbl == NULL) {
		mem_free(&list);
		return FALSE;
	}
	for (i = 0; i < count; i++) {
		(list->tbl + i)->name = (TCHAR *)(tbl + i)->name;
		(list->tbl + i)->name_hash = str2hash((TCHAR *)(tbl + i)->name);
		(list->tbl + i)->func = (tbl + i)->func;
	}
	list->count = count;
	// searched in the order the tables were added
	if (ext_tbl == NULL) {
		ext_tbl = list;
	} else {
		for (pl = ext_tbl; pl->next != NULL; pl = pl->next);
		pl->next = list;
	}
	return TRUE;
}

/*
 * FreeFuncTables - release the tables added by AddFuncTable
 */
void FreeFuncTables(void)
{
	FUNCTBLLIST *list, *next;

	for (list = ext_tbl; list != NULL; list = next) {
		next = list->next;
		mem_free(&list->tbl);
		mem_free(&list);
	}
	ext_tbl = NULL;
}
/* End of source */
