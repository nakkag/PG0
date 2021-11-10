/*
 * PG0
 *
 * frame.c
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef	_INC_OLE

/* Define */
#define FRAME_CNT				1		// ���E�t���[����
#define NOMOVESIZE				6		// �t���[���̈ړ������l

/* Global Variables */

/* Local Function Prototypes */

/*
 * InitializeFrame - �t���[���̏�����
 */
BOOL InitializeFrame(const HWND hWnd)
{
	SetCapture(hWnd);
	return TRUE;
}

/*
 * FreeFrame - �t���[���̉��
 */
void FreeFrame(void)
{
	ReleaseCapture();
}

/*
 * DrawVerticalFrame - �t���[���̕`��
 */
int DrawVerticalFrame(const HWND hWnd, const HWND hContainer)
{
	RECT window_rect, container_rect;
	POINT apos;

	GetCursorPos((LPPOINT)&apos);
	GetWindowRect(hWnd, (LPRECT)&window_rect);
	GetWindowRect(hContainer, (LPRECT)&container_rect);

	// �t���[���̈ړ�����
	if (apos.x <= (window_rect.left + NOMOVESIZE + GetSystemMetrics(SM_CXFRAME))) {
		apos.x = window_rect.left + NOMOVESIZE + GetSystemMetrics(SM_CXFRAME);
	} else if (apos.x >= (window_rect.right - (NOMOVESIZE + (FRAME_CNT * 2)) - GetSystemMetrics(SM_CXFRAME))) {
		apos.x = window_rect.right - (NOMOVESIZE + (FRAME_CNT * 2)) - GetSystemMetrics(SM_CXFRAME);
	}
	return (apos.x - window_rect.left) - GetSystemMetrics(SM_CXFRAME);
}

/*
 * DrawHorizonFrame - �t���[���̕`��
 */
int DrawHorizonFrame(const HWND hWnd, const HWND hContainer)
{
	RECT window_rect, container_rect;
	POINT apos;

	GetCursorPos((LPPOINT)&apos);
	GetWindowRect(hWnd, (LPRECT)&window_rect);
	GetWindowRect(hContainer, (LPRECT)&container_rect);

	// �t���[���̈ړ�����
	if (apos.y <= (window_rect.top + NOMOVESIZE + GetSystemMetrics(SM_CYFRAME))) {
		apos.y = window_rect.top + NOMOVESIZE + GetSystemMetrics(SM_CYFRAME);
	} else if (apos.y >= (window_rect.bottom - (NOMOVESIZE + (FRAME_CNT * 2)) - GetSystemMetrics(SM_CYFRAME))) {
		apos.y = window_rect.bottom - (NOMOVESIZE + (FRAME_CNT * 2)) - GetSystemMetrics(SM_CYFRAME);
	}
	return (apos.y - window_rect.top) - GetSystemMetrics(SM_CYFRAME);
}

/*
 * DrawVariableFrame - �t���[���̕`��
 */
int DrawVariableFrame(const HWND hWnd)
{
	RECT window_rect;
	POINT apos;

	GetCursorPos((LPPOINT)&apos);
	GetWindowRect(hWnd, (LPRECT)&window_rect);
	return (apos.x - window_rect.left) - GetSystemMetrics(SM_CXFRAME);
}
/* End of source */
