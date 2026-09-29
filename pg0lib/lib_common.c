/*
 * PG0 library
 *
 * lib_common.c
 *
 * Helpers shared by the PG0.5 library DLLs (io, math, string, screen).
 */

/* Include Files */
#include <windows.h>
#include <shlobj.h>
#include <tchar.h>
#include <math.h>
#include <float.h>

#include "lib_common.h"

#pragma comment(lib, "shell32.lib")

/* Define */
#define NUMBER_BUF_SIZE			340

/* Global Variables */

/* Local Function Prototypes */

/*
 * lib_param_count - number of parameters in the list
 */
int lib_param_count(VALUEINFO *param)
{
	int cnt = 0;
	for (; param != NULL; param = param->next) {
		cnt++;
	}
	return cnt;
}

/*
 * lib_param - parameter at the index (NULL when missing)
 */
VALUEINFO *lib_param(VALUEINFO *param, int index)
{
	for (; param != NULL && index > 0; param = param->next, index--);
	return param;
}

/*
 * lib_is_number - integer or float
 */
BOOL lib_is_number(VALUEINFO *vi)
{
	if (vi == NULL || vi->v == NULL) {
		return FALSE;
	}
	return (vi->v->type == TYPE_INTEGER || vi->v->type == TYPE_FLOAT);
}

/*
 * lib_string_to_number - leading number of a string (same rule as the web version)
 */
double lib_string_to_number(const TCHAR *str)
{
	TCHAR buf[NUMBER_BUF_SIZE];
	const TCHAR *p;
	int len = 0;
	BOOL int_digits = FALSE, frac_digits = FALSE, dot = FALSE;

	if (str == NULL) {
		return 0;
	}
	p = str;
	if (*p == TEXT('-')) {
		buf[len++] = *p++;
	}
	while (*p >= TEXT('0') && *p <= TEXT('9')) {
		if (len < NUMBER_BUF_SIZE - 2) {
			buf[len++] = *p;
		}
		p++;
		int_digits = TRUE;
	}
	if (*p == TEXT('.')) {
		const TCHAR *q = p + 1;
		while (*q >= TEXT('0') && *q <= TEXT('9')) {
			q++;
			frac_digits = TRUE;
		}
		if (int_digits || frac_digits) {
			dot = TRUE;
			if (len < NUMBER_BUF_SIZE - 2) {
				buf[len++] = TEXT('.');
			}
			for (q = p + 1; *q >= TEXT('0') && *q <= TEXT('9'); q++) {
				if (len < NUMBER_BUF_SIZE - 2) {
					buf[len++] = *q;
				}
			}
		}
	}
	buf[len] = TEXT('\0');
	if (dot) {
		return _tcstod(buf, NULL);
	}
	if (int_digits) {
		return (double)lib_clamp_int(_tcstod(buf, NULL));
	}
	return 0;
}

/*
 * lib_to_float - numeric value of a parameter
 */
double lib_to_float(VALUEINFO *vi)
{
	TCHAR *str;
	double d;

	if (vi == NULL || vi->v == NULL) {
		return 0;
	}
	switch (vi->v->type) {
	case TYPE_INTEGER:
		return (double)vi->v->u.iValue;
	case TYPE_FLOAT:
		return vi->v->u.fValue;
	case TYPE_STRING:
		return lib_string_to_number(vi->v->u.sValue);
	case TYPE_ARRAY:
		str = lib_array_to_string(vi->v->u.array);
		d = lib_string_to_number(str);
		mem_free(&str);
		return d;
	}
	return 0;
}

/*
 * lib_to_int - integer value of a parameter (truncated, clamped)
 */
int lib_to_int(VALUEINFO *vi)
{
	if (vi != NULL && vi->v != NULL && vi->v->type == TYPE_INTEGER) {
		return vi->v->u.iValue;
	}
	return lib_clamp_int(lib_to_float(vi));
}

/*
 * lib_array_to_string - concatenate the elements of an array
 */
TCHAR *lib_array_to_string(VALUEINFO *list)
{
	VALUEINFO *vi;
	TCHAR *ret, *p, *tmp;
	int len = 0;

	for (vi = list; vi != NULL; vi = vi->next) {
		if (vi->v == NULL || vi->v->type == TYPE_ARRAY) {
			continue;
		}
		if (vi->v->type == TYPE_STRING) {
			len += lstrlen(vi->v->u.sValue);
		} else {
			tmp = GetValueString(vi->v);
			len += lstrlen(tmp);
			mem_free(&tmp);
		}
	}
	p = ret = mem_calloc(sizeof(TCHAR) * (len + 1));
	if (ret == NULL) {
		return NULL;
	}
	for (vi = list; vi != NULL; vi = vi->next) {
		if (vi->v == NULL || vi->v->type == TYPE_ARRAY) {
			continue;
		}
		if (vi->v->type == TYPE_STRING) {
			lstrcpy(p, vi->v->u.sValue);
		} else {
			tmp = GetValueString(vi->v);
			lstrcpy(p, tmp);
			mem_free(&tmp);
		}
		p += lstrlen(p);
	}
	return ret;
}

/*
 * lib_to_string - string value of a parameter (arrays are concatenated)
 */
TCHAR *lib_to_string(VALUEINFO *vi)
{
	if (vi == NULL || vi->v == NULL) {
		return alloc_copy(TEXT(""));
	}
	if (vi->v->type == TYPE_ARRAY) {
		return lib_array_to_string(vi->v->u.array);
	}
	return GetValueString(vi->v);
}

/*
 * lib_to_display_string - string value of a parameter (arrays as {...})
 */
TCHAR *lib_to_display_string(VALUEINFO *vi)
{
	TCHAR *str;
	int size;

	if (vi == NULL || vi->v == NULL) {
		return alloc_copy(TEXT(""));
	}
	if (vi->v->type == TYPE_ARRAY) {
		size = ArrayToStringSize(vi->v->u.array, FALSE);
		str = mem_alloc(sizeof(TCHAR) * (size + 1));
		if (str == NULL) {
			return NULL;
		}
		ArrayToString(vi->v->u.array, str, FALSE);
		return str;
	}
	return GetValueString(vi->v);
}

/*
 * lib_find_key - element of an array by key (case insensitive)
 */
VALUEINFO *lib_find_key(VALUEINFO *array, const TCHAR *key)
{
	VALUEINFO *vi;

	if (array == NULL || array->v == NULL || array->v->type != TYPE_ARRAY) {
		return NULL;
	}
	for (vi = array->v->u.array; vi != NULL; vi = vi->next) {
		if (vi->name != NULL && *vi->name != TEXT('\0') && str_cmp_i(vi->name, key) == 0) {
			return vi;
		}
	}
	return NULL;
}

/*
 * lib_find_index - element of an array by position
 */
VALUEINFO *lib_find_index(VALUEINFO *array, int index)
{
	VALUEINFO *vi;

	if (array == NULL || array->v == NULL || array->v->type != TYPE_ARRAY) {
		return NULL;
	}
	for (vi = array->v->u.array; vi != NULL && index > 0; vi = vi->next, index--);
	return vi;
}

/*
 * lib_array_count - number of elements of an array
 */
int lib_array_count(VALUEINFO *array)
{
	VALUEINFO *vi;
	int cnt = 0;

	if (array == NULL || array->v == NULL || array->v->type != TYPE_ARRAY) {
		return 0;
	}
	for (vi = array->v->u.array; vi != NULL; vi = vi->next) {
		cnt++;
	}
	return cnt;
}

/*
 * lib_opt_number - numeric option of an option array
 */
BOOL lib_opt_number(VALUEINFO *opts, const TCHAR *key, double *value)
{
	VALUEINFO *vi = lib_find_key(opts, key);
	if (vi == NULL || !lib_is_number(vi)) {
		return FALSE;
	}
	*value = lib_to_float(vi);
	return TRUE;
}

/*
 * lib_opt_string - string option of an option array
 */
BOOL lib_opt_string(VALUEINFO *opts, const TCHAR *key, TCHAR **value)
{
	VALUEINFO *vi = lib_find_key(opts, key);
	if (vi == NULL || vi->v->type != TYPE_STRING) {
		return FALSE;
	}
	*value = vi->v->u.sValue;
	return TRUE;
}

/*
 * lib_check_int - the value is an integer within the 32bit range
 */
BOOL lib_check_int(double d)
{
	if (_isnan(d) || !_finite(d)) {
		return FALSE;
	}
	if (d != floor(d)) {
		return FALSE;
	}
	if (d > 2147483647.0 || d < -2147483648.0) {
		return FALSE;
	}
	return TRUE;
}

/*
 * lib_clamp_int - truncate to the 32bit integer range
 */
int lib_clamp_int(double d)
{
	if (_isnan(d)) {
		return 0;
	}
	if (d >= 2147483647.0) {
		return 0x7FFFFFFF;
	}
	if (d <= -2147483648.0) {
		return (int)0x80000000;
	}
	return (int)d;
}

/*
 * lib_set_int - set an integer
 */
void lib_set_int(VALUEINFO *vi, int i)
{
	vi->v->u.iValue = i;
	vi->v->type = TYPE_INTEGER;
}

/*
 * lib_set_float - set a float
 */
void lib_set_float(VALUEINFO *vi, double d)
{
	vi->v->u.fValue = d;
	vi->v->type = TYPE_FLOAT;
}

/*
 * lib_set_number - set a number (integer when it has no fraction)
 */
void lib_set_number(VALUEINFO *vi, double d)
{
	if (lib_check_int(d)) {
		lib_set_int(vi, (int)d);
	} else {
		lib_set_float(vi, d);
	}
}

/*
 * lib_set_string - set a string
 */
BOOL lib_set_string(VALUEINFO *vi, const TCHAR *str)
{
	vi->v->u.sValue = alloc_copy((str != NULL) ? str : TEXT(""));
	if (vi->v->u.sValue == NULL) {
		vi->v->type = TYPE_INTEGER;
		vi->v->u.iValue = 0;
		return FALSE;
	}
	vi->v->type = TYPE_STRING;
	return TRUE;
}

/*
 * lib_set_array - set an array (the list is owned by the value)
 */
void lib_set_array(VALUEINFO *vi, VALUEINFO *list)
{
	vi->v->u.array = list;
	vi->v->type = TYPE_ARRAY;
}

/*
 * lib_set_name - set the key name of an element
 */
BOOL lib_set_name(VALUEINFO *vi, const TCHAR *name)
{
	if (name == NULL || *name == TEXT('\0')) {
		return TRUE;
	}
	vi->name = alloc_copy(name);
	if (vi->name == NULL) {
		return FALSE;
	}
	str_lower(vi->name);
	vi->name_hash = str2hash(vi->name);
	vi->org_name = alloc_copy(name);
	return TRUE;
}

/*
 * lib_new_int - new element holding an integer
 */
VALUEINFO *lib_new_int(const TCHAR *name, int i)
{
	VALUEINFO *vi = AllocValue();
	if (vi == NULL) {
		return NULL;
	}
	lib_set_name(vi, name);
	lib_set_int(vi, i);
	return vi;
}

/*
 * lib_new_float - new element holding a float
 */
VALUEINFO *lib_new_float(const TCHAR *name, double d)
{
	VALUEINFO *vi = AllocValue();
	if (vi == NULL) {
		return NULL;
	}
	lib_set_name(vi, name);
	lib_set_float(vi, d);
	return vi;
}

/*
 * lib_new_number - new element holding a number
 */
VALUEINFO *lib_new_number(const TCHAR *name, double d)
{
	VALUEINFO *vi = AllocValue();
	if (vi == NULL) {
		return NULL;
	}
	lib_set_name(vi, name);
	lib_set_number(vi, d);
	return vi;
}

/*
 * lib_new_string - new element holding a string
 */
VALUEINFO *lib_new_string(const TCHAR *name, const TCHAR *str)
{
	VALUEINFO *vi = AllocValue();
	if (vi == NULL) {
		return NULL;
	}
	lib_set_name(vi, name);
	lib_set_string(vi, str);
	return vi;
}

/*
 * lib_new_array - new element holding an array
 */
VALUEINFO *lib_new_array(const TCHAR *name, VALUEINFO *list)
{
	VALUEINFO *vi = AllocValue();
	if (vi == NULL) {
		FreeValueList(list);
		return NULL;
	}
	lib_set_name(vi, name);
	lib_set_array(vi, list);
	return vi;
}

/*
 * lib_list_append - append an element to a list
 */
VALUEINFO *lib_list_append(VALUEINFO **top, VALUEINFO **last, VALUEINFO *vi)
{
	if (vi == NULL) {
		return NULL;
	}
	vi->next = NULL;
	if (*top == NULL) {
		*top = vi;
	} else {
		(*last)->next = vi;
	}
	*last = vi;
	return vi;
}

/*
 * lib_check_stop - ask the host whether the script was stopped
 */
typedef int (SFUNC *LIB_SCRIPT_CALLBACK)(EXECINFO *ei, TOKEN *cu_tk);

BOOL lib_check_stop(EXECINFO *ei)
{
	SCRIPTINFO *sci;

	if (ei == NULL || ei->sci == NULL) {
		return FALSE;
	}
	sci = (ei->sci->sci_top != NULL) ? ei->sci->sci_top : ei->sci;
	if (sci->callback == NULL) {
		return FALSE;
	}
	// any non-zero result stops the script, as in the interpreter itself
	return (((LIB_SCRIPT_CALLBACK)sci->callback)(ei, NULL) != 0);
}

/*
 * lib_sleep - wait for the time, returning early when the script is stopped
 */
void lib_sleep(EXECINFO *ei, double ms)
{
	LARGE_INTEGER freq, st, now;
	DWORD check = GetTickCount();

	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&st);
	for (;;) {
		Sleep(1);
		QueryPerformanceCounter(&now);
		if ((double)(now.QuadPart - st.QuadPart) * 1000.0 / (double)freq.QuadPart > ms) {
			break;
		}
		if (GetTickCount() - check >= 100) {
			if (lib_check_stop(ei)) {
				break;
			}
			check = GetTickCount();
		}
	}
}

/*
 * lib_call_function - call a script or standard function of the host
 */
typedef VALUEINFO *(*LIB_EXEC_FUNCTION)(EXECINFO *ei, TCHAR *name, VALUEINFO *param);

BOOL lib_call_function(EXECINFO *ei, const TCHAR *name, VALUEINFO *param)
{
	LIB_EXEC_FUNCTION func;
	VALUEINFO *ret;
	TCHAR *lname;

	func = (LIB_EXEC_FUNCTION)GetProcAddress(GetModuleHandle(NULL), "ExecFunction");
	if (func == NULL) {
		return FALSE;
	}
	lname = alloc_copy(name);
	if (lname == NULL) {
		return FALSE;
	}
	str_lower(lname);
	ret = func(ei, lname, param);
	mem_free(&lname);
	if (ret == (VALUEINFO *)RET_ERROR || ret == (VALUEINFO *)RET_EXIT) {
		return FALSE;
	}
	FreeValueList(ret);
	return TRUE;
}

/*
 * lib_print - output a string through the host's print function
 */
BOOL lib_print(EXECINFO *ei, const TCHAR *str)
{
	VALUEINFO *param;
	BOOL ret;

	param = lib_new_string(NULL, str);
	if (param == NULL) {
		return FALSE;
	}
	ret = lib_call_function(ei, TEXT("print"), param);
	FreeValueList(param);
	return ret;
}

/*
 * lib_now_ms - milliseconds since 1970-01-01 00:00:00 UTC
 */
double lib_now_ms(void)
{
	FILETIME ft;
	ULARGE_INTEGER ul;

	GetSystemTimeAsFileTime(&ft);
	ul.LowPart = ft.dwLowDateTime;
	ul.HighPart = ft.dwHighDateTime;
	return (double)(ul.QuadPart - 116444736000000000ULL) / 10000.0;
}

/*
 * lib_get_app_data_dir - %LOCALAPPDATA%\pg0 (created when missing)
 */
BOOL lib_get_app_data_dir(TCHAR *path, int size)
{
	if (size < MAX_PATH) {
		return FALSE;
	}
	if (FAILED(SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, path))) {
		return FALSE;
	}
	lstrcat(path, TEXT("\\pg0"));
	CreateDirectory(path, NULL);
	return TRUE;
}
/* End of source */
