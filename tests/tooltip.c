#include <assert.h>
#include "skippy.h"

bool debuglog = false;
Atom _NET_WM_WINDOW_TYPE_TOOLTIP;

// Window metadata is independent of layout and does not need a window manager.
void wm_wid_set_info(session_t *ps, Window window, const char *name, Atom type) {}

// Exercise the real tooltip lifecycle, rendering, Unicode layout and cache.
int main(void)
{
	session_t ps = SESSIONT_INIT;
	ps.dpy = XOpenDisplay(NULL);
	if (!ps.dpy) {
		puts("Tooltip tests require an X display (for example xvfb-run).");
		return 77;
	}
	ps.screen = DefaultScreen(ps.dpy);
	ps.root = RootWindow(ps.dpy, ps.screen);
	ps.o.pseudoTrans = false;
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
	assert(!tt->mapped && !pango_layout_get_text(tt->layout)[0]);
	free_pictw(&ps, &cw.label_icon);
	puts("Caption width, icon pixels, alignment and desktop-label checks passed.");

	tooltip_unmap(tt);
	assert(!tt->mapped && !pango_layout_get_text(tt->layout)[0]);
	tooltip_destroy(tt);
	pango_xft_shutdown_display(ps.dpy, ps.screen);
	XCloseDisplay(ps.dpy);
	puts("Tooltip UTF-8, RTL, resizing, layout cache and lifecycle checks passed.");
	return 0;
}
