/*
 * PG0
 *
 * online.h
 */

#ifndef _INC_ONLINE_H
#define _INC_ONLINE_H

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE

/* Define */

/* Struct */
// script read from the online storage
typedef struct _ONLINE_SCRIPT {
	TCHAR *code;
	BOOL pg05_mode;
	BOOL has_speed;
	int speed;
} ONLINE_SCRIPT;

/* Function Prototypes */
BOOL online_initialize(const HINSTANCE hInstance);
void online_get_ini(const TCHAR *ini_path);
void online_put_ini(const TCHAR *ini_path);
void online_set_code_view(const LOGFONT *font, const BOOL line_no);
void online_clear(void);
const TCHAR *online_get_name(void);
const TCHAR *online_get_cid(void);
void online_export_cid(void);
BOOL online_open(const HWND hWnd, ONLINE_SCRIPT *script);
BOOL online_history(const HWND hWnd, ONLINE_SCRIPT *script);
BOOL online_save(const HWND hWnd, const TCHAR *code, const TCHAR *default_name, const BOOL pg05_mode, const int speed);
void online_show_diff(const HWND hWnd, const TCHAR *title, const TCHAR *old_code, const TCHAR *new_code);

#endif
/* End of source */
