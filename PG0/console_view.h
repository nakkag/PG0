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

//�m�ۂ���T�C�Y
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
#define WM_VIEW_SETSCROLLLOCK	(MSG_OFFSET + 8)
//���̓��[�h������
#define WM_VIEW_INITINPUTMODE	(MSG_OFFSET + 9)
//���̓��[�h�ݒ�
#define WM_VIEW_SETINPUTMODE	(MSG_OFFSET + 10)
//���̓��[�h�擾
#define WM_VIEW_GETINPUTMODE	(MSG_OFFSET + 11)
//���͕�����擾
#define WM_VIEW_GETINPUTSTRING	(MSG_OFFSET + 12)

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
#ifndef DECL_CONSOLE_BUFFER
#define DECL_CONSOLE_BUFFER
typedef struct _VIEW_BUFFER {
	//�ێ����Ă�����e
	TCHAR *buf;
	//�X�^�C����� (buf �̕����ʒu�ƑΉ�)
	char *style;
	//�F��� (buf �̕����ʒu�ƑΉ�)
	char *color;
	//buf �̊m�ۂ��Ă���T�C�Y
	int size;
	//buf ��'\0'�܂ł̒���
	int len;

	//�L�����b�g�̈ʒu (buf)
	TCHAR *cp;
	//�I���J�n�ʒu (buf)
	TCHAR *sp;
	//�㉺�ړ����̃L�����b�g��X���W
	int cpx;

	//�\���s���̃I�t�Z�b�g (buf + line[3] ��4�s�ڂ̈ʒu)
	int *line;
	//�\���s�̒���
	int *linelen;
	//line �̊m�ۂ��Ă���T�C�Y
	int linesize;
	//�s�� (�\���s)
	int view_line;
	//�s�� (�_���s)
	int logic_line;
	//�E�B���h�E�̕�
	int width;

	//�X�N���[�� �o�[�̂܂݂̌��݂̈ʒu
	int pos;
	//�X�N���[�� �o�[�̌��݂̂܂݂̈ʒu�̍ő�l
	int max;
	//�X�N���[���̃��b�N (0-�X�N���[���o�[�̈ʒu�ŌŒ�A1-�\���ʒu�ŌŒ�)
	int sc_lock;

	//�ő啶����
	int Limit;
	//�ő�s��
	int LineLimit;
	//�^�u�X�g�b�v
	int TabStop;
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

	//�e�L�X�g�̐F
	COLORREF TextColor;
	//�w�i�F
	COLORREF BackClolor;
	//�w�i�F�̃u���V
	HBRUSH hBkBrush;

	//�}�E�X���t���O
	BOOL mousedown;

	// ���̓��[�h
	BOOL input_mode;
	TCHAR input_string[INPUT_SIZE + 1];
	TCHAR input_buf[INPUT_SIZE + 1];

#ifdef OP_XP_STYLE
	// XP
	HMODULE hModThemes;
	HTHEME hTheme;
#endif	// OP_XP_STYLE

	//���[�U���
	long param1;
	long param2;
} VIEW_BUFFER;
#endif

/* Function Prototypes */
BOOL RegisterConsoleWindow(HINSTANCE hInstance);

#endif
/* End of source */
