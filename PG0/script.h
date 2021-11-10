/*
 * PG0
 *
 * script.h
 *
 * Copyright (C) 1996-2018 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef SCRIPT_H
#define SCRIPT_H

/* Include Files */
#include "script_struct.h"

/* Define */
#ifdef _DEBUG_TOKEN
#define DEBUG_SET				//解析木にデバッグ用の文字列を保持
#endif

#define BUF_SIZE				256

/* Struct */

/* Function Prototypes */
void InitializeScript();
void EndScript();
void Error(EXECINFO *ei, ERROR_CODE err_id, TCHAR *msg, TCHAR *pre_str);

//解放
void FreeToken(TOKEN *tk);
void FreeExecInfo(EXECINFO *ei);
void FreeFuncInfo(FUNCINFO *fi);
void FreeScriptInfo(SCRIPTINFO *sci);

//スクリプト情報
void InitializeScriptInfo(SCRIPTINFO *sci, BOOL op_exp, BOOL op_extension);
VALUEINFO *GetVariable(EXECINFO *ei, TCHAR *name);
BOOL SetVariable(EXECINFO *ei, TCHAR *name, VALUE *v);

//実行
int ExecSentense(EXECINFO *ei, TOKEN *cu_tk, VALUEINFO **retvi, VALUEINFO **retstack);
VALUEINFO *ExecFunction(EXECINFO *ei, TCHAR *name, VALUEINFO *param);
int ExecScript(SCRIPTINFO *sci, VALUEINFO *arg_vi, VALUEINFO **ret_vi);

//解析
TOKEN *ParseVariable(EXECINFO *ei, TCHAR *buf);
TOKEN *ParseSentence(EXECINFO *ei, TCHAR *buf, int level);

//読み込み
void GetFilePathName(TCHAR *path, TCHAR *dir, TCHAR *name);
TCHAR *read_file(TCHAR *path);
SCRIPTINFO *ReadScriptFile(SCRIPTINFO *sci, TCHAR *path, TCHAR *FileName);
BOOL ReadScriptFiles(SCRIPTINFO *sci, TCHAR *path, TCHAR *FileName);
TCHAR *Preprocessor(SCRIPTINFO *sci, TCHAR *path, TCHAR *p);

//関数テーブル
void InitFuncAddress();
LIBFUNC GetFuncAddress(TCHAR *FuncName);

#endif
/* End of source */
