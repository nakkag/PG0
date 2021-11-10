/*
 * PG0
 *
 * toolbar.c
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef	_INC_OLE
#include <commctrl.h>

#include "dpi.h"
#include "resource.h"

/* Define */
#define BITMAP_CNT						3

#define TOOLBAR_INDENT					Scale(9)

/* Global Variables */
static TBBUTTON tbb[] = {
	{0,					ID_MENUITEM_EXEC,		TBSTATE_ENABLED,	TBSTYLE_BUTTON, 0, 0},
	{1,					ID_MENUITEM_STEP,		TBSTATE_ENABLED,	TBSTYLE_BUTTON, 0, 0},
	{2,					ID_MENUITEM_STOP,		TBSTATE_ENABLED,	TBSTYLE_BUTTON, 0, 0},
};

/* Local Function Prototypes */

/*
 * toolbar_create - StatusBar�̍쐬
 */
HWND toolbar_create(const HINSTANCE hInst, const HWND hWnd, const int id)
{
	HWND hToolBar;
	INITCOMMONCONTROLSEX ic;

	ic.dwSize = sizeof(INITCOMMONCONTROLSEX);
	ic.dwICC = ICC_BAR_CLASSES;
	InitCommonControlsEx(&ic);

	if (GetAwareness() != PROCESS_DPI_UNAWARE && GetScale() >= 300) {
		hToolBar = CreateToolbarEx(hWnd, WS_CHILD | TBSTYLE_TOOLTIPS,
			id, BITMAP_CNT, hInst, IDR_TOOLBAR_96, tbb, sizeof(tbb) / sizeof(TBBUTTON), 0, 0,
			96, 96, sizeof(TBBUTTON));
	}
	else if (GetAwareness() != PROCESS_DPI_UNAWARE && GetScale() >= 150) {
		hToolBar = CreateToolbarEx(hWnd, WS_CHILD | TBSTYLE_TOOLTIPS,
			id, BITMAP_CNT, hInst, IDR_TOOLBAR_64, tbb, sizeof(tbb) / sizeof(TBBUTTON), 0, 0,
			64, 64, sizeof(TBBUTTON));
	}
	else {
		hToolBar = CreateToolbarEx(hWnd, WS_CHILD | TBSTYLE_TOOLTIPS,
			id, BITMAP_CNT, hInst, IDR_TOOLBAR, tbb, sizeof(tbb) / sizeof(TBBUTTON), 0, 0,
			32, 32, sizeof(TBBUTTON));
	}
	SetWindowLongPtr(hToolBar, GWL_STYLE, GetWindowLongPtr(hToolBar, GWL_STYLE) | TBSTYLE_FLAT);
	SendMessage(hToolBar, TB_SETINDENT, TOOLBAR_INDENT, 0);
	ShowWindow(hToolBar, SW_SHOW);
	return hToolBar;
}
/* End of source */
