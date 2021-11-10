/*
 * PG0
 *
 * script_string.h
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef SCRIPT_STRING_H
#define SCRIPT_STRING_H

/* Include Files */
#include <windows.h>
#include <tchar.h>

/* Define */
#define INT_LENGTH		12
#define FLOAT_LENGTH	330

/* Struct */

/* Function Prototypes */
char *tchar2char(const TCHAR *str);
TCHAR *char2tchar(const char *str);
TCHAR *str_cpy(TCHAR *ret, const TCHAR *buf);
TCHAR *str_cpy_n(TCHAR *ret, TCHAR *buf, const int len);
int str_cmp_n(const TCHAR *buf1, const TCHAR *buf2, const int len);
int str_cmp_i(const TCHAR *buf1, const TCHAR *buf2);
int str_cmp_ni(const TCHAR *buf1, const TCHAR *buf2, const int len);
TCHAR *f2a(const double f);
TCHAR *i2a(const int i);
int x2d(const TCHAR *str);
int o2d(const TCHAR *str);
BOOL conv_ctrl(TCHAR *buf);
TCHAR *reconv_ctrl(TCHAR *buf);
TCHAR *str_skip(TCHAR *p, TCHAR end);
TCHAR *get_pair_brace(TCHAR *buf);
BOOL str_trim(TCHAR *buf);
void str_lower(TCHAR *p);
int str2hash(TCHAR *str);

#endif
/* End of source */
