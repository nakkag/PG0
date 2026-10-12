/*
 * PG0
 *
 * script_minify.h
 *
 * Copyright (C) 1996-2026 by Nakashima Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef SCRIPT_MINIFY_H
#define SCRIPT_MINIFY_H

/* Include Files */
#include "script_struct.h"

/* Define */

/* Struct */

/* Function Prototypes */
// short names for the variables and the functions of a script chain (before ScriptToImage)
BOOL MinifyScript(SCRIPTINFO *sci);

#endif
/* End of source */
