/*
 * PG0
 *
 * script_parse.c
 *
 * Copyright (C) 1996-2020 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#include <windows.h>

#include "script.h"
#include "script_string.h"
#include "script_memory.h"

/* Define */
#define IS_SPACE(c)				(c == TEXT(' ') || c == TEXT('\t') || c == TEXT('\r'))

/* Global Variables */
typedef struct _PARSEINFO {
	EXECINFO *ei;
	SYM_TYPE type;
	SYM_TYPE compoundType;
	TCHAR *p;
	TCHAR *r;
	BOOL concat;
	BOOL decl;
	BOOL postfix;
	BOOL case_end;
	BOOL condition;
	BOOL extension;
	int level;
	int line;
} PARSEINFO;

/* Local Function Prototypes */
static BOOL GetExtensionToken(PARSEINFO *pi);
static void GetExtensionKeyword(PARSEINFO *pi, TCHAR *s);
static BOOL GetToken(PARSEINFO *pi);
static TOKEN *CreateToken(int type, TCHAR *p, int line);

//��
static TOKEN *Primary(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *Array(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *UnaryOperator(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *Multiplicative(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *Additive(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *Shift(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *Relational(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *Equality(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *AND(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *ExclusiveOR(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *OR(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *LogicalAND(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *LogicalOR(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *ArrayKey(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *CompoundAssignment(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *Assignment(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *Expression(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *ExpressionStatement(PARSEINFO *pi, TOKEN *cu_tk);

//����
static TOKEN *Condition(PARSEINFO *pi, TOKEN *cu_tk, BOOL isDo);
static TOKEN *IfStatement(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *SwitchStatement(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *WhileStatement(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *DoWhileStatement(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *ForStatement(PARSEINFO *pi, TOKEN *cu_tk);

static TOKEN *JumpStatement(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *CaseStatement(PARSEINFO *pi, TOKEN *cu_tk);

//��`
static TOKEN *VarDeclList(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *VarDecl(PARSEINFO *pi, TOKEN *cu_tk);

//�\��
static TOKEN *CompoundStatement(PARSEINFO *pi, TOKEN *cu_tk);
static TOKEN *StatementList(PARSEINFO *pi, TOKEN *cu_tk);

/*
 * GetExtensionToken - ������(�g��)
 */
static BOOL GetExtensionToken(PARSEINFO *pi)
{
	switch (*pi->p) {
	case TEXT(':'):
		pi->type = SYM_LABELEND;
		pi->concat = TRUE;
		break;

	case TEXT('\''):
	case TEXT('\"'):
		if (pi->concat == FALSE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		pi->type = SYM_CONST_STRING;
		pi->r = str_skip(pi->p, *pi->p);
		if (*pi->r == TEXT('\0')) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		pi->concat = FALSE;
		break;

	case TEXT('<'):
		if (pi->concat == TRUE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		if (*(pi->p + 1) == TEXT('<')) {
			if (*(pi->p + 2) == TEXT('<')) {
				pi->type = SYM_LEFTSHIFT_LOGICAL;
				pi->r += 2;
				if (*(pi->p + 3) == TEXT('=')) {
					pi->compoundType = pi->type;
					pi->type = SYM_COMP_EQ;
					pi->r++;
				}
			} else {
				pi->type = SYM_LEFTSHIFT;
				pi->r++;
				if (*(pi->p + 2) == TEXT('=')) {
					pi->compoundType = pi->type;
					pi->type = SYM_COMP_EQ;
					pi->r++;
				}
			}
			pi->concat = TRUE;
		}
		break;

	case TEXT('>'):
		if (pi->concat == TRUE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		if (*(pi->p + 1) == TEXT('>')) {
			if (*(pi->p + 2) == TEXT('>')) {
				pi->type = SYM_RIGHTSHIFT_LOGICAL;
				pi->r += 2;
				if (*(pi->p + 3) == TEXT('=')) {
					pi->compoundType = pi->type;
					pi->type = SYM_COMP_EQ;
					pi->r++;
				}
			} else {
				pi->type = SYM_RIGHTSHIFT;
				pi->r++;
				if (*(pi->p + 2) == TEXT('=')) {
					pi->compoundType = pi->type;
					pi->type = SYM_COMP_EQ;
					pi->r++;
				}
			}
			pi->concat = TRUE;
		}
		break;

	case TEXT('&'):
		if (pi->concat == TRUE) {
			if (pi->extension == TRUE) {
				break;
			}
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		if (*(pi->p + 1) != TEXT('&')) {
			pi->type = SYM_AND;
			pi->concat = TRUE;
		}
		if (*(pi->p + 1) == TEXT('=')) {
			pi->compoundType = pi->type;
			pi->type = SYM_COMP_EQ;
			pi->r++;
		}
		break;

	case TEXT('|'):
		if (pi->concat == TRUE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		if (*(pi->p + 1) != TEXT('|')) {
			pi->type = SYM_OR;
			pi->concat = TRUE;
		}
		if (*(pi->p + 1) == TEXT('=')) {
			pi->compoundType = pi->type;
			pi->type = SYM_COMP_EQ;
			pi->r++;
		}
		break;

	case TEXT('^'):
		if (pi->concat == TRUE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		pi->type = SYM_XOR;
		pi->concat = TRUE;
		if (*(pi->p + 1) == TEXT('=')) {
			pi->compoundType = pi->type;
			pi->type = SYM_COMP_EQ;
			pi->r++;
		}
		break;

	case TEXT('~'):
		if (pi->concat == FALSE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		pi->type = SYM_BITNOT;
		break;

	case TEXT('+'):
		if (pi->concat == TRUE) {
			if (*(pi->p + 1) == TEXT('+')) {
				pi->type = SYM_INC;
				pi->r++;
			}
			break;
		}
		if (*(pi->p + 1) == TEXT('+')) {
			pi->type = SYM_BINC;
			pi->r++;
			pi->concat = FALSE;
		}
		if (*(pi->p + 1) == TEXT('=')) {
			pi->type = SYM_COMP_EQ;
			pi->compoundType = SYM_ADD;
			pi->r++;
			pi->concat = TRUE;
		}
		break;

	case TEXT('-'):
		if (pi->concat == TRUE) {
			if (*(pi->p + 1) == TEXT('-')) {
				pi->type = SYM_DEC;
				pi->r++;
			}
			break;
		}
		if (*(pi->p + 1) == TEXT('-')) {
			pi->type = SYM_BDEC;
			pi->r++;
			pi->concat = FALSE;
		}
		if (*(pi->p + 1) == TEXT('=')) {
			pi->type = SYM_COMP_EQ;
			pi->compoundType = SYM_SUB;
			pi->r++;
			pi->concat = TRUE;
		}
		break;

	case TEXT('*'):
		if (pi->concat == TRUE) {
			break;
		}
		if (*(pi->p + 1) == TEXT('=')) {
			pi->type = SYM_COMP_EQ;
			pi->compoundType = SYM_MULTI;
			pi->r++;
			pi->concat = TRUE;
		}
		break;

	case TEXT('/'):
		if (pi->concat == TRUE) {
			break;
		}
		if (*(pi->p + 1) == TEXT('=')) {
			pi->type = SYM_COMP_EQ;
			pi->compoundType = SYM_DIV;
			pi->r++;
			pi->concat = TRUE;
		}
		break;

	case TEXT('%'):
		if (pi->concat == TRUE) {
			break;
		}
		if (*(pi->p + 1) == TEXT('=')) {
			pi->type = SYM_COMP_EQ;
			pi->compoundType = SYM_MOD;
			pi->r++;
			pi->concat = TRUE;
		}
		break;
	}
	return TRUE;
}

/*
 * GetExtensionKeyword - �\���(�g��)
 */
static void GetExtensionKeyword(PARSEINFO *pi, TCHAR *s)
{
	//�\���
	if (str_cmp_i(s, TEXT("for")) == 0) {
		pi->type = SYM_FOR;
	} else if (str_cmp_i(s, TEXT("do")) == 0) {
		pi->type = SYM_DO;
	} else if (str_cmp_i(s, TEXT("break")) == 0) {
		pi->type = SYM_BREAK;
	} else if (str_cmp_i(s, TEXT("continue")) == 0) {
		pi->type = SYM_CONTINUE;
	} else if (str_cmp_i(s, TEXT("switch")) == 0) {
		pi->type = SYM_SWITCH;
	} else if (str_cmp_i(s, TEXT("case")) == 0) {
		pi->type = SYM_CASE;
	} else if (str_cmp_i(s, TEXT("default")) == 0) {
		pi->type = SYM_DEFAULT;
	} else if (str_cmp_i(s, TEXT("return")) == 0) {
		pi->type = SYM_RETURN;
	}else if(str_cmp_i(s, TEXT("function")) == 0){
		pi->type = SYM_FUNCSTART;
	}
}

/*
 * GetToken - ������
 */
static BOOL GetToken(PARSEINFO *pi)
{
	TCHAR *r, *s;
	int prev_type = pi->type;

#ifdef UNICODE
	for (pi->p = pi->r; IS_SPACE(*pi->p) || *pi->p == TEXT('�@'); pi->p++);
#else
	for (pi->p = pi->r; IS_SPACE(*pi->p) || (*pi->p == (TCHAR)0x81 && *(pi->p + 1) == (TCHAR)0x40); pi->p++) {
		if (*pi->p == (TCHAR)0x81 && *(pi->p + 1) == (TCHAR)0x40) {
			// �S�p��
			pi->p++;
		}
	}
#endif
	pi->r = pi->p;
	pi->type = 0;

	if (pi->extension == TRUE) {
		// Extension
		if (GetExtensionToken(pi) == FALSE) {
			return FALSE;
		}
		if (pi->type != 0) {
			pi->r++;
			return TRUE;
		}
	}

	switch (*pi->p) {
	case TEXT('\0'):
		pi->type = SYM_EOF;
		return TRUE;

	case '#':
		pi->type = SYM_PREP;
		pi->concat = TRUE;
		break;

	case TEXT('\n'):
		pi->line++;
		if (pi->concat == FALSE ||
			prev_type == SYM_EXIT || prev_type == SYM_RETURN ||
			prev_type == SYM_BREAK || prev_type == SYM_CONTINUE) {
			pi->type = SYM_LINEEND;
			pi->concat = TRUE;
		} else {
			pi->p++;
			pi->r = pi->p;
			return GetToken(pi);
		}
		break;

	case TEXT(';'):
		pi->type = SYM_LINESEP;
		pi->concat = TRUE;
		break;

	case TEXT(','):
		pi->type = SYM_WORDEND;
		pi->concat = TRUE;
		break;

	case TEXT('{'):
		if (pi->concat == FALSE) {
			Error(pi->ei, ERR_SENTENCE_PREV, pi->p, NULL);
			return FALSE;
		}
		if (get_pair_brace(pi->p) == NULL) {
			Error(pi->ei, ERR_PARENTHESES, pi->p, NULL);
			return FALSE;
		}
		pi->type = SYM_BOPEN;
		pi->concat = TRUE;
		break;

	case TEXT('}'):
		pi->type = SYM_BCLOSE;
		pi->concat = FALSE;
		break;

	case TEXT('('):
		if (pi->concat == FALSE) {
			Error(pi->ei, ERR_SENTENCE_PREV, pi->p, NULL);
			return FALSE;
		}
		if (get_pair_brace(pi->p) == NULL) {
			Error(pi->ei, ERR_PARENTHESES, pi->p, NULL);
			return FALSE;
		}
		pi->type = SYM_OPEN;
		pi->concat = TRUE;
		break;

	case TEXT(')'):
		pi->type = SYM_CLOSE;
		pi->concat = FALSE;
		break;

	case TEXT('['):
		if (pi->concat == TRUE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		if (get_pair_brace(pi->p) == NULL) {
			Error(pi->ei, ERR_PARENTHESES, pi->p, NULL);
			return FALSE;
		}
		pi->type = SYM_ARRAYOPEN;
		pi->concat = TRUE;
		break;

	case TEXT(']'):
		pi->type = SYM_ARRAYCLOSE;
		pi->concat = FALSE;
		break;

	case TEXT('='):
		if (pi->concat == TRUE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		switch (*(pi->p + 1)) {
		case TEXT('='):
			pi->type = SYM_EQEQ;
			pi->r++;
			break;

		default:
			pi->type = SYM_EQ;
			break;
		}
		pi->concat = TRUE;
		break;

	case TEXT('!'):
		if (pi->concat == TRUE) {
			pi->type = SYM_NOT;
			break;
		}
		if (*(pi->p + 1) == TEXT('=')) {
			pi->type = SYM_NTEQ;
			pi->r++;
		} else {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		pi->concat = TRUE;
		break;

	case TEXT('<'):
		if (pi->concat == TRUE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		if (*(pi->p + 1) == TEXT('=')) {
			pi->type = SYM_LEFTEQ;
			pi->r++;
		} else {
			pi->type = SYM_LEFT;
		}
		pi->concat = TRUE;
		break;

	case TEXT('>'):
		if (pi->concat == TRUE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		if (*(pi->p + 1) == TEXT('=')) {
			pi->type = SYM_RIGHTEQ;
			pi->r++;
		} else {
			pi->type = SYM_RIGHT;
		}
		pi->concat = TRUE;
		break;

	case TEXT('&'):
		if (pi->concat == TRUE) {
			if (pi->extension == TRUE) {
				break;
			}
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		if (*(pi->p + 1) == TEXT('&')) {
			pi->type = SYM_CPAND;
			pi->r++;
		} else {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		pi->concat = TRUE;
		break;

	case TEXT('|'):
		if (pi->concat == TRUE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		if (*(pi->p + 1) == TEXT('|')) {
			pi->type = SYM_CPOR;
			pi->r++;
		} else {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		pi->concat = TRUE;
		break;

	case TEXT('*'):
		if (pi->concat == TRUE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		pi->type = SYM_MULTI;
		pi->concat = TRUE;
		break;

	case TEXT('/'):
		if (*(pi->p + 1) == TEXT('/')) {
			//�R�����g
			for (; *pi->r != TEXT('\0') && *pi->r != TEXT('\r') && *pi->r != TEXT('\n'); pi->r++);
			return GetToken(pi);
		}
		if (pi->concat == TRUE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		pi->type = SYM_DIV;
		pi->concat = TRUE;
		break;

	case TEXT('%'):
		if (pi->concat == TRUE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return FALSE;
		}
		pi->type = SYM_MOD;
		pi->concat = TRUE;
		break;

	case TEXT('+'):
		if (pi->concat == TRUE) {
			if (*(pi->p + 1) == TEXT('+')) {
				Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
				return FALSE;
			} else {
				pi->type = SYM_PLUS;
			}
			break;
		}
		pi->type = SYM_ADD;
		pi->concat = TRUE;
		break;

	case TEXT('-'):
		if (pi->concat == TRUE) {
			if (*(pi->p + 1) == TEXT('-')) {
				Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
				return FALSE;
			} else {
				pi->type = SYM_MINS;
			}
			break;
		}
		pi->type = SYM_SUB;
		pi->concat = TRUE;
		break;
	}

	if (pi->type != 0) {
		pi->r++;
		return TRUE;
	}
	if (pi->concat == FALSE) {
		Error(pi->ei, ERR_SENTENCE_PREV, pi->p, NULL);
		return FALSE;
	}
	pi->concat = FALSE;

	if ((*pi->p >= TEXT('0') && *pi->p <= TEXT('9')) ||
		(*pi->p == TEXT('.') && *(pi->p + 1) >= TEXT('0') && *(pi->p + 1) <= TEXT('9'))) {
		//�萔
		pi->type = SYM_CONST_INT;
		if (pi->extension == TRUE && *pi->p == TEXT('0') && (*(pi->p + 1) == TEXT('x') || *(pi->p + 1) == TEXT('X'))) {
			for (pi->r += 2; (*pi->r >= TEXT('0') && *pi->r <= TEXT('9')) ||
				(*pi->r >= TEXT('A') && *pi->r <= TEXT('F')) ||
				(*pi->r >= TEXT('a') && *pi->r <= TEXT('f')); pi->r++);
		} else if (pi->extension == TRUE && *pi->p == TEXT('0') && *pi->p != TEXT('.') && *(pi->p + 1) != TEXT('.')) {
			for (pi->r++; *pi->r >= TEXT('0') && *pi->r <= TEXT('9'); pi->r++);
		} else {
			if (pi->extension == TRUE && *pi->r == TEXT('.')) {
				pi->type = SYM_CONST_FLOAT;
			}
			for (pi->r++; (*pi->r >= TEXT('0') && *pi->r <= TEXT('9')) || *pi->r == TEXT('.'); pi->r++) {
				if (pi->extension == TRUE && *pi->r == TEXT('.')) {
					if (pi->type == SYM_CONST_FLOAT) {
						Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
						return FALSE;
					}
					pi->type = SYM_CONST_FLOAT;
				}
			}
		}
		return TRUE;
	}

	if (pi->extension == TRUE) {
		pi->r = (*pi->p == TEXT('&')) ? pi->p + 1 : pi->p;
	} else {
		pi->r = pi->p;
	}
	for (; (*pi->r >= TEXT('a') && *pi->r <= TEXT('z')) || (*pi->r >= TEXT('A') && *pi->r <= TEXT('Z')) ||
		(*pi->r >= TEXT('0') && *pi->r <= TEXT('9')) || *pi->r == TEXT('_'); pi->r++);
	if (pi->p == pi->r) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return FALSE;
	}
	s = alloc_copy_n(pi->p, (int)(pi->r - pi->p));
	if (s == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return FALSE;
	}

	//�\���
	if (str_cmp_i(s, TEXT("var")) == 0) {
		pi->type = SYM_VAR;
	} else if (str_cmp_i(s, TEXT("exit")) == 0) {
		pi->type = SYM_EXIT;
	} else if (str_cmp_i(s, TEXT("if")) == 0) {
		pi->type = SYM_IF;
	} else if (str_cmp_i(s, TEXT("else")) == 0) {
		pi->type = SYM_ELSE;
	} else if (str_cmp_i(s, TEXT("while")) == 0) {
		pi->type = SYM_WHILE;
	}
	if (pi->extension == TRUE) {
		// Extension
		GetExtensionKeyword(pi, s);
	}
	mem_free(&s);
	if (pi->type != 0) {
		pi->concat = TRUE;
		return TRUE;
	}

#ifdef UNICODE
	for (r = pi->r; *r == TEXT(' ') || *r == TEXT('\t') || *r == TEXT('�@'); r++);
#else
	for (r = pi->r; *r == TEXT(' ') || *r == TEXT('\t') || (*r == (TCHAR)0x81 && *(r + 1) == (TCHAR)0x40); r++) {
		if (*r == (TCHAR)0x81 && *(r + 1) == (TCHAR)0x40) {
			// �S�p��
			r++;
		}
	}
#endif
	if (pi->extension == TRUE && *r == '(') {
		// �֐�
		pi->type = SYM_FUNC;
		pi->concat = TRUE;
	} else {
		// �ϐ�
		pi->type = SYM_VARIABLE;
	}
	return TRUE;
}

/*
 * CreateToken - �g�[�N���̍쐬
 */
static TOKEN *CreateToken(int type, TCHAR *p, int line)
{
	TOKEN *tk;

	tk = mem_calloc(sizeof(TOKEN));
	if (tk == NULL) {
		return NULL;
	}
	tk->sym_type = type;
	tk->line = line;
	tk->err = p;
	return tk;
}

/*
 * Primary - �v�f
 */
static TOKEN *Primary(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk, ttk;
	TCHAR *s;

	switch (pi->type) {
	case SYM_BOPEN:
		cu_tk = cu_tk->next = CreateToken(SYM_BOPEN_PRIMARY, pi->p, pi->line);
		if (cu_tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		cu_tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			return NULL;
		}
		ttk.next = NULL;
		if (Expression(pi, &ttk) == NULL) {
			FreeToken(ttk.next);
			return NULL;
		}
		cu_tk->target = ttk.next;

		if (pi->type != SYM_BCLOSE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return NULL;
		}
		if (GetToken(pi) == FALSE) {
			return NULL;
		}
		break;

	case SYM_OPEN:
		if (GetToken(pi) == FALSE) {
			return NULL;
		}
		cu_tk = ArrayKey(pi, cu_tk);
		if (pi->type != SYM_CLOSE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return NULL;
		}
		if (GetToken(pi) == FALSE) {
			return NULL;
		}
		break;

	case SYM_FUNC:
		if (*pi->p == TEXT('&')) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return NULL;
		}
		//�����̊J�n�ʒu��ݒ�
		cu_tk = cu_tk->next = CreateToken(SYM_ARGSTART, pi->p, -1);
		if (cu_tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		cu_tk->buf = alloc_copy(TEXT("(argument start)"));
#endif
		//�֐���
		tk = CreateToken(pi->type, pi->p, pi->line);
		if (tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
		tk->buf = alloc_copy_n(pi->p, (int)(pi->r - pi->p));
		if (tk->buf == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			FreeToken(tk);
			return NULL;
		}
		str_lower(tk->buf);
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}

		// (
		if (pi->type != SYM_OPEN) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			FreeToken(tk);
			return NULL;
		}
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}

		//����
		cu_tk = Expression(pi, cu_tk);
		if (cu_tk == NULL) {
			FreeToken(tk);
			return NULL;
		}

		// )
		if (pi->type != SYM_CLOSE) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			FreeToken(tk);
			return NULL;
		}
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}

		//�֐�����ǉ�
		cu_tk = cu_tk->next = tk;
		break;

	case SYM_VARIABLE:
		//�ϐ�
		if (pi->decl == TRUE || pi->ei->decl == TRUE) {
			//�ϐ��錾
			cu_tk = cu_tk->next = CreateToken(SYM_DECLVARIABLE, pi->p, pi->line);
		} else {
			cu_tk = cu_tk->next = CreateToken(pi->type, pi->p, pi->line);
		}
		if (cu_tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
		cu_tk->buf = alloc_copy_n(pi->p, (int)(pi->r - pi->p));
		if (cu_tk->buf == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
		if (GetToken(pi) == FALSE) {
			return NULL;
		}
		break;

	case SYM_CONST_INT:
		//�萔(����)
		cu_tk = cu_tk->next = CreateToken(pi->type, pi->p, pi->line);
		if (cu_tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
		s = alloc_copy_n(pi->p, (int)(pi->r - pi->p));
		if (s == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}

		if (*s != TEXT('0') || *(s + 1) == TEXT('\0')) {
			//10�i��
			cu_tk->i = _ttoi(s);

		} else if (*(s + 1) == TEXT('x') || *(s + 1) == TEXT('X')) {
			//16�i��
			cu_tk->i = x2d(s + 2);
		} else {
			//8�i��
			cu_tk->i = o2d(s + 1);
		}
		mem_free(&s);

		if (GetToken(pi) == FALSE) {
			return NULL;
		}
		if (pi->decl == TRUE || pi->type == SYM_EQ || pi->type == SYM_COMP_EQ) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return NULL;
		}
		break;

	case SYM_CONST_FLOAT:
		//�萔(����)
		cu_tk = cu_tk->next = CreateToken(pi->type, pi->p, pi->line);
		if (cu_tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
		s = alloc_copy_n(pi->p, (int)(pi->r - pi->p));
		if (s == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
		cu_tk->f = _ttof(s);
		mem_free(&s);

		if (GetToken(pi) == FALSE) {
			return NULL;
		}
		if (pi->decl == TRUE || pi->type == SYM_EQ || pi->type == SYM_COMP_EQ) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return NULL;
		}
		break;

	case SYM_CONST_STRING:
		//������萔
		cu_tk = cu_tk->next = CreateToken(pi->type, pi->p, pi->line);
		if (cu_tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
		cu_tk->buf = alloc_copy_n(pi->p + 1, (int)(pi->r - pi->p) - 2);
		if (cu_tk->buf == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
		if (GetToken(pi) == FALSE) {
			return NULL;
		}
		if (pi->decl == TRUE || pi->type == SYM_EQ || pi->type == SYM_COMP_EQ) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			return NULL;
		}
		break;
	}
	return cu_tk;
}

/*
 * Array - �z��
 */
static TOKEN *Array(PARSEINFO *pi, TOKEN *cu_tk)
{
	cu_tk = Primary(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_ARRAYOPEN) {
		if (GetToken(pi) == FALSE) {
			return NULL;
		}
		if (pi->type != SYM_ARRAYCLOSE) {
			//�v�f
			cu_tk = LogicalOR(pi, cu_tk);
			if (cu_tk == NULL) {
				return NULL;
			}
			if (pi->type != SYM_ARRAYCLOSE) {
				Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
				return NULL;
			}
			//�z�񎯕ʎq�̒ǉ�
			cu_tk = cu_tk->next = CreateToken(SYM_ARRAY, pi->p, pi->line);
			if (cu_tk == NULL) {
				Error(pi->ei, ERR_ALLOC, pi->p, NULL);
				return NULL;
			}
#ifdef DEBUG_SET
			cu_tk->buf = alloc_copy(TEXT("(Array)"));
#endif
		}
		if (GetToken(pi) == FALSE) {
			return NULL;
		}
		cu_tk = Primary(pi, cu_tk);
		if (cu_tk == NULL) {
			return NULL;
		}
	}
	return cu_tk;
}

/*
 * UnaryOperator - �P�����Z�q
 */
static TOKEN *UnaryOperator(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk;
#ifdef DEBUG_SET
	TCHAR *tmp;
#endif

	cu_tk = Array(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_NOT || pi->type == SYM_BITNOT ||
		pi->type == SYM_PLUS || pi->type == SYM_MINS ||
		pi->type == SYM_INC || pi->type == SYM_DEC ||
		pi->type == SYM_BINC || pi->type == SYM_BDEC) {

		tk = CreateToken(pi->type, pi->p, pi->line);
		if (tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		tmp = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (pi->type == SYM_BINC || pi->type == SYM_BDEC) {
			//��u
#ifdef DEBUG_SET
			tk->buf = alloc_join(tmp, TEXT("(Postfix)"));
			mem_free(&tmp);
#endif
			pi->postfix = TRUE;
			cu_tk = cu_tk->next = tk;
			if (GetToken(pi) == FALSE) {
				return NULL;
			}
			cu_tk = UnaryOperator(pi, cu_tk);
			if (cu_tk == NULL) {
				return NULL;
			}
		} else {
			//�O�u
#ifdef DEBUG_SET
			tk->buf = tmp;
#endif
			if (GetToken(pi) == FALSE) {
				FreeToken(tk);
				return NULL;
			}
			cu_tk = UnaryOperator(pi, cu_tk);
			if (cu_tk == NULL) {
				FreeToken(tk);
				return NULL;
			}
			cu_tk = cu_tk->next = tk;
		}
	}
	return cu_tk;
}

/*
 * Multiplicative - ��Z�A���Z�A��]
 */
static TOKEN *Multiplicative(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk;

	cu_tk = UnaryOperator(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_MULTI || pi->type == SYM_DIV || pi->type == SYM_MOD) {
		tk = CreateToken(pi->type, pi->p, pi->line);
		if (tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = UnaryOperator(pi, cu_tk);
		if (cu_tk == NULL) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = cu_tk->next = tk;
	}
	return cu_tk;
}

/*
 * Additive - ���Z�A���Z
 */
static TOKEN *Additive(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk;

	cu_tk = Multiplicative(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_ADD || pi->type == SYM_SUB) {
		tk = CreateToken(pi->type, pi->p, pi->line);
		if (tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = Multiplicative(pi, cu_tk);
		if (cu_tk == NULL) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = cu_tk->next = tk;
	}
	return cu_tk;
}

/*
 * Shift - �r�b�g�V�t�g
 */
static TOKEN *Shift(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk;

	cu_tk = Additive(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_LEFTSHIFT || pi->type == SYM_RIGHTSHIFT ||
		pi->type == SYM_LEFTSHIFT_LOGICAL || pi->type == SYM_RIGHTSHIFT_LOGICAL) {
		tk = CreateToken(pi->type, pi->p, pi->line);
		if (tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = Additive(pi, cu_tk);
		if (cu_tk == NULL) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = cu_tk->next = tk;
	}
	return cu_tk;
}

/*
 * Relational - ��r (<, >, <=, >=)
 */
static TOKEN *Relational(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk;

	cu_tk = Shift(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_LEFT || pi->type == SYM_LEFTEQ ||
		pi->type == SYM_RIGHT || pi->type == SYM_RIGHTEQ) {
		tk = CreateToken(pi->type, pi->p, pi->line);
		if (tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = Shift(pi, cu_tk);
		if (cu_tk == NULL) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = cu_tk->next = tk;
	}
	return cu_tk;
}

/*
 * Equality - ��r (==, !=)
 */
static TOKEN *Equality(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk;

	cu_tk = Relational(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_EQEQ || pi->type == SYM_NTEQ) {
		tk = CreateToken(pi->type, pi->p, pi->line);
		if (tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = Relational(pi, cu_tk);
		if (cu_tk == NULL) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = cu_tk->next = tk;
	}
	return cu_tk;
}

/*
 * AND - �_����
 */
static TOKEN *AND(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk;

	cu_tk = Equality(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_AND) {
		tk = CreateToken(pi->type, pi->p, pi->line);
		if (tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = Equality(pi, cu_tk);
		if (cu_tk == NULL) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = cu_tk->next = tk;
	}
	return cu_tk;
}

/*
 * ExclusiveOR - �r���I�_���a
 */
static TOKEN *ExclusiveOR(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk;

	cu_tk = AND(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_XOR) {
		tk = CreateToken(pi->type, pi->p, pi->line);
		if (tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = AND(pi, cu_tk);
		if (cu_tk == NULL) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = cu_tk->next = tk;
	}
	return cu_tk;
}

/*
 * OR - �_���a
 */
static TOKEN *OR(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk;

	cu_tk = ExclusiveOR(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_OR) {
		tk = CreateToken(pi->type, pi->p, pi->line);
		if (tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = ExclusiveOR(pi, cu_tk);
		if (cu_tk == NULL) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = cu_tk->next = tk;
	}
	return cu_tk;
}

/*
 * LogicalAND - �_����
 */
static TOKEN *LogicalAND(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk, *jmp_tk;

	cu_tk = OR(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_CPAND) {
		//�O�̎���0�̏ꍇ��AND�̏I���Ɉړ�
		jmp_tk = cu_tk = cu_tk->next = CreateToken(SYM_JZE, pi->p, -1);
		if (cu_tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		cu_tk->buf = alloc_copy(TEXT("jze AND_END"));
#endif
		// LogicalAND
		tk = CreateToken(pi->type, pi->p, pi->line);
		if (tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = OR(pi, cu_tk);
		if (cu_tk == NULL) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = cu_tk->next = tk;
		//ANDEND:
		jmp_tk->link = cu_tk = cu_tk->next = CreateToken(SYM_DAMMY, pi->p, -1);
		if (cu_tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		cu_tk->buf = alloc_copy(TEXT("AND_END:"));
#endif
	}
	return cu_tk;
}

/*
 * LogicalOR - �_���a
 */
static TOKEN *LogicalOR(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk, *jmp_tk;

	cu_tk = LogicalAND(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_CPOR) {
		//�O�̎���0�ȊO�̏ꍇ��OR�̏I���Ɉړ�
		jmp_tk = cu_tk = cu_tk->next = CreateToken(SYM_JNZ, pi->p, -1);
		if (cu_tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		cu_tk->buf = alloc_copy(TEXT("jnz OR_END"));
#endif
		// LogicalOR
		tk = CreateToken(pi->type, pi->p, pi->line);
		if (tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = LogicalAND(pi, cu_tk);
		if (cu_tk == NULL) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = cu_tk->next = tk;
		//OREND:
		jmp_tk->link = cu_tk = cu_tk->next = CreateToken(SYM_DAMMY, pi->p, -1);
		if (cu_tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		cu_tk->buf = alloc_copy(TEXT("OR_END:"));
#endif
	}
	return cu_tk;
}

/*
* ArrayKey - �z��̃L�[
*/
static TOKEN *ArrayKey(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk;

	cu_tk = LogicalOR(pi, cu_tk);
	if (pi->case_end == TRUE) {
		return cu_tk;
	}
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_LABELEND) {
		tk = CreateToken(pi->type, pi->p, pi->line);
		if (tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = LogicalOR(pi, cu_tk);
		if (cu_tk == NULL) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = cu_tk->next = tk;
	}
	return cu_tk;
}

/*
 * CompoundAssignment - ����������Z��
 */
static TOKEN *CompoundAssignment(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *comp_eqtk;
	TOKEN *comptk;
	TOKEN *eqtk;

	cu_tk = ArrayKey(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_COMP_EQ) {
		// ����������Z�q
		comp_eqtk = CreateToken(pi->type, pi->p, pi->line);
		if (comp_eqtk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		comp_eqtk->buf = alloc_copy(TEXT("(=)"));
#endif
		// �Z�p���Z�q
		comptk = CreateToken(pi->compoundType, pi->p, pi->line);
		if (comptk == NULL) {
			FreeToken(comp_eqtk);
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		comptk->buf = alloc_copy_n(pi->p, pi->r - pi->p - 1);
#endif
		// ������Z�q
		eqtk = CreateToken(SYM_EQ, pi->p, pi->line);
		if (eqtk == NULL) {
			FreeToken(comp_eqtk);
			FreeToken(comptk);
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		eqtk->buf = alloc_copy(TEXT("="));
#endif
		if (GetToken(pi) == FALSE) {
			FreeToken(comp_eqtk);
			FreeToken(comptk);
			FreeToken(eqtk);
			return NULL;
		}
		cu_tk = ArrayKey(pi, cu_tk);
		if (cu_tk == NULL) {
			FreeToken(comp_eqtk);
			FreeToken(comptk);
			FreeToken(eqtk);
			return NULL;
		}
		cu_tk = cu_tk->next = comp_eqtk;
		cu_tk = cu_tk->next = comptk;
		cu_tk = cu_tk->next = eqtk;
	}
	return cu_tk;
}

/*
 * Assignment - �����
 */
static TOKEN *Assignment(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk;

	if (pi->condition == TRUE) {
		return ArrayKey(pi, cu_tk);
	}
	cu_tk = CompoundAssignment(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_EQ) {
		tk = CreateToken(pi->type, pi->p, pi->line);
		if (tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = Assignment(pi, cu_tk);
		if (cu_tk == NULL) {
			FreeToken(tk);
			return NULL;
		}
		cu_tk = cu_tk->next = tk;
	}
	return cu_tk;
}

/*
 * Expression - ��1 , ��2
 */
static TOKEN *Expression(PARSEINFO *pi, TOKEN *cu_tk)
{
	cu_tk = Assignment(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	while (pi->type == SYM_WORDEND) {
		cu_tk = cu_tk->next = CreateToken(pi->type, pi->p, pi->line);
		if (cu_tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		cu_tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			return NULL;
		}
		cu_tk = Assignment(pi, cu_tk);
		if (cu_tk == NULL) {
			return NULL;
		}
	}
	return cu_tk;
}

/*
 * ExpressionStatement - ��
 */
static TOKEN *ExpressionStatement(PARSEINFO *pi, TOKEN *cu_tk)
{
	cu_tk = Expression(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	if (pi->type == SYM_EOF || pi->type == SYM_BCLOSE) {
		return cu_tk;
	}
	if (pi->type != SYM_LINEEND && pi->type != SYM_LINESEP) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	cu_tk = cu_tk->next = CreateToken(pi->type, pi->p, (pi->type != SYM_LINEEND) ? pi->line : (pi->line - 1));
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	if (pi->type != SYM_LINEEND) {
		cu_tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
	} else {
		cu_tk->buf = alloc_copy(TEXT(";"));
	}
#endif
	if (GetToken(pi) == FALSE) {
		return NULL;
	}
	return cu_tk;
}

/*
 * Condition - ������
 */
static TOKEN *Condition(PARSEINFO *pi, TOKEN *cu_tk, BOOL isDo)
{
	TCHAR *r;
	// (
	if (GetToken(pi) == FALSE) {
		return NULL;
	}
	if (pi->type != SYM_OPEN) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	if (GetToken(pi) == FALSE) {
		return NULL;
	}
	if (pi->type == SYM_CLOSE) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	//������
	pi->condition = TRUE;
	cu_tk = ArrayKey(pi, cu_tk);
	pi->condition = FALSE;
	if (cu_tk == NULL) {
		return NULL;
	}
	// )
	if (pi->type != SYM_CLOSE) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	pi->concat = (isDo == FALSE) ? TRUE : FALSE;
	r = pi->p;
	if (GetToken(pi) == FALSE) {
		return NULL;
	}
	if (isDo == FALSE && pi->type != SYM_BOPEN) {
		Error(pi->ei, ERR_BLOCK, r, NULL);
		return NULL;
	}
	return cu_tk;
}

/*
 * IfStatement - if��
 */
static TOKEN *IfStatement(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *end_tk, *else_tk;
	int line;

	//CMP�J�n�ʒu
	cu_tk = cu_tk->next = CreateToken(SYM_CMPSTART, pi->p, pi->line);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("CMPSTART:"));
#endif

	//������
	line = pi->line;
	cu_tk = Condition(pi, cu_tk, FALSE);
	if (cu_tk == NULL) {
		return NULL;
	}
	//CMP��ǉ�
	cu_tk = cu_tk->next = CreateToken(SYM_CMP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("CMP"));
#endif

	//ELSE�Ɉړ�
	else_tk = cu_tk = cu_tk->next = CreateToken(SYM_JUMP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("jump ELSE"));
#endif

	//�����Ώ�
	cu_tk = StatementList(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	line = pi->line;

	//��r�I���ʒu�Ɉړ�
	end_tk = cu_tk = cu_tk->next = CreateToken(SYM_JUMP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("jump CMPEND"));
#endif

	//ELSE:
	else_tk->link = cu_tk = cu_tk->next = CreateToken(SYM_ELSE, pi->p, line);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("ELSE:"));
#endif
	if (pi->type == SYM_ELSE) {
		TCHAR *r = pi->p;
		//�����Ώ�
		pi->concat = TRUE;
		if (GetToken(pi) == FALSE) {
			return NULL;
		}
		if (pi->extension == TRUE && pi->type == SYM_IF) {
			cu_tk = IfStatement(pi, cu_tk);
			if (cu_tk == NULL) {
				return NULL;
			}
		} else {
			if (pi->type != SYM_BOPEN) {
				Error(pi->ei, ERR_BLOCK, r, NULL);
				return NULL;
			}
			cu_tk = StatementList(pi, cu_tk);
			if (cu_tk == NULL) {
				return NULL;
			}
		}
	}

	//��r�I���ʒu
	end_tk->link = cu_tk = cu_tk->next = CreateToken(SYM_CMPEND, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("CMPEND:"));
#endif
	return cu_tk;
}

/*
 * SwitchStatement - switch��
 */
static TOKEN *SwitchStatement(PARSEINFO *pi, TOKEN *cu_tk)
{
	int line = pi->line;
	//������
	cu_tk = Condition(pi, cu_tk, FALSE);
	if (cu_tk == NULL) {
		return NULL;
	}
	//switch�̒ǉ�
	cu_tk = cu_tk->next = CreateToken(SYM_SWITCH, pi->p, line);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("SWITCH"));
#endif

	//�����Ώ�
	if (pi->type != SYM_BOPEN) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	cu_tk = StatementList(pi, cu_tk);
	return cu_tk;
}

/*
 * WhileStatement - while��
 */
static TOKEN *WhileStatement(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *st_tk, *end_tk, ttk;

	//���[�v�J�n�ʒu
	st_tk = cu_tk = cu_tk->next = CreateToken(SYM_LOOPSTART, pi->p, pi->line);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("LOOPSTART:"));
#endif

	//������
	cu_tk = Condition(pi, cu_tk, FALSE);
	if (cu_tk == NULL) {
		return NULL;
	}
	//LOOP��ǉ�
	cu_tk = cu_tk->next = CreateToken(SYM_LOOP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("LOOP"));
#endif

	//�����Ώ�
	ttk.next = NULL;
	if (StatementList(pi, &ttk) == NULL) {
		FreeToken(ttk.next);
		return NULL;
	}
	cu_tk->target = ttk.next;

	//���[�v�I���ʒu�Ɉړ�
	end_tk = cu_tk = cu_tk->next = CreateToken(SYM_JUMP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("jump LOOPEND"));
#endif

	//���[�v�J�n�ʒu�Ɉړ�
	cu_tk = cu_tk->next = CreateToken(SYM_JUMP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("jump LOOPSTART"));
#endif
	cu_tk->link = st_tk;

	//���[�v�I���ʒu
	end_tk->link = cu_tk = cu_tk->next = CreateToken(SYM_LOOPEND, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("LOOPEND:"));
#endif
	return cu_tk;
}

/*
 * DoWhileStatement - do�`while��
 */
static TOKEN *DoWhileStatement(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *st_tk, *end_tk, ttk;

	//���[�v�J�n�ʒu
	st_tk = cu_tk = cu_tk->next = CreateToken(SYM_LOOPSTART, pi->p, pi->line);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("LOOPSTART:"));
#endif

	//LOOP��ǉ�
	cu_tk = cu_tk->next = CreateToken(SYM_LOOP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("LOOP"));
#endif
	if (GetToken(pi) == FALSE) {
		return NULL;
	}
	if (pi->type != SYM_BOPEN) {
		Error(pi->ei, ERR_BLOCK, pi->p, NULL);
		return NULL;
	}

	//�����Ώ�
	ttk.next = NULL;
	if (StatementList(pi, &ttk) == NULL) {
		FreeToken(ttk.next);
		return NULL;
	}
	cu_tk->target = ttk.next;

	//LOOP�p�̃_�~�[
	cu_tk = cu_tk->next = CreateToken(SYM_DAMMY, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("DAMMY:"));
#endif

	//while�������Ă��邩�`�F�b�N
	if (pi->type != SYM_WHILE) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	//������
	cu_tk = Condition(pi, cu_tk, TRUE);
	if (cu_tk == NULL) {
		return NULL;
	}
	//CMP��ǉ�
	cu_tk = cu_tk->next = CreateToken(SYM_CMP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("CMP"));
#endif

	if (pi->type != SYM_LINEEND && pi->type != SYM_LINESEP && pi->type != SYM_EOF && pi->type != SYM_BCLOSE) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}

	//���[�v�I���ʒu�Ɉړ�
	end_tk = cu_tk = cu_tk->next = CreateToken(SYM_JUMP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("jump LOOPEND"));
#endif

	//���[�v�J�n�ʒu�Ɉړ�
	cu_tk = cu_tk->next = CreateToken(SYM_JUMP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("jump LOOPSTART"));
#endif
	cu_tk->link = st_tk;

	//���[�v�I���ʒu
	end_tk->link = cu_tk = cu_tk->next = CreateToken(SYM_LOOPEND, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("LOOPEND:"));
#endif
	return cu_tk;
}

/*
 * ForStatement - for��
 */
static TOKEN *ForStatement(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *st_tk, *end_tk, re_tk, ttk;
	int line = pi->line;

	if (GetToken(pi) == FALSE) {
		return NULL;
	}

	// (
	if (pi->type != SYM_OPEN) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	if (GetToken(pi) == FALSE) {
		return NULL;
	}

	//������
	if (pi->type == SYM_VAR) {
		//var
		if (GetToken(pi) == FALSE) {
			return NULL;
		}
		cu_tk = VarDeclList(pi, cu_tk);
	} else {
		cu_tk = Expression(pi, cu_tk);
	}
	if (cu_tk == NULL) {
		return NULL;
	}
	// ;
	if (pi->type != SYM_LINESEP) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	cu_tk = cu_tk->next = CreateToken(pi->type, pi->p, pi->line);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif

	//���[�v�J�n�ʒu
	st_tk = cu_tk = cu_tk->next = CreateToken(SYM_LOOPSTART, pi->p, line);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("LOOPSTART:"));
#endif
	if (GetToken(pi) == FALSE) {
		return NULL;
	}

	//����
	pi->condition = TRUE;
	cu_tk = ArrayKey(pi, cu_tk);
	pi->condition = FALSE;
	if (cu_tk == NULL) {
		return NULL;
	}

	// ;
	if (pi->type != SYM_LINESEP) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	if (GetToken(pi) == FALSE) {
		return NULL;
	}

	//�ď�����
	re_tk.next = NULL;
	if (Expression(pi, &re_tk) == NULL) {
		return NULL;
	}

	// )
	if (pi->type != SYM_CLOSE) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		FreeToken(re_tk.next);
		return NULL;
	}

	//LOOP��ǉ�
	cu_tk = cu_tk->next = CreateToken(SYM_LOOP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		FreeToken(re_tk.next);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("LOOP"));
#endif
	pi->concat = TRUE;
	if (GetToken(pi) == FALSE) {
		FreeToken(re_tk.next);
		return NULL;
	}

	//�����Ώ�
	ttk.next = NULL;
	if (StatementList(pi, &ttk) == NULL) {
		FreeToken(re_tk.next);
		FreeToken(ttk.next);
		return NULL;
	}
	cu_tk->target = ttk.next;

	//���[�v�I���ʒu�Ɉړ�
	end_tk = cu_tk = cu_tk->next = CreateToken(SYM_JUMP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		FreeToken(re_tk.next);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("jump LOOPEND"));
#endif

	//�ď�������ǉ�
	for (cu_tk->next = re_tk.next; cu_tk->next != NULL; cu_tk = cu_tk->next);
	cu_tk = cu_tk->next = CreateToken(SYM_LINESEP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT(";"));
#endif

	//���[�v�J�n�ʒu�Ɉړ�
	cu_tk = cu_tk->next = CreateToken(SYM_JUMP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("jump LOOPSTART"));
#endif
	cu_tk->link = st_tk;

	//���[�v�I���ʒu
	end_tk->link = cu_tk = cu_tk->next = CreateToken(SYM_LOOPEND, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("LOOPEND:"));
#endif
	return cu_tk;
}

/*
 * JumpStatement - �W�����v
 */
static TOKEN *JumpStatement(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk;
	int type = pi->type;

	//token
	tk = CreateToken(pi->type, pi->p, pi->line);
	if (tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
	if (GetToken(pi) == FALSE) {
		FreeToken(tk);
		return NULL;
	}
	if (type == SYM_EXIT || type == SYM_RETURN) {
		//�l
		pi->condition = TRUE;
		cu_tk = ArrayKey(pi, cu_tk);
		pi->condition = FALSE;
		if (cu_tk == NULL) {
			FreeToken(tk);
			return NULL;
		}
	}
	cu_tk = cu_tk->next = tk;

	if (pi->type == SYM_EOF || pi->type == SYM_BCLOSE) {
		return cu_tk;
	}
	if (pi->type != SYM_LINEEND && pi->type != SYM_LINESEP) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	cu_tk = cu_tk->next = CreateToken(pi->type, pi->p, (pi->type != SYM_LINEEND) ? pi->line : (pi->line - 1));
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT(";"));
#endif
	if (GetToken(pi) == FALSE) {
		return NULL;
	}
	return cu_tk;
}

/*
 * CaseStatement - CASE���x��
 */
static TOKEN *CaseStatement(PARSEINFO *pi, TOKEN *cu_tk)
{
	int type = pi->type;

	cu_tk = cu_tk->next = CreateToken(type, pi->p, pi->line);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
	if (GetToken(pi) == FALSE) {
		return NULL;
	}
	if (type == SYM_CASE) {
		//�l
		pi->case_end = pi->condition = TRUE;
		cu_tk = ArrayKey(pi, cu_tk);
		pi->case_end = pi->condition = FALSE;
		if (cu_tk == NULL) {
			return NULL;
		}
	}
	if (pi->type != SYM_LABELEND) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	cu_tk = cu_tk->next = CreateToken(pi->type, pi->p, pi->line);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
	if (GetToken(pi) == FALSE) {
		return NULL;
	}
	return cu_tk;
}

/*
 * VarDeclList - �ϐ����X�g
 */
static TOKEN *VarDeclList(PARSEINFO *pi, TOKEN *cu_tk)
{
	while (1) {
		//����
		pi->decl = TRUE;
		cu_tk = Array(pi, cu_tk);
		pi->decl = FALSE;
		if (cu_tk == NULL) {
			return NULL;
		}

		if (pi->type == SYM_EQ) {
			// = 
			cu_tk = Assignment(pi, cu_tk);
			if (cu_tk == NULL) {
				return NULL;
			}
		}

		if (pi->type != SYM_WORDEND) {
			break;
		}
		// ,
		cu_tk = cu_tk->next = CreateToken(pi->type, pi->p, pi->line);
		if (cu_tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			return NULL;
		}
#ifdef DEBUG_SET
		cu_tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		if (GetToken(pi) == FALSE) {
			return NULL;
		}
	}
	return cu_tk;
}

/*
 * VarDecl - �ϐ�
 */
static TOKEN *VarDecl(PARSEINFO *pi, TOKEN *cu_tk)
{
	//var
	if (GetToken(pi) == FALSE) {
		return NULL;
	}
	//�ϐ����X�g
	cu_tk = VarDeclList(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}
	if (pi->type == SYM_EOF || pi->type == SYM_BCLOSE) {
		return cu_tk;
	}
	if (pi->type != SYM_LINEEND && pi->type != SYM_LINESEP) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	// ;
	cu_tk = cu_tk->next = CreateToken(pi->type, pi->p, (pi->type != SYM_LINEEND) ? pi->line : (pi->line - 1));
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
	if (GetToken(pi) == FALSE) {
		return NULL;
	}
	return cu_tk;
}

/*
 * FuncDecl - �֐�
 */
static TOKEN *FuncDecl(PARSEINFO *pi, TOKEN *cu_tk)
{
	FUNCINFO *fi;
	TOKEN *tk, *end_tk;

	// �֐��I���ʒu�Ɉړ�
	end_tk = cu_tk = cu_tk->next = CreateToken(SYM_JUMP, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("jump FUNCEND"));
#endif

	// �֐�
	cu_tk = cu_tk->next = CreateToken(pi->type, pi->p, pi->line);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("FUNCSTART"));
#endif
	if (GetToken(pi) == FALSE) {
		return NULL;
	}

	// �֐���
	if (pi->type != SYM_FUNC) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	tk = CreateToken(pi->type, pi->p, pi->line);
	if (tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
	tk->buf = alloc_copy_n(pi->p, (int)(pi->r - pi->p));
	if (tk->buf == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		FreeToken(tk);
		return NULL;
	}
	str_lower(tk->buf);
	if (GetToken(pi) == FALSE) {
		FreeToken(tk);
		return NULL;
	}

	// �Ăяo���p���̐ݒ�
	if (pi->ei->sci->fi == NULL) {
		fi = pi->ei->sci->fi = mem_calloc(sizeof(FUNCINFO));
	} else {
		for (fi = pi->ei->sci->fi; fi->next != NULL; fi = fi->next);
		fi->next = mem_calloc(sizeof(FUNCINFO));
		fi = fi->next;
	}
	if (fi == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		FreeToken(tk);
		return NULL;
	}
	fi->name = alloc_copy(tk->buf);
	if (fi->name == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		FreeToken(tk);
		return NULL;
	}
	fi->name_hash = str2hash(fi->name);
	fi->tk = cu_tk;

	// (
	if (pi->type != SYM_OPEN) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		FreeToken(tk);
		return NULL;
	}
	if (GetToken(pi) == FALSE) {
		FreeToken(tk);
		return NULL;
	}

	// ����
	cu_tk = VarDeclList(pi, cu_tk);

	// )
	if (pi->type != SYM_CLOSE) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		FreeToken(tk);
		return NULL;
	}
	pi->concat = TRUE;
	if (GetToken(pi) == FALSE) {
		FreeToken(tk);
		return NULL;
	}

	// �֐����̐ݒ�
	cu_tk = cu_tk->next = tk;

	// �֐��{��
	if (pi->type != SYM_BOPEN) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	cu_tk = CompoundStatement(pi, cu_tk);
	if (cu_tk == NULL) {
		return NULL;
	}

	// �֐��I���ʒu
	end_tk->link = cu_tk = cu_tk->next = CreateToken(SYM_FUNCEND, pi->p, -1);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy(TEXT("FUNCEND:"));
#endif
	return cu_tk;
}

/*
 * CompoundStatement - �u���b�N
 */
static TOKEN *CompoundStatement(PARSEINFO *pi, TOKEN *cu_tk)
{
	TOKEN *tk, ttk;

	if (GetToken(pi) == FALSE) {
		return NULL;
	}
	//�u���b�N�̏I���ʒu�܂ŉ��
	ttk.next = NULL;
	tk = &ttk;
	while (tk != NULL && pi->type != SYM_EOF && pi->type != SYM_BCLOSE) {
		tk = StatementList(pi, tk);
	}
	cu_tk->target = ttk.next;
	if (tk == NULL) {
		return NULL;
	}
	if (pi->type != SYM_BCLOSE) {
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		return NULL;
	}
	pi->concat = TRUE;

#ifndef PG0_CMD
	cu_tk = cu_tk->next = CreateToken(pi->type, pi->p, pi->line);
	if (cu_tk == NULL) {
		Error(pi->ei, ERR_ALLOC, pi->p, NULL);
		return NULL;
	}
#ifdef DEBUG_SET
	cu_tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
#endif

	if (GetToken(pi) == FALSE) {
		return NULL;
	}
	return cu_tk;
}

/*
 * StatementList - ��
 */
static TOKEN *StatementList(PARSEINFO *pi, TOKEN *cu_tk)
{
	pi->level++;

	switch (pi->type) {
	case SYM_EOF:
		break;

	case SYM_PREP:
		//�v���v���Z�b�T
		pi->r = Preprocessor(pi->ei->sci, pi->ei->sci->path, pi->r);
		if (pi->r == NULL) {
			cu_tk = NULL;
			break;
		}
		if (pi->ei->sci->extension == TRUE && pi->extension == FALSE) {
			pi->extension = TRUE;
		}
		if (GetToken(pi) == FALSE) {
			cu_tk = NULL;
			break;
		}
		break;

	case SYM_BOPEN:
		//�u���b�N
		cu_tk = cu_tk->next = CreateToken(pi->type, pi->p, pi->line);
		if (cu_tk == NULL) {
			Error(pi->ei, ERR_ALLOC, pi->p, NULL);
			break;
		}
#ifdef DEBUG_SET
		cu_tk->buf = alloc_copy_n(pi->p, pi->r - pi->p);
#endif
		cu_tk = CompoundStatement(pi, cu_tk);
		break;

	case SYM_IF:
		cu_tk = IfStatement(pi, cu_tk);
		break;

	case SYM_WHILE:
		cu_tk = WhileStatement(pi, cu_tk);
		break;

	case SYM_SWITCH:
		cu_tk = SwitchStatement(pi, cu_tk);
		break;

	case SYM_DO:
		cu_tk = DoWhileStatement(pi, cu_tk);
		break;

	case SYM_FOR:
		cu_tk = ForStatement(pi, cu_tk);
		break;

	case SYM_BREAK:
	case SYM_CONTINUE:
	case SYM_RETURN:
		cu_tk = JumpStatement(pi, cu_tk);
		break;

	case SYM_CASE:
	case SYM_DEFAULT:
		cu_tk = CaseStatement(pi, cu_tk);
		break;

	case SYM_FUNCSTART:
		//�֐�
#ifndef UNLIMIT_FUNC_LEVEL
		if (pi->level > 1) {
			Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
			cu_tk = NULL;
			break;
		}
#endif
		cu_tk = FuncDecl(pi, cu_tk);
		break;

	case SYM_EXIT:
		cu_tk = JumpStatement(pi, cu_tk);
		break;

	case SYM_VAR:
		//�ϐ�
		cu_tk = VarDecl(pi, cu_tk);
		break;

	case SYM_VARIABLE:
	case SYM_CONST_INT:
	case SYM_CONST_FLOAT:
	case SYM_OPEN:
	case SYM_NOT:
	case SYM_PLUS:
	case SYM_MINS:
	case SYM_WORDEND:
	case SYM_LINEEND:
	case SYM_LINESEP:
	case SYM_FUNC:
	case SYM_BITNOT:
	case SYM_INC:
	case SYM_DEC:
		//��
		cu_tk = ExpressionStatement(pi, cu_tk);
		break;

	default:
		Error(pi->ei, ERR_SENTENCE, pi->p, NULL);
		cu_tk = NULL;
		break;
	}
	pi->level--;
	return cu_tk;
}

/*
 * ParseVariable - �ϐ��擾
 */
TOKEN *ParseVariable(EXECINFO *ei, TCHAR *buf)
{
	TOKEN tk;
	PARSEINFO pi;

	ZeroMemory(&pi, sizeof(PARSEINFO));
	pi.ei = ei;
	pi.p = pi.r = buf;
	pi.concat = TRUE;
	pi.level = 1;

	if (GetToken(&pi) == FALSE) {
		return NULL;
	}

	//���
	ZeroMemory(&tk, sizeof(TOKEN));
	if (Array(&pi, &tk) == NULL || pi.type != SYM_EOF) {
		//�G���[���͑S�ĉ��
		FreeToken(tk.next);
		return NULL;
	}
	return tk.next;
}

/*
 * ParseSentence - �\�����
 */
TOKEN *ParseSentence(EXECINFO *ei, TCHAR *buf, int level)
{
	TOKEN *cu_tk, tk;
	PARSEINFO pi;

	ZeroMemory(&pi, sizeof(PARSEINFO));
	pi.ei = ei;
	pi.p = pi.r = buf;
	pi.concat = TRUE;
	pi.level = level;
	if (ei->sci != NULL) {
		pi.extension = ei->sci->extension;
	}
	if (GetToken(&pi) == FALSE) {
		return NULL;
	}

	//���
	ZeroMemory(&tk, sizeof(TOKEN));
	cu_tk = &tk;
	while (cu_tk != NULL && pi.type != SYM_EOF) {
		cu_tk = StatementList(&pi, cu_tk);
	}
	if (cu_tk == NULL) {
		//�G���[���͑S�ĉ��
		FreeToken(tk.next);
		return NULL;
	}
	return tk.next;
}
/* End of source */
