/*
 * PG0
 *
 * main.c
 *
 * Copyright (C) 1996-2019 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <setupapi.h>
#include <shlobj.h>

#pragma comment(lib, "SetupAPI.lib")

#include "../PG0/script_memory.h"
#include "../PG0/script_string.h"
#include "../PG0/Profile.h"

#include "resource.h"

/* Define */
#define  MICROBIT_ID		TEXT("VID_0D28&PID_0204")
#define  FIRMWARE_VERSION	"3"

/* Global Variables */
HANDLE comPort;
BOOL radio_on = FALSE;

/* Local Function Prototypes */

/*
 * _send - データを送信して結果を受信する
 */
static char *_send(HANDLE port, char *buf, int timeout)
{
	char *retbuf;
	DWORD errors;
	COMSTAT comStat;
	DWORD size;

	if (WriteFile(port, buf, (DWORD)strlen(buf), &size, NULL) == FALSE) {
		return NULL;
	}
	retbuf = mem_alloc(1);
	if (retbuf == NULL) {
		return NULL;
	}
	*retbuf = '\0';
	while (1) {
		char *tmpbuf;
		char *abuf;
		char *p;
		int cnt = 0;

		ClearCommError(port, &errors, &comStat);
		while (comStat.cbInQue == 0) {
			Sleep(10);
			ClearCommError(port, &errors, &comStat);
			cnt++;
			if (timeout > 0 && cnt > timeout) {
				return NULL;
			}
		}
		tmpbuf = mem_alloc(comStat.cbInQue + 1);
		if (tmpbuf == NULL) {
			mem_free(&retbuf);
			return NULL;
		}
		ReadFile(port, tmpbuf, comStat.cbInQue, &size, NULL);
		tmpbuf[size] = '\0';

		size = (DWORD)strlen(retbuf) + (DWORD)strlen(tmpbuf) + 1;
		abuf = mem_alloc(size);
		if (tmpbuf == NULL) {
			mem_free(&tmpbuf);
			mem_free(&retbuf);
			return NULL;
		}
		strcpy_s(abuf, strlen(retbuf) + 1, retbuf);
		strcpy_s(abuf + strlen(retbuf), strlen(tmpbuf) + 1, tmpbuf);
		mem_free(&tmpbuf);
		mem_free(&retbuf);
		retbuf = abuf;

		// 終端マーク(\x01)が来るまで受信を溜め込む
		for (p = retbuf; *p != '\0'; p++) {
			if (*p == '\x01') {
				break;
			}
		}
		if (*p == '\x01') {
			*p = '\0';
			break;
		}
	}
	return retbuf;
}

/*
 * sendData - データの送信
 */
BOOL sendData(char cmd, TCHAR *buf, TCHAR *ret, TCHAR *ErrStr)
{
	char *cbuf = NULL;
	char *sendBuf;
	char *retBuf;
	int size;

	if (comPort == NULL) {
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("Unable to find micro:bit"));
		} else {
			lstrcpy(ErrStr, TEXT("micro:bitに接続されていません。"));
		}
		return FALSE;
	}
	if (buf != NULL) {
		WCHAR *wbuf;
		int len;

#ifdef UNICODE
		wbuf = buf;
#else
		// ASCIIからUTF-16に変換
		len = MultiByteToWideChar(CP_ACP, 0, buf, -1, NULL, 0);
		if ((wbuf = (WCHAR *)mem_alloc(sizeof(WCHAR) * len)) == NULL) {
			lstrcpy(ErrStr, TEXT("Memory Error"));
			return FALSE;
		}
		MultiByteToWideChar(CP_ACP, 0, buf, -1, wbuf, len);
#endif
		// UTF-16からUTF-8に変換
		len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
		if ((cbuf = (BYTE *)mem_alloc(sizeof(BYTE) * len)) == NULL) {
#ifndef UNICODE
			mem_free(&wbuf);
#endif
			lstrcpy(ErrStr, TEXT("Memory Error"));
			return FALSE;
		}
		WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, cbuf, len, NULL, NULL);
#ifndef UNICODE
		mem_free(&wbuf);
#endif
		if (cbuf == NULL) {
			return FALSE;
		}
		size = 1 + (int)strlen(cbuf) + 1 + 1;
	} else {
		size = 3;
	}
	sendBuf = mem_alloc(size);
	if (sendBuf == NULL) {
		mem_free(&cbuf);
		lstrcpy(ErrStr, TEXT("Memory Error"));
		return FALSE;
	}
	// コマンド
	sendBuf[0] = cmd;
	if (cbuf != NULL) {
		// 引数
		strcpy_s(sendBuf + 1, size - 2, cbuf);
		mem_free(&cbuf);
	}
	// 終端マーク(\x01)
	sendBuf[size - 2] = '\x01';
	sendBuf[size - 1] = '\0';
	retBuf = _send(comPort, sendBuf, 0);
	mem_free(&sendBuf);
	if (retBuf == NULL) {
		lstrcpy(ErrStr, TEXT("Send Error"));
		return FALSE;
	}
	if (*retBuf == '\x02' || *retBuf == '\x03') {
		mem_free(&retBuf);
		if (PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale())) != LANG_JAPANESE) {
			lstrcpy(ErrStr, TEXT("micro:bit Error"));
		} else {
			lstrcpy(ErrStr, TEXT("micro:bit内部エラー"));
		}
		return FALSE;
	}
	TCHAR *tmp = char2tchar(retBuf);
	mem_free(&retBuf);
	lstrcpy(ret, tmp);
	mem_free(&tmp);
	return TRUE;
}

/*
 * GetMicrobitPort - Micro:bit用のポートを取得
 */
static BOOL GetMicrobitPort(TCHAR *ret, DWORD retSize)
{
	GUID ClassGuid[1];
	DWORD size;
	BOOL bRet;
	HDEVINFO hDevInfo = NULL;
	SP_DEVINFO_DATA sdd;
	DWORD index = 0;

	sdd.cbSize = sizeof(SP_DEVINFO_DATA);

	bRet = SetupDiClassGuidsFromName(TEXT("PORTS"), (LPGUID)&ClassGuid, 1, &size);
	if (!bRet) {
		return FALSE;
	}
	hDevInfo = SetupDiGetClassDevs(&ClassGuid[0], NULL, NULL, DIGCF_PRESENT | DIGCF_PROFILE);
	if (hDevInfo) {
		index = 0;
		while (SetupDiEnumDeviceInfo(hDevInfo, index++, &sdd)) {
			TCHAR buf[MAX_PATH];
			TCHAR *p;
			DWORD dwReqSize = 0;
			DWORD dwPropType;
			BOOL match = FALSE;

			bRet = SetupDiGetDeviceRegistryProperty(hDevInfo, &sdd, SPDRP_HARDWAREID, &dwPropType, (LPBYTE)buf, sizeof(buf), &dwReqSize);
			for (p = buf; *p != TEXT('\0'); p++) {
				if (str_cmp_ni(p, MICROBIT_ID, lstrlen(MICROBIT_ID)) == 0) {
					match = TRUE;
					break;
				}
			}
			if (match) {
				HKEY hKey = NULL;
				DWORD dwType = REG_SZ;
				hKey = SetupDiOpenDevRegKey(hDevInfo, &sdd, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
				if (hKey) {
					long lRet;
					size = retSize;
					lRet = RegQueryValueEx(hKey, TEXT("PortName"), 0, &dwType, (LPBYTE)ret, &size);
					RegCloseKey(hKey);
					SetupDiDestroyDeviceInfoList(hDevInfo);
					return TRUE;
				}
			}
		}
		SetupDiDestroyDeviceInfoList(hDevInfo);
	}
	return FALSE;
}

/*
 * OpenPort - シリアルポートを開く
 */
static HANDLE OpenPort(TCHAR *comPortStr)
{
	HANDLE port;
	DCB dcb;
	char *retBuf;

	port = CreateFile(comPortStr, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (port == (HANDLE)-1) {
		return port;
	}
	GetCommState(port, &dcb);
	dcb.BaudRate = 115200; // 速度
	dcb.ByteSize = 8; // データ長
	dcb.Parity = NOPARITY; // パリティ
	dcb.StopBits = ONESTOPBIT; // ストップビット長
	dcb.fOutxCtsFlow = FALSE; // 送信時CTSフロー
	dcb.fRtsControl = RTS_CONTROL_ENABLE; // RTSフロー
	SetCommState(port, &dcb);

	PurgeComm(port, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
	
	retBuf = _send(port, "\x10\x01", 100);
	if (retBuf == NULL) {
		CloseHandle(port);
		return NULL;
	}
	// ファームウェアバージョンの確認
	if (strcmp(retBuf, FIRMWARE_VERSION) != 0) {
		mem_free(&retBuf);
		CloseHandle(port);
		return NULL;
	}
	mem_free(&retBuf);
	return port;
}

/*
 * CopyFirmware - ファームウェアのコピー
 */
static BOOL CopyFirmware(HINSTANCE hInst)
{
	TCHAR drives[MAX_PATH];
	TCHAR volumeName[MAX_PATH];

	GetLogicalDriveStrings(MAX_PATH, drives);
	const TCHAR *ptr = drives;
	while (*ptr != TEXT('\0')) {
		GetVolumeInformation(ptr, volumeName, MAX_PATH, NULL, NULL, NULL, NULL, 0);
		if (lstrcmpi(volumeName, TEXT("MICROBIT")) == 0) {
			HANDLE hFile;
			HRSRC hRsrc;
			HGLOBAL hMem;
			TCHAR to_path[MAX_PATH];
			char *str;
			DWORD ret;

			hRsrc = FindResourceEx(hInst, TEXT("HEX"), MAKEINTRESOURCE(IDR_HEX_FIRMWARE), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
			if (hRsrc == NULL) {
				return FALSE;
			}
			hMem = LoadResource(hInst, hRsrc);
			if (hMem == NULL) {
				return FALSE;
			}
			str = LockResource(hMem);
			if (str == NULL) {
				return FALSE;
			}
			wsprintf(to_path, TEXT("%sfirmware.hex"), ptr);
			hFile = CreateFile(to_path, GENERIC_READ | GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (hFile == NULL || hFile == (HANDLE)-1) {
				return FALSE;
			}
			if (WriteFile(hFile, str, (DWORD)strlen(str), &ret, NULL) == FALSE) {
				CloseHandle(hFile);
				return FALSE;
			}
			CloseHandle(hFile);
			break;
		}
		for (; *ptr != TEXT('\0'); ptr++);
		ptr++;
	}
	return TRUE;
}

/*
 * DllMain - DLLメイン
 */
int WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, PVOID pvReserved)
{
	static TCHAR ini_path[MAX_PATH];
	TCHAR ComPortStr[MAX_PATH] = { 0 };

	switch (fdwReason) {
	case DLL_PROCESS_ATTACH:
		// INIファイル
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, ini_path))) {
			lstrcat(ini_path, TEXT("\\pg0"));
			CreateDirectory(ini_path, NULL);
			lstrcat(ini_path, TEXT("\\pg0microbit.ini"));
			profile_initialize(ini_path, TRUE);
			profile_get_string(TEXT("com"), TEXT("port"), TEXT(""), ComPortStr, MAX_PATH - 1, ini_path);
			profile_free();
		}

		radio_on = FALSE;
		// 乱数の初期化
		srand((unsigned)GetTickCount());
		if (*ComPortStr != TEXT('\0') || GetMicrobitPort(ComPortStr, sizeof(ComPortStr)) == TRUE) {
			comPort = OpenPort(ComPortStr);
			if (comPort == (HANDLE)-1) {
				comPort = NULL;
				break;
			}
			// ランタイムのコピー
			if (comPort == NULL && CopyFirmware(hInstance) == TRUE) {
				Sleep(1000);
				comPort = OpenPort(ComPortStr);
				if (comPort == (HANDLE)-1) {
					comPort = NULL;
					break;
				}
			}
		} else {
			comPort = NULL;
		}
		break;

	case DLL_PROCESS_DETACH:
		if (comPort != NULL) {
			if (radio_on == TRUE) {
				char *retBuf = _send(comPort, "\x12radio.off\x01", 100);
				mem_free(&retBuf);
			}
			CloseHandle(comPort);
		}
		break;

	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}
/* End of source */
