#ifndef SNDBUSMENU_H
#define SNDBUSMENU_H

#include <glib-object.h>

#include "snitem.h"

G_BEGIN_DECLS

#define SN_TYPE_DBUSMENU sn_dbusmenu_get_type()
G_DECLARE_FINAL_TYPE(SnDbusmenu, sn_dbusmenu, SN, DBUSMENU, GObject);

SnDbusmenu*	sn_dbusmenu_new		(const char *busname,
					const char *busobj,
					SnItem *snitem);

G_END_DECLS

#define DBUSMENU_XML	\
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"	\
	"<node>\n"	\
	"    <interface name=\"com.canonical.dbusmenu\">\n"	\
	"        <!-- methods -->\n"	\
	"        <method name=\"GetLayout\">\n"	\
	"            <arg type=\"i\" name=\"parentId\" direction=\"in\"/>\n"	\
	"            <arg type=\"i\" name=\"recursionDepth\" direction=\"in\"/>\n"	\
	"            <arg type=\"as\" name=\"propertyNames\" direction=\"in\"/>\n"	\
	"            <arg type=\"u\" name=\"revision\" direction=\"out\"/>\n"	\
	"            <arg type=\"(ia{sv}av)\" name=\"layout\" direction=\"out\"/>\n"	\
	"        </method>\n"	\
	"        <method name=\"Event\">\n"	\
	"            <arg type=\"i\" name=\"id\" direction=\"in\"/>\n"	\
	"            <arg type=\"s\" name=\"eventId\" direction=\"in\"/>\n"	\
	"            <arg type=\"v\" name=\"data\" direction=\"in\"/>\n"	\
	"            <arg type=\"u\" name=\"timestamp\" direction=\"in\"/>\n"	\
	"        </method>\n"	\
	"        <method name=\"AboutToShow\">\n"	\
	"            <arg type=\"i\" name=\"id\" direction=\"in\"/>\n"	\
	"            <arg type=\"b\" name=\"needUpdate\" direction=\"out\"/>\n"	\
	"        </method>\n"	\
	"        <!--\n"	\
	"        <method name=\"AboutToShowGroup\">\n"	\
	"            <arg type=\"ai\" name=\"ids\" direction=\"in\"/>\n"	\
	"            <arg type=\"ai\" name=\"updatesNeeded\" direction=\"out\"/>\n"	\
	"            <arg type=\"ai\" name=\"idErrors\" direction=\"out\"/>\n"	\
	"        </method>\n"	\
	"        <method name=\"GetGroupProperties\">\n"	\
	"            <arg type=\"ai\" name=\"ids\" direction=\"in\"/>\n"	\
	"            <arg type=\"as\" name=\"propertyNames\" direction=\"in\"/>\n"	\
	"            <arg type=\"a(ia{sv})\" name=\"properties\" direction=\"out\"/>\n"	\
	"        </method>\n"	\
	"        <method name=\"GetProperty\">\n"	\
	"            <arg type=\"i\" name=\"id\" direction=\"in\"/>\n"	\
	"            <arg type=\"s\" name=\"name\" direction=\"in\"/>\n"	\
	"            <arg type=\"v\" name=\"value\" direction=\"out\"/>\n"	\
	"        </method>\n"	\
	"        <method name=\"EventGroup\">\n"	\
	"            <arg type=\"a(isvu)\" name=\"events\" direction=\"in\"/>\n"	\
	"            <arg type=\"ai\" name=\"idErrors\" direction=\"out\"/>\n"	\
	"        </method>\n"	\
	"        -->\n"	\
	"        <!-- properties -->\n"	\
	"        <!--\n"	\
	"        <property name=\"Version\" type=\"u\" access=\"read\"/>\n"	\
	"        <property name=\"TextDirection\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"Status\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"IconThemePath\" type=\"as\" access=\"read\"/>\n"	\
	"        -->\n"	\
	"        <!-- Signals -->\n"	\
	"        <signal name=\"ItemsPropertiesUpdated\">\n"	\
	"            <arg type=\"a(ia{sv})\" name=\"updatedProps\" direction=\"out\"/>\n"	\
	"            <arg type=\"a(ias)\" name=\"removedProps\" direction=\"out\"/>\n"	\
	"        </signal>\n"	\
	"        <signal name=\"LayoutUpdated\">\n"	\
	"            <arg type=\"u\" name=\"revision\" direction=\"out\"/>\n"	\
	"            <arg type=\"i\" name=\"parent\" direction=\"out\"/>\n"	\
	"        </signal>\n"	\
	"        <!--\n"	\
	"        <signal name=\"ItemActivationRequested\">\n"	\
	"            <arg type=\"i\" name=\"id\" direction=\"out\"/>\n"	\
	"            <arg type=\"u\" name=\"timestamp\" direction=\"out\"/>\n"	\
	"        </signal>\n"	\
	"        -->\n"	\
	"    </interface>\n"	\
	"</node>\n"

#endif /* SNDBUSMENU_H */
