#ifndef SNITEM_H
#define SNITEM_H

#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SN_TYPE_ITEM sn_item_get_type()
G_DECLARE_FINAL_TYPE(SnItem, sn_item, SN, ITEM, GtkWidget)

SnItem*		sn_item_new			(const char *busname,
						const char *busobj,
						int iconsize);

char*		sn_item_get_busname		(SnItem *self);
gboolean	sn_item_get_popover_visible	(SnItem *self);
void		sn_item_set_menu_model		(SnItem *widget, GMenu *menu);
void		sn_item_set_actiongroup		(SnItem *self,
						const char *prefix,
						GSimpleActionGroup *group);
void		sn_item_clear_actiongroup	(SnItem *self, const char *prefix);
void		sn_item_clear_menu_model	(SnItem *widget);

G_END_DECLS

#define STATUSNOTIFIERITEM_XML	\
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"	\
	"<node>\n"	\
	"    <interface name=\"org.kde.StatusNotifierItem\">\n"	\
	"        <!-- methods -->\n"	\
	"        <method name=\"Activate\">\n"	\
	"            <arg name=\"x\" type=\"i\" direction=\"in\"/>\n"	\
	"            <arg name=\"y\" type=\"i\" direction=\"in\"/>\n"	\
	"        </method>\n"	\
	"        <!--\n"	\
	"        <method name=\"Scroll\">\n"	\
	"          <arg name=\"delta\" type=\"i\" direction=\"in\"/>\n"	\
	"          <arg name=\"orientation\" type=\"s\" direction=\"in\"/>\n"	\
	"        </method>\n"	\
	"        <method name=\"ContextMenu\">\n"	\
	"            <arg name=\"x\" type=\"i\" direction=\"in\"/>\n"	\
	"            <arg name=\"y\" type=\"i\" direction=\"in\"/>\n"	\
	"        </method>\n"	\
	"        <method name=\"SecondaryActivate\">\n"	\
	"            <arg name=\"x\" type=\"i\" direction=\"in\"/>\n"	\
	"            <arg name=\"y\" type=\"i\" direction=\"in\"/>\n"	\
	"        </method>\n"	\
	"        -->\n"	\
	"        <!-- properties -->\n"	\
	"        <property name=\"Menu\" type=\"o\" access=\"read\"/>\n"	\
	"        <property name=\"IconName\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"IconPixmap\" type=\"a(iiay)\" access=\"read\"/>\n"	\
	"        <property name=\"IconThemePath\" type=\"s\" access=\"read\"/>\n"	\
	"        <!--\n"	\
	"        <property name=\"OverlayIconName\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"OverlayIconPixmap\" type=\"a(iiay)\" access=\"read\"/>\n"	\
	"        <property name=\"AttentionIconName\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"AttentionIconPixmap\" type=\"a(iiay)\" access=\"read\"/>\n"	\
	"        <property name=\"Category\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"Id\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"Title\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"Status\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"WindowId\" type=\"i\" access=\"read\"/>\n"	\
	"        <property name=\"ItemIsMenu\" type=\"b\" access=\"read\"/>\n"	\
	"        <property name=\"AttentionMovieName\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"ToolTip\" type=\"(sa(iiay)ss)\" access=\"read\"/>\n"	\
	"        -->\n"	\
	"        <!-- signals -->\n"	\
	"        <signal name=\"NewIcon\"/>\n"	\
	"        <!--\n"	\
	"        <signal name=\"NewAttentionIcon\"/>\n"	\
	"        <signal name=\"NewOverlayIcon\"/>\n"	\
	"        <signal name=\"NewTitle\"/>\n"	\
	"        <signal name=\"NewToolTip\"/>\n"	\
	"        <signal name=\"NewStatus\">\n"	\
	"          <arg name=\"status\" type=\"s\"/>\n"	\
	"        </signal>\n"	\
	"        -->\n"	\
	"  </interface>\n"	\
	"</node>\n"

#endif /* SNITEM_H */
