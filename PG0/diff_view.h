/*
 * PG0
 *
 * diff_view.h
 */

#ifndef _INC_DIFF_VIEW_H
#define _INC_DIFF_VIEW_H

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE

/* Define */
#define DIFF_VIEW_WND_CLASS				TEXT("PG0DiffView")

// diff view messages (WM_COPY copies the code of the selected lines)
#define DVM_SETRESULT					(WM_APP + 51)	// lParam = DIFF_RESULT * (freed by the view)
#define DVM_SETLOADING					(WM_APP + 52)	// wParam = TRUE while the versions are being read
#define DVM_SETLINENO					(WM_APP + 53)	// wParam = TRUE to show the line numbers
#define DVM_SETFOLDTEXT					(WM_APP + 54)	// lParam = label of folded lines, "%d" is the number of them
#define DVM_SELECTALL					(WM_APP + 55)
#define DVM_HASSELECTION				(WM_APP + 56)

/* Struct */

/* Function Prototypes */
BOOL diff_view_register(const HINSTANCE hInstance);

#endif
/* End of source */
