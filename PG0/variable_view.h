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

//�m�ۂ���T�C�Y
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

//������̒ǉ�
#define WM_VIEW_ADDTEXT			(MSG_OFFSET + 0)
//struct _BUFFER �̎擾
#define WM_VIEW_GETBUFFERINFO	(MSG_OFFSET + 1)
//�e�L�X�g�F�Ɣw�i�F�̐ݒ� (wParam - TextColor, lParam - BackColor)
#define WM_VIEW_SETCOLOR		(MSG_OFFSET + 2)
//�s�Ԃ̐ݒ� (wParam - �s�Ԃ̃h�b�g��)
#define WM_VIEW_SETSPACING		(MSG_OFFSET + 4)
//�_���s�������̐ݒ�
#define WM_VIEW_SETLINELIMIT	(MSG_OFFSET + 5)
//�t�H���g��s�ԕύX�̔��f
#define WM_VIEW_REFLECT			(MSG_OFFSET + 6)
//�X�N���[���̃��b�N (wPrama - 0�ŃX�N���[���ʒu�Œ�A1�ŕ\���ʒu�Œ�)
#define WM_VIEW_IS_SELECTION	(MSG_OFFSET + 7)

//�ϐ��̒ǉ�
#define WM_VIEW_SETVARIABLE		(MSG_OFFSET + 10)

#define WM_VIEW_SET_SEPSIZE		(MSG_OFFSET + 11)
#define WM_VIEW_GET_SEPSIZE		(MSG_OFFSET + 12)

#define WM_VIEW_SET_HEADER_NAME	(MSG_OFFSET + 13)
#define WM_VIEW_SET_HEADER_VALUE	(MSG_OFFSET + 14)

#define WM_VIEW_SET_HEX_MODE	(MSG_OFFSET + 15)

//���������R�[�h
#ifndef DEF_ORN
#define DEF_ORN
#define ORN_COLOR				0x03
#define ORN_BOLD				0x02
#define ORN_UNDERLINE			0x1F
#define ORN_REVERSE				0x16
#define ORN_ITALIC				0x14
#endif

//�X�^�C���}�X�N
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
//�l���
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
	//�s�� (�\���s)
	int view_line;
	int draw_width;
	//�E�B���h�E�̕�
	int width;
	//�w�b�_�[�̍���
	int header_height;
	long sep_size;

	//�X�N���[�� �o�[�̂܂݂̌��݂̈ʒu
	int pos_x;
	//�X�N���[�� �o�[�̌��݂̂܂݂̈ʒu�̍ő�l
	int max_x;
	//�X�N���[�� �o�[�̂܂݂̌��݂̈ʒu
	int pos_y;
	//�X�N���[�� �o�[�̌��݂̂܂݂̈ʒu�̍ő�l
	int max_y;

	//���}�[�W��
	int LeftMargin;
	//�E�}�[�W��
	int RightMargin;
	//�s��
	int Spacing;

	//�`��p���
	HDC mdc;
	HBITMAP hBmp;
	HBITMAP hRetBmp;

	//�t�H���g���
	LOGFONT lf;
	//�t�H���g�̍���
	int FontHeight;
	//1�����̃t�H���g�̕�
	int CharWidth;
	//�Œ�s�b�` - TRUE, �σs�b�` - FALSE
	BOOL Fixed;
	//�L�����N�^�Z�b�g (default - SHIFTJIS_CHARSET)
	BYTE CharSet;

	//�e�L�X�g�̐F
	COLORREF TextColor;
	//�w�i�F
	COLORREF BackClolor;
	//�w�i�F�̃u���V
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
