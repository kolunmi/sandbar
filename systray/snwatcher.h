#ifndef SNWATCHER_H
#define SNWATCHER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define SN_TYPE_WATCHER sn_watcher_get_type()
G_DECLARE_FINAL_TYPE(SnWatcher, sn_watcher, SN, WATCHER, GObject)

SnWatcher	*sn_watcher_new		(void);

G_END_DECLS

#define STATUSNOTIFIERWATCHER_XML	\
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"	\
	"<node>\n"	\
	"    <interface name=\"org.kde.StatusNotifierWatcher\">\n"	\
	"        <!-- methods -->\n"	\
	"        <method name=\"RegisterStatusNotifierItem\">\n"	\
	"            <arg name=\"service\" type=\"s\" direction=\"in\" />\n"	\
	"        </method>\n"	\
	"        <!-- properties -->\n"	\
	"        <property name=\"IsStatusNotifierHostRegistered\" type=\"b\" access=\"read\" />\n"	\
	"        <property name=\"ProtocolVersion\" type=\"i\" access=\"read\" />\n"	\
	"        <property name=\"RegisteredStatusNotifierItems\" type=\"as\" access=\"read\" />\n"	\
	"        <!-- signals -->\n"	\
	"        <signal name=\"StatusNotifierItemRegistered\">\n"	\
	"            <arg type=\"s\"/>\n"	\
	"        </signal>\n"	\
	"        <signal name=\"StatusNotifierItemUnregistered\">\n"	\
	"            <arg type=\"s\"/>\n"	\
	"        </signal>\n"	\
	"        <signal name=\"StatusNotifierHostRegistered\">\n"	\
	"        </signal>\n"	\
	"    </interface>\n"	\
	"</node>\n"

#endif /* SNWATCHER_H */
