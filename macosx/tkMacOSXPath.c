/*
 * tkMacOSXPath.c --
 *
 *	This file implements path drawing API's using CoreGraphics on Mac OS X.
 *
 * Copyright (c) 2005-2006  Mats Bengtsson
 *
 * $Id$
 *
 */

#include "tkMacOSXInt.h"
#include "tkPath.h"
#include "tkIntPath.h"


#ifdef WORDS_BIGENDIAN
#	define BlueFloatFromXColorPtr(xc)   (float) ((((xc)->pixel >> 0)  & 0xFF)) / 255.0
#	define GreenFloatFromXColorPtr(xc)  (float) ((((xc)->pixel >> 8)  & 0xFF)) / 255.0
#	define RedFloatFromXColorPtr(xc)    (float) ((((xc)->pixel >> 16) & 0xFF)) / 255.0
#else
#	define BlueFloatFromXColorPtr(xc)   (float) ((((xc)->pixel >> 16) & 0xFF)) / 255.0
#	define GreenFloatFromXColorPtr(xc)  (float) ((((xc)->pixel >> 8)  & 0xFF)) / 255.0
#	define RedFloatFromXColorPtr(xc)    (float) ((((xc)->pixel >> 0)  & 0xFF)) / 255.0
#endif

#define FloatToFixed(a) ((Fixed)((float) (a) * fixed1))

extern int gUseAntiAlias;

/* For debugging. */
extern Tcl_Interp *gInterp;

const float kValidDomain[2] = {0, 1};
const float kValidRange[8] = {0, 1, 0, 1, 0, 1, 0, 1};

/*
 * This is used as a place holder for platform dependent stuff between each call.
 */
typedef struct TkPathContext_ {
    Drawable 		d;
    CGContextRef 	c;
} TkPathContext_;

typedef struct PathATSUIRecord {
    ATSUStyle 		atsuStyle;
    ATSUTextLayout 	atsuLayout;
} PathATSUIRecord;

void
PathSetUpCGContext(    
        MacDrawable *macWin,
        CGrafPtr destPort,
        CGContextRef *contextPtr)
{
    CGContextRef outContext;
    OSStatus err;
    Rect boundsRect;
    CGAffineTransform transform;

    err = QDBeginCGContext(destPort, contextPtr);
    outContext = *contextPtr;
    
    CGContextSaveGState(outContext);
    
    GetPortBounds(destPort, &boundsRect);
    
    CGContextResetCTM(outContext);
    transform = CGAffineTransformMake(1.0, 0.0, 0.0, -1.0, 0, 
            (float)(boundsRect.bottom - boundsRect.top));
    CGContextConcatCTM(outContext, transform);
    
    CGContextSetShouldAntialias(outContext, gUseAntiAlias);
    CGContextSetInterpolationQuality(outContext, kCGInterpolationHigh);
    
    /* Since we are using Pixmaps only we need no clipping or shifting. */
}

void
PathReleaseCGContext(
        MacDrawable *macWin,
        CGrafPtr destPort, 
        CGContextRef *outContext)
{
    CGContextResetCTM(*outContext);
    CGContextRestoreGState(*outContext);
    QDEndCGContext(destPort, outContext);
}

CGColorSpaceRef GetTheColorSpaceRef(void)
{
    static CGColorSpaceRef deviceRGB = NULL;
    if (deviceRGB == NULL) {
        deviceRGB = CGColorSpaceCreateDeviceRGB();
    }
    return deviceRGB;
}

#if 0	// 10.3
/* Cache some common colors to speed things up. */
typedef struct LookupColor {
    int from;
    CGColorRef colorRef;
} LookupTable;
static LookupColor ColorTable[] = {

};
void
PreallocateColorRefs(void)
{

}
#endif

static LookupTable LineCapStyleLookupTable[] = {
    {CapNotLast, 		kCGLineCapButt},
    {CapButt, 	 		kCGLineCapButt},
    {CapRound, 	 		kCGLineCapRound},
    {CapProjecting, 	kCGLineCapSquare}
};

static LookupTable LineJoinStyleLookupTable[] = {
    {JoinMiter, 	kCGLineJoinMiter},
    {JoinRound,		kCGLineJoinRound},
    {JoinBevel, 	kCGLineJoinBevel}
};

void
PathSetCGContextStyle(CGContextRef c, Tk_PathStyle *style)
{
    Tk_Dash *dash;
    int fill = 0, stroke = 0;
    
    /** Drawing attribute functions. **/
    
    /* Set the line width in the current graphics state to `width'. */    
    CGContextSetLineWidth(c, style->strokeWidth);
    
    /* Set the line cap in the current graphics state to `cap'. */
    CGContextSetLineCap(c, 
            TableLookup(LineCapStyleLookupTable, 4, style->capStyle));

    /* Set the line join in the current graphics state to `join'. */
    CGContextSetLineJoin(c,
            TableLookup(LineJoinStyleLookupTable, 3, style->joinStyle));
    
    /* Set the miter limit in the current graphics state to `limit'. */
    CGContextSetMiterLimit(c, style->miterLimit);

    /* Set the line dash patttern in the current graphics state. */
    dash = &(style->dash);
    if ((dash != NULL) && (dash->number != 0)) {
        int	len;
        float 	phase;
        float 	*array;
    
        PathParseDashToArray(dash, style->strokeWidth, &len, &array);
        if (len > 0) {
            phase = 0.0;
            CGContextSetLineDash(c, phase, array, len);
            ckfree((char *) array);
        }
    }
    
    /* Set the current fill colorspace in the context `c' to `DeviceRGB' and
     * set the components of the current fill color to `(red, green, blue,
     * alpha)'. */
    if (style->fillColor != NULL) {
        fill = 1;
        CGContextSetRGBFillColor(c, 
                RedFloatFromXColorPtr(style->fillColor), 
                GreenFloatFromXColorPtr(style->fillColor),
                BlueFloatFromXColorPtr(style->fillColor),
                style->fillOpacity);
    }
    
    /* Set the current stroke colorspace in the context `c' to `DeviceRGB' and
    * set the components of the current stroke color to `(red, green, blue,
    * alpha)'. */
    if (style->strokeColor != NULL) {
        stroke = 1;
        CGContextSetRGBStrokeColor(c, 
                RedFloatFromXColorPtr(style->strokeColor), 
                GreenFloatFromXColorPtr(style->strokeColor),
                BlueFloatFromXColorPtr(style->strokeColor),
                style->strokeOpacity);
    }
    if (stroke && fill) {
        CGContextSetTextDrawingMode(c, kCGTextFillStroke);
    } else if (stroke) {
        CGContextSetTextDrawingMode(c, kCGTextStroke);
    } else if (fill) {
        CGContextSetTextDrawingMode(c, kCGTextFill);    
    }

    if (style->fillStipple != None) {
        /* @@@ TODO */
        //CGContextSetFillPattern(c, CGPatternRef pattern, const float color[]);
    }
    if (style->strokeStipple != None) {
        /* @@@ TODO */
        //CGContextSetStrokePattern(c, CGPatternRef pattern, const float color[]);
    }
}

/* Various ATSUI support functions. */

static OSStatus
CreateATSUIStyle(const char *fontFamily, float fontSize, ATSUStyle *atsuStylePtr)
{
    OSStatus	err = noErr;
    ATSUStyle 	style;
    ATSUFontID	atsuFont;
    Fixed		atsuSize;
    ATSUAttributeTag		tags[2] = { kATSUFontTag, kATSUSizeTag };
    ByteCount		    	sizes[2] = { sizeof(ATSUFontID), sizeof(Fixed) };
    ATSUAttributeValuePtr	values[2];

    *atsuStylePtr = NULL;
    style = NULL;
    atsuFont = 0;
    atsuSize = FloatToFixed(fontSize);
    err = ATSUFindFontFromName((Ptr) fontFamily, strlen(fontFamily), kFontPostscriptName,
            kFontNoPlatformCode, kFontNoScriptCode, kFontNoLanguageCode, &atsuFont);
    /*
    	status = ATSUFindFontFromName(fontName1, strlen(fontName1), kFontFamilyName, 
        kFontMacintoshPlatform, kFontRomanScript, kFontNoLanguageCode, &atsuFont);
    */
    if (err != noErr) {
        return err;
    }
    values[0] = &atsuFont;
    values[1] = &atsuSize;
    
    err = ATSUCreateStyle(&style);
    if (err != noErr) {
        if (style) ATSUDisposeStyle(style);
        return err;
    }
    err = ATSUSetAttributes(style, sizeof(tags)/sizeof(tags[0]),
            tags, sizes, values);
    if (err != noErr) {
        if (style) ATSUDisposeStyle(style);
        return err;
    }
    *atsuStylePtr = style;
    return noErr;
}

static OSStatus
CreateLayoutForString(CFStringRef cfString, ATSUStyle atsuStyle, ATSUTextLayout *layoutPtr)
{
    ATSUTextLayout layout = NULL;
    CFIndex length;
    OSStatus err = noErr;
    UniChar *buffer;
    CFRange range;
    
    *layoutPtr = NULL;
    length = CFStringGetLength(cfString);
    if (length == 0) {
        return noErr;
    }
    range = CFRangeMake(0, length);
    buffer = (UniChar *) ckalloc(length * sizeof(UniChar));
    CFStringGetCharacters(cfString, range, buffer);
    err = ATSUCreateTextLayoutWithTextPtr(buffer, 0, 
            length, length, 1, (unsigned long *) &length, &atsuStyle, &layout);
    if (err != noErr) {
        ckfree((char *)buffer);
        return noErr;
    }
    *layoutPtr = layout;
    return noErr;
}

TkPathContext	
TkPathInit(Tk_Window tkwin, Drawable d)
{
    CGContextRef cgContext;
    TkPathContext_ *context = (TkPathContext_ *) ckalloc((unsigned) (sizeof(TkPathContext_)));
    
    PathSetUpCGContext((MacDrawable *) d, TkMacOSXGetDrawablePort(d), &cgContext);
    context->d = d;
    context->c = cgContext;
    return (TkPathContext) context;
}

void
TkPathPushTMatrix(TkPathContext ctx, TMatrix *mPtr)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGAffineTransform transform;

    /* Return the transform [ a b c d tx ty ]. */
    transform = CGAffineTransformMake(
            (float) mPtr->a, (float) mPtr->b,
            (float) mPtr->c, (float) mPtr->d,
            (float) mPtr->tx, (float) mPtr->ty);
    CGContextConcatCTM(context->c, transform);    
}

void
TkPathBeginPath(TkPathContext ctx, Tk_PathStyle *stylePtr)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGContextBeginPath(context->c);
    PathSetCGContextStyle(context->c, stylePtr);
}

void
TkPathMoveTo(TkPathContext ctx, double x, double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGContextMoveToPoint(context->c, x, y);
}

void
TkPathLineTo(TkPathContext ctx, double x, double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGContextAddLineToPoint(context->c, x, y);
}

void
TkPathLinesTo(TkPathContext ctx, double *pts, int n)
{
    //TkPathContext_ *context = (TkPathContext_ *) ctx;
    /* Add a set of lines to the context's path. */
    //CGContextAddLines(context->c, const CGPoint points[], size_t count);
}

void
TkPathQuadBezier(TkPathContext ctx, double ctrlX, double ctrlY, double x, double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGContextAddQuadCurveToPoint(context->c, ctrlX, ctrlY, x, y);
}

void
TkPathCurveTo(TkPathContext ctx, double ctrlX1, double ctrlY1, 
        double ctrlX2, double ctrlY2, double x, double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGContextAddCurveToPoint(context->c, ctrlX1, ctrlY1, ctrlX2, ctrlY2, x, y);
}

void
TkPathArcTo(TkPathContext ctx,
        double rx, double ry, 
        double phiDegrees, 	/* The rotation angle in degrees! */
        char largeArcFlag, char sweepFlag, double x, double y)
{
    //TkPathContext_ *context = (TkPathContext_ *) ctx;
    // @@@ Should we try to use the native arc functions here?
    TkPathArcToUsingBezier(ctx, rx, ry, phiDegrees, largeArcFlag, sweepFlag, x, y);
}

void
TkPathRect(TkPathContext ctx, double x, double y, double width, double height)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGRect r;
    r = CGRectMake(x, y, width, height);
    CGContextAddRect(context->c, r);
}

void
TkPathOval(TkPathContext ctx, double cx, double cy, double rx, double ry)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;

#if 0	// 10.4
    if (&CGContextAddEllipseInRect != NULL) {
        CGRect r;
        r = CGRectMake(cx-rx, cy-ry, 2*rx, 2*ry);
        CGContextAddEllipseInRect(context->c, r);
    } else {
#endif
    if (rx == ry) {
        CGContextMoveToPoint(context->c, cx+rx, cy);
        CGContextAddArc(context->c, cx, cy, rx, 0.0, 2*M_PI, 1);
        CGContextClosePath(context->c);
    } else {
        CGContextSaveGState(context->c);
        CGContextTranslateCTM(context->c, cx, cy);
        CGContextScaleCTM(context->c, rx, ry);
        CGContextMoveToPoint(context->c, 1, 0);
        CGContextAddArc(context->c, 0.0, 0.0, 1.0, 0.0, 2*M_PI, 1);
        CGContextRestoreGState(context->c);
        CGContextClosePath(context->c);
    }
}

void
TkPathImage(TkPathContext ctx, Tk_Image image, Tk_PhotoHandle photo, 
        double x, double y, double width, double height)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGRect rectangle;
    CGImageRef cgImage;
    CGDataProviderRef provider;
    CGColorSpaceRef colorspace;
    CGImageAlphaInfo alphaInfo;
    size_t size;
    Tk_PhotoImageBlock block;
    
    /* Return value? */
    Tk_PhotoGetImage(photo, &block);
    size = block.pitch * block.height;
    
    /*
     * The offset array contains the offsets from the address of a pixel to 
     * the addresses of the bytes containing the red, green, blue and alpha 
     * (transparency) components.  These are normally 0, 1, 2 and 3. 
     * @@@ There are more cases to consider than these!
     */
    if (block.offset[3] == 3) {
        alphaInfo = kCGImageAlphaLast;
    } else if (block.offset[3] == 0) {
        alphaInfo = kCGImageAlphaFirst;
    } else {
        /* @@@ What to do here? */
        return;
    }
    provider = CGDataProviderCreateWithData(NULL, block.pixelPtr, size, NULL);
    colorspace = CGColorSpaceCreateDeviceRGB();
    cgImage = CGImageCreate(block.width, block.height, 
            8, 						/* bitsPerComponent */
            block.pixelSize*8,	 	/* bitsPerPixel */
            block.pitch, 			/* bytesPerRow */
            colorspace,				/* colorspace */
            alphaInfo,				/* alphaInfo */
            provider, NULL, 
            1, 						/* shouldInterpolate */
            kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorspace);
    if (width == 0.0) {
        width = (double) block.width;
    }
    if (height == 0.0) {
        height = (double) block.height;
    }
    
    /* Flip back to an upright coordinate system since CGContextDrawImage expect this. */
    CGContextSaveGState(context->c);
    CGContextTranslateCTM(context->c, x, y+height);
    CGContextScaleCTM(context->c, 1, -1);
    rectangle = CGRectMake(0.0, 0.0, width, height);
    CGContextDrawImage(context->c, rectangle, cgImage);
    CGImageRelease(cgImage);
    CGContextRestoreGState(context->c);
}

void
TkPathClosePath(TkPathContext ctx)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGContextClosePath(context->c);
}

void
TkPathTextConfig(Tk_PathTextStyle *textStylePtr, char *text, void **customPtr)
{
    PathATSUIRecord *recordPtr;
    ATSUStyle 		atsuStyle = NULL;
    ATSUTextLayout 	atsuLayout = NULL;
    CFStringRef 	cf;    	    
    OSStatus 		err;

    TkPathTextFree(textStylePtr, *customPtr);
    err = CreateATSUIStyle(textStylePtr->fontFamily, textStylePtr->fontSize, &atsuStyle);
    if (err != noErr) {
        return;
    }
    cf = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
    /* 		choice = Tcl_GetStringFromObj(objv[i + 1], &choiceLen);
            title = CFStringCreateWithBytes(NULL, (unsigned char*) choice, choiceLen,
			kCFStringEncodingUTF8, false);
    */
    err = CreateLayoutForString(cf, atsuStyle, &atsuLayout);
    CFRelease(cf);
    if (err != noErr) {
        return;
    }
    recordPtr = (PathATSUIRecord *) ckalloc(sizeof(PathATSUIRecord));
    recordPtr->atsuStyle = atsuStyle;
    recordPtr->atsuLayout = atsuLayout;
    *customPtr = (PathATSUIRecord *) recordPtr;
    return;
}

void
TkPathTextDraw(TkPathContext ctx, Tk_PathTextStyle *textStylePtr, double x, double y, char *text, void *custom)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    PathATSUIRecord *recordPtr = (PathATSUIRecord *) custom;
    ByteCount iSize = sizeof(CGContextRef);
    ATSUAttributeTag iTag = kATSUCGContextTag;
    ATSUAttributeValuePtr iValuePtr = &(context->c);
    
    ATSUSetLayoutControls(recordPtr->atsuLayout, 1, &iTag, &iSize, &iValuePtr);
    CGContextSaveGState(context->c);
    CGContextTranslateCTM(context->c, x, y);
    CGContextScaleCTM(context->c, 1, -1);
    ATSUDrawText(recordPtr->atsuLayout, kATSUFromTextBeginning, kATSUToTextEnd, 0, 0);
    CGContextRestoreGState(context->c);
}

void
TkPathTextFree(Tk_PathTextStyle *textStylePtr, void *custom)
{
    PathATSUIRecord *recordPtr = (PathATSUIRecord *) custom;
    if (recordPtr) {
        if (recordPtr->atsuStyle) {
            ATSUDisposeStyle(recordPtr->atsuStyle);
        }
        if (recordPtr->atsuLayout) {
            ATSUDisposeTextLayout(recordPtr->atsuLayout);
        }
    }
}

PathRect
TkPathTextMeasureBbox(Tk_PathTextStyle *textStylePtr, char *text, void *custom)
{
    PathATSUIRecord *recordPtr = (PathATSUIRecord *) custom;
    ATSUTextMeasurement before, after, ascent, descent;	/* Fixed */
    PathRect r;
    
    ATSUGetUnjustifiedBounds(recordPtr->atsuLayout, kATSUFromTextBeginning, kATSUToTextEnd, 	
            &before, &after, &ascent, &descent);
    r.x1 = 0.0;
    r.y1 = -Fix2X(ascent);
    r.x2 = Fix2X(after - before);
    r.y2 = Fix2X(descent);
    return r;
}

void		
TkPathClipToPath(TkPathContext ctx, int fillRule)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;

    /* If you need to grow the clipping path after it’s shrunk, you must save the
     * graphics state before you clip, then restore the graphics state to restore the current
     * clipping path. */
    CGContextSaveGState(context->c);
    if (fillRule == WindingRule) {
        CGContextClip(context->c);
    } else if (fillRule == EvenOddRule) {
        CGContextEOClip(context->c);
    }
}

void
TkPathReleaseClipToPath(TkPathContext ctx)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGContextRestoreGState(context->c);
}

void
TkPathStroke(TkPathContext ctx, Tk_PathStyle *style)
{       
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGContextStrokePath(context->c);
}

void
TkPathFill(TkPathContext ctx, Tk_PathStyle *style)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if (style->fillRule == WindingRule) {
        CGContextFillPath(context->c);
    } else if (style->fillRule == EvenOddRule) {
        CGContextEOFillPath(context->c);
    }
}

void        
TkPathFillAndStroke(TkPathContext ctx, Tk_PathStyle *style)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if (style->fillRule == WindingRule) {
        CGContextDrawPath(context->c, kCGPathFillStroke);
    } else if (style->fillRule == EvenOddRule) {
        CGContextDrawPath(context->c, kCGPathEOFillStroke);
    }
}

void
TkPathEndPath(TkPathContext ctx)
{
    //TkPathContext_ *context = (TkPathContext_ *) ctx;
    /* Empty ??? */
}

void
TkPathFree(TkPathContext ctx)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    PathReleaseCGContext((MacDrawable *) context->d, TkMacOSXGetDrawablePort(context->d), &(context->c));
    ckfree((char *) ctx);
}

int		
TkPathDrawingDestroysPath(void)
{
    return 1;
}

int		
TkPathPixelAlign(void)
{
    return 0;
}

/* TkPathGetCurrentPosition --
 *
 * 		Returns the current pen position in untransformed coordinates!
 */
 
int		
TkPathGetCurrentPosition(TkPathContext ctx, PathPoint *ptPtr)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGPoint cgpt;
    
    cgpt = CGContextGetPathCurrentPoint(context->c);
    ptPtr->x = cgpt.x;
    ptPtr->y = cgpt.y;
    return TCL_OK;
}

int 
TkPathBoundingBox(TkPathContext ctx, PathRect *rPtr)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGRect cgRect;
    
    /* This one is not very useful since it includes the control points. */
    cgRect = CGContextGetPathBoundingBox(context->c);
    rPtr->x1 = cgRect.origin.x;
    rPtr->y1 = cgRect.origin.y;
    rPtr->x2 = cgRect.origin.x + cgRect.size.width;
    rPtr->y2 = cgRect.origin.y + cgRect.size.height;
    return TCL_OK;
}

/*
 * Using CGShading for fill gradients.
 */

static void
ShadeEvaluate(void *info, const float *in, float *out)
{
    GradientStopArray 	*stopArrPtr = (GradientStopArray *) info;
    GradientStop        **stopPtrPtr = stopArrPtr->stops;
    GradientStop		*stop1 = NULL, *stop2 = NULL;
    int					nstops = stopArrPtr->nstops;
    int					i = 0;
    float 				par = *in;
    float				f1, f2;

    /* Find the two stops for this point. Tricky! */
    while ((i < nstops) && ((*stopPtrPtr)->offset < par)) {
        stopPtrPtr++, i++;
    }
    if (i == 0) {
        /* First stop > 0. */
        stop1 = *stopPtrPtr;
        stop2 = stop1;
    } else if (i == nstops) {
        /* We have stepped beyond the last stop; step back! */
        stop1 = *(stopPtrPtr - 1);
        stop2 = stop1;
    } else {
        stop1 = *(stopPtrPtr - 1);
        stop2 = *stopPtrPtr;
    }
    /* Interpolate between the two stops. 
     * "If two gradient stops have the same offset value, 
     * then the latter gradient stop controls the color value at the 
     * overlap point."
     */
    if (fabs(stop2->offset - stop1->offset) < 1e-6) {
        *out++ = RedFloatFromXColorPtr(stop2->color);
        *out++ = GreenFloatFromXColorPtr(stop2->color);
        *out++ = BlueFloatFromXColorPtr(stop2->color); 
        *out++ = stop2->opacity;
    } else {
        f1 = (stop2->offset - par)/(stop2->offset - stop1->offset);
        f2 = (par - stop1->offset)/(stop2->offset - stop1->offset);
        *out++ = f1 * RedFloatFromXColorPtr(stop1->color) + 
                f2 * RedFloatFromXColorPtr(stop2->color);
        *out++ = f1 * GreenFloatFromXColorPtr(stop1->color) + 
                f2 * GreenFloatFromXColorPtr(stop2->color);
        *out++ = f1 * BlueFloatFromXColorPtr(stop1->color) + 
                f2 * BlueFloatFromXColorPtr(stop2->color);
        *out++ = f1 * stop1->opacity + f2 * stop2->opacity;
    }
}

static void
ShadeRelease(void *info)
{
    /* Not sure if anything to do here. */
}

void
TkPathPaintLinearGradient(TkPathContext ctx, PathRect *bbox, LinearGradientFill *fillPtr, int fillRule)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGShadingRef 		shading;
    CGPoint 			start, end;
    CGColorSpaceRef 	colorSpaceRef;
    CGFunctionRef 		function;
    CGFunctionCallbacks callbacks;
    PathRect 			*trans = fillPtr->transitionPtr;		/* The transition line. */
    GradientStopArray 	*stopArrPtr = fillPtr->stopArrPtr;

    callbacks.version = 0;
    callbacks.evaluate = ShadeEvaluate;
    callbacks.releaseInfo = ShadeRelease;
    colorSpaceRef = CGColorSpaceCreateDeviceRGB();

    /*
     * We need to do like this since this is how SVG defines gradient drawing
     * in case the transition vector is in relative coordinates.
     */
    if (fillPtr->units == kPathGradientUnitsBoundingBox) {
        CGContextSaveGState(context->c);
        CGContextTranslateCTM(context->c, bbox->x1, bbox->y1);
        CGContextScaleCTM(context->c, bbox->x2 - bbox->x1, bbox->y2 - bbox->y1);
    }
    function = CGFunctionCreate((void *) stopArrPtr, 1, kValidDomain, 4, kValidRange, &callbacks);
    start = CGPointMake(trans->x1, trans->y1);
    end   = CGPointMake(trans->x2, trans->y2);
    shading = CGShadingCreateAxial(colorSpaceRef, start, end, function, 1, 1);
    CGContextDrawShading(context->c, shading);
    CGShadingRelease(shading);
    CGFunctionRelease(function);
    CGColorSpaceRelease(colorSpaceRef);
    if (fillPtr->units == kPathGradientUnitsBoundingBox) {
        CGContextRestoreGState(context->c);
    }
}

void
TkPathPaintRadialGradient(TkPathContext ctx, PathRect *bbox, RadialGradientFill *fillPtr, int fillRule)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGShadingRef 		shading;
    CGPoint 			start, end;
    CGColorSpaceRef 	colorSpaceRef;
    CGFunctionRef 		function;
    CGFunctionCallbacks callbacks;
    RadialTransition    *tPtr = fillPtr->radialPtr;
    GradientStopArray 	*stopArrPtr = fillPtr->stopArrPtr;
    
    callbacks.version = 0;
    callbacks.evaluate = ShadeEvaluate;
    callbacks.releaseInfo = ShadeRelease;
    colorSpaceRef = CGColorSpaceCreateDeviceRGB();

    /*
     * We need to do like this since this is how SVG defines gradient drawing
     * in case the transition vector is in relative coordinates.
     */
    if (fillPtr->units == kPathGradientUnitsBoundingBox) {
        CGContextSaveGState(context->c);
        CGContextTranslateCTM(context->c, bbox->x1, bbox->y1);
        CGContextScaleCTM(context->c, bbox->x2 - bbox->x1, bbox->y2 - bbox->y1);
    }
    function = CGFunctionCreate((void *) stopArrPtr, 1, kValidDomain, 4, kValidRange, &callbacks);
    start = CGPointMake(tPtr->focalX, tPtr->focalY);
    end   = CGPointMake(tPtr->centerX, tPtr->centerY);
    shading = CGShadingCreateRadial(colorSpaceRef, start, 0.0, end, tPtr->radius, function, 1, 1);
    CGContextDrawShading(context->c, shading);
    CGShadingRelease(shading);
    CGFunctionRelease(function);
    CGColorSpaceRelease(colorSpaceRef);
    if (fillPtr->units == kPathGradientUnitsBoundingBox) {
        CGContextRestoreGState(context->c);
    }
}

