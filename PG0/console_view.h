/*
 * PG0
 *
 * console_view.h
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef VIEW_H
#define VIEW_H

/* Include Files */
#include <windows.h>
#include <tchar.h>
#ifdef OP_XP_STYLE
#include <uxtheme.h>
#include <vssym32.h>
#endif	// OP_XP_STYLE

#ifdef VIEW_USER_H
#include VIEW_USER_H
#endif

/* Define */
#ifndef CONSOLE_WND_CLASS
#define CONSOLE_WND_CLASS		TEXT("ConsoleViewClass")
#endif

//確保するサイズ
#ifndef DEF_RESERVE
#define DEF_RESERVE
#define RESERVE_SZIE			4096
#define RESERVE_LINE			100
#endif

#define INPUT_SIZE				1024

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
#define WM_VIEW_SETSCROLLLOCK	(MSG_OFFSET + 8)
//入力モード初期化
#define WM_VIEW_INITINPUTMODE	(MSG_OFFSET + 9)
//入力モード設定
#define WM_VIEW_SETINPUTMODE	(MSG_OFFSET + 10)
//入力モード取得
#define WM_VIEW_GETINPUTMODE	(MSG_OFFSET + 11)
//入力文字列取得
#define WM_VIEW_GETINPUTSTRING	(MSG_OFFSET + 12)

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
#ifndef DECL_CONSOLE_BUFFER
#define DECL_CONSOLE_BUFFER
typedef struct _VIEW_BUFFER {
	//保持している内容
	TCHAR *buf;
	//スタイル情報 (buf の文字位置と対応)
	char *style;
	//色情報 (buf の文字位置と対応)
	char *color;
	//buf の確保しているサイズ
	int size;
	//buf の'\0'までの長さ
	int len;

	//キャレットの位置 (buf)
	TCHAR *cp;
	//選択開始位置 (buf)
	TCHAR *sp;
	//上下移動時のキャレットのX座標
	int cpx;

	//表示行頭のオフセット (buf + line[3] で4行目の位置)
	int *line;
	//表示行の長さ
	int *linelen;
	//line の確保しているサイズ
	int linesize;
	//行数 (表示行)
	int view_line;
	//行数 (論理行)
	int logic_line;
	//ウィンドウの幅
	int width;

	//スクロール バーのつまみの現在の位置
	int pos;
	//スクロール バーの現在のつまみの位置の最大値
	int max;
	//スクロールのロック (0-スクロールバーの位置で固定、1-表示位置で固定)
	int sc_lock;

	//最大文字数
	int Limit;
	//最大行数
	int LineLimit;
	//タブストップ
	int TabStop;
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

	//テキストの色
	COLORREF TextColor;
	//背景色
	COLORREF BackClolor;
	//背景色のブラシ
	HBRUSH hBkBrush;

	//マウス情報フラグ
	BOOL mousedown;

	// 入力モード
	BOOL input_mode;
	TCHAR input_string[INPUT_SIZE + 1];
	TCHAR input_buf[INPUT_SIZE + 1];

#ifdef OP_XP_STYLE
	// XP
	HMODULE hModThemes;
	HTHEME hTheme;
#endif	// OP_XP_STYLE

	//ユーザ情報
	long param1;
	long param2;
} VIEW_BUFFER;
#endif

/* Function Prototypes */
BOOL RegisterConsoleWindow(HINSTANCE hInstance);

#endif
/* End of source */
