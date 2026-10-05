/*
 * PG0
 *
 * text_diff.h
 */

#ifndef _INC_TEXT_DIFF_H
#define _INC_TEXT_DIFF_H

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE

/* Define */

/* Struct */
typedef enum _DIFF_TYPE {
	DIFF_EQUAL = 0,
	DIFF_DELETE,
	DIFF_INSERT,
	DIFF_CHANGE_OLD,		// the old form of an edited line
	DIFF_CHANGE_NEW			// the new form of an edited line
} DIFF_TYPE;

// piece of a changed line (the parts of a line follow each other from its start)
typedef struct _DIFF_PART {
	int start;
	int len;
	BOOL changed;
} DIFF_PART;

// line of the comparison, in the order a unified diff shows them
typedef struct _DIFF_ROW {
	DIFF_TYPE type;
	int old_num;			// 1-based line numbers (0 where the line is not on that side)
	int new_num;
	const TCHAR *text;		// not terminated
	int len;
	DIFF_PART *parts;		// on changed lines
	int part_count;
} DIFF_ROW;

typedef struct _DIFF_RESULT {
	DIFF_ROW *rows;
	int count;
	TCHAR *old_buf;			// copies of the texts the rows point into
	TCHAR *new_buf;
} DIFF_RESULT;

/* Function Prototypes */
DIFF_RESULT *text_diff_compare(const TCHAR *old_text, const TCHAR *new_text);
void text_diff_free(DIFF_RESULT *dr);

#endif
/* End of source */
