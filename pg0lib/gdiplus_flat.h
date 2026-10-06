/*
 * PG0 library
 *
 * gdiplus_flat.h
 *
 * C declarations for the part of the GDI+ flat API used by the screen library.
 */

#ifndef PG0_GDIPLUS_FLAT_H
#define PG0_GDIPLUS_FLAT_H

#include <windows.h>

#pragma comment(lib, "gdiplus.lib")

#define WINGDIPAPI				__stdcall
#define GDIPCONST				const

typedef int GpStatus;
#define GpOk					0

typedef float REAL;
typedef DWORD ARGB;
typedef INT PixelFormat;

typedef struct GpGraphics GpGraphics;
typedef struct GpImage GpImage;
typedef struct GpImage GpBitmap;
typedef struct GpBrush GpBrush;
typedef struct GpBrush GpSolidFill;
typedef struct GpPen GpPen;
typedef struct GpPath GpPath;
typedef struct GpMatrix GpMatrix;
typedef struct GpFontFamily GpFontFamily;
typedef struct GpFontCollection GpFontCollection;
typedef struct GpFont GpFont;
typedef struct GpStringFormat GpStringFormat;
typedef struct GpImageAttributes GpImageAttributes;

typedef struct _GpPointF {
	REAL X;
	REAL Y;
} GpPointF;

typedef struct _GpRectF {
	REAL X;
	REAL Y;
	REAL Width;
	REAL Height;
} GpRectF;

typedef struct _GpColorMatrix {
	REAL m[5][5];
} GpColorMatrix;

typedef struct _GdiplusStartupInput {
	UINT32 GdiplusVersion;
	void *DebugEventCallback;
	BOOL SuppressBackgroundThread;
	BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

typedef struct _GdiplusStartupOutput {
	void *NotificationHook;
	void *NotificationUnhook;
} GdiplusStartupOutput;

/* pixel formats */
#define PixelFormatIndexed		0x00010000
#define PixelFormatGDI			0x00020000
#define PixelFormatAlpha		0x00040000
#define PixelFormatPAlpha		0x00080000
#define PixelFormatCanonical	0x00200000
#define PixelFormat32bppRGB		(9 | (32 << 8) | PixelFormatGDI)
#define PixelFormat32bppARGB	(10 | (32 << 8) | PixelFormatAlpha | PixelFormatGDI | PixelFormatCanonical)
#define PixelFormat32bppPARGB	(11 | (32 << 8) | PixelFormatAlpha | PixelFormatPAlpha | PixelFormatGDI)

/* units */
#define UnitWorld				0
#define UnitDisplay				1
#define UnitPixel				2
#define UnitPoint				3

/* quality modes */
#define SmoothingModeNone		3
#define SmoothingModeAntiAlias	4
#define PixelOffsetModeNone		3
#define PixelOffsetModeHalf		4
#define InterpolationModeBilinear				3
#define InterpolationModeNearestNeighbor		5
#define InterpolationModeHighQualityBilinear	6
#define InterpolationModeHighQualityBicubic		7
#define CompositingModeSourceOver	0
#define CompositingModeSourceCopy	1
#define CompositingQualityHighQuality	2
#define CompositingQualityAssumeLinear	4
#define TextRenderingHintAntiAlias	4
#define FlushIntentionFlush		0
#define FlushIntentionSync		1
#define MatrixOrderPrepend		0
#define MatrixOrderAppend		1
#define FillModeAlternate		0
#define FillModeWinding			1
#define WrapModeTile			0
#define WrapModeTileFlipXY		3
#define WrapModeClamp			4
#define ColorAdjustTypeDefault	0
#define ColorAdjustTypeBitmap	1
#define ColorMatrixFlagsDefault	0
#define LineCapFlat				0
#define LineJoinMiter			0

/* fonts */
#define FontStyleRegular		0
#define FontStyleBold			1
#define FontStyleItalic			2
#define FontStyleBoldItalic		3

#define StringFormatFlagsNoFitBlackBox			0x00000004
#define StringFormatFlagsMeasureTrailingSpaces	0x00000800
#define StringFormatFlagsNoWrap					0x00001000
#define StringFormatFlagsNoClip					0x00004000
#define StringTrimmingNone		0

GpStatus WINGDIPAPI GdiplusStartup(ULONG_PTR *token, GDIPCONST GdiplusStartupInput *input, GdiplusStartupOutput *output);
void WINGDIPAPI GdiplusShutdown(ULONG_PTR token);

GpStatus WINGDIPAPI GdipCreateBitmapFromScan0(INT width, INT height, INT stride, PixelFormat format, BYTE *scan0, GpBitmap **bitmap);
GpStatus WINGDIPAPI GdipDisposeImage(GpImage *image);
GpStatus WINGDIPAPI GdipSaveImageToFile(GpImage *image, GDIPCONST WCHAR *filename, GDIPCONST GUID *clsidEncoder, GDIPCONST void *encoderParams);

GpStatus WINGDIPAPI GdipGetImageGraphicsContext(GpImage *image, GpGraphics **graphics);
GpStatus WINGDIPAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics);
GpStatus WINGDIPAPI GdipDeleteGraphics(GpGraphics *graphics);
GpStatus WINGDIPAPI GdipSetSmoothingMode(GpGraphics *graphics, int smoothingMode);
GpStatus WINGDIPAPI GdipSetPixelOffsetMode(GpGraphics *graphics, int pixelOffsetMode);
GpStatus WINGDIPAPI GdipSetInterpolationMode(GpGraphics *graphics, int interpolationMode);
GpStatus WINGDIPAPI GdipSetCompositingMode(GpGraphics *graphics, int compositingMode);
GpStatus WINGDIPAPI GdipSetCompositingQuality(GpGraphics *graphics, int compositingQuality);
GpStatus WINGDIPAPI GdipSetTextRenderingHint(GpGraphics *graphics, int mode);
GpStatus WINGDIPAPI GdipGraphicsClear(GpGraphics *graphics, ARGB color);
GpStatus WINGDIPAPI GdipFlush(GpGraphics *graphics, int intention);
GpStatus WINGDIPAPI GdipTranslateWorldTransform(GpGraphics *graphics, REAL dx, REAL dy, int order);
GpStatus WINGDIPAPI GdipRotateWorldTransform(GpGraphics *graphics, REAL angle, int order);
GpStatus WINGDIPAPI GdipResetWorldTransform(GpGraphics *graphics);

GpStatus WINGDIPAPI GdipCreatePen1(ARGB color, REAL width, int unit, GpPen **pen);
GpStatus WINGDIPAPI GdipDeletePen(GpPen *pen);
GpStatus WINGDIPAPI GdipCreateSolidFill(ARGB color, GpSolidFill **brush);
GpStatus WINGDIPAPI GdipDeleteBrush(GpBrush *brush);

GpStatus WINGDIPAPI GdipDrawLine(GpGraphics *graphics, GpPen *pen, REAL x1, REAL y1, REAL x2, REAL y2);
GpStatus WINGDIPAPI GdipDrawRectangle(GpGraphics *graphics, GpPen *pen, REAL x, REAL y, REAL width, REAL height);
GpStatus WINGDIPAPI GdipFillRectangle(GpGraphics *graphics, GpBrush *brush, REAL x, REAL y, REAL width, REAL height);
GpStatus WINGDIPAPI GdipDrawPath(GpGraphics *graphics, GpPen *pen, GpPath *path);
GpStatus WINGDIPAPI GdipFillPath(GpGraphics *graphics, GpBrush *brush, GpPath *path);
GpStatus WINGDIPAPI GdipDrawImageRectRect(GpGraphics *graphics, GpImage *image,
	REAL dstx, REAL dsty, REAL dstwidth, REAL dstheight,
	REAL srcx, REAL srcy, REAL srcwidth, REAL srcheight,
	int srcUnit, GDIPCONST GpImageAttributes *imageAttributes, void *callback, void *callbackData);
GpStatus WINGDIPAPI GdipDrawString(GpGraphics *graphics, GDIPCONST WCHAR *string, INT length, GDIPCONST GpFont *font,
	GDIPCONST GpRectF *layoutRect, GDIPCONST GpStringFormat *stringFormat, GDIPCONST GpBrush *brush);
GpStatus WINGDIPAPI GdipMeasureString(GpGraphics *graphics, GDIPCONST WCHAR *string, INT length, GDIPCONST GpFont *font,
	GDIPCONST GpRectF *layoutRect, GDIPCONST GpStringFormat *stringFormat, GpRectF *boundingBox,
	INT *codepointsFitted, INT *linesFilled);

GpStatus WINGDIPAPI GdipCreatePath(int brushMode, GpPath **path);
GpStatus WINGDIPAPI GdipDeletePath(GpPath *path);
GpStatus WINGDIPAPI GdipAddPathArc(GpPath *path, REAL x, REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle);
GpStatus WINGDIPAPI GdipAddPathEllipse(GpPath *path, REAL x, REAL y, REAL width, REAL height);
GpStatus WINGDIPAPI GdipAddPathLine2(GpPath *path, GDIPCONST GpPointF *points, INT count);
GpStatus WINGDIPAPI GdipClosePathFigure(GpPath *path);
GpStatus WINGDIPAPI GdipAddPathString(GpPath *path, GDIPCONST WCHAR *string, INT length, GDIPCONST GpFontFamily *family,
	INT style, REAL emSize, GDIPCONST GpRectF *layoutRect, GDIPCONST GpStringFormat *format);

GpStatus WINGDIPAPI GdipCreateFontFamilyFromName(GDIPCONST WCHAR *name, GpFontCollection *fontCollection, GpFontFamily **fontFamily);
GpStatus WINGDIPAPI GdipDeleteFontFamily(GpFontFamily *fontFamily);
GpStatus WINGDIPAPI GdipGetGenericFontFamilySansSerif(GpFontFamily **nativeFamily);
GpStatus WINGDIPAPI GdipGetGenericFontFamilySerif(GpFontFamily **nativeFamily);
GpStatus WINGDIPAPI GdipGetGenericFontFamilyMonospace(GpFontFamily **nativeFamily);
GpStatus WINGDIPAPI GdipGetCellAscent(GDIPCONST GpFontFamily *family, INT style, UINT16 *CellAscent);
GpStatus WINGDIPAPI GdipGetEmHeight(GDIPCONST GpFontFamily *family, INT style, UINT16 *EmHeight);
GpStatus WINGDIPAPI GdipCreateFont(GDIPCONST GpFontFamily *fontFamily, REAL emSize, INT style, int unit, GpFont **font);
GpStatus WINGDIPAPI GdipDeleteFont(GpFont *font);

GpStatus WINGDIPAPI GdipStringFormatGetGenericTypographic(GpStringFormat **format);
GpStatus WINGDIPAPI GdipCloneStringFormat(GDIPCONST GpStringFormat *format, GpStringFormat **newFormat);
GpStatus WINGDIPAPI GdipSetStringFormatFlags(GpStringFormat *format, INT flags);
GpStatus WINGDIPAPI GdipGetStringFormatFlags(GDIPCONST GpStringFormat *format, INT *flags);
GpStatus WINGDIPAPI GdipSetStringFormatTrimming(GpStringFormat *format, int trimming);
GpStatus WINGDIPAPI GdipDeleteStringFormat(GpStringFormat *format);

GpStatus WINGDIPAPI GdipCreateImageAttributes(GpImageAttributes **imageattr);
GpStatus WINGDIPAPI GdipDisposeImageAttributes(GpImageAttributes *imageattr);
GpStatus WINGDIPAPI GdipSetImageAttributesColorMatrix(GpImageAttributes *imageattr, int type, BOOL enableFlag,
	GDIPCONST GpColorMatrix *colorMatrix, GDIPCONST GpColorMatrix *grayMatrix, int flags);
GpStatus WINGDIPAPI GdipSetImageAttributesWrapMode(GpImageAttributes *imageAttr, int wrap, ARGB argb, BOOL clamp);

#endif
/* End of source */
