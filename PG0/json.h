/*
 * PG0
 *
 * json.h
 */

#ifndef _INC_JSON_H
#define _INC_JSON_H

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE

/* Define */

/* Struct */
typedef enum _JSON_TYPE {
	JSON_NULL = 0,
	JSON_FALSE,
	JSON_TRUE,
	JSON_NUMBER,
	JSON_STRING,
	JSON_ARRAY,
	JSON_OBJECT
} JSON_TYPE;

// value of a JSON document (elements of an array and members of an object are linked by next)
typedef struct _JSON {
	JSON_TYPE type;
	TCHAR *key;
	TCHAR *str;
	double num;
	struct _JSON *child;
	struct _JSON *next;
} JSON;

// JSON text being written
typedef struct _JSON_WRITER {
	TCHAR *buf;
	int len;
	int size;
	BOOL error;
	BOOL first;
} JSON_WRITER;

/* Function Prototypes */
JSON *json_parse(const char *utf8);
void json_free(JSON *js);
JSON *json_get(const JSON *obj, const TCHAR *key);
const TCHAR *json_get_string(const JSON *obj, const TCHAR *key);
double json_get_number(const JSON *obj, const TCHAR *key, const double def);
BOOL json_is_true(const JSON *js);

void json_writer_init(JSON_WRITER *jw);
void json_writer_free(JSON_WRITER *jw);
char *json_writer_to_utf8(JSON_WRITER *jw);
void json_begin_object(JSON_WRITER *jw);
void json_end_object(JSON_WRITER *jw);
void json_add_string(JSON_WRITER *jw, const TCHAR *key, const TCHAR *value);
void json_add_number(JSON_WRITER *jw, const TCHAR *key, const double value);
void json_add_string_array(JSON_WRITER *jw, const TCHAR *key, const TCHAR **values, const int count);

#endif
/* End of source */
