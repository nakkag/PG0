/*
 * PG0
 *
 * script_struct.h
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef SCRIPT_STRUCT_H
#define SCRIPT_STRUCT_H

/* Include Files */
#include <windows.h>
#include <tchar.h>

/* Define */
#define SFUNC					__stdcall
typedef int (SFUNC *LIBFUNC)();

/* Struct */
// �G���[�^�C�v
typedef enum {
	ERR_SENTENCE = 0,
	ERR_SENTENCE_PREV,
	ERR_NOTDECLARE,
	ERR_DECLARE,
	ERR_INDEX,
	ERR_PARENTHESES,
	ERR_BLOCK,
	ERR_DIVZERO,
	ERR_OPERATOR,
	ERR_ARRAYOPERATOR,
	ERR_ALLOC,
	ERR_ARGUMENTCNT,
	ERR_FILEOPEN,
	ERR_SCRIPT,
	ERR_FUNCTION,
	ERR_FUNCTION_EXEC,
} ERROR_CODE;

// �߂�l�^�C�v
typedef enum {
	RET_RETURN = -3,
	RET_EXIT = -2,
	RET_ERROR = -1,
	RET_SUCCESS = 0,
	RET_BREAK,
	RET_CONTINUE
} RET_TYPE;

// �֐��^�C�v
typedef enum {
	FUNC_NONE = 0,
	FUNC_SCRIPT,
	FUNC_LIBRARY
} FUNCTION_TYPE;

// �ϐ��^�C�v
typedef enum {
	TYPE_INTEGER = 0,
	TYPE_FLOAT,
	TYPE_STRING,
	TYPE_ARRAY
} VALUE_TYPE;

//�V���{��
typedef enum {
	SYM_NONE = 0,
	SYM_EOF,				// '\0'
	SYM_COMMENT,			// //
	SYM_PREP,				// #
	SYM_LINEEND,			// \n
	SYM_LINESEP,			// ;
	SYM_WORDEND,			// ,

	SYM_BOPEN,				// {
	SYM_BOPEN_PRIMARY,		// {
	SYM_BCLOSE,				// }

	SYM_OPEN,				// (
	SYM_CLOSE,				// )

	SYM_ARRAYOPEN,			// [
	SYM_ARRAYCLOSE,			// ]

	SYM_EQ,					// =

	SYM_CPAND,				// &&
	SYM_CPOR,				// ||

	SYM_LEFT,				// <
	SYM_LEFTEQ,				// <=

	SYM_RIGHT,				// >
	SYM_RIGHTEQ,			// >=

	SYM_EQEQ,				// ==
	SYM_NTEQ,				// !=

	SYM_ADD,				// +
	SYM_SUB,				// -

	SYM_MULTI,				// *
	SYM_DIV,				// /
	SYM_MOD,				// %

	SYM_NOT,				// !
	SYM_PLUS,				// +
	SYM_MINS,				// -

	SYM_CONST_INT,			// ���l�萔
	SYM_DECLVARIABLE,		// �ϐ���`
	SYM_VARIABLE,			// �ϐ�
	SYM_ARRAY,				// �z��

	//�\���
	SYM_VAR,				// var
	SYM_IF,					// if
	SYM_ELSE,				// else
	SYM_WHILE,				// while
	SYM_EXIT,				// exit

	SYM_JUMP,				// jump
	SYM_JZE,				// jump Zero
	SYM_JNZ,				// jump Not Zero

	SYM_CMP,				// Compare
	SYM_CMPSTART,			// Compare start
	SYM_CMPEND,				// Compare end
	SYM_LOOP,				// loop
	SYM_LOOPSTART,			// Loop start
	SYM_LOOPEND,			// Loop end

	// Extension
	SYM_LABELEND,			// :
	SYM_AND,				// &
	SYM_OR,					// |
	SYM_XOR,				// ^
	SYM_LEFTSHIFT,			// <<
	SYM_RIGHTSHIFT,			// >>
	SYM_LEFTSHIFT_LOGICAL,	// <<<
	SYM_RIGHTSHIFT_LOGICAL,	// >>>
	SYM_BITNOT,				// ~

	SYM_COMP_EQ,			// += -= *= /= %= &= |= ^= <<= >>= <<<= >>>=

	SYM_INC,				// ++
	SYM_DEC,				// --
	SYM_BINC,				// ++ (��u)
	SYM_BDEC,				// -- (��u)

	SYM_CONST_FLOAT,		// �萔(����)
	SYM_CONST_STRING,		// ������萔

	SYM_FOR,				// for
	SYM_DO,					// do
	SYM_BREAK,				// break
	SYM_CONTINUE,			// continue
	SYM_SWITCH,				// switch
	SYM_CASE,				// case
	SYM_DEFAULT,			// default
	SYM_DAMMY,				// Dammy

	SYM_FUNCSTART,			// Function start
	SYM_FUNCEND,			// Function end
	SYM_FUNC,				// Function name
	SYM_ARGSTART,			// Argument start
	SYM_RETURN,				// return
} SYM_TYPE;

//�g�[�N��
typedef struct _TOKEN {
	SYM_TYPE sym_type;
	TCHAR *buf;
	int i;
	double f;

	//�G���[�ʒu (link)
	TCHAR *err;

	//���̃g�[�N��
	struct _TOKEN *next;
	//�u���b�N
	struct _TOKEN *target;
	//SYM_JUMP�̃����N��
	struct _TOKEN *link;

	// �s�ԍ�
	int line;
} TOKEN;

// �l
typedef struct _VALUE {
	union {
		struct _VALUEINFO *array;
		TCHAR *sValue;
		int iValue;
		double fValue;
	} u;
	VALUE_TYPE type;
	// �{��
	struct _VALUEINFO *vi;
} VALUE;

// �l���
typedef struct _VALUEINFO {
	// ���O
	TCHAR *name;
	TCHAR *org_name;
	int name_hash;

	// �l
	struct _VALUE *v;

	//���X�g
	struct _VALUEINFO *next;
} VALUEINFO;

// �֐��A�h���X���
typedef struct _FUNCADDRINFO {
	//�֐���
	TCHAR *name;
	int name_hash;

	void *addr;
	FUNCTION_TYPE func_type;
	struct _EXECINFO *ei;

	struct _FUNCADDRINFO *next;
} FUNCADDRINFO;

//���s���
typedef struct _EXECINFO {
	TCHAR *name;
	TCHAR *err;

	//�ϐ��錾�t���O
	BOOL decl;
	//�ʒu�w����s
	SYM_TYPE to_tk;

	//�X�N���v�g���
	struct _SCRIPTINFO *sci;
	//���[�J���ϐ�
	struct _VALUEINFO *vi;
	// �֐��A�h���X���
	struct _FUNCADDRINFO *funcaddr;
	//�e
	struct _EXECINFO *parent;

	//��u
	struct _VALUEINFO *inc_vi;
	struct _VALUEINFO *dec_vi;

	// EXIT�t���O
	BOOL exit;
	BOOL line_mode;
} EXECINFO;

//�֐����
typedef struct _FUNCINFO {
	//�֐���
	TCHAR *name;
	int name_hash;
	//��͖� (link)
	struct _TOKEN *tk;

	struct _FUNCINFO *next;
} FUNCINFO;

//���C�u�������
typedef struct _LIBRARYINFO {
	HANDLE hModul;

	struct _LIBRARYINFO *next;
} LIBRARYINFO;

//�X�N���v�g���
typedef struct _SCRIPTINFO {
	//�t�@�C����
	TCHAR *name;
	TCHAR *path;
	TCHAR *buf;

	//�I�v�V����
	BOOL strict_val_op;
	BOOL strict_val;
	BOOL extension;

	//��͖�
	struct _TOKEN *tk;
	//�֐����X�g
	struct _FUNCINFO *fi;
	//���C�u����
	struct _LIBRARYINFO *lib;
	//���s���
	struct _EXECINFO *ei;
	//�R�[���o�b�N
	LIBFUNC callback;
	//�X�N���v�g���̃��X�g
	struct _SCRIPTINFO *sci_top;
	struct _SCRIPTINFO *next;

	long param1;
	long param2;
} SCRIPTINFO;

#endif
/* End of source */
