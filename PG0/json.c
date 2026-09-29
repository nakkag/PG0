/*
 * PG0
 *
 * json.c
 *
 * Minimal JSON parser and writer for the online storage API.
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <tchar.h>
#include <stdio.h>
#include <math.h>
#include <float.h>

#include "script_memory.h"
#include "json.h"

/* Define */
#define JSON_MAX_DEPTH					64
#define WRITER_RESERVE					1024

/* Global Variables */

/* Local Function Prototypes */
static void skip_space(const WCHAR **p);
static TCHAR *parse_string(const WCHAR **p);
static JSON *parse_value(const WCHAR **p, const int depth);

/*
 * skip_space - skip white space
 */
static void skip_space(const WCHAR **p)
{
	while (**p == L' ' || **p == L'\t' || **p == L'\r' || **p == L'\n') {
		(*p)++;
	}
}

/*
 * hex_value - value of a hexadecimal digit (-1 when it is not one)
 */
static int hex_value(const WCHAR c)
{
	if (c >= L'0' && c <= L'9') {
		return c - L'0';
	}
	if (c >= L'a' && c <= L'f') {
		return c - L'a' + 10;
	}
	if (c >= L'A' && c <= L'F') {
		return c - L'A' + 10;
	}
	return -1;
}

/*
 * parse_string - read a string starting at the opening quote
 */
static TCHAR *parse_string(const WCHAR **p)
{
	const WCHAR *s;
	TCHAR *ret, *r;
	int len = 0;

	if (**p != L'"') {
		return NULL;
	}
	// the unescaped string is never longer than the escaped one
	for (s = *p + 1; *s != L'"'; s++) {
		if (*s == L'\0') {
			return NULL;
		}
		if (*s == L'\\' && *(s + 1) != L'\0') {
			s++;
		}
		len++;
	}
	if ((ret = mem_alloc(sizeof(TCHAR) * (len + 1))) == NULL) {
		return NULL;
	}
	r = ret;
	for (s = *p + 1; *s != L'"'; s++) {
		if (*s != L'\\') {
			*(r++) = *s;
			continue;
		}
		s++;
		switch (*s) {
		case L'b': *(r++) = L'\b'; break;
		case L'f': *(r++) = L'\f'; break;
		case L'n': *(r++) = L'\n'; break;
		case L'r': *(r++) = L'\r'; break;
		case L't': *(r++) = L'\t'; break;
		case L'u':
			{
				int i, c = 0;
				for (i = 1; i <= 4; i++) {
					int h = hex_value(*(s + i));
					if (h < 0) {
						mem_free(&ret);
						return NULL;
					}
					c = c * 16 + h;
				}
				*(r++) = (WCHAR)c;
				s += 4;
			}
			break;
		default:
			*(r++) = *s;
			break;
		}
	}
	*r = L'\0';
	*p = s + 1;
	return ret;
}

/*
 * new_value - allocate a value
 */
static JSON *new_value(const JSON_TYPE type)
{
	JSON *js;

	if ((js = mem_calloc(sizeof(JSON))) == NULL) {
		return NULL;
	}
	js->type = type;
	return js;
}

/*
 * parse_container - read an array or an object
 */
static JSON *parse_container(const WCHAR **p, const int depth)
{
	JSON *js, *item, *last = NULL;
	const BOOL object = (**p == L'{');
	const WCHAR close = (object) ? L'}' : L']';

	if ((js = new_value((object) ? JSON_OBJECT : JSON_ARRAY)) == NULL) {
		return NULL;
	}
	(*p)++;
	skip_space(p);
	if (**p == close) {
		(*p)++;
		return js;
	}
	while (1) {
		TCHAR *key = NULL;

		skip_space(p);
		if (object) {
			if ((key = parse_string(p)) == NULL) {
				break;
			}
			skip_space(p);
			if (**p != L':') {
				mem_free(&key);
				break;
			}
			(*p)++;
		}
		if ((item = parse_value(p, depth + 1)) == NULL) {
			mem_free(&key);
			break;
		}
		item->key = key;
		if (last == NULL) {
			js->child = item;
		} else {
			last->next = item;
		}
		last = item;
		skip_space(p);
		if (**p == L',') {
			(*p)++;
			continue;
		}
		if (**p == close) {
			(*p)++;
			return js;
		}
		break;
	}
	json_free(js);
	return NULL;
}

/*
 * parse_value - read a value
 */
static JSON *parse_value(const WCHAR **p, const int depth)
{
	JSON *js;

	if (depth > JSON_MAX_DEPTH) {
		return NULL;
	}
	skip_space(p);
	switch (**p) {
	case L'{':
	case L'[':
		return parse_container(p, depth);

	case L'"':
		if ((js = new_value(JSON_STRING)) == NULL) {
			return NULL;
		}
		if ((js->str = parse_string(p)) == NULL) {
			json_free(js);
			return NULL;
		}
		return js;

	case L't':
		if (wcsncmp(*p, L"true", 4) != 0) {
			return NULL;
		}
		*p += 4;
		return new_value(JSON_TRUE);

	case L'f':
		if (wcsncmp(*p, L"false", 5) != 0) {
			return NULL;
		}
		*p += 5;
		return new_value(JSON_FALSE);

	case L'n':
		if (wcsncmp(*p, L"null", 4) != 0) {
			return NULL;
		}
		*p += 4;
		return new_value(JSON_NULL);

	default:
		{
			WCHAR *end;
			double num;

			if (**p != L'-' && (**p < L'0' || **p > L'9')) {
				return NULL;
			}
			num = wcstod(*p, &end);
			if (end == *p) {
				return NULL;
			}
			*p = end;
			if ((js = new_value(JSON_NUMBER)) == NULL) {
				return NULL;
			}
			js->num = num;
			return js;
		}
	}
}

/*
 * json_parse - parse a JSON text in UTF-8 (NULL when it is not valid)
 */
JSON *json_parse(const char *utf8)
{
	JSON *js;
	WCHAR *wbuf;
	const WCHAR *p;
	int len;

	if (utf8 == NULL) {
		return NULL;
	}
	len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	if (len <= 0 || (wbuf = mem_alloc(sizeof(WCHAR) * len)) == NULL) {
		return NULL;
	}
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wbuf, len);
	p = wbuf;
	if (*p == 0xFEFF) {
		p++;
	}
	js = parse_value(&p, 0);
	if (js != NULL) {
		skip_space(&p);
		if (*p != L'\0') {
			json_free(js);
			js = NULL;
		}
	}
	mem_free(&wbuf);
	return js;
}

/*
 * json_free - free a value and everything under it
 */
void json_free(JSON *js)
{
	JSON *next;

	while (js != NULL) {
		next = js->next;
		json_free(js->child);
		mem_free(&js->key);
		mem_free(&js->str);
		mem_free(&js);
		js = next;
	}
}

/*
 * json_get - member of an object
 */
JSON *json_get(const JSON *obj, const TCHAR *key)
{
	JSON *js;

	if (obj == NULL || obj->type != JSON_OBJECT) {
		return NULL;
	}
	for (js = obj->child; js != NULL; js = js->next) {
		if (js->key != NULL && lstrcmp(js->key, key) == 0) {
			return js;
		}
	}
	return NULL;
}

/*
 * json_get_string - string member of an object (NULL when it is not a string)
 */
const TCHAR *json_get_string(const JSON *obj, const TCHAR *key)
{
	JSON *js = json_get(obj, key);

	if (js == NULL || js->type != JSON_STRING) {
		return NULL;
	}
	return js->str;
}

/*
 * json_get_number - number member of an object
 */
double json_get_number(const JSON *obj, const TCHAR *key, const double def)
{
	JSON *js = json_get(obj, key);

	if (js == NULL || js->type != JSON_NUMBER) {
		return def;
	}
	return js->num;
}

/*
 * json_is_true - truthiness of a value like JavaScript
 */
BOOL json_is_true(const JSON *js)
{
	if (js == NULL) {
		return FALSE;
	}
	switch (js->type) {
	case JSON_TRUE:
	case JSON_ARRAY:
	case JSON_OBJECT:
		return TRUE;
	case JSON_NUMBER:
		return (js->num != 0 && !_isnan(js->num));
	case JSON_STRING:
		return (js->str != NULL && *js->str != TEXT('\0'));
	default:
		return FALSE;
	}
}

/*
 * writer_append - append characters to the text
 */
static void writer_append(JSON_WRITER *jw, const TCHAR *str, const int len)
{
	TCHAR *tmp;

	if (jw->error) {
		return;
	}
	if (jw->len + len + 1 > jw->size) {
		int size = jw->size + len + WRITER_RESERVE;
		if ((tmp = mem_alloc(sizeof(TCHAR) * size)) == NULL) {
			jw->error = TRUE;
			return;
		}
		if (jw->buf != NULL) {
			CopyMemory(tmp, jw->buf, sizeof(TCHAR) * jw->len);
			mem_free(&jw->buf);
		}
		jw->buf = tmp;
		jw->size = size;
	}
	CopyMemory(jw->buf + jw->len, str, sizeof(TCHAR) * len);
	jw->len += len;
	*(jw->buf + jw->len) = TEXT('\0');
}

/*
 * writer_append_string - append a quoted and escaped string
 */
static void writer_append_string(JSON_WRITER *jw, const TCHAR *str)
{
	const TCHAR *p, *s;
	TCHAR esc[8];

	writer_append(jw, TEXT("\""), 1);
	for (p = s = (str != NULL) ? str : TEXT(""); *p != TEXT('\0'); p++) {
		switch (*p) {
		case TEXT('"'): lstrcpy(esc, TEXT("\\\"")); break;
		case TEXT('\\'): lstrcpy(esc, TEXT("\\\\")); break;
		case TEXT('\b'): lstrcpy(esc, TEXT("\\b")); break;
		case TEXT('\f'): lstrcpy(esc, TEXT("\\f")); break;
		case TEXT('\n'): lstrcpy(esc, TEXT("\\n")); break;
		case TEXT('\r'): lstrcpy(esc, TEXT("\\r")); break;
		case TEXT('\t'): lstrcpy(esc, TEXT("\\t")); break;
		default:
			if ((unsigned int)*p >= 0x20) {
				continue;
			}
			wsprintf(esc, TEXT("\\u%04x"), (unsigned int)*p);
			break;
		}
		writer_append(jw, s, (int)(p - s));
		writer_append(jw, esc, lstrlen(esc));
		s = p + 1;
	}
	writer_append(jw, s, (int)(p - s));
	writer_append(jw, TEXT("\""), 1);
}

/*
 * writer_append_key - append the separator and the key of a member
 */
static void writer_append_key(JSON_WRITER *jw, const TCHAR *key)
{
	if (!jw->first) {
		writer_append(jw, TEXT(","), 1);
	}
	jw->first = FALSE;
	writer_append_string(jw, key);
	writer_append(jw, TEXT(":"), 1);
}

/*
 * json_writer_init - start writing a JSON text
 */
void json_writer_init(JSON_WRITER *jw)
{
	ZeroMemory(jw, sizeof(JSON_WRITER));
	jw->first = TRUE;
}

/*
 * json_writer_free - free the text
 */
void json_writer_free(JSON_WRITER *jw)
{
	mem_free(&jw->buf);
	jw->len = jw->size = 0;
}

/*
 * json_writer_to_utf8 - the text in UTF-8 (NULL on error)
 */
char *json_writer_to_utf8(JSON_WRITER *jw)
{
	char *ret;
	int len;

	if (jw->error || jw->buf == NULL) {
		return NULL;
	}
	len = WideCharToMultiByte(CP_UTF8, 0, jw->buf, -1, NULL, 0, NULL, NULL);
	if (len <= 0 || (ret = mem_alloc(len)) == NULL) {
		return NULL;
	}
	WideCharToMultiByte(CP_UTF8, 0, jw->buf, -1, ret, len, NULL, NULL);
	return ret;
}

/*
 * json_begin_object - start an object
 */
void json_begin_object(JSON_WRITER *jw)
{
	writer_append(jw, TEXT("{"), 1);
	jw->first = TRUE;
}

/*
 * json_end_object - end an object
 */
void json_end_object(JSON_WRITER *jw)
{
	writer_append(jw, TEXT("}"), 1);
	jw->first = FALSE;
}

/*
 * json_add_string - add a string member
 */
void json_add_string(JSON_WRITER *jw, const TCHAR *key, const TCHAR *value)
{
	writer_append_key(jw, key);
	writer_append_string(jw, value);
}

/*
 * json_add_number - add a number member
 */
void json_add_number(JSON_WRITER *jw, const TCHAR *key, const double value)
{
	TCHAR buf[64];

	writer_append_key(jw, key);
	if (floor(value) == value && fabs(value) < 1e15) {
		_stprintf_s(buf, 64, TEXT("%.0f"), value);
	} else {
		_stprintf_s(buf, 64, TEXT("%.17g"), value);
	}
	writer_append(jw, buf, lstrlen(buf));
}

/*
 * json_add_string_array - add a member that is an array of strings
 */
void json_add_string_array(JSON_WRITER *jw, const TCHAR *key, const TCHAR **values, const int count)
{
	int i;

	writer_append_key(jw, key);
	writer_append(jw, TEXT("["), 1);
	for (i = 0; i < count; i++) {
		if (i > 0) {
			writer_append(jw, TEXT(","), 1);
		}
		writer_append_string(jw, values[i]);
	}
	writer_append(jw, TEXT("]"), 1);
}
/* End of source */
