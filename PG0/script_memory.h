/*
 * PG0
 *
 * script_memory.h
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef SCRIPT_MEMORY_H
#define SCRIPT_MEMORY_H

/* Include Files */
#include <windows.h>
#include <tchar.h>

/* Define */

/* Struct */

/* Function Prototypes */
void *mem_alloc(const int size);
void *mem_calloc(const int size);
void *mem_realloc(void *mem, const int size);
void mem_free(void **mem);
#ifdef _DEBUG
void mem_debug(void);
#endif

TCHAR *alloc_copy(const TCHAR *buf);
TCHAR *alloc_copy_n(TCHAR *buf, const int size);
TCHAR *alloc_join(const TCHAR *buf1, const TCHAR *buf2);

#endif
/* End of source */
