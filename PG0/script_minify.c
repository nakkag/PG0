/*
 * PG0
 *
 * script_minify.c
 *
 * Copyright (C) 1996-2026 by Nakashima Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 *
 * Short names for the variables and the functions a program declares, given
 * before its parse tree is written to an image (pg0gen): the image is smaller
 * and reads like nothing. The web version compiles its apps the same way
 * (app_server.js); the editor keeps every name, since it shows them.
 *
 * The functions are one set for the whole chain (a script calls the functions
 * of the scripts it imports): _a, _b, ... A name that no script declares is a
 * library function and stays. The variables are each script's own: a, b, ...,
 * z, _, aa, ba, ... (lower case: the names of variables are compared without
 * regard to the case of half-width letters). A variable named inside a {...}
 * that makes an array keeps its name, which becomes the key of the element
 * ({x, y} is {"x": ..., "y": ...}); argv and argc keep theirs, since the
 * executable hands the command line to the program under them.
 */

/* Include Files */
#include <windows.h>
#include <tchar.h>

#include "script.h"
#include "script_string.h"
#include "script_memory.h"
#include "script_minify.h"

/* Define */
#define NAME_FIRST				TEXT("abcdefghijklmnopqrstuvwxyz_")
#define NAME_REST				TEXT("abcdefghijklmnopqrstuvwxyz0123456789_")
#define NAME_SIZE				16

/* Struct */
// original name (in lower case, with its hash) -> short name
typedef struct _NAME_MAP {
	TCHAR **keys;
	int *hashes;
	TCHAR **names;
	int count;
	int capacity;
} NAME_MAP;

// what the walk over a script renames with
typedef struct _RENAME {
	NAME_MAP *funcs;
	NAME_MAP *keep;
	NAME_MAP *vars;
	int next;
	BOOL error;
} RENAME;

/* Global Variables */
// names a variable may not be given: the reserved words of up to three letters
static const TCHAR *keywords[] = {
	TEXT("var"), TEXT("if"), TEXT("for"), TEXT("do"), NULL
};

/* Local Function Prototypes */
static void map_free(NAME_MAP *map);
static int map_find(NAME_MAP *map, const TCHAR *name);
static BOOL map_add(NAME_MAP *map, const TCHAR *name, const TCHAR *short_name);
static void short_name(int n, TCHAR *ret);
static BOOL walk_tokens(TOKEN *tk, BOOL in_brace, BOOL (*func)(TOKEN *tk, BOOL in_brace, void *arg), void *arg);
static BOOL collect_keep(TOKEN *tk, BOOL in_brace, void *arg);
static BOOL rename_token(TOKEN *tk, BOOL in_brace, void *arg);
static BOOL set_name(TCHAR **buf, const TCHAR *name, const BOOL ref);

/*
 * map_free - free a name map
 */
static void map_free(NAME_MAP *map)
{
	int i;

	for (i = 0; i < map->count; i++) {
		mem_free(&*(map->keys + i));
		mem_free(&*(map->names + i));
	}
	mem_free((void **)&map->keys);
	mem_free((void **)&map->hashes);
	mem_free((void **)&map->names);
	ZeroMemory(map, sizeof(NAME_MAP));
}

/*
 * map_find - index of a name (whatever the case of its half-width letters), -1 when it is not in the map
 */
static int map_find(NAME_MAP *map, const TCHAR *name)
{
	TCHAR *key;
	int hash, i;

	if ((key = alloc_copy(name)) == NULL) {
		return -1;
	}
	str_lower(key);
	hash = str2hash(key);
	for (i = 0; i < map->count; i++) {
		if (hash == *(map->hashes + i) && lstrcmp(key, *(map->keys + i)) == 0) {
			mem_free(&key);
			return i;
		}
	}
	mem_free(&key);
	return -1;
}

/*
 * map_add - add a name with its short name
 */
static BOOL map_add(NAME_MAP *map, const TCHAR *name, const TCHAR *short_name)
{
	TCHAR *key, *value;

	if (map->count >= map->capacity) {
		int capacity = (map->capacity == 0) ? 64 : map->capacity * 2;
		TCHAR **keys = (map->keys == NULL) ? mem_alloc(sizeof(TCHAR *) * capacity) : mem_realloc(map->keys, sizeof(TCHAR *) * capacity);
		int *hashes;
		TCHAR **names;
		if (keys == NULL) {
			return FALSE;
		}
		map->keys = keys;
		hashes = (map->hashes == NULL) ? mem_alloc(sizeof(int) * capacity) : mem_realloc(map->hashes, sizeof(int) * capacity);
		if (hashes == NULL) {
			return FALSE;
		}
		map->hashes = hashes;
		names = (map->names == NULL) ? mem_alloc(sizeof(TCHAR *) * capacity) : mem_realloc(map->names, sizeof(TCHAR *) * capacity);
		if (names == NULL) {
			return FALSE;
		}
		map->names = names;
		map->capacity = capacity;
	}
	if ((key = alloc_copy(name)) == NULL) {
		return FALSE;
	}
	str_lower(key);
	if ((value = alloc_copy((short_name != NULL) ? short_name : TEXT(""))) == NULL) {
		mem_free(&key);
		return FALSE;
	}
	*(map->keys + map->count) = key;
	*(map->hashes + map->count) = str2hash(key);
	*(map->names + map->count) = value;
	map->count++;
	return TRUE;
}

/*
 * short_name - the n-th short name (from 0): a, b, ..., z, _, aa, ba, ..., then three characters
 */
static void short_name(int n, TCHAR *ret)
{
	const int first = lstrlen(NAME_FIRST);
	const int rest = lstrlen(NAME_REST);
	TCHAR *p = ret;

	*(p++) = *(NAME_FIRST + (n % first));
	n /= first;
	while (n > 0 && p - ret < NAME_SIZE - 1) {
		n--;
		*(p++) = *(NAME_REST + (n % rest));
		n /= rest;
	}
	*p = TEXT('\0');
}

/*
 * walk_tokens - every token of a list and of the blocks under it; in_brace under a {...} that makes an array
 */
static BOOL walk_tokens(TOKEN *tk, BOOL in_brace, BOOL (*func)(TOKEN *tk, BOOL in_brace, void *arg), void *arg)
{
	for (; tk != NULL; tk = tk->next) {
		if (func(tk, in_brace, arg) == FALSE) {
			return FALSE;
		}
		if (tk->target != NULL &&
			walk_tokens(tk->target, in_brace || tk->sym_type == SYM_BOPEN_PRIMARY, func, arg) == FALSE) {
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * collect_keep - the variables named inside a {...} that makes an array keep their names
 */
static BOOL collect_keep(TOKEN *tk, BOOL in_brace, void *arg)
{
	NAME_MAP *keep = (NAME_MAP *)arg;
	const TCHAR *name;

	if (!in_brace || (tk->sym_type != SYM_VARIABLE && tk->sym_type != SYM_DECLVARIABLE) || tk->buf == NULL) {
		return TRUE;
	}
	name = (*tk->buf == TEXT('&')) ? tk->buf + 1 : tk->buf;
	if (map_find(keep, name) >= 0) {
		return TRUE;
	}
	return map_add(keep, name, NULL);
}

/*
 * set_name - a token's name, with & before it for an argument passed by reference
 */
static BOOL set_name(TCHAR **buf, const TCHAR *name, const BOOL ref)
{
	TCHAR *p = (ref) ? alloc_join(TEXT("&"), name) : alloc_copy(name);

	if (p == NULL) {
		return FALSE;
	}
	mem_free(&*buf);
	*buf = p;
	return TRUE;
}

/*
 * rename_token - give a function or a variable its short name
 */
static BOOL rename_token(TOKEN *tk, BOOL in_brace, void *arg)
{
	RENAME *rn = (RENAME *)arg;
	const TCHAR *name;
	TCHAR buf[NAME_SIZE];
	BOOL ref;
	int i, k;

	if (tk->buf == NULL) {
		return TRUE;
	}
	switch (tk->sym_type) {
	case SYM_FUNCSTART:
	case SYM_FUNC:
		// (a name that no script declares is a library function)
		if ((i = map_find(rn->funcs, tk->buf)) >= 0 && set_name(&tk->buf, *(rn->funcs->names + i), FALSE) == FALSE) {
			rn->error = TRUE;
			return FALSE;
		}
		break;

	case SYM_VARIABLE:
	case SYM_DECLVARIABLE:
		ref = (*tk->buf == TEXT('&'));
		name = (ref) ? tk->buf + 1 : tk->buf;
		if (map_find(rn->keep, name) >= 0) {
			break;
		}
		if ((i = map_find(rn->vars, name)) < 0) {
			// the next short name that is not a reserved word and not a name that is kept
			for (;;) {
				short_name(rn->next++, buf);
				for (k = 0; keywords[k] != NULL && lstrcmp(keywords[k], buf) != 0; k++);
				if (keywords[k] == NULL && map_find(rn->keep, buf) < 0) {
					break;
				}
			}
			if (map_add(rn->vars, name, buf) == FALSE) {
				rn->error = TRUE;
				return FALSE;
			}
			i = rn->vars->count - 1;
		}
		if (set_name(&tk->buf, *(rn->vars->names + i), ref) == FALSE) {
			rn->error = TRUE;
			return FALSE;
		}
		break;
	}
	return TRUE;
}

/*
 * MinifyScript - short names for the variables and the functions of a script chain
 */
BOOL MinifyScript(SCRIPTINFO *sci)
{
	NAME_MAP funcs, keep, vars;
	SCRIPTINFO *csci;
	FUNCINFO *fi;
	RENAME rn;
	TCHAR buf[NAME_SIZE];
	BOOL ret = TRUE;
	int i;

	ZeroMemory(&funcs, sizeof(NAME_MAP));
	ZeroMemory(&keep, sizeof(NAME_MAP));
	ZeroMemory(&vars, sizeof(NAME_MAP));

	// the functions of every script, one set
	for (csci = sci; ret == TRUE && csci != NULL; csci = csci->next) {
		for (fi = csci->fi; ret == TRUE && fi != NULL; fi = fi->next) {
			if (fi->name == NULL || map_find(&funcs, fi->name) >= 0) {
				continue;
			}
			*buf = TEXT('_');
			short_name(funcs.count, buf + 1);
			ret = map_add(&funcs, fi->name, buf);
		}
	}

	for (csci = sci; ret == TRUE && csci != NULL; csci = csci->next) {
		// the variables that keep their names
		ret = map_add(&keep, TEXT("argv"), NULL) && map_add(&keep, TEXT("argc"), NULL) &&
			walk_tokens(csci->tk, FALSE, collect_keep, &keep);
		if (ret == TRUE) {
			ZeroMemory(&rn, sizeof(RENAME));
			rn.funcs = &funcs;
			rn.keep = &keep;
			rn.vars = &vars;
			ret = walk_tokens(csci->tk, FALSE, rename_token, &rn) && !rn.error;
		}
		// the list of functions names them as the tokens do
		for (fi = csci->fi; ret == TRUE && fi != NULL; fi = fi->next) {
			if (fi->name != NULL && (i = map_find(&funcs, fi->name)) >= 0) {
				ret = set_name(&fi->name, *(funcs.names + i), FALSE);
				if (ret == TRUE) {
					fi->name_hash = str2hash(fi->name);
				}
			}
		}
		map_free(&keep);
		map_free(&vars);
	}
	map_free(&funcs);
	return ret;
}
/* End of source */
