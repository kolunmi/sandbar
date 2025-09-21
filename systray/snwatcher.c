#include "snwatcher.h"

#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

struct _SnWatcher
{
	GObject parent_instance;

	GDBusConnection* conn;
	GList* tracked_items;

	int owner_id;
	int obj_reg_id;
	int sig_sub_id;
};

G_DEFINE_FINAL_TYPE(SnWatcher, sn_watcher, G_TYPE_OBJECT)

enum
{
	TRAYITEM_REGISTERED,
	TRAYITEM_UNREGISTERED,
	LAST_SIGNAL
};

static unsigned int signals[LAST_SIGNAL];

static void		sn_watcher_dispose			(GObject *obj);

static void		sn_watcher_call_method_handler		(GDBusConnection *conn,
                                                                const char *sender,
                                                                const char *object_path,
                                                                const char *interface_name,
                                                                const char *method_name,
                                                                GVariant *parameters,
                                                                GDBusMethodInvocation *invocation,
                                                                void *data);

static GVariant*	sn_watcher_prop_get_handler		(GDBusConnection* conn,
                                                                const char* sender,
                                                                const char* object_path,
                                                                const char* interface_name,
                                                                const char* property_name,
                                                                GError** err,
                                                                void *data);

static GDBusInterfaceVTable interface_vtable = {
	sn_watcher_call_method_handler,
	sn_watcher_prop_get_handler,
	NULL
};


static void
sn_watcher_register_item(SnWatcher *self,
                         const char *busname,
                         const char *busobj)
{
	// Check if we are already tracking this item
	if (g_list_find_custom(self->tracked_items, busname,(GCompareFunc)strcmp)
	    != NULL) {
		return;
	}

	g_debug("Registering %s", busname);
	self->tracked_items = g_list_prepend(self->tracked_items, g_strdup(busname));

	g_signal_emit(self, signals[TRAYITEM_REGISTERED], 0, busname, busobj);

	// Dbus signal is emitted only to conform to the specification.
	// We don't use this ourselves.
	GError *err = NULL;
	g_dbus_connection_emit_signal(self->conn,
	                              NULL,
	                              "/StatusNotifierWatcher",
	                              "org.kde.StatusNotifierWatcher",
	                              "StatusNotifierItemRegistered",
	                              g_variant_new("(s)", busname),
	                              &err);
	if (err) {
		g_warning("%s", err->message);
		g_error_free(err);
	}
}

static void
sn_watcher_unregister_item(SnWatcher *self, const char *busname)
{
	g_debug("Unregistering %s", busname);

	g_signal_emit(self, signals[TRAYITEM_UNREGISTERED], 0, busname);

	// Dbus signal is emitted only to conform to the specification.
	// We don't use this ourselves.
	GError *err = NULL;
	g_dbus_connection_emit_signal(self->conn,
	                              NULL,
	                              "/StatusNotifierWatcher",
	                              "org.kde.StatusNotifierWatcher",
	                              "StatusNotifierItemUnregistered",
	                              g_variant_new("(s)", busname),
	                              &err);
	if (err) {
		g_warning("%s", err->message);
		g_error_free(err);
	}
}

static void
bus_get_snitems_helper(void *data, void *udata)
{
	char *busname = (char*)data;
	GVariantBuilder *builder = (GVariantBuilder*)udata;

	g_variant_builder_add_value(builder, g_variant_new_string(busname));
}

static GVariant*
sn_watcher_prop_get_handler(GDBusConnection* conn,
                            const char* sender,
                            const char* object_path,
                            const char* interface_name,
                            const char* property_name,
                            GError** err,
                            void *data)
{
	SnWatcher *self = SN_WATCHER(data);

	if (strcmp(property_name, "ProtocolVersion") == 0) {
		return g_variant_new("i", 0);
	} else if (strcmp(property_name, "IsStatusNotifierHostRegistered") == 0) {
		return g_variant_new("b", TRUE);
	} else if (strcmp(property_name, "RegisteredStatusNotifierItems") == 0) {
		if (!self->tracked_items)
			return g_variant_new("as", NULL);

		GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
		g_list_foreach(self->tracked_items, bus_get_snitems_helper, builder);
		GVariant *as = g_variant_builder_end(builder);

		g_variant_builder_unref(builder);

		return as;
	} else {
		g_set_error(err,
		            G_DBUS_ERROR,
		            G_DBUS_ERROR_UNKNOWN_PROPERTY,
		            "Unknown property '%s'.",
		            property_name);
		return NULL;
	}
}


static void
sn_watcher_call_method_handler(GDBusConnection *conn,
                               const char *sender,
                               const char *obj_path,
                               const char *iface_name,
                               const char *method_name,
                               GVariant *params,
                               GDBusMethodInvocation *invoc,
                               void *data)
{
	SnWatcher *self = SN_WATCHER(data);

	if (strcmp(method_name, "RegisterStatusNotifierItem") == 0) {
		const char *param;
		const char *busobj;
		const char *registree_name;

		g_variant_get(params, "(&s)", &param);

		if (g_str_has_prefix(param, "/"))
			busobj = param;
		else
			busobj = "/StatusNotifierItem";

		if (g_str_has_prefix(param, ":"))
			registree_name = param;
		else
			registree_name = sender;

		sn_watcher_register_item(self, registree_name, busobj);
		g_dbus_method_invocation_return_value(invoc, NULL);

	} else {
		g_dbus_method_invocation_return_dbus_error(invoc,
		                                           "org.freedesktop.DBus.Error.UnknownMethod",
		                                           "Unknown method");
	}
}

static void
sn_watcher_monitor_bus(GDBusConnection* conn,
                       const char* sender,
                       const char* objpath,
                       const char* iface_name,
                       const char* signame,
                       GVariant *params,
                       void *data)
{
	SnWatcher *self = SN_WATCHER(data);

	if (strcmp(signame, "NameOwnerChanged") == 0) {
		if (!self->tracked_items)
			return;

		const char *name;
		const char *old_owner;
		const char *new_owner;
		g_variant_get(params, "(&s&s&s)", &name, &old_owner, &new_owner);
		if (strcmp(new_owner, "") == 0) {
			GList *pmatch = g_list_find_custom(self->tracked_items,
			                                   name,
			                                   (GCompareFunc)strcmp);
			if (pmatch) {
				sn_watcher_unregister_item(self, pmatch->data);
				g_free(pmatch->data);
				self->tracked_items = g_list_delete_link(self->tracked_items,
				                                         pmatch);
			}
		}
	}
}

static void
sn_watcher_unregister_all(SnWatcher *self)
{
	GList *tmp = self->tracked_items;

	while (tmp) {
		GList *next = tmp->next;
		sn_watcher_unregister_item(self, tmp->data);
		g_free(tmp->data);
		self->tracked_items = g_list_delete_link(self->tracked_items, tmp);
		tmp = next;
	}
}

static void
sn_watcher_bus_acquired_handler(GDBusConnection *conn, const char *busname, void *data)
{
	SnWatcher *self = SN_WATCHER(data);

	self->conn = conn;

	GError *err = NULL;
	GDBusNodeInfo *nodeinfo = g_dbus_node_info_new_for_xml(STATUSNOTIFIERWATCHER_XML, NULL);

	self->obj_reg_id =
		g_dbus_connection_register_object(self->conn,
		                                  "/StatusNotifierWatcher",
		                                  nodeinfo->interfaces[0],
		                                  &interface_vtable,
		                                  self,
		                                  NULL,
		                                  &err);

	g_dbus_node_info_unref(nodeinfo);

	if (err) {
		g_error("%s", err->message);
		g_error_free(err);
		exit(-1);
	}

	self->sig_sub_id =
		g_dbus_connection_signal_subscribe(self->conn,
		                                   NULL,  // Listen to all senders);
		                                   "org.freedesktop.DBus",
		                                   "NameOwnerChanged",
		                                   NULL,  // Match all obj paths
		                                   NULL,  // Match all arg0s
		                                   G_DBUS_SIGNAL_FLAGS_NONE,
		                                   sn_watcher_monitor_bus,
		                                   self,
		                                   NULL);
}

static void
sn_watcher_name_acquired_handler(GDBusConnection *conn, const char *busname, void *data)
{
	SnWatcher *self = SN_WATCHER(data);

	GError *err = NULL;

	g_dbus_connection_emit_signal(self->conn,
	                              NULL,
	                              "/StatusNotifierWatcher",
	                              "org.kde.StatusNotifierWatcher",
	                              "StatusNotifierHostRegistered",
	                              NULL,
	                              &err);

	if (err) {
		g_warning("%s", err->message);
		g_error_free(err);
	}
}

static void
sn_watcher_name_lost_handler(GDBusConnection *conn, const char *busname, void *data)
{
	g_error("Could not acquire %s, maybe another instance is running?", busname);
	exit(-1);
}

static GObject*
sn_watcher_constructor(GType type,
                       unsigned int n_construct_properties,
                       GObjectConstructParam *construct_properties)
{
	static GObject *singleton = NULL;

	if (singleton == NULL) {
		singleton =
			G_OBJECT_CLASS(sn_watcher_parent_class)->constructor(type,
		                                                             n_construct_properties,
		                                                             construct_properties);
		g_object_add_weak_pointer(singleton, (void*)&singleton);

		return singleton;
	}

	return g_object_ref(singleton);
}


static void
sn_watcher_class_init(SnWatcherClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->constructor = sn_watcher_constructor;
	object_class->dispose = sn_watcher_dispose;

	signals[TRAYITEM_REGISTERED] = g_signal_new("trayitem-registered",
	                                            SN_TYPE_WATCHER,
	                                            G_SIGNAL_RUN_LAST,
	                                            0,
	                                            NULL, NULL,
	                                            NULL,
	                                            G_TYPE_NONE,
	                                            2,
	                                            G_TYPE_STRING,
	                                            G_TYPE_STRING);

	signals[TRAYITEM_UNREGISTERED] = g_signal_new("trayitem-unregistered",
	                                              SN_TYPE_WATCHER,
	                                              G_SIGNAL_RUN_LAST,
	                                              0,
	                                              NULL, NULL,
	                                              NULL,
	                                              G_TYPE_NONE,
	                                              1,
	                                              G_TYPE_STRING);
}

static void
sn_watcher_init(SnWatcher *self)
{
	self->owner_id =
		g_bus_own_name(G_BUS_TYPE_SESSION,
		               "org.kde.StatusNotifierWatcher",
		               G_BUS_NAME_OWNER_FLAGS_NONE,
		               sn_watcher_bus_acquired_handler,
		               sn_watcher_name_acquired_handler,
		               sn_watcher_name_lost_handler,
		               self,
		               NULL);

	g_debug("Created snwatcher");
}

static void
sn_watcher_dispose(GObject *obj)
{
	g_debug("Disposing snwatcher");
	SnWatcher *self = SN_WATCHER(obj);

	if (self->sig_sub_id > 0) {
		g_dbus_connection_signal_unsubscribe(self->conn, self->sig_sub_id);
		self->sig_sub_id = 0;
	}

	if (self->obj_reg_id > 0) {
		g_dbus_connection_unregister_object(self->conn, self->obj_reg_id);
		self->obj_reg_id = 0;
	}

	if (self->tracked_items) {
		sn_watcher_unregister_all(self);
		self->tracked_items = NULL;
	}

	if (self->owner_id > 0) {
		g_bus_unown_name(self->owner_id);
		self->owner_id = 0;
		self->conn = NULL;
	}

	G_OBJECT_CLASS(sn_watcher_parent_class)->dispose(obj);
}

SnWatcher*
sn_watcher_new(void)
{
	return g_object_new(SN_TYPE_WATCHER, NULL);
}
