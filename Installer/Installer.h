/*
 * PG0 Installer
 *
 * Installer.h
 *
 * Copyright (C) 1996-2026 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef _INC_PG0_INSTALLER_H
#define _INC_PG0_INSTALLER_H

/* Define */
#define BUF_SIZE						256

#define APP_NAME						TEXT("PG0")
#define APP_EXE							TEXT("pg0.exe")
#define APP_PUBLISHER					TEXT("Ohno Tomoaki")
#define APP_URL							TEXT("https://nakka.com/soft/pg0/")
// settings of PG0 (%LOCALAPPDATA%\pg0: pg0.ini, pg0_screen.ini, values)
#define APP_DATA_FOLDER					TEXT("pg0")

// window class of the PG0 editor (the title varies, so only the class is checked)
#define MAIN_WND_CLASS					TEXT("PG0EditMainWndClass")

// default folder name under Program Files
#define DEFAULT_FOLDER					TEXT("pg0")
// uninstaller placed in the install folder
#define UNINSTALL_EXE					TEXT("uninstall.exe")
// list of the installed files
#define UNINSTALL_LOG					TEXT("uninstall.dat")
// file list left by the previous installer (EXEpress)
#define OLD_INSTALL_LOG					TEXT("install.DAT")

// registration in the list of installed programs
#define UNINSTALL_KEY					TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall")
#define UNINSTALL_SUBKEY				TEXT("PG0")

// command line options
#define CMD_UNINSTALL					TEXT("/uninstall")
#define CMD_UNINSTALL_RUN				TEXT("/uninstallrun")
#define CMD_DELETE_DATA					TEXT("/deletedata")
#define CMD_PID							TEXT("/pid:")

/* Struct */
// where the program is registered in the list of installed programs
typedef struct _REG_TARGET {
	HKEY root;									// HKEY_LOCAL_MACHINE / HKEY_CURRENT_USER
	REGSAM view;								// KEY_WOW64_32KEY / KEY_WOW64_64KEY
	TCHAR subkey[BUF_SIZE];						// key name under Uninstall
} REG_TARGET;

// growable string buffer
typedef struct _STR_BUF {
	TCHAR *buf;
	DWORD len;									// characters stored
	DWORD size;									// characters allocated
} STR_BUF;

#endif
/* End of source */
