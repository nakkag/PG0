/*
 * PG0
 *
 * variable_view.h
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef VARIABLE_VIEW_H
#define VARIABLE_VIEW_H

/* Include Files */
#include <windows.h>
#include <tchar.h>
#ifdef OP_XP_STYLE
#include <uxtheme.h>
#include <vssym32.h>
#endif	// OP_XP_STYLE

#include "script.h"

/* Define */
#ifndef VARIABLE_WND_CLASS
#define VARIABLE_WND_CLASS		TEXT("VariableViewClass")
#endif

//確保するサイズ
#ifndef DEF_RESERVE
#define DEF_RESERVE
#define RESERVE_SZIE			4096
#define RESERVE_LINE			100
#endif

#ifndef MSG_OFFSET
#define MSG_OFFSET				WM_APP + 100
#endif
#ifndef MSG_NOTIFY_OFFSET
#define MSG_NOTIFY_OFFSET		WM_APP + 500
#endif

//文字列の追加
#define WM_VIEW_ADDTEXT			(MSG_OFFSET + 0)
//struct _BUFFER の取得
#define WM_VIEW_GETBUFFERINFO	(MSG_OFFSET + 1)
//テキスト色と背景色の設定 (wParam - TextColor, lParam - BackColor)
#define WM_VIEW_SETCOLOR		(MSG_OFFSET + 2)
//行間の設定 (wParam - 行間のドット数)
#define WM_VIEW_SETSPACING		(MSG_OFFSET + 4)
//論理行数制限の設定
#define WM_VIEW_SETLINELIMIT	(MSG_OFFSET + 5)
//フォントや行間変更の反映
#define WM_VIEW_REFLECT			(MSG_OFFSET + 6)
//スクロールのロック (wPrama - 0でスクロール位置固定、1で表示位置固定)
#define WM_VIEW_IS_SELECTION	(MSG_OFFSET + 7)

//変数の追加
#define WM_VIEW_SETVARIABLE		(MSG_OFFSET + 10)

#define WM_VIEW_SET_SEPSIZE		(MSG_OFFSET + 11)
#define WM_VIEW_GET_SEPSIZE		(MSG_OFFSET + 12)

#define WM_VIEW_SET_HEADER_NAME	(MSG_OFFSET + 13)
#define WM_VIEW_SET_HEADER_VALUE	(MSG_OFFSET + 14)

#define WM_VIEW_SET_HEX_MODE	(MSG_OFFSET + 15)

//文字装飾コード
#ifndef DEF_ORN
#define DEF_ORN
#define ORN_COLOR				0x03
#define ORN_BOLD				0x02
#define ORN_UNDERLINE			0x1F
#define ORN_REVERSE				0x16
#define ORN_ITALIC				0x14
#endif

//スタイルマスク
#ifndef DEF_STYLE
#define DEF_STYLE
#define STYLE_TEXTCOLOR			1
#define STYLE_BACKCOLOR			2
#define STYLE_RETURNCOLOR		4
#define STYLE_BOLD				8
#define STYLE_UNDERLINE			16
#define STYLE_REVERSE			32
#define STYLE_ITALIC			64
#endif

/* Struct */
//値情報
typedef struct _VALUE_VIEW_INFO {
	VALUEINFO *vi;
	TCHAR *name;
	TCHAR *buf;

	struct _VALUE_VIEW_INFO *child;
	int child_count;
	int alloc_count;

	VALUEINFO *org_vi;
	BOOL change;
	BOOL valid;
	BOOL select;
	BOOL open;
	int index;
	int indent;
} VALUE_VIEW_INFO;

#ifndef DECL_VARIABLE_BUFFER
#define DECL_VARIABLE_BUFFER
typedef struct _VARIABLE_BUFFER {
	//行数 (表示行)
	int view_line;
	int draw_width;
	//ウィンドウの幅
	int width;
	//ヘッダーの高さ
	int header_height;
	long sep_size;

	//スクロール バーのつまみの現在の位置
	int pos_x;
	//スクロール バーの現在のつまみの位置の最大値
	int max_x;
	//スクロール バーのつまみの現在の位置
	int pos_y;
	//スクロール バーの現在のつまみの位置の最大値
	int max_y;

	//左マージン
	int LeftMargin;
	//右マージン
	int RightMargin;
	//行間
	int Spacing;

	//描画用情報
	HDC mdc;
	HBITMAP hBmp;
	HBITMAP hRetBmp;

	//フォント情報
	LOGFONT lf;
	//フォントの高さ
	int FontHeight;
	//1文字のフォントの幅
	int CharWidth;
	//固定ピッチ - TRUE, 可変ピッチ - FALSE
	BOOL Fixed;
	//キャラクタセット (default - SHIFTJIS_CHARSET)
	BYTE CharSet;

	//テキストの色
	COLORREF TextColor;
	//背景色
	COLORREF BackClolor;
	//背景色のブラシ
	HBRUSH hBkBrush;

	TCHAR header_name[BUF_SIZE];
	TCHAR header_value[BUF_SIZE];

#ifdef OP_XP_STYLE
	// XP
	HMODULE hModThemes;
	HTHEME hTheme;
#endif	// OP_XP_STYLE
} VARIABLE_BUFFER;
#endif

/* Function Prototypes */
BOOL RegisterVariableWindow(HINSTANCE hInstance);

#endif
/* End of source */
