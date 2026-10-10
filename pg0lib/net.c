/*
 * PG0 library
 *
 * net.c
 *
 * Online play (net.pg0), the same API as the web version's net.js.
 * Copies of one script stored online (the same cid) meet in a room on the server
 * and pass values to each other over a WebSocket (net_server.js of the web version),
 * so a program run here and the same program in a browser can play together.
 * The cid is the one of the online script in the editor of pg0.exe (PG0_CID), or
 * the one a program built by pg0gen.exe carries; without it netJoin() returns 0.
 * Built into such a program with PG0_STATIC_LIB (lib_static.c).
 */

/* Include Files */
#include <windows.h>
#include <tchar.h>
#include <math.h>
#include <float.h>
#include <winhttp.h>

#include "lib_common.h"
#include "../PG0/json.h"
#include "../PG0/http.h"

#pragma comment(lib, "winhttp.lib")

/* Define */
#define NET_PATH				L"/api/net"
#define NET_USER_AGENT			L"PG0"
#define NET_URL_SIZE			1024
#define NET_CID_SIZE			128
#define NET_RESULT_SIZE			16
// received values kept for netReceive(); past this the oldest are dropped
#define NET_QUEUE				1000
// nesting of the arrays sent and received; deeper values become 0
#define NET_DEPTH				32
// netJoin() waits this long for the server at most
#define NET_JOIN_WAIT			10000
#define NET_TIMEOUT				10000
// at the end of the script the thread of the connection is waited for this long at most
#define NET_END_WAIT			2000
// a message of the server larger than this is dropped
#define NET_RECEIVE_MAX			(4 * 1024 * 1024)
#define NET_RECEIVE_BLOCK		4096
// the same notice at most once a second
#define NET_NOTICE_INTERVAL		1000
// limits until the server tells its own
#define NET_DEFAULT_BYTES		16384
#define NET_DEFAULT_RATE		60

/* Struct */
// a value received from a player
typedef struct _NET_VALUE {
	int from;
	VALUEINFO *v;
	struct _NET_VALUE *next;
} NET_VALUE;

// one connection to the relay and what it has received (net.js: _netConnection)
typedef struct _NET_CONN {
	// the script and the thread of the connection each hold a reference
	LONG refs;
	CRITICAL_SECTION cs;
	HMODULE module;
	HANDLE thread;

	// where to connect (read by the thread)
	TCHAR *host;
	INTERNET_PORT port;
	BOOL secure;
	TCHAR *headers;
	char *join;

	// handles a blocking call of the thread may be using (net_close closes them to stop it)
	HINTERNET hRequest;
	HINTERNET hWebSocket;
	// closed by the script (netLeave, netJoin, the end of the run)
	BOOL closed;
	// the WebSocket is open
	BOOL open;

	// what the server has told
	TCHAR result[NET_RESULT_SIZE];
	int id;
	TCHAR *room;
	int *players;
	int player_count;
	int player_size;
	int limit_bytes;
	int limit_rate;
	NET_VALUE *queue;
	NET_VALUE *queue_last;
	int queue_count;
	NET_VALUE *last;
	// warnings of the server, shown by the script
	BOOL warn_size;
	BOOL warn_rate;

	// when the warnings were shown (script only)
	DWORD warned_size;
	DWORD warned_rate;
} NET_CONN;

// text being built
typedef struct _NET_TEXT {
	TCHAR *buf;
	int len;
	int size;
	BOOL error;
} NET_TEXT;

// notices (net.js: NET_ERROR_*)
typedef enum {
	NET_MSG_CID = 0,
	NET_MSG_CONNECT,
	NET_MSG_FULL,
	NET_MSG_CLOSED,
	NET_MSG_BUSY,
	NET_MSG_SIZE,
	NET_MSG_RATE,
} NET_MSG_ID;

/* Global Variables */
// the connection of the run (used by the script only)
static NET_CONN *net = NULL;

static const TCHAR *msg_jp[] = {
	TEXT("netJoin: オンラインに保存したプログラムでだけ使えます（保存すると cid が付きます）。"),
	TEXT("netJoin: サーバーにつながりませんでした。"),
	TEXT("netJoin: 部屋「{room}」はいっぱいです。"),
	TEXT("netJoin: 部屋「{room}」は締め切られています（ゲームが始まっています）。"),
	TEXT("netJoin: サーバーが混み合っています。しばらくしてからもう一度試してください。"),
	TEXT("netSend: 送る値が大きすぎます（{bytes} バイトまで）。"),
	TEXT("netSend: 送る回数が多すぎるので、一部を送りませんでした（1 秒に {rate} 回まで）。"),
};
static const TCHAR *msg_en[] = {
	TEXT("netJoin: works only in a program saved online (saving gives it a cid)."),
	TEXT("netJoin: could not connect to the server."),
	TEXT("netJoin: room \"{room}\" is full."),
	TEXT("netJoin: room \"{room}\" is closed: the game has started."),
	TEXT("netJoin: the server is busy. Try again later."),
	TEXT("netSend: the value is too large to send (up to {bytes} bytes)."),
	TEXT("netSend: too many values were sent, so some were not ({rate} a second at most)."),
};

/* Local Function Prototypes */
static DWORD WINAPI net_thread(LPVOID arg);

#ifndef PG0_STATIC_LIB
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
#endif

/*
 * is_space - white space as defined by JavaScript's trim()
 */
static BOOL is_space(TCHAR c)
{
	if ((c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0xA0 || c == 0x1680 ||
		(c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
		c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF) {
		return TRUE;
	}
	return FALSE;
}

/*
 * to_int32 - a number as JavaScript's (n | 0)
 */
static int to_int32(double d)
{
	if (!_finite(d)) {
		return 0;
	}
	d = (d < 0) ? ceil(d) : floor(d);
	d = fmod(d, 4294967296.0);
	if (d < 0) {
		d += 4294967296.0;
	}
	if (d >= 2147483648.0) {
		d -= 4294967296.0;
	}
	return (int)d;
}

/*
 * param_int - a parameter as parseInt() of the web version (FALSE when it is NaN)
 */
static BOOL param_int(VALUEINFO *vi, int *n)
{
	double d;

	if (vi == NULL || vi->v == NULL || vi->v->type == TYPE_ARRAY) {
		return FALSE;
	}
	if (vi->v->type == TYPE_INTEGER) {
		*n = vi->v->u.iValue;
		return TRUE;
	}
	d = (vi->v->type == TYPE_STRING) ? lib_string_to_number(vi->v->u.sValue) : vi->v->u.fValue;
	if (!_finite(d)) {
		return FALSE;
	}
	*n = lib_clamp_int(d);
	return TRUE;
}

/*
 * text_add_n - append characters
 */
static void text_add_n(NET_TEXT *t, const TCHAR *str, const int len)
{
	TCHAR *nb;
	int size;

	if (t->error || len <= 0) {
		return;
	}
	if (t->len + len + 1 > t->size) {
		size = (t->len + len + 1) * 2;
		if (size < 256) {
			size = 256;
		}
		nb = (t->buf == NULL) ? mem_alloc(sizeof(TCHAR) * size) : mem_realloc(t->buf, sizeof(TCHAR) * size);
		if (nb == NULL) {
			t->error = TRUE;
			return;
		}
		t->buf = nb;
		t->size = size;
	}
	CopyMemory(t->buf + t->len, str, sizeof(TCHAR) * len);
	t->len += len;
	t->buf[t->len] = TEXT('\0');
}

/*
 * text_add - append a string
 */
static void text_add(NET_TEXT *t, const TCHAR *str)
{
	text_add_n(t, str, lstrlen(str));
}

/*
 * text_add_string - append a string in quotes, escaped as JavaScript's JSON.stringify()
 */
static void text_add_string(NET_TEXT *t, const TCHAR *str)
{
	const TCHAR *p, *s;
	TCHAR esc[8];

	text_add(t, TEXT("\""));
	for (s = p = str; *p != TEXT('\0'); p++) {
		*esc = TEXT('\0');
		switch (*p) {
		case TEXT('"'):
			lstrcpy(esc, TEXT("\\\""));
			break;
		case TEXT('\\'):
			lstrcpy(esc, TEXT("\\\\"));
			break;
		case TEXT('\b'):
			lstrcpy(esc, TEXT("\\b"));
			break;
		case TEXT('\f'):
			lstrcpy(esc, TEXT("\\f"));
			break;
		case TEXT('\n'):
			lstrcpy(esc, TEXT("\\n"));
			break;
		case TEXT('\r'):
			lstrcpy(esc, TEXT("\\r"));
			break;
		case TEXT('\t'):
			lstrcpy(esc, TEXT("\\t"));
			break;
		default:
			if (*p >= 0xD800 && *p <= 0xDBFF && *(p + 1) >= 0xDC00 && *(p + 1) <= 0xDFFF) {
				// a surrogate pair as it is
				p++;
			} else if (*p < 0x20 || (*p >= 0xD800 && *p <= 0xDFFF)) {
				// a lone surrogate would not survive UTF-8
				wsprintf(esc, TEXT("\\u%04x"), *p);
			}
			break;
		}
		if (*esc != TEXT('\0')) {
			text_add_n(t, s, (int)(p - s));
			text_add(t, esc);
			s = p + 1;
		}
	}
	text_add_n(t, s, (int)(p - s));
	text_add(t, TEXT("\""));
}

/*
 * format_float - the shortest text that reads back as the same number (non-finite numbers are 0)
 */
static void format_float(TCHAR *buf, const int size, const double d)
{
	int prec;

	if (!_finite(d) || d == 0) {
		lstrcpy(buf, TEXT("0"));
		return;
	}
	for (prec = 15; prec <= 17; prec++) {
		_stprintf_s(buf, size, TEXT("%.*g"), prec, d);
		if (_tcstod(buf, NULL) == d) {
			break;
		}
	}
}

/*
 * text_add_value - append a value as the web version's interpreter keeps it
 *                  ({"type", "num" | "str" | "array"}, net.js: _netValue)
 */
static void text_add_value(NET_TEXT *t, VALUEINFO *vi, const int depth)
{
	VALUEINFO *e;
	TCHAR buf[FLOAT_LENGTH + 32];
	const TCHAR *name;

	if (vi == NULL || vi->v == NULL || depth > NET_DEPTH) {
		text_add(t, TEXT("{\"type\":0,\"num\":0}"));
		return;
	}
	switch (vi->v->type) {
	case TYPE_FLOAT:
		text_add(t, TEXT("{\"type\":1,\"num\":"));
		format_float(buf, FLOAT_LENGTH + 32, vi->v->u.fValue);
		text_add(t, buf);
		text_add(t, TEXT("}"));
		break;

	case TYPE_STRING:
		text_add(t, TEXT("{\"type\":2,\"str\":"));
		text_add_string(t, (vi->v->u.sValue != NULL) ? vi->v->u.sValue : TEXT(""));
		text_add(t, TEXT("}"));
		break;

	case TYPE_ARRAY:
		text_add(t, TEXT("{\"type\":3,\"array\":["));
		for (e = vi->v->u.array; e != NULL; e = e->next) {
			if (e != vi->v->u.array) {
				text_add(t, TEXT(","));
			}
			// the key as it was written
			name = (e->org_name != NULL) ? e->org_name : ((e->name != NULL) ? e->name : TEXT(""));
			text_add(t, TEXT("{\"name\":"));
			text_add_string(t, name);
			text_add(t, TEXT(",\"v\":"));
			text_add_value(t, e, depth + 1);
			text_add(t, TEXT("}"));
		}
		text_add(t, TEXT("]}"));
		break;

	default:
		wsprintf(buf, TEXT("{\"type\":0,\"num\":%d}"), vi->v->u.iValue);
		text_add(t, buf);
		break;
	}
}

/*
 * to_utf8 - a string in UTF-8
 */
static char *to_utf8(const TCHAR *str)
{
	char *buf;
	int len;

	len = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
	if (len <= 0 || (buf = mem_alloc(len)) == NULL) {
		return NULL;
	}
	WideCharToMultiByte(CP_UTF8, 0, str, -1, buf, len, NULL, NULL);
	return buf;
}

/*
 * set_value - a value as the interpreter keeps it, rebuilt from whatever arrived
 *             (net.js: _netValue): anything else becomes 0, and nesting is bounded
 */
static void set_value(VALUEINFO *vi, const JSON *j, const int depth)
{
	const JSON *m, *e;
	VALUEINFO *top = NULL, *last = NULL, *ev;

	lib_set_int(vi, 0);
	if (j == NULL || j->type != JSON_OBJECT || depth > NET_DEPTH) {
		return;
	}
	m = json_get(j, TEXT("type"));
	if (m == NULL || m->type != JSON_NUMBER || m->num != floor(m->num) || m->num < TYPE_INTEGER || m->num > TYPE_ARRAY) {
		return;
	}
	switch ((int)m->num) {
	case TYPE_INTEGER:
		m = json_get(j, TEXT("num"));
		lib_set_int(vi, (m != NULL && m->type == JSON_NUMBER) ? to_int32(m->num) : 0);
		break;

	case TYPE_FLOAT:
		m = json_get(j, TEXT("num"));
		lib_set_float(vi, (m != NULL && m->type == JSON_NUMBER && _finite(m->num)) ? m->num : 0);
		break;

	case TYPE_STRING:
		lib_set_string(vi, json_get_string(j, TEXT("str")));
		break;

	case TYPE_ARRAY:
		m = json_get(j, TEXT("array"));
		for (e = (m != NULL && m->type == JSON_ARRAY) ? m->child : NULL; e != NULL; e = e->next) {
			if ((ev = AllocValue()) == NULL) {
				break;
			}
			if (e->type == JSON_OBJECT) {
				lib_set_name(ev, json_get_string(e, TEXT("name")));
				set_value(ev, json_get(e, TEXT("v")), depth + 1);
			}
			lib_list_append(&top, &last, ev);
		}
		lib_set_array(vi, top);
		break;
	}
}

/*
 * copy_value - set a copy of a value
 */
static void copy_value(VALUEINFO *to, VALUEINFO *from)
{
	switch (from->v->type) {
	case TYPE_ARRAY:
		lib_set_array(to, CopyValueList(from->v->u.array));
		break;
	case TYPE_STRING:
		lib_set_string(to, from->v->u.sValue);
		break;
	case TYPE_FLOAT:
		lib_set_float(to, from->v->u.fValue);
		break;
	default:
		lib_set_int(to, from->v->u.iValue);
		break;
	}
}

/*
 * free_values - free a list of received values
 */
static void free_values(NET_VALUE *nv)
{
	NET_VALUE *next;

	for (; nv != NULL; nv = next) {
		next = nv->next;
		FreeValue(nv->v);
		mem_free(&nv);
	}
}

/*
 * net_release - drop a reference to a connection (freed with the last one)
 */
static void net_release(NET_CONN *c)
{
	if (c == NULL || InterlockedDecrement(&c->refs) > 0) {
		return;
	}
	DeleteCriticalSection(&c->cs);
	if (c->thread != NULL) {
		CloseHandle(c->thread);
	}
	mem_free(&c->host);
	mem_free(&c->headers);
	mem_free(&c->join);
	mem_free(&c->room);
	mem_free(&c->players);
	free_values(c->queue);
	free_values(c->last);
	mem_free(&c);
}

/*
 * net_close - close the connection (net.js: me.close); what was received stays
 */
static void net_close(NET_CONN *c)
{
	HINTERNET hRequest, hWebSocket;

	if (c == NULL) {
		return;
	}
	EnterCriticalSection(&c->cs);
	c->closed = TRUE;
	c->open = FALSE;
	c->id = 0;
	c->player_count = 0;
	hRequest = c->hRequest;
	hWebSocket = c->hWebSocket;
	c->hRequest = NULL;
	c->hWebSocket = NULL;
	LeaveCriticalSection(&c->cs);
	// a blocking call of the thread on these handles returns
	if (hWebSocket != NULL) {
		WinHttpCloseHandle(hWebSocket);
	}
	if (hRequest != NULL) {
		WinHttpCloseHandle(hRequest);
	}
}

/*
 * net_hold - the thread is about to block on a handle (FALSE: the script has closed the connection)
 */
static BOOL net_hold(NET_CONN *c, HINTERNET *slot, HINTERNET h)
{
	EnterCriticalSection(&c->cs);
	if (c->closed) {
		LeaveCriticalSection(&c->cs);
		WinHttpCloseHandle(h);
		return FALSE;
	}
	*slot = h;
	LeaveCriticalSection(&c->cs);
	return TRUE;
}

/*
 * net_drop - the thread is done with a handle (net_close may have closed it already)
 */
static void net_drop(NET_CONN *c, HINTERNET *slot, HINTERNET h)
{
	BOOL owned;

	EnterCriticalSection(&c->cs);
	owned = (*slot == h);
	if (owned) {
		*slot = NULL;
	}
	LeaveCriticalSection(&c->cs);
	if (owned) {
		WinHttpCloseHandle(h);
	}
}

/*
 * net_closed - the script has closed the connection
 */
static BOOL net_closed(NET_CONN *c)
{
	BOOL closed;

	EnterCriticalSection(&c->cs);
	closed = c->closed;
	LeaveCriticalSection(&c->cs);
	return closed;
}

/*
 * add_player - a player entered (the numbers are kept in order)
 */
static void add_player(NET_CONN *c, const int id)
{
	int *np;
	int i, j;

	for (i = 0; i < c->player_count; i++) {
		if (c->players[i] == id) {
			return;
		}
	}
	if (c->player_count >= c->player_size) {
		np = (c->players == NULL) ? mem_alloc(sizeof(int) * (c->player_size + 8)) :
			mem_realloc(c->players, sizeof(int) * (c->player_size + 8));
		if (np == NULL) {
			return;
		}
		c->players = np;
		c->player_size += 8;
	}
	for (i = 0; i < c->player_count && c->players[i] < id; i++);
	for (j = c->player_count; j > i; j--) {
		c->players[j] = c->players[j - 1];
	}
	c->players[i] = id;
	c->player_count++;
}

/*
 * remove_last - take out the last value of a player
 */
static NET_VALUE *remove_last(NET_CONN *c, const int from)
{
	NET_VALUE *nv, *prev = NULL;

	for (nv = c->last; nv != NULL; prev = nv, nv = nv->next) {
		if (nv->from == from) {
			if (prev == NULL) {
				c->last = nv->next;
			} else {
				prev->next = nv->next;
			}
			nv->next = NULL;
			return nv;
		}
	}
	return NULL;
}

/*
 * on_joined - in the room
 */
static void on_joined(NET_CONN *c, const JSON *js)
{
	const JSON *m, *e;
	const TCHAR *room;
	int i;

	EnterCriticalSection(&c->cs);
	c->id = to_int32(json_get_number(js, TEXT("id"), 0));
	mem_free(&c->room);
	room = json_get_string(js, TEXT("room"));
	c->room = alloc_copy((room != NULL) ? room : TEXT(""));
	c->player_count = 0;
	m = json_get(js, TEXT("players"));
	if (m != NULL && m->type == JSON_ARRAY) {
		for (e = m->child; e != NULL; e = e->next) {
			i = (e->type == JSON_NUMBER) ? to_int32(e->num) : 0;
			add_player(c, i);
		}
	} else {
		add_player(c, c->id);
	}
	m = json_get(js, TEXT("limits"));
	if (json_is_true(m)) {
		c->limit_bytes = to_int32(json_get_number(m, TEXT("bytes"), 0));
		c->limit_rate = to_int32(json_get_number(m, TEXT("rate"), 0));
	}
	lstrcpy(c->result, TEXT("joined"));
	LeaveCriticalSection(&c->cs);
}

/*
 * on_exit - a player left
 */
static void on_exit(NET_CONN *c, const int id)
{
	NET_VALUE *nv;
	int i, j;

	EnterCriticalSection(&c->cs);
	for (i = j = 0; i < c->player_count; i++) {
		if (c->players[i] != id) {
			c->players[j++] = c->players[i];
		}
	}
	c->player_count = j;
	nv = remove_last(c, id);
	LeaveCriticalSection(&c->cs);
	free_values(nv);
}

/*
 * on_message - a value from a player: the last one of the player, and in the queue
 */
static void on_message(NET_CONN *c, const JSON *js)
{
	NET_VALUE *nv, *lv, *old, *drop = NULL;

	nv = mem_calloc(sizeof(NET_VALUE));
	lv = mem_calloc(sizeof(NET_VALUE));
	if (nv == NULL || lv == NULL || (nv->v = AllocValue()) == NULL || (lv->v = AllocValue()) == NULL) {
		free_values(nv);
		free_values(lv);
		return;
	}
	nv->from = lv->from = to_int32(json_get_number(js, TEXT("from"), 0));
	set_value(nv->v, json_get(js, TEXT("d")), 0);
	copy_value(lv->v, nv->v);

	EnterCriticalSection(&c->cs);
	old = remove_last(c, lv->from);
	lv->next = c->last;
	c->last = lv;
	if (c->queue == NULL) {
		c->queue = nv;
	} else {
		c->queue_last->next = nv;
	}
	c->queue_last = nv;
	if (++c->queue_count > NET_QUEUE) {
		drop = c->queue;
		c->queue = drop->next;
		drop->next = NULL;
		c->queue_count--;
	}
	LeaveCriticalSection(&c->cs);
	free_values(old);
	free_values(drop);
}

/*
 * on_error - the answer to netJoin(), or a warning about what was sent
 */
static void on_error(NET_CONN *c, const JSON *js)
{
	const TCHAR *code = json_get_string(js, TEXT("code"));

	EnterCriticalSection(&c->cs);
	if (*c->result == TEXT('\0')) {
		lstrcpyn(c->result, (code != NULL && *code != TEXT('\0')) ? code : TEXT("join"), NET_RESULT_SIZE);
	} else if (code != NULL && _tcscmp(code, TEXT("size")) == 0) {
		c->warn_size = TRUE;
	} else if (code != NULL && _tcscmp(code, TEXT("rate")) == 0) {
		c->warn_rate = TRUE;
	}
	LeaveCriticalSection(&c->cs);
}

/*
 * on_receive - a message of the server (net.js: me.receive)
 */
static void on_receive(NET_CONN *c, const char *text)
{
	JSON *js;
	const TCHAR *t;
	int id;

	js = json_parse(text);
	if (js == NULL || js->type != JSON_OBJECT || (t = json_get_string(js, TEXT("t"))) == NULL) {
		json_free(js);
		return;
	}
	if (_tcscmp(t, TEXT("joined")) == 0) {
		on_joined(c, js);
	} else if (_tcscmp(t, TEXT("enter")) == 0) {
		id = to_int32(json_get_number(js, TEXT("id"), 0));
		EnterCriticalSection(&c->cs);
		add_player(c, id);
		LeaveCriticalSection(&c->cs);
	} else if (_tcscmp(t, TEXT("exit")) == 0) {
		on_exit(c, to_int32(json_get_number(js, TEXT("id"), 0)));
	} else if (_tcscmp(t, TEXT("msg")) == 0) {
		on_message(c, js);
	} else if (_tcscmp(t, TEXT("error")) == 0) {
		on_error(c, js);
	}
	json_free(js);
}

/*
 * receive_loop - receive the messages of the server until the connection ends
 */
static void receive_loop(NET_CONN *c, HINTERNET hWebSocket)
{
	WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
	char *buf, *nb;
	DWORD len = 0, size = NET_RECEIVE_BLOCK, read;
	BOOL drop = FALSE;

	if ((buf = mem_alloc(size + 1)) == NULL) {
		return;
	}
	while (!net_closed(c)) {
		if (size - len < NET_RECEIVE_BLOCK) {
			if (size >= NET_RECEIVE_MAX) {
				// too large: the rest of the message is read and dropped
				drop = TRUE;
				len = 0;
			} else if ((nb = mem_realloc(buf, size * 2 + 1)) != NULL) {
				buf = nb;
				size *= 2;
			} else {
				drop = TRUE;
				len = 0;
			}
		}
		read = 0;
		if (WinHttpWebSocketReceive(hWebSocket, buf + len, size - len, &read, &type) != NO_ERROR ||
			type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
			break;
		}
		len += read;
		if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE) {
			continue;
		}
		if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE && !drop) {
			buf[len] = '\0';
			on_receive(c, buf);
		}
		len = 0;
		drop = FALSE;
	}
	mem_free(&buf);
}

/*
 * net_thread - connect, enter the room and receive (net.js: the WebSocket and its events)
 */
static DWORD WINAPI net_thread(LPVOID arg)
{
	NET_CONN *c = (NET_CONN *)arg;
#ifndef PG0_STATIC_LIB
	HMODULE hModule = c->module;
#endif
	HINTERNET hSession, hConnect = NULL, hRequest = NULL, hWebSocket = NULL;
	DWORD status = 0, size = sizeof(DWORD);

	hSession = WinHttpOpen(NET_USER_AGENT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (hSession == NULL) {
		hSession = WinHttpOpen(NET_USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	}
	if (hSession != NULL) {
		WinHttpSetTimeouts(hSession, NET_TIMEOUT, NET_TIMEOUT, NET_TIMEOUT, NET_TIMEOUT);
		hConnect = WinHttpConnect(hSession, c->host, c->port, 0);
	}
	if (hConnect != NULL) {
		hRequest = WinHttpOpenRequest(hConnect, L"GET", NET_PATH, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
			(c->secure) ? WINHTTP_FLAG_SECURE : 0);
	}
	if (hRequest != NULL && net_hold(c, &c->hRequest, hRequest)) {
		if (WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) &&
			WinHttpSendRequest(hRequest, c->headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
			WinHttpReceiveResponse(hRequest, NULL) &&
			WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
				&status, &size, WINHTTP_NO_HEADER_INDEX) &&
			status == HTTP_STATUS_SWITCH_PROTOCOLS && !net_closed(c)) {
			hWebSocket = WinHttpWebSocketCompleteUpgrade(hRequest, (DWORD_PTR)NULL);
		}
		net_drop(c, &c->hRequest, hRequest);
	}
	if (hWebSocket != NULL && net_hold(c, &c->hWebSocket, hWebSocket)) {
		EnterCriticalSection(&c->cs);
		c->open = !c->closed;
		LeaveCriticalSection(&c->cs);
		if (WinHttpWebSocketSend(hWebSocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, c->join, lstrlenA(c->join)) == NO_ERROR) {
			receive_loop(c, hWebSocket);
		}
		net_drop(c, &c->hWebSocket, hWebSocket);
	}

	EnterCriticalSection(&c->cs);
	c->open = FALSE;
	c->id = 0;
	c->player_count = 0;
	if (*c->result == TEXT('\0')) {
		lstrcpy(c->result, TEXT("connect"));
	}
	LeaveCriticalSection(&c->cs);
	if (hConnect != NULL) {
		WinHttpCloseHandle(hConnect);
	}
	if (hSession != NULL) {
		WinHttpCloseHandle(hSession);
	}
	net_release(c);
#ifndef PG0_STATIC_LIB
	// the library stays loaded until here, even when the script has ended
	FreeLibraryAndExitThread(hModule, 0);
#endif
	return 0;
}

/*
 * net_new - a connection to the server of the online scripts (not started yet)
 */
static NET_CONN *net_new(const TCHAR *cid, const TCHAR *room, const int max)
{
	NET_CONN *c;
	URL_COMPONENTS uc;
	NET_TEXT t;
	TCHAR server[NET_URL_SIZE];
	TCHAR buf[INT_LENGTH + 16];
	DWORD len;

	if ((c = mem_calloc(sizeof(NET_CONN))) == NULL) {
		return NULL;
	}
	InitializeCriticalSection(&c->cs);
	c->refs = 1;
	c->limit_bytes = NET_DEFAULT_BYTES;
	c->limit_rate = NET_DEFAULT_RATE;

	// the server pg0.exe uses for the online scripts
	len = GetEnvironmentVariable(ONLINE_SERVER_ENV, server, NET_URL_SIZE);
	if (len == 0 || len >= NET_URL_SIZE) {
		lstrcpy(server, ONLINE_DEFAULT_SERVER);
	}
	ZeroMemory(&uc, sizeof(URL_COMPONENTS));
	uc.dwStructSize = sizeof(URL_COMPONENTS);
	uc.dwSchemeLength = (DWORD)-1;
	uc.dwHostNameLength = (DWORD)-1;
	if (WinHttpCrackUrl(server, 0, 0, &uc) && uc.dwHostNameLength > 0 &&
		(uc.nScheme == INTERNET_SCHEME_HTTPS || uc.nScheme == INTERNET_SCHEME_HTTP)) {
		c->host = alloc_copy_n(uc.lpszHostName, uc.dwHostNameLength);
		c->port = uc.nPort;
		c->secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
		// the server accepts its own pages only, so the request comes as one of them
		ZeroMemory(&t, sizeof(NET_TEXT));
		text_add(&t, (c->secure) ? TEXT("Origin: https://") : TEXT("Origin: http://"));
		text_add_n(&t, uc.lpszHostName, uc.dwHostNameLength);
		if (c->port != ((c->secure) ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT)) {
			wsprintf(buf, TEXT(":%u"), c->port);
			text_add(&t, buf);
		}
		text_add(&t, TEXT("\r\n"));
		c->headers = (t.error) ? NULL : t.buf;
		if (t.error) {
			mem_free(&t.buf);
		}
	}

	// {"t": "join", "cid": "...", "room": "name" | "", "max": 2}
	ZeroMemory(&t, sizeof(NET_TEXT));
	text_add(&t, TEXT("{\"t\":\"join\",\"cid\":"));
	text_add_string(&t, cid);
	text_add(&t, TEXT(",\"room\":"));
	text_add_string(&t, room);
	wsprintf(buf, TEXT(",\"max\":%d}"), max);
	text_add(&t, buf);
	if (!t.error) {
		c->join = to_utf8(t.buf);
	}
	mem_free(&t.buf);
	return c;
}

/*
 * net_start - start the thread of the connection
 */
static BOOL net_start(NET_CONN *c)
{
	if (c->host == NULL || c->headers == NULL || c->join == NULL) {
		return FALSE;
	}
#ifndef PG0_STATIC_LIB
	// the thread keeps the library loaded until it ends, which may be after the run
	if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCTSTR)net_thread, &c->module)) {
		return FALSE;
	}
#endif
	InterlockedIncrement(&c->refs);
	c->thread = CreateThread(NULL, 0, net_thread, c, 0, NULL);
	if (c->thread == NULL) {
		InterlockedDecrement(&c->refs);
		if (c->module != NULL) {
			FreeLibrary(c->module);
		}
		return FALSE;
	}
	return TRUE;
}

/*
 * net_current - the connection when it is in a room, with its lock held (net.js: _netCurrent)
 */
static NET_CONN *net_current(void)
{
	if (net == NULL) {
		return NULL;
	}
	EnterCriticalSection(&net->cs);
	if (net->open && net->id != 0) {
		return net;
	}
	LeaveCriticalSection(&net->cs);
	return NULL;
}

/*
 * net_notice - show a notice in the console like an error, without stopping the script
 *              ("{key}" is replaced by the value)
 */
static void net_notice(EXECINFO *ei, const NET_MSG_ID id, const TCHAR *key, const TCHAR *value)
{
	const TCHAR *text, *p = NULL;
	TCHAR mark[NET_RESULT_SIZE];
	NET_TEXT t;
	VALUEINFO *param;

	text = (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) == LANG_JAPANESE) ? msg_jp[id] : msg_en[id];
	ZeroMemory(&t, sizeof(NET_TEXT));
	if (key != NULL) {
		wsprintf(mark, TEXT("{%s}"), key);
		p = _tcsstr(text, mark);
	}
	if (p != NULL) {
		text_add_n(&t, text, (int)(p - text));
		text_add(&t, value);
		text_add(&t, p + lstrlen(mark));
	} else {
		text_add(&t, text);
	}
	if (!t.error && (param = lib_new_string(NULL, t.buf)) != NULL) {
		lib_call_function(ei, TEXT("error"), param);
		FreeValueList(param);
	}
	mem_free(&t.buf);
}

/*
 * net_warn - a warning about what was sent, the same one at most once a second (net.js: me.warn)
 */
static void net_warn(EXECINFO *ei, NET_CONN *c, const NET_MSG_ID id)
{
	DWORD *warned = (id == NET_MSG_SIZE) ? &c->warned_size : &c->warned_rate;
	TCHAR buf[INT_LENGTH];
	int limit;

	if (*warned != 0 && GetTickCount() - *warned <= NET_NOTICE_INTERVAL) {
		return;
	}
	*warned = GetTickCount();
	if (*warned == 0) {
		*warned = 1;
	}
	EnterCriticalSection(&c->cs);
	limit = (id == NET_MSG_SIZE) ? c->limit_bytes : c->limit_rate;
	LeaveCriticalSection(&c->cs);
	wsprintf(buf, TEXT("%d"), limit);
	net_notice(ei, id, (id == NET_MSG_SIZE) ? TEXT("bytes") : TEXT("rate"), buf);
}

/*
 * net_server_warnings - show the warnings the server has sent since the last call
 */
static void net_server_warnings(EXECINFO *ei)
{
	BOOL size, rate;

	if (net == NULL) {
		return;
	}
	EnterCriticalSection(&net->cs);
	size = net->warn_size;
	rate = net->warn_rate;
	net->warn_size = FALSE;
	net->warn_rate = FALSE;
	LeaveCriticalSection(&net->cs);
	if (size) {
		net_warn(ei, net, NET_MSG_SIZE);
	}
	if (rate) {
		net_warn(ei, net, NET_MSG_RATE);
	}
}

/*
 * net_send - send a message to the server (FALSE: not in a room, or it could not be sent)
 */
static BOOL net_send(const TCHAR *text)
{
	NET_CONN *c;
	HINTERNET hWebSocket;
	char *utf8;
	DWORD ret;

	if ((c = net_current()) == NULL) {
		return FALSE;
	}
	hWebSocket = c->hWebSocket;
	LeaveCriticalSection(&c->cs);
	if (hWebSocket == NULL || (utf8 = to_utf8(text)) == NULL) {
		return FALSE;
	}
	ret = WinHttpWebSocketSend(hWebSocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, utf8, lstrlenA(utf8));
	mem_free(&utf8);
	return (ret == NO_ERROR);
}

/*
 * net_unload - leave the room at the end of the script
 */
static void net_unload(void)
{
	if (net != NULL) {
		net_close(net);
		// the thread ends soon after its handles are closed, and frees what it holds
		if (net->thread != NULL) {
			WaitForSingleObject(net->thread, NET_END_WAIT);
		}
	}
	net_release(net);
	net = NULL;
}

#ifdef PG0_STATIC_LIB
/*
 * net_lib_term - the library built into a program: called when the script has ended
 */
void net_lib_term(void)
{
	net_unload();
}
#else
/*
 * _lib_unload - called by PG0 when the script ends: leave the room
 */
void SFUNC _lib_unload(void)
{
	net_unload();
}
#endif

/*
 * _lib_func_netjoin - enter a room of this script: netJoin(room = "", players = 2)
 *                     returns the player number (1, 2, ...), or 0 when it could not
 */
int SFUNC _lib_func_netjoin(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	NET_CONN *c;
	VALUEINFO *vi;
	TCHAR cid[NET_CID_SIZE];
	TCHAR result[NET_RESULT_SIZE];
	TCHAR *room, *p, *r;
	DWORD start, check, len;
	BOOL stopped = FALSE;
	int max = 2, id = 0;

	lib_set_int(ret, 0);
	net_close(net);
	vi = lib_param(param, 0);
	room = (vi != NULL && vi->v->type != TYPE_ARRAY) ? lib_to_string(vi) : alloc_copy(TEXT(""));
	if (room == NULL) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	for (p = room; *p != TEXT('\0') && is_space(*p); p++);
	for (r = p + lstrlen(p); r > p && is_space(*(r - 1)); r--);
	*r = TEXT('\0');
	MoveMemory(room, p, sizeof(TCHAR) * (lstrlen(p) + 1));
	if ((vi = lib_param(param, 1)) != NULL && !param_int(vi, &max)) {
		max = 2;
	}

	// the cid of the script stored online, set by pg0.exe
	len = GetEnvironmentVariable(ONLINE_CID_ENV, cid, NET_CID_SIZE);
	if (len == 0 || len >= NET_CID_SIZE) {
		net_notice(ei, NET_MSG_CID, NULL, NULL);
		mem_free(&room);
		return 0;
	}
	if ((c = net_new(cid, room, max)) == NULL) {
		mem_free(&room);
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	net_release(net);
	net = c;
	if (!net_start(c)) {
		lstrcpy(c->result, TEXT("connect"));
	}

	// until the server answers (10 seconds at most) or the script is stopped
	start = check = GetTickCount();
	while (1) {
		EnterCriticalSection(&c->cs);
		lstrcpy(result, c->result);
		id = c->id;
		LeaveCriticalSection(&c->cs);
		if (*result != TEXT('\0') || GetTickCount() - start >= NET_JOIN_WAIT) {
			break;
		}
		if (GetTickCount() - check >= 100) {
			check = GetTickCount();
			if (lib_check_stop(ei)) {
				stopped = TRUE;
				break;
			}
		}
		Sleep(10);
	}
	if (_tcscmp(result, TEXT("joined")) != 0) {
		if (!stopped) {
			if (_tcscmp(result, TEXT("cid")) == 0) {
				net_notice(ei, NET_MSG_CID, NULL, NULL);
			} else if (_tcscmp(result, TEXT("full")) == 0) {
				net_notice(ei, NET_MSG_FULL, TEXT("room"), room);
			} else if (_tcscmp(result, TEXT("closed")) == 0) {
				net_notice(ei, NET_MSG_CLOSED, TEXT("room"), room);
			} else if (_tcscmp(result, TEXT("busy")) == 0) {
				net_notice(ei, NET_MSG_BUSY, NULL, NULL);
			} else {
				net_notice(ei, NET_MSG_CONNECT, NULL, NULL);
			}
		}
		net_close(c);
		mem_free(&room);
		return 0;
	}
	mem_free(&room);
	lib_set_int(ret, id);
	return 0;
}

/*
 * _lib_func_netclose - nobody else may enter the room, even when a seat is free or becomes free
 *                      returns 1 when it was done
 */
int SFUNC _lib_func_netclose(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	net_server_warnings(ei);
	lib_set_int(ret, (net_send(TEXT("{\"t\":\"close\"}"))) ? 1 : 0);
	return 0;
}

/*
 * _lib_func_netleave - leave the room
 */
int SFUNC _lib_func_netleave(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	lib_set_int(ret, 0);
	net_close(net);
	return 0;
}

/*
 * _lib_func_netid - this player's number, 0 when not in a room
 */
int SFUNC _lib_func_netid(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	NET_CONN *c;

	net_server_warnings(ei);
	lib_set_int(ret, 0);
	if ((c = net_current()) != NULL) {
		lib_set_int(ret, c->id);
		LeaveCriticalSection(&c->cs);
	}
	return 0;
}

/*
 * _lib_func_netroom - the name of the room ("" when not in one)
 */
int SFUNC _lib_func_netroom(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	NET_CONN *c;
	BOOL ok;

	net_server_warnings(ei);
	if ((c = net_current()) != NULL) {
		ok = lib_set_string(ret, (c->room != NULL) ? c->room : TEXT(""));
		LeaveCriticalSection(&c->cs);
	} else {
		ok = lib_set_string(ret, TEXT(""));
	}
	if (!ok) {
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	return 0;
}

/*
 * _lib_func_netcount - how many players are in the room, this one included
 */
int SFUNC _lib_func_netcount(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	NET_CONN *c;

	net_server_warnings(ei);
	lib_set_int(ret, 0);
	if ((c = net_current()) != NULL) {
		lib_set_int(ret, c->player_count);
		LeaveCriticalSection(&c->cs);
	}
	return 0;
}

/*
 * _lib_func_netplayers - the numbers of the players in the room, in order
 */
int SFUNC _lib_func_netplayers(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	NET_CONN *c;
	VALUEINFO *top = NULL, *last = NULL;
	int i;

	net_server_warnings(ei);
	if ((c = net_current()) != NULL) {
		for (i = 0; i < c->player_count; i++) {
			lib_list_append(&top, &last, lib_new_int(NULL, c->players[i]));
		}
		LeaveCriticalSection(&c->cs);
	}
	lib_set_array(ret, top);
	return 0;
}

/*
 * _lib_func_netsend - send a value to the other players (or to one player): netSend(value, to = all)
 *                     returns 1 when it was sent
 */
int SFUNC _lib_func_netsend(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	NET_CONN *c;
	NET_TEXT t;
	TCHAR buf[INT_LENGTH + 16];
	int bytes, to = 0;

	if (param == NULL) {
		return -2;
	}
	net_server_warnings(ei);
	lib_set_int(ret, 0);
	if ((c = net_current()) == NULL) {
		return 0;
	}
	bytes = c->limit_bytes;
	LeaveCriticalSection(&c->cs);

	// {"t": "send", "d": <value>, "to": <player number, optional>}
	ZeroMemory(&t, sizeof(NET_TEXT));
	text_add(&t, TEXT("{\"t\":\"send\",\"d\":"));
	text_add_value(&t, param, 0);
	if (param->next != NULL) {
		if (!param_int(param->next, &to)) {
			to = 0;
		}
		wsprintf(buf, TEXT(",\"to\":%d"), to);
		text_add(&t, buf);
	}
	text_add(&t, TEXT("}"));
	if (t.error) {
		mem_free(&t.buf);
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	// the limit of the server counts the characters of the text
	if (bytes > 0 && t.len > bytes) {
		net_warn(ei, net, NET_MSG_SIZE);
	} else if (net_send(t.buf)) {
		lib_set_int(ret, 1);
	}
	mem_free(&t.buf);
	return 0;
}

/*
 * _lib_func_netavailable - how many received values are waiting for netReceive()
 */
int SFUNC _lib_func_netavailable(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	net_server_warnings(ei);
	lib_set_int(ret, 0);
	if (net != NULL) {
		EnterCriticalSection(&net->cs);
		lib_set_int(ret, net->queue_count);
		LeaveCriticalSection(&net->cs);
	}
	return 0;
}

/*
 * _lib_func_netreceive - the oldest received value as {"from": player, "data": value},
 *                        or 0 when nothing is waiting
 */
int SFUNC _lib_func_netreceive(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	NET_VALUE *nv = NULL;
	VALUEINFO *from;

	net_server_warnings(ei);
	lib_set_int(ret, 0);
	if (net != NULL) {
		EnterCriticalSection(&net->cs);
		if ((nv = net->queue) != NULL) {
			net->queue = nv->next;
			if (net->queue == NULL) {
				net->queue_last = NULL;
			}
			net->queue_count--;
			nv->next = NULL;
		}
		LeaveCriticalSection(&net->cs);
	}
	if (nv == NULL) {
		return 0;
	}
	from = lib_new_int(TEXT("from"), nv->from);
	if (from == NULL || !lib_set_name(nv->v, TEXT("data"))) {
		FreeValue(from);
		free_values(nv);
		lstrcpy(ErrStr, LIB_ERR_ALLOC);
		return -1;
	}
	from->next = nv->v;
	lib_set_array(ret, from);
	mem_free(&nv);
	return 0;
}

/*
 * _lib_func_netlast - the last value received from a player (0 when none),
 *                     whether or not it was taken with netReceive(): netLast(player)
 */
int SFUNC _lib_func_netlast(EXECINFO *ei, VALUEINFO *param, VALUEINFO *ret, TCHAR *ErrStr)
{
	NET_VALUE *nv;
	int from;

	if (param == NULL) {
		return -2;
	}
	net_server_warnings(ei);
	lib_set_int(ret, 0);
	if (!param_int(param, &from)) {
		from = 0;
	}
	if (net != NULL) {
		EnterCriticalSection(&net->cs);
		for (nv = net->last; nv != NULL && nv->from != from; nv = nv->next);
		if (nv != NULL) {
			copy_value(ret, nv->v);
		}
		LeaveCriticalSection(&net->cs);
	}
	return 0;
}
/* End of source */
