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

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <iconv.h>
#include <libgnomevfs/gnome-vfs-mime-info.h>

#include "balsa-app.h"
#include "balsa-message.h"
#include "mime.h"
#include "misc.h"

/*
#include <libmutt/mutt.h>
#include <libmutt/mime.h>
*/

#include <gdk-pixbuf/gdk-pixbuf.h>

#ifdef HAVE_GTKHTML
#include <libgtkhtml/gtkhtml.h>
#endif

#ifdef HAVE_PCRE
#  include <pcreposix.h>
#else
#  include <sys/types.h>
#  include <regex.h>
#endif

#include "quote-color.h"
#include "sendmsg-window.h"

#include <libgnomevfs/gnome-vfs-mime-handlers.h>

enum {
    SELECT_PART,
    LAST_SIGNAL,
};

struct _BalsaPartInfo {
    LibBalsaMessage *message;
    LibBalsaMessageBody *body;

    /* The widget to add to the container; referenced */
    GtkWidget *widget;

    /* The widget to give focus to; just an pointer */
    GtkWidget *focus_widget;

    /* The contect menu; referenced */
    GtkWidget *popup_menu;

    /* True if balsa knows how to display this part */
    gboolean can_display;
};

static gint balsa_message_signals[LAST_SIGNAL];

/* widget */
static void balsa_message_class_init(BalsaMessageClass * klass);
static void balsa_message_init(BalsaMessage * bm);

static void balsa_message_destroy(GtkObject * object);

static gint balsa_message_focus_in_part(GtkWidget * widget,
					GdkEventFocus * event,
					BalsaMessage * bm);
static gint balsa_message_focus_out_part(GtkWidget * widget,
					 GdkEventFocus * event,
					 BalsaMessage * bm);

static gint balsa_message_key_press_event(GtkWidget * widget,
					  GdkEventKey * event,
					  BalsaMessage * bm);

static void bm_message_weak_ref_cb(BalsaMessage * bm,
                                   LibBalsaMessage * message);

static void display_headers(BalsaMessage * bm);
static void display_content(BalsaMessage * bm);

static void display_part(BalsaMessage * bm, LibBalsaMessageBody * body);
static void display_multipart(BalsaMessage * bm,
			      LibBalsaMessageBody * body);

static void save_part(BalsaPartInfo * info);

static void select_icon_cb(GnomeIconList * ilist, gint num,
			   GdkEventButton * event, BalsaMessage * bm);
static gboolean bm_popup_menu_cb(GtkWidget * widget, gpointer data);
static gboolean bm_do_popup(GnomeIconList * ilist, GdkEventButton * event);
static BalsaPartInfo *add_part(BalsaMessage *bm, gint part);
static void add_multipart(BalsaMessage *bm, LibBalsaMessageBody *parent);
static void select_part(BalsaMessage * bm, gint part);
static void part_context_menu_save(GtkWidget * menu_item,
				   BalsaPartInfo * info);
/* static void part_context_menu_view(GtkWidget * menu_item, */
/* 				   BalsaPartInfo * info); */

static void add_header_gchar(BalsaMessage * bm, const gchar *header,
			     const gchar *label, const gchar *value);
static void add_header_glist(BalsaMessage * bm, gchar * header,
			     gchar * label, GList * list);

static void scroll_set(GtkAdjustment * adj, gint value);
static void scroll_change(GtkAdjustment * adj, gint diff);

#ifdef HAVE_GTKHTML
static void balsa_gtk_html_size_request(GtkWidget * widget,
					GtkRequisition * requisition,
					gpointer data);
static void balsa_gtk_html_link_clicked(GObject * obj, 
					const gchar *url);
#endif
static void balsa_gtk_html_on_url(GtkWidget *html, const gchar *url);
static void balsa_icon_list_size_request(GtkWidget * widget,
					 GtkRequisition * requisition,
					 gpointer data);

static void part_info_init(BalsaMessage * bm, BalsaPartInfo * info);
static void part_info_init_image(BalsaMessage * bm, BalsaPartInfo * info);
static void part_info_init_other(BalsaMessage * bm, BalsaPartInfo * info);
static void part_info_init_mimetext(BalsaMessage * bm,
				    BalsaPartInfo * info);
static void part_info_init_video(BalsaMessage * bm, BalsaPartInfo * info);
static void part_info_init_message(BalsaMessage * bm,
				   BalsaPartInfo * info);
static void part_info_init_application(BalsaMessage * bm,
				       BalsaPartInfo * info);
static void part_info_init_audio(BalsaMessage * bm, BalsaPartInfo * info);
static void part_info_init_model(BalsaMessage * bm, BalsaPartInfo * info);
static void part_info_init_unknown(BalsaMessage * bm,
				   BalsaPartInfo * info);
#ifdef HAVE_GTKHTML
static void part_info_init_html(BalsaMessage * bm, BalsaPartInfo * info,
				gchar * ptr, size_t len);
#endif
static GtkWidget* part_info_mime_button_vfs (BalsaPartInfo* info, const gchar* content_type);
static GtkWidget* part_info_mime_button (BalsaPartInfo* info, const gchar* content_type, const gchar* key);
static void part_context_menu_call_url(GtkWidget * menu_item, BalsaPartInfo * info);
static void part_context_menu_mail(GtkWidget * menu_item, BalsaPartInfo * info);
static void part_context_menu_cb(GtkWidget * menu_item, BalsaPartInfo * info);
static void part_context_menu_vfs_cb(GtkWidget * menu_item, BalsaPartInfo * info);
static void part_create_menu (BalsaPartInfo* info);

static GtkViewportClass *parent_class = NULL;

/* stuff needed for sending Message Disposition Notifications */
static gboolean rfc2298_address_equal(LibBalsaAddress *a, LibBalsaAddress *b);
static void handle_mdn_request(LibBalsaMessage *message);
static LibBalsaMessage *create_mdn_reply (LibBalsaMessage *for_msg, gboolean manual);
static GtkWidget* create_mdn_dialog (gchar *sender, gchar *mdn_to_address,
				     LibBalsaMessage *send_msg);
static void mdn_dialog_response(GtkWidget * dialog, gint response,
                                gpointer user_data);

static BalsaPartInfo* part_info_new(LibBalsaMessageBody* body,
				    LibBalsaMessage* msg);
static void part_info_free(BalsaPartInfo* info);
static GtkTextTag * quote_tag(GtkTextBuffer * buffer, gint level);
static gboolean resize_idle(GtkWidget * widget);

GtkType
balsa_message_get_type()
{
    static GtkType balsa_message_type = 0;

    if (!balsa_message_type) {
	static const GTypeInfo balsa_message_info = {
	    sizeof(BalsaMessageClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) balsa_message_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(BalsaMessage),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) balsa_message_init
	};

	balsa_message_type =
	    g_type_register_static(GTK_TYPE_VIEWPORT, "BalsaMessage",
                                   &balsa_message_info, 0);
    }

    return balsa_message_type;
}


static void
balsa_message_class_init(BalsaMessageClass * klass)
{
    GtkObjectClass *object_class;

    object_class = GTK_OBJECT_CLASS(klass);

    balsa_message_signals[SELECT_PART] =
	g_signal_new("select-part",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(BalsaMessageClass, select_part),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
		     G_TYPE_NONE, 0);

    object_class->destroy = balsa_message_destroy;

    parent_class = gtk_type_class(gtk_viewport_get_type());

    klass->select_part = NULL;

}

static void
balsa_message_init(BalsaMessage * bm)
{
    /* The vbox widget */
    bm->vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_add(GTK_CONTAINER(bm), bm->vbox);
    gtk_widget_show(bm->vbox);

    /* Widget to hold headers */
    bm->header_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(bm->header_text), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(bm->header_text), 2);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(bm->header_text), 2);
    g_signal_connect(G_OBJECT(bm->header_text), "key_press_event",
		     G_CALLBACK(balsa_message_key_press_event),
		     (gpointer) bm);
    gtk_box_pack_start(GTK_BOX(bm->vbox), bm->header_text, FALSE, FALSE, 0);

    /* Widget to hold content */
    bm->content = gtk_vbox_new(FALSE, 1);
    gtk_box_pack_start(GTK_BOX(bm->vbox), bm->content, TRUE, TRUE, 0);
    gtk_widget_show(bm->content);

    /* Widget to hold icons */
    bm->part_list = gnome_icon_list_new(100, NULL, FALSE);
    gnome_icon_list_set_selection_mode(GNOME_ICON_LIST(bm->part_list),
				       GTK_SELECTION_MULTIPLE);
    bm->select_icon_handler =
        g_signal_connect(G_OBJECT(bm->part_list), "select_icon",
		         G_CALLBACK(select_icon_cb), bm);
    g_signal_connect(G_OBJECT(bm->part_list), "popup-menu",
                     G_CALLBACK(bm_popup_menu_cb), NULL);
    g_signal_connect(G_OBJECT(bm->part_list), "size_request",
		     G_CALLBACK(balsa_icon_list_size_request),
		     (gpointer) bm);
    gtk_box_pack_end(GTK_BOX(bm->vbox), bm->part_list, FALSE, FALSE, 0);

    bm->current_part = NULL;
    bm->message = NULL;

    bm->wrap_text = balsa_app.browse_wrap;
    bm->shown_headers = balsa_app.shown_headers;

}

static void
balsa_message_destroy(GtkObject * object)
{
    BalsaMessage* bm = BALSA_MESSAGE(object);
    if (bm->part_list) {
        balsa_message_set(bm, NULL);
        gtk_widget_destroy(bm->part_list);
        bm->part_list = NULL;
    }

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}

static gint
balsa_message_focus_in_part(GtkWidget * widget, GdkEventFocus * event,
			    BalsaMessage * bm)
{
    g_return_val_if_fail(widget != NULL, FALSE);
    g_return_val_if_fail(bm != NULL, FALSE);
    g_return_val_if_fail(BALSA_IS_MESSAGE(bm), FALSE);

    bm->content_has_focus = TRUE;

    return FALSE;
}

static gint
balsa_message_focus_out_part(GtkWidget * widget, GdkEventFocus * event,
			     BalsaMessage * bm)
{
    g_return_val_if_fail(widget != NULL, FALSE);
    g_return_val_if_fail(bm != NULL, FALSE);
    g_return_val_if_fail(BALSA_IS_MESSAGE(bm), FALSE);

    bm->content_has_focus = FALSE;

    return FALSE;

}

static void
save_dialog_ok(GtkWidget* save_dialog, BalsaPartInfo * info)
{
    const gchar *filename;
    gboolean do_save, result;

    gtk_widget_hide(save_dialog); 
    filename 
	= gtk_file_selection_get_filename(GTK_FILE_SELECTION(save_dialog));
    
    g_free(balsa_app.save_dir);
    balsa_app.save_dir = g_path_get_dirname(filename);
    
    if ( access(filename, F_OK) == 0 ) {
	GtkWidget *confirm;
	
	/* File exists. check if they really want to overwrite */
	confirm = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window),
                                         GTK_DIALOG_MODAL,
                                         GTK_MESSAGE_QUESTION,
                                         GTK_BUTTONS_YES_NO,
                                         _("File already exists. Overwrite?"));
	do_save = (gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_YES);
        gtk_widget_destroy(confirm);
	if(do_save)
	    unlink(filename);
    } else
	do_save = TRUE;
    
    if ( do_save ) {
	result = libbalsa_message_body_save(info->body, NULL,
                /* FIXME: change arg 3 of libbalsa_message_body_save to
                 * const gchar *, to avoid this ugly cast: */
                                            (gchar *) filename);
	if (!result) {
	    gchar *msg;
	    GtkWidget *msgbox;
	    
	    msg = g_strdup_printf(_(" Could not save %s: %s"), 
				  filename, strerror(errno));
	    msgbox = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window),
                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_ERROR,
                                            GTK_BUTTONS_CLOSE,
                                            msg);
	    g_free(msg);
	    gtk_dialog_run(GTK_DIALOG(msgbox));
            gtk_widget_destroy(msgbox);
	}
    }
}

static void
save_part(BalsaPartInfo * info)
{
    gchar *filename;
    GtkWidget *save_dialog;
    
    g_return_if_fail(info != 0);

    save_dialog = gtk_file_selection_new(_("Save MIME Part"));
    gtk_window_set_wmclass(GTK_WINDOW(save_dialog), "save_part", "Balsa");

    if (balsa_app.save_dir)
	filename = g_strdup_printf("%s/%s", balsa_app.save_dir,
				   info->body->filename 
				   ? info->body->filename : "");
    else if(!balsa_app.save_dir && info->body->filename)
	filename = g_strdup(info->body->filename);
    else filename = NULL;

    if (filename) {
	gtk_file_selection_set_filename(GTK_FILE_SELECTION(save_dialog),
                                        filename);
	g_free(filename);
    }

    gtk_window_set_transient_for(GTK_WINDOW(save_dialog),
				 GTK_WINDOW(balsa_app.main_window));
    gtk_window_set_modal(GTK_WINDOW(save_dialog), TRUE);
    if(gtk_dialog_run(GTK_DIALOG(save_dialog)) == GTK_RESPONSE_OK)
        save_dialog_ok(save_dialog, info);
    gtk_widget_destroy(save_dialog);
}

GtkWidget *
balsa_message_new(void)
{
    BalsaMessage *bm;

    bm = g_object_new(BALSA_TYPE_MESSAGE, NULL);

    return GTK_WIDGET(bm);
}

static void
select_icon_cb(GnomeIconList * ilist, gint num, GdkEventButton * event,
	       BalsaMessage * bm)
{
    select_part(bm, num);

    if (event && event->type == GDK_BUTTON_PRESS && event->button == 3)
        bm_do_popup(ilist, event);
}

/* bm_popup_menu_cb:
 * callback for the "popup-menu" signal, which is issued when the user
 * hits shift-F10
 */
static gboolean
bm_popup_menu_cb(GtkWidget * widget, gpointer data)
{
    return bm_do_popup(GNOME_ICON_LIST(widget), NULL);
}

static gboolean
bm_do_popup(GnomeIconList * ilist, GdkEventButton * event)
{
    GList *list;
    gint num;
    BalsaPartInfo *info;

    if (!(list = gnome_icon_list_get_selection(ilist)))
        return FALSE;

    num = GPOINTER_TO_INT(list->data);
    info = (BalsaPartInfo *) gnome_icon_list_get_icon_data(ilist, num);
    g_assert(info != NULL);
    if (info->popup_menu) {
        gint event_button;
        guint event_time;

        if (event) {
            event_button = event->button;
            event_time = event->time;
        } else {
            event_button = 0;
            event_time = gtk_get_current_event_time();
        }
        gtk_menu_popup(GTK_MENU(info->popup_menu), NULL, NULL, NULL, NULL,
                       event_button, event_time);
        return TRUE;
    }
    return FALSE;
}

static void
bm_message_weak_ref_cb(BalsaMessage * bm, LibBalsaMessage * message)
{
    if (bm->message == message) {
        bm->message = NULL;
	balsa_message_set(bm, NULL);
    }
}

/* balsa_message_set:
   returns TRUE on success, FALSE on failure (message content could not be
   accessed).

   if message == NULL, clears the display and returns TRUE
*/
gboolean
balsa_message_set(BalsaMessage * bm, LibBalsaMessage * message)
{
    gboolean is_new;
    gint part_count;

    g_return_val_if_fail(bm != NULL, FALSE);

    /* Leave this out. When settings (eg wrap) are changed it is OK to 
       call message_set with the same messagr */
    /*    if (bm->message == message) */
    /*      return; */


    select_part(bm, -1);
    if (bm->message != NULL) {
        g_object_weak_unref(G_OBJECT(bm->message),
	   	            (GWeakNotify) bm_message_weak_ref_cb,
		            (gpointer) bm);
	libbalsa_message_body_unref(bm->message);
        bm->message = NULL;
    }
    gnome_icon_list_clear(GNOME_ICON_LIST(bm->part_list));

    if (message == NULL) {
	gtk_widget_hide(bm->header_text);
	gtk_widget_hide(bm->part_list);
	return TRUE;
    }

    bm->message = message;

    g_object_weak_ref(G_OBJECT(message),
		      (GWeakNotify) bm_message_weak_ref_cb,
		      (gpointer) bm);

    is_new = message->flags & LIBBALSA_MESSAGE_FLAG_NEW;
    if(!libbalsa_message_body_ref(bm->message)) 
	return FALSE;

    display_headers(bm);
    display_content(bm);

    /*
     * At this point we check if (a) a message was new (its not new
     * any longer) and (b) a Disposition-Notification-To header line is
     * present.
     *
     */
    if (is_new && message->dispnotify_to)
	handle_mdn_request (message);

    /*
     * FIXME: This is a workaround for what may or may not be a libmutt bug.
     *
     * If the Content-Type: header is  multipart/alternative; boundary="XXX" 
     * and no parts are found then mutt produces a message with no parts, even 
     * if there is a single unmarked part (ie a normal email).
     */
    part_count =
        gnome_icon_list_get_num_icons(GNOME_ICON_LIST(bm->part_list));
    if (part_count == 0) {
	gtk_widget_hide(bm->part_list);

	/* This is really annoying if you are browsing, since you keep
           getting a dialog... */
	/* balsa_information(LIBBALSA_INFORMATION_WARNING, _("Message
           contains no parts!")); */
	return TRUE;
    }

    gnome_icon_list_select_icon(GNOME_ICON_LIST(bm->part_list), 0);

    /* We show the part list if:
     *    there is > 1 part
     * or we don't know how to display the one part.
     */
    if (part_count > 1) {
	gtk_widget_show(bm->part_list);
    } else {
	BalsaPartInfo *info = (BalsaPartInfo *)
	    gnome_icon_list_get_icon_data(GNOME_ICON_LIST(bm->part_list),
					  0);
	g_assert( info != NULL );

	if (info->can_display)
	    gtk_widget_hide(bm->part_list);
	else
	    gtk_widget_show(bm->part_list);
    }

    return TRUE;
}

void
balsa_message_save_current_part(BalsaMessage * bm)
{
    g_return_if_fail(bm != NULL);

    if (bm->current_part)
	save_part(bm->current_part);
}

void
balsa_message_set_displayed_headers(BalsaMessage * bmessage,
				    ShownHeaders sh)
{
    g_return_if_fail(bmessage != NULL);
    g_return_if_fail(sh >= HEADERS_NONE && sh <= HEADERS_ALL);

    bmessage->shown_headers = sh;

    if (bmessage->message)
	display_headers(bmessage);

}

void
balsa_message_set_wrap(BalsaMessage * bm, gboolean wrap)
{
    g_return_if_fail(bm != NULL);

    bm->wrap_text = wrap;

    /* This is easier than reformating all the widgets... */
    if (bm->message) {
	LibBalsaMessage *msg = bm->message;
	balsa_message_set(bm, msg);
    }
}

/* This function should split \n into separate lines. */
static void
add_header_gchar(BalsaMessage * bm, const gchar *header, const gchar *label,
		 const gchar *value)
{
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bm->header_text));
    GtkTextIter insert;
    GtkTextTag *tag = NULL;
    gchar pad[] = "                ";
    gchar cr[] = "\n";
    gchar *line_start, *line_end;
    gchar *wrapped_value;
    if (!(bm->shown_headers == HEADERS_ALL || 
          libbalsa_find_word(header, balsa_app.selected_headers))) 
	return;

    /* always display the label in the predefined font */
    if (strcmp(header, "subject") == 0)
        tag = gtk_text_buffer_create_tag(buffer, NULL,
                                         "font", balsa_app.subject_font,
                                         NULL);

    gtk_text_buffer_get_iter_at_mark(buffer, &insert, 
                                     gtk_text_buffer_get_insert(buffer));
    gtk_text_buffer_insert_with_tags(buffer, &insert,
                                     label, -1, 
                                     tag, NULL);
    
    if (value && *value != '\0') {
        gint pad_chars = 15 - strlen(label);

        gtk_text_buffer_insert_with_tags(buffer, &insert,
                                         pad, MAX(1, pad_chars),
                                         tag, NULL);

	wrapped_value = g_strdup(value);
	libbalsa_wrap_string(wrapped_value, balsa_app.wraplength - 15);
        libbalsa_utf8_sanitize(wrapped_value);

	/* We must insert the first line. Each subsequent line must be indented 
	   by 15 spaces. So we need to rewrap lines 2+
	 */
	line_end = wrapped_value;
	while (*line_end != '\0') {
	    line_start = line_end;
	    line_end++;
	    while (*line_end != '\0' && *line_end != '\n')
		line_end++;

	    if (line_start != wrapped_value)
                gtk_text_buffer_insert_with_tags(buffer, &insert,
                                                 pad, 15,
                                                 tag, NULL);
            gtk_text_buffer_insert_with_tags(buffer, &insert, 
                                             line_start,
                                             line_end - line_start, 
                                             tag, NULL);
            gtk_text_buffer_insert(buffer, &insert, cr, 1);
	    if (*line_end != '\0')
		line_end++;
	}
	g_free(wrapped_value);
    } else {
        gtk_text_buffer_insert(buffer, &insert, cr, 1);
    }
}

static void
add_header_glist(BalsaMessage * bm, gchar * header, gchar * label,
		 GList * list)
{
    gchar *value;

    if (list == NULL)
	return;

    if (!(bm->shown_headers == HEADERS_ALL || libbalsa_find_word(header, balsa_app.selected_headers))) 
	return;

    value = libbalsa_make_string_from_list(list);

    add_header_gchar(bm, header, label, value);

    g_free(value);
}

static void
display_headers(BalsaMessage * bm)
{
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bm->header_text));
    LibBalsaMessage *message = bm->message;
    GList *p, *lst;
    gchar **pair, *hdr;
    gchar *date;

    gtk_text_buffer_set_text(buffer, "", 0);
 
    if (bm->shown_headers == HEADERS_NONE) {
	gtk_widget_hide(bm->header_text);
	return;
    } else {
	gtk_widget_show(bm->header_text);
    }

    add_header_gchar(bm, "subject", _("Subject:"), 
		     LIBBALSA_MESSAGE_GET_SUBJECT(message));

    date = libbalsa_message_date_to_gchar(message, balsa_app.date_string);
    add_header_gchar(bm, "date", _("Date:"), date);
    g_free(date);

    if (message->from) {
	gchar *from = libbalsa_address_to_gchar(message->from, 0);
	add_header_gchar(bm, "from", _("From:"), from);
	g_free(from);
    }

    if (message->reply_to) {
	gchar *reply_to = libbalsa_address_to_gchar(message->reply_to, 0);
	add_header_gchar(bm, "reply-to", _("Reply-To:"), reply_to);
	g_free(reply_to);
    }
    add_header_glist(bm, "to", _("To:"), message->to_list);
    add_header_glist(bm, "cc", _("Cc:"), message->cc_list);
    add_header_glist(bm, "bcc", _("Bcc:"), message->bcc_list);

    if (message->fcc_url)
	add_header_gchar(bm, "fcc", _("Fcc:"), message->fcc_url);

    if (message->dispnotify_to) {
	gchar *mdn_to = libbalsa_address_to_gchar(message->dispnotify_to, 0);
	add_header_gchar(bm, "disposition-notification-to", 
			 _("Disposition-Notification-To:"), mdn_to);
	g_free(mdn_to);
    }

    /* remaining headers */
    lst = libbalsa_message_user_hdrs(message);
    for (p = g_list_first(lst); p; p = g_list_next(p)) {
	pair = p->data;
	hdr = g_strconcat(pair[0], ":", NULL);
	add_header_gchar(bm, pair[0], hdr, pair[1]);
	g_free(hdr);
	g_strfreev(pair);
    }
    g_list_free(lst);

    gtk_widget_queue_resize(GTK_WIDGET(bm->header_text));

}


static void
part_info_init_model(BalsaMessage * bm, BalsaPartInfo * info)
{
    g_print("TODO: part_info_init_model\n");
    part_info_init_unknown(bm, info);
}

static void
part_info_init_other(BalsaMessage * bm, BalsaPartInfo * info)
{
    g_print("TODO: part_info_init_other\n");
    part_info_init_unknown(bm, info);
}

static void
part_info_init_audio(BalsaMessage * bm, BalsaPartInfo * info)
{
    g_print("TODO: part_info_init_audio\n");
    part_info_init_unknown(bm, info);
}

static void
part_info_init_application(BalsaMessage * bm, BalsaPartInfo * info)
{
    g_print("TODO: part_info_init_application\n");
    part_info_init_unknown(bm, info);
}

static void
part_info_init_image(BalsaMessage * bm, BalsaPartInfo * info)
{
    GtkWidget *image;

    libbalsa_message_body_save_temporary(info->body, NULL);
    image = gtk_image_new_from_file(info->body->temp_filename);
    info->widget = image;
    info->focus_widget = image;
    info->can_display = TRUE;
}

typedef enum _rfc_extbody_t {
    RFC2046_EXTBODY_FTP,
    RFC2046_EXTBODY_ANONFTP,
    RFC2046_EXTBODY_TFTP,
    RFC2046_EXTBODY_LOCALFILE,
    RFC2046_EXTBODY_MAILSERVER,
    RFC2017_EXTBODY_URL,
    RFC2046_EXTBODY_UNKNOWN
} rfc_extbody_t;

typedef struct _rfc_extbody_id {
    gchar *id_string;
    rfc_extbody_t action;
} rfc_extbody_id;

static rfc_extbody_id rfc_extbodys[] = {
    { "ftp",         RFC2046_EXTBODY_FTP },
    { "anon-ftp",    RFC2046_EXTBODY_ANONFTP },
    { "tftp",        RFC2046_EXTBODY_TFTP},
    { "local-file",  RFC2046_EXTBODY_LOCALFILE},
    { "mail-server", RFC2046_EXTBODY_MAILSERVER},
    { "URL",         RFC2017_EXTBODY_URL}, 
    { NULL,          RFC2046_EXTBODY_UNKNOWN}};

static void
part_info_init_message_extbody_url(BalsaMessage * bm, BalsaPartInfo * info,
				   rfc_extbody_t url_type)
{
    GtkWidget *vbox;
    GtkWidget *button;
    GString *msg = NULL;
    gchar *url;

    if (url_type == RFC2046_EXTBODY_LOCALFILE) {
	gchar *local_name;

	local_name = 
	    libbalsa_message_body_get_parameter(info->body, "name");

	if (!local_name) {
	    part_info_init_unknown(bm, info);
	    return;
	}

	url = g_strdup_printf("file:%s", local_name);
	msg = g_string_new(_("Content Type: external-body\n"));
	g_string_append_printf(msg, _("Access type: local-file\n"));
	g_string_append_printf(msg, _("File name: %s"), local_name);
	g_free(local_name);
    } else if (url_type == RFC2017_EXTBODY_URL) {
	gchar *local_name;

	local_name = 
	    libbalsa_message_body_get_parameter(info->body, "URL");

	if (!local_name) {
	    part_info_init_unknown(bm, info);
	    return;
	}

	url = g_strdup(local_name);
	msg = g_string_new(_("Content Type: external-body\n"));
	g_string_append_printf(msg, _("Access type: URL\n"));
	g_string_append_printf(msg, _("URL: %s"), url);
	g_free(local_name);
    } else { /* *FTP* */
	gchar *ftp_dir, *ftp_name, *ftp_site;
	    
	ftp_dir = 
	    libbalsa_message_body_get_parameter(info->body, "directory");
	ftp_name = 
	    libbalsa_message_body_get_parameter(info->body, "name");
	ftp_site = 
	    libbalsa_message_body_get_parameter(info->body, "site");

	if (!ftp_name || !ftp_site) {
	    part_info_init_unknown(bm, info);
	    g_free(ftp_dir);
	    g_free(ftp_name);
	    g_free(ftp_site);
	    return;
	}

	if (ftp_dir)
	    url = g_strdup_printf("%s://%s/%s/%s", 
				  url_type == RFC2046_EXTBODY_TFTP ? "tftp" : "ftp",
				  ftp_site, ftp_dir, ftp_name);
	else
	    url = g_strdup_printf("%s://%s/%s", 
				  url_type == RFC2046_EXTBODY_TFTP ? "tftp" : "ftp",
				  ftp_site, ftp_name);
	msg = g_string_new(_("Content Type: external-body\n"));
	g_string_append_printf(msg, _("Access type: %s\n"),
			  url_type == RFC2046_EXTBODY_TFTP ? "tftp" :
			  url_type == RFC2046_EXTBODY_FTP ? "ftp" : "anon-ftp");
	g_string_append_printf(msg, _("FTP site: %s\n"), ftp_site);
	if (ftp_dir)
	    g_string_append_printf(msg, _("Directory: %s\n"), ftp_dir);
	g_string_append_printf(msg, _("File name: %s"), ftp_name);
	g_free(ftp_dir);
	g_free(ftp_name);
	g_free(ftp_site);
    }

    /* now create the widget... */
    vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new(msg->str), FALSE, FALSE, 1);
    g_string_free(msg, TRUE);

    button = gtk_button_new_with_label(url);
    gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 5);
    g_object_set_data(G_OBJECT(button), "call_url", url);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(part_context_menu_call_url),
		     (gpointer) info);

    gtk_widget_show_all(vbox);

    info->focus_widget = vbox;
    info->widget = vbox;
    info->can_display = FALSE;    
}

static void
part_info_init_message_extbody_mail(BalsaMessage * bm, BalsaPartInfo * info)
{
    GtkWidget *vbox;
    GtkWidget *button;
    GString *msg = NULL;
    gchar *mail_subject, *mail_site;
	    
    mail_site =
	libbalsa_message_body_get_parameter(info->body, "server");

    if (!mail_site) {
	part_info_init_unknown(bm, info);
	return;
    }

    mail_subject =
	libbalsa_message_body_get_parameter(info->body, "subject");

    msg = g_string_new(_("Content Type: external-body\n"));
    g_string_append (msg, _("Access type: mail-server\n"));
    g_string_append_printf(msg, _("Mail server: %s\n"), mail_site);
    if (mail_subject)
	g_string_append_printf(msg, _("Subject: %s\n"), mail_subject);
    g_free(mail_subject);
    g_free(mail_site);

    /* now create the widget... */
    vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new(msg->str), FALSE, FALSE, 1);
    g_string_free(msg, TRUE);

    button = gtk_button_new_with_label(_("Send message to obtain this part"));
    gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 5);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(part_context_menu_mail),
		     (gpointer) info);

    gtk_widget_show_all(vbox);

    info->focus_widget = vbox;
    info->widget = vbox;
    info->can_display = FALSE;    
}

static void
part_info_init_message(BalsaMessage * bm, BalsaPartInfo * info)
{
    gchar* body_type;
    g_return_if_fail(info->body);

    body_type = libbalsa_message_body_get_content_type(info->body);
    if (!g_ascii_strcasecmp("message/external-body", body_type)) {
	gchar *access_type;
	rfc_extbody_id *extbody_type = rfc_extbodys;

	access_type = 
	    libbalsa_message_body_get_parameter(info->body, "access-type");
	while (extbody_type->id_string && 
	       g_ascii_strcasecmp(extbody_type->id_string, access_type))
	    extbody_type++;
	switch (extbody_type->action) {
	case RFC2046_EXTBODY_FTP:
	case RFC2046_EXTBODY_ANONFTP:
	case RFC2046_EXTBODY_TFTP:
	case RFC2046_EXTBODY_LOCALFILE:
	case RFC2017_EXTBODY_URL:
	    part_info_init_message_extbody_url(bm, info, extbody_type->action);
	    break;
	case RFC2046_EXTBODY_MAILSERVER:
	    part_info_init_message_extbody_mail(bm, info);
	    break;
	case RFC2046_EXTBODY_UNKNOWN:
	    g_print("TODO: part_info_init_message (external-body, access-type %s)\n",
		    access_type);
	    part_info_init_unknown(bm, info);
	    break;
	default:
	    g_error("Undefined external body action %d!", extbody_type->action);
	    break;
	}
	g_free(access_type);
    } else {
	g_print("TODO: part_info_init_message\n");
	part_info_init_unknown(bm, info);
    }
    g_free(body_type);
}

static void
part_info_init_unknown(BalsaMessage * bm, BalsaPartInfo * info)
{
    GtkWidget *vbox;
    GtkWidget *button;
    gchar *msg;
    const gchar *cmd, *content_desc;
    gchar *content_type;
    

    vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    content_type = libbalsa_message_body_get_content_type(info->body);
    if((button=part_info_mime_button_vfs(info, content_type))) {
	gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 2);
    } else if ((cmd = gnome_vfs_mime_get_value(content_type, "view")) != NULL) {
        button = part_info_mime_button (info, content_type, "view");
	gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 2);
    } else if ((cmd = gnome_vfs_mime_get_value (content_type, "open")) != NULL) {
        button = part_info_mime_button (info, content_type, "open");
	gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 2);
    } else {
	gtk_box_pack_start(GTK_BOX(vbox),
			   gtk_label_new(_("No open or view action defined in GNOME MIME for this content type")),
			   FALSE, FALSE, 1);
    }

    if((content_desc=gnome_vfs_mime_get_description(content_type)))
	msg = g_strdup_printf(_("Type: %s (%s)"), content_desc, content_type);
    else
    msg = g_strdup_printf(_("Content Type: %s"), content_type);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new(msg), FALSE, FALSE, 1);
    g_free(msg);

    if (info->body->filename) {
	msg = g_strdup_printf(_("Filename: %s"), info->body->filename);
	gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new(msg), FALSE, FALSE,
			   1);
	g_free(msg);
    }

    g_free(content_type);

    button = gtk_button_new_with_label(_("Save part"));
    gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 5);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(part_context_menu_save), (gpointer) info);

    gtk_widget_show_all(vbox);

    info->focus_widget = vbox;
    info->widget = vbox;
    info->can_display = FALSE;
}


static GtkWidget*
part_info_mime_button (BalsaPartInfo* info, const gchar* content_type, 
		       const gchar* key)
{
    GtkWidget* button;
    gchar* msg;
    const gchar* cmd;
    

    cmd = gnome_vfs_mime_get_value (content_type, (char*) key);
    msg = g_strdup_printf(_("View part with %s"), cmd);
    button = gtk_button_new_with_label(msg);
    g_object_set_data (G_OBJECT (button), "mime_action",  (gpointer) key);
    g_free(msg);

    g_signal_connect(G_OBJECT(button), "clicked",
                     G_CALLBACK(part_context_menu_cb), (gpointer) info);

    return button;
}


static GtkWidget*
part_info_mime_button_vfs (BalsaPartInfo* info, const gchar* content_type)
{
    GtkWidget* button=NULL;
    gchar* msg;
    const gchar* cmd;
    GnomeVFSMimeApplication *app=
	gnome_vfs_mime_get_default_application(content_type);

    if(app) {
	cmd = app->command;
	msg = g_strdup_printf(_("View part with %s"), app->name);
	button = gtk_button_new_with_label(msg);
	g_object_set_data (G_OBJECT (button), "mime_action", 
			     (gpointer) g_strdup(app->id)); /* *** */
	g_free(msg);

	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(part_context_menu_vfs_cb),
                         (gpointer) info);

	gnome_vfs_mime_application_free(app);
	
    }
    return button;
}

static void
display_multipart(BalsaMessage * bm, LibBalsaMessageBody * body)
{
    LibBalsaMessageBody *part;

    for (part = body->parts; part; part = part->next) {
	display_part(bm, part);
    }
}


static void
part_info_init_video(BalsaMessage * bm, BalsaPartInfo * info)
{
    g_print("TODO: part_info_init_video\n");
    part_info_init_unknown(bm, info);
}

/* HELPER FUNCTIONS ----------------------------------------------- */
/* reflows a paragraph in given string. The paragraph to reflow is
determined by the cursor position. If mode is <0, whole string is
reflowed. Replace tabs with single spaces, squeeze neighboring spaces. 
Single '\n' replaced with spaces, double - retained. 
HQ piece of code, modify only after thorough testing.
*/
/* find_beg_and_end - finds beginning and end of a paragraph;
 *l will store the pointer to the first character of the paragraph,
 *u - to the '\0' or first '\n' character delimiting the paragraph.
 */
static
    void
find_beg_and_end(gchar * str, gint pos, gchar ** l, gchar ** u)
{
    *l = str + pos;

    while (*l > str && (*(*l - 1) == '\n'))
        (*l)--;
	    
    *u = str + pos;

    while (**u && (**u!='\n'))
        (*u)++;
}

/* lspace - last was space, iidx - insertion index.  */
void
reflow_string(gchar * str, gint mode, gint * cur_pos, int width)
{
    gchar *l, *u, *sppos, *lnbeg, *iidx;
    gint lnl = 0, lspace = 0;	/* 1 -> skip leading spaces */

    if (mode < 0) {
	l = str;
	u = str + strlen(str);
    } else
	find_beg_and_end(str, *cur_pos, &l, &u);

    lnbeg = sppos = iidx = l;

    while (l < u) {
	if (lnl && *l == '\n') {
	    *(iidx - 1) = '\n';
	    *iidx++ = '\n';
	    lspace = 1;
	    lnbeg = sppos = iidx;
	} else if (isspace((unsigned char) *l)) {
	    lnl = *l == '\n';
	    if (!lspace) {
		sppos = iidx;
		*iidx++ = ' ';
	    } else if (iidx - str < *cur_pos)
		(*cur_pos)--;
	    lspace = 1;
	} else {
	    lspace = 0;
	    lnl = 0;
	    if (iidx - lnbeg >= width && lnbeg < sppos) {
		*sppos = '\n';
		lnbeg = sppos + 1;
	    }
	    *iidx++ = *l;
	}
	l++;
    }
    /* job is done, shrink remainings */
    while ((*iidx++ = *u++));
}

typedef struct _message_url_t {
    gint start, end;             /* pos in the buffer */
    gchar *url;                  /* the link */
    gboolean is_mailto;          /* open sendmsg window or external URL call */
} message_url_t;

static void handle_url(const message_url_t* url);
static void pointer_over_url(GtkWidget * widget, message_url_t * url,
                             gboolean set);
static message_url_t *find_url(GtkWidget * widget, gint x, gint y,
                               GList * url_list);

#ifdef HAVE_PCRE
static const char *url_str = "\\b(((https?|ftps?|nntp)://)|(mailto:|news:))(%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#\\w])+\\b";
#else
static const char *url_str = "(((https?|ftps?|nntp)://)|(mailto:|news:))(%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#[:alnum:]])+";
#endif

/* the cursors which are displayed over URL's and normal message text */
static GdkCursor *url_cursor_normal = NULL;
static GdkCursor *url_cursor_over_url = NULL;

static void
free_url_list(GList *l)
{
    if (l) {
	GList *p = l;

	while (p) {
	    message_url_t *url_data = (message_url_t *)p->data;
	    
	    g_free(url_data->url);
	    g_free(url_data);
	    p = g_list_next(p);
	}
	g_list_free(l);
    }
}

/* prescanner: 
 * used to find candidates for lines containing URL's.
 * Empirially, this approach is faster (by factor of 8) than scanning
 * entire message with regexec. YMMV.
 * s - is the line to scan. 
 * returns TRUE if the line may contain an URL.
 */
static gboolean
prescanner(const gchar *s)
{
    gint left = strlen(s) - 6;
    
    if (left <= 0)
	return FALSE;
    
    while (left--) {
	switch (tolower(*s++)) {
	case 'f':    /* ftp:/, ftps: */
	    if (tolower(*s) == 't' &&
		tolower(*(s + 1)) == 'p' &&
		(*(s + 2) == ':' || tolower(*(s + 2)) == 's') &&
		(*(s + 3) == ':' || *(s + 3) == '/'))
		return TRUE;
	    break;
	case 'h':    /* http:, https */
	    if (tolower(*s) == 't' &&
		tolower(*(s + 1)) == 't' &&
		tolower(*(s + 2)) == 'p' &&
		(*(s + 3) == ':' || tolower(*(s + 3)) == 's'))
		return TRUE;
	    break;
	case 'm':    /* mailt */
	    if (tolower(*s) == 'a' &&
		tolower(*(s + 1)) == 'i' &&
		tolower(*(s + 2)) == 'l' &&
		tolower(*(s + 3)) == 't')
		return TRUE;
	    break;
	case 'n':    /* news:, nntp: */
	    if ((tolower(*s) == 'e' || tolower(*s) == 'n') &&
		(tolower(*(s + 1)) == 'w' || tolower(*(s + 1)) == 't') &&
		(tolower(*(s + 2)) == 's' || tolower(*(s + 2)) == 'p') &&
		*(s + 3) == ':')
		return TRUE;
	    break;
	}
    }
    
    return FALSE;
}

/* do a gtk_text_buffer_insert, but mark URL's with balsa_app.url_color */
static void
insert_with_url(GtkTextBuffer * buffer, GtkTextIter * insert,
                GtkTextTag * quote_tag, const char *chars,
                regex_t *url_reg, GList **url_list, gint textline)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *url_tag = gtk_text_tag_table_lookup(table, "url");
    gint match, offset = 0;
    regmatch_t url_match;
    gchar *p, *buf;

    if (!url_tag)
        url_tag = gtk_text_buffer_create_tag(buffer, "url",
                                             "foreground-gdk", 
                                             &balsa_app.url_color,
                                             NULL);

    buf = p = g_strdup(chars);

    if (prescanner(p)) {
	match = regexec(url_reg, p, 1, &url_match, 0);
	while (!match) {
	    gchar *buf;
	    message_url_t *url_found;
	    
	    if (url_match.rm_so) {
		buf = g_strndup(p, url_match.rm_so);
                gtk_text_buffer_insert_with_tags(buffer, insert,
                                                 buf, -1,
                                                 quote_tag, NULL); 
		g_free(buf);
	    }
	    
	    buf = g_strndup(p + url_match.rm_so, 
			    url_match.rm_eo - url_match.rm_so);
	    /* remember the URL and its position within the text */
	    url_found = g_malloc(sizeof(message_url_t));
            url_found->start = gtk_text_iter_get_offset(insert);
            gtk_text_buffer_insert_with_tags(buffer, insert, buf, -1,
                                             url_tag, NULL);
            url_found->end = gtk_text_iter_get_offset(insert);
	    
	    url_found->url = buf;  /* gets freed later... */
	    url_found->is_mailto = (tolower(*buf) == 'm');
	    *url_list = g_list_append(*url_list, url_found);
	    
	    p += url_match.rm_eo;
	    offset += url_match.rm_eo;
	    if (prescanner(p))
		match = regexec(url_reg, p, 1, &url_match, 0);
	    else
		match = -1;
	}
    }

    if (*p)
        gtk_text_buffer_insert_with_tags(buffer, insert, p, -1,
                                         quote_tag, NULL);
    g_free(buf);
}

/* set the gtk_text widget's cursor to a vertical bar
   fix event mask so that pointer motions are reported (if necessary) */
static gboolean
fix_text_widget(GtkWidget *widget, gpointer data)
{
    GdkWindow *w =
        gtk_text_view_get_window(GTK_TEXT_VIEW(widget),
                                 GTK_TEXT_WINDOW_TEXT);
    
    if (data)
	gdk_window_set_events(w, gdk_window_get_events(w) | GDK_POINTER_MOTION_MASK);
    if (!url_cursor_normal || !url_cursor_over_url) {
	url_cursor_normal = gdk_cursor_new(GDK_XTERM);
	url_cursor_over_url = gdk_cursor_new(GDK_HAND2);
    }
    gdk_window_set_cursor(w, url_cursor_normal);
    return FALSE;
}

/* check if we are over an url and change the cursor in this case */
static gboolean
check_over_url(GtkWidget * widget, GdkEventMotion * event,
               GList * url_list)
{
    gint x, y;
    GdkModifierType mask;
    static gboolean was_over_url = FALSE;
    static message_url_t *current_url = NULL;
    GdkWindow *window;
    message_url_t *url;

    window = gtk_text_view_get_window(GTK_TEXT_VIEW(widget),
                                      GTK_TEXT_WINDOW_TEXT);
    /* FIXME: why can't we just use
     * x = event->x;
     * y = event->y;
     * ??? */
    gdk_window_get_pointer(window, &x, &y, &mask);
    url = find_url(widget, x, y, url_list);

    if (url) {
        if (!url_cursor_normal || !url_cursor_over_url) {
            url_cursor_normal = gdk_cursor_new(GDK_LEFT_PTR);
            url_cursor_over_url = gdk_cursor_new(GDK_HAND2);
        }
        if (!was_over_url) {
            gdk_window_set_cursor(window, url_cursor_over_url);
            was_over_url = TRUE;
        }
        if (url != current_url) {
            pointer_over_url(widget, current_url, FALSE);
            pointer_over_url(widget, url, TRUE);
        }
    } else if (was_over_url) {
        gdk_window_set_cursor(window, url_cursor_normal);
        pointer_over_url(widget, current_url, FALSE);
        was_over_url = FALSE;
    }

    current_url = url;
    return FALSE;
}

/* store the coordinates at which the button was pressed */
static gint stored_x = -1, stored_y = -1;
static GdkModifierType stored_mask = -1;
#define STORED_MASK_BITS (  GDK_SHIFT_MASK   \
                          | GDK_CONTROL_MASK \
                          | GDK_MOD1_MASK    \
                          | GDK_MOD2_MASK    \
                          | GDK_MOD3_MASK    \
                          | GDK_MOD4_MASK    \
                          | GDK_MOD5_MASK    )

static gboolean
store_button_coords(GtkWidget * widget, GdkEventButton * event,
                    gpointer data)
{
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        GdkWindow *window =
            gtk_text_view_get_window(GTK_TEXT_VIEW(widget),
                                     GTK_TEXT_WINDOW_TEXT);

        gdk_window_get_pointer(window, &stored_x, &stored_y, &stored_mask);

        /* compare only shift, ctrl, and mod1-mod5 */
        stored_mask &= STORED_MASK_BITS;
    }
    return FALSE;
}

/* if the mouse button was released over an URL, and the mouse hasn't
 * moved since the button was pressed, try to call the URL */
static gboolean
check_call_url(GtkWidget * widget, GdkEventButton * event,
               GList * url_list)
{
    gint x, y;
    message_url_t *url;

    if (event->type != GDK_BUTTON_RELEASE || event->button != 1)
        return FALSE;

    x = event->x;
    y = event->y;
    if (x == stored_x && y == stored_y
        && (event->state & STORED_MASK_BITS) == stored_mask) {
        url = find_url(widget, x, y, url_list);
        if (url)
            handle_url(url);
    }
    return FALSE;
}

static gboolean
status_bar_refresh(gpointer data)
{
    gnome_appbar_refresh(balsa_app.appbar);
    return FALSE;
}
#define SCHEDULE_BAR_REFRESH()	gtk_timeout_add(5000, status_bar_refresh, NULL);

static void
handle_url(const message_url_t* url)
{
    if (url->is_mailto) {
	BalsaSendmsg *snd = 
	    sendmsg_window_new(GTK_WIDGET(balsa_app.main_window),
			       NULL, SEND_NORMAL);
	sendmsg_window_process_url(url->url + 7,
				   sendmsg_window_set_field, snd);	
    } else {
	gchar *notice = g_strdup_printf(_("Calling URL %s..."),
					url->url);
        GError *err = NULL;

        gnome_appbar_set_status(balsa_app.appbar, notice);
	SCHEDULE_BAR_REFRESH();
        g_free(notice);
        gnome_url_show(url->url, &err);
        if (err) {
            g_print(_("Error showing %s: %s\n"), url->url,
                    err->message);
            g_error_free(err);
        }
    }
}

/* END OF HELPER FUNCTIONS ----------------------------------------------- */

static void
part_info_init_mimetext(BalsaMessage * bm, BalsaPartInfo * info)
{
    static regex_t *url_reg = NULL;
    FILE *fp;
    gboolean ishtml;
    gchar *content_type;
    gchar *ptr = NULL;
    size_t alloced;

    /* one-time compilation of a constant url_str expression */
    if (!url_reg) {
        url_reg = g_malloc(sizeof(regex_t));
        if (regcomp(url_reg, url_str, REG_EXTENDED | REG_ICASE) != 0)
            g_warning
                ("part_info_init_mimetext: url regex compilation failed.");
    }

    /* proper code */
    if (!libbalsa_message_body_save_temporary(info->body, NULL)) {
        balsa_information
            (LIBBALSA_INFORMATION_ERROR,
             _("Error writing to temporary file %s.\n"
               "Check the directory permissions."),
             info->body->temp_filename);
        return;
    }

    if ((fp = fopen(info->body->temp_filename, "r")) == NULL) {
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("Cannot open temporary file %s."),
                          info->body->temp_filename);
        return;
    }

    alloced = libbalsa_readfile(fp, &ptr);
    if (!ptr)
        return;

    content_type = libbalsa_message_body_get_content_type(info->body);
    ishtml = (g_ascii_strcasecmp(content_type, "text/html") == 0);
    g_free(content_type);

    /* This causes a memory leak */
    /* if( info->body->filename == NULL ) */
    /*   info->body->filename = g_strdup( "textfile" ); */

    if (ishtml) {
#ifdef HAVE_GTKHTML
        part_info_init_html(bm, info, ptr, alloced);
#else
        part_info_init_unknown(bm, info);
#endif
    } else {
        GtkWidget *item;
        GtkTextBuffer *buffer;
        regex_t rex;
        GList *url_list = NULL;

        libbalsa_utf8_sanitize(ptr);

        if (bm->wrap_text) {
            if (balsa_app.recognize_rfc2646_format_flowed
                && libbalsa_flowed_rfc2646(info->body)) {
                ptr =
                    libbalsa_wrap_rfc2646(ptr,
                                          balsa_app.browse_wrap_length,
                                          FALSE, TRUE);
            } else
                libbalsa_wrap_string(ptr, balsa_app.browse_wrap_length);
        }

        item = gtk_text_view_new();
        buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(item));

        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(item), 2);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(item), 2);

        /* set the message font */
        gtk_widget_modify_font(item,
                               pango_font_description_from_string
                               (balsa_app.message_font));

        g_signal_connect(G_OBJECT(item), "key_press_event",
                         G_CALLBACK(balsa_message_key_press_event),
                           (gpointer) bm);
        g_signal_connect(G_OBJECT(item), "focus_in_event",
                         G_CALLBACK(balsa_message_focus_in_part),
                           (gpointer) bm);
        g_signal_connect(G_OBJECT(item), "focus_out_event",
                         G_CALLBACK(balsa_message_focus_out_part),
                           (gpointer) bm);
        allocate_quote_colors(GTK_WIDGET(bm), balsa_app.quoted_color,
                              0, MAX_QUOTED_COLOR - 1);
        if (regcomp(&rex, balsa_app.quote_regex, REG_EXTENDED) != 0) {
            g_warning
                ("part_info_init_mimetext: quote regex compilation failed.");
            gtk_text_buffer_insert_at_cursor(buffer, ptr, -1);
        } else {
            GtkTextIter insert;
            gchar **lines;
            gchar **l = g_strsplit(ptr, "\n", -1);

            gtk_text_buffer_get_iter_at_mark(buffer, &insert, 
                                             gtk_text_buffer_get_insert
                                             (buffer));

            for (lines = l; *lines; ++lines) {
                gint quote_level = is_a_quote(*lines, &rex);
                GtkTextTag *tag = quote_tag(buffer, quote_level);

                /* tag is NULL if the line isn't quoted, but it causes
                 * no harm */
                insert_with_url(buffer, &insert, tag, *lines, url_reg,
                                &url_list, lines - l);
                gtk_text_buffer_insert(buffer, &insert, "\n", 1);
            }
            g_strfreev(l);
            regfree(&rex);
        }

        g_signal_connect_after(G_OBJECT(item), "realize",
                               G_CALLBACK(fix_text_widget), url_list);
        if (url_list) {
            g_signal_connect(G_OBJECT(item), "button_press_event",
                             G_CALLBACK(store_button_coords), NULL);
            g_signal_connect(G_OBJECT(item), "button_release_event",
                             G_CALLBACK(check_call_url), url_list);
            g_signal_connect(G_OBJECT(item), "motion-notify-event",
                             G_CALLBACK(check_over_url), url_list);
        }

        g_free(ptr);

        gtk_text_view_set_editable(GTK_TEXT_VIEW(item), FALSE);

        gtk_widget_show(item);
        info->focus_widget = item;
        info->widget = item;
        info->can_display = TRUE;
        /* size allocation may not be correct, so we'll check back later
         */
        gtk_idle_add((GtkFunction) resize_idle, item);
    }

    fclose(fp);
}
#ifdef HAVE_GTKHTML
static void
part_info_init_html(BalsaMessage * bm, BalsaPartInfo * info, gchar * ptr,
		    size_t len)
{
    GtkWidget *html;
    HtmlDocument *document;

    html = html_view_new();

    document = html_document_new();
    html_view_set_document(HTML_VIEW(html), document);

    html_document_open_stream(document, "text/html");
    html_document_write_stream(document, ptr, len);
    html_document_close_stream (document);

    g_signal_connect(G_OBJECT(html), "size_request",
		     G_CALLBACK(balsa_gtk_html_size_request),
                     (gpointer) bm);
    g_signal_connect(G_OBJECT(document), "link_clicked",
		     G_CALLBACK(balsa_gtk_html_link_clicked), NULL);
    g_signal_connect(G_OBJECT(html), "on_url",
		     G_CALLBACK(balsa_gtk_html_on_url), bm);

    gtk_widget_show(html);

    info->focus_widget = html;
    info->widget = html;
    info->can_display = TRUE;
}
#endif

static void
part_info_init(BalsaMessage * bm, BalsaPartInfo * info)
{
    LibBalsaMessageBodyType type;

    g_return_if_fail(bm != NULL);
    g_return_if_fail(info != NULL);
    g_return_if_fail(info->body != NULL);

    type = libbalsa_message_body_type(info->body);

    switch (type) {
    case LIBBALSA_MESSAGE_BODY_TYPE_OTHER:
	if (balsa_app.debug)
	    fprintf(stderr, "part: other\n");
	part_info_init_other(bm, info);
	break;
    case LIBBALSA_MESSAGE_BODY_TYPE_AUDIO:
	if (balsa_app.debug)
	    fprintf(stderr, "part: audio\n");
	part_info_init_audio(bm, info);
	break;
    case LIBBALSA_MESSAGE_BODY_TYPE_APPLICATION:
	if (balsa_app.debug)
	    fprintf(stderr, "part: application\n");
	part_info_init_application(bm, info);
	break;
    case LIBBALSA_MESSAGE_BODY_TYPE_IMAGE:
	if (balsa_app.debug)
	    fprintf(stderr, "part: image\n");
	part_info_init_image(bm, info);
	break;
    case LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE:
	if (balsa_app.debug)
	    fprintf(stderr, "part: message\n");
	part_info_init_message(bm, info);
	fprintf(stderr, "part end: multipart\n");
	break;
    case LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART:
	break;
    case LIBBALSA_MESSAGE_BODY_TYPE_TEXT:
	if (balsa_app.debug)
	    fprintf(stderr, "part: text\n");
	part_info_init_mimetext(bm, info);
	break;
    case LIBBALSA_MESSAGE_BODY_TYPE_VIDEO:
	if (balsa_app.debug)
	    fprintf(stderr, "part: video\n");
	part_info_init_video(bm, info);
	break;
    case LIBBALSA_MESSAGE_BODY_TYPE_MODEL:
	if (balsa_app.debug)
	    fprintf(stderr, "part: model\n");
	part_info_init_model(bm, info);
	break;
    }

    /* The widget is unref'd in part_info_free */
    if(info->widget) {
	g_object_ref(G_OBJECT(info->widget));
	gtk_object_sink(GTK_OBJECT(info->widget));
    }

    return;
}

static void
display_part(BalsaMessage * bm, LibBalsaMessageBody * body)
{
    BalsaPartInfo *info = NULL;
    gchar *pix = NULL;
    gchar *content_type = libbalsa_message_body_get_content_type(body);
    gchar *icon_title = NULL;
    gint pos;
    gboolean is_multipart=libbalsa_message_body_is_multipart(body);

    if(!is_multipart ||
       g_ascii_strcasecmp(content_type, "multipart/mixed")==0 ||
       g_ascii_strcasecmp(content_type, "multipart/alternative")==0) {
    info = part_info_new(body, bm->message);

	if (is_multipart) {
	    icon_title = g_strdup_printf("%s parts", strchr(content_type, '/')+1);
	    *icon_title = toupper (*icon_title);
	} else if (body->filename)
	icon_title =
	    g_strdup_printf("%s (%s)", body->filename, content_type);
    else
	icon_title = g_strdup_printf("(%s)", content_type);

    pix = libbalsa_icon_finder(content_type, body->filename, NULL);

    part_create_menu (info);
    pos = gnome_icon_list_append(GNOME_ICON_LIST(bm->part_list),
				 pix, icon_title);

    gnome_icon_list_set_icon_data_full(GNOME_ICON_LIST(bm->part_list), pos,
				       info, (GtkDestroyNotify)part_info_free);

    g_free(icon_title);
    g_free(pix);
    }
    if (is_multipart) {
	if (balsa_app.debug)
	    fprintf(stderr, "part: multipart\n");
	display_multipart(bm, body);
	if (balsa_app.debug)
	    fprintf(stderr, "part end: multipart\n");
    }
    g_free(content_type);
}

static void
display_content(BalsaMessage * bm)
{
    LibBalsaMessageBody *body;

    for (body = bm->message->body_list; body; body = body->next)
	display_part(bm, body);
}

static void add_vfs_menu_item(BalsaPartInfo *info, 
			      const GnomeVFSMimeApplication *app)
{
    gchar *menu_label = g_strdup_printf(_("Open with %s"), app->name);
    GtkWidget *menu_item = gtk_menu_item_new_with_label (menu_label);
    
    g_object_set_data (G_OBJECT (menu_item), "mime_action", 
			 g_strdup(app->id));
    g_signal_connect (G_OBJECT (menu_item), "activate",
			GTK_SIGNAL_FUNC (part_context_menu_vfs_cb),
			(gpointer) info);
    gtk_menu_shell_append (GTK_MENU_SHELL (info->popup_menu), menu_item);
    g_free (menu_label);
}

static gboolean in_gnome_vfs(const GnomeVFSMimeApplication *default_app, 
			     const GList *short_list, const gchar *cmd) 
{
    gchar *cmd_base=g_strdup(cmd), *arg=strchr(cmd_base, '%');
    
    /* Note: Tries to remove the entrire argument containing %f etc., so that
             we e.g. get rid of the whole "file:%f", not just "%f" */
    if(arg) {
	while(arg!=cmd && *arg!=' ')
	    arg--;
	
	*arg='\0';
    }
    g_strstrip(cmd_base);
    
    if(default_app && default_app->command && strcmp(default_app->command, cmd_base)==0) {
	g_free(cmd_base);
	return TRUE;
    } else {
	const GList *item;

	for(item=short_list; item; item=g_list_next(item)) {
	    GnomeVFSMimeApplication *app=item->data;
	    
	    if(app->command && strcmp(app->command, cmd_base)==0) {
		g_free(cmd_base);
		return TRUE;
	    }
	}
    }
    g_free(cmd_base);
    
    return FALSE;
}


static void
part_create_menu (BalsaPartInfo* info) 
/* Remarks: Will add items in the following order:
            1) Default application according to GnomeVFS.
	    2) GNOME MIME/GnomeVFS key values that don't match default
	       application or anything on the shortlist.
	    3) GnomeVFS shortlist applications, with the default one (sometimes
	       included on shortlist, sometimes not) excluded. */ 
{
    GtkWidget* menu_item;
    GList* list;
    GList* key_list, *app_list;
    gchar* content_type;
    gchar* key;
    const gchar* cmd;
    gchar* menu_label;
    gchar** split_key;
    gint i;
    GnomeVFSMimeApplication *def_app, *app;
    
    info->popup_menu = gtk_menu_new ();
    
    content_type = libbalsa_message_body_get_content_type (info->body);
    key_list = list = gnome_vfs_mime_get_key_list(content_type);
    /* gdk_threads_leave(); releasing GDK lock was necessary for broken
     * gnome-vfs versions */
    app_list = gnome_vfs_mime_get_short_list_applications(content_type);
    /* gdk_threads_enter(); */

    if((def_app=gnome_vfs_mime_get_default_application(content_type))) {
	add_vfs_menu_item(info, def_app);
    }
    

    while (list) {
        key = list->data;

        if (key && g_ascii_strcasecmp (key, "icon-filename") 
	    && g_ascii_strncasecmp (key, "fm-", 3)
	    /* Get rid of additional GnomeVFS entries: */
	    && (!strstr(key, "_") || strstr(key, "."))
	    && g_ascii_strncasecmp(key, "description", 11)) {
	    
            if ((cmd = gnome_vfs_mime_get_value (content_type, key)) != NULL &&
		!in_gnome_vfs(def_app, app_list, cmd)) {
                if (g_ascii_strcasecmp (key, "open") == 0 || 
                    g_ascii_strcasecmp (key, "view") == 0 || 
                    g_ascii_strcasecmp (key, "edit") == 0 ||
                    g_ascii_strcasecmp (key, "ascii-view") == 0) {
                    /* uppercase first letter, make label */
		    menu_label = g_strdup_printf ("%s (\"%s\")", key, cmd);
                    *menu_label = toupper (*menu_label);
                } else {
                    split_key = g_strsplit (key, ".", -1);

		    i = 0;
                    while (split_key[i+1] != NULL) {
                        ++i;
                    }
                    menu_label = split_key[i];
                    menu_label = g_strdup (menu_label);
                    g_strfreev (split_key);
                }
                menu_item = gtk_menu_item_new_with_label (menu_label);
                g_object_set_data (G_OBJECT (menu_item), "mime_action", 
                                   key);
                g_signal_connect (G_OBJECT (menu_item), "activate",
                                  G_CALLBACK (part_context_menu_cb),
                                  (gpointer) info);
                gtk_menu_shell_append (GTK_MENU_SHELL (info->popup_menu), menu_item);
                g_free (menu_label);
            }
        }
        list = g_list_next (list);
    }

    list=app_list;

    while (list) {
	app=list->data;

	if(app && (!def_app || strcmp(app->name, def_app->name)!=0)) {
	    add_vfs_menu_item(info, app);
	}

        list = g_list_next (list);
    }
    gnome_vfs_mime_application_free(def_app);
    

    menu_item = gtk_menu_item_new_with_label (_("Save..."));
    g_signal_connect (G_OBJECT (menu_item), "activate",
                      G_CALLBACK (part_context_menu_save), (gpointer) info);
    gtk_menu_shell_append (GTK_MENU_SHELL (info->popup_menu), menu_item);

    gtk_widget_show_all (info->popup_menu);

    g_list_free (key_list);
    gnome_vfs_mime_application_list_free (app_list);
    g_free (content_type);
}


static BalsaPartInfo*
part_info_new(LibBalsaMessageBody* body, LibBalsaMessage* msg) 
{
    BalsaPartInfo* info = (BalsaPartInfo *) g_new0(BalsaPartInfo, 1);
    info->body = body;
    info->message = msg;
    info->can_display = FALSE;
    return info;
}

static void
part_info_free(BalsaPartInfo* info)
{
    g_return_if_fail(info);

    if (info->widget) {
	GList *widget_list;
	
	widget_list = 
	    g_object_get_data(G_OBJECT(info->widget), "url-list");
 	free_url_list(widget_list);
        /* FIXME: Why unref will not do? */
	gtk_widget_destroy(info->widget);
    }
    if (info->popup_menu)
	gtk_widget_destroy(info->popup_menu);

    g_free(info);
}

static void
part_context_menu_save(GtkWidget * menu_item, BalsaPartInfo * info)
{
    save_part(info);
}


static void
part_context_menu_call_url(GtkWidget * menu_item, BalsaPartInfo * info)
{
    gchar *url = g_object_get_data (G_OBJECT (menu_item), "call_url");
    GError *err = NULL;

    g_return_if_fail(url);
    gnome_url_show(url, &err);
    if (err) {
        g_print(_("Error showing %s: %s\n"), url, err->message);
        g_error_free(err);
    }
}


static void
part_context_menu_mail(GtkWidget * menu_item, BalsaPartInfo * info)
{
    LibBalsaMessage *message;
    LibBalsaMessageBody *body;
    gchar *data;
    FILE *part;

    /* create a message */
    message = libbalsa_message_new();
    data = libbalsa_address_to_gchar(balsa_app.current_ident->address, 0);
    message->from = libbalsa_address_new_from_string(data);
    g_free (data);

    data = libbalsa_message_body_get_parameter(info->body, "subject");
    if (data)
	LIBBALSA_MESSAGE_SET_SUBJECT(message, data);

    data = libbalsa_message_body_get_parameter(info->body, "server");
    message->to_list = libbalsa_address_new_list_from_string(data);
    g_free (data);

    /* the original body my have some data to be returned as commands... */
    body = libbalsa_message_body_new(message);

    libbalsa_message_body_save_temporary(info->body, NULL);
    part = fopen(info->body->temp_filename, "r");
    if (part) {
	gchar *p;

	libbalsa_readfile(part, &data);
	/* ignore everything before the first two newlines */
	if ((p = strstr (data, "\n\n")))
	    body->buffer = g_strdup(p + 2);
	else
	    body->buffer = g_strdup(data);
	g_free(data);
	fclose(part);
    }
    if (info->body->charset)
	body->charset = g_strdup(info->body->charset);
    else
	body->charset = g_strdup("US-ASCII");
    libbalsa_message_append_part(message, body);
#if ENABLE_ESMTP
    libbalsa_message_send(message, balsa_app.outbox, NULL,
			  balsa_app.encoding_style,  
			  balsa_app.smtp_server,
			  balsa_app.smtp_authctx,
			  balsa_app.smtp_tls_mode,
			  FALSE);
#else
    libbalsa_message_send(message, balsa_app.outbox, NULL,
			  balsa_app.encoding_style,
			  FALSE);
#endif
    g_object_unref(G_OBJECT(message));    
}


static void
part_context_menu_cb(GtkWidget * menu_item, BalsaPartInfo * info)
{
    gchar *content_type, *fpos;
    const gchar *cmd;
    gchar* key;


    content_type = libbalsa_message_body_get_content_type(info->body);
    key = g_object_get_data (G_OBJECT (menu_item), "mime_action");

    if (key != NULL
        && (cmd = gnome_vfs_mime_get_value(content_type, key)) != NULL) {
	if (!libbalsa_message_body_save_temporary(info->body, NULL)) {
	    balsa_information(LIBBALSA_INFORMATION_WARNING,
			      _("could not create temporary file %s"),
			      info->body->temp_filename);
	    g_free(content_type);
	    return;
	}

	if ((fpos = strstr(cmd, "%f")) != NULL) {
	    gchar *exe_str, *cps = g_strdup(cmd);
	    cps[fpos - cmd + 1] = 's';
	    exe_str = g_strdup_printf(cps, info->body->temp_filename);
	    gnome_execute_shell(NULL, exe_str);
	    fprintf(stderr, "Executed: %s\n", exe_str);
            g_free (cps);
            g_free (exe_str);
	}
    } else
	fprintf(stderr, "view for %s returned NULL\n", content_type);

    g_free(content_type);
}


static void
part_context_menu_vfs_cb(GtkWidget * menu_item, BalsaPartInfo * info)
{
    gchar *id;
    
    if((id = g_object_get_data (G_OBJECT (menu_item), "mime_action"))) {
	GnomeVFSMimeApplication *app=
	    gnome_vfs_mime_application_new_from_id(id);
	if(app) {
	    if (libbalsa_message_body_save_temporary(info->body, NULL)) {
                gboolean tmp =
                    (app->expects_uris ==
                     GNOME_VFS_MIME_APPLICATION_ARGUMENT_TYPE_URIS);
                gchar *exe_str =
                    g_strdup_printf("%s %s%s", app->command,
                                    tmp ? "file:" : "",
                                    info->body->temp_filename);
		
		gnome_execute_shell(NULL, exe_str);
		fprintf(stderr, "Executed: %s\n", exe_str);
		g_free (exe_str);
	    } else {
		balsa_information(LIBBALSA_INFORMATION_WARNING,
				  _("could not create temporary file %s"),
				  info->body->temp_filename);
	    }
	    gnome_vfs_mime_application_free(app);    
	} else {
	    fprintf(stderr, "lookup for application %s returned NULL\n", id);
	}
    }
}

void
balsa_message_next_part(BalsaMessage * bmessage)
{
    GnomeIconList *gil;
    guint icons;
    GList *list;
    guint index = 0;

    g_return_if_fail(bmessage != NULL);
    g_return_if_fail(bmessage->part_list != NULL);

    gil = GNOME_ICON_LIST(bmessage->part_list);
    if ((icons = gnome_icon_list_get_num_icons(gil)) == 0)
	return;

    if ((list = gnome_icon_list_get_selection(gil))) {
	index = GPOINTER_TO_INT(list->data);
	if (++index >= icons)
	    index = 0;
    }

    gnome_icon_list_select_icon(gil, index);
}

void
balsa_message_previous_part(BalsaMessage * bmessage)
{
    GnomeIconList *gil;
    guint icons;
    GList *list;
    gint index = 0;

    g_return_if_fail(bmessage != NULL);
    g_return_if_fail(bmessage->part_list != NULL);

    gil = GNOME_ICON_LIST(bmessage->part_list);
    if ((icons = gnome_icon_list_get_num_icons(gil)) <=  1)
	return;

    if ((list = gnome_icon_list_get_selection(gil))) {
	index = GPOINTER_TO_INT(list->data);

	if (--index < 0)
	    index = icons - 1;
    }

    gnome_icon_list_select_icon(gil, index);
}

static LibBalsaMessageBody*
preferred_part(LibBalsaMessageBody *parts)
{
    /* TODO: Consult preferences and/or previous selections */

    LibBalsaMessageBody *body;
    gchar *content_type;

#ifdef HAVE_GTKHTML
    for(body=parts; body; body=body->next) {
	content_type = libbalsa_message_body_get_content_type(body);

	if(g_ascii_strcasecmp(content_type, "text/html")==0) {
	    g_free(content_type);
	    return body;
	}
	g_free(content_type);
    }
#endif /* HAVE_GTKHTML */

    for(body=parts; body; body=body->next) {
	content_type = libbalsa_message_body_get_content_type(body);

	if(g_ascii_strcasecmp(content_type, "text/plain")==0) {
	    g_free(content_type);
	    return body;
	}
	g_free(content_type);
    }


    return parts;
}



static gint part_icon_no(BalsaMessage *bm, const LibBalsaMessageBody *body)
{
    const BalsaPartInfo *info;
    gint part =
        gnome_icon_list_get_num_icons(GNOME_ICON_LIST(bm->part_list));

    while (--part >= 0) {
	info = (const BalsaPartInfo *) gnome_icon_list_get_icon_data
	    (GNOME_ICON_LIST(bm->part_list), part);
	if(info->body==body)
	    break;
    }
    return part;
}


static void add_body(BalsaMessage *bm, 
		     LibBalsaMessageBody *body)
{
    if(body) {
	gint part=part_icon_no(bm, body);
	
	if(part>=0)
	    add_part(bm, part);
	else
	    add_multipart(bm, body);
    }
}


static void add_multipart(BalsaMessage *bm, LibBalsaMessageBody *parent)
/* Remarks: *** The tests/assumptions made are NOT verified with the RFCs */
{
    if(parent->parts) {
	gchar *content_type = 
	    libbalsa_message_body_get_content_type(parent);
	if(g_ascii_strcasecmp(content_type, "multipart/related")==0) {
	    /* Add the first part */
	    add_body(bm, parent->parts);
	} else if(g_ascii_strcasecmp(content_type, "multipart/alternative")==0) {
	    /* Add the most suitable part. */
	    add_body(bm, preferred_part(parent->parts));
	} else {
	    /* Add first (main) part + anything else with 
	       Content-Disposition: inline */
	    LibBalsaMessageBody *body=parent->parts;
	    
	    if(body) {
		add_body(bm, body);
		for(body=body->next; body; body=body->next) {
                    if(libbalsa_message_body_is_inline(body))
			add_body(bm, body);
		}
	    }
	}
	g_free(content_type);
    }
}

static GtkWidget *old_widget, *new_widget;
static gdouble old_upper, new_upper;
static gint resize_idle_id;

static gboolean
resize_idle(GtkWidget * widget)
{
    gdk_threads_enter();
    resize_idle_id = 0;
    if (GTK_IS_WIDGET(widget))
        gtk_widget_queue_resize(widget);
    old_widget = new_widget;
    old_upper = new_upper;
    gdk_threads_leave();

    return FALSE;
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
        gtk_idle_remove(resize_idle_id);
    resize_idle_id = gtk_idle_add((GtkFunction) resize_idle, widget);
}

static BalsaPartInfo *add_part(BalsaMessage *bm, gint part)
{
    GnomeIconList *gil = GNOME_ICON_LIST(bm->part_list);
    BalsaPartInfo *info=NULL;

    if (part != -1) {
	info = (BalsaPartInfo *) gnome_icon_list_get_icon_data(gil, part);

	g_assert(info != NULL);

        g_signal_handler_block(G_OBJECT(gil), bm->select_icon_handler);
	gnome_icon_list_select_icon(gil, part);
        g_signal_handler_unblock(G_OBJECT(gil), bm->select_icon_handler);

	if (info->widget == NULL)
	    part_info_init(bm, info);

	if (info->widget) {
	    gtk_container_add(GTK_CONTAINER(bm->content), info->widget);
	    gtk_widget_show(info->widget);
            if (GTK_IS_LAYOUT(info->widget)) {
                GtkAdjustment *vadj =
                    gtk_layout_get_vadjustment(GTK_LAYOUT(info->widget));
                g_signal_connect(G_OBJECT(vadj), "changed",
                                 G_CALLBACK(vadj_change_cb), info->widget);
            }
	}
	add_multipart(bm, info->body);
    }
    
    return info;
}

static void
hide_all_parts(BalsaMessage * bm)
{
    if (bm->current_part) {
        gint part =
            gnome_icon_list_get_num_icons(GNOME_ICON_LIST(bm->part_list));

        while (--part >= 0) {
            BalsaPartInfo *current_part =
                (BalsaPartInfo *)
                gnome_icon_list_get_icon_data(GNOME_ICON_LIST
                                              (bm->part_list), part);

            if (current_part && current_part->widget
                && current_part->widget->parent) {
                gtk_container_remove(GTK_CONTAINER(bm->content),
                                     current_part->widget);
            }
        }
        gnome_icon_list_unselect_all(GNOME_ICON_LIST(bm->part_list));

        bm->current_part = NULL;
    }
}

/* 
 * If part == -1 then change to no part
 * must release selection before hiding a text widget.
 */
static void
select_part(BalsaMessage * bm, gint part)
{
    hide_all_parts(bm);
    gtk_widget_modify_font(bm->header_text,
                           pango_font_description_from_string
                           (balsa_app.message_font));

    bm->current_part = add_part(bm, part);

    if(bm->current_part)
	g_signal_emit(G_OBJECT(bm), balsa_message_signals[SELECT_PART], 0);

    scroll_set(GTK_VIEWPORT(bm)->hadjustment, 0);
    scroll_set(GTK_VIEWPORT(bm)->vadjustment, 0);

    gtk_widget_queue_resize(GTK_WIDGET(bm));

}

static void
scroll_set(GtkAdjustment * adj, gint value)
{
    gfloat upper;

    if (!adj)
	return;

    adj->value = value;

    upper = adj->upper - adj->page_size;
    adj->value = MIN(adj->value, upper);
    adj->value = MAX(adj->value, 0.0);

    g_signal_emit_by_name(G_OBJECT(adj), "value_changed", 0);
}

static void
scroll_change(GtkAdjustment * adj, gint diff)
{
    gfloat upper;

    adj->value += diff;

    upper = adj->upper - adj->page_size;
    adj->value = MIN(adj->value, upper);
    adj->value = MAX(adj->value, 0.0);

    g_signal_emit_by_name(G_OBJECT(adj), "value_changed", 0);
}

static gint
balsa_message_key_press_event(GtkWidget * widget, GdkEventKey * event,
			      BalsaMessage * bm)
{
    GtkViewport *viewport;
    int page_adjust;

    viewport = GTK_VIEWPORT(bm);

    if (balsa_app.pgdownmod) {
	    page_adjust = (viewport->vadjustment->page_size *
		 balsa_app.pgdown_percent) / 100;
    } else {
	    page_adjust = viewport->vadjustment->page_increment;
    }

    switch (event->keyval) {
    case GDK_Up:
	scroll_change(viewport->vadjustment,
		      -viewport->vadjustment->step_increment);
	break;
    case GDK_Down:
	scroll_change(viewport->vadjustment,
		      viewport->vadjustment->step_increment);
	break;
    case GDK_Page_Up:
	scroll_change(viewport->vadjustment,
		      -page_adjust);
	break;
    case GDK_Page_Down:
	scroll_change(viewport->vadjustment,
		      page_adjust);
	break;
    case GDK_Home:
	if (event->state & GDK_CONTROL_MASK)
	    scroll_change(viewport->vadjustment,
			  -viewport->vadjustment->value);
	else
	    return FALSE;
	break;
    case GDK_End:
	if (event->state & GDK_CONTROL_MASK)
	    scroll_change(viewport->vadjustment,
			  viewport->vadjustment->upper);
	else
	    return FALSE;
	break;

    default:
	return FALSE;
    }
    return TRUE;
}


#ifdef HAVE_GTKHTML
/* balsa_gtk_html_size_request:
   report the requested size of the HTML widget.
*/
static void
balsa_gtk_html_size_request(GtkWidget * widget,
			    GtkRequisition * requisition, gpointer data)
{
    g_return_if_fail(widget != NULL);
    g_return_if_fail(HTML_IS_VIEW(widget));
    g_return_if_fail(requisition != NULL);

    requisition->width = GTK_LAYOUT(widget)->hadjustment->upper +
        widget->style->xthickness * 2;
    requisition->height = GTK_LAYOUT(widget)->vadjustment->upper +
        widget->style->ythickness * 2;
}

static void
balsa_gtk_html_link_clicked(GObject *obj, const gchar *url)
{
    GError *err = NULL;

    g_return_if_fail(HTML_IS_DOCUMENT(obj));

    gnome_url_show(url, &err);
    if (err) {
        g_print(_("Error showing %s: %s\n"), url, err->message);
        g_error_free(err);
    }
}
#endif /* defined HAVE_GTKHTML */

static void
balsa_gtk_html_on_url(GtkWidget *html, const gchar *url)
{
    if( url ) {
	gnome_appbar_set_status(balsa_app.appbar, url);
	SCHEDULE_BAR_REFRESH();
    } else 
	gnome_appbar_refresh(balsa_app.appbar);
}

static void
balsa_icon_list_size_request(GtkWidget * widget,
			     GtkRequisition * requisition, gpointer data)
{
    GnomeIconList *gil;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(GNOME_IS_ICON_LIST(widget));
    g_return_if_fail(requisition != NULL);

    gil = GNOME_ICON_LIST(widget);

    requisition->width =
        (GTK_CONTAINER(widget)->border_width + widget->style->xthickness +
         1) * 2;
    requisition->height =
        (GTK_CONTAINER(widget)->border_width + widget->style->ythickness +
         1) * 2;

    /* requisition->width = gil->hadj->upper; */
    requisition->height += gil->adj->upper;

}

/*
 * This function informs the caller if the currently selected part 
 * supports selection/copying etc. Currently only the GtkEditable derived 
 * widgets
 * and GtkTextView
 * are supported for this (GtkHTML could be, but I don't have a 
 * working build right now)
 */
gboolean balsa_message_can_select(BalsaMessage * bmessage)
{
    GtkWidget *w;

    g_return_val_if_fail(bmessage != NULL, FALSE);

    if (bmessage->current_part == NULL
        || (w = bmessage->current_part->focus_widget) == NULL)
	return FALSE;

    return GTK_IS_EDITABLE(w) || GTK_IS_TEXT_VIEW(w);
}

void
balsa_message_copy_clipboard(BalsaMessage * bmessage)
{
    GtkWidget *w;

    g_return_if_fail(bmessage != NULL);
    g_return_if_fail(bmessage->current_part != NULL);
    g_return_if_fail((w = bmessage->current_part->focus_widget) != NULL);

    if (GTK_IS_EDITABLE(w))
        gtk_editable_copy_clipboard(GTK_EDITABLE(w));
    else if (GTK_IS_TEXT_VIEW(w)) {
        GtkTextBuffer *buffer =
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));
        GtkClipboard *clipboard = gtk_clipboard_get(GDK_NONE);
        gtk_text_buffer_copy_clipboard(buffer, clipboard);
    }
}

void
balsa_message_select_all(BalsaMessage * bmessage)
{
    GtkWidget *w;

    g_return_if_fail(bmessage != NULL);
    g_return_if_fail(bmessage->current_part != NULL);
    g_return_if_fail((w = bmessage->current_part->focus_widget) != NULL);

    if (GTK_IS_EDITABLE(w))
        gtk_editable_select_region(GTK_EDITABLE(w), 0, -1);
    else if (GTK_IS_TEXT_VIEW(w)) {
        GtkTextBuffer *buffer =
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));
        GtkTextIter start, end;

        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gtk_text_buffer_move_mark_by_name(buffer, "insert", &start);
        gtk_text_buffer_move_mark_by_name(buffer, "selection_bound", &end);
    }
}

/* rfc2298_address_equal
   compares two addresses according to rfc2298: local-part@domain is equal,
   if the local-parts are case sensitive equal, but the domain case-insensitive
*/
static gboolean 
rfc2298_address_equal(LibBalsaAddress *a, LibBalsaAddress *b)
{
    gchar *a_string, *b_string, *a_atptr, *b_atptr;
    gint a_atpos, b_atpos;

    if (!a || !b)
	return FALSE;

    a_string = libbalsa_address_to_gchar (a, -1);
    b_string = libbalsa_address_to_gchar (b, -1);
    
    /* first find the "@" in the two addresses */
    a_atptr = strchr (a_string, '@');
    b_atptr = strchr (b_string, '@');
    if (!a_atptr || !b_atptr) {
	g_free (a_string);
	g_free (b_string);
	return FALSE;
    }
    a_atpos = a_atptr - a_string;
    b_atpos = b_atptr - b_string;

    /* now compare the strings */
    if (!a_atpos || !b_atpos || a_atpos != b_atpos || 
	strncmp (a_string, b_string, a_atpos) ||
	g_ascii_strcasecmp (a_atptr, b_atptr)) {
	g_free (a_string);
	g_free (b_string);
	return FALSE;
    } else {
	g_free (a_string);
	g_free (b_string);
	return TRUE;
    }
}

static void
handle_mdn_request(LibBalsaMessage *message)
{
    gboolean suspicious, found;
    LibBalsaAddress *use_from, *addr;
    GList *list;
    BalsaMDNReply action;
    LibBalsaMessage *mdn;

    /* Check if the dispnotify_to address is equal to the (in this order,
       if present) reply_to, from or sender address. */
    if (message->reply_to)
	use_from = message->reply_to;
    else if (message->from)
	use_from = message->from;
    else if (message->sender)
	use_from = message->sender;
    else
	use_from = NULL;
    suspicious = !rfc2298_address_equal (message->dispnotify_to, use_from);
    
    if (!suspicious) {
	/* Try to find "my" address first in the to, then in the cc list */
	list = g_list_first(message->to_list);
	found = FALSE;
	while (list && !found) {
	    addr = list->data;
	    found = rfc2298_address_equal (balsa_app.current_ident->address, addr);
	    list = list->next;
	}
	if (!found) {
	    list = g_list_first(message->cc_list);
	    while (list && !found) {
		addr = list->data;
		found = rfc2298_address_equal (balsa_app.current_ident->address, addr);
		list = list->next;
	    }
	}
	suspicious = !found;
    }
    
    /* Now we decide from the settings of balsa_app.mdn_reply_[not]clean what
       to do...
    */
    if (suspicious)
	action = balsa_app.mdn_reply_notclean;
    else
	action = balsa_app.mdn_reply_clean;
    if (action == BALSA_MDN_REPLY_NEVER)
	return;
    
    /* We *may* send a reply, so let's create a message for that... */
    mdn = create_mdn_reply (message, action == BALSA_MDN_REPLY_ASKME);

    /* if the user wants to be asked, display a dialog, otherwise send... */
    if (action == BALSA_MDN_REPLY_ASKME) {
	gchar *sender;
	gchar *reply_to;
	
	sender = libbalsa_address_to_gchar (use_from, 0);
	reply_to = libbalsa_address_to_gchar (message->dispnotify_to, -1);
	gtk_widget_show_all (create_mdn_dialog (sender, reply_to, mdn));
	g_free (reply_to);
	g_free (sender);
    } else {
#if ENABLE_ESMTP
	libbalsa_message_send(mdn, balsa_app.outbox, NULL,
			      balsa_app.encoding_style,  
			      balsa_app.smtp_server,
			      balsa_app.smtp_authctx,
			      balsa_app.smtp_tls_mode,
			      balsa_app.send_rfc2646_format_flowed);
#else
	libbalsa_message_send(mdn, balsa_app.outbox, NULL,
			      balsa_app.encoding_style,
			      balsa_app.send_rfc2646_format_flowed);
#endif
	g_object_unref(G_OBJECT(mdn));
    }
}

static LibBalsaMessage *create_mdn_reply (LibBalsaMessage *for_msg, 
					  gboolean manual)
{
    LibBalsaMessage *message;
    LibBalsaMessageBody *body;
    gchar *date, *dummy;

    /* create a message with the header set from the incoming message */
    message = libbalsa_message_new();
    dummy = libbalsa_address_to_gchar(balsa_app.current_ident->address, 0);
    message->from = libbalsa_address_new_from_string(dummy);
    g_free (dummy);
    LIBBALSA_MESSAGE_SET_SUBJECT(message,
				 g_strdup("Message Disposition Notification"));
    dummy = libbalsa_address_to_gchar(for_msg->dispnotify_to, 0);
    message->to_list = libbalsa_address_new_list_from_string(dummy);
    g_free (dummy);

    /* the first part of the body is an informational note */
    body = libbalsa_message_body_new(message);
    date = libbalsa_message_date_to_gchar(for_msg, balsa_app.date_string);
    dummy = libbalsa_make_string_from_list(for_msg->to_list);
    body->buffer = g_strdup_printf(
	"The message sent on %s to %s with subject \"%s\" has been displayed.\n"
	"There is no guarantee that the message has been read or understood.\n\n",
	date, dummy, LIBBALSA_MESSAGE_GET_SUBJECT(for_msg));
    g_free (date);
    g_free (dummy);
    if (balsa_app.wordwrap)
	libbalsa_wrap_string(body->buffer, balsa_app.wraplength);
    body->charset = g_strdup ("ISO-8859-1");
    libbalsa_message_append_part(message, body);
    
    /* the second part is a rfc2298 compliant message/disposition-notification */
    body = libbalsa_message_body_new(message);
    dummy = libbalsa_address_to_gchar(balsa_app.current_ident->address, -1);
    body->buffer = g_strdup_printf("Reporting-UA: %s;" PACKAGE " " VERSION "\n"
				   "Final-Recipient: rfc822;%s\n"
				   "Original-Message-ID: %s\n"
				   "Disposition: %s-action/MDN-sent-%sly;displayed",
				   dummy, dummy, for_msg->message_id, 
				   manual ? "manual" : "automatic",
				   manual ? "manual" : "automatical");
    g_free (dummy);
    body->mime_type = g_strdup ("message/disposition-notification");
    body->charset = g_strdup ("US-ASCII");
    libbalsa_message_append_part(message, body);
    return message;
}

static GtkWidget *
create_mdn_dialog(gchar * sender, gchar * mdn_to_address,
                  LibBalsaMessage * send_msg)
{
    GtkWidget *mdn_dialog;

    mdn_dialog =
        gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window),
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_QUESTION,
                               GTK_BUTTONS_YES_NO,
                               _("The sender of this mail, %s, "
                                 "requested \n"
                                 "a Message Disposition Notification"
                                 "(MDN) to be returned to `%s'.\n"
                                 "Do you want to send "
                                 "this notification?"),
                               sender, mdn_to_address);
    gtk_window_set_title(GTK_WINDOW(mdn_dialog), _("reply to MDN?"));
    g_object_set_data(G_OBJECT(mdn_dialog), "balsa-send-msg", send_msg);
    g_signal_connect(G_OBJECT(mdn_dialog), "response",
                     G_CALLBACK(mdn_dialog_response), NULL);

    return mdn_dialog;
}

static void
mdn_dialog_response(GtkWidget * dialog, gint response, gpointer user_data)
{
    LibBalsaMessage *send_msg =
        LIBBALSA_MESSAGE(g_object_get_data(G_OBJECT(dialog),
                                           "balsa-send-msg"));

    if (response == GTK_RESPONSE_YES) {
#if ENABLE_ESMTP
        libbalsa_message_send(send_msg, balsa_app.outbox, NULL,
                              balsa_app.encoding_style,
                              balsa_app.smtp_server,
                              balsa_app.smtp_authctx,
                              balsa_app.smtp_tls_mode,
                              balsa_app.send_rfc2646_format_flowed);
#else
        libbalsa_message_send(send_msg, balsa_app.outbox, NULL,
                              balsa_app.encoding_style,
                              balsa_app.send_rfc2646_format_flowed);
#endif
    }

    g_object_unref(G_OBJECT(send_msg));
    gtk_widget_destroy(dialog);
}

/* quote_tag:
 * lookup the GtkTextTag for coloring quoted lines of a given level;
 * create the tag if it isn't found.
 *
 * returns NULL if the level is 0 (unquoted)
 */
static GtkTextTag *
quote_tag(GtkTextBuffer * buffer, gint level)
{
    GtkTextTag *tag = NULL;

    if (level > 0) {
        GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
        gchar *name;

        /* Modulus the quote level by the max,
         * ie, always have "1 <= quote level <= MAX"
         * this allows cycling through the possible
         * quote colors over again as the quote level
         * grows arbitrarily deep. */
        level = (level - 1) % MAX_QUOTED_COLOR;
        name = g_strdup_printf("quote-%d", level);
        tag = gtk_text_tag_table_lookup(table, name);

        if (!tag)
            tag =
                gtk_text_buffer_create_tag(buffer, name, "foreground-gdk",
                                           &balsa_app.quoted_color[level],
                                           NULL);
        g_free(name);
    }

    return tag;
}

/* pointer_over_url:
 * change style of a url and set/clear the status bar.
 */
static void
pointer_over_url(GtkWidget * widget, message_url_t * url, gboolean set)
{
    GtkTextBuffer *buffer;
    GtkTextTagTable *table;
    static const gchar name[] = "emphasize";
    GtkTextTag *tag;
    GtkTextIter start, end;

    if (!url)
        return;

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
    table = gtk_text_buffer_get_tag_table(buffer);
    tag = gtk_text_tag_table_lookup(table, name);
    if (!tag)
        tag = gtk_text_buffer_create_tag(buffer, name, 
                                         "underline",
                                         PANGO_UNDERLINE_SINGLE,
                                         "foreground", "red",
                                         NULL);

    gtk_text_buffer_get_iter_at_offset(buffer, &start, url->start);
    gtk_text_buffer_get_iter_at_offset(buffer, &end, url->end);
    
    if (set) {
        gtk_text_buffer_apply_tag(buffer, tag, &start, &end);
        balsa_gtk_html_on_url(NULL, url->url);
    } else {
        gtk_text_buffer_remove_tag(buffer, tag, &start, &end);
        balsa_gtk_html_on_url(NULL, NULL);
    }
}

/* find_url:
 * look in widget at coordinates x, y for a URL in url_list.
 */
static message_url_t *
find_url(GtkWidget * widget, gint x, gint y, GList * url_list)
{
    GtkTextIter iter;
    gint offset;
    message_url_t *url;

    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget),
                                          GTK_TEXT_WINDOW_TEXT,
                                          x, y, &x, &y);
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, x, y);
    offset = gtk_text_iter_get_offset(&iter);

    while (url_list) {
        url = (message_url_t *) url_list->data;
        if (url->start <= offset && offset < url->end)
            return url;
        url_list = g_list_next(url_list);
    }

    return NULL;
}
