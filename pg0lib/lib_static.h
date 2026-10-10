/*
 * PG0 library
 *
 * lib_static.h
 *
 * The io, math, string, screen and net libraries built into a program (pg0app)
 * instead of being loaded as DLLs. Compile the library sources with PG0_STATIC_LIB.
 */

#ifndef LIB_STATIC_H
#define LIB_STATIC_H

/* Include Files */
#include <windows.h>
#include <tchar.h>

/* Define */

/* Struct */

/* Function Prototypes */
// register the built-in libraries with the interpreter (after InitializeScript)
BOOL lib_static_init(void);
// release the built-in libraries (windows, sound) after the script has ended
void lib_static_term(void);
// the DLL name (with or without a path) is one of the built-in libraries
BOOL lib_static_is_builtin(const TCHAR *name);

#endif
/* End of source */
