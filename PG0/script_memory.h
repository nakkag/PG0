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
// the program exports its allocator; library DLLs (PG0_LIB) allocate through it
// so that memory can move between the program and the libraries safely
#ifdef PG0_LIB
#define MEM_EXPORT
#else
#define MEM_EXPORT				__declspec(dllexport)
#endif
MEM_EXPORT void *mem_alloc(const int size);
MEM_EXPORT void *mem_calloc(const int size);
MEM_EXPORT void *mem_realloc(void *mem, const int size);
MEM_EXPORT void mem_free(void **mem);
#ifdef _DEBUG
void mem_debug(void);
#endif

TCHAR *alloc_copy(const TCHAR *buf);
TCHAR *alloc_copy_n(TCHAR *buf, const int size);
TCHAR *alloc_join(const TCHAR *buf1, const TCHAR *buf2);

#endif
/* End of source */
