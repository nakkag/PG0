/*
 * PG0 library
 *
 * lib_static.c
 *
 * The io, math, string, screen and net libraries built into a program (pg0app).
 * The function tables are generated from *_funcs.h and registered with AddFuncTable,
 * so the interpreter finds "_lib_func_..." the same way as the standard functions.
 */

/* Include Files */
#include <windows.h>
#include <tchar.h>

#include "../PG0/script.h"
#include "../PG0/script_string.h"
#include "lib_static.h"

/* Define */

/* Struct */
typedef struct _BUILTINLIB {
	const TCHAR *dll;
	const LIBFUNCTBL *tbl;
	int count;
} BUILTINLIB;

/* Global Variables */
// prototypes of the library functions
#define LIB_FUNC(name)			int SFUNC _lib_func_##name(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr);
#include "io_funcs.h"
#include "math_funcs.h"
#include "string_funcs.h"
#include "screen_funcs.h"
#include "net_funcs.h"
#undef LIB_FUNC

// function tables
#define LIB_FUNC(name)			{ TEXT("_lib_func_") TEXT(#name), _lib_func_##name },
static const LIBFUNCTBL io_tbl[] = {
#include "io_funcs.h"
};
static const LIBFUNCTBL math_tbl[] = {
#include "math_funcs.h"
};
static const LIBFUNCTBL string_tbl[] = {
#include "string_funcs.h"
};
static const LIBFUNCTBL screen_tbl[] = {
#include "screen_funcs.h"
};
static const LIBFUNCTBL net_tbl[] = {
#include "net_funcs.h"
};
#undef LIB_FUNC

#define TABLE(dll, tbl)			{ TEXT(dll), tbl, sizeof(tbl) / sizeof(LIBFUNCTBL) }
static const BUILTINLIB builtin[] = {
	TABLE("pg0_io.dll", io_tbl),
	TABLE("pg0_math.dll", math_tbl),
	TABLE("pg0_string.dll", string_tbl),
	TABLE("pg0_screen.dll", screen_tbl),
	TABLE("pg0_net.dll", net_tbl),
};
#undef TABLE

/* Local Function Prototypes */
// screen.c (PG0_STATIC_LIB)
void screen_lib_init(void);
void screen_lib_term(void);
// net.c (PG0_STATIC_LIB)
void net_lib_term(void);

/*
 * lib_static_init - register the built-in libraries with the interpreter
 */
BOOL lib_static_init(void)
{
	int i;

	screen_lib_init();
	for (i = 0; i < sizeof(builtin) / sizeof(BUILTINLIB); i++) {
		if (AddFuncTable(builtin[i].tbl, builtin[i].count) == FALSE) {
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * lib_static_term - release the built-in libraries
 */
void lib_static_term(void)
{
	net_lib_term();
	screen_lib_term();
}

/*
 * lib_static_is_builtin - the DLL name is one of the built-in libraries
 */
BOOL lib_static_is_builtin(const TCHAR *name)
{
	const TCHAR *p, *r;
	int i;

	if (name == NULL) {
		return FALSE;
	}
	// file name only
	for (p = r = name; *p != TEXT('\0'); p++) {
		if (*p == TEXT('\\') || *p == TEXT('/')) {
			r = p + 1;
		}
	}
	for (i = 0; i < sizeof(builtin) / sizeof(BUILTINLIB); i++) {
		if (str_cmp_i((TCHAR *)r, (TCHAR *)builtin[i].dll) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}
/* End of source */
