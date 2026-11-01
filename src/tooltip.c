/* Skippy-xd
 *
 * Copyright (C) 2004 Hyriand <hyriand@thegraveyard.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "skippy.h"

void
tooltip_destroy(Tooltip *tt)
{
	session_t * const ps = tt->mainwin->ps;

	if(tt->layout)
		g_object_unref(tt->layout);
	if(tt->draw)
		XftDrawDestroy(tt->draw);
	if(tt->color.pixel != None)
		XftColorFree(ps->dpy,
		             tt->mainwin->visual,
		             tt->mainwin->colormap,
		             &tt->color);
	if(tt->background.pixel != None)
		XftColorFree(ps->dpy,
		             tt->mainwin->visual,
		             tt->mainwin->colormap,
		             &tt->background);
	if(tt->border.pixel != None)
		XftColorFree(ps->dpy,
		             tt->mainwin->visual,
		             tt->mainwin->colormap,
		             &tt->border);
	if(tt->outline.pixel != None)
		XftColorFree(ps->dpy,
		             tt->mainwin->visual,
		             tt->mainwin->colormap,
		             &tt->outline);
	if(tt->window != None)
		XDestroyWindow(ps->dpy, tt->window);
	
	free(tt);
}

Tooltip *
tooltip_create(MainWin *mw) {
	session_t * const ps = mw->ps;
	const char *tmp;
	long int tmp_l;
	
	Tooltip *tt = allocchk(malloc(sizeof(Tooltip)));
	
	tt->mainwin = mw;
	tt->client = NULL;
	tt->window = None;
	tt->mapped = false;
	tt->width = tt->height = 0;
	tt->layout = NULL;
	tt->draw = 0;
	tt->color.pixel = tt->background.pixel = tt->border.pixel = tt->outline.pixel = None;
	
	{
		XSetWindowAttributes attr = {
			.override_redirect = True,
			.border_pixel = None,
			.background_pixel = None,
			.event_mask = ExposureMask,
			.colormap = mw->colormap,
		};
		
		tt->window = XCreateWindow(ps->dpy,
				ps->o.pseudoTrans ? mw->window : ps->root,
				0, 0, 1, 1, 0,
				mw->depth, InputOutput, mw->visual,
				CWBorderPixel|CWBackPixel|CWOverrideRedirect|CWEventMask|CWColormap,
				&attr);
	}

	if (!tt->window) {
		printfef(false, "(): WARNING: Couldn't create tooltip window.");
		tooltip_destroy(tt);
		return 0;
	}
	wm_wid_set_info(ps, tt->window, "skippy-xd label", _NET_WM_WINDOW_TYPE_TOOLTIP);

	tmp = ps->o.tooltip_border;
	if(! XftColorAllocName(ps->dpy, mw->visual, mw->colormap, tmp, &tt->border))
	{
		printfef(false, "(): WARNING: Invalid color '%s'.\n", tmp);
		tooltip_destroy(tt);
		return 0;
	}

	tmp = ps->o.tooltip_background;
	if(! XftColorAllocName(ps->dpy, mw->visual, mw->colormap, tmp, &tt->background))
	{
		printfef(false, "(): WARNING: Invalid color '%s'.\n", tmp);
		tooltip_destroy(tt);
		return 0;
	}

	tmp = ps->o.tooltip_backgroundHighlight;
	if(! XftColorAllocName(ps->dpy, mw->visual, mw->colormap, tmp, &tt->backgroundHighlight))
	{
		printfef(false, "(): WARNING: Invalid color '%s'.\n", tmp);
		tooltip_destroy(tt);
		return 0;
	}

	tmp_l = alphaconv(ps->o.tooltip_opacity);
	tt->background.color.alpha = tmp_l;
	tt->backgroundHighlight.color.alpha = tmp_l;
	tt->border.color.alpha = tmp_l;
	
	tmp = ps->o.tooltip_text;
	if(! XftColorAllocName(ps->dpy, mw->visual, mw->colormap, tmp, &tt->color))
	{
		printfef(false, "(): WARNING: Couldn't allocate color '%s'.\n", tmp);
		tooltip_destroy(tt);
		return 0;
	}
	
	tmp = ps->o.tooltip_textOutline;
	if(strcasecmp(tmp, "none") != 0)
	{
		if(! XftColorAllocName(ps->dpy, mw->visual, mw->colormap, tmp, &tt->outline))
		{
			printfef(false, "(): WARNING: Couldn't allocate color '%s'.\n", tmp);
			tooltip_destroy(tt);
			return 0;
		}
	}
	
	tt->draw = XftDrawCreate(ps->dpy, tt->window, mw->visual, mw->colormap);
	if(! tt->draw)
	{
		printfef(false, "(): WARNING: Couldn't create Xft draw surface.\n");
		tooltip_destroy(tt);
		return 0;
	}
	
	// Pango owns fallback, shaping and truncation; Xft still draws our window.
	PangoContext *context = pango_font_map_create_context(
			pango_xft_get_font_map(ps->dpy, ps->screen));
	tt->layout = pango_layout_new(context);
	g_object_unref(context);
	PangoFontDescription *font = pango_font_description_from_string(ps->o.tooltip_font);
	pango_layout_set_font_description(tt->layout, font);
	pango_font_description_free(font);
	pango_layout_set_single_paragraph_mode(tt->layout, TRUE);
	pango_layout_set_ellipsize(tt->layout, PANGO_ELLIPSIZE_END);
	pango_layout_set_height(tt->layout, -1);

	// Set tooltip window input region to empty to prevent disgusting
	// racing situations
	{
		XserverRegion region = XFixesCreateRegion(ps->dpy, NULL, 0);
		XFixesSetWindowShapeRegion(ps->dpy, tt->window, ShapeInput, 0, 0, region);
		XFixesDestroyRegion(ps->dpy, region);
	}

	return tt;
}

// Desktop-page labels have no application icon.
static pictw_t *
tooltip_icon(Tooltip *tt)
{
	if (!tt->client || tt->client->mode == CLIDISP_DESKTOP
			|| !tt->mainwin->ps->o.tooltip_iconSize) return NULL;
	return tt->client->label_icon;
}

// Reuse the shaped layout until the full title or available width changes.
void
tooltip_map(Tooltip *tt, ClientWin *cw, const FcChar8 *text, int len)
{
	session_t * const ps = tt->mainwin->ps;
	tt->client = cw;
	pictw_t *icon = tooltip_icon(tt);
	int icon_width = icon ? icon->width + 6 : 0;
	int available = cw->mini.width * ps->o.tooltip_width;
	int text_width = available - 8 - icon_width;
	if (text_width < 1) {
		tooltip_unmap(tt);
		return;
	}
	// Broken UTF8_STRING properties must not warn or invalidate the layout on each draw.
	char *valid = NULL;
	if (!g_utf8_validate((const char *)text, len, NULL)) {
		valid = g_utf8_make_valid((const char *)text, len);
		text = (const FcChar8 *)valid;
		len = strlen(valid);
	}
	int max_width = text_width * PANGO_SCALE;
	const char *previous = pango_layout_get_text(tt->layout);
	bool changed = strlen(previous) != (size_t)len || memcmp(previous, text, len);
	if (changed || pango_layout_get_width(tt->layout) != max_width) {
		pango_layout_set_text(tt->layout, (const char *)text, len);
		pango_layout_set_width(tt->layout, max_width);
		PangoRectangle ink, logical;
		pango_layout_get_pixel_extents(tt->layout, &ink, &logical);
		// Include overhangs and combining marks as well as the logical line box.
		tt->extents.x = MIN(ink.x, logical.x);
		tt->extents.y = MIN(ink.y, logical.y);
		tt->extents.width = MAX(ink.x + ink.width, logical.x + logical.width) - tt->extents.x;
		tt->extents.height = MAX(ink.y + ink.height, logical.y + logical.height) - tt->extents.y;
	}
	// The icon, spacing, padding and text must all fit inside the preview width.
	unsigned int width = MIN(available, tt->extents.width + icon_width + 8);
	unsigned int height = MAX(tt->extents.height, icon ? icon->height : 0) + 5;
	if (tt->width != width || tt->height != height) {
		tt->width = width;
		tt->height = height;
		XResizeWindow(ps->dpy, tt->window, width, height);
	}
	g_free(valid);
	tooltip_move(tt, cw);
}

void
tooltip_move(Tooltip *tt, ClientWin *cw) {

	session_t *ps = tt->mainwin->ps;
	int x = ps->o.tooltip_offsetX,
		y = ps->o.tooltip_offsetY;

	x += cw->mini.x;
	if (ps->o.tooltip_align == ALIGN_MID)
		x += ((int)cw->mini.width - (int)tt->width) / 2;
	else if (ps->o.tooltip_align == ALIGN_RIGHT)
		x += (int)cw->mini.width - (int)tt->width;
	switch (ps->o.tooltip_position) {
		case ALIGN_LEFT:	// top: label sits above the window
			y += cw->mini.y - tt->height;
			break;
		case ALIGN_MID:		// mid: label in the middle of the window
			y += cw->mini.y + cw->mini.height/2 - tt->height / 2;
			break;
		default:			// bottom (default): label below the window
			y += cw->mini.y + cw->mini.height;
			break;
	}

	x = MIN(MAX(0, x), tt->mainwin->x + tt->mainwin->width - tt->width);
	y = MIN(MAX(0, y), tt->mainwin->y + tt->mainwin->height - tt->height);
	
	XMoveWindow(tt->mainwin->ps->dpy, tt->window, x, y);
}

// Map or unmap the label window, only when its state actually changes.
// A label is raised when it is mapped so that it stays on top of
// thumbnails mapped after it (e.g. a bottom label overlapping the next row).
static void
tooltip_set_visible(Tooltip *tt, bool visible)
{
	if (visible == tt->mapped)
		return;

	if (visible)
		XMapRaised(tt->mainwin->ps->dpy, tt->window);
	else
		XUnmapWindow(tt->mainwin->ps->dpy, tt->window);
	tt->mapped = visible;
}

void
tooltip_unmap(Tooltip *tt)
{
	tooltip_set_visible(tt, false);
	pango_layout_set_text(tt->layout, "", 0);
}

void
tooltip_draw(Tooltip *tt, bool focused)
{
	if (!tt || !pango_layout_get_text(tt->layout)[0])
		return;

	// label/show decides whether this label is shown at all
	bool visible = tt->mainwin->ps->o.tooltip_show == TOOLTIP_SHOW_ALL
		|| (tt->mainwin->ps->o.tooltip_show == TOOLTIP_SHOW_FOCUSED && focused);
	tooltip_set_visible(tt, visible);
	if (!visible)
		return;

	XftDrawRect(tt->draw, &tt->border, 0, 0, tt->width, 1);
	XftDrawRect(tt->draw, &tt->border, 0, 1, 1, tt->height - 2);
	XftDrawRect(tt->draw, &tt->border, 0, tt->height - 1, tt->width, 1);
	XftDrawRect(tt->draw, &tt->border, tt->width - 1, 1, 1, tt->height - 2);

	if (focused)
		XftDrawRect(tt->draw, &tt->backgroundHighlight, 1, 1, tt->width - 2, tt->height - 2);
	else
		XftDrawRect(tt->draw, &tt->background, 1, 1, tt->width - 2, tt->height - 2);

	pictw_t *icon = tooltip_icon(tt);
	int base_x = 4 - tt->extents.x;
	int base_y = 2 + ((int)tt->height - 5 - tt->extents.height) / 2 - tt->extents.y;
	if (icon) {
		XRenderComposite(tt->mainwin->ps->dpy, PictOpOver, icon->pict, None,
				XftDrawPicture(tt->draw), 0, 0, 0, 0, 4,
				((int)tt->height - icon->height) / 2, icon->width, icon->height);
		base_x += icon->width + 6;
	}

	if (tt->outline.pixel != None) {
		for (int dx = -1; dx <= 1; dx++) {
			for (int dy = -1; dy <= 1; dy++) {
				if (dx == 0 && dy == 0)
					continue;
				pango_xft_render_layout(tt->draw, &tt->outline, tt->layout,
						(base_x + dx) * PANGO_SCALE, (base_y + dy) * PANGO_SCALE);
			}
		}
	}

	pango_xft_render_layout(tt->draw, &tt->color, tt->layout,
			base_x * PANGO_SCALE, base_y * PANGO_SCALE);
}
