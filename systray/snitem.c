#include "snitem.h"

#include "sndbusmenu.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct _SnItem
{
	GtkWidget parent_instance;

	char* busname;
	char* busobj;

	GMenu* init_menu;
	GtkGesture* lclick;
	GtkGesture* rclick;
	GtkWidget* image;
	GtkWidget* popovermenu;

	GDBusProxy* proxy;
	GSList* cachedicons;
	GVariant* iconpixmap;
	SnDbusmenu* dbusmenu;
	char *iconpath;
	char* iconname;

	unsigned long lclick_id;
	unsigned long popup_id;
	unsigned long proxy_id;
	unsigned long rclick_id;

	int icon_source;
	int iconsize;
	int status;
	gboolean in_destruction;
	gboolean menu_visible;
};

G_DEFINE_FINAL_TYPE(SnItem, sn_item, GTK_TYPE_WIDGET)

enum
{
	PROP_BUSNAME = 1,
	PROP_BUSOBJ,
	PROP_ICONSIZE,
	PROP_DBUSMENU,
	PROP_MENUVISIBLE,
	N_PROPERTIES
};

enum
{
	RIGHTCLICK,
	LAST_SIGNAL
};

enum icon_sources
{
	ICON_SOURCE_UNKNOWN,
	ICON_SOURCE_NAME,
	ICON_SOURCE_PATH,
	ICON_SOURCE_PIXMAP,
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };
static unsigned int signals[LAST_SIGNAL];

typedef struct {
	char* iconname;
	char *iconpath;
	GVariant* iconpixmap;
	GdkPaintable* icondata;
} cached_icon_t;

static void	sn_item_constructed	(GObject *obj);
static void	sn_item_dispose		(GObject *obj);
static void	sn_item_finalize	(GObject *obj);

static void	sn_item_size_allocate	(GtkWidget *widget,
                                        int width,
                                        int height,
                                        int baseline);

static void	sn_item_measure		(GtkWidget *widget,
                                        GtkOrientation orientation,
                                        int for_size,
                                        int *minimum,
                                        int *natural,
                                        int *minimum_baseline,
                                        int *natural_baseline);

static void request_newicon_name(SnItem *self);
static void request_newicon_pixmap(SnItem *self);
static void request_newicon_path(SnItem *self);

static gboolean
validate_pixdata(GVariant *icondata)
{
	int32_t width, height;
	GVariant *bytearr;
	size_t size;

	g_variant_get_child(icondata, 0, "i", &width);
	g_variant_get_child(icondata, 1, "i", &height);
	bytearr = g_variant_get_child_value(icondata, 2);
	size = g_variant_get_size(bytearr);

	g_variant_unref(bytearr);

	if (width == 0 || height == 0 || size == 0)
		return false;
	else
		return true;
}

static void
argb_to_rgba(int32_t width, int32_t height, unsigned char *icon_data)
{
	// Icon data is ARGB, gdk textures are RGBA. Flip the channels
	for (int32_t i = 0; i < 4 * width * height; i += 4) {
		unsigned char alpha = icon_data[i];
		icon_data[i]        = icon_data[i + 1];
		icon_data[i + 1]    = icon_data[i + 2];
		icon_data[i + 2]    = icon_data[i + 3];
		icon_data[i + 3]    = alpha;
	}
}

static int
find_cached_icon_path(cached_icon_t *cicon,
                      const char *path)
{
	if (cicon->iconpath == NULL || path == NULL)
		return -1;

	return strcmp(cicon->iconname, path);
}

static int
find_cached_icon_name(cached_icon_t *cicon,
                      const char *name)
{
	if (cicon->iconname == NULL || name == NULL)
		return -1;

	return strcmp(cicon->iconname, name);
}

static int
find_cached_icon_pixmap(cached_icon_t *cicon, GVariant *pixmap)
{
	if (cicon->iconpixmap == NULL || pixmap == NULL)
		return -1;

	if (g_variant_equal(cicon->iconpixmap, pixmap))
		return 0;
	else
		return 1;
}

static void
cachedicons_free(void *data)
{
	cached_icon_t *cicon = (cached_icon_t*)data;
	g_free(cicon->iconname);
	g_free(cicon->iconpath);
	if (cicon->iconpixmap != NULL)
		g_variant_unref(cicon->iconpixmap);
	if (cicon->icondata != NULL)
		g_object_unref(cicon->icondata);
	g_free(cicon);
}

static void
pixbuf_destroy(unsigned char *pixeld, void *data)
{
	g_free(pixeld);
}

static GVariant*
select_icon_by_size(GVariant *vicondata, int32_t target_icon_size)
{
	// Apps broadcast icons as variant a(iiay)
	// Meaning array of tuples, tuple representing an icon
	// first 2 members ii in each tuple are width and height
	// We iterate the array and pick the icon size closest to
	// the target based on its width and save the index
	GVariantIter iter;
	int selected_index = 0;
	int current_index = 0;
	int32_t diff = INT32_MAX;
	GVariant *child;
	g_variant_iter_init(&iter, vicondata);
	while ((child = g_variant_iter_next_value(&iter))) {
		int32_t curwidth;
		g_variant_get_child(child, 0, "i", &curwidth);
		int32_t curdiff;
		if (curwidth > target_icon_size)
			curdiff = curwidth - target_icon_size;
		else
			curdiff = target_icon_size - curwidth;

		if (curdiff < diff)
			selected_index = current_index;

		current_index = current_index + 1;
		g_variant_unref(child);
	}

	GVariant *selected = g_variant_get_child_value(vicondata,
	                                               (size_t)selected_index);

	// Discard if the array is empty
	if (validate_pixdata(selected)) {
		return selected;
	} else {
		g_variant_unref(selected);
		return NULL;
	}
}

static GdkPaintable*
get_paintable_from_name(const char *iconname, int32_t iconsize)
{
	GdkPaintable *paintable = NULL;
	GtkIconPaintable *icon;

	GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
	icon = gtk_icon_theme_lookup_icon(theme,
	                                  iconname,
	                                  NULL,  // const char **fallbacks
	                                  iconsize,
	                                  1,
	                                  GTK_TEXT_DIR_LTR,
	                                  0);  // GtkIconLookupFlags
	paintable = GDK_PAINTABLE(icon);

	return paintable;
}

static GdkPaintable*
get_paintable_from_path(const char *path, int32_t iconsize)
{
	GdkPaintable *paintable = NULL;

	GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_size(path,
	                                                     iconsize,
	                                                     iconsize,
	                                                     NULL);

	GdkTexture* texture = gdk_texture_new_for_pixbuf(pixbuf);
	paintable = GDK_PAINTABLE(texture);

	g_object_unref(pixbuf);

	return paintable;
}

static GdkPaintable*
get_paintable_from_data(GVariant *iconpixmap_v, int32_t iconsize)
{
	GdkPaintable *paintable;
	GVariantIter iter;

	int32_t width;
	int32_t height;
	GVariant *vicondata;

	g_variant_iter_init(&iter, iconpixmap_v);

	g_variant_iter_next(&iter, "i", &width);
	g_variant_iter_next(&iter, "i", &height);
	vicondata = g_variant_iter_next_value(&iter);

	size_t size = g_variant_get_size(vicondata);
	const void *icon_data_dup = g_variant_get_data(vicondata);

	unsigned char *icon_data = g_memdup2(icon_data_dup, size);
	argb_to_rgba(width, height, icon_data);

	if (height == 0) {
		g_variant_unref(vicondata);
		return NULL;
	}
	int32_t padding = size / height - 4 * width;
	int32_t rowstride = 4 * width + padding;

	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(icon_data,
	                                             GDK_COLORSPACE_RGB,
	                                             true,
	                                             8,
	                                             width,
	                                             height,
	                                             rowstride,
	                                             (GdkPixbufDestroyNotify)pixbuf_destroy,
	                                             NULL);

	GdkTexture *texture = gdk_texture_new_for_pixbuf(pixbuf);
	paintable = GDK_PAINTABLE(texture);

	g_object_unref(pixbuf);
	g_variant_unref(vicondata);

	return paintable;
}

static void
new_iconname_handler(GObject *obj, GAsyncResult *res, void *data)
{
	SnItem *self = SN_ITEM(data);
	GDBusProxy *proxy = G_DBUS_PROXY(obj);

	GError *err = NULL;
	GVariant *retvariant = g_dbus_proxy_call_finish(proxy, res, &err);
	// (v)

	if (err != NULL) {
		switch (err->code) {
			case G_DBUS_ERROR_UNKNOWN_OBJECT:
				// Remote object went away while call was underway
				break;
			case G_DBUS_ERROR_UNKNOWN_PROPERTY:
				// Expected when ICON_SOURCE_UNKNOWN
				break;
			default:
				g_warning("%s\n", err->message);
				break;
		}
		g_error_free(err);
		request_newicon_pixmap(self);
		g_object_unref(self);
		return;
	}

	GVariant *iconname_v;
	const char *iconname = NULL;
	g_variant_get(retvariant, "(v)", &iconname_v);
	g_variant_get(iconname_v, "&s", &iconname);
	g_variant_unref(retvariant);

	// New iconname invalid
	if (iconname == NULL || strcmp(iconname, "") == 0) {
		self->icon_source = ICON_SOURCE_UNKNOWN;
		g_variant_unref(iconname_v);
		g_object_unref(self);
		request_newicon_pixmap(self);
		return;
	// New iconname is a path
	} else if (access(iconname, R_OK) == 0) {
		self->icon_source = ICON_SOURCE_UNKNOWN;
		g_variant_unref(iconname_v);
		g_object_unref(self);
		request_newicon_path(self);
		return;
	}

	// Icon didn't change
	if (strcmp(iconname, self->iconname) == 0) {
		g_variant_unref(iconname_v);
		g_object_unref(self);
		return;
	}

	GSList *elem = g_slist_find_custom(self->cachedicons,
	                                   iconname,
	                                   (GCompareFunc)find_cached_icon_name);

	// Cache hit
	if (elem != NULL) {
		cached_icon_t *cicon = (cached_icon_t*)elem->data;
		self->iconname = cicon->iconname;
		gtk_image_set_from_paintable(GTK_IMAGE(self->image), cicon->icondata);
		g_debug("%s: Icon cache hit - iconname", self->busname);
	// Cache miss -> cache new icon
	} else {
		cached_icon_t *cicon = g_malloc0(sizeof(cached_icon_t));
		self->iconname = g_strdup(iconname);
		cicon->iconname = self->iconname;
		cicon->icondata = get_paintable_from_name(self->iconname,
		                                          self->iconsize);
		gtk_image_set_from_paintable(GTK_IMAGE(self->image), cicon->icondata);
		self->cachedicons = g_slist_prepend(self->cachedicons, cicon);
		self->icon_source = ICON_SOURCE_NAME;
	}

	g_variant_unref(iconname_v);
	g_object_unref(self);
}

static void
new_iconpath_handler(GObject *obj, GAsyncResult *res, void *data)
{
	SnItem *self = SN_ITEM(data);
	GDBusProxy *proxy = G_DBUS_PROXY(obj);

	GError *err = NULL;
	GVariant *retvariant = g_dbus_proxy_call_finish(proxy, res, &err);
	// (v)

	if (err != NULL) {
		switch (err->code) {
			case G_DBUS_ERROR_UNKNOWN_OBJECT:
				// Remote object went away while call was underway
				break;
			case G_DBUS_ERROR_UNKNOWN_PROPERTY:
				// Expected when ICON_SOURCE_UNKNOWN
				break;
			default:
				g_warning("%s\n", err->message);
				break;
		}
		g_error_free(err);
		request_newicon_pixmap(self);
		g_object_unref(self);
		return;
	}

	GVariant *viconpath;
	const char *iconpath = NULL;
	g_variant_get(retvariant, "(v)", &viconpath);
	g_variant_get(viconpath, "&s", &iconpath);
	g_variant_unref(retvariant);

	// New iconpath is invalid
	if (iconpath == NULL || strcmp(iconpath, "") == 0 || access(iconpath, R_OK) == 0) {
		self->icon_source = ICON_SOURCE_UNKNOWN;
		g_variant_unref(viconpath);
		g_object_unref(self);
		request_newicon_pixmap(self);
		return;
	// New iconpath is not a path but possibly an iconname
	} else if (iconpath != NULL && strcmp(iconpath, "") != 0) {
		self->icon_source = ICON_SOURCE_UNKNOWN;
		g_variant_unref(viconpath);
		g_object_unref(self);
		request_newicon_name(self);
		return;
	}

	// Icon didn't change
	if (strcmp(iconpath, self->iconpath) == 0) {
		g_variant_unref(viconpath);
		request_newicon_pixmap(self);
		g_object_unref(self);
		return;
	}

	GSList *elem = g_slist_find_custom(self->cachedicons,
	                                   iconpath,
	                                   (GCompareFunc)find_cached_icon_path);

	// Cache hit
	if (elem != NULL) {
		cached_icon_t *cicon = (cached_icon_t*)elem->data;
		self->iconpath = cicon->iconpath;
		gtk_image_set_from_paintable(GTK_IMAGE(self->image), cicon->icondata);
		g_debug("%s: Icon cache hit - iconpath", self->busname);
	// Cache miss -> cache new icon
	} else {
		cached_icon_t *cicon = g_malloc0(sizeof(cached_icon_t));
		self->iconpath = g_strdup(iconpath);
		cicon->iconpath = self->iconpath;
		cicon->icondata = get_paintable_from_name(self->iconpath,
		                                          self->iconsize);
		gtk_image_set_from_paintable(GTK_IMAGE(self->image), cicon->icondata);
		self->cachedicons = g_slist_prepend(self->cachedicons, cicon);
		self->icon_source = ICON_SOURCE_PATH;
	}

	g_variant_unref(viconpath);
	g_object_unref(self);
}

static void
new_pixmaps_handler(GObject *obj, GAsyncResult *res, void *data)
{
	SnItem *self = SN_ITEM(data);
	GDBusProxy *proxy = G_DBUS_PROXY(obj);

	GError *err = NULL;
	GVariant *retvariant = g_dbus_proxy_call_finish(proxy, res, &err);
	// (v)

	if (err != NULL) {
		switch (err->code) {
			case G_DBUS_ERROR_UNKNOWN_OBJECT:
				// Remote object went away while call was underway
				break;
			case G_DBUS_ERROR_UNKNOWN_PROPERTY:
				// Expected when ICON_SOURCE_UNKNOWN
				break;
			default:
				g_warning("%s\n", err->message);
		}
		g_error_free(err);
		g_object_unref(self);
		return;
	}

	GVariant *newpixmaps;
	g_variant_get(retvariant, "(v)", &newpixmaps);

	GVariant *pixmap = select_icon_by_size(newpixmaps, self->iconsize);

	// No valid icon in data
	if (pixmap == NULL) {
		self->icon_source = ICON_SOURCE_UNKNOWN;
		g_variant_unref(newpixmaps);
		g_variant_unref(retvariant);
		g_object_unref(self);
		return;
	}

	// Icon didn't change
	if (self->iconpixmap && g_variant_equal(pixmap, self->iconpixmap)) {
		g_variant_unref(pixmap);
		g_variant_unref(newpixmaps);
		g_variant_unref(retvariant);
		g_object_unref(self);
		return;
	}

	GSList *elem = g_slist_find_custom(self->cachedicons,
	                                   pixmap,
	                                   (GCompareFunc)find_cached_icon_pixmap);

	// Cache hit
	if (elem != NULL) {
		cached_icon_t *cicon = (cached_icon_t*)elem->data;
		self->iconpixmap = cicon->iconpixmap;
		gtk_image_set_from_paintable(GTK_IMAGE(self->image), cicon->icondata);
		g_debug("%s: Icon cache hit - pixmap", self->busname);
	// Cache miss -> cache new icon
	} else {
		cached_icon_t *cicon = g_malloc0(sizeof(cached_icon_t));
		self->iconpixmap = g_variant_ref(pixmap);
		cicon->iconpixmap = self->iconpixmap;
		cicon->icondata = get_paintable_from_data(self->iconpixmap,
		                                          self->iconsize);
		gtk_image_set_from_paintable(GTK_IMAGE(self->image), cicon->icondata);
		self->cachedicons = g_slist_prepend(self->cachedicons, cicon);
		self->icon_source = ICON_SOURCE_PIXMAP;
	}

	g_variant_unref(pixmap);
	g_variant_unref(newpixmaps);
	g_variant_unref(retvariant);
	g_object_unref(self);
}

static void
request_newicon_name(SnItem *self)
{
	g_dbus_proxy_call(self->proxy,
			  "org.freedesktop.DBus.Properties.Get",
			  g_variant_new("(ss)",
					"org.kde.StatusNotifierItem",
					"IconName"),
			  G_DBUS_CALL_FLAGS_NONE,
			  -1,
			  NULL,
			  new_iconname_handler,
			  g_object_ref(self));
}

static void
request_newicon_path(SnItem *self)
{
	g_dbus_proxy_call(self->proxy,
			  "org.freedesktop.DBus.Properties.Get",
			  g_variant_new("(ss)",
					"org.kde.StatusNotifierItem",
					"IconName"),
			  G_DBUS_CALL_FLAGS_NONE,
			  -1,
			  NULL,
			  new_iconpath_handler,
			  g_object_ref(self));
}

static void
request_newicon_pixmap(SnItem *self)
{
	g_dbus_proxy_call(self->proxy,
			  "org.freedesktop.DBus.Properties.Get",
			  g_variant_new("(ss)",
					"org.kde.StatusNotifierItem",
					"IconPixmap"),
			  G_DBUS_CALL_FLAGS_NONE,
			  -1,
			  NULL,
			  new_pixmaps_handler,
			  g_object_ref(self));
}

static void
proxy_signal_handler(GDBusProxy *proxy,
                             const char *sender,
                             const char *signal,
                             GVariant *data_v,
                             void *data)
{
	SnItem *self = SN_ITEM(data);

	if (strcmp(signal, "NewIcon") == 0) {
		switch (self->icon_source) {
			case ICON_SOURCE_NAME:
				request_newicon_name(self);
				break;
			case ICON_SOURCE_PATH:
				request_newicon_path(self);
				break;
			case ICON_SOURCE_PIXMAP:
				request_newicon_pixmap(self);
				break;
			default:
				request_newicon_name(self);
				break;
		}
	}
}

static void
popup_popover(SnDbusmenu *dbusmenu, SnItem *self)
{
	if (self->in_destruction)
		return;

	g_object_set(self, "menuvisible", true, NULL);
	gtk_popover_popup(GTK_POPOVER(self->popovermenu));
}

static void
leftclick_handler(GtkGestureClick *click,
                          int n_press,
                          double x,
                          double y,
                          void *data)
{
	SnItem *self = SN_ITEM(data);

	g_dbus_proxy_call(self->proxy,
	                  "Activate",
	                  g_variant_new("(ii)", 0, 0),
	                  G_DBUS_CALL_FLAGS_NONE,
	                  -1,
	                  NULL,
	                  NULL,
	                  NULL);
}

static void
rightclick_handler(GtkGestureClick *click,
                           int n_press,
                           double x,
                           double y,
                           void *data)
{
	SnItem *self = SN_ITEM(data);
	if (self->in_destruction)
		return;

	g_signal_emit(self, signals[RIGHTCLICK], 0);
}

static void
connect_to_menu(SnItem *self)
{
	if (self->in_destruction)
		return;

	const char *menu_buspath = NULL;
	GVariant *vmenupath = g_dbus_proxy_get_cached_property(self->proxy, "Menu");

	if (vmenupath != NULL) {
		g_variant_get(vmenupath, "&o", &menu_buspath);
		if (strcmp(menu_buspath, "") != 0) {
			self->dbusmenu = sn_dbusmenu_new(self->busname, menu_buspath, self);

			self->rclick_id = g_signal_connect(self->rclick,
			                                   "pressed",
			                                   G_CALLBACK(rightclick_handler),
			                                   self);

			self->popup_id = g_signal_connect(self->dbusmenu,
			                                  "abouttoshowhandled",
			                                  G_CALLBACK(popup_popover),
			                                  self);
		}
		g_variant_unref(vmenupath);
	}
}

static void
select_icon_source(SnItem *self)
{
	char *iconname_or_path = NULL;
	GVariant *vname = g_dbus_proxy_get_cached_property(self->proxy, "IconName");
	GVariant *vpixmaps = g_dbus_proxy_get_cached_property(self->proxy, "IconPixmap");

	if (vname != NULL) {
		g_variant_get(vname, "s", &iconname_or_path);
		if (strcmp(iconname_or_path, "") == 0) {
			g_free(iconname_or_path);
			iconname_or_path = NULL;
		}
	}

	if (iconname_or_path != NULL && access(iconname_or_path, R_OK) == 0) {
		self->iconpath = iconname_or_path;
		self->icon_source = ICON_SOURCE_PATH;
	} else if (iconname_or_path != NULL) {
		self->iconname = iconname_or_path;
		self->icon_source = ICON_SOURCE_NAME;
	} else if (vpixmaps != NULL) {
		GVariant *pixmap = select_icon_by_size(vpixmaps, self->iconsize);
		if (pixmap != NULL) {
			self->iconpixmap = pixmap;
			self->icon_source = ICON_SOURCE_PIXMAP;
		}
	} else {
		self->iconname = g_strdup("missing-icon");
		self->icon_source = ICON_SOURCE_UNKNOWN;
	}

	if (vname != NULL)
		g_variant_unref(vname);
	if (vpixmaps != NULL)
		g_variant_unref(vpixmaps);
}

static void
add_icontheme_path(GDBusProxy *proxy)
{
	const char *iconthemepath;
	GVariant *viconthemepath;
	GtkIconTheme *theme;

	viconthemepath = g_dbus_proxy_get_cached_property(proxy, "IconThemePath");
	theme = gtk_icon_theme_get_for_display(gdk_display_get_default());

	if (viconthemepath != NULL) {
		g_variant_get(viconthemepath, "&s", &iconthemepath);
		gtk_icon_theme_add_search_path(theme, iconthemepath);
		g_variant_unref(viconthemepath);
	}
}

static void
proxy_ready_handler(GObject *obj, GAsyncResult *res, void *data)
{
	SnItem *self = SN_ITEM(data);

	GError *err = NULL;
	GDBusProxy *proxy = g_dbus_proxy_new_for_bus_finish(res, &err);

	if (err != NULL) {
		g_warning("Failed to construct gdbusproxy for snitem: %s\n", err->message);
		g_error_free(err);
		g_object_unref(self);
		return;
	}
	self->proxy = proxy;
	self->proxy_id = g_signal_connect(self->proxy,
	                                 "g-signal",
	                                 G_CALLBACK(proxy_signal_handler),
	                                 self);

	add_icontheme_path(proxy);
	select_icon_source(self);

	GdkPaintable *paintable;
	cached_icon_t *cicon = g_malloc0(sizeof(cached_icon_t));

	switch (self->icon_source) {
		case ICON_SOURCE_NAME:
			paintable = get_paintable_from_name(self->iconname, self->iconsize);
			cicon->iconname = self->iconname;
			cicon->icondata = paintable;
			break;
		case ICON_SOURCE_PIXMAP:
			paintable = get_paintable_from_data(self->iconpixmap, self->iconsize);
			cicon->iconpixmap = self->iconpixmap;
			cicon->icondata = paintable;
			break;
		case ICON_SOURCE_PATH:
			paintable = get_paintable_from_path(self->iconpath, self->iconsize);
			cicon->iconpath = self->iconpath;
			cicon->icondata = paintable;
			break;
		case ICON_SOURCE_UNKNOWN:
			paintable = get_paintable_from_name(self->iconname, self->iconsize);
			cicon->iconname = self->iconname;
			cicon->icondata = paintable;
			break;
		default:
			g_assert_not_reached();
			break;
	}

	self->cachedicons = g_slist_prepend(self->cachedicons, cicon);
	gtk_image_set_from_paintable(GTK_IMAGE(self->image), paintable);

	self->lclick_id = g_signal_connect(self->lclick,
	                                   "pressed",
	                                   G_CALLBACK(leftclick_handler),
	                                   self);
	connect_to_menu(self);

	g_object_unref(self);
}

static void
sn_item_notify_closed(GtkPopover *popover, void *data)
{
	SnItem *self = SN_ITEM(data);
	g_object_set(self, "menuvisible", false, NULL);
}

static void
sn_item_measure(GtkWidget *widget,
                GtkOrientation orientation,
                int for_size,
                int *minimum,
                int *natural,
                int *minimum_baseline,
                int *natural_baseline)
{
	SnItem *self = SN_ITEM(widget);

	switch (orientation) {
		case GTK_ORIENTATION_HORIZONTAL:
			*natural = self->iconsize;
			*minimum = self->iconsize;
			*minimum_baseline = -1;
			*natural_baseline = -1;
			break;
		case GTK_ORIENTATION_VERTICAL:
			*natural = self->iconsize;
			*minimum = self->iconsize;
			*minimum_baseline = -1;
			*natural_baseline = -1;
			break;
	}
}

static void
sn_item_size_allocate(GtkWidget *widget,
                      int width,
                      int height,
                      int baseline)
{
	SnItem *self = SN_ITEM(widget);
	gtk_widget_size_allocate(self->image, &(GtkAllocation) {0, 0, width, height}, -1);
	gtk_popover_present(GTK_POPOVER(self->popovermenu));
}

static void
sn_item_set_property(GObject *object,
                     unsigned int property_id,
                     const GValue *value,
                     GParamSpec *pspec)
{
	SnItem *self = SN_ITEM(object);

	switch (property_id) {
		case PROP_BUSNAME:
			self->busname = g_strdup(g_value_get_string(value));
			break;
		case PROP_BUSOBJ:
			self->busobj = g_strdup(g_value_get_string(value));
			break;
		case PROP_ICONSIZE:
			self->iconsize = g_value_get_int(value);
			break;
		case PROP_DBUSMENU:
			self->dbusmenu = g_value_get_object(value);
			break;
		case PROP_MENUVISIBLE:
			self->menu_visible = g_value_get_boolean(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void
sn_item_get_property(GObject *object, unsigned int property_id, GValue *value, GParamSpec *pspec)
{
	SnItem *self = SN_ITEM(object);

	switch (property_id) {
		case PROP_BUSNAME:
			g_value_set_string(value, self->busname);
			break;
		case PROP_BUSOBJ:
			g_value_set_string(value, self->busobj);
			break;
		case PROP_ICONSIZE:
			g_value_set_int(value, self->iconsize);
			break;
		case PROP_DBUSMENU:
			g_value_set_object(value, self->dbusmenu);
			break;
		case PROP_MENUVISIBLE:
			g_value_set_boolean(value, self->menu_visible);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void
sn_item_class_init(SnItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

	widget_class->measure = sn_item_measure;
	widget_class->size_allocate = sn_item_size_allocate;

	object_class->set_property = sn_item_set_property;
	object_class->get_property = sn_item_get_property;

	object_class->constructed = sn_item_constructed;
	object_class->dispose = sn_item_dispose;
	object_class->finalize = sn_item_finalize;

	obj_properties[PROP_BUSNAME] =
		g_param_spec_string("busname", NULL, NULL,
		                    NULL,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT_ONLY |
		                    G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_BUSOBJ] =
		g_param_spec_string("busobj", NULL, NULL,
		                    NULL,
		                    G_PARAM_WRITABLE |
		                    G_PARAM_CONSTRUCT_ONLY |
		                    G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_ICONSIZE] =
		g_param_spec_int("iconsize", NULL, NULL,
		                 INT_MIN,
		                 INT_MAX,
		                 22,
		                 G_PARAM_WRITABLE |
		                 G_PARAM_CONSTRUCT_ONLY |
		                 G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_DBUSMENU] =
		g_param_spec_object("dbusmenu", NULL, NULL,
		                    SN_TYPE_DBUSMENU,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_MENUVISIBLE] =
		g_param_spec_boolean("menuvisible", NULL, NULL,
		                     false,
		                     G_PARAM_CONSTRUCT |
		                     G_PARAM_READWRITE |
		                     G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties(object_class, N_PROPERTIES, obj_properties);

	signals[RIGHTCLICK] = g_signal_new("rightclick",
	                                   SN_TYPE_ITEM,
	                                   G_SIGNAL_RUN_LAST,
	                                   0,
	                                   NULL,
	                                   NULL,
	                                   NULL,
	                                   G_TYPE_NONE,
	                                   0);

	gtk_widget_class_set_css_name(widget_class, "systray-item");
}

static void
sn_item_init(SnItem *self)
{
	GtkWidget *widget = GTK_WIDGET(self);

	self->in_destruction = false;
	self->icon_source = ICON_SOURCE_UNKNOWN;

	self->image = gtk_image_new();
	gtk_widget_set_parent(self->image, widget);

	self->init_menu = g_menu_new();
	self->popovermenu = gtk_popover_menu_new_from_model(G_MENU_MODEL(self->init_menu));
	gtk_popover_menu_set_flags(GTK_POPOVER_MENU(self->popovermenu), GTK_POPOVER_MENU_NESTED);
	gtk_popover_set_has_arrow(GTK_POPOVER(self->popovermenu), false);
	gtk_widget_set_parent(self->popovermenu, widget);

	self->lclick = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(self->lclick), 1);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(self->lclick));

	self->rclick = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(self->rclick), 3);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(self->rclick));

	g_signal_connect(self->popovermenu, "closed", G_CALLBACK(sn_item_notify_closed), self);
}

static void
sn_item_constructed(GObject *obj)
{
	SnItem *self = SN_ITEM(obj);

	GDBusNodeInfo *nodeinfo = g_dbus_node_info_new_for_xml(STATUSNOTIFIERITEM_XML, NULL);
	g_dbus_proxy_new_for_bus(G_BUS_TYPE_SESSION,
	                         G_DBUS_PROXY_FLAGS_NONE,
	                         nodeinfo->interfaces[0],
	                         self->busname,
	                         self->busobj,
	                         "org.kde.StatusNotifierItem",
	                         NULL,
	                         proxy_ready_handler,
	                         g_object_ref(self));
	g_dbus_node_info_unref(nodeinfo);

	G_OBJECT_CLASS(sn_item_parent_class)->constructed(obj);
}

static void
sn_item_dispose(GObject *obj)
{
	SnItem *self = SN_ITEM(obj);

	self->in_destruction = true;

	g_clear_signal_handler(&self->lclick_id, self->lclick);
	g_clear_signal_handler(&self->rclick_id, self->rclick);
	g_clear_signal_handler(&self->proxy_id, self->proxy);
	g_clear_signal_handler(&self->popup_id, self->dbusmenu);

	if (self->dbusmenu != NULL) {
		// Unref will be called from sndbusmenu dispose function
		g_object_ref(self);

		g_object_unref(self->dbusmenu);
		self->dbusmenu = NULL;
	}

	if (self->proxy != NULL) {
		g_object_unref(self->proxy);
		self->proxy = NULL;
	}

	if (self->popovermenu != NULL) {
		gtk_widget_unparent(self->popovermenu);
		self->popovermenu = NULL;
		g_object_unref(self->init_menu);
		self->init_menu = NULL;
	}

	if (self->image != NULL) {
		gtk_widget_unparent(self->image);
		self->image = NULL;
	}

	G_OBJECT_CLASS(sn_item_parent_class)->dispose(obj);
}

static void
sn_item_finalize(GObject *object)
{
	SnItem *self = SN_ITEM(object);

	g_free(self->busname);
	g_free(self->busobj);

	g_slist_free_full(self->cachedicons, cachedicons_free);

	G_OBJECT_CLASS(sn_item_parent_class)->finalize(object);
}

/* PUBLIC METHODS */
void
sn_item_set_menu_model(SnItem *self, GMenu* menu)
{
	g_return_if_fail(SN_IS_ITEM(self));
	g_return_if_fail(G_IS_MENU(menu));

	if (self->popovermenu == NULL)
		return;

	gtk_popover_menu_set_menu_model(GTK_POPOVER_MENU(self->popovermenu), G_MENU_MODEL(menu));
}

void
sn_item_clear_menu_model(SnItem *self)
{
	g_return_if_fail(SN_IS_ITEM(self));

	if (self->popovermenu == NULL)
		return;

	GtkPopoverMenu *popovermenu = GTK_POPOVER_MENU(self->popovermenu);
	GMenuModel *menumodel = G_MENU_MODEL(self->init_menu);

	gtk_popover_menu_set_menu_model(popovermenu, menumodel);
}

void
sn_item_set_actiongroup(SnItem *self, const char *prefix, GSimpleActionGroup *group)
{
	g_return_if_fail(SN_IS_ITEM(self));
	g_return_if_fail(G_IS_SIMPLE_ACTION_GROUP(group));

	gtk_widget_insert_action_group(GTK_WIDGET(self),
	                               prefix,
	                               G_ACTION_GROUP(group));
}

void
sn_item_clear_actiongroup(SnItem *self, const char *prefix)
{
	g_return_if_fail(SN_IS_ITEM(self));

	gtk_widget_insert_action_group(GTK_WIDGET(self),
	                               prefix,
	                               NULL);
}

gboolean
sn_item_get_popover_visible(SnItem *self)
{
	g_return_val_if_fail(SN_IS_ITEM(self), true);

	return self->menu_visible;
}

SnItem*
sn_item_new(const char *busname, const char *busobj, int iconsize)
{
	return g_object_new(SN_TYPE_ITEM,
	                    "busname", busname,
	                    "busobj", busobj,
	                    "iconsize", iconsize,
	                    NULL);
}
/* PUBLIC METHODS */
