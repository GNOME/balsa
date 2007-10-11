/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#include <string.h>
#include <libgnomevfs/gnome-vfs-mime-info.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include "config.h"
#include "balsa-app.h"
#include "balsa-icons.h"
#include "mime-stream-shared.h"
#include <glib/gi18n.h>
#include "balsa-mime-widget-image.h"
#include "balsa-mime-widget-message.h"
#include "balsa-mime-widget-multipart.h"
#include "balsa-mime-widget-text.h"
#include "balsa-mime-widget-callbacks.h"
#include "balsa-mime-widget-crypto.h"
#include "balsa-mime-widget.h"


/* object related functions */
static void balsa_mime_widget_init (GTypeInstance *instance, gpointer g_class);
static void balsa_mime_widget_class_init(BalsaMimeWidgetClass * klass);


/* fall-back widget (unknown/unsupported mime type) */
static BalsaMimeWidget *balsa_mime_widget_new_unknown(BalsaMessage * bm,
						      LibBalsaMessageBody *
						      mime_body,
						      const gchar *
						      content_type);

/* buttons to handle unknown mime types */
static GtkWidget *part_info_mime_button(LibBalsaMessageBody * mime_body,
					const gchar * content_type,
					const gchar * key);
static GtkWidget *part_info_mime_button_vfs(LibBalsaMessageBody *
					    mime_body,
					    const gchar * content_type);

static void vadj_change_cb(GtkAdjustment *vadj, GtkWidget *widget);


static GObjectClass *parent_class = NULL;


GType
balsa_mime_widget_get_type()
{
    static GType balsa_mime_widget_type = 0;

    if (!balsa_mime_widget_type) {
        static const GTypeInfo balsa_mime_widget_info = {
            sizeof(BalsaMimeWidgetClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) balsa_mime_widget_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(BalsaMimeWidget),
            0,                  /* n_preallocs */
            (GInstanceInitFunc) balsa_mime_widget_init
        };

        balsa_mime_widget_type =
            g_type_register_static(G_TYPE_OBJECT, "BalsaMimeWidget",
                                   &balsa_mime_widget_info, 0);
    }

    return balsa_mime_widget_type;
}


static void
balsa_mime_widget_init (GTypeInstance *instance, gpointer g_class)
{
  BalsaMimeWidget *self = (BalsaMimeWidget *)instance;

  self->widget = NULL;
  self->container = NULL;
}


static void
balsa_mime_widget_class_init(BalsaMimeWidgetClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    parent_class = g_type_class_ref(G_TYPE_OBJECT);
    object_class->finalize = balsa_mime_widget_destroy;
}


/* wildcard defined whether the mime_type matches all subtypes */
typedef struct _mime_delegate_t {
    gboolean wildcard;
    const gchar *mime_type;
    BalsaMimeWidget *(*handler) (BalsaMessage * bm,
				 LibBalsaMessageBody * mime_body,
				 const gchar * content_type,
				 gpointer data);
} mime_delegate_t;

static mime_delegate_t mime_delegate[] =
    { {FALSE,  "message/delivery-status",      balsa_mime_widget_new_text},
      {TRUE,  "message/",                      balsa_mime_widget_new_message},
      {TRUE,  "text/",                         balsa_mime_widget_new_text},
      {TRUE,  "multipart/",                    balsa_mime_widget_new_multipart},
      {TRUE,  "image/",                        balsa_mime_widget_new_image},
#ifdef HAVE_GPGME
      {FALSE, "application/pgp-signature",     balsa_mime_widget_new_signature},
      {FALSE, "application/pkcs7-signature",   balsa_mime_widget_new_signature},
      {FALSE, "application/x-pkcs7-signature", balsa_mime_widget_new_signature},
#endif
      {FALSE, NULL,         NULL}
    };


BalsaMimeWidget *
balsa_mime_widget_new(BalsaMessage * bm, LibBalsaMessageBody * mime_body, gpointer data)
{
    BalsaMimeWidget *mw = NULL;
    gchar *content_type;
    mime_delegate_t *delegate;

    g_return_val_if_fail(bm != NULL, NULL);
    g_return_val_if_fail(mime_body != NULL, NULL);

    /* determine the content type of the passed MIME body */
    content_type = libbalsa_message_body_get_mime_type(mime_body);
    delegate = mime_delegate;
    while (delegate->handler &&
	   ((delegate->wildcard &&
	     g_ascii_strncasecmp(delegate->mime_type, content_type,
				 strlen(delegate->mime_type))) ||
	    (!delegate->wildcard &&
	     g_ascii_strcasecmp(delegate->mime_type, content_type))))
	delegate++;

    if (delegate->handler)
	mw = (delegate->handler) (bm, mime_body, content_type, data);
    /* fall back to default if no handler is present */
    if (!mw)
	mw = balsa_mime_widget_new_unknown(bm, mime_body, content_type);

    if (mw) {
	if (mw->widget) {
	    g_signal_connect(G_OBJECT(mw->widget), "focus_in_event",
			     G_CALLBACK(balsa_mime_widget_limit_focus),
			     (gpointer) bm);
	    g_signal_connect(G_OBJECT(mw->widget), "focus_out_event",
			     G_CALLBACK(balsa_mime_widget_unlimit_focus),
			     (gpointer) bm);
#ifdef HAVE_GPGME
	    if (mime_body->sig_info &&
		g_ascii_strcasecmp("application/pgp-signature", content_type) &&
		g_ascii_strcasecmp("application/pkcs7-signature", content_type) &&
		g_ascii_strcasecmp("application/x-pkcs7-signature", content_type)) {
		GtkWidget * signature = 
		    balsa_mime_widget_signature_widget(mime_body, content_type);
		mw->widget = balsa_mime_widget_crypto_frame(mime_body, mw->widget,
							    mime_body->was_encrypted,
							    signature);
	    }
#endif
#if GLIB_CHECK_VERSION(2, 10, 0)
            g_object_ref_sink(mw->widget);
#else                           /* GLIB_CHECK_VERSION(2, 10, 0) */
            g_object_ref(G_OBJECT(mw->widget));
            gtk_object_sink(GTK_OBJECT(mw->widget));
#endif                          /* GLIB_CHECK_VERSION(2, 10, 0) */

	    if (GTK_IS_LAYOUT(mw->widget)) 
		g_signal_connect(G_OBJECT(gtk_layout_get_vadjustment(GTK_LAYOUT(mw->widget))),
				 "changed",
				 G_CALLBACK(vadj_change_cb), mw->widget);
	}
    }
    g_free(content_type);
    
    gtk_widget_show_all(mw->widget);

    return mw;
}


void
balsa_mime_widget_destroy(GObject * object)
{
    BalsaMimeWidget * mime_widget = BALSA_MIME_WIDGET(object);

    if (mime_widget->container && mime_widget->container != mime_widget->widget)
	gtk_widget_destroy(mime_widget->container);
    mime_widget->container = NULL;
    if (mime_widget->widget)
	gtk_widget_destroy(mime_widget->widget);
    mime_widget->widget = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}


static BalsaMimeWidget *
balsa_mime_widget_new_unknown(BalsaMessage * bm,
			      LibBalsaMessageBody * mime_body,
			      const gchar * content_type)
{
    GtkWidget *hbox;
    GtkWidget *button = NULL;
    gchar *msg;
    const gchar *content_desc;
    BalsaMimeWidget *mw;
    gchar *use_content_type;

    g_return_val_if_fail(mime_body, NULL);
    mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);

    mw->widget = gtk_vbox_new(FALSE, BMW_VBOX_SPACE);
    gtk_container_set_border_width(GTK_CONTAINER(mw->widget),
				   BMW_CONTAINER_BORDER);

    if (mime_body->filename) {
	msg = g_strdup_printf(_("File name: %s"), mime_body->filename);
	gtk_box_pack_start(GTK_BOX(mw->widget), gtk_label_new(msg), FALSE,
			   FALSE, 0);
	g_free(msg);
    }

    /* guess content_type if not specified or if generic app/octet-stream */
    /* on local mailboxes only, to avoid possibly long downloads */
    if ((content_type == NULL ||
	 g_ascii_strcasecmp(content_type, "application/octet-stream") == 0)
	&& LIBBALSA_IS_MAILBOX_LOCAL(mime_body->message->mailbox)) {
        GError *err = NULL;
	ssize_t length = 1024 /* g_mime_stream_length(stream) */ ;
	gpointer buffer;
	ssize_t size;
	GMimeStream *stream = 
            libbalsa_message_body_get_stream(mime_body, &err);
        if(!stream) {
            libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                             _("Error reading message part: %s"),
                             err ? err->message : "Unknown error");
            g_clear_error(&err);
            use_content_type = g_strdup(content_type);
        } else {
            buffer = g_malloc(length);
            libbalsa_mime_stream_shared_lock(stream);
            size = g_mime_stream_read(stream, buffer, length);
            libbalsa_mime_stream_shared_unlock(stream);
            g_object_unref(stream);
            use_content_type =
                g_strdup(gnome_vfs_get_mime_type_for_data(buffer, size));
            if (g_ascii_strncasecmp(use_content_type, "text", 4) == 0
                && libbalsa_text_attr_string(buffer) & LIBBALSA_TEXT_HI_BIT) {
                /* Hmmm...better stick with application/octet-stream. */
                g_free(use_content_type);
                use_content_type = g_strdup("application/octet-stream");
            }
            g_free(buffer);
        }
    } else
	use_content_type = g_strdup(content_type);

    if ((content_desc = gnome_vfs_mime_get_description(use_content_type)))
	msg = g_strdup_printf(_("Type: %s (%s)"), content_desc,
			      use_content_type);
    else
	msg = g_strdup_printf(_("Content Type: %s"), use_content_type);
    gtk_box_pack_start(GTK_BOX(mw->widget), gtk_label_new(msg), FALSE,
		       FALSE, 0);
    g_free(msg);

    hbox = gtk_hbox_new(TRUE, BMW_HBOX_SPACE);
    if ((button = part_info_mime_button_vfs(mime_body, use_content_type))
	|| (button =
	    part_info_mime_button(mime_body, use_content_type, "view"))
	|| (button =
	    part_info_mime_button(mime_body, use_content_type, "open")))
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
    else
	gtk_box_pack_start(GTK_BOX(mw->widget),
			   gtk_label_new(_("No open or view action "
					   "defined in GNOME MIME "
					   "for this content type")),
			   FALSE, FALSE, 0);
    g_free(use_content_type);

    button = gtk_button_new_with_mnemonic(_("S_ave part"));
    gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(balsa_mime_widget_ctx_menu_save),
		     (gpointer) mime_body);

    gtk_box_pack_start(GTK_BOX(mw->widget), hbox, FALSE, FALSE, 0);

    return mw;
}


static GtkWidget *
part_info_mime_button(LibBalsaMessageBody * mime_body,
		      const gchar * content_type, const gchar * key)
{
    GtkWidget *button = NULL;
    gchar *msg;
    const gchar *cmd =
	gnome_vfs_mime_get_value(content_type, (char *) key);

    if (cmd) {
	msg = g_strdup_printf(_("View _part with %s"), cmd);
	button = gtk_button_new_with_mnemonic(msg);
	g_object_set_data(G_OBJECT(button), "mime_action", (gpointer) key);
	g_free(msg);

	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(balsa_mime_widget_ctx_menu_cb),
			 (gpointer) mime_body);
    }

    return button;
}


static GtkWidget *
part_info_mime_button_vfs(LibBalsaMessageBody * mime_body,
			  const gchar * content_type)
{
    GtkWidget *button = NULL;
    gchar *msg;
    GnomeVFSMimeApplication *app =
	gnome_vfs_mime_get_default_application(content_type);

    if (app) {
	msg = g_strdup_printf(_("View _part with %s"), app->name);
	button = gtk_button_new_with_mnemonic(msg);
	g_object_set_data_full(G_OBJECT(button), "mime_action",
			       (gpointer) g_strdup(app->id), g_free);
	g_free(msg);

	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(balsa_mime_widget_ctx_menu_vfs_cb),
			 (gpointer) mime_body);

	gnome_vfs_mime_application_free(app);

    }
    return button;
}


static gint resize_idle_id;

static GtkWidget *old_widget, *new_widget;
static gdouble old_upper, new_upper;

static gboolean
resize_idle(GtkWidget * widget)
{
    gdk_threads_enter();
    resize_idle_id = 0;
    gtk_widget_queue_resize(widget);
    old_widget = new_widget;
    old_upper = new_upper;
    gdk_threads_leave();

    return FALSE;
}


void
balsa_mime_widget_schedule_resize(GtkWidget * widget)
{
    g_object_ref(widget);
    resize_idle_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                                     (GSourceFunc) resize_idle,
                                     widget, g_object_unref);
}


static void 
vadj_change_cb(GtkAdjustment *vadj, GtkWidget *widget)
{
    gdouble upper = vadj->upper;

    /* do nothing if it's the same widget and the height hasn't changed
     *
     * an HtmlView widget seems to grow by 4 pixels each time we resize
     * it, whence the following unobvious test: */
    if (widget == old_widget
        && upper >= old_upper && upper <= old_upper + 4)
        return;
    new_widget = widget;
    new_upper = upper;
    if (resize_idle_id) 
        g_source_remove(resize_idle_id);
    balsa_mime_widget_schedule_resize(widget);
}
