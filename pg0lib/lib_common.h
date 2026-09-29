/*
 * PG0 library
 *
 * lib_common.h
 *
 * Helpers shared by the PG0.5 library DLLs (io, math, string, screen).
 */

#ifndef PG0_LIB_COMMON_H
#define PG0_LIB_COMMON_H

/* Include Files */
#include <windows.h>
#include <tchar.h>

#include "../PG0/script_struct.h"
#include "../PG0/script_memory.h"
#include "../PG0/script_string.h"
#include "../PG0/script_utility.h"

/* Define */
#define LIB_ERR_NAN				TEXT("Not a Number")
#define LIB_ERR_ALLOC			TEXT("Alloc error")

/* Function Prototypes */
/* parameters */
int lib_param_count(VALUEINFO *param);
VALUEINFO *lib_param(VALUEINFO *param, int index);
BOOL lib_is_number(VALUEINFO *vi);
double lib_to_float(VALUEINFO *vi);
int lib_to_int(VALUEINFO *vi);
double lib_string_to_number(const TCHAR *str);
TCHAR *lib_to_string(VALUEINFO *vi);
TCHAR *lib_to_display_string(VALUEINFO *vi);
TCHAR *lib_array_to_string(VALUEINFO *list);
VALUEINFO *lib_find_key(VALUEINFO *array, const TCHAR *key);
VALUEINFO *lib_find_index(VALUEINFO *array, int index);
int lib_array_count(VALUEINFO *array);
BOOL lib_opt_number(VALUEINFO *opts, const TCHAR *key, double *value);
BOOL lib_opt_string(VALUEINFO *opts, const TCHAR *key, TCHAR **value);

/* return values */
BOOL lib_check_int(double d);
int lib_clamp_int(double d);
void lib_set_int(VALUEINFO *vi, int i);
void lib_set_float(VALUEINFO *vi, double d);
void lib_set_number(VALUEINFO *vi, double d);
BOOL lib_set_string(VALUEINFO *vi, const TCHAR *str);
void lib_set_array(VALUEINFO *vi, VALUEINFO *list);
BOOL lib_set_name(VALUEINFO *vi, const TCHAR *name);
VALUEINFO *lib_new_int(const TCHAR *name, int i);
VALUEINFO *lib_new_float(const TCHAR *name, double d);
VALUEINFO *lib_new_number(const TCHAR *name, double d);
VALUEINFO *lib_new_string(const TCHAR *name, const TCHAR *str);
VALUEINFO *lib_new_array(const TCHAR *name, VALUEINFO *list);
VALUEINFO *lib_list_append(VALUEINFO **top, VALUEINFO **last, VALUEINFO *vi);

/* host interaction */
BOOL lib_check_stop(EXECINFO *ei);
void lib_sleep(EXECINFO *ei, double ms);
BOOL lib_call_function(EXECINFO *ei, const TCHAR *name, VALUEINFO *param);
BOOL lib_print(EXECINFO *ei, const TCHAR *str);
double lib_now_ms(void);
BOOL lib_get_app_data_dir(TCHAR *path, int size);

#endif
/* End of source */
