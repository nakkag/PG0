/*
 * PG0 library
 *
 * math.c
 *
 * Numeric functions (math.pg0), the same API as the web version's math.js.
 */

/* Include Files */
#define _CRT_RAND_S
#include <windows.h>
#include <tchar.h>
#include <math.h>
#include <float.h>
#include <stdlib.h>

#include "lib_common.h"

/* Define */

/* Global Variables */
static BOOL random_seeded = FALSE;
static unsigned int random_state = 0;

/* Local Function Prototypes */

/*
 * DllMain
 */
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(hinst);
	}
	return TRUE;
}

/*
 * number_result - store a number, raising an error for NaN
 */
static int number_result(VALUEINFO *ret, double num, BOOL float_result, TCHAR *ErrStr)
{
	if (_isnan(num)) {
		lstrcpy(ErrStr, LIB_ERR_NAN);
		return -1;
	}
	if (!float_result) {
		lib_set_int(ret, lib_clamp_int(num));
		return 0;
	}
	lib_set_number(ret, num);
	return 0;
}

/*
 * _lib_func_abs - absolute value
 */
int SFUNC _lib_func_abs(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	double num;

	if (param == NULL) {
		return -2;
	}
	num = fabs(lib_to_float(param));
	if (param->v->type == TYPE_FLOAT && !lib_check_int(num)) {
		lib_set_float(ret, num);
	} else {
		lib_set_number(ret, num);
	}
	return 0;
}

/*
 * _lib_func_atan - arc tangent
 */
int SFUNC _lib_func_atan(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (param == NULL) {
		return -2;
	}
	lib_set_number(ret, atan(lib_to_float(param)));
	return 0;
}

/*
 * _lib_func_cos - cosine
 */
int SFUNC _lib_func_cos(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (param == NULL) {
		return -2;
	}
	lib_set_number(ret, cos(lib_to_float(param)));
	return 0;
}

/*
 * _lib_func_exp - e raised to the power
 */
int SFUNC _lib_func_exp(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (param == NULL) {
		return -2;
	}
	lib_set_number(ret, exp(lib_to_float(param)));
	return 0;
}

/*
 * _lib_func_log - natural logarithm
 */
int SFUNC _lib_func_log(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	double num;

	if (param == NULL) {
		return -2;
	}
	num = log(lib_to_float(param));
	if (!_finite(num) && !_isnan(num) && num < 0) {
		lstrcpy(ErrStr, TEXT("-Infinity"));
		return -1;
	}
	if (_isnan(num)) {
		lstrcpy(ErrStr, LIB_ERR_NAN);
		return -1;
	}
	lib_set_number(ret, num);
	return 0;
}

/*
 * random_seed - seed the reproducible generator (mulberry32) from a string
 */
static void random_seed(const TCHAR *seed)
{
	unsigned int a = 0;
	const TCHAR *p;

	for (p = seed; *p != TEXT('\0'); p++) {
		a = a * 31u + (unsigned int)(unsigned short)*p;
	}
	random_state = a;
	random_seeded = TRUE;
}

/*
 * random_next - next value of the seeded generator (mulberry32)
 */
static double random_next(void)
{
	unsigned int t;

	random_state += 0x6D2B79F5u;
	t = (random_state ^ (random_state >> 15)) * (1u | random_state);
	t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
	return (double)(t ^ (t >> 14)) / 4294967296.0;
}

/*
 * _lib_func_random - random number in [0, 1)
 */
int SFUNC _lib_func_random(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (param != NULL) {
		TCHAR *seed = lib_to_string(param);
		if (seed == NULL) {
			lstrcpy(ErrStr, LIB_ERR_ALLOC);
			return -1;
		}
		random_seed(seed);
		mem_free(&seed);
	}
	if (random_seeded) {
		lib_set_float(ret, random_next());
	} else {
		unsigned int r;
		if (rand_s(&r) != 0) {
			r = (unsigned int)GetTickCount() * 2654435761u;
		}
		lib_set_float(ret, (double)r / 4294967296.0);
	}
	return 0;
}

/*
 * _lib_func_sign - sign of a number
 */
int SFUNC _lib_func_sign(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	double num;

	if (param == NULL) {
		return -2;
	}
	num = lib_to_float(param);
	lib_set_int(ret, (num > 0) ? 1 : ((num < 0) ? -1 : 0));
	return 0;
}

/*
 * _lib_func_sin - sine
 */
int SFUNC _lib_func_sin(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (param == NULL) {
		return -2;
	}
	lib_set_number(ret, sin(lib_to_float(param)));
	return 0;
}

/*
 * _lib_func_sqrt - square root
 */
int SFUNC _lib_func_sqrt(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (param == NULL) {
		return -2;
	}
	return number_result(ret, sqrt(lib_to_float(param)), TRUE, ErrStr);
}

/*
 * _lib_func_tan - tangent
 */
int SFUNC _lib_func_tan(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (param == NULL) {
		return -2;
	}
	lib_set_number(ret, tan(lib_to_float(param)));
	return 0;
}

/*
 * _lib_func_pow - power
 */
int SFUNC _lib_func_pow(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (lib_param_count(param) < 2) {
		return -2;
	}
	return number_result(ret, pow(lib_to_float(param), lib_to_float(param->next)), TRUE, ErrStr);
}

/*
 * collect_numbers - numbers of the parameters (arrays are flattened one level)
 */
static double *collect_numbers(VALUEINFO *param, int *count)
{
	VALUEINFO *vi, *a;
	double *values;
	int cnt = 0;

	for (vi = param; vi != NULL; vi = vi->next) {
		if (vi->v->type == TYPE_ARRAY) {
			for (a = vi->v->u.array; a != NULL; a = a->next) {
				cnt++;
			}
		} else {
			cnt++;
		}
	}
	values = mem_alloc(sizeof(double) * (cnt + 1));
	if (values == NULL) {
		*count = 0;
		return NULL;
	}
	cnt = 0;
	for (vi = param; vi != NULL; vi = vi->next) {
		if (vi->v->type == TYPE_ARRAY) {
			for (a = vi->v->u.array; a != NULL; a = a->next) {
				if (a->v->type == TYPE_ARRAY) {
					continue;
				}
				values[cnt++] = lib_to_float(a);
			}
		} else {
			values[cnt++] = lib_to_float(vi);
		}
	}
	*count = cnt;
	return values;
}

/*
 * _lib_func_max - largest value
 */
int SFUNC _lib_func_max(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	double *values, num = 0;
	int cnt, i;

	if (param == NULL) {
		return -2;
	}
	values = collect_numbers(param, &cnt);
	if (values == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	for (i = 0; i < cnt; i++) {
		if (i == 0 || values[i] > num) {
			num = values[i];
		}
	}
	mem_free(&values);
	lib_set_number(ret, num);
	return 0;
}

/*
 * _lib_func_min - smallest value
 */
int SFUNC _lib_func_min(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	double *values, num = 0;
	int cnt, i;

	if (param == NULL) {
		return -2;
	}
	values = collect_numbers(param, &cnt);
	if (values == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	for (i = 0; i < cnt; i++) {
		if (i == 0 || values[i] < num) {
			num = values[i];
		}
	}
	mem_free(&values);
	lib_set_number(ret, num);
	return 0;
}

/*
 * _lib_func_floor - round down
 */
int SFUNC _lib_func_floor(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (param == NULL) {
		return -2;
	}
	return number_result(ret, floor(lib_to_float(param)), FALSE, ErrStr);
}

/*
 * _lib_func_ceil - round up
 */
int SFUNC _lib_func_ceil(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (param == NULL) {
		return -2;
	}
	return number_result(ret, ceil(lib_to_float(param)), FALSE, ErrStr);
}

/*
 * _lib_func_round - round to the nearest integer (halves go up)
 */
int SFUNC _lib_func_round(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (param == NULL) {
		return -2;
	}
	return number_result(ret, floor(lib_to_float(param) + 0.5), FALSE, ErrStr);
}

/*
 * _lib_func_hypot - length of the hypotenuse
 */
int SFUNC _lib_func_hypot(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (lib_param_count(param) < 2) {
		return -2;
	}
	return number_result(ret, _hypot(lib_to_float(param), lib_to_float(param->next)), TRUE, ErrStr);
}

/*
 * _lib_func_atan2 - angle of the point (x, y)
 */
int SFUNC _lib_func_atan2(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	if (lib_param_count(param) < 2) {
		return -2;
	}
	return number_result(ret, atan2(lib_to_float(param), lib_to_float(param->next)), TRUE, ErrStr);
}
/* End of source */
