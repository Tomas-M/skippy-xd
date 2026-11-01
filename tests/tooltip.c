#include <assert.h>
#include <locale.h>
#include "skippy.h"

bool debuglog = false;
// Exercise the real tooltip lifecycle, rendering, Unicode layout and cache.
int main(void)
{
	setlocale(LC_ALL, "");
	session_t ps = SESSIONT_INIT;
	ps.dpy = XOpenDisplay(NULL);
	if (!ps.dpy) {
		puts("Tooltip tests require an X display (for example xvfb-run).");
		return 77;
	}
	ps.screen = DefaultScreen(ps.dpy);
	ps.root = RootWindow(ps.dpy, ps.screen);
	ps.o.pseudoTrans = false;
	wm_get_atoms(&ps);
	MainWin mw = {
		.ps = &ps, .visual = DefaultVisual(ps.dpy, ps.screen),
		.colormap = DefaultColormap(ps.dpy, ps.screen),
		.depth = DefaultDepth(ps.dpy, ps.screen), .width = 1280, .height = 800,
	};
	ps.o.tooltip_font = "DejaVu Sans,WenQuanYi Micro Hei 11";
	ps.o.tooltip_border = ps.o.tooltip_background = ps.o.tooltip_textOutline = "black";
	ps.o.tooltip_backgroundHighlight = "gray";
	ps.o.tooltip_text = "white";
	ClientWin cw = {.mainwin = &mw, .mini = {.width = 800, .height = 180}};
	Tooltip *tt = tooltip_create(&mw);
	assert(tt);

	const char *plain = "Title <b>literal</b> Čeština é";
	tooltip_map(tt, &cw, (const FcChar8 *)plain, strlen(plain));
	assert(!strcmp(pango_layout_get_text(tt->layout), plain));
	assert(!pango_layout_is_ellipsized(tt->layout));
	assert(pango_layout_get_unknown_glyphs_count(tt->layout) == 0);
	unsigned int serial = pango_layout_get_serial(tt->layout);
	unsigned int full_width = tt->width;
	for (int i = 0; i < 8; i++) {
		tooltip_map(tt, &cw, (const FcChar8 *)plain, strlen(plain));
		tooltip_draw(tt, i % 2);
	}
	assert(pango_layout_get_serial(tt->layout) == serial);

	// ICCCM COMPOUND_TEXT and Latin-1 titles must be decoded before layout.
	Window source = XCreateSimpleWindow(ps.dpy, ps.root, 0, 0, 100, 100, 0, 0, 0);
	char *compound = "Čeština 日本語 中文";
	XTextProperty property;
	assert(Xutf8TextListToTextProperty(ps.dpy, &compound, 1,
			XCompoundTextStyle, &property) == Success);
	XSetTextProperty(ps.dpy, source, &property, XA_WM_NAME);
	XFree(property.value);
	int length;
	FcChar8 *decoded = wm_get_window_title(&ps, source, &length);
	assert(decoded && !strcmp((char *)decoded, compound));
	free(decoded);
	const char latin1[] = "caf\xe9";
	XChangeProperty(ps.dpy, source, XA_WM_NAME, XA_STRING, 8,
			PropModeReplace, (const unsigned char *)latin1, 4);
	decoded = wm_get_window_title(&ps, source, &length);
	assert(decoded && !strcmp((char *)decoded, "café"));
	free(decoded);
	XChangeProperty(ps.dpy, source, XA_WM_NAME, XA_STRING, 8,
			PropModeReplace, (const unsigned char *)compound, strlen(compound));
	decoded = wm_get_window_title(&ps, source, &length);
	assert(decoded && !strcmp((char *)decoded, compound));
	free(decoded);
	char *long_title = g_strnfill(600, 'A');
	XChangeProperty(ps.dpy, source, XA_WM_NAME, XA_STRING, 8,
			PropModeReplace, (unsigned char *)long_title, 600);
	decoded = wm_get_window_title(&ps, source, &length);
	assert(decoded && length == 600);
	free(decoded);
	g_free(long_title);
	XDestroyWindow(ps.dpy, source);
	const char malformed[] = "bad \xff title";
	tooltip_map(tt, &cw, (const FcChar8 *)malformed, strlen(malformed));
	assert(g_utf8_validate(pango_layout_get_text(tt->layout), -1, NULL));
	serial = pango_layout_get_serial(tt->layout);
	tooltip_map(tt, &cw, (const FcChar8 *)malformed, strlen(malformed));
	assert(pango_layout_get_serial(tt->layout) == serial);
	tooltip_map(tt, &cw, (const FcChar8 *)plain, strlen(plain));
	puts("ICCCM encodings, long titles and malformed UTF-8 checks passed.");

	// Narrowing must preserve the original UTF-8 text so widening restores it.
	cw.mini.width = 80;
	tooltip_map(tt, &cw, (const FcChar8 *)plain, strlen(plain));
	assert(pango_layout_is_ellipsized(tt->layout));
	assert(!strcmp(pango_layout_get_text(tt->layout), plain));
	assert(tt->width < full_width);
	cw.mini.width = 800;
	tooltip_map(tt, &cw, (const FcChar8 *)plain, strlen(plain));
	assert(!pango_layout_is_ellipsized(tt->layout) && tt->width == full_width);

	const char *rtl = "العربية עברית";
	tooltip_map(tt, &cw, (const FcChar8 *)rtl, strlen(rtl));
	assert(pango_layout_get_unknown_glyphs_count(tt->layout) == 0);
	assert(pango_layout_get_direction(tt->layout, 0) == PANGO_DIRECTION_RTL);
	tooltip_draw(tt, true);

	// CJK coverage needs an installed CJK font; other checks are font-independent.
	PangoFontFamily **families;
	int count;
	bool cjk = false;
	pango_font_map_list_families(pango_context_get_font_map(
			pango_layout_get_context(tt->layout)), &families, &count);
	for (int i = 0; i < count; i++)
		cjk |= !strcmp(pango_font_family_get_name(families[i]), "WenQuanYi Micro Hei");
	g_free(families);
	if (cjk) {
		const char *mixed = "Latin 漢字かな中文";
		tooltip_map(tt, &cw, (const FcChar8 *)mixed, strlen(mixed));
		assert(pango_layout_get_unknown_glyphs_count(tt->layout) == 0);
		PangoLayoutIter *iter = pango_layout_get_iter(tt->layout);
		bool latin_font = false, cjk_font = false;
		do {
			PangoLayoutRun *run = pango_layout_iter_get_run_readonly(iter);
			if (!run) continue;
			PangoFontDescription *font = pango_font_describe(run->item->analysis.font);
			const char *family = pango_font_description_get_family(font);
			latin_font |= !strcmp(family, "DejaVu Sans");
			cjk_font |= !strcmp(family, "WenQuanYi Micro Hei");
			pango_font_description_free(font);
		} while (pango_layout_iter_next_run(iter));
		pango_layout_iter_free(iter);
		assert(latin_font && cjk_font);
		cw.mini.width = 80;
		tooltip_map(tt, &cw, (const FcChar8 *)mixed, strlen(mixed));
		assert(pango_layout_is_ellipsized(tt->layout));
		assert(!strcmp(pango_layout_get_text(tt->layout), mixed));
		tooltip_draw(tt, true);
		puts("DejaVu Latin / WenQuanYi CJK fallback and ellipsization passed.");
	} else puts("CJK coverage check skipped: WenQuanYi Micro Hei is not installed.");

	// The whole top caption fits the thumbnail and paints a native 16 px icon.
	ps.o.tooltip_width = 1.0;
	ps.o.tooltip_position = ALIGN_LEFT;
	ps.o.tooltip_align = ALIGN_LEFT;
	ps.o.tooltip_iconSize = 16;
	cw.mini.x = 100;
	cw.mini.y = 200;
	cw.mini.width = 160;
	cw.label_icon = create_pictw(&ps, 16, 16, 32);
	assert(cw.label_icon);
	XRenderColor blue = {.blue = 65535, .alpha = 65535};
	XRenderFillRectangle(ps.dpy, PictOpSrc, cw.label_icon->pict, &blue, 0, 0, 16, 16);
	tooltip_map(tt, &cw, (const FcChar8 *)plain, strlen(plain));
	assert(tt->width <= cw.mini.width && pango_layout_is_ellipsized(tt->layout));
	tooltip_draw(tt, false);
	XWindowAttributes attrs;
	XGetWindowAttributes(ps.dpy, tt->window, &attrs);
	assert(attrs.x == cw.mini.x);
	assert(attrs.y + attrs.height + 5 == cw.mini.y);
	XImage *pixel = XGetImage(ps.dpy, tt->window, 12,
			(tt->height - 16) / 2 + 8, 1, 1, AllPlanes, ZPixmap);
	assert(pixel && (XGetPixel(pixel, 0, 0) & 0xffffff) == 0x0000ff);
	XDestroyImage(pixel);
	for (int alignment = ALIGN_LEFT; alignment <= ALIGN_RIGHT; alignment++) {
		ps.o.tooltip_align = alignment;
		tooltip_map(tt, &cw, (const FcChar8 *)"Short", 5);
		XGetWindowAttributes(ps.dpy, tt->window, &attrs);
		int expected = cw.mini.x;
		if (alignment == ALIGN_MID) expected += (cw.mini.width - tt->width) / 2;
		if (alignment == ALIGN_RIGHT) expected += cw.mini.width - tt->width;
		assert(attrs.x == expected);
	}
	unsigned int with_icon = tt->width;
	cw.mode = CLIDISP_DESKTOP;
	tooltip_map(tt, &cw, (const FcChar8 *)"Short", 5);
	assert(tt->width + 22 == with_icon);
	cw.mode = CLIDISP_THUMBNAIL;
	cw.mini.width = 20;
	tooltip_map(tt, &cw, (const FcChar8 *)plain, strlen(plain));
	assert(!tt->mapped && !strcmp(pango_layout_get_text(tt->layout), plain));
	free_pictw(&ps, &cw.label_icon);
	puts("Caption width, icon pixels, alignment and desktop-label checks passed.");

	// An integrated header is a full-width child of the tile with no inner border.
	cw.tooltip = tt;
	cw.paneltype = WINTYPE_WINDOW;
	cw.src.width = 400;
	cw.mini.width = 160;
	cw.mini.window = XCreateSimpleWindow(ps.dpy, ps.root, 100, 200, 160, 180, 0, 0, 0);
	XMapWindow(ps.dpy, cw.mini.window);
	cw.caption_height = tooltip_caption_height(&cw, 0.4f);
	assert(cw.caption_height == tt->height && cw.caption_height > 16);
	assert(!tooltip_caption_height(&cw, 0.01f));
	for (int position = ALIGN_LEFT; position <= ALIGN_RIGHT; position += 2) {
		ps.o.tooltip_position = position;
		tooltip_map(tt, &cw, (const FcChar8 *)"Short", 5);
		tooltip_draw(tt, true);
		Window root, parent, *children;
		unsigned int count;
		XQueryTree(ps.dpy, tt->window, &root, &parent, &children, &count);
		XFree(children);
		assert(parent == cw.mini.window);
		XGetWindowAttributes(ps.dpy, tt->window, &attrs);
		assert(attrs.x == 0 && attrs.width == cw.mini.width);
		assert(attrs.height == cw.caption_height);
		assert(attrs.y == (position == ALIGN_LEFT ? 0 : clientwin_preview_height(&cw)));
		pixel = XGetImage(ps.dpy, tt->window, 0, 0, 1, 1, AllPlanes, ZPixmap);
		assert(pixel && XGetPixel(pixel, 0, 0) == tt->backgroundHighlight.pixel);
		XDestroyImage(pixel);
	}
	ps.o.mode = PROGMODE_PAGING;
	assert(!tooltip_caption_height(&cw, 1));
	ps.o.mode = PROGMODE_SWITCH;
	ps.o.tooltip_position = ALIGN_MID;
	assert(!tooltip_caption_height(&cw, 1));
	puts("Integrated top/bottom header geometry and shared-frame checks passed.");

	// Header line insets grow by two pixels on each side without scaling the text.
	cw.caption_height = 0;
	ps.o.tooltip_position = ALIGN_LEFT;
	tooltip_map(tt, &cw, (const FcChar8 *)"Short", 5);
	int header_height = tt->height;
	ps.o.tooltip_position = ALIGN_MID;
	tooltip_map(tt, &cw, (const FcChar8 *)"Short", 5);
	assert(tt->height + 4 == header_height);

	// Workspace bubbles remain wholly inside their preview and respect live settings.
	cw.mode = CLIDISP_DESKTOP;
	cw.mini.width = 320;
	ps.o.mode = PROGMODE_PAGING;
	ps.o.tooltip_position = ps.o.tooltip_align = ALIGN_LEFT;
	ps.o.desktop_label_padding = 5;
	ps.o.desktop_label_margin = 8;
	ps.o.desktop_label_radius = 8;
	const char *workspace = "Workspace 1   Ctrl+F1";
	tooltip_map(tt, &cw, (const FcChar8 *)workspace, strlen(workspace));
	tooltip_draw(tt, true);
	XGetWindowAttributes(ps.dpy, tt->window, &attrs);
	assert(attrs.x == cw.mini.x + 8 && attrs.y == cw.mini.y + 8);
	assert(attrs.width <= cw.mini.width - 16 && attrs.height == tt->text_height + 10);
	Bool bounding, clip;
	int bx, by, cx, cy;
	unsigned int bw, bh, clipw, cliph;
	XShapeQueryExtents(ps.dpy, tt->window, &bounding, &bx, &by, &bw, &bh,
			&clip, &cx, &cy, &clipw, &cliph);
	assert(bounding && tt->shape_radius == 8);
	ps.o.tooltip_position = ps.o.tooltip_align = ALIGN_RIGHT;
	tooltip_map(tt, &cw, (const FcChar8 *)workspace, strlen(workspace));
	XGetWindowAttributes(ps.dpy, tt->window, &attrs);
	assert(attrs.x + attrs.width == cw.mini.x + cw.mini.width - 8);
	assert(attrs.y + attrs.height == cw.mini.y + cw.mini.height - 8);
	unsigned int old_height = tt->height;
	ps.o.desktop_label_padding = 8;
	ps.o.desktop_label_radius = 0;
	tooltip_map(tt, &cw, (const FcChar8 *)workspace, strlen(workspace));
	assert(tt->height == old_height + 6);
	XShapeQueryExtents(ps.dpy, tt->window, &bounding, &bx, &by, &bw, &bh,
			&clip, &cx, &cy, &clipw, &cliph);
	assert(!bounding);
	cw.mini.width = 20;
	tooltip_map(tt, &cw, (const FcChar8 *)workspace, strlen(workspace));
	tooltip_draw(tt, true);
	assert(!tt->mapped && !tt->width);
	puts("Header padding and inset workspace bubble padding/radius checks passed.");

	tooltip_unmap(tt);
	assert(!tt->mapped && !pango_layout_get_text(tt->layout)[0]);
	tooltip_destroy(tt);
	XDestroyWindow(ps.dpy, cw.mini.window);
	pango_xft_shutdown_display(ps.dpy, ps.screen);
	XCloseDisplay(ps.dpy);
	puts("Tooltip UTF-8, RTL, resizing, layout cache and lifecycle checks passed.");
	return 0;
}
