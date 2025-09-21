#ifndef SNHOST_H
#define SNHOST_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SN_TYPE_HOST sn_host_get_type()
G_DECLARE_FINAL_TYPE(SnHost, sn_host, SN, HOST, GtkWindow)

SnHost	*sn_host_new		(int defaultwidth,
				int defaultheight,
				int iconsize,
				int margins,
				int spacing,
				const char *conn);

G_END_DECLS

#endif /* SNHOST_H */
