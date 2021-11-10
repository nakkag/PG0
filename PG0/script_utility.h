/*
 * PG0
 *
 * script_utility.h
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef SCRIPT_UTILITY_H
#define SCRIPT_UTILITY_H

/* Include Files */
#include <windows.h>
#include <tchar.h>

/* Define */

/* Struct */

/* Function Prototypes */
VALUEINFO *AllocValue();
void FreeValue(VALUEINFO *vi);
void FreeValueList(VALUEINFO *vi);

VALUEINFO *CopyValueList(VALUEINFO *From);
VALUEINFO *CopyValue(VALUEINFO *From);

int GetValueInt(VALUE *v);
TCHAR *GetValueString(VALUE *v);
double GetValueFloat(VALUE *v);
BOOL GetValueBoolean(VALUE *v);

VALUEINFO *IntToVariable(TCHAR *name, const int i);
int VariableToInt(VALUEINFO *vi);
VALUEINFO *StringToVariable(TCHAR *name, const TCHAR *buf);
TCHAR *VariableToString(VALUEINFO *vi);

int ArrayToStringSize(VALUEINFO *vi, BOOL hex_mode);
void ArrayToString(VALUEINFO *vi, TCHAR *buf, BOOL hex_mode);

#endif
/* End of source */
