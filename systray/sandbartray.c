#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>

#include "snhost.h"

typedef struct args_parsed {
	char cssdata[64];
	int barheight;
	int position;
} args_parsed;

static int margin = 0;
static int spacing = 0;

enum {
	SANDBAR_POSITION_TOP,
	SANDBAR_POSITION_BOTTOM
};

static void
activate(GtkApplication* app, void *data)
{
	args_parsed *args = (args_parsed*)data;

	GdkDisplay *display = gdk_display_get_default();

	int iconsize, win_default_width, win_default_height;

	iconsize = args->barheight - 2 * margin;
	win_default_width = args->barheight;
	win_default_height = args->barheight;

	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, args->cssdata);
	gtk_style_context_add_provider_for_display(display,
	                                           GTK_STYLE_PROVIDER(css),
	                                           GTK_STYLE_PROVIDER_PRIORITY_USER);
	g_object_unref(css);

	gboolean anchors[4];

	switch (args->position) {
		case SANDBAR_POSITION_TOP:
			anchors[GTK_LAYER_SHELL_EDGE_LEFT]   = FALSE;
			anchors[GTK_LAYER_SHELL_EDGE_RIGHT]  = TRUE;
			anchors[GTK_LAYER_SHELL_EDGE_TOP]    = TRUE;
			anchors[GTK_LAYER_SHELL_EDGE_BOTTOM] = FALSE;
			break;
		case SANDBAR_POSITION_BOTTOM:
			anchors[GTK_LAYER_SHELL_EDGE_LEFT]   = FALSE;
			anchors[GTK_LAYER_SHELL_EDGE_RIGHT]  = TRUE;
			anchors[GTK_LAYER_SHELL_EDGE_TOP]    = FALSE;
			anchors[GTK_LAYER_SHELL_EDGE_BOTTOM] = TRUE;
			break;
		default:
			g_assert_not_reached();
			break;
	}

	GListModel *mons = gdk_display_get_monitors(display);

	// Create tray for each monitor
	unsigned int i;
	for (i = 0; i < g_list_model_get_n_items(mons); i++) {
		GdkMonitor *mon = g_list_model_get_item(mons, i);
		const char *conn = gdk_monitor_get_connector(mon);

		SnHost *host = sn_host_new(win_default_width,
		                           win_default_height,
		                           iconsize,
		                           margin,
		                           spacing,
		                           conn);

		GtkWindow *window = GTK_WINDOW(host);

		gtk_window_set_application(window, app);

		gtk_layer_init_for_window(window);
		gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_BOTTOM);
		gtk_layer_set_exclusive_zone(window, -1);

		gtk_layer_set_monitor(window, mon);

		for (int j = 0; j < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; j++) {
			gtk_layer_set_anchor(window, j, anchors[j]);
		}

		gtk_window_present(window);
	}
}

static void
terminate_app_helper(void *data, void *udata)
{
	GtkWindow *window = GTK_WINDOW(data);

	gtk_window_close(window);
}

static gboolean
terminate_app(GtkApplication *app)
{
	GList *windows = gtk_application_get_windows(app);
	g_list_foreach(windows, terminate_app_helper, NULL);

    return G_SOURCE_REMOVE;
}

int
main(int argc, char *argv[])
{
	const char cssskele[] = "window{background-color:%s;}";

	args_parsed args;
	args.barheight = 22;
	args.position = SANDBAR_POSITION_TOP;
	snprintf(args.cssdata, sizeof(args.cssdata), cssskele, "#222222");

	int option;
	while ((option = getopt(argc, argv, "bc:s:")) != -1) {
		switch (option) {
			case 'b':    // "bottom"
				args.position = SANDBAR_POSITION_BOTTOM;
				break;
			case 'c':    // "color"
				snprintf(args.cssdata, sizeof(args.cssdata), cssskele, optarg);
				break;
			case 's':    // "size"
				args.barheight = strtol(optarg, NULL, 10);
				break;
		}
	}

	GtkApplication *app = gtk_application_new("org.sandbar.sandbartray",
	                                          G_APPLICATION_DEFAULT_FLAGS);

	g_signal_connect(app, "activate", G_CALLBACK(activate), &args);

	g_unix_signal_add(SIGINT, (GSourceFunc)terminate_app, app);
	g_unix_signal_add(SIGTERM, (GSourceFunc)terminate_app, app);

	char *argv_inner[] = { argv[0], NULL };
	int status = g_application_run(G_APPLICATION(app), 1, argv_inner);

	g_object_unref(app);

	return status;
}
