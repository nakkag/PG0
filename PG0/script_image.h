/*
 * PG0
 *
 * script_image.h
 *
 * Copyright (C) 1996-2026 by Nakashima Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef SCRIPT_IMAGE_H
#define SCRIPT_IMAGE_H

/* Include Files */
#include "script_struct.h"

/* Define */
// resource that holds the image in a generated program (RT_RCDATA, neutral language)
#define IMAGE_RESOURCE_NAME		TEXT("PG0SCRIPT")
// library whose presence makes the program a window application
#define IMAGE_LIB_SCREEN		TEXT("pg0_screen.dll")
// library that needs the cid of the script stored online (lib/net.pg0)
#define IMAGE_LIB_NET			TEXT("pg0_net.dll")
// resource with the cid and the server of the script stored online, for IMAGE_LIB_NET
// (RT_RCDATA, neutral language): "<cid>\n<server>" in UTF-16
#define IMAGE_ONLINE_RESOURCE_NAME	TEXT("PG0ONLINE")

/* Struct */

/* Function Prototypes */
// parse tree (all scripts of the chain and the libraries) -> image
BYTE *ScriptToImage(SCRIPTINFO *sci, DWORD *size);
// image -> parse tree (the libraries are not loaded; see ImageLibraryName)
SCRIPTINFO *ImageToScript(const BYTE *buf, DWORD size);
// libraries recorded in an image
int ImageLibraryCount(const BYTE *buf, DWORD size);
BOOL ImageLibraryName(const BYTE *buf, DWORD size, int index, TCHAR *name, int name_size);

#endif
/* End of source */
