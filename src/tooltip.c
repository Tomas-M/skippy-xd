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
	tt->parent = ps->o.pseudoTrans ? mw->window : ps->root;
	tt->mapped = false;
	tt->width = tt->height = 0;
	tt->text_height = tt->shape_width = tt->shape_height = tt->shape_radius = 0;
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
				tt->parent,
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

// Reserve a stable line for all tiles, including labels shown only on focus.
int
tooltip_caption_height(ClientWin *cw, float scale)
{
	session_t *ps = cw->mainwin->ps;
	if (!cw->tooltip || !ps->o.tooltip_show || ps->o.mode == PROGMODE_PAGING)
		return 0;
	if (cw->paneltype != WINTYPE_WINDOW || ps->o.tooltip_position == ALIGN_MID)
		return 0;
	int minimum = 9 + (cw->label_icon ? cw->label_icon->width + 6 : 0);
	return cw->src.width * scale >= minimum ? cw->tooltip->height : 0;
}

// Application headers gain two pixels above and below the existing line inset.
static int
tooltip_padding_y(Tooltip *tt)
{
	const ClientWin *cw = tt->client;
	if (cw->mode == CLIDISP_DESKTOP) return tt->mainwin->ps->o.desktop_label_padding;
	bool header = cw->paneltype == WINTYPE_WINDOW && tt->mainwin->ps->o.mode != PROGMODE_PAGING;
	return header && tt->mainwin->ps->o.tooltip_position != ALIGN_MID ? 4 : 2;
}

// Reuse the workspace bubble's bounding mask across focus and damage redraws.
static void
tooltip_shape(Tooltip *tt, int width, int height, int radius)
{
	if (tt->shape_width == width && tt->shape_height == height && tt->shape_radius == radius) return;
	session_t *ps = tt->mainwin->ps;
	Pixmap mask = None;
	if (radius) {
		mask = XCreatePixmap(ps->dpy, tt->window, width, height, 1);
		GC gc = XCreateGC(ps->dpy, mask, 0, NULL);
		XSetForeground(ps->dpy, gc, 0);
		XFillRectangle(ps->dpy, mask, gc, 0, 0, width, height);
		XSetForeground(ps->dpy, gc, 1);
		int diameter = radius * 2;
		XFillRectangle(ps->dpy, mask, gc, radius, 0, width - diameter, height);
		XFillRectangle(ps->dpy, mask, gc, 0, radius, width, height - diameter);
		for (int y = 0; y < 2; y++)
			for (int x = 0; x < 2; x++)
				XFillArc(ps->dpy, mask, gc, x * (width - diameter), y * (height - diameter), diameter, diameter, 0, 360 * 64);
		XFreeGC(ps->dpy, gc);
	}
	XShapeCombineMask(ps->dpy, tt->window, ShapeBounding, 0, 0, mask, ShapeSet);
	if (mask) XFreePixmap(ps->dpy, mask);
	tt->shape_width = width; tt->shape_height = height; tt->shape_radius = radius;
}

static void tooltip_set_visible(Tooltip *tt, bool visible);

// Normalize and measure the full title before choosing a scale for the tiles.
static bool
tooltip_set_text(Tooltip *tt, const FcChar8 *text, int len)
{
	char *valid = NULL;
	if (!g_utf8_validate((const char *)text, len, NULL)) {
		valid = g_utf8_make_valid((const char *)text, len);
		text = (const FcChar8 *)valid;
		len = strlen(valid);
	}
	const char *previous = pango_layout_get_text(tt->layout);
	bool changed = strlen(previous) != (size_t)len || memcmp(previous, text, len);
	if (changed) {
		pango_layout_set_text(tt->layout, (const char *)text, len);
		pango_layout_set_width(tt->layout, -1);
		PangoRectangle ink, logical;
		pango_layout_get_pixel_extents(tt->layout, &ink, &logical);
		tt->text_height = MAX(ink.y + ink.height, logical.y + logical.height) - MIN(ink.y, logical.y);
	}
	g_free(valid);
	return changed;
}

// Reuse the shaped layout until the full title or available width changes.
void
tooltip_map(Tooltip *tt, ClientWin *cw, const FcChar8 *text, int len)
{
	tt->client = cw;
	bool changed = tooltip_set_text(tt, text, len);
	session_t *ps = tt->mainwin->ps;
	bool desktop = cw->mode == CLIDISP_DESKTOP;
	pictw_t *icon = tooltip_icon(tt);
	int icon_width = icon ? icon->width + 6 : 0;
	int padding = desktop ? ps->o.desktop_label_padding : 4;
	tt->height = MAX(tt->text_height, icon ? icon->height : 0) + tooltip_padding_y(tt) * 2 + !desktop;
	int available = cw->mini.width * ps->o.tooltip_width;
	if (desktop) available = MIN(available, (int)cw->mini.width - ps->o.desktop_label_margin * 2);
	int text_width = available - padding * 2 - icon_width;
	bool too_short = desktop && (int)tt->height > (int)cw->mini.height - ps->o.desktop_label_margin * 2;
	if (text_width < 1 || too_short) {
		tt->width = 0;
		tooltip_set_visible(tt, false);
		return;
	}
	int max_width = text_width * PANGO_SCALE;
	if (changed || pango_layout_get_width(tt->layout) != max_width) {
		pango_layout_set_width(tt->layout, max_width);
		PangoRectangle ink, logical;
		pango_layout_get_pixel_extents(tt->layout, &ink, &logical);
		tt->extents.x = MIN(ink.x, logical.x);
		tt->extents.y = MIN(ink.y, logical.y);
		tt->extents.width = MAX(ink.x + ink.width, logical.x + logical.width) - tt->extents.x;
		tt->extents.height = MAX(ink.y + ink.height, logical.y + logical.height) - tt->extents.y;
	}
	tt->width = MIN(available, tt->extents.width + icon_width + padding * 2);
	tooltip_move(tt, cw);
}

void
tooltip_move(Tooltip *tt, ClientWin *cw) {

	session_t *ps = tt->mainwin->ps;
	bool integrated = cw->caption_height > 0;
	Window parent = integrated ? cw->mini.window : (ps->o.pseudoTrans ? tt->mainwin->window : ps->root);
	if (tt->parent != parent) {
		tooltip_set_visible(tt, false);
		XReparentWindow(ps->dpy, tt->window, parent, 0, 0);
		tt->parent = parent;
	}
	int x = 0, y = 0;
	int width = tt->width, height = tt->height;
	if (integrated) {
		width = cw->mini.width;
		height = cw->caption_height;
		if (ps->o.tooltip_position == ALIGN_RIGHT) y = clientwin_preview_height(cw);
	}
	else if (cw->mode == CLIDISP_DESKTOP) {
		int margin = ps->o.desktop_label_margin;
		x = cw->mini.x + margin;
		y = cw->mini.y + margin;
		int free_x = (int)cw->mini.width - margin * 2 - width;
		int free_y = (int)cw->mini.height - margin * 2 - height;
		if (ps->o.tooltip_align == ALIGN_MID) x += free_x / 2;
		else if (ps->o.tooltip_align == ALIGN_RIGHT) x += free_x;
		if (ps->o.tooltip_position == ALIGN_MID) y += free_y / 2;
		else if (ps->o.tooltip_position == ALIGN_RIGHT) y += free_y;
	}
	else {
		x = cw->mini.x + ps->o.tooltip_offsetX;
		if (ps->o.tooltip_align == ALIGN_MID) x += ((int)cw->mini.width - width) / 2;
		else if (ps->o.tooltip_align == ALIGN_RIGHT) x += (int)cw->mini.width - width;
		y = cw->mini.y + ps->o.tooltip_offsetY;
		if (ps->o.tooltip_position == ALIGN_LEFT) y -= height;
		else if (ps->o.tooltip_position == ALIGN_MID) y += ((int)cw->mini.height - height) / 2;
		else y += cw->mini.height;
		x = MIN(MAX(0, x), tt->mainwin->x + tt->mainwin->width - width);
		y = MIN(MAX(0, y), tt->mainwin->y + tt->mainwin->height - height);
	}
	XMoveResizeWindow(ps->dpy, tt->window, x, y, MAX(1, width), MAX(1, height));
	int radius = cw->mode == CLIDISP_DESKTOP ? ps->o.desktop_label_radius : 0;
	tooltip_shape(tt, MAX(1, width), MAX(1, height), MIN(radius, MIN(width, height) / 2));
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
	if (!tt || !tt->width || !pango_layout_get_text(tt->layout)[0])
		return;

	// label/show decides whether this label is shown at all
	bool visible = tt->mainwin->ps->o.tooltip_show == TOOLTIP_SHOW_ALL
		|| (tt->mainwin->ps->o.tooltip_show == TOOLTIP_SHOW_FOCUSED && focused);
	bool integrated = tt->client && tt->client->caption_height > 0;
	if (tt->client) tooltip_move(tt, (ClientWin *)tt->client);
	tooltip_set_visible(tt, visible || integrated);
	if (!visible && !integrated) return;
	int width = integrated ? tt->client->mini.width : tt->width;

	bool desktop = tt->client->mode == CLIDISP_DESKTOP;
	if (!integrated && !desktop) {
		XftDrawRect(tt->draw, &tt->border, 0, 0, tt->width, 1);
		XftDrawRect(tt->draw, &tt->border, 0, 1, 1, tt->height - 2);
		XftDrawRect(tt->draw, &tt->border, 0, tt->height - 1, tt->width, 1);
		XftDrawRect(tt->draw, &tt->border, tt->width - 1, 1, 1, tt->height - 2);
	}
	int inset = integrated || desktop ? 0 : 1;
	XftDrawRect(tt->draw, focused ? &tt->backgroundHighlight : &tt->background,
			inset, inset, width - inset * 2, tt->height - inset * 2);
	if (!visible) return;

	pictw_t *icon = tooltip_icon(tt);
	int left = 0;
	if (integrated && tt->mainwin->ps->o.tooltip_align == ALIGN_MID) left = (width - tt->width) / 2;
	if (integrated && tt->mainwin->ps->o.tooltip_align == ALIGN_RIGHT) left = width - tt->width;
	int padding_x = desktop ? tt->mainwin->ps->o.desktop_label_padding : 4;
	int padding_y = tooltip_padding_y(tt);
	int base_x = left + padding_x - tt->extents.x;
	int base_y = padding_y + ((int)tt->height - padding_y * 2 - !desktop - tt->extents.height) / 2 - tt->extents.y;
	if (icon) {
		XRenderComposite(tt->mainwin->ps->dpy, PictOpOver, icon->pict, None,
				XftDrawPicture(tt->draw), 0, 0, 0, 0, left + padding_x,
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
