/*
 * PG0
 *
 * online_view.h
 */

#ifndef _INC_ONLINE_VIEW_H
#define _INC_ONLINE_VIEW_H

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE

/* Define */
#define ONLINE_LIST_WND_CLASS			TEXT("PG0OnlineList")
#define ONLINE_CHIPS_WND_CLASS			TEXT("PG0OnlineChips")

#define ONLINE_CID_SIZE					64
#define ONLINE_TAG_SIZE					32
#define ONLINE_MAX_TAGS					8

// list messages
#define OLM_ADDITEM						(WM_APP + 1)	// lParam = ONLINE_ITEM * (copied)
#define OLM_DELETEITEM					(WM_APP + 2)	// wParam = index
#define OLM_CLEAR						(WM_APP + 3)
#define OLM_GETITEM						(WM_APP + 4)	// wParam = index, returns ONLINE_ITEM *
#define OLM_GETCOUNT					(WM_APP + 5)
#define OLM_FINDCID						(WM_APP + 6)	// lParam = cid, returns the index or -1
#define OLM_SETLOADING					(WM_APP + 7)	// wParam = TRUE while the first page is being read
#define OLM_SETMORE						(WM_APP + 8)	// wParam = TRUE to show the "read more" item
#define OLM_SETMORETEXT					(WM_APP + 9)	// lParam = text of the "read more" item
#define OLM_SETCURRENTTEXT				(WM_APP + 10)	// lParam = text of the "current version" badge
#define OLM_SETSELTAG					(WM_APP + 11)	// lParam = genre chosen as the filter (its badges choose the item)

// chips messages
#define OCM_ADDCHIP						(WM_APP + 21)	// lParam = label
#define OCM_SETSEL						(WM_APP + 22)	// wParam = index
#define OCM_GETSEL						(WM_APP + 23)
#define OCM_GETHEIGHT					(WM_APP + 24)

// notification to the parent: wParam = control ID, lParam = ONLINE_NOTIFY *
#define WM_ONLINE_NOTIFY				(WM_APP + 40)
#define OLN_OPEN						1		// an item was chosen
#define OLN_MENU						2		// the menu of an item was pressed (pt = where to show it)
#define OLN_TAG							3		// a genre badge was pressed (tag = index in the item)
#define OLN_MORE						4		// "read more" was chosen
#define OCN_SELECT						5		// a chip was chosen

/* Struct */
// item of the online list
typedef struct _ONLINE_ITEM {
	TCHAR cid[ONLINE_CID_SIZE];
	TCHAR *name;
	TCHAR *author;
	TCHAR *memo;
	TCHAR *time;
	double update_time;
	BOOL private_mode;
	BOOL current;
	BOOL menu;
	int tag_count;
	TCHAR tags[ONLINE_MAX_TAGS][ONLINE_TAG_SIZE];
	TCHAR *tag_labels[ONLINE_MAX_TAGS];
} ONLINE_ITEM;

typedef struct _ONLINE_NOTIFY {
	int code;
	int index;
	int tag;
	POINT pt;
} ONLINE_NOTIFY;

/* Function Prototypes */
BOOL online_view_register(const HINSTANCE hInstance);
void online_item_free(ONLINE_ITEM *item);

#endif
/* End of source */
