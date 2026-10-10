/*
 * PG0
 *
 * http.h
 */

#ifndef _INC_HTTP_H
#define _INC_HTTP_H

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE

/* Define */
#define HTTP_STATUS_TEXT_SIZE			128

// server of the online scripts; the environment variable replaces it
// (pg0.exe sets it from its settings, so that pg0gen.exe started from it uses the same server)
#define ONLINE_DEFAULT_SERVER			TEXT("https://pg0.jp")
#define ONLINE_SERVER_ENV				TEXT("PG0_SERVER")
// cid of the online script in the editor while it runs (lib/net.pg0 connects only with it)
#define ONLINE_CID_ENV					TEXT("PG0_CID")

/* Struct */
// result of a request, posted to the window as LPARAM (free it with http_free_result)
typedef struct _HTTP_RESULT {
	int id;
	int status;
	TCHAR status_text[HTTP_STATUS_TEXT_SIZE];
	char *body;
	LPARAM param;
} HTTP_RESULT;

/* Function Prototypes */
HTTP_RESULT *http_request(const TCHAR *method, const TCHAR *url, const char *body);
BOOL http_request_async(const HWND hWnd, const UINT msg, const int id, const TCHAR *method, const TCHAR *url, const char *body, const LPARAM param);
void http_cancel(const HWND hWnd);
void http_free_result(HTTP_RESULT *res);
TCHAR *http_encode_component(const TCHAR *str);

#endif
/* End of source */
