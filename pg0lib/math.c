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
#include <stdio.h>

#include "lib_common.h"

/* Define */
#define NUMBER_STRING_SIZE		64

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
 * digits_value - the number 0.digits * 10^n
 */
static double digits_value(const TCHAR *digits, int k, int n)
{
	TCHAR tmp[NUMBER_STRING_SIZE];
	int i, len = 0;

	tmp[len++] = TEXT('.');
	for (i = 0; i < k; i++) {
		tmp[len++] = digits[i];
	}
	wsprintf(tmp + len, TEXT("e%d"), n);
	return _tcstod(tmp, NULL);
}

/*
 * shortest_digits - the fewest significant digits that read back as num (num > 0),
 *                   num is 0.digits * 10^n
 */
static int shortest_digits(double num, TCHAR *digits, int *n)
{
	TCHAR tmp[NUMBER_STRING_SIZE];
	TCHAR *p;
	int prec, k = 0, i;

	for (prec = 1; prec <= 17; prec++) {
		_stprintf_s(tmp, NUMBER_STRING_SIZE, TEXT("%.*e"), prec - 1, num);
		k = 0;
		for (p = tmp; *p != TEXT('e'); p++) {
			if (*p != TEXT('.')) {
				digits[k++] = *p;
			}
		}
		*n = _ttoi(p + 1) + 1;
		if (digits_value(digits, k, *n) == num) {
			break;
		}
		/* next to a power of two the numbers reading back as num reach further above it,
		   so the digits one step up may read back when the nearest ones do not */
		for (i = k - 1; i >= 0 && digits[i] == TEXT('9'); i--) {
			digits[i] = TEXT('0');
		}
		if (i < 0) {
			digits[0] = TEXT('1');
			(*n)++;
		} else {
			digits[i]++;
		}
		if (digits_value(digits, k, *n) == num) {
			break;
		}
	}
	while (k > 1 && digits[k - 1] == TEXT('0')) {
		k--;
	}
	return k;
}

/*
 * js_number_string - format a number like JavaScript's String(num)
 */
static void js_number_string(double num, TCHAR *buf)
{
	TCHAR digits[NUMBER_STRING_SIZE];
	TCHAR *r = buf;
	int k, n, i;

	if (_isnan(num)) {
		lstrcpy(buf, TEXT("NaN"));
		return;
	}
	if (num == 0) {
		lstrcpy(buf, TEXT("0"));
		return;
	}
	if (num < 0) {
		*(r++) = TEXT('-');
		num = -num;
	}
	if (!_finite(num)) {
		lstrcpy(r, TEXT("Infinity"));
		return;
	}
	k = shortest_digits(num, digits, &n);
	if (k <= n && n <= 21) {
		for (i = 0; i < k; i++) {
			*(r++) = digits[i];
		}
		for (; i < n; i++) {
			*(r++) = TEXT('0');
		}
	} else if (0 < n && n <= 21) {
		for (i = 0; i < k; i++) {
			if (i == n) {
				*(r++) = TEXT('.');
			}
			*(r++) = digits[i];
		}
	} else if (-6 < n && n <= 0) {
		*(r++) = TEXT('0');
		*(r++) = TEXT('.');
		for (i = n; i < 0; i++) {
			*(r++) = TEXT('0');
		}
		for (i = 0; i < k; i++) {
			*(r++) = digits[i];
		}
	} else {
		*(r++) = digits[0];
		if (k > 1) {
			*(r++) = TEXT('.');
			for (i = 1; i < k; i++) {
				*(r++) = digits[i];
			}
		}
		r += wsprintf(r, TEXT("e%c%d"), (n - 1 >= 0) ? TEXT('+') : TEXT('-'), abs(n - 1));
	}
	*r = TEXT('\0');
}

/*
 * js_float_string - format a float like the web version's getValueString()
 *                   (String(num) cut or padded to 16 decimal places)
 */
static void js_float_string(double num, TCHAR *buf)
{
	TCHAR *p;
	int len, i;
	BOOL zero = TRUE;

	if (_isnan(num)) {
		lstrcpy(buf, TEXT("0"));
		return;
	}
	js_number_string(num, buf);
	if (_finite(num) && floor(num) == num) {
		lstrcat(buf, TEXT(".0000000000000000"));
		return;
	}
	/* length of String(parseInt(num)) */
	p = buf;
	if (*p == TEXT('-')) {
		p++;
	}
	for (i = 0; p[i] >= TEXT('0') && p[i] <= TEXT('9'); i++) {
		if (p[i] != TEXT('0')) {
			zero = FALSE;
		}
	}
	if (i == 0) {
		len = 3;
	} else if (zero) {
		len = 1;
	} else {
		len = (int)(p - buf) + i;
	}
	len += 1 + 16;
	for (i = lstrlen(buf); i < len; i++) {
		buf[i] = TEXT('0');
	}
	buf[len] = TEXT('\0');
}

/*
 * seed_add - add the characters of a string to the seed hash
 */
static void seed_add(unsigned int *a, const TCHAR *str)
{
	const TCHAR *p;

	if (str == NULL) {
		return;
	}
	for (p = str; *p != TEXT('\0'); p++) {
		*a = *a * 31u + (unsigned int)(unsigned short)*p;
	}
}

/*
 * seed_add_escaped - add a string with its control characters escaped
 *                    (the web version's reConvCtrl())
 */
static void seed_add_escaped(unsigned int *a, const TCHAR *str)
{
	const TCHAR *p;
	TCHAR esc[3] = {TEXT('\\'), TEXT('\0'), TEXT('\0')};

	if (str == NULL) {
		return;
	}
	for (p = str; *p != TEXT('\0'); p++) {
		switch (*p) {
		case TEXT('\\'): esc[1] = TEXT('\\'); break;
		case TEXT('\r'): esc[1] = TEXT('r'); break;
		case TEXT('\n'): esc[1] = TEXT('n'); break;
		case TEXT('\t'): esc[1] = TEXT('t'); break;
		case TEXT('\b'): esc[1] = TEXT('b'); break;
		case TEXT('"'): esc[1] = TEXT('"'); break;
		case TEXT('\''): esc[1] = TEXT('\''); break;
		default: esc[1] = TEXT('\0'); break;
		}
		if (esc[1] != TEXT('\0')) {
			seed_add(a, esc);
		} else {
			*a = *a * 31u + (unsigned int)(unsigned short)*p;
		}
	}
}

/*
 * random_seed - seed the reproducible generator (mulberry32) with a value
 *               converted to a string in the same way as the web version
 */
static void random_seed(VALUE *v)
{
	VALUEINFO *vi;
	TCHAR buf[NUMBER_STRING_SIZE];
	unsigned int a = 0;

	random_seeded = TRUE;
	if (v == NULL) {
		random_state = a;
		return;
	}
	switch (v->type) {
	case TYPE_STRING:
		seed_add(&a, v->u.sValue);
		break;
	case TYPE_FLOAT:
		js_number_string(v->u.fValue, buf);
		seed_add(&a, buf);
		break;
	case TYPE_ARRAY:
		for (vi = v->u.array; vi != NULL; vi = vi->next) {
			if (vi->v == NULL) {
				continue;
			}
			switch (vi->v->type) {
			case TYPE_STRING:
				seed_add_escaped(&a, vi->v->u.sValue);
				break;
			case TYPE_FLOAT:
				js_float_string(vi->v->u.fValue, buf);
				seed_add(&a, buf);
				break;
			case TYPE_ARRAY:
				break;
			default:
				wsprintf(buf, TEXT("%d"), vi->v->u.iValue);
				seed_add(&a, buf);
				break;
			}
		}
		break;
	default:
		wsprintf(buf, TEXT("%d"), v->u.iValue);
		seed_add(&a, buf);
		break;
	}
	random_state = a;
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
		random_seed(param->v);
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
 * _lib_func_round - round to the nearest integer (halves go up, like Math.round)
 */
int SFUNC _lib_func_round(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	double num, f;

	if (param == NULL) {
		return -2;
	}
	// not floor(num + 0.5): the addition rounds 0.49999999999999994 up to 1
	num = lib_to_float(param);
	f = floor(num);
	return number_result(ret, (num - f >= 0.5) ? f + 1 : f, FALSE, ErrStr);
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
