#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcft/fcft.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <pixman-1/pixman.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-util.h>

#ifdef __unix__
#include <unistd.h>
#endif

#include "utf8.h"
#include "xdg-shell-protocol.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "river-status-unstable-v1-protocol.h"
#include "river-control-unstable-v1-protocol.h"

#define DIE(fmt, ...)						\
	do {							\
		fprintf(stderr, fmt "\n", ##__VA_ARGS__);	\
		exit(1);					\
	} while (0)
#define EDIE(fmt, ...)						\
	DIE(fmt ": %s", ##__VA_ARGS__, strerror(errno));

#define MIN(a, b)				\
	((a) < (b) ? (a) : (b))
#define MAX(a, b)				\
	((a) > (b) ? (a) : (b))

#define PROGRAM "sandbar"
#define VERSION "0.2"
#define USAGE								\
	"usage: sandbar [OPTIONS]\n"					\
	"Bar Config\n"							\
	"	-hidden					bars will initially be hidden\n" \
	"	-bottom					bars will initially be drawn at the bottom\n" \
	"	-hide-vacant-tags			do not display empty and inactive tags\n" \
	"	-no-title				do not display current view title\n" \
	"	-no-status-commands			disable in-line commands in status text\n" \
	"	-no-layout				do not display the current layout\n" \
	"	-no-mode				do not display the current mode\n" \
	"	-font [FONT]				specify a font\n" \
	"	-tags [NUMBER OF TAGS] [FIRST]...[LAST]	specify custom tag names\n" \
	"	-vertical-padding [PIXELS]		specify vertical pixel padding above and below text\n" \
	"	-scale [BUFFER_SCALE]			specify buffer scale value for integer scaling\n" \
	"	-active-fg-color [RGBA]			specify text color of active tags or monitors\n" \
	"	-active-bg-color [RGBA]			specify background color of active tags or monitors\n" \
	"	-inactive-fg-color [RGBA]		specify text color of inactive tags or monitors\n" \
	"	-inactive-bg-color [RGBA]		specify background color of inactive tags or monitors\n" \
	"	-urgent-fg-color [RGBA]			specify text color of urgent tags\n" \
	"	-urgent-bg-color [RGBA]			specify background color of urgent tags\n" \
	"	-title-fg-color [RGBA]			specify text color of title bar\n" \
	"	-title-bg-color [RGBA]			specify background color of title bar\n" \
	"Other\n"							\
	"	-v					get version information\n" \
	"	-h					view this help text\n"

typedef struct {
	struct wl_output *wl_output;
	struct wl_surface *wl_surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct zriver_output_status_v1 *river_output_status;
	
	uint32_t registry_name;
	char *output_name;

	bool configured;
	uint32_t width, height;
	uint32_t textpadding;
	uint32_t stride, bufsize;
	
	uint32_t mtags, ctags, urg;
	bool sel;
	char *layout, *title, *status;
	
	bool hidden, bottom;
	bool redraw;

	struct wl_list link;
} Bar;

typedef struct {
	struct wl_seat *wl_seat;
	struct wl_pointer *wl_pointer;
	struct zriver_seat_status_v1 *river_seat_status;
	uint32_t registry_name;

	Bar *bar;
	bool hovering;
	uint32_t pointer_x, pointer_y;
	uint32_t pointer_button;

	char *mode;
	
	struct wl_list link;
} Seat;

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct zriver_status_manager_v1 *river_status_manager;
static struct zriver_control_v1 *river_control;
static struct wl_cursor_image *cursor_image;
static struct wl_surface *cursor_surface;

static struct wl_list bar_list, seat_list;

static char **tags;
static uint32_t tags_l;

static char *fontstr = "monospace:size=16";
static struct fcft_font *font;
static uint32_t height, textpadding, vertical_padding = 1, buffer_scale = 1;

static bool hidden, bottom, hide_vacant, no_title, no_status_commands, no_mode, no_layout;

static pixman_color_t active_fg_color = { .red = 0xeeee, .green = 0xeeee, .blue = 0xeeee, .alpha = 0xffff, };
static pixman_color_t active_bg_color = { .red = 0x0000, .green = 0x5555, .blue = 0x7777, .alpha = 0xffff, };
static pixman_color_t inactive_fg_color = { .red = 0xbbbb, .green = 0xbbbb, .blue = 0xbbbb, .alpha = 0xffff, };
static pixman_color_t inactive_bg_color = { .red = 0x2222, .green = 0x2222, .blue = 0x2222, .alpha = 0xffff, };
static pixman_color_t urgent_fg_color = { .red = 0x2222, .green = 0x2222, .blue = 0x2222, .alpha = 0xffff, };
static pixman_color_t urgent_bg_color = { .red = 0xeeee, .green = 0xeeee, .blue = 0xeeee, .alpha = 0xffff, };
static pixman_color_t title_fg_color = { .red = 0xeeee, .green = 0xeeee, .blue = 0xeeee, .alpha = 0xffff, };
static pixman_color_t title_bg_color = { .red = 0x0000, .green = 0x5555, .blue = 0x7777, .alpha = 0xffff, };

static bool run_display;

static void
wl_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	/* Sent by the compositor when it's no longer using this buffer */
	wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener = {
	.release = wl_buffer_release,
};

/* Shared memory support function adapted from [wayland-book] */
static int
allocate_shm_file(size_t size)
{
	int fd = memfd_create("surface", MFD_CLOEXEC);
	if (fd == -1)
		return -1;
	int ret;
	do {
		ret = ftruncate(fd, size);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1) {
		close(fd);
		return -1;
	}
	return fd;
}

/* Color parsing logic adapted from [sway] */
static int
parse_color(const char *str, pixman_color_t *clr)
{
	if (*str == '#')
		str++;
	int len = strlen(str);

	// Disallows "0x" prefix that strtoul would ignore
	if ((len != 6 && len != 8) || !isxdigit(str[0]) || !isxdigit(str[1]))
		return -1;

	char *ptr;
	uint32_t parsed = strtoul(str, &ptr, 16);
	if (*ptr)
		return -1;

	if (len == 8) {
		clr->alpha = (parsed & 0xff) * 0x101;
		parsed >>= 8;
	} else {
		clr->alpha = 0xffff;
	}
	clr->red = ((parsed >> 16) & 0xff) * 0x101;
	clr->green = ((parsed >>  8) & 0xff) * 0x101;
	clr->blue = ((parsed >>  0) & 0xff) * 0x101;
	return 0;
}

static uint32_t
draw_text(char *text,
	  uint32_t x,
	  uint32_t y,
	  pixman_image_t *foreground,
	  pixman_image_t *background,
	  pixman_color_t *fg_color,
	  pixman_color_t *bg_color,
	  uint32_t max_x,
	  uint32_t buf_height,
	  uint32_t padding,
	  bool commands)
{
	if (!text || !*text || !max_x)
		return x;

	uint32_t ix = x, nx;

	if ((nx = x + padding) + padding >= max_x)
		return x;
	x = nx;

	bool draw_fg = foreground && fg_color;
	bool draw_bg = background && bg_color;

	pixman_image_t *fg_fill;
	pixman_color_t cur_bg_color;
	if (draw_fg)
		fg_fill = pixman_image_create_solid_fill(fg_color);
	if (draw_bg)
		cur_bg_color = *bg_color;

	uint32_t codepoint, state = UTF8_ACCEPT, last_cp = 0;
	for (char *p = text; *p; p++) {
		/* Check for inline ^ commands */
		if (!no_status_commands && commands && state == UTF8_ACCEPT && *p == '^') {
			p++;
			if (*p != '^') {
				/* Parse color */
				char *arg, *end;
				if (!(arg = strchr(p, '(')) || !(end = strchr(arg + 1, ')')))
					continue;
				*arg++ = '\0';
				*end = '\0';
				if (!strcmp(p, "bg")) {
					if (draw_bg) {
						if (!*arg)
							cur_bg_color = *bg_color;
						else
							parse_color(arg, &cur_bg_color);
					}
				} else if (!strcmp(p, "fg")) {
					if (draw_fg) {
						pixman_color_t color;
						bool refresh = true;
						if (!*arg)
							color = *fg_color;
						else if (parse_color(arg, &color) == -1)
							refresh = false;
						if (refresh) {
							pixman_image_unref(fg_fill);
							fg_fill = pixman_image_create_solid_fill(&color);
						}
					}
				}

				/* Restore string for later redraws */
				*--arg = '(';
				*end = ')';
				p = end;
				continue;
			}
		}

		/* Returns nonzero if more bytes are needed */
		if (utf8decode(&state, &codepoint, *p))
			continue;

		/* Turn off subpixel rendering, which complicates things when
		 * mixed with alpha channels */
		const struct fcft_glyph *glyph = fcft_rasterize_char_utf32(font, codepoint, FCFT_SUBPIXEL_NONE);
		if (!glyph)
			continue;

		/* Adjust x position based on kerning with previous glyph */
		long kern = 0;
		if (last_cp)
			fcft_kerning(font, last_cp, codepoint, &kern, NULL);
		if ((nx = x + kern + glyph->advance.x) + padding > max_x)
			break;
		last_cp = codepoint;
		x += kern;

		if (draw_fg) {
			/* Detect and handle pre-rendered glyphs (e.g. emoji) */
			if (pixman_image_get_format(glyph->pix) == PIXMAN_a8r8g8b8) {
				/* Only the alpha channel of the mask is used, so we can
				 * use fgfill here to blend prerendered glyphs with the
				 * same opacity */
				pixman_image_composite32(
					PIXMAN_OP_OVER, glyph->pix, fg_fill, foreground, 0, 0, 0, 0,
					x + glyph->x, y - glyph->y, glyph->width, glyph->height);
			} else {
				/* Applying the foreground color here would mess up
				 * component alphas for subpixel-rendered text, so we
				 * apply it when blending. */
				pixman_image_composite32(
					PIXMAN_OP_OVER, fg_fill, glyph->pix, foreground, 0, 0, 0, 0,
					x + glyph->x, y - glyph->y, glyph->width, glyph->height);
			}
		}
		
		if (draw_bg) {
			pixman_image_fill_boxes(PIXMAN_OP_OVER, background,
						&cur_bg_color, 1, &(pixman_box32_t){
							.x1 = x, .x2 = nx,
							.y1 = 0, .y2 = buf_height
						});
		}
		
		/* increment pen position */
		x = nx;
	}
	
	if (draw_fg)
		pixman_image_unref(fg_fill);
	if (!last_cp)
		return ix;
	
	nx = x + padding;
	if (draw_bg) {
		/* Fill padding background */
		pixman_image_fill_boxes(PIXMAN_OP_OVER, background,
					bg_color, 1, &(pixman_box32_t){
						.x1 = ix, .x2 = ix + padding,
						.y1 = 0, .y2 = buf_height
					});
		pixman_image_fill_boxes(PIXMAN_OP_OVER, background,
					bg_color, 1, &(pixman_box32_t){
						.x1 = x, .x2 = nx,
						.y1 = 0, .y2 = buf_height
					});
	}
	
	return nx;
}

#define TEXT_WIDTH(text, maxwidth, padding, commands)			\
	draw_text(text, 0, 0, NULL, NULL, NULL, NULL, maxwidth, 0, padding, commands)

static int
draw_frame(Bar *bar)
{
	/* Allocate buffer to be attached to the surface */
        int fd = allocate_shm_file(bar->bufsize);
	if (fd == -1)
		return -1;

	uint32_t *data = mmap(NULL, bar->bufsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		return -1;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, bar->bufsize);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, bar->width, bar->height, bar->stride, WL_SHM_FORMAT_ARGB8888);
	wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
	wl_shm_pool_destroy(pool);
	close(fd);

	/* Pixman image corresponding to main buffer */
	pixman_image_t *final = pixman_image_create_bits(PIXMAN_a8r8g8b8, bar->width, bar->height, data, bar->width * 4);
	
	/* Text background and foreground layers */
	pixman_image_t *foreground = pixman_image_create_bits(PIXMAN_a8r8g8b8, bar->width, bar->height, NULL, bar->width * 4);
	pixman_image_t *background = pixman_image_create_bits(PIXMAN_a8r8g8b8, bar->width, bar->height, NULL, bar->width * 4);
	
	/* Draw on images */
	uint32_t x = 0;
	uint32_t y = (bar->height + font->ascent - font->descent) / 2;
	uint32_t boxs = font->height / 9;
	uint32_t boxw = font->height / 6 + 2;
	
	for (uint32_t i = 0; i < tags_l; i++) {
		const bool active = bar->mtags & 1 << i;
		const bool occupied = bar->ctags & 1 << i;
		const bool urgent = bar->urg & 1 << i;
		
		if (hide_vacant && !active && !occupied && !urgent)
			continue;

		pixman_color_t *fg_color = urgent ? &urgent_fg_color : (active ? &active_fg_color : &inactive_fg_color);
		pixman_color_t *bg_color = urgent ? &urgent_bg_color : (active ? &active_bg_color : &inactive_bg_color);
		
		if (!hide_vacant && occupied) {
			pixman_image_fill_boxes(PIXMAN_OP_SRC, foreground,
						fg_color, 1, &(pixman_box32_t){
							.x1 = x + boxs, .x2 = x + boxs + boxw,
							.y1 = boxs, .y2 = boxs + boxw
						});
			if ((!bar->sel || !active) && boxw >= 3) {
				/* Make box hollow */
				pixman_image_fill_boxes(PIXMAN_OP_SRC, foreground,
							&(pixman_color_t){ 0 },
							1, &(pixman_box32_t){
								.x1 = x + boxs + 1, .x2 = x + boxs + boxw - 1,
								.y1 = boxs + 1, .y2 = boxs + boxw - 1
							});
			}
		}
		
		x = draw_text(tags[i], x, y, foreground, background, fg_color, bg_color,
			      bar->width, bar->height, bar->textpadding, false);
	}

	if (!no_mode) {
		Seat *seat;
		wl_list_for_each(seat, &seat_list, link) {
			x = draw_text(seat->mode, x, y, foreground, background,
					  &inactive_fg_color, &inactive_bg_color, bar->width,
					  bar->height, bar->textpadding, false);
		}
	}

	if (!no_layout) {
		if (bar->mtags & bar->ctags) {
			x = draw_text(bar->layout, x, y, foreground, background,
					  &inactive_fg_color, &inactive_bg_color, bar->width,
					  bar->height, bar->textpadding, false);
		}
	}
	
	uint32_t status_width = TEXT_WIDTH(bar->status, bar->width - x, bar->textpadding, true);
	draw_text(bar->status, bar->width - status_width, y, foreground,
		  background, &inactive_fg_color, &inactive_bg_color,
		  bar->width, bar->height, bar->textpadding, true);

	if (!no_title) {
		x = draw_text(bar->title, x, y, foreground, background,
			      bar->sel ? &title_fg_color : &inactive_fg_color,
			      bar->sel ? &title_bg_color : &inactive_bg_color,
			      bar->width - status_width, bar->height, bar->textpadding,
			      false);
	}

	pixman_image_fill_boxes(PIXMAN_OP_SRC, background,
				bar->sel ? &title_bg_color : &title_bg_color, 1,
				&(pixman_box32_t){
					.x1 = x, .x2 = bar->width - status_width,
					.y1 = 0, .y2 = bar->height
				});

	/* Draw background and foreground on bar */
	pixman_image_composite32(PIXMAN_OP_OVER, background, NULL, final, 0, 0, 0, 0, 0, 0, bar->width, bar->height);
	pixman_image_composite32(PIXMAN_OP_OVER, foreground, NULL, final, 0, 0, 0, 0, 0, 0, bar->width, bar->height);

	pixman_image_unref(foreground);
	pixman_image_unref(background);
	pixman_image_unref(final);
	
	munmap(data, bar->bufsize);

	wl_surface_set_buffer_scale(bar->wl_surface, buffer_scale);
	wl_surface_attach(bar->wl_surface, buffer, 0, 0);
	wl_surface_damage_buffer(bar->wl_surface, 0, 0, bar->width, bar->height);
	wl_surface_commit(bar->wl_surface);

	return 0;
}

/* Layer-surface setup adapted from layer-shell example in [wlroots] */
static void
layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
			uint32_t serial, uint32_t w, uint32_t h)
{
	zwlr_layer_surface_v1_ack_configure(surface, serial);
	
	Bar *bar = (Bar *)data;
	
	w *= buffer_scale;
	h *= buffer_scale;

	if (bar->configured && w == bar->width && h == bar->height)
		return;
	
	bar->width = w;
	bar->height = h;
	bar->stride = bar->width * 4;
	bar->bufsize = bar->stride * bar->height;
	bar->configured = true;

	draw_frame(bar);
}

static void
layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface)
{
	run_display = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

static void
pointer_enter(void *data, struct wl_pointer *pointer,
	      uint32_t serial, struct wl_surface *surface,
	      wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	Seat *seat = (Seat *)data;

	seat->hovering = true;
	
	if (!cursor_image) {
		struct wl_cursor_theme *cursor_theme = wl_cursor_theme_load(NULL, 24 * buffer_scale, shm);
		cursor_image = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr")->images[0];
		cursor_surface = wl_compositor_create_surface(compositor);
		wl_surface_set_buffer_scale(cursor_surface, buffer_scale);
		wl_surface_attach(cursor_surface, wl_cursor_image_get_buffer(cursor_image), 0, 0);
		wl_surface_commit(cursor_surface);
	}
	wl_pointer_set_cursor(pointer, serial, cursor_surface,
			      cursor_image->hotspot_x,
			      cursor_image->hotspot_y);
}

static void
pointer_leave(void *data, struct wl_pointer *pointer,
	      uint32_t serial, struct wl_surface *surface)
{
	Seat *seat = (Seat *)data;
	
	seat->hovering = false;
}

static void
pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
	       uint32_t time, uint32_t button, uint32_t state)
{
	Seat *seat = (Seat *)data;

	seat->pointer_button = state == WL_POINTER_BUTTON_STATE_PRESSED ? button : 0;
}

static void
pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
	       wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	Seat *seat = (Seat *)data;

	seat->pointer_x = wl_fixed_to_int(surface_x);
	seat->pointer_y = wl_fixed_to_int(surface_y);
}

static void
pointer_frame(void *data, struct wl_pointer *pointer)
{
	Seat *seat = (Seat *)data;

	if (!seat->pointer_button || !seat->bar || !seat->hovering)
		return;

	uint32_t button = seat->pointer_button;
	seat->pointer_button = 0;
	
	uint32_t i = 0, x = 0;
	do {
		if (hide_vacant) {
			const bool active = seat->bar->mtags & 1 << i;
			const bool occupied = seat->bar->ctags & 1 << i;
			const bool urgent = seat->bar->urg & 1 << i;
			if (!active && !occupied && !urgent)
				continue;
		}
		x += TEXT_WIDTH(tags[i], seat->bar->width - x, seat->bar->textpadding, false) / buffer_scale;
	} while (seat->pointer_x >= x && ++i < tags_l);
	if (i < tags_l) {
		/* Clicked on tags */
		char *cmd;
		if (button == BTN_LEFT)
			cmd = "set-focused-tags";
		else if (button == BTN_MIDDLE)
			cmd = "toggle-focused-tags";
		else if (button == BTN_RIGHT)
			cmd = "set-view-tags";
		else
			return;

		zriver_control_v1_add_argument(river_control, cmd);
		char buf[32];
		snprintf(buf, sizeof(buf), "%d", 1 << i);
		zriver_control_v1_add_argument(river_control, buf);
		zriver_control_v1_run_command(river_control, seat->wl_seat);
		return;
	}
	
	Seat *it;
	wl_list_for_each(it, &seat_list, link) {
		x += TEXT_WIDTH(it->mode, seat->bar->width - x, seat->bar->textpadding, false) / buffer_scale;
		if (seat->pointer_x < x) {
			/* clicked on mode */
			char *mode;
			if (button == BTN_LEFT)
				mode = "normal";
			else if (button == BTN_RIGHT)
				mode = "passthrough";
			else
				return;
			zriver_control_v1_add_argument(river_control, "enter-mode");
			zriver_control_v1_add_argument(river_control, mode);
			zriver_control_v1_run_command(river_control, it->wl_seat);
			return;
		}
	}

	// TODO: run custom commands upon clicking layout, title, status
	if (seat->bar->mtags & seat->bar->ctags) {
		x += TEXT_WIDTH(seat->bar->layout, seat->bar->width - x, seat->bar->textpadding, false) / buffer_scale;
		if (seat->pointer_x < x) {
			/* clicked on layout */
			return;
		}
	}
	
	if (seat->pointer_x < seat->bar->width / buffer_scale - TEXT_WIDTH(seat->bar->status, seat->bar->width - x, seat->bar->textpadding, true) / buffer_scale) {
		/* clicked on title */
		return;
	}
	
	/* clicked on status */
}

static void
pointer_axis(void *data, struct wl_pointer *pointer,
	     uint32_t time, uint32_t axis, wl_fixed_t value)
{
}

static void
pointer_axis_discrete(void *data, struct wl_pointer *pointer,
		      uint32_t axis, int32_t discrete)
{
}

static void
pointer_axis_source(void *data, struct wl_pointer *pointer,
		    uint32_t axis_source)
{
}

static void
pointer_axis_stop(void *data, struct wl_pointer *pointer,
		  uint32_t time, uint32_t axis)
{
}

static void
pointer_axis_value120(void *data, struct wl_pointer *pointer,
		      uint32_t axis, int32_t discrete)
{
}

static const struct wl_pointer_listener pointer_listener = {
	.axis = pointer_axis,
	.axis_discrete = pointer_axis_discrete,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_value120 = pointer_axis_value120,
	.button = pointer_button,
	.enter = pointer_enter,
	.frame = pointer_frame,
	.leave = pointer_leave,
	.motion = pointer_motion,
};

static void
output_description(void *data, struct wl_output *wl_output,
	const char *description)
{
}

static void
output_done(void *data, struct wl_output *wl_output)
{
}

static void
output_geometry(void *data, struct wl_output *wl_output,
	int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
	int32_t subpixel, const char *make, const char *model,
	int32_t transform)
{
}

static void
output_mode(void *data, struct wl_output *wl_output,
	uint32_t flags, int32_t width, int32_t height,
	int32_t refresh)
{
}

static void
output_name(void *data, struct wl_output *wl_output,
	const char *name)
{
	Bar *bar = (Bar *)data;

	if (bar->output_name)
		free(bar->output_name);
	if (!(bar->output_name = strdup(name)))
		EDIE("strdup");
}

static void
output_scale(void *data, struct wl_output *wl_output,
	int32_t factor)
{
}

static const struct wl_output_listener output_listener = {
	.description = output_description,
	.done = output_done,
	.geometry = output_geometry,
	.mode = output_mode,
	.name = output_name,
	.scale = output_scale,
};

static void
seat_capabilities(void *data, struct wl_seat *wl_seat,
		  uint32_t capabilities)
{
	Seat *seat = (Seat *)data;
	
	const uint32_t has_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
	if (has_pointer && !seat->wl_pointer) {
		seat->wl_pointer = wl_seat_get_pointer(seat->wl_seat);
		wl_pointer_add_listener(seat->wl_pointer, &pointer_listener, seat);
	} else if (!has_pointer && seat->wl_pointer) {
		wl_pointer_destroy(seat->wl_pointer);
		seat->wl_pointer = NULL;
	}
}

static void
seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

static void
river_output_status_focused_tags(void *data, struct zriver_output_status_v1 *output_status,
				 uint32_t tags)
{
	Bar *bar = (Bar *)data;

	bar->mtags = tags;
	bar->redraw = true;
}

static void
river_output_status_urgent_tags(void *data, struct zriver_output_status_v1 *output_status,
				uint32_t tags)
{
	Bar *bar = (Bar *)data;

	bar->urg = tags;
	bar->redraw = true;
}

static void
river_output_status_view_tags(void *data, struct zriver_output_status_v1 *output_status,
			      struct wl_array *wl_array)
{
	Bar *bar = (Bar *)data;

	bar->ctags = 0;

	uint32_t *it;
	wl_array_for_each(it, wl_array)
		bar->ctags |= *it;
	bar->redraw = true;
}

static void
river_output_status_layout_name(void *data, struct zriver_output_status_v1 *output_status,
				const char *name)
{
	Bar *bar = (Bar *)data;

	if (bar->layout)
		free(bar->layout);
	if (!(bar->layout = strdup(name)))
		EDIE("strdup");
	bar->redraw = true;
}

static void
river_output_status_layout_name_clear(void *data, struct zriver_output_status_v1 *output_status)
{
	Bar *bar = (Bar *)data;

	if (bar->layout) {
		free(bar->layout);
		bar->layout = NULL;
	}
}

static const struct zriver_output_status_v1_listener river_output_status_listener = {
	.focused_tags = river_output_status_focused_tags,
	.urgent_tags = river_output_status_urgent_tags,
	.view_tags = river_output_status_view_tags,
	.layout_name = river_output_status_layout_name,
	.layout_name_clear = river_output_status_layout_name_clear
};

static void
river_seat_status_focused_output(void *data, struct zriver_seat_status_v1 *seat_status,
				 struct wl_output *wl_output)
{
	Seat *seat = (Seat *)data;

	Bar *bar;
	wl_list_for_each(bar, &bar_list, link) {
		if (bar->wl_output == wl_output) {
			seat->bar = bar;
			seat->bar->sel = true;
			seat->bar->redraw = true;
			return;
		}
	}
	seat->bar = NULL;
}

static void
river_seat_status_unfocused_output(void *data, struct zriver_seat_status_v1 *seat_status,
				   struct wl_output *wl_output)
{
	Seat *seat = (Seat *)data;

	if (seat->bar) {
		seat->bar->sel = false;
		seat->bar->redraw = true;
		seat->bar = NULL;
	}
}

static void
river_seat_status_focused_view(void *data, struct zriver_seat_status_v1 *seat_status,
			       const char *title)
{
	if (no_title)
		return;
	
	Seat *seat = (Seat *)data;

	if (!seat->bar)
		return;
	if (seat->bar->title)
		free(seat->bar->title);
	if (!(seat->bar->title = strdup(title)))
		EDIE("strdup");
	seat->bar->redraw = true;
}

static void
river_seat_status_mode(void *data, struct zriver_seat_status_v1 *seat_status,
		       const char *name)
{
	Seat *seat = (Seat *)data;

	if (seat->mode)
		free(seat->mode);
	if (!(seat->mode = strdup(name)))
		EDIE("strdup");
	
	Bar *bar;
	wl_list_for_each(bar, &bar_list, link)
		bar->redraw = true;
}

static const struct zriver_seat_status_v1_listener river_seat_status_listener = {
	.focused_output = river_seat_status_focused_output,
	.unfocused_output = river_seat_status_unfocused_output,
	.focused_view = river_seat_status_focused_view,
	.mode = river_seat_status_mode,
};

static void
show_bar(Bar *bar)
{
	bar->wl_surface = wl_compositor_create_surface(compositor);
	if (!bar->wl_surface)
		DIE("Could not create wl_surface");

	bar->layer_surface = zwlr_layer_shell_v1_get_layer_surface(layer_shell, bar->wl_surface, bar->wl_output,
								   ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, PROGRAM);
	if (!bar->layer_surface)
		DIE("Could not create layer_surface");
	zwlr_layer_surface_v1_add_listener(bar->layer_surface, &layer_surface_listener, bar);

	zwlr_layer_surface_v1_set_size(bar->layer_surface, 0, bar->height / buffer_scale);
	zwlr_layer_surface_v1_set_anchor(bar->layer_surface,
					 (bar->bottom ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)
					 | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
					 | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(bar->layer_surface, bar->height / buffer_scale);
	wl_surface_commit(bar->wl_surface);

	bar->hidden = false;
}

static void
hide_bar(Bar *bar)
{
	zwlr_layer_surface_v1_destroy(bar->layer_surface);
	wl_surface_destroy(bar->wl_surface);

	bar->configured = false;
	bar->hidden = true;
}

static void
setup_bar(Bar *bar)
{
	bar->height = height * buffer_scale;
	bar->textpadding = textpadding;
	bar->bottom = bottom;
	bar->hidden = hidden;

	if (!(bar->river_output_status = zriver_status_manager_v1_get_river_output_status(river_status_manager, bar->wl_output)))
		DIE("Could not create river_output_status");
	zriver_output_status_v1_add_listener(bar->river_output_status, &river_output_status_listener, bar);

	if (!bar->hidden)
		show_bar(bar);
}

static void
setup_seat(Seat *seat)
{
	if (!(seat->river_seat_status = zriver_status_manager_v1_get_river_seat_status(river_status_manager, seat->wl_seat)))
		DIE("Could not create river_seat_status");
	zriver_seat_status_v1_add_listener(seat->river_seat_status, &river_seat_status_listener, seat);
}

static void
handle_global(void *data, struct wl_registry *registry,
	      uint32_t name, const char *interface, uint32_t version)
{
	if (!strcmp(interface, wl_compositor_interface.name)) {
		compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	} else if (!strcmp(interface, wl_shm_interface.name)) {
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
		layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
	} else if (!strcmp(interface, zriver_status_manager_v1_interface.name)) {
		river_status_manager = wl_registry_bind(registry, name, &zriver_status_manager_v1_interface, 4);
	} else if (!strcmp(interface, zriver_control_v1_interface.name)) {
		river_control = wl_registry_bind(registry, name, &zriver_control_v1_interface, 1);
	} else if (!strcmp(interface, wl_output_interface.name)) {
		Bar *bar = calloc(1, sizeof(Bar));
		if (!bar)
			EDIE("calloc");
		bar->registry_name = name;
		bar->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 4);
		wl_output_add_listener(bar->wl_output, &output_listener, bar);
		if (run_display)
			setup_bar(bar);
		wl_list_insert(&bar_list, &bar->link);
	} else if (!strcmp(interface, wl_seat_interface.name)) {
		Seat *seat = calloc(1, sizeof(Seat));
		if (!seat)
			EDIE("calloc");
		seat->registry_name = name;
		seat->wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
		wl_seat_add_listener(seat->wl_seat, &seat_listener, seat);
		if (run_display)
			setup_seat(seat);
		wl_list_insert(&seat_list, &seat->link);
	}
}

static void
teardown_bar(Bar *bar)
{
	if (bar->title)
		free(bar->title);
	if (bar->layout)
		free(bar->layout);
	if (bar->status)
		free(bar->status);
	if (bar->output_name)
		free(bar->output_name);
	zriver_output_status_v1_destroy(bar->river_output_status);
	if (!bar->hidden) {
		zwlr_layer_surface_v1_destroy(bar->layer_surface);
		wl_surface_destroy(bar->wl_surface);
	}
	wl_output_destroy(bar->wl_output);
	free(bar);
}

static void
teardown_seat(Seat *seat)
{
	if (seat->mode)
		free(seat->mode);
	zriver_seat_status_v1_destroy(seat->river_seat_status);
	if (seat->wl_pointer)
		wl_pointer_destroy(seat->wl_pointer);
	wl_seat_destroy(seat->wl_seat);
	free(seat);
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	Bar *bar;
	Seat *seat;
	
	wl_list_for_each(bar, &bar_list, link) {
		if (bar->registry_name == name) {
			wl_list_remove(&bar->link);
			teardown_bar(bar);
			return;
		}
	}
	wl_list_for_each(seat, &seat_list, link) {
		if (seat->registry_name == name) {
			wl_list_remove(&seat->link);
			teardown_seat(seat);
			return;
		}
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove
};

static void
set_status(Bar *bar, char *data)
{
	if (bar->status)
		free(bar->status);
	if (!(bar->status = strdup(data)))
		EDIE("strdup");
	bar->redraw = true;
}

static void
set_visible(Bar *bar, char *data)
{
	if (bar->hidden)
		show_bar(bar);
}

static void
set_invisible(Bar *bar, char *data)
{
	if (!bar->hidden)
		hide_bar(bar);
}

static void
toggle_visibility(Bar *bar, char *data)
{
	if (bar->hidden)
		show_bar(bar);
	else
		hide_bar(bar);
}

static void
set_top(Bar *bar, char *data)
{
	if (!bar->hidden) {
		zwlr_layer_surface_v1_set_anchor(bar->layer_surface,
						 ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
						 | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
						 | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
		bar->redraw = true;
	}
	bar->bottom = false;
}

static void
set_bottom(Bar *bar, char *data)
{
	if (!bar->hidden) {
		zwlr_layer_surface_v1_set_anchor(bar->layer_surface,
						 ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
						 | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
						 | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
		bar->redraw = true;
	}
	bar->bottom = true;
}

static void
toggle_location(Bar *bar, char *data)
{
	if (bar->bottom)
		set_top(bar, NULL);
	else
		set_bottom(bar, NULL);
}

static int
advance_word(char **beg, char **end)
{
	for (*beg = *end; **beg == ' '; (*beg)++);
	for (*end = *beg; **end && **end != ' '; (*end)++);
	if (!**end)
		/* last word */
		return -1;
	**end = '\0';
	(*end)++;
	return 0;
}

static int
read_stdin(void)
{
	char buf[8192];
	ssize_t len = read(STDIN_FILENO, buf, sizeof(buf));
	if (len == -1)
		EDIE("read");
	if (len == 0)
		return -1;
	
	char *linebeg, *lineend, *wordbeg, *wordend;
	for (linebeg = (char *)&buf;
	     (lineend = memchr(linebeg, '\n', (char *)&buf + len - linebeg));
	     linebeg = lineend) {
		*lineend++ = '\0';

		wordend = linebeg;
		if (advance_word(&wordbeg, &wordend) == -1)
			continue;
		char *output = wordbeg;
		advance_word(&wordbeg, &wordend);

		void (*func)(Bar *, char *);
		if (!strcmp(wordbeg, "status")) {
			if (!*wordend)
				continue;
			func = set_status;
		} else if (!strcmp(wordbeg, "show")) {
			func = set_visible;
		} else if (!strcmp(wordbeg, "hide")) {
			func = set_invisible;
		} else if (!strcmp(wordbeg, "toggle-visibility")) {
			func = toggle_visibility;
		} else if (!strcmp(wordbeg, "set-top")) {
			func = set_top;
		} else if (!strcmp(wordbeg, "set-bottom")) {
			func = set_bottom;
		} else if (!strcmp(wordbeg, "toggle-location")) {
			func = toggle_location;
		} else {
			continue;
		}
		
		Bar *bar;
		if (!strcmp(output, "all")) {
			wl_list_for_each(bar, &bar_list, link)
				func(bar, wordend);
		} else if (!strcmp(output, "selected")) {
			wl_list_for_each(bar, &bar_list, link)
				if (bar->sel)
					func(bar, wordend);
		} else {
			wl_list_for_each(bar, &bar_list, link) {
				if (bar->output_name && !strcmp(output, bar->output_name)) {
					func(bar, wordend);
					break;
				}
			}
		}
	}
	
	return 0;
}

static void
event_loop(void)
{
	int wl_fd = wl_display_get_fd(display);

	while (run_display) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(wl_fd, &rfds);
		FD_SET(STDIN_FILENO, &rfds);

		wl_display_flush(display);

		if (select(wl_fd + 1, &rfds, NULL, NULL, NULL) == -1) {
			if (errno == EINTR)
				continue;
			else
				EDIE("select");
		}
		
		if (FD_ISSET(wl_fd, &rfds))
			if (wl_display_dispatch(display) == -1)
				break;
		if (FD_ISSET(STDIN_FILENO, &rfds))
			if (read_stdin() == -1)
				break;
		
		Bar *bar;
		wl_list_for_each(bar, &bar_list, link) {
			if (bar->redraw) {
				if (!bar->hidden)
					draw_frame(bar);
				bar->redraw = false;
			}
		}
	}
}

void
sig_handler(int sig)
{
	if (sig == SIGINT || sig == SIGHUP || sig == SIGTERM)
		run_display = false;
}

int
main(int argc, char **argv)
{
	Bar *bar, *bar2;
	Seat *seat, *seat2;

	/* Parse options */
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-hide-vacant-tags")) {
			hide_vacant = true;
		} else if (!strcmp(argv[i], "-bottom")) {
			bottom = true;
		} else if (!strcmp(argv[i], "-hidden")) {
			hidden = true;
		} else if (!strcmp(argv[i], "-no-title")) {
			no_title = true;
		} else if (!strcmp(argv[i], "-no-status-commands")) {
			no_status_commands = true;
		} else if (!strcmp(argv[i], "-no-mode")) {
			no_mode = true;
		} else if (!strcmp(argv[i], "-no-layout")) {
			no_layout = true;
		} else if (!strcmp(argv[i], "-font")) {
			if (++i >= argc)
				DIE("Option -font requires an argument");
			fontstr = argv[i];
		} else if (!strcmp(argv[i], "-vertical-padding")) {
			if (++i >= argc)
				DIE("Option -vertical-padding requires an argument");
			vertical_padding = MAX(MIN(atoi(argv[i]), 100), 0);
		} else if (!strcmp(argv[i], "-scale")) {
			if (++i >= argc)
				DIE("Option -scale requires an argument");
			buffer_scale = strtoul(argv[i], &argv[i] + strlen(argv[i]), 10);
		} else if (!strcmp(argv[i], "-active-fg-color")) {
			if (++i >= argc)
				DIE("Option -active-fg-color requires an argument");
			if (parse_color(argv[i], &active_fg_color) == -1)
				DIE("malformed color string");
		} else if (!strcmp(argv[i], "-active-bg-color")) {
			if (++i >= argc)
				DIE("Option -active-bg-color requires an argument");
			if (parse_color(argv[i], &active_bg_color) == -1)
				DIE("malformed color string");
		} else if (!strcmp(argv[i], "-inactive-fg-color")) {
			if (++i >= argc)
				DIE("Option -inactive-fg-color requires an argument");
			if (parse_color(argv[i], &inactive_fg_color) == -1)
				DIE("malformed color string");
		} else if (!strcmp(argv[i], "-inactive-bg-color")) {
			if (++i >= argc)
				DIE("Option -inactive-bg-color requires an argument");
			if (parse_color(argv[i], &inactive_bg_color) == -1)
				DIE("malformed color string");
		} else if (!strcmp(argv[i], "-urgent-fg-color")) {
			if (++i >= argc)
				DIE("Option -urgent-fg-color requires an argument");
			if (parse_color(argv[i], &urgent_fg_color) == -1)
				DIE("malformed color string");
		} else if (!strcmp(argv[i], "-urgent-bg-color")) {
			if (++i >= argc)
				DIE("Option -urgent-bg-color requires an argument");
			if (parse_color(argv[i], &urgent_bg_color) == -1)
				DIE("malformed color string");
		} else if (!strcmp(argv[i], "-title-fg-color")) {
			if (++i >= argc)
				DIE("Option -title-fg-color requires an argument");
			if (parse_color(argv[i], &title_fg_color) == -1)
				DIE("malformed color string");
		} else if (!strcmp(argv[i], "-title-bg-color")) {
			if (++i >= argc)
				DIE("Option -title-bg-color requires an argument");
			if (parse_color(argv[i], &title_bg_color) == -1)
				DIE("malformed color string");
		} else if (!strcmp(argv[i], "-tags")) {
			if (++i + 1 >= argc)
				DIE("Option -tags requires at least two arguments");
			int v;
			if ((v = atoi(argv[i])) <= 0 || i + v >= argc)
				DIE("-tags: invalid arguments");
			if (tags) {
				for (uint32_t j = 0; j < tags_l; j++)
					free(tags[j]);
				free(tags);
			}
			if (!(tags = malloc(v * sizeof(char *))))
				EDIE("malloc");
			for (int j = 0; j < v; j++)
				if (!(tags[j] = strdup(argv[i + 1 + j])))
					EDIE("strdup");
			tags_l = v;
			i += v;
		} else if (!strcmp(argv[i], "-v")) {
			fprintf(stderr, PROGRAM " " VERSION "\n");
			return 0;
		} else if (!strcmp(argv[i], "-h")) {
			fprintf(stderr, USAGE);
			return 0;
		} else {
			DIE("Option '%s' not recognized\n" USAGE, argv[i]);
		}
	}

	/* Set up display and protocols */
	if (!(display = wl_display_connect(NULL)))
		DIE("Failed to create display");

	wl_list_init(&bar_list);
	wl_list_init(&seat_list);
	
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!compositor || !shm || !layer_shell || !river_status_manager || !river_control)
		DIE("Compositor does not support all needed protocols");

	/* Load selected font */
	fcft_init(FCFT_LOG_COLORIZE_AUTO, 0, FCFT_LOG_CLASS_ERROR);
	fcft_set_scaling_filter(FCFT_SCALING_FILTER_LANCZOS3);

	unsigned int dpi = 96 * buffer_scale;
	char buf[10];
	snprintf(buf, sizeof buf, "dpi=%u", dpi);
	if (!(font = fcft_from_name(1, (const char *[]) {fontstr}, buf)))
		DIE("Could not load font");
	textpadding = font->height / 2;
	height = font->height / buffer_scale + vertical_padding * 2;

	/* Configure tag names */
	if (!tags) {
		tags_l = 9;
		if (!(tags = malloc(tags_l * sizeof(char *))))
			EDIE("malloc");
		char buf[32];
		for (uint32_t i = 0; i < tags_l; i++) {
			snprintf(buf, sizeof(buf), "%d", i + 1);
			if (!(tags[i] = strdup(buf)))
				EDIE("strdup");
		}
	}
	
	/* Setup bars and seats */
	wl_list_for_each(bar, &bar_list, link)
		setup_bar(bar);
	wl_list_for_each(seat, &seat_list, link)
		setup_seat(seat);
	wl_display_roundtrip(display);

	/* Configure stdin */
	if (fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK) == -1)
		EDIE("fcntl");

	/* Set up signals */
	signal(SIGINT, sig_handler);
	signal(SIGHUP, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGCHLD, SIG_IGN);
	
	/* Run */
	run_display = true;
	event_loop();

	/* Clean everything up */
	if (tags) {
		for (uint32_t i = 0; i < tags_l; i++)
			free(tags[i]);
		free(tags);
	}

	wl_list_for_each_safe(bar, bar2, &bar_list, link)
		teardown_bar(bar);
	wl_list_for_each_safe(seat, seat2, &seat_list, link)
		teardown_seat(seat);
	
	zriver_control_v1_destroy(river_control);
	zriver_status_manager_v1_destroy(river_status_manager);
	zwlr_layer_shell_v1_destroy(layer_shell);
	
	fcft_destroy(font);
	fcft_fini();
	
	wl_shm_destroy(shm);
	wl_compositor_destroy(compositor);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);

	return 0;
}
