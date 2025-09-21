#include "snhost.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "snwatcher.h"
#include "snitem.h"


struct _SnHost
{
	GtkWidget parent_instance;

	GtkWidget* box;
	SnWatcher* watcher;
	GHashTable* snitems;
	char* mon;

	unsigned long reg_sub_id;
	unsigned long unreg_sub_id;

	int defaultwidth;
	int defaultheight;
	int iconsize;
	int margins;
	int spacing;

	int nitems;
	int curwidth;
	gboolean exiting;
};

G_DEFINE_FINAL_TYPE(SnHost, sn_host, GTK_TYPE_WINDOW)

enum
{
	PROP_MON = 1,
	PROP_DEFAULTWIDTH,
	PROP_DEFAULTHEIGHT,
	PROP_ICONSIZE,
	PROP_MARGINS,
	PROP_SPACING,
	N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };


static void	sn_host_constructed	(GObject *obj);
static void	sn_host_dispose		(GObject *obj);
static void	sn_host_finalize	(GObject *obj);


static void
sandbar_request_resize(SnHost *self)
{
	if (self->exiting) {
		// Restore original size on exit.
		self->curwidth = 0;
	} else if (self->nitems <= 1) {
		// Width of 1 icon even when there are none.
		self->curwidth = self->iconsize + 2 * self->margins;
	} else {
		self->curwidth =
			// Icons themselves.
			self->nitems * self->iconsize +

			// Spacing between icons.
			(self->nitems - 1) * self->spacing +

			// Margins before first icon and after last icon.
			2 * self->margins;
	}

	// Print the resize information to standard output.
	// The parent process can read this from its stdin.
	printf("%s %s %i\n", self->mon, "resize", self->curwidth);
	fflush(stdout);
}

static void
sn_host_register_item(SnWatcher *watcher,
                      const char *busname,
                      const char *busobj,
                      SnHost *self)
{
	g_debug("Adding %s to snhost %s", busname, self->mon);

	SnItem *snitem = sn_item_new(busname, busobj, self->iconsize);
	gtk_box_append(GTK_BOX(self->box), GTK_WIDGET(snitem));
	g_hash_table_insert(self->snitems, g_strdup(busname), snitem);

	self->nitems = self->nitems + 1;
	sandbar_request_resize(self);
}

static void
sn_host_unregister_item(SnWatcher *watcher, const char *busname, SnHost *self)
{
	g_debug("Removing %s from snhost %s", busname, self->mon);

	GtkBox *box = GTK_BOX(self->box);
	void *match = g_hash_table_lookup(self->snitems, busname);
	GtkWidget *snitem = GTK_WIDGET(match);

	gtk_box_remove(box, snitem);
	g_hash_table_remove(self->snitems, busname);

	self->nitems = self->nitems - 1;
	sandbar_request_resize(self);
}

static void
sn_host_set_property(GObject *object,
                     unsigned int property_id,
                     const GValue *value,
                     GParamSpec *pspec)
{
	SnHost *self = SN_HOST(object);

	switch (property_id) {
		case PROP_MON:
			self->mon = g_value_dup_string(value);
			break;
		case PROP_DEFAULTWIDTH:
			self->defaultwidth = g_value_get_int(value);
			break;
		case PROP_DEFAULTHEIGHT:
			self->defaultheight = g_value_get_int(value);
			break;
		case PROP_ICONSIZE:
			self->iconsize = g_value_get_int(value);
			break;
		case PROP_MARGINS:
			self->margins = g_value_get_int(value);
			break;
		case PROP_SPACING:
			self->spacing = g_value_get_int(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void
sn_host_class_init(SnHostClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->set_property = sn_host_set_property;
	object_class->constructed = sn_host_constructed;
	object_class->dispose = sn_host_dispose;
	object_class->finalize = sn_host_finalize;

	obj_properties[PROP_MON] =
		g_param_spec_string("mon", NULL, NULL,
		                 NULL,
		                 G_PARAM_CONSTRUCT_ONLY |
		                 G_PARAM_WRITABLE |
		                 G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_DEFAULTWIDTH] =
		g_param_spec_int("defaultwidth", NULL, NULL,
		                 INT_MIN,
		                 INT_MAX,
		                 22,
		                 G_PARAM_CONSTRUCT_ONLY |
		                 G_PARAM_WRITABLE |
		                 G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_DEFAULTHEIGHT] =
		g_param_spec_int("defaultheight", NULL, NULL,
		                 INT_MIN,
		                 INT_MAX,
		                 22,
		                 G_PARAM_CONSTRUCT_ONLY |
		                 G_PARAM_WRITABLE |
		                 G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_ICONSIZE] =
		g_param_spec_int("iconsize", NULL, NULL,
		                 INT_MIN,
		                 INT_MAX,
		                 22,
		                 G_PARAM_CONSTRUCT_ONLY |
		                 G_PARAM_WRITABLE |
		                 G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_MARGINS] =
		g_param_spec_int("margins", NULL, NULL,
		                 INT_MIN,
		                 INT_MAX,
		                 4,
		                 G_PARAM_CONSTRUCT_ONLY |
		                 G_PARAM_WRITABLE |
		                 G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_SPACING] =
		g_param_spec_int("spacing", NULL, NULL,
		                 INT_MIN,
		                 INT_MAX,
		                 4,
		                 G_PARAM_CONSTRUCT_ONLY |
		                 G_PARAM_WRITABLE |
		                 G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties(object_class, N_PROPERTIES, obj_properties);
}

static void
sn_host_init(SnHost *self)
{
	self->snitems = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	self->exiting = FALSE;
	self->nitems = 0;

	self->watcher = sn_watcher_new();

	self->reg_sub_id = g_signal_connect(self->watcher,
	                                    "trayitem-registered",
	                                    G_CALLBACK(sn_host_register_item),
	                                    self);

	self->unreg_sub_id = g_signal_connect(self->watcher,
	                                      "trayitem-unregistered",
	                                      G_CALLBACK(sn_host_unregister_item),
	                                      self);
}

static void
sn_host_constructed(GObject *obj)
{
	SnHost *self = SN_HOST(obj);

	GtkWindow *window = GTK_WINDOW(self);
	gtk_window_set_decorated(window, FALSE);
	gtk_window_set_default_size(window, self->defaultwidth, self->defaultheight);

	self->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, self->spacing);
	gtk_box_set_homogeneous(GTK_BOX(self->box), TRUE);
	gtk_box_set_spacing(GTK_BOX(self->box), self->margins);

	GtkWidget *widget = GTK_WIDGET(self->box);
	gtk_widget_set_vexpand(widget, TRUE);
	gtk_widget_set_hexpand(widget, TRUE);
	gtk_widget_set_margin_start(widget, self->margins);
	gtk_widget_set_margin_end(widget, self->margins);
	gtk_widget_set_margin_top(widget, self->margins);
	gtk_widget_set_margin_bottom(widget, self->margins);

	gtk_window_set_child(GTK_WINDOW(self), self->box);

	sandbar_request_resize(self);

	g_debug("Created snhost for monitor %s", self->mon);

	G_OBJECT_CLASS(sn_host_parent_class)->constructed(obj);
}

static void
sn_host_dispose(GObject *obj)
{
	SnHost *self = SN_HOST(obj);

	g_debug("Disposing snhost of monitor %s", self->mon);
	self->exiting = TRUE;

	if (self->reg_sub_id > 0) {
		g_signal_handler_disconnect(self->watcher, self->reg_sub_id);
		self->reg_sub_id = 0;
	}

	if (self->unreg_sub_id > 0) {
		g_signal_handler_disconnect(self->watcher, self->unreg_sub_id);
		self->reg_sub_id = 0;
	}

	if (self->watcher) {
		g_object_unref(self->watcher);
		self->watcher = NULL;
	}

	sandbar_request_resize(self);

	G_OBJECT_CLASS(sn_host_parent_class)->dispose(obj);
}

static void
sn_host_finalize(GObject *obj)
{
	SnHost *self = SN_HOST(obj);

	g_hash_table_destroy(self->snitems);
	g_free(self->mon);

	G_OBJECT_CLASS(sn_host_parent_class)->finalize(obj);
}

SnHost*
sn_host_new(int defaultwidth,
            int defaultheight,
            int iconsize,
            int margins,
            int spacing,
            const char *conn)
{
	return g_object_new(SN_TYPE_HOST,
	                    "defaultwidth", defaultwidth,
	                    "defaultheight", defaultheight,
	                    "iconsize", iconsize,
	                    "margins", margins,
	                    "spacing", spacing,
	                    "mon", conn,
	                    NULL);
}
