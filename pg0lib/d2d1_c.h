/*
 * PG0 library
 *
 * d2d1_c.h
 *
 * C vtable layouts of the Direct2D interfaces used by screen_d2d.c.
 * The SDK's d2d1.h only forward-declares the interfaces for C; the method
 * order below was generated from its C++ declarations (10.0.26100.0/um/d2d1.h).
 * Entries are untyped; callers cast them to the right function type.
 */

#ifndef PG0_D2D1_C_H
#define PG0_D2D1_C_H

#include <d2d1.h>

typedef struct ID2D1ResourceVtbl {
	void *QueryInterface;
	void *AddRef;
	void *Release;
	void *GetFactory;
} ID2D1ResourceVtbl;
struct ID2D1Resource {
	const ID2D1ResourceVtbl *lpVtbl;
};

typedef struct ID2D1ImageVtbl {
	void *QueryInterface;
	void *AddRef;
	void *Release;
	void *GetFactory;
} ID2D1ImageVtbl;
struct ID2D1Image {
	const ID2D1ImageVtbl *lpVtbl;
};

typedef struct ID2D1BitmapVtbl {
	void *QueryInterface;
	void *AddRef;
	void *Release;
	void *GetFactory;
	void *GetSize;
	void *GetPixelSize;
	void *GetPixelFormat;
	void *GetDpi;
	void *CopyFromBitmap;
	void *CopyFromRenderTarget;
	void *CopyFromMemory;
} ID2D1BitmapVtbl;
struct ID2D1Bitmap {
	const ID2D1BitmapVtbl *lpVtbl;
};

typedef struct ID2D1BrushVtbl {
	void *QueryInterface;
	void *AddRef;
	void *Release;
	void *GetFactory;
	void *SetOpacity;
	void *SetTransform;
	void *GetOpacity;
	void *GetTransform;
} ID2D1BrushVtbl;
struct ID2D1Brush {
	const ID2D1BrushVtbl *lpVtbl;
};

typedef struct ID2D1SolidColorBrushVtbl {
	void *QueryInterface;
	void *AddRef;
	void *Release;
	void *GetFactory;
	void *SetOpacity;
	void *SetTransform;
	void *GetOpacity;
	void *GetTransform;
	void *SetColor;
	void *GetColor;
} ID2D1SolidColorBrushVtbl;
struct ID2D1SolidColorBrush {
	const ID2D1SolidColorBrushVtbl *lpVtbl;
};

typedef struct ID2D1GeometryVtbl {
	void *QueryInterface;
	void *AddRef;
	void *Release;
	void *GetFactory;
	void *GetBounds;
	void *GetWidenedBounds;
	void *StrokeContainsPoint;
	void *FillContainsPoint;
	void *CompareWithGeometry;
	void *Simplify;
	void *Tessellate;
	void *CombineWithGeometry;
	void *Outline;
	void *ComputeArea;
	void *ComputeLength;
	void *ComputePointAtLength;
	void *Widen;
} ID2D1GeometryVtbl;
struct ID2D1Geometry {
	const ID2D1GeometryVtbl *lpVtbl;
};

typedef struct ID2D1PathGeometryVtbl {
	void *QueryInterface;
	void *AddRef;
	void *Release;
	void *GetFactory;
	void *GetBounds;
	void *GetWidenedBounds;
	void *StrokeContainsPoint;
	void *FillContainsPoint;
	void *CompareWithGeometry;
	void *Simplify;
	void *Tessellate;
	void *CombineWithGeometry;
	void *Outline;
	void *ComputeArea;
	void *ComputeLength;
	void *ComputePointAtLength;
	void *Widen;
	void *Open;
	void *Stream;
	void *GetSegmentCount;
	void *GetFigureCount;
} ID2D1PathGeometryVtbl;
struct ID2D1PathGeometry {
	const ID2D1PathGeometryVtbl *lpVtbl;
};

typedef struct ID2D1SimplifiedGeometrySinkVtbl {
	void *QueryInterface;
	void *AddRef;
	void *Release;
	void *SetFillMode;
	void *SetSegmentFlags;
	void *BeginFigure;
	void *AddLines;
	void *AddBeziers;
	void *EndFigure;
	void *Close;
} ID2D1SimplifiedGeometrySinkVtbl;
struct ID2D1SimplifiedGeometrySink {
	const ID2D1SimplifiedGeometrySinkVtbl *lpVtbl;
};

typedef struct ID2D1GeometrySinkVtbl {
	void *QueryInterface;
	void *AddRef;
	void *Release;
	void *SetFillMode;
	void *SetSegmentFlags;
	void *BeginFigure;
	void *AddLines;
	void *AddBeziers;
	void *EndFigure;
	void *Close;
	void *AddLine;
	void *AddBezier;
	void *AddQuadraticBezier;
	void *AddQuadraticBeziers;
	void *AddArc;
} ID2D1GeometrySinkVtbl;
struct ID2D1GeometrySink {
	const ID2D1GeometrySinkVtbl *lpVtbl;
};

typedef struct ID2D1RenderTargetVtbl {
	void *QueryInterface;
	void *AddRef;
	void *Release;
	void *GetFactory;
	void *CreateBitmap;
	void *CreateBitmapFromWicBitmap;
	void *CreateSharedBitmap;
	void *CreateBitmapBrush;
	void *CreateSolidColorBrush;
	void *CreateGradientStopCollection;
	void *CreateLinearGradientBrush;
	void *CreateRadialGradientBrush;
	void *CreateCompatibleRenderTarget;
	void *CreateLayer;
	void *CreateMesh;
	void *DrawLine;
	void *DrawRectangle;
	void *FillRectangle;
	void *DrawRoundedRectangle;
	void *FillRoundedRectangle;
	void *DrawEllipse;
	void *FillEllipse;
	void *DrawGeometry;
	void *FillGeometry;
	void *FillMesh;
	void *FillOpacityMask;
	void *DrawBitmap;
	void *DrawText;
	void *DrawTextLayout;
	void *DrawGlyphRun;
	void *SetTransform;
	void *GetTransform;
	void *SetAntialiasMode;
	void *GetAntialiasMode;
	void *SetTextAntialiasMode;
	void *GetTextAntialiasMode;
	void *SetTextRenderingParams;
	void *GetTextRenderingParams;
	void *SetTags;
	void *GetTags;
	void *PushLayer;
	void *PopLayer;
	void *Flush;
	void *SaveDrawingState;
	void *RestoreDrawingState;
	void *PushAxisAlignedClip;
	void *PopAxisAlignedClip;
	void *Clear;
	void *BeginDraw;
	void *EndDraw;
	void *GetPixelFormat;
	void *SetDpi;
	void *GetDpi;
	void *GetSize;
	void *GetPixelSize;
	void *GetMaximumBitmapSize;
	void *IsSupported;
} ID2D1RenderTargetVtbl;
struct ID2D1RenderTarget {
	const ID2D1RenderTargetVtbl *lpVtbl;
};

typedef struct ID2D1HwndRenderTargetVtbl {
	void *QueryInterface;
	void *AddRef;
	void *Release;
	void *GetFactory;
	void *CreateBitmap;
	void *CreateBitmapFromWicBitmap;
	void *CreateSharedBitmap;
	void *CreateBitmapBrush;
	void *CreateSolidColorBrush;
	void *CreateGradientStopCollection;
	void *CreateLinearGradientBrush;
	void *CreateRadialGradientBrush;
	void *CreateCompatibleRenderTarget;
	void *CreateLayer;
	void *CreateMesh;
	void *DrawLine;
	void *DrawRectangle;
	void *FillRectangle;
	void *DrawRoundedRectangle;
	void *FillRoundedRectangle;
	void *DrawEllipse;
	void *FillEllipse;
	void *DrawGeometry;
	void *FillGeometry;
	void *FillMesh;
	void *FillOpacityMask;
	void *DrawBitmap;
	void *DrawText;
	void *DrawTextLayout;
	void *DrawGlyphRun;
	void *SetTransform;
	void *GetTransform;
	void *SetAntialiasMode;
	void *GetAntialiasMode;
	void *SetTextAntialiasMode;
	void *GetTextAntialiasMode;
	void *SetTextRenderingParams;
	void *GetTextRenderingParams;
	void *SetTags;
	void *GetTags;
	void *PushLayer;
	void *PopLayer;
	void *Flush;
	void *SaveDrawingState;
	void *RestoreDrawingState;
	void *PushAxisAlignedClip;
	void *PopAxisAlignedClip;
	void *Clear;
	void *BeginDraw;
	void *EndDraw;
	void *GetPixelFormat;
	void *SetDpi;
	void *GetDpi;
	void *GetSize;
	void *GetPixelSize;
	void *GetMaximumBitmapSize;
	void *IsSupported;
	void *CheckWindowState;
	void *Resize;
	void *GetHwnd;
} ID2D1HwndRenderTargetVtbl;
struct ID2D1HwndRenderTarget {
	const ID2D1HwndRenderTargetVtbl *lpVtbl;
};

typedef struct ID2D1FactoryVtbl {
	void *QueryInterface;
	void *AddRef;
	void *Release;
	void *ReloadSystemMetrics;
	void *GetDesktopDpi;
	void *CreateRectangleGeometry;
	void *CreateRoundedRectangleGeometry;
	void *CreateEllipseGeometry;
	void *CreateGeometryGroup;
	void *CreateTransformedGeometry;
	void *CreatePathGeometry;
	void *CreateStrokeStyle;
	void *CreateDrawingStateBlock;
	void *CreateWicBitmapRenderTarget;
	void *CreateHwndRenderTarget;
	void *CreateDxgiSurfaceRenderTarget;
	void *CreateDCRenderTarget;
} ID2D1FactoryVtbl;
struct ID2D1Factory {
	const ID2D1FactoryVtbl *lpVtbl;
};

#endif
/* End of source */
