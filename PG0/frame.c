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
#define FRAME_CNT				1		// 境界フレーム数
#define NOMOVESIZE				6		// フレームの移動制限値

/* Global Variables */

/* Local Function Prototypes */

/*
 * InitializeFrame - フレームの初期化
 */
BOOL InitializeFrame(const HWND hWnd)
{
	SetCapture(hWnd);
	return TRUE;
}

/*
 * FreeFrame - フレームの解放
 */
void FreeFrame(void)
{
	ReleaseCapture();
}

/*
 * DrawVerticalFrame - フレームの描画
 */
int DrawVerticalFrame(const HWND hWnd, const HWND hContainer)
{
	RECT window_rect, container_rect;
	POINT apos;

	GetCursorPos((LPPOINT)&apos);
	GetWindowRect(hWnd, (LPRECT)&window_rect);
	GetWindowRect(hContainer, (LPRECT)&container_rect);

	// フレームの移動制限
	if (apos.x <= (window_rect.left + NOMOVESIZE + GetSystemMetrics(SM_CXFRAME))) {
		apos.x = window_rect.left + NOMOVESIZE + GetSystemMetrics(SM_CXFRAME);
	} else if (apos.x >= (window_rect.right - (NOMOVESIZE + (FRAME_CNT * 2)) - GetSystemMetrics(SM_CXFRAME))) {
		apos.x = window_rect.right - (NOMOVESIZE + (FRAME_CNT * 2)) - GetSystemMetrics(SM_CXFRAME);
	}
	return (apos.x - window_rect.left) - GetSystemMetrics(SM_CXFRAME);
}

/*
 * DrawHorizonFrame - フレームの描画
 */
int DrawHorizonFrame(const HWND hWnd, const HWND hContainer)
{
	RECT window_rect, container_rect;
	POINT apos;

	GetCursorPos((LPPOINT)&apos);
	GetWindowRect(hWnd, (LPRECT)&window_rect);
	GetWindowRect(hContainer, (LPRECT)&container_rect);

	// フレームの移動制限
	if (apos.y <= (window_rect.top + NOMOVESIZE + GetSystemMetrics(SM_CYFRAME))) {
		apos.y = window_rect.top + NOMOVESIZE + GetSystemMetrics(SM_CYFRAME);
	} else if (apos.y >= (window_rect.bottom - (NOMOVESIZE + (FRAME_CNT * 2)) - GetSystemMetrics(SM_CYFRAME))) {
		apos.y = window_rect.bottom - (NOMOVESIZE + (FRAME_CNT * 2)) - GetSystemMetrics(SM_CYFRAME);
	}
	return (apos.y - window_rect.top) - GetSystemMetrics(SM_CYFRAME);
}

/*
 * DrawVariableFrame - フレームの描画
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
