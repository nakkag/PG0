/*
 * PG0
 *
 * text_diff.c
 *
 * Line by line comparison of two texts for the revision history,
 * the same as text_diff.js of the web version.
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <tchar.h>
#include <string.h>

#include "script_memory.h"
#include "text_diff.h"

/* Define */
#define OP_EQUAL						0
#define OP_DELETE						1
#define OP_INSERT						2

// past this many differences the rest of the text is taken as replaced as a whole,
// which keeps the time and memory of comparing large texts bounded
#define EDIT_LIMIT						2000
// removed and added lines alike in at least this share of their characters (spaces aside) count as one changed line
#define CHANGE_SIMILARITY				0.5
// blocks of removed and added lines up to this many pairs are matched up line by line;
// larger ones pair the lines in order
#define PAIR_LIMIT						2500
// lines longer than this many tokens are not compared word by word
#define TOKEN_LIMIT						1000

#define DIR_UP							1
#define DIR_LEFT						2
#define DIR_PAIR						3

/* Global Variables */
// run of the same operation in an edit
typedef struct _RUN {
	int op;
	int count;
} RUN;

// edit as runs in order
typedef struct _OPS {
	RUN *runs;
	int count;
	int size;
	BOOL error;
} OPS;

// line of a text, or token of a line
typedef struct _PIECE {
	const TCHAR *str;
	int len;
	unsigned int hash;
} PIECE;

// tokens of a line
typedef struct _TOKENS {
	PIECE *items;
	int count;
} TOKENS;

// parts of a line being built
typedef struct _PARTS {
	DIFF_PART *items;
	int count;
	int size;
	BOOL error;
} PARTS;

/* Local Function Prototypes */

/*
 * grow - allocate or extend a buffer (mem_realloc does not take NULL)
 */
static void *grow(void *mem, const int size)
{
	return (mem == NULL) ? mem_alloc(size) : mem_realloc(mem, size);
}

/*
 * push_op - add a run to the edit, joining it to the last one of the same operation
 */
static void push_op(OPS *ops, const int op, const int count)
{
	RUN *runs;
	int size;

	if (count <= 0) {
		return;
	}
	if (ops->count > 0 && ops->runs[ops->count - 1].op == op) {
		ops->runs[ops->count - 1].count += count;
		return;
	}
	if (ops->count >= ops->size) {
		size = (ops->size == 0) ? 32 : ops->size * 2;
		if ((runs = grow(ops->runs, sizeof(RUN) * size)) == NULL) {
			ops->error = TRUE;
			return;
		}
		ops->runs = runs;
		ops->size = size;
	}
	ops->runs[ops->count].op = op;
	ops->runs[ops->count].count = count;
	ops->count++;
}

/*
 * hash_string - hash of a piece of text (FNV-1a)
 */
static unsigned int hash_string(const TCHAR *str, const int len)
{
	unsigned int hash = 2166136261u;
	int i;

	for (i = 0; i < len; i++) {
		hash ^= (unsigned int)str[i];
		hash *= 16777619u;
	}
	return hash;
}

/*
 * piece_equal - two pieces have the same text
 */
static BOOL piece_equal(const PIECE *a, const PIECE *b)
{
	return (a->hash == b->hash && a->len == b->len &&
		(a->len == 0 || memcmp(a->str, b->str, sizeof(TCHAR) * a->len) == 0));
}

/*
 * myers - Myers' O(ND) algorithm on a[0..n) and b[0..m); adds the edit to ops in order,
 *         or returns FALSE when the texts differ in more than the limit
 */
static BOOL myers(const PIECE *a, const int n, const PIECE *b, const int m, const int limit, OPS *ops)
{
	OPS rev;
	int *v, *trace = NULL, *tmp;
	int max = (n + m < limit) ? n + m : limit;
	int off = max + 1;
	int trace_size = 0;
	int found = -1;
	int d, k, x, y, i, size;

	if ((v = mem_calloc(sizeof(int) * (2 * max + 3))) == NULL) {
		return FALSE;
	}
	for (d = 0; d <= max && found < 0; d++) {
		// the furthest reaches before each round, kept for the walk back (round d at d * d)
		if ((d + 1) * (d + 1) > trace_size) {
			for (size = (trace_size == 0) ? 1024 : trace_size * 2; size < (d + 1) * (d + 1); size *= 2);
			if ((tmp = grow(trace, sizeof(int) * size)) == NULL) {
				mem_free((void **)&v);
				mem_free((void **)&trace);
				return FALSE;
			}
			trace = tmp;
			trace_size = size;
		}
		CopyMemory(trace + d * d, v + off - d, sizeof(int) * (2 * d + 1));
		for (k = -d; k <= d; k += 2) {
			if (k == -d || (k != d && v[off + k - 1] < v[off + k + 1])) {
				x = v[off + k + 1];
			} else {
				x = v[off + k - 1] + 1;
			}
			y = x - k;
			while (x < n && y < m && piece_equal(&a[x], &b[y])) {
				x++;
				y++;
			}
			v[off + k] = x;
			if (x >= n && y >= m) {
				found = d;
				break;
			}
		}
	}
	mem_free((void **)&v);
	if (found < 0) {
		mem_free((void **)&trace);
		return FALSE;
	}

	// walk back from the end along the moves that reached it
	ZeroMemory(&rev, sizeof(OPS));
	x = n;
	y = m;
	for (d = found; d > 0; d--) {
		const int *vd = trace + d * d;
		BOOL down;
		int prev_k, prev_x, start_x;

		k = x - y;
		down = (k == -d || (k != d && vd[k - 1 + d] < vd[k + 1 + d]));
		prev_k = (down) ? k + 1 : k - 1;
		prev_x = vd[prev_k + d];
		start_x = (down) ? prev_x : prev_x + 1;
		push_op(&rev, OP_EQUAL, x - start_x);
		push_op(&rev, (down) ? OP_INSERT : OP_DELETE, 1);
		x = prev_x;
		y = prev_x - prev_k;
	}
	push_op(&rev, OP_EQUAL, x);
	mem_free((void **)&trace);
	for (i = rev.count - 1; i >= 0; i--) {
		push_op(ops, rev.runs[i].op, rev.runs[i].count);
	}
	if (rev.error) {
		ops->error = TRUE;
	}
	mem_free((void **)&rev.runs);
	return TRUE;
}

/*
 * diff - the edit that turns a into b
 */
static void diff(const PIECE *a, const int n, const PIECE *b, const int m, const int limit, OPS *ops)
{
	int start = 0, end_a = n, end_b = m;

	while (start < n && start < m && piece_equal(&a[start], &b[start])) {
		start++;
	}
	while (end_a > start && end_b > start && piece_equal(&a[end_a - 1], &b[end_b - 1])) {
		end_a--;
		end_b--;
	}
	push_op(ops, OP_EQUAL, start);
	if (myers(a + start, end_a - start, b + start, end_b - start, limit, ops) == FALSE) {
		push_op(ops, OP_DELETE, end_a - start);
		push_op(ops, OP_INSERT, end_b - start);
	}
	push_op(ops, OP_EQUAL, n - end_a);
}

/*
 * split_lines - lines of a text (the line breaks are CR+LF, LF or a lone CR)
 */
static PIECE *split_lines(const TCHAR *text, int *count)
{
	const TCHAR *p, *s;
	PIECE *lines;
	int n = 1, i = 0;

	for (p = text; *p != TEXT('\0'); p++) {
		if (*p == TEXT('\r') && *(p + 1) == TEXT('\n')) {
			p++;
		}
		if (*p == TEXT('\r') || *p == TEXT('\n')) {
			n++;
		}
	}
	if ((lines = mem_alloc(sizeof(PIECE) * n)) == NULL) {
		return NULL;
	}
	for (s = p = text;; p++) {
		if (*p != TEXT('\0') && *p != TEXT('\r') && *p != TEXT('\n')) {
			continue;
		}
		lines[i].str = s;
		lines[i].len = (int)(p - s);
		lines[i].hash = hash_string(s, lines[i].len);
		i++;
		if (*p == TEXT('\0')) {
			break;
		}
		if (*p == TEXT('\r') && *(p + 1) == TEXT('\n')) {
			p++;
		}
		s = p + 1;
	}
	*count = n;
	return lines;
}

/*
 * is_word_char - character of a word ([A-Za-z0-9_])
 */
static BOOL is_word_char(const TCHAR c)
{
	return ((c >= TEXT('A') && c <= TEXT('Z')) || (c >= TEXT('a') && c <= TEXT('z')) ||
		(c >= TEXT('0') && c <= TEXT('9')) || c == TEXT('_'));
}

/*
 * is_space_char - white space (\s of JavaScript)
 */
static BOOL is_space_char(const TCHAR c)
{
	return ((c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0xA0 || c == 0x1680 ||
		(c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
		c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF);
}

/*
 * token_end - end of the token that starts at i: a word, a run of spaces, or a single other character
 *             (a surrogate pair is one character)
 */
static int token_end(const TCHAR *str, const int len, int i)
{
	if (is_word_char(str[i])) {
		for (i++; i < len && is_word_char(str[i]); i++);
	} else if (is_space_char(str[i])) {
		for (i++; i < len && is_space_char(str[i]); i++);
	} else if (IS_HIGH_SURROGATE(str[i]) && i + 1 < len && IS_LOW_SURROGATE(str[i + 1])) {
		i += 2;
	} else {
		i++;
	}
	return i;
}

/*
 * tokenize - tokens of a line
 */
static BOOL tokenize(const PIECE *line, TOKENS *tokens)
{
	int i, n = 0;

	for (i = 0; i < line->len; i = token_end(line->str, line->len, i)) {
		n++;
	}
	if ((tokens->items = mem_alloc(sizeof(PIECE) * (n + 1))) == NULL) {
		return FALSE;
	}
	tokens->count = 0;
	for (i = 0; i < line->len;) {
		int end = token_end(line->str, line->len, i);
		PIECE *t = &tokens->items[tokens->count++];
		t->str = line->str + i;
		t->len = end - i;
		t->hash = hash_string(t->str, t->len);
		i = end;
	}
	return TRUE;
}

/*
 * free_tokens - free the tokens of the lines
 */
static void free_tokens(TOKENS *tokens, const int count)
{
	int i;

	if (tokens == NULL) {
		return;
	}
	for (i = 0; i < count; i++) {
		mem_free((void **)&tokens[i].items);
	}
	mem_free((void **)&tokens);
}

/*
 * push_part - add a token to the parts of a line, joining it to the last part when both are changed or not
 */
static void push_part(PARTS *parts, const int start, const int len, const BOOL changed)
{
	DIFF_PART *items;
	int size;

	if (parts->count > 0 && parts->items[parts->count - 1].changed == changed) {
		parts->items[parts->count - 1].len += len;
		return;
	}
	if (parts->count >= parts->size) {
		size = (parts->size == 0) ? 8 : parts->size * 2;
		if ((items = grow(parts->items, sizeof(DIFF_PART) * size)) == NULL) {
			parts->error = TRUE;
			return;
		}
		parts->items = items;
		parts->size = size;
	}
	parts->items[parts->count].start = start;
	parts->items[parts->count].len = len;
	parts->items[parts->count].changed = changed;
	parts->count++;
}

/*
 * trimmed_length - length of a token without white space (0 for a run of spaces)
 */
static int trimmed_length(const PIECE *t)
{
	return (t->len > 0 && is_space_char(*t->str)) ? 0 : t->len;
}

/*
 * compare_line - how alike two lines are, from 0 to 1;
 *                with old_parts and new_parts, also the parts of each line that differ
 */
static double compare_line(const PIECE *old_line, const TOKENS *old_tokens, const PIECE *new_line, const TOKENS *new_tokens,
	PARTS *old_parts, PARTS *new_parts)
{
	OPS ops;
	int same = 0, total = 0, i = 0, j = 0, n, c, len;

	if (old_tokens->count > TOKEN_LIMIT || new_tokens->count > TOKEN_LIMIT) {
		return 0;
	}
	ZeroMemory(&ops, sizeof(OPS));
	diff(old_tokens->items, old_tokens->count, new_tokens->items, new_tokens->count, EDIT_LIMIT, &ops);
	if (ops.error) {
		mem_free((void **)&ops.runs);
		return 0;
	}
	for (n = 0; n < ops.count; n++) {
		for (c = 0; c < ops.runs[n].count; c++) {
			if (ops.runs[n].op == OP_EQUAL) {
				const PIECE *t = &old_tokens->items[i++];
				const PIECE *u = &new_tokens->items[j++];
				len = trimmed_length(t);
				same += len * 2;
				total += len * 2;
				if (old_parts != NULL) {
					push_part(old_parts, (int)(t->str - old_line->str), t->len, FALSE);
					push_part(new_parts, (int)(u->str - new_line->str), u->len, FALSE);
				}
			} else if (ops.runs[n].op == OP_DELETE) {
				const PIECE *t = &old_tokens->items[i++];
				total += trimmed_length(t);
				if (old_parts != NULL) {
					push_part(old_parts, (int)(t->str - old_line->str), t->len, TRUE);
				}
			} else {
				const PIECE *u = &new_tokens->items[j++];
				total += trimmed_length(u);
				if (new_parts != NULL) {
					push_part(new_parts, (int)(u->str - new_line->str), u->len, TRUE);
				}
			}
		}
	}
	mem_free((void **)&ops.runs);
	return (total == 0) ? 1 : (double)same / total;
}

/*
 * set_change - make a removed line and an added line the old and new forms of an edited line
 */
static void set_change(DIFF_ROW *old_row, const PIECE *old_line, const TOKENS *old_tokens,
	DIFF_ROW *new_row, const PIECE *new_line, const TOKENS *new_tokens)
{
	PARTS old_parts, new_parts;

	ZeroMemory(&old_parts, sizeof(PARTS));
	ZeroMemory(&new_parts, sizeof(PARTS));
	compare_line(old_line, old_tokens, new_line, new_tokens, &old_parts, &new_parts);
	old_row->type = DIFF_CHANGE_OLD;
	new_row->type = DIFF_CHANGE_NEW;
	// without the parts the lines are still shown as changed, only without the words marked
	if (!old_parts.error && !new_parts.error) {
		old_row->parts = old_parts.items;
		old_row->part_count = old_parts.count;
		new_row->parts = new_parts.items;
		new_row->part_count = new_parts.count;
	} else {
		mem_free((void **)&old_parts.items);
		mem_free((void **)&new_parts.items);
	}
}

/*
 * pair_lines - match removed lines with added lines that are edits of them, keeping both in order
 */
static BOOL pair_lines(DIFF_ROW *old_rows, const PIECE *old_lines, const int k, DIFF_ROW *new_rows, const PIECE *new_lines, const int m)
{
	TOKENS *old_tokens, *new_tokens;
	double *score = NULL;
	unsigned char *from = NULL;
	BOOL ret = FALSE;
	int i, j;

	if (k == 0 || m == 0) {
		return TRUE;
	}
	old_tokens = mem_calloc(sizeof(TOKENS) * k);
	new_tokens = mem_calloc(sizeof(TOKENS) * m);
	if (old_tokens == NULL || new_tokens == NULL) {
		goto end;
	}
	for (i = 0; i < k; i++) {
		if (tokenize(&old_lines[i], &old_tokens[i]) == FALSE) {
			goto end;
		}
	}
	for (j = 0; j < m; j++) {
		if (tokenize(&new_lines[j], &new_tokens[j]) == FALSE) {
			goto end;
		}
	}
	if ((__int64)k * m > PAIR_LIMIT) {
		for (i = 0; i < k && i < m; i++) {
			if (compare_line(&old_lines[i], &old_tokens[i], &new_lines[i], &new_tokens[i], NULL, NULL) >= CHANGE_SIMILARITY) {
				set_change(&old_rows[i], &old_lines[i], &old_tokens[i], &new_rows[i], &new_lines[i], &new_tokens[i]);
			}
		}
		ret = TRUE;
		goto end;
	}

	// the order-keeping matching with the highest total likeness
	score = mem_calloc(sizeof(double) * (k + 1) * (m + 1));
	from = mem_calloc(sizeof(unsigned char) * (k + 1) * (m + 1));
	if (score == NULL || from == NULL) {
		goto end;
	}
	for (i = 1; i <= k; i++) {
		for (j = 1; j <= m; j++) {
			int cell = i * (m + 1) + j;
			double best = score[cell - (m + 1)];
			double s;
			int dir = DIR_UP;
			if (score[cell - 1] > best) {
				best = score[cell - 1];
				dir = DIR_LEFT;
			}
			s = compare_line(&old_lines[i - 1], &old_tokens[i - 1], &new_lines[j - 1], &new_tokens[j - 1], NULL, NULL);
			if (s >= CHANGE_SIMILARITY && score[cell - (m + 1) - 1] + s > best) {
				best = score[cell - (m + 1) - 1] + s;
				dir = DIR_PAIR;
			}
			score[cell] = best;
			from[cell] = (unsigned char)dir;
		}
	}
	for (i = k, j = m; i > 0 && j > 0;) {
		int dir = from[i * (m + 1) + j];
		if (dir == DIR_PAIR) {
			set_change(&old_rows[i - 1], &old_lines[i - 1], &old_tokens[i - 1], &new_rows[j - 1], &new_lines[j - 1], &new_tokens[j - 1]);
			i--;
			j--;
		} else if (dir == DIR_UP) {
			i--;
		} else {
			j--;
		}
	}
	ret = TRUE;

end:
	mem_free((void **)&score);
	mem_free((void **)&from);
	free_tokens(old_tokens, k);
	free_tokens(new_tokens, m);
	return ret;
}

/*
 * set_row - a line of the comparison
 */
static void set_row(DIFF_ROW *row, const DIFF_TYPE type, const int old_num, const int new_num, const PIECE *line)
{
	row->type = type;
	row->old_num = old_num;
	row->new_num = new_num;
	row->text = line->str;
	row->len = line->len;
}

/*
 * text_diff_compare - compare two texts line by line (old_text NULL stands for no text at all,
 *                     so every line of new_text is added); free the result with text_diff_free
 */
DIFF_RESULT *text_diff_compare(const TCHAR *old_text, const TCHAR *new_text)
{
	DIFF_RESULT *dr;
	PIECE *a = NULL, *b = NULL;
	OPS ops;
	int na = 0, nb = 0, total = 0, i = 0, j = 0, n, c;

	ZeroMemory(&ops, sizeof(OPS));
	if ((dr = mem_calloc(sizeof(DIFF_RESULT))) == NULL) {
		return NULL;
	}
	if (old_text != NULL && ((dr->old_buf = alloc_copy(old_text)) == NULL || (a = split_lines(dr->old_buf, &na)) == NULL)) {
		goto error;
	}
	if ((dr->new_buf = alloc_copy(new_text)) == NULL || (b = split_lines(dr->new_buf, &nb)) == NULL) {
		goto error;
	}
	diff(a, na, b, nb, EDIT_LIMIT, &ops);
	if (ops.error) {
		goto error;
	}
	for (n = 0; n < ops.count; n++) {
		total += ops.runs[n].count;
	}
	if ((dr->rows = mem_calloc(sizeof(DIFF_ROW) * (total + 1))) == NULL) {
		goto error;
	}
	for (n = 0; n < ops.count; n++) {
		int dels = 0, adds = 0, old_start, new_start;

		if (ops.runs[n].op == OP_EQUAL) {
			for (c = 0; c < ops.runs[n].count; c++) {
				set_row(&dr->rows[dr->count++], DIFF_EQUAL, i + 1, j + 1, &a[i]);
				i++;
				j++;
			}
			continue;
		}
		// a block of removed and added lines between unchanged ones
		for (; n < ops.count && ops.runs[n].op != OP_EQUAL; n++) {
			if (ops.runs[n].op == OP_DELETE) {
				dels += ops.runs[n].count;
			} else {
				adds += ops.runs[n].count;
			}
		}
		n--;
		old_start = dr->count;
		for (c = 0; c < dels; c++) {
			set_row(&dr->rows[dr->count++], DIFF_DELETE, i + c + 1, 0, &a[i + c]);
		}
		new_start = dr->count;
		for (c = 0; c < adds; c++) {
			set_row(&dr->rows[dr->count++], DIFF_INSERT, 0, j + c + 1, &b[j + c]);
		}
		if (pair_lines(dr->rows + old_start, a + i, dels, dr->rows + new_start, b + j, adds) == FALSE) {
			goto error;
		}
		i += dels;
		j += adds;
	}
	mem_free((void **)&ops.runs);
	mem_free((void **)&a);
	mem_free((void **)&b);
	return dr;

error:
	mem_free((void **)&ops.runs);
	mem_free((void **)&a);
	mem_free((void **)&b);
	text_diff_free(dr);
	return NULL;
}

/*
 * text_diff_free - free the result of text_diff_compare
 */
void text_diff_free(DIFF_RESULT *dr)
{
	int i;

	if (dr == NULL) {
		return;
	}
	if (dr->rows != NULL) {
		for (i = 0; i < dr->count; i++) {
			mem_free((void **)&dr->rows[i].parts);
		}
		mem_free((void **)&dr->rows);
	}
	mem_free(&dr->old_buf);
	mem_free(&dr->new_buf);
	mem_free((void **)&dr);
}
/* End of source */
