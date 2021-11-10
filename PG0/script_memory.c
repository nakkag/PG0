/*
 * PG0
 *
 * script_memory.c
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#include <windows.h>
#include <tchar.h>

#include "script_string.h"

/* Define */

/* Global Variables */
#ifdef _DEBUG
static SIZE_T all_alloc_size = 0;

//#define MEM_CHECK
#ifdef MEM_CHECK
#define ADDRESS_CNT		100000
#define DEBUG_ADDRESS	0
static long address[ADDRESS_CNT];
static int address_index;
#endif	//MEM_CHECK

#endif	//_DEBUG

/* Local Function Prototypes */

/*
 * mem_alloc - バッファを確保
 */
void *mem_alloc(const int size)
{
#ifdef _DEBUG
	void *mem;

	mem = HeapAlloc(GetProcessHeap(), 0, size);
	if (mem == NULL) {
		return mem;
	}
	all_alloc_size += HeapSize(GetProcessHeap(), 0, mem);
#ifdef MEM_CHECK
	if (address_index < ADDRESS_CNT) {
		if (address_index == DEBUG_ADDRESS) {
			address[address_index] = (long)mem;
		} else {
			address[address_index] = (long)mem;
		}
		address_index++;
	}
#endif	//MEM_CHECK
	return mem;
#else	//_DEBUG
	return HeapAlloc(GetProcessHeap(), 0, size);
#endif	//_DEBUG
}

/*
 * mem_calloc - 初期化したバッファを確保
 */
void *mem_calloc(const int size)
{
#ifdef _DEBUG
	void *mem;

	mem = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
	if (mem == NULL) {
		return mem;
	}
	all_alloc_size += HeapSize(GetProcessHeap(), 0, mem);
#ifdef MEM_CHECK
	if (address_index < ADDRESS_CNT) {
		if (address_index == DEBUG_ADDRESS) {
			address[address_index] = (long)mem;
		} else {
			address[address_index] = (long)mem;
		}
		address_index++;
	}
#endif	//MEM_CHECK
	return mem;
#else	//_DEBUG
	return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
#endif	//_DEBUG
}

/*
 * mem_realloc - バッファを再確保
 */
void *mem_realloc(void *mem, const int size)
{
#ifdef _DEBUG
	all_alloc_size -= HeapSize(GetProcessHeap(), 0, mem);
	mem = HeapReAlloc(GetProcessHeap(), 0, mem, size);
	all_alloc_size += HeapSize(GetProcessHeap(), 0, mem);
#ifdef MEM_CHECK
	if (address_index < ADDRESS_CNT) {
		if (address_index == DEBUG_ADDRESS) {
			address[address_index] = (long)mem;
		} else {
			address[address_index] = (long)mem;
		}
		address_index++;
	}
#endif	//MEM_CHECK
	return mem;
#else	//_DEBUG
	return HeapReAlloc(GetProcessHeap(), 0, mem, size);
#endif	//_DEBUG
}

/*
 * mem_free - バッファを解放
 */
void mem_free(void **mem)
{
	if (*mem != NULL) {
#ifdef _DEBUG
		all_alloc_size -= HeapSize(GetProcessHeap(), 0, *mem);
#ifdef MEM_CHECK
		{
			int i;
			for (i = 0; i < ADDRESS_CNT; i++) {
				if (address[i] == (long)*mem) {
					address[i] = 0;
					break;
				}
			}
		}
#endif	//MEM_CHECK
#endif	//_DEBUG
		HeapFree(GetProcessHeap(), 0, *mem);
		*mem = NULL;
	}
}

/*
 * mem_debug - メモリ情報の表示
 */
#ifdef _DEBUG
void mem_debug(void)
{
	TCHAR buf[256];

	if (all_alloc_size == 0) {
		return;
	}
	wsprintf(buf, TEXT("Memory leak: %lu bytes"), all_alloc_size);
	MessageBox(NULL, buf, TEXT("debug"), 0);
#ifdef MEM_CHECK
	{
		int i;
		for (i = 0; i < ADDRESS_CNT; i++) {
			if (address[i] != 0) {
				wsprintf(buf, TEXT("leak address: %u, %lu"), i, address[i]);
				MessageBox(NULL, buf, TEXT("debug"), 0);
				break;
			}
		}
	}
#endif	//MEM_CHECK
}
#endif	//_DEBUG

/*
 * alloc_copy - バッファを確保して文字列をコピーする
 */
TCHAR *alloc_copy(const TCHAR *buf)
{
	TCHAR *ret;

	if (buf == NULL) {
		return NULL;
	}
	ret = (TCHAR *)mem_alloc(sizeof(TCHAR) * (lstrlen(buf) + 1));
	if (ret != NULL) {
		lstrcpy(ret, buf);
	}
	return ret;
}

/*
 * alloc_copy_n - バッファを確保して指定長さ分の文字列をコピーする
 */
TCHAR *alloc_copy_n(TCHAR *buf, const int size)
{
	TCHAR *ret;

	if (buf == NULL) {
		return NULL;
	}
	ret = (TCHAR *)mem_alloc(sizeof(TCHAR) * (size + 1));
	if (ret != NULL) {
		str_cpy_n(ret, buf, size);
	}
	return ret;
}

/*
 * alloc_join - バッファを確保して文字列を連結する
 */
TCHAR *alloc_join(const TCHAR *buf1, const TCHAR *buf2)
{
	TCHAR *ret, *r;

	if (buf2 == NULL) {
		return alloc_copy(buf1);
	}
	if (buf1 == NULL) {
		return alloc_copy(buf2);
	}
	ret = mem_alloc(sizeof(TCHAR) * (lstrlen(buf1) + lstrlen(buf2) + 1));
	if (ret == NULL) {
		return NULL;
	}
	r = str_cpy(ret, buf1);
	lstrcpy(r, buf2);
	return ret;
}
/* End of source */
