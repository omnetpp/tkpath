/*
 * tkUnixCairoPath.c --
 *
 *	This file implements path drawing API's using the Cairo rendering engine.
 *
 * Copyright (c) 2005  Mats Bengtsson
 *
 * $Id$
 */

#include <cairo.h>
#include <cairo-xlib.h>
#include <tkUnixInt.h>
#include "tkPath.h"
#include "tkIntPath.h"

#define BlueDoubleFromXColorPtr(xc)   (double) (((xc)->pixel & 0xFF)) / 255.0
#define GreenDoubleFromXColorPtr(xc)  (double) ((((xc)->pixel >> 8) & 0xFF)) / 255.0
#define RedDoubleFromXColorPtr(xc)    (double) ((((xc)->pixel >> 16) & 0xFF)) / 255.0

extern int gUseAntiAlias;

static  cairo_t *gctx = NULL;

void TkPathInit(Display *display, Drawable d)
{
    if (gctx != NULL) {
        Tcl_Panic("the path drawing context gctx is already in use\n");
    }
    gctx = cairo_create();
    cairo_set_target_drawable(gctx, display, d);
}

void
TkPathPushTMatrix(Drawable d, TMatrix *m)
{
    cairo_matrix_t *matrix = cairo_matrix_create();
    cairo_matrix_set_affine(matrix, m->a, m->b, m->c, m->d, m->tx, m->ty);
    cairo_concat_matrix(gctx, matrix);
}

void TkPathBeginPath(Drawable d, Tk_PathStyle *style)
{
    cairo_new_path(gctx);
}

void TkPathMoveTo(Drawable d, double x, double y)
{
    cairo_move_to(gctx, x, y);
}

void TkPathLineTo(Drawable d, double x, double y)
{
    cairo_line_to(gctx, x, y);
}

void TkPathQuadBezier(Drawable d, double ctrlX, double ctrlY, double x, double y)
{
    double cx, cy;
    double x31, y31, x32, y32;
    
    cairo_current_point(gctx, &cx, &cy);

    // conversion of quadratic bezier curve to cubic bezier curve: (mozilla/svg)
    /* Unchecked! Must be an approximation! */
    x31 = cx + (ctrlX - cx) * 2 / 3;
    y31 = cy + (ctrlY - cy) * 2 / 3;
    x32 = ctrlX + (x - ctrlX) / 3;
    y32 = ctrlY + (y - ctrlY) / 3;

    cairo_curve_to(gctx, x31, y31, x32, y32, x, y);
}

void TkPathCurveTo(Drawable d, double x1, double y1, 
        double x2, double y2, double x, double y)
{
    cairo_curve_to(gctx, x1, y1, x2, y2, x, y);
}

void TkPathArcTo(Drawable d,
        double rx, double ry, 
        double phiDegrees, 	/* The rotation angle in degrees! */
        char largeArcFlag, char sweepFlag, double x, double y)
{
    TkPathArcToUsingBezier(d, rx, ry, phiDegrees, largeArcFlag, sweepFlag, x, y);
}

void TkPathClosePath(Drawable d)
{
    cairo_close_path(gctx);
}

void TkPathClipToPath(Drawable d, int fillRule)
{
    /* Clipping to path is done by default. */
    /* Note: cairo_clip does not consume the current path */
    //cairo_clip(gctx);
}

void TkPathReleaseClipToPath(Drawable d)
{
    //cairo_reset_clip(gctx);
}

void TkPathStroke(Drawable d, Tk_PathStyle *style)
{       
    Tk_Dash *dash;

    cairo_set_rgb_color(gctx,
            RedDoubleFromXColorPtr(style->strokeColor),
            GreenDoubleFromXColorPtr(style->strokeColor),
            BlueDoubleFromXColorPtr(style->strokeColor));
    cairo_set_alpha(gctx, style->strokeOpacity);
    cairo_set_line_width(gctx, style->strokeWidth);

    switch (style->capStyle) {
        case CapNotLast:
        case CapButt:
            cairo_set_line_cap(gctx, CAIRO_LINE_CAP_BUTT);
            break;
        case CapRound:
            cairo_set_line_cap(gctx, CAIRO_LINE_CAP_ROUND);
            break;
        default:
            cairo_set_line_cap(gctx, CAIRO_LINE_CAP_SQUARE);
            break;
    }
    switch (style->joinStyle) {
        case JoinMiter: 
            cairo_set_line_join(gctx, CAIRO_LINE_JOIN_MITER);
            break;
        case JoinRound:
            cairo_set_line_join(gctx, CAIRO_LINE_JOIN_ROUND);
            break;
        default:
            cairo_set_line_join(gctx, CAIRO_LINE_JOIN_BEVEL);
            break;
    }
    cairo_set_miter_limit(gctx, style->miterLimit);

    dash = &(style->dash);
    if ((dash != NULL) && (dash->number != 0)) {
        int	i, len;
        float 	*array;
    
        PathParseDashToArray(dash, style->strokeWidth, &len, &array);
        if (len > 0) {
            double *dashes = (double *) ckalloc(len*sizeof(double));

            for (i = 0; i < len; i++) {
                dashes[i] = array[i];
            }
            cairo_set_dash(gctx, dashes, len, style->offset);
            ckfree((char *) dashes);
            ckfree((char *) array);
        }
    }

    cairo_stroke(gctx);
}

void TkPathFill(Drawable d, Tk_PathStyle *style)
{
    cairo_set_rgb_color(gctx,
            RedDoubleFromXColorPtr(style->fillColor),
            GreenDoubleFromXColorPtr(style->fillColor),
            BlueDoubleFromXColorPtr(style->fillColor));
    cairo_set_alpha(gctx, style->fillOpacity);
    cairo_set_fill_rule(gctx, 
            (style->fillRule == WindingRule) ? CAIRO_FILL_RULE_WINDING : CAIRO_FILL_RULE_EVEN_ODD);
    cairo_fill(gctx);
}

void TkPathFillAndStroke(Drawable d, Tk_PathStyle *style)
{
    /*
     * The current path is consumed by filling.
     * Need therfore to save the current context and restore after.
     */
    cairo_save(gctx);
    TkPathFill(d, style);
    cairo_restore(gctx);
    TkPathStroke(d, style);
}

void TkPathEndPath(Drawable d)
{
    /* Empty ??? */
}

void TkPathFree(Drawable d)
{
    cairo_destroy(gctx);
    gctx = NULL;
}

int TkPathDrawingDestroysPath(void)
{
    /* We use save/restore instead. */
    return 0;
}

int TkPathGetCurrentPosition(Drawable d, PathPoint *pt)
{
    cairo_current_point(gctx, &(pt->x), &(pt->y));
    return TCL_OK;
}

int TkPathBoundingBox(PathRect *rPtr)
{
    return TCL_ERROR;
}

void TkPathPaintLinearGradient(Drawable d, PathRect *bbox, LinearGradientFill *fillPtr, int fillRule)
{    
    int					i;
    int					nstops;
    int					fillMethod;
    double				x1, y1, x2, y2;
    PathRect 			transition;		/* The transition line. */
    GradientStop 		*stop;
    cairo_pattern_t 	*pattern;
    cairo_extend_t		extend;
    
    /*
     * The current path is consumed by filling.
     * Need therfore to save the current context and restore after.
     */
    cairo_save(gctx);

    transition = fillPtr->transition;
    nstops = fillPtr->nstops;
    fillMethod = fillPtr->method;
    
    /* Scale up 'transition' vector to bbox. */
    x1 = bbox->x1 + (bbox->x2 - bbox->x1)*transition.x1;
    y1 = bbox->y1 + (bbox->y2 - bbox->y1)*transition.y1;
    x2 = bbox->x1 + (bbox->x2 - bbox->x1)*transition.x2;
    y2 = bbox->y1 + (bbox->y2 - bbox->y1)*transition.y2;

    pattern = cairo_pattern_create_linear(x1, y1, x2, y2);
    for (i = 0; i < nstops; i++) {
        stop = fillPtr->stops[i];
        cairo_pattern_add_color_stop(pattern, stop->offset, 
                RedDoubleFromXColorPtr(stop->color),
                GreenDoubleFromXColorPtr(stop->color),
                BlueDoubleFromXColorPtr(stop->color),
                stop->opacity);
    }
    cairo_set_pattern(gctx, pattern);
    cairo_set_fill_rule(gctx, 
            (fillRule == WindingRule) ? CAIRO_FILL_RULE_WINDING : CAIRO_FILL_RULE_EVEN_ODD);
            
    switch (fillMethod) {
        case kPathGradientMethodPad: 
            extend = CAIRO_EXTEND_NONE;
            break;
        case kPathGradientMethodRepeat:
            extend = CAIRO_EXTEND_REPEAT;
            break;
        case kPathGradientMethodReflect:
            extend = CAIRO_EXTEND_REFLECT;
            break;
        default:
            extend = CAIRO_EXTEND_NONE;
            break;
    }
    cairo_pattern_set_extend(pattern, extend);
    cairo_fill(gctx);
    
    cairo_pattern_destroy(pattern);
    cairo_restore(gctx);
}
            
