/*
 * PG0
 *
 * func_microbit.c
 *
 * Copyright (C) 1996-2019 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#include <windows.h>
#include <tchar.h>

#include "../PG0/script_struct.h"
#include "../PG0/script_memory.h"
#include "../PG0/script_string.h"
#include "../PG0/script_utility.h"

/* Define */
#define BUF_SIZE		256
#define NUM_BUF_SIZE	100

/* Global Variables */
extern BOOL radio_on;

/* Local Function Prototypes */
BOOL sendData(char cmd, TCHAR *buf, TCHAR *ret, TCHAR *ErrStr);

/*
 * StringToNumber - 文字列を数値に変換
 */
static void StringToNumber(TCHAR *buf, VALUEINFO *ret)
{
	TCHAR *p;

	for (p = buf; *p != TEXT('\0') && *p != TEXT('.'); p++);
	if (*p == TEXT('.')) {
		ret->v->u.fValue = _ttof(buf);
		ret->v->type = TYPE_FLOAT;
		if (ret->v->u.fValue == (double)((int)ret->v->u.fValue)) {
			ret->v->u.iValue = (int)ret->v->u.fValue;
			ret->v->type = TYPE_INTEGER;
		}
	} else {
		ret->v->u.iValue = _ttoi(buf);
		ret->v->type = TYPE_INTEGER;
	}
}

/*
 * _lib_func_sleep - 待機
 */
int SFUNC _lib_func_sleep(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *vi = param;

	if (param == NULL) {
		return -2;
	}
	if (vi->v->type != TYPE_INTEGER && vi->v->type != TYPE_FLOAT) {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a number"));
		} else {
			lstrcpy(ErrStr, TEXT("引数は数値のみです。"));
		}
		return -1;
	}
	Sleep(VariableToInt(vi));
	return 0;
}

/*
 * _lib_func_rand - 乱数取得
 */
int SFUNC _lib_func_rand(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	ret->v->u.iValue = rand();
	ret->v->type = TYPE_INTEGER;
	return 0;
}

/*
 * _lib_func_running_time - 実行時間の取得
 */
int SFUNC _lib_func_running_time(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x11', TEXT("running_time"), recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_temperature - 温度の取得
 */
int SFUNC _lib_func_temperature(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x11', TEXT("temperature"), recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_light_level - 明るさレベルの取得
 */
int SFUNC _lib_func_light_level(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("display.read_light_level"), recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_led_show_string - 文字列の表示
 */
int SFUNC _lib_func_led_show_string(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *vi = param;
	TCHAR *str;
	TCHAR recv[BUF_SIZE];

	if (param == NULL) {
		return -2;
	}
	if (vi->v->type != TYPE_ARRAY) {
		str = VariableToString(vi);
	} else {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a string or a number"));
		} else {
			lstrcpy(ErrStr, TEXT("引数に配列は指定できません。"));
		}
		return -1;
	}
	if (sendData('\x20', str, recv, ErrStr) == FALSE) {
		return -1;
	}
	return 0;
}

/*
 * _lib_func_led_show_leds - イメージの表示
 */
int SFUNC _lib_func_led_show_leds(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *vi = param;
	VALUEINFO *va;
	TCHAR recv[BUF_SIZE];
	TCHAR *str;
	int i, j = 0;
	int cnt = 0;
	int size = (5 + 1) * 5;

	if (param == NULL) {
		return -2;
	}
	if (vi->v->type != TYPE_ARRAY) {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a array"));
		} else {
			lstrcpy(ErrStr, TEXT("引数は配列のみです。"));
		}
		return -1;
	}
	str = mem_alloc(sizeof(TCHAR) * (size + 1));
	*str = TEXT('\0');
	for (va = param->v->u.array; va != NULL; va = va->next) {
		if (cnt > 0 && cnt % 5 == 0) {
			str[j++] = TEXT(':');
			if (j >= size) {
				break;
			}
		}
		cnt++;
		switch (va->v->type) {
		case TYPE_ARRAY:
			i = 0;
			break;
		case TYPE_STRING:
			i = _ttoi(va->v->u.sValue);
			break;
		default:
			i = VariableToInt(va);
			break;
		}
		str[j++] = TEXT('0') + i;
		if (j >= size) {
			break;
		}
	}
	str[j] = TEXT('\0');
	if (lstrlen(str) == 1) {
		lstrcat(str, TEXT("0"));
	}
	if (sendData('\x21', str, recv, ErrStr) == FALSE) {
		mem_free(&str);
		return -1;
	}
	mem_free(&str);
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_led_show_icon - アイコンの表示
 */
int SFUNC _lib_func_led_show_icon(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *vi = param;
	TCHAR recv[BUF_SIZE];
	TCHAR buf[BUF_SIZE] = { 0 };

	if (param == NULL) {
		return -2;
	}
	if (vi->v->type != TYPE_STRING) {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a string"));
		} else {
			lstrcpy(ErrStr, TEXT("引数は文字列のみです。"));
		}
		return -1;
	}
	if (lstrcmpi(vi->v->u.sValue, TEXT("heart")) == 0) {
		lstrcpy(buf, TEXT("09090:99999:99999:09990:00900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("small_heart")) == 0) {
		lstrcpy(buf, TEXT("00000:09090:09990:00900:00000"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("yes")) == 0) {
		lstrcpy(buf, TEXT("00000:00009:00090:90900:09000"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("no")) == 0) {
		lstrcpy(buf, TEXT("90009:09090:00900:09090:90009"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("happy")) == 0) {
		lstrcpy(buf, TEXT("00000:09090:00000:90009:09990"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("sad")) == 0) {
		lstrcpy(buf, TEXT("00000:09090:00000:09990:90009"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("confused")) == 0) {
		lstrcpy(buf, TEXT("00000:09090:00000:09090:90909"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("angry")) == 0) {
		lstrcpy(buf, TEXT("90009:09090:00000:99999:90909"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("asleep")) == 0) {
		lstrcpy(buf, TEXT("00000:99099:00000:09990:00000"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("surprised")) == 0) {
		lstrcpy(buf, TEXT("09090:00000:00900:09090:00900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("silly")) == 0) {
		lstrcpy(buf, TEXT("90009:00000:99999:00909:00999"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("fabulous")) == 0) {
		lstrcpy(buf, TEXT("99999:99099:00000:09090:09990"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("meh")) == 0) {
		lstrcpy(buf, TEXT("09090:00000:00090:00900:09000"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("tshirt")) == 0) {
		lstrcpy(buf, TEXT("99099:99999:09990:09990:09990"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("rollerskate")) == 0) {
		lstrcpy(buf, TEXT("00099:00099:99999:99999:09090"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("duck")) == 0) {
		lstrcpy(buf, TEXT("09900:99900:09999:09990:00000"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("house")) == 0) {
		lstrcpy(buf, TEXT("00900:09990:99999:09990:09090"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("tortoise")) == 0) {
		lstrcpy(buf, TEXT("00000:09990:99999:09090:00000"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("butterfly")) == 0) {
		lstrcpy(buf, TEXT("99099:99999:00900:99999:99099"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("stickfigure")) == 0) {
		lstrcpy(buf, TEXT("00900:99999:00900:09090:90009"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("ghost")) == 0) {
		lstrcpy(buf, TEXT("99999:90909:99999:99999:90909"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("sword")) == 0) {
		lstrcpy(buf, TEXT("00900:00900:00900:09990:00900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("giraffe")) == 0) {
		lstrcpy(buf, TEXT("99000:09000:09000:09990:09090"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("skull")) == 0) {
		lstrcpy(buf, TEXT("09990:90909:99999:09990:09990"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("umbrella")) == 0) {
		lstrcpy(buf, TEXT("09990:99999:00900:90900:09900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("snake")) == 0) {
		lstrcpy(buf, TEXT("99000:99099:09090:09990:00000"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("rabbit")) == 0) {
		lstrcpy(buf, TEXT("90900:90900:99990:99090:99990"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("cow")) == 0) {
		lstrcpy(buf, TEXT("90009:90009:99999:09990:00900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("quarter_note")) == 0) {
		lstrcpy(buf, TEXT("00900:00900:00900:99900:99900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("eigth_note")) == 0) {
		lstrcpy(buf, TEXT("00900:00990:00909:99900:99900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("pitchfork")) == 0) {
		lstrcpy(buf, TEXT("90909:90909:99999:00900:00900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("target")) == 0) {
		lstrcpy(buf, TEXT("00900:09990:99099:09990:00900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("triangle")) == 0) {
		lstrcpy(buf, TEXT("00000:00900:09090:99999:00000"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("left_triangle")) == 0) {
		lstrcpy(buf, TEXT("90000:99000:90900:90090:99999"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("chessboard")) == 0) {
		lstrcpy(buf, TEXT("09090:90909:09090:90909:09090"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("diamond")) == 0) {
		lstrcpy(buf, TEXT("00900:09090:90009:09090:00900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("small_diamond")) == 0) {
		lstrcpy(buf, TEXT("00000:00900:09090:00900:00000"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("square")) == 0) {
		lstrcpy(buf, TEXT("99999:90009:90009:90009:99999"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("square_small")) == 0) {
		lstrcpy(buf, TEXT("00000:09990:09090:09990:00000"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("scissors")) == 0) {
		lstrcpy(buf, TEXT("99009:99090:00900:99090:99009"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("arrow_n")) == 0) {
		lstrcpy(buf, TEXT("00900:09990:90909:00900:00900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("arrow_ne")) == 0) {
		lstrcpy(buf, TEXT("00999:00099:00909:09000:90000"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("arrow_e")) == 0) {
		lstrcpy(buf, TEXT("00900:00090:99999:00090:00900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("arrow_se")) == 0) {
		lstrcpy(buf, TEXT("90000:09000:00909:00099:00999"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("arrow_s")) == 0) {
		lstrcpy(buf, TEXT("00900:00900:90909:09990:00900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("arrow_sw")) == 0) {
		lstrcpy(buf, TEXT("00009:00090:90900:99000:99900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("arrow_w")) == 0) {
		lstrcpy(buf, TEXT("00900:09000:99999:09000:00900"));
	} else if (lstrcmpi(vi->v->u.sValue, TEXT("arrow_nw")) == 0) {
		lstrcpy(buf, TEXT("99900:99000:90900:00090:00009"));
	} else {
		ret->v->u.iValue = -1;
		ret->v->type = TYPE_INTEGER;
		return 0;
	}
	if (sendData('\x21', buf, recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_led_get_pixel - 指定ピクセルの明るさを取得
 */
int SFUNC _lib_func_led_get_pixel(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *v1;
	VALUEINFO *v2;
	TCHAR recv[BUF_SIZE];
	TCHAR buf[BUF_SIZE];

	if (param == NULL || param->next == NULL) {
		return -2;
	}
	v1 = param;
	v2 = param->next;
	if ((v1->v->type == TYPE_INTEGER || v1->v->type == TYPE_FLOAT) &&
		(v2->v->type == TYPE_INTEGER || v2->v->type == TYPE_FLOAT)) {
		wsprintf(buf, TEXT("%d,%d"), VariableToInt(v1), VariableToInt(v2));
	} else {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a number"));
		} else {
			lstrcpy(ErrStr, TEXT("引数は数値のみです。"));
		}
		return -1;
	}
	if (sendData('\x23', buf, recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_led_set_pixel - 指定ピクセルの明るさを設定
 */
int SFUNC _lib_func_led_set_pixel(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *v1;
	VALUEINFO *v2;
	VALUEINFO *v3;
	TCHAR recv[BUF_SIZE];
	TCHAR buf[BUF_SIZE];

	if (param == NULL || param->next == NULL || param->next->next == NULL) {
		return -2;
	}
	v1 = param;
	v2 = param->next;
	v3 = param->next->next;
	if ((v1->v->type == TYPE_INTEGER || v1->v->type == TYPE_FLOAT) &&
		(v2->v->type == TYPE_INTEGER || v2->v->type == TYPE_FLOAT) &&
		(v3->v->type == TYPE_INTEGER || v3->v->type == TYPE_FLOAT)) {
		wsprintf(buf, TEXT("%d,%d,%d"), VariableToInt(v1), VariableToInt(v2), VariableToInt(v3));
	} else {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a number"));
		} else {
			lstrcpy(ErrStr, TEXT("引数は数値のみです。"));
		}
		return -1;
	}
	if (sendData('\x24', buf, recv, ErrStr) == FALSE) {
		return -1;
	}
	return 0;
}

/*
 * _lib_func_led_on - 画面のオン
 */
int SFUNC _lib_func_led_on(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("display.on"), recv, ErrStr) == FALSE) {
		return -1;
	}
	return 0;
}

/*
 * _lib_func_led_off - 画面のオフ
 */
int SFUNC _lib_func_led_off(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("display.off"), recv, ErrStr) == FALSE) {
		return -1;
	}
	return 0;
}

/*
 * _lib_func_button_a_is_pressed - ボタンAが押されているか
 */
int SFUNC _lib_func_button_a_is_pressed(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("pin5.read_digital"), recv, ErrStr) == FALSE) {
		return -1;
	}
	ret->v->u.iValue = (_ttoi(recv) == 0) ? 1 : 0;
	ret->v->type = TYPE_INTEGER;
	return 0;
}

/*
 * _lib_func_button_a_get_presses - ボタンAが押された回数を取得
 */
int SFUNC _lib_func_button_a_get_presses(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("button_a.get_presses"), recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_button_b_is_pressed - ボタンBが押されているか
 */
int SFUNC _lib_func_button_b_is_pressed(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("pin11.read_digital"), recv, ErrStr) == FALSE) {
		return -1;
	}
	ret->v->u.iValue = (_ttoi(recv) == 0) ? 1 : 0;
	ret->v->type = TYPE_INTEGER;
	return 0;
}

/*
 * _lib_func_button_b_get_presses - ボタンBが押された回数を取得
 */
int SFUNC _lib_func_button_b_get_presses(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("button_b.get_presses"), recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_pin_read_digital - PINのデジタル値の取得
 */
int SFUNC _lib_func_pin_read_digital(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *vi = param;
	TCHAR recv[BUF_SIZE];
	TCHAR *buf;

	if (param == NULL) {
		return -2;
	}
	if (vi->v->type != TYPE_INTEGER && vi->v->type != TYPE_FLOAT) {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a number"));
		} else {
			lstrcpy(ErrStr, TEXT("引数は数値のみです。"));
		}
		return -1;
	}
	buf = mem_alloc(sizeof(TCHAR) * (lstrlen(TEXT("pin.read_digital")) + NUM_BUF_SIZE + 1));
	wsprintf(buf, TEXT("pin%d.read_digital"), VariableToInt(vi));
	if (sendData('\x12', buf, recv, ErrStr) == FALSE) {
		mem_free(&buf);
		return -1;
	}
	mem_free(&buf);
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_pin_write_digital - PINにデジタル値を設定
 */
int SFUNC _lib_func_pin_write_digital(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *v1;
	VALUEINFO *v2;
	TCHAR recv[BUF_SIZE];
	TCHAR *buf;

	if (param == NULL || param->next == NULL) {
		return -2;
	}
	v1 = param;
	v2 = param->next;
	if ((v1->v->type != TYPE_INTEGER && v1->v->type != TYPE_FLOAT) ||
		(v2->v->type != TYPE_INTEGER && v2->v->type != TYPE_FLOAT)) {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a number"));
		} else {
			lstrcpy(ErrStr, TEXT("引数は数値のみです。"));
		}
		return -1;
	}
	buf = mem_alloc(sizeof(TCHAR) * (lstrlen(v1->v->u.sValue) + lstrlen(TEXT("pin.write_digital")) + NUM_BUF_SIZE * 2 + 1));
	wsprintf(buf, TEXT("pin%d.write_digital,%d"), VariableToInt(v1), VariableToInt(v2));
	str_lower(buf);
	if (sendData('\x13', buf, recv, ErrStr) == FALSE) {
		mem_free(&buf);
		return -1;
	}
	mem_free(&buf);
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_pin_read_analog - PINの電圧取得
 */
int SFUNC _lib_func_pin_read_analog(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *vi = param;
	TCHAR recv[BUF_SIZE];
	TCHAR *buf;

	if (param == NULL) {
		return -2;
	}
	if (vi->v->type != TYPE_INTEGER && vi->v->type != TYPE_FLOAT) {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a number"));
		} else {
			lstrcpy(ErrStr, TEXT("引数は数値のみです。"));
		}
		return -1;
	}
	buf = mem_alloc(sizeof(TCHAR) * (lstrlen(TEXT("pin.read_analog")) + NUM_BUF_SIZE + 1));
	wsprintf(buf, TEXT("pin%d.read_analog"), VariableToInt(vi));
	if (sendData('\x12', buf, recv, ErrStr) == FALSE) {
		mem_free(&buf);
		return -1;
	}
	mem_free(&buf);
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_pin_write_analog - PINに電圧を設定
 */
int SFUNC _lib_func_pin_write_analog(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *v1;
	VALUEINFO *v2;
	TCHAR recv[BUF_SIZE];
	TCHAR *buf;

	if (param == NULL || param->next == NULL) {
		return -2;
	}
	v1 = param;
	v2 = param->next;
	if ((v1->v->type != TYPE_INTEGER && v1->v->type != TYPE_FLOAT) ||
		(v2->v->type != TYPE_INTEGER && v2->v->type != TYPE_FLOAT)) {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a number"));
		} else {
			lstrcpy(ErrStr, TEXT("引数は数値のみです。"));
		}
		return -1;
	}
	buf = mem_alloc(sizeof(TCHAR) * (lstrlen(TEXT("pin.write_analog")) + NUM_BUF_SIZE * 2 + 1));
	wsprintf(buf, TEXT("pin%d.write_analog,%d"), VariableToInt(v1), VariableToInt(v2));
	if (sendData('\x13', buf, recv, ErrStr) == FALSE) {
		mem_free(&buf);
		return -1;
	}
	mem_free(&buf);
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_pin_set_analog_period - 出力されるPWM信号の周期をperiodにマイクロ秒単位で設定
 * 有効な最小値は256μs
 */
int SFUNC _lib_func_pin_set_analog_period(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *v1;
	VALUEINFO *v2;
	TCHAR recv[BUF_SIZE];
	TCHAR *buf;

	if (param == NULL || param->next == NULL) {
		return -2;
	}
	v1 = param;
	v2 = param->next;
	if ((v1->v->type != TYPE_INTEGER && v1->v->type != TYPE_FLOAT) ||
		(v2->v->type != TYPE_INTEGER && v2->v->type != TYPE_FLOAT)) {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a number"));
		} else {
			lstrcpy(ErrStr, TEXT("引数は数値のみです。"));
		}
		return -1;
	}
	buf = mem_alloc(sizeof(TCHAR) * (lstrlen(TEXT("pin.set_analog_period_microseconds")) + NUM_BUF_SIZE * 2 + 1));
	wsprintf(buf, TEXT("pin%d.set_analog_period_microseconds,%d"), VariableToInt(v1), VariableToInt(v2));
	if (sendData('\x13', buf, recv, ErrStr) == FALSE) {
		mem_free(&buf);
		return -1;
	}
	mem_free(&buf);
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_calibrate_compass - コンパスの調整処理を開始
 */
int SFUNC _lib_func_calibrate_compass(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x37', NULL, recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_compass_heading - 3軸の磁力から計算された方位を取得
 * 値は時計回りの角度を示す 0 から 360 までの整数で、0 は北となる
 */
int SFUNC _lib_func_compass_heading(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("compass.heading"), recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_magnetic_force_x - x軸の磁場強度を取得
 */
int SFUNC _lib_func_magnetic_force_x(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("compass.get_x"), recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_magnetic_force_y - y軸の磁場強度を取得
 */
int SFUNC _lib_func_magnetic_force_y(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("compass.get_y"), recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_magnetic_force_z - z軸の磁場強度を取得
 */
int SFUNC _lib_func_magnetic_force_z(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("compass.get_z"), recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_magnetic_strength - 磁場の強さをナノテスラで取得
 */
int SFUNC _lib_func_magnetic_strength(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("compass.get_field_strength"), recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_current_gesture - 現在のジェスチャーを取得
 */
int SFUNC _lib_func_current_gesture(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("accelerometer.current_gesture"), recv, ErrStr) == FALSE) {
		return -1;
	}
	ret->v->u.sValue = alloc_copy(recv);
	ret->v->type = TYPE_STRING;
	return 0;
}

/*
 * _lib_func_was_gesture - ジェスチャーを取得
 */
int SFUNC _lib_func_was_gesture(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *vi = param;
	TCHAR recv[BUF_SIZE];

	if (param == NULL) {
		return -2;
	}
	if (vi->v->type != TYPE_STRING) {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a string"));
		} else {
			lstrcpy(ErrStr, TEXT("引数は文字列のみです。"));
		}
		return -1;
	}
	if (sendData('\x41', vi->v->u.sValue, recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_acceleration_x - x軸の加速度を取得
 */
int SFUNC _lib_func_acceleration_x(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("accelerometer.get_x"), recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_acceleration_y - y軸の加速度を取得
 */
int SFUNC _lib_func_acceleration_y(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("accelerometer.get_y"), recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_acceleration_z - z軸の加速度を取得
 */
int SFUNC _lib_func_acceleration_z(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("accelerometer.get_z"), recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_radio_set_group - RADIOで送るグループを設定(0～255)
 */
int SFUNC _lib_func_radio_set_group(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *v1;
	TCHAR recv[BUF_SIZE];
	TCHAR buf[BUF_SIZE];

	if (param == NULL) {
		return -2;
	}
	v1 = param;
	if (v1->v->type == TYPE_INTEGER || v1->v->type == TYPE_FLOAT) {
		wsprintf(buf, TEXT("%d"), VariableToInt(v1));
	} else {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a number"));
		} else {
			lstrcpy(ErrStr, TEXT("引数は数値のみです。"));
		}
		return -1;
	}
	if (radio_on == FALSE) {
		if (sendData('\x12', TEXT("radio.on"), recv, ErrStr) == FALSE) {
			return -1;
		}
		radio_on = TRUE;
	}
	if (sendData('\x50', buf, recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_radio_set_power - RADIOで送る信号強度を設定(0～7)
 */
int SFUNC _lib_func_radio_set_power(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *v1;
	TCHAR recv[BUF_SIZE];
	TCHAR buf[BUF_SIZE];

	if (param == NULL) {
		return -2;
	}
	v1 = param;
	if (v1->v->type == TYPE_INTEGER || v1->v->type == TYPE_FLOAT) {
		wsprintf(buf, TEXT("%d"), VariableToInt(v1));
	} else {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a number"));
		} else {
			lstrcpy(ErrStr, TEXT("引数は数値のみです。"));
		}
		return -1;
	}
	if (radio_on == FALSE) {
		if (sendData('\x12', TEXT("radio.on"), recv, ErrStr) == FALSE) {
			return -1;
		}
		radio_on = TRUE;
	}
	if (sendData('\x51', buf, recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_radio_send_string - RADIOで文字列送信
 */
int SFUNC _lib_func_radio_send_string(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *vi = param;
	TCHAR *str;
	TCHAR recv[BUF_SIZE];

	if (param == NULL) {
		return -2;
	}
	if (vi->v->type != TYPE_ARRAY) {
		str = VariableToString(vi);
	} else {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a string or a number"));
		} else {
			lstrcpy(ErrStr, TEXT("引数に配列は指定できません。"));
		}
		return -1;
	}
	if (radio_on == FALSE) {
		if (sendData('\x12', TEXT("radio.on"), recv, ErrStr) == FALSE) {
			return -1;
		}
		radio_on = TRUE;
	}
	if (sendData('\x52', str, recv, ErrStr) == FALSE) {
		return -1;
	}
	return 0;
}

/*
 * _lib_func_radio_receive_string - RADIOで文字列受信
 */
int SFUNC _lib_func_radio_receive_string(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (radio_on == FALSE) {
		if (sendData('\x12', TEXT("radio.on"), recv, ErrStr) == FALSE) {
			return -1;
		}
		radio_on = TRUE;
	}
	if (sendData('\x53', NULL, recv, ErrStr) == FALSE) {
		return -1;
	}
	ret->v->u.sValue = alloc_copy(recv);
	ret->v->type = TYPE_STRING;
	return 0;
}

/*
 * _lib_func_music_playtone - 指定されたミリ秒間の整数周波数でピッチを再生
 */
int SFUNC _lib_func_music_playtone(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	VALUEINFO *v1;
	TCHAR recv[BUF_SIZE];
	TCHAR buf[BUF_SIZE];

	if (param == NULL) {
		return -2;
	}
	v1 = param;
	if (v1->v->type == TYPE_INTEGER || v1->v->type == TYPE_FLOAT) {
		wsprintf(buf, TEXT("%d"), VariableToInt(v1));
	} else {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Argument must be a number"));
		} else {
			lstrcpy(ErrStr, TEXT("引数は数値のみです。"));
		}
		return -1;
	}
	if (sendData('\x60', buf, recv, ErrStr) == FALSE) {
		return -1;
	}
	StringToNumber(recv, ret);
	return 0;
}

/*
 * _lib_func_music_stop - 演奏の停止
 */
int SFUNC _lib_func_music_stop(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	TCHAR recv[BUF_SIZE];

	if (sendData('\x12', TEXT("music.stop"), recv, ErrStr) == FALSE) {
		return -1;
	}
	return 0;
}

// compatibility
int SFUNC _lib_func_show_string(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	return _lib_func_led_show_string(ei, param, ret, ErrStr);
}

int SFUNC _lib_func_show_leds(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	return _lib_func_led_show_leds(ei, param, ret, ErrStr);
}

int SFUNC _lib_func_show_icon(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	return _lib_func_led_show_icon(ei, param, ret, ErrStr);
}

int SFUNC _lib_func_get_pixel(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	return _lib_func_led_get_pixel(ei, param, ret, ErrStr);
}

int SFUNC _lib_func_set_pixel(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	return _lib_func_led_set_pixel(ei, param, ret, ErrStr);
}

int SFUNC _lib_func_display_on(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	return _lib_func_led_on(ei, param, ret, ErrStr);
}

int SFUNC _lib_func_display_off(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	return _lib_func_led_off(ei, param, ret, ErrStr);
}

int SFUNC _lib_func_read_digital(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	return _lib_func_pin_read_digital(ei, param, ret, ErrStr);
}

int SFUNC _lib_func_write_digital(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	return _lib_func_pin_write_digital(ei, param, ret, ErrStr);
}

int SFUNC _lib_func_read_analog(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	return _lib_func_pin_read_analog(ei, param, ret, ErrStr);
}

int SFUNC _lib_func_write_analog(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	return _lib_func_pin_write_analog(ei, param, ret, ErrStr);
}

int SFUNC _lib_func_set_analog_period(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	return _lib_func_pin_set_analog_period(ei, param, ret, ErrStr);
}
/* End of source */
