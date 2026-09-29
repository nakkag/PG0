/*
 * PG0
 *
 * http.c
 *
 * HTTP requests on a worker thread (WinHTTP); the result is posted to a window.
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <tchar.h>
#include <process.h>
#include <winhttp.h>

#include "script_memory.h"
#include "http.h"

#pragma comment(lib, "winhttp.lib")

/* Define */
#define USER_AGENT						L"PG0"
#define TIMEOUT_RESOLVE					10000
#define TIMEOUT_CONNECT					10000
#define TIMEOUT_SEND					30000
#define TIMEOUT_RECEIVE					30000
#define MAX_BODY_SIZE					(32 * 1024 * 1024)

/* Global Variables */
typedef struct _HTTP_REQUEST {
	HWND hWnd;
	UINT msg;
	int id;
	TCHAR *method;
	TCHAR *url;
	char *body;
	LPARAM param;
	BOOL cancelled;
	struct _HTTP_REQUEST *next;
} HTTP_REQUEST;

// requests in flight, so that a window being destroyed can stop the results addressed to it
static CRITICAL_SECTION cs;
static BOOL cs_ready = FALSE;
static HTTP_REQUEST *requests = NULL;

/* Local Function Prototypes */

/*
 * unlink_request - take a request out of the list (with the lock held)
 */
static void unlink_request(const HTTP_REQUEST *req)
{
	HTTP_REQUEST **pp;

	for (pp = &requests; *pp != NULL; pp = &(*pp)->next) {
		if (*pp == req) {
			*pp = req->next;
			break;
		}
	}
}

/*
 * read_body - read the response body
 */
static char *read_body(const HINTERNET hRequest)
{
	char *buf = NULL, *tmp;
	DWORD len = 0, avail, read;

	if ((buf = mem_alloc(1)) == NULL) {
		return NULL;
	}
	while (1) {
		avail = 0;
		if (WinHttpQueryDataAvailable(hRequest, &avail) == FALSE) {
			mem_free(&buf);
			return NULL;
		}
		if (avail == 0) {
			break;
		}
		if (len + avail > MAX_BODY_SIZE) {
			mem_free(&buf);
			return NULL;
		}
		if ((tmp = mem_realloc(buf, len + avail + 1)) == NULL) {
			mem_free(&buf);
			return NULL;
		}
		buf = tmp;
		read = 0;
		if (WinHttpReadData(hRequest, buf + len, avail, &read) == FALSE) {
			mem_free(&buf);
			return NULL;
		}
		len += read;
	}
	*(buf + len) = '\0';
	return buf;
}

/*
 * send_request - send the request and fill in the result (status 0 when the connection fails)
 */
static void send_request(const HTTP_REQUEST *req, HTTP_RESULT *res)
{
	URL_COMPONENTS uc;
	HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
	TCHAR *host = NULL;
	DWORD status = 0, size, body_len;

	ZeroMemory(&uc, sizeof(URL_COMPONENTS));
	uc.dwStructSize = sizeof(URL_COMPONENTS);
	uc.dwSchemeLength = (DWORD)-1;
	uc.dwHostNameLength = (DWORD)-1;
	uc.dwUrlPathLength = (DWORD)-1;
	uc.dwExtraInfoLength = (DWORD)-1;
	if (WinHttpCrackUrl(req->url, 0, 0, &uc) == FALSE) {
		return;
	}
	if ((host = alloc_copy_n(uc.lpszHostName, uc.dwHostNameLength)) == NULL) {
		return;
	}

	hSession = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (hSession == NULL) {
		hSession = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	}
	if (hSession == NULL) {
		goto end;
	}
	WinHttpSetTimeouts(hSession, TIMEOUT_RESOLVE, TIMEOUT_CONNECT, TIMEOUT_SEND, TIMEOUT_RECEIVE);
	if ((hConnect = WinHttpConnect(hSession, host, uc.nPort, 0)) == NULL) {
		goto end;
	}
	// the path and the query are the rest of the URL
	hRequest = WinHttpOpenRequest(hConnect, req->method, (*uc.lpszUrlPath != TEXT('\0')) ? uc.lpszUrlPath : TEXT("/"),
		NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		(uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
	if (hRequest == NULL) {
		goto end;
	}
	body_len = (req->body != NULL) ? lstrlenA(req->body) : 0;
	if (WinHttpSendRequest(hRequest,
		(req->body != NULL) ? L"Content-Type: application/json\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS, (req->body != NULL) ? (DWORD)-1L : 0,
		(req->body != NULL) ? req->body : WINHTTP_NO_REQUEST_DATA, body_len, body_len, 0) == FALSE) {
		goto end;
	}
	if (WinHttpReceiveResponse(hRequest, NULL) == FALSE) {
		goto end;
	}
	size = sizeof(DWORD);
	if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
		&status, &size, WINHTTP_NO_HEADER_INDEX) == FALSE) {
		goto end;
	}
	size = sizeof(res->status_text);
	if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_TEXT, WINHTTP_HEADER_NAME_BY_INDEX,
		res->status_text, &size, WINHTTP_NO_HEADER_INDEX) == FALSE) {
		*res->status_text = TEXT('\0');
	}
	if ((res->body = read_body(hRequest)) == NULL) {
		goto end;
	}
	res->status = (int)status;

end:
	if (hRequest != NULL) {
		WinHttpCloseHandle(hRequest);
	}
	if (hConnect != NULL) {
		WinHttpCloseHandle(hConnect);
	}
	if (hSession != NULL) {
		WinHttpCloseHandle(hSession);
	}
	mem_free(&host);
}

/*
 * free_request - free a request
 */
static void free_request(HTTP_REQUEST *req)
{
	mem_free(&req->method);
	mem_free(&req->url);
	mem_free(&req->body);
	mem_free(&req);
}

/*
 * request_thread - send a request and post the result
 */
static unsigned int __stdcall request_thread(void *arg)
{
	HTTP_REQUEST *req = (HTTP_REQUEST *)arg;
	HTTP_RESULT *res;

	if ((res = mem_calloc(sizeof(HTTP_RESULT))) != NULL) {
		res->id = req->id;
		res->param = req->param;
		send_request(req, res);
		if (res->status == 0) {
			mem_free(&res->body);
		}
	}
	// posted under the lock, so that http_cancel either sees the request or finds the message in the queue
	EnterCriticalSection(&cs);
	unlink_request(req);
	if (res != NULL && (req->cancelled || PostMessage(req->hWnd, req->msg, 0, (LPARAM)res) == FALSE)) {
		http_free_result(res);
	}
	LeaveCriticalSection(&cs);
	free_request(req);
	return 0;
}

/*
 * http_request_async - send a request on a worker thread and post the HTTP_RESULT to the window
 *                      (the first call is made on the window's thread)
 */
BOOL http_request_async(const HWND hWnd, const UINT msg, const int id, const TCHAR *method, const TCHAR *url, const char *body, const LPARAM param)
{
	HTTP_REQUEST *req;
	HANDLE hThread;

	if (!cs_ready) {
		InitializeCriticalSection(&cs);
		cs_ready = TRUE;
	}
	if ((req = mem_calloc(sizeof(HTTP_REQUEST))) == NULL) {
		return FALSE;
	}
	req->hWnd = hWnd;
	req->msg = msg;
	req->id = id;
	req->param = param;
	req->method = alloc_copy(method);
	req->url = alloc_copy(url);
	if (body != NULL) {
		int len = lstrlenA(body) + 1;
		if ((req->body = mem_alloc(len)) != NULL) {
			CopyMemory(req->body, body, len);
		}
	}
	if (req->method == NULL || req->url == NULL || (body != NULL && req->body == NULL)) {
		free_request(req);
		return FALSE;
	}
	EnterCriticalSection(&cs);
	req->next = requests;
	requests = req;
	LeaveCriticalSection(&cs);
	hThread = (HANDLE)_beginthreadex(NULL, 0, request_thread, req, 0, NULL);
	if (hThread == NULL) {
		EnterCriticalSection(&cs);
		unlink_request(req);
		LeaveCriticalSection(&cs);
		free_request(req);
		return FALSE;
	}
	CloseHandle(hThread);
	return TRUE;
}

/*
 * http_cancel - the results of the requests addressed to the window are no longer wanted
 *               (results already in the queue of the window remain to be freed by the caller)
 */
void http_cancel(const HWND hWnd)
{
	HTTP_REQUEST *req;

	if (!cs_ready) {
		return;
	}
	EnterCriticalSection(&cs);
	for (req = requests; req != NULL; req = req->next) {
		if (req->hWnd == hWnd) {
			req->cancelled = TRUE;
		}
	}
	LeaveCriticalSection(&cs);
}

/*
 * http_free_result - free a result
 */
void http_free_result(HTTP_RESULT *res)
{
	if (res == NULL) {
		return;
	}
	mem_free(&res->body);
	mem_free(&res);
}

/*
 * http_encode_component - encode a string like JavaScript's encodeURIComponent
 */
TCHAR *http_encode_component(const TCHAR *str)
{
	static const TCHAR hex[] = TEXT("0123456789ABCDEF");
	unsigned char *utf8, *p;
	TCHAR *ret, *r;
	int len;

	len = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
	if (len <= 0 || (utf8 = mem_alloc(len)) == NULL) {
		return NULL;
	}
	WideCharToMultiByte(CP_UTF8, 0, str, -1, (char *)utf8, len, NULL, NULL);
	if ((ret = mem_alloc(sizeof(TCHAR) * (len * 3 + 1))) == NULL) {
		mem_free(&utf8);
		return NULL;
	}
	for (p = utf8, r = ret; *p != '\0'; p++) {
		if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
			*p == '-' || *p == '_' || *p == '.' || *p == '!' || *p == '~' || *p == '*' ||
			*p == '\'' || *p == '(' || *p == ')') {
			*(r++) = (TCHAR)*p;
		} else {
			*(r++) = TEXT('%');
			*(r++) = hex[*p >> 4];
			*(r++) = hex[*p & 0x0F];
		}
	}
	*r = TEXT('\0');
	mem_free(&utf8);
	return ret;
}
/* End of source */
