/*
 * PG0
 *
 * frame.h
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef _INC_FRAME_H
#define _INC_FRAME_H

/* Include Files */

/* Define */

/* Struct */

/* Function Prototypes */
BOOL InitializeFrame(const HWND hWnd);
void FreeFrame(void);
int DrawVerticalFrame(const HWND hWnd, const HWND hContainer);
int DrawHorizonFrame(const HWND hWnd, const HWND hContainer);
int DrawVariableFrame(const HWND hWnd);

#endif
/* End of source */
