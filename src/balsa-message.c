/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* vim:set ts=4 sw=4 ai: */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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
/*  gdkx needed for GDK_FONT_XFONT */
#include <gdk/gdkx.h>

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
#include <gtkhtml/gtkhtml.h>
#include <gtkhtml/gtkhtml-stream.h>
#endif

#ifdef HAVE_PCRE
#  include <pcreposix.h>
#else
#  include <sys/types.h>
#  include <regex.h>
#endif

#include "quote-color.h"
#include "sendmsg-window.h"

#ifdef HAVE_GNOME_VFS
# include <libgnomevfs/gnome-vfs-mime-handlers.h>
#endif

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

static void message_destroyed_cb(LibBalsaMessage * message,
				 BalsaMessage * bm);

static void display_headers(BalsaMessage * bm);
static void display_content(BalsaMessage * bm);

static void display_part(BalsaMessage * bm, LibBalsaMessageBody * body);
static void display_multipart(BalsaMessage * bm,
			      LibBalsaMessageBody * body);

static void save_part(BalsaPartInfo * info);

static void select_icon_cb(GnomeIconList * ilist, gint num,
			   GdkEventButton * event, BalsaMessage * bm);
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

static void balsa_gtk_text_size_request(GtkWidget * widget,
					GtkRequisition * requisition,
					gpointer data);
#ifdef HAVE_GTKHTML
static void balsa_gtk_html_size_request(GtkWidget * widget,
					GtkRequisition * requisition,
					gpointer data);
static gboolean balsa_gtk_html_url_requested(GtkWidget *html, const gchar *url,
					     GtkHTMLStream* stream,
					     LibBalsaMessage* msg);
static void balsa_gtk_html_link_clicked(GtkWidget *html, 
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
#ifdef HAVE_GNOME_VFS
static GtkWidget* part_info_mime_button_vfs (BalsaPartInfo* info, const gchar* content_type);
#endif
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
static void mdn_dialog_delete (GtkWidget *dialog, GdkEvent *event, gpointer user_data);
static void no_mdn_reply (GtkWidget *widget, gpointer user_data);
static void send_mdn_reply (GtkWidget *widget, gpointer user_data);

static BalsaPartInfo* part_info_new(LibBalsaMessageBody* body,
				    LibBalsaMessage* msg);
static void part_info_free(BalsaPartInfo* info);

guint balsa_message_get_type()
{
    static guint balsa_message_type = 0;

    if (!balsa_message_type) {
	GtkTypeInfo balsa_message_info = {
	    "BalsaMessage",
	    sizeof(BalsaMessage),
	    sizeof(BalsaMessageClass),
	    (GtkClassInitFunc) balsa_message_class_init,
	    (GtkObjectInitFunc) balsa_message_init,
	    (GtkArgSetFunc) NULL,
	    (GtkArgGetFunc) NULL,
	    (GtkClassInitFunc) NULL
	};

	balsa_message_type =
	    gtk_type_unique(gtk_viewport_get_type(), &balsa_message_info);
    }

    return balsa_message_type;
}


static void
balsa_message_class_init(BalsaMessageClass * klass)
{
    GtkObjectClass *object_class;

    object_class = GTK_OBJECT_CLASS(klass);

    balsa_message_signals[SELECT_PART] =
	gtk_signal_new("select-part",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(BalsaMessageClass, select_part),
		       gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

    gtk_object_class_add_signals(object_class, balsa_message_signals,
				 LAST_SIGNAL);

    object_class->destroy = balsa_message_destroy;

    parent_class = gtk_type_class(gtk_viewport_get_type());

    klass->select_part = NULL;

}

static void
balsa_message_init(BalsaMessage * bm)
{
    bm->table = gtk_table_new(3, 1, FALSE);
    gtk_container_add(GTK_CONTAINER(bm), bm->table);

    gtk_widget_show(bm->table);

    bm->content = gtk_vbox_new(FALSE, 0);

    gtk_table_attach(GTK_TABLE(bm->table), bm->content, 0, 1, 1,
		     2, GTK_EXPAND | GTK_FILL,
		     GTK_EXPAND | GTK_FILL, 0, 1);
    gtk_widget_show(bm->content);

    bm->header_text = gtk_text_new(NULL, NULL);
    gtk_signal_connect(GTK_OBJECT(bm->header_text), "key_press_event",
		       (GtkSignalFunc) balsa_message_key_press_event,
		       (gpointer) bm);
    gtk_signal_connect(GTK_OBJECT(bm->header_text), "size_request",
		       (GtkSignalFunc) balsa_gtk_text_size_request,
		       (gpointer) bm);

    gtk_table_attach(GTK_TABLE(bm->table), bm->header_text, 0, 1, 0, 1,
		     GTK_EXPAND | GTK_FILL, 0, 0, 1);

    bm->part_list = gnome_icon_list_new(100, NULL, FALSE);

    gnome_icon_list_set_selection_mode(GNOME_ICON_LIST(bm->part_list),
				       GTK_SELECTION_MULTIPLE);
    gtk_signal_connect(GTK_OBJECT(bm->part_list), "select_icon",
		       GTK_SIGNAL_FUNC(select_icon_cb), bm);
    gtk_signal_connect(GTK_OBJECT(bm->part_list), "size_request",
		       GTK_SIGNAL_FUNC(balsa_icon_list_size_request),
		       (gpointer) bm);

    gtk_table_attach(GTK_TABLE(bm->table), bm->part_list, 0, 1, 2, 3,
		     GTK_EXPAND | GTK_FILL, 0, 0, 1);

    bm->current_part = NULL;
    bm->message = NULL;

    bm->wrap_text = balsa_app.browse_wrap;
    bm->shown_headers = balsa_app.shown_headers;

}

static void
balsa_message_destroy(GtkObject * object)
{
    BalsaMessage* bm = BALSA_MESSAGE(object);
    balsa_message_set(bm, NULL);
    gtk_widget_destroy(bm->part_list);

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
save_dialog_ok(GtkWidget* button, GtkWidget* save_dialog)
{
    gchar *filename;
    gboolean do_save, result;
    BalsaPartInfo * info;

    gtk_widget_hide(GTK_WIDGET(save_dialog)); 
    info = gtk_object_get_user_data(GTK_OBJECT(save_dialog));
    filename 
	= gtk_file_selection_get_filename(GTK_FILE_SELECTION(save_dialog));
    
    g_free(balsa_app.save_dir);
    balsa_app.save_dir = g_dirname(filename);
    
    if ( access(filename, F_OK) == 0 ) {
	GtkWidget *confirm;
	
	/* File exists. check if they really want to overwrite */
	confirm = gnome_question_dialog_modal_parented(
	    _("File already exists. Overwrite?"),
	    NULL, NULL, GTK_WINDOW(balsa_app.main_window));
	do_save = (gnome_dialog_run_and_close(GNOME_DIALOG(confirm)) == 0);
	if(do_save)
	    unlink(filename);
    } else
	do_save = TRUE;
    
    if ( do_save ) {
	result = libbalsa_message_body_save(info->body, NULL, filename);
	if (!result) {
	    gchar *msg;
	    GtkWidget *msgbox;
	    
	    msg = g_strdup_printf(_(" Could not save %s: %s"), 
				  filename, strerror(errno));
	    msgbox = gnome_error_dialog_parented(msg, GTK_WINDOW
						 (balsa_app.main_window));
	    g_free(msg);
	    gnome_dialog_run_and_close(GNOME_DIALOG(msgbox));
	}
    }
    gtk_object_destroy(GTK_OBJECT(save_dialog));
}

static void
save_part(BalsaPartInfo * info)
{
    gchar *filename;
    GtkFileSelection *save_dialog;
    
    g_return_if_fail(info != 0);

    save_dialog = 
	GTK_FILE_SELECTION(gtk_file_selection_new(_("Save MIME Part")));
    /* start workaround for prematurely realized widget returned
     * by some GTK+ versions */
    if(GTK_WIDGET_REALIZED(save_dialog))
        gtk_widget_unrealize(GTK_WIDGET(save_dialog));
    /* end workaround for prematurely realized widget */
    gtk_window_set_wmclass(GTK_WINDOW(save_dialog), "save_part", "Balsa");

    if (balsa_app.save_dir)
	filename = g_strdup_printf("%s/%s", balsa_app.save_dir,
				   info->body->filename 
				   ? info->body->filename : "");
    else if(!balsa_app.save_dir && info->body->filename)
	filename = g_strdup(info->body->filename);
    else filename = NULL;

    if (filename) {
	gtk_file_selection_set_filename(save_dialog, filename);
	g_free(filename);
    }

    gtk_object_set_user_data(GTK_OBJECT(save_dialog), info);
    gtk_widget_set_parent_window(GTK_WIDGET(save_dialog),
				 GTK_WIDGET(balsa_app.main_window)->window);
    gtk_signal_connect(GTK_OBJECT(save_dialog->ok_button), "clicked",
		       (GtkSignalFunc) save_dialog_ok, save_dialog);
    gtk_signal_connect_object(GTK_OBJECT(save_dialog->cancel_button), 
			      "clicked",
			      (GtkSignalFunc)gtk_widget_destroy,
			      GTK_OBJECT(save_dialog));

    gtk_window_set_modal(GTK_WINDOW(save_dialog), TRUE);
    gtk_widget_show_all(GTK_WIDGET(save_dialog));
}

GtkWidget *
balsa_message_new(void)
{
    BalsaMessage *bm;

    bm = gtk_type_new(balsa_message_get_type());

    return GTK_WIDGET(bm);
}

static void
select_icon_cb(GnomeIconList * ilist, gint num, GdkEventButton * event,
	       BalsaMessage * bm)
{

    BalsaPartInfo *info;

    if (event == NULL)
	return;

    if (event->button == 1) {
	select_part(bm, num);
    } else if (event->button == 3) {
	info = (BalsaPartInfo *) gnome_icon_list_get_icon_data(ilist, num);
	
	g_assert(info != NULL);

	if (info->popup_menu) {
	    gtk_menu_popup(GTK_MENU(info->popup_menu),
			   NULL, NULL, NULL, NULL,
			   event->button, event->time);
	}
    }
}

static void
message_destroyed_cb(LibBalsaMessage * message, BalsaMessage * bm)
{
    if (bm->message == message)
	balsa_message_set(bm, NULL);
}

void
balsa_message_clear(BalsaMessage * bm)
{
    g_return_if_fail(bm != NULL);

    balsa_message_set(bm, NULL);

}

/* balsa_message_set:
   returns TRUE on success, FALSE on failure (message content could not be
   accessed).
*/
gboolean
balsa_message_set(BalsaMessage * bm, LibBalsaMessage * message)
{
    gboolean had_focus;
    gboolean is_new;

    g_return_val_if_fail(bm != NULL, FALSE);

    /* Leave this out. When settings (eg wrap) are changed it is OK to 
       call message_set with the same messagr */
    /*    if (bm->message == message) */
    /*      return; */

    had_focus = bm->content_has_focus;

    select_part(bm, -1);
    if (bm->message != NULL) {
	gtk_signal_disconnect_by_func(GTK_OBJECT(bm->message),
				      GTK_SIGNAL_FUNC
				      (message_destroyed_cb),
				      (gpointer) bm);
	libbalsa_message_body_unref(bm->message);
    }
    bm->message = NULL;
    bm->part_count = 0;
    gnome_icon_list_clear(GNOME_ICON_LIST(bm->part_list));

    if (message == NULL) {
	gtk_widget_hide(bm->header_text);
	return TRUE;
    }

    bm->message = message;

    gtk_signal_connect(GTK_OBJECT(message), "destroy",
		       GTK_SIGNAL_FUNC(message_destroyed_cb),
		       (gpointer) bm);

    is_new = message->flags & LIBBALSA_MESSAGE_FLAG_NEW;
    if(!libbalsa_message_body_ref(bm->message, TRUE)) 
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
    if (bm->part_count == 0) {
	gtk_widget_hide(bm->part_list);

	/* This is really annoying if you are browsing, since you keep
           getting a dialog... */
	/* balsa_information(LIBBALSA_INFORMATION_WARNING, _("Message
           contains no parts!")); */
	return TRUE;
    }

    gnome_icon_list_select_icon(GNOME_ICON_LIST(bm->part_list), 0);

    select_part(bm, 0);
    if ( /*had_focus&& */ bm->current_part
	&& bm->current_part->focus_widget)
	gtk_widget_grab_focus(bm->current_part->focus_widget);

    /* We show the part list if:
     *    there is > 1 part
     * or we don't know how to display the one part.
     */
    if (bm->part_count > 1) {
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
    /*    GtkWidget *w; */
    GdkFont *fnt;
    gchar pad[] = "                ";
    gchar cr[] = "\n";
    gchar *line_start, *line_end;
    gchar *wrapped_value;
    const gchar *msgcharset;

    if (!(bm->shown_headers == HEADERS_ALL || libbalsa_find_word(header, balsa_app.selected_headers))) 
	return;

    /* always display the label in the predefined font */
    if (strcmp(header, "subject") != 0)
	fnt = gdk_font_load(balsa_app.message_font);
    else
	fnt = gdk_font_load(balsa_app.subject_font);

    gtk_text_insert(GTK_TEXT(bm->header_text), fnt, NULL, NULL, label, -1);
    gdk_font_unref(fnt);
    
    /* select the font for the value according to the msg charset */
    if ((msgcharset = libbalsa_message_charset(bm->message))) {
	if (strcmp(header, "subject") != 0)
	    fnt = balsa_get_font_by_charset(balsa_app.message_font, 
					    msgcharset);
	else
	    fnt = balsa_get_font_by_charset(balsa_app.subject_font,
					    msgcharset);
    } else {
	if (strcmp(header, "subject") != 0)
	    fnt = gdk_font_load(balsa_app.message_font);
	else
	    fnt = gdk_font_load(balsa_app.subject_font);
    }
	
    if (value && *value != '\0') {
	if (strlen(label) < 15)
	    gtk_text_insert(GTK_TEXT(bm->header_text), fnt, NULL, NULL,
			    pad, 15 - strlen(label));
	else
	    gtk_text_insert(GTK_TEXT(bm->header_text), fnt, NULL, NULL,
			    pad, 1);

	wrapped_value = g_strdup(value);
	libbalsa_wrap_string(wrapped_value, balsa_app.wraplength - 15);

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
		gtk_text_insert(GTK_TEXT(bm->header_text), fnt, NULL, NULL,
				pad, 15);
	    gtk_text_insert(GTK_TEXT(bm->header_text), fnt, NULL, NULL,
			    line_start, line_end - line_start);
	    gtk_text_insert(GTK_TEXT(bm->header_text), fnt, NULL, NULL, cr,
			    -1);
	    if (*line_end != '\0')
		line_end++;
	}
	g_free(wrapped_value);
    } else {
	gtk_text_insert(GTK_TEXT(bm->header_text), fnt, NULL, NULL, cr,
			-1);
    }
    gdk_font_unref(fnt);
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
    LibBalsaMessage *message = bm->message;
    GList *p, *lst;
    gchar **pair, *hdr;
    gchar *date;

    gtk_editable_delete_text(GTK_EDITABLE(bm->header_text), 0, -1);
 
    if (bm->shown_headers == HEADERS_NONE) {
	gtk_widget_hide(bm->header_text);
	return;
    } else {
	gtk_widget_show(bm->header_text);
    }

    gtk_text_freeze(GTK_TEXT(bm->header_text));

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

    gtk_text_thaw(GTK_TEXT(bm->header_text));

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
    GdkPixbuf *pb;

    GdkPixmap *pixmap = NULL;
    GdkBitmap *mask = NULL;

    GtkWidget *image;

    libbalsa_message_body_save_temporary(info->body, NULL);

    if( (pb = gdk_pixbuf_new_from_file(info->body->temp_filename)) ) {
	gdk_pixbuf_render_pixmap_and_mask(pb, &pixmap, &mask, 0);
	gdk_pixbuf_unref(pb);
    } else {
	g_print
	    (_("balsa/pixbuf: Could not load image. It is likely corrupted."));
	return;
    }

    if (pixmap) {
	image = gtk_pixmap_new(pixmap, mask);

	info->widget = image;
	info->focus_widget = image;
	info->can_display = TRUE;
    }

    if (pixmap)
	gdk_pixmap_unref(pixmap);
    if (mask)
	gdk_bitmap_unref(mask);

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
	g_string_sprintfa(msg, _("Access type: local-file\n"));
	g_string_sprintfa(msg, _("File name: %s"), local_name);
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
	g_string_sprintfa(msg, _("Access type: URL\n"));
	g_string_sprintfa(msg, _("URL: %s"), url);
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
	g_string_sprintfa(msg, _("Access type: %s\n"),
			  url_type == RFC2046_EXTBODY_TFTP ? "tftp" :
			  url_type == RFC2046_EXTBODY_FTP ? "ftp" : "anon-ftp");
	g_string_sprintfa(msg, _("FTP site: %s\n"), ftp_site);
	if (ftp_dir)
	    g_string_sprintfa(msg, _("Directory: %s\n"), ftp_dir);
	g_string_sprintfa(msg, _("File name: %s"), ftp_name);
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
    gtk_object_set_data(GTK_OBJECT(button), "call_url", url);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(part_context_menu_call_url),
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
    msg = g_string_append (msg, _("Access type: mail-server\n"));
    g_string_sprintfa(msg, _("Mail server: %s\n"), mail_site);
    if (mail_subject)
	g_string_sprintfa(msg, _("Subject: %s\n"), mail_subject);
    g_free(mail_subject);
    g_free(mail_site);

    /* now create the widget... */
    vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new(msg->str), FALSE, FALSE, 1);
    g_string_free(msg, TRUE);

    button = gtk_button_new_with_label(_("Send message to obtain this part"));
    gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 5);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(part_context_menu_mail),
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
    if (!g_strcasecmp("message/external-body", body_type)) {
	gchar *access_type;
	rfc_extbody_id *extbody_type = rfc_extbodys;

	access_type = 
	    libbalsa_message_body_get_parameter(info->body, "access-type");
	while (extbody_type->id_string && 
	       g_strcasecmp(extbody_type->id_string, access_type))
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
#ifdef HAVE_GNOME_VFS
    if((button=part_info_mime_button_vfs(info, content_type))) {
	gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 2);
    } else
#endif
    if ((cmd = gnome_mime_get_value(content_type, "view")) != NULL) {
        button = part_info_mime_button (info, content_type, "view");
	gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 2);
    } else if ((cmd = gnome_mime_get_value (content_type, "open")) != NULL) {
        button = part_info_mime_button (info, content_type, "open");
	gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 2);
    } else {
	gtk_box_pack_start(GTK_BOX(vbox),
			   gtk_label_new(_("No open or view action defined in GNOME MIME for this content type")),
			   FALSE, FALSE, 1);
    }

#ifdef HAVE_GNOME_VFS
    if((content_desc=gnome_vfs_mime_get_description(content_type)))
	msg = g_strdup_printf(_("Type: %s (%s)"), content_desc, content_type);
    else
#endif
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
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       (GtkSignalFunc) part_context_menu_save,
		       (gpointer) info);

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
    

    cmd = gnome_mime_get_value (content_type, (char*) key);
    msg = g_strdup_printf(_("View part with %s"), cmd);
    button = gtk_button_new_with_label(msg);
    gtk_object_set_data (GTK_OBJECT (button), "mime_action",  (gpointer) key);
    g_free(msg);

    gtk_signal_connect(GTK_OBJECT(button), "clicked",
                       (GtkSignalFunc) part_context_menu_cb,
                       (gpointer) info);

    return button;
}


#ifdef HAVE_GNOME_VFS
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
	gtk_object_set_data (GTK_OBJECT (button), "mime_action", 
			     (gpointer) g_strdup(app->id)); /* *** */
	g_free(msg);

	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			   (GtkSignalFunc) part_context_menu_vfs_cb,
			   (gpointer) info);

	gnome_vfs_mime_application_free(app);
	
    }
    return button;
}
#endif

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

/* the name should really be one_or_two_const_fields_to_end */
static gint
two_const_fields_to_end(const gchar * ptr)
{
    int cnt = 0;

    while (*ptr && cnt < 3) {
	if (*ptr == '*')
	    return 0;
	if (*ptr++ == '-')
	    cnt++;
    }
    return cnt < 3;
}

/* get_font_name returns font name based on given font 
   wildcard 'base' and given character set encoding.
   Algorithm: copy max first 12 fields, cutting additionally 
   at most two last, if they are constant.
   FIXME: this data duplicates information in sendmsg-window.c
*/
static struct {
    gchar *charset, *font_postfix;
    gboolean use_fontset;
} charset2font[] = {
    {"iso-8859-1", "iso8859-1", FALSE}, 
    {"iso-8859-2", "iso8859-2", FALSE}, 
    {"iso-8859-3", "iso8859-3", FALSE}, 
    {"iso-8859-4", "iso8859-4", FALSE}, 
    {"iso-8859-5", "iso8859-5", FALSE}, 
    {"iso-8859-7", "iso8859-7", FALSE}, 
    {"iso-8859-9", "iso8859-9", FALSE},
    {"iso-8859-13", "iso8859-13", FALSE}, 
    {"iso-8859-14", "iso8859-14", FALSE}, 
    {"iso-8859-15", "iso8859-15", FALSE}, 
    {"euc-jp", "jisx0208.1983-0", TRUE}, 
    {"euc-kr", "ksc5601.1987-0", TRUE}, 
    {"koi-8-r", "koi8-r", FALSE}, 
    {"koi-8-u", "koi8-u", FALSE}, 
    {"koi8-r",  "koi8-r", FALSE}, 
    {"koi8-u",  "koi8-u", FALSE}, 
    {"windows-1251", "microsoft-cp1251", FALSE}, 
    {"windows-1251", "mswcyr-1", FALSE}, 
    {"us-ascii", "iso8859-1", FALSE}
};

/* get_font_name:
   returns a font name corresponding to given font and charset.
   If use_fontset is provided, it will pass the information if fontset is
   recommended.
*/
static gchar *
get_font_name(const gchar * base, const gchar * charset, 
	      gboolean * use_fontset)
{
    gchar *res;
    const gchar *ptr = base, *postfix = NULL;
    int dash_cnt = 0, len, i;

    g_return_val_if_fail(base != NULL, NULL);
    g_return_val_if_fail(charset != NULL, NULL);

    for (i = ELEMENTS(charset2font) - 1; i >= 0; i--)
	if (g_strcasecmp(charset, charset2font[i].charset) == 0) {
	    postfix = charset2font[i].font_postfix;
	    if(use_fontset) *use_fontset = charset2font[i].use_fontset;
	    break;
	}
    if (!postfix)
	return g_strdup(base);	/* print warning here? */

    /* assemble the XLFD */
    while (*ptr) {
	if (*ptr == '-')
	    dash_cnt++;
	if (dash_cnt >= 13)
	    break;
	if (two_const_fields_to_end(ptr))
	    break;
	ptr++;
    }

    /* defense against a patologically short base font wildcard implemented
     * in the chunk below
     * extra space for two dashes and '\0' */
    len = ptr - base;
    /* if(dash_cnt>12) len--; */
    if (len < 1)
	len = 1;
    res = (gchar *) g_malloc (len + strlen(postfix) + 2);
    if (balsa_app.debug)
	fprintf(stderr, "* base font name: %s\n*    and postfix: %s\n"
		"*    mallocating: %d bytes\n", base, postfix,
		len + strlen(postfix) + 2);

    if (len > 1)
	strncpy(res, base, len);
    else
	*res='*';

    res[len] = '-';
    strcpy(res + len + 1, postfix);
    return res;
}

/* balsa_get_font_by_charset:
   returns font or fontset as specified by general base and charset.
   Follows code from gfontsel except from the fact that it tries 
   to never return NULL.
*/
GdkFont*
balsa_get_font_by_charset(const gchar * base, const gchar * charset)
{
    gchar * fontname;
    GdkFont *result;
    gboolean fs;
    XFontStruct *xfs;

    fontname = get_font_name(base, charset, &fs);
    result   = gdk_font_load (fontname);
    xfs = result ? GDK_FONT_XFONT (result) : NULL;

    if (!xfs || (xfs->min_byte1 != 0 || xfs->max_byte1 != 0))
    {
	gchar *tmp_name;
	g_print("balsa_get_font_by_charset: using fontset\n");
	if(result) gdk_font_unref (result);
	tmp_name = g_strconcat (fontname, ",*", NULL);
	result = gdk_fontset_load (tmp_name);
	g_free (tmp_name);
    }

    if(!result)
	g_print("Cannot find font: %s for charset %s\n", fontname, charset);

    g_free (fontname);
    return result;
}


/* HELPER FUNCTIONS ----------------------------------------------- */
static GdkFont*
find_body_font(LibBalsaMessageBody * body)
{
    gchar *charset;
    GdkFont * font = NULL;

    charset = libbalsa_message_body_get_parameter(body, "charset");

    if (charset) {
	font = balsa_get_font_by_charset(balsa_app.message_font, charset);
	g_free(charset);
    }
    return font;
}


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
    guint line, start, end;      /* text line and pos in the line */
    gchar *url;                  /* the link */
    gboolean is_mailto;          /* open sendmsg window or external URL call */
} message_url_t;

typedef struct _hotarea_t {
    gint xul, yul, xlr, ylr;     /* positions within the text widget */
    message_url_t *url;          /* the link */
} hotarea_t;

#define LINE_WRAP_ROOM           8      /* from gtk_text... */
#define DEFAULT_TAB_STOP_WIDTH   8      /* chars per tab */

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

static void
free_hotarea_list(GList *l)
{
    if (l) {
	GList *p = l;

	while (p) {
	    g_free(p->data);
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

/* do a gtk_text_insert, but mark URL's with balsa_app.url_color */
static void
gtk_text_insert_with_url(GtkText *text, GdkFont *font, GdkColor *dflt, 
			 const char *chars, regex_t *url_reg,
			 GList **url_list, gint textline)
{
    gint match, offset = 0;
    regmatch_t url_match;
    gchar *p, *buf;

    /* replace tabs with the correct number of spaces */
    if (strchr(chars,'\t')) { 
	gchar *src, *dst;
	gint n = 0;
	
	buf = p = dst = 
	    g_strnfill(DEFAULT_TAB_STOP_WIDTH * strlen(chars), ' ');
	for (src = (gchar *)chars; *src; src++, n++) {
	    if (*src == '\t') {
		gint newpos = 
		    ((n / DEFAULT_TAB_STOP_WIDTH) + 1) * DEFAULT_TAB_STOP_WIDTH;
		dst += newpos - n;
		n = newpos - 1;
	    } else
		*dst++ = *src;
	}
	*dst = 0;
    } else
	buf = p = g_strdup(chars);

    if (prescanner(p)) {
	match = regexec(url_reg, p, 1, &url_match, 0);
	while (!match) {
	    gchar *buf;
	    message_url_t *url_found;
	    
	    if (url_match.rm_so) {
		buf = g_strndup(p, url_match.rm_so);
		gtk_text_insert(text, font, dflt, NULL, buf, -1);
		g_free(buf);
	    }
	    
	    buf = g_strndup(p + url_match.rm_so, 
			    url_match.rm_eo - url_match.rm_so);
	    gtk_text_insert(text, font, &balsa_app.url_color, NULL, buf, -1);
	    
	    /* remember the URL and its position within the text */
	    url_found = g_malloc(sizeof(message_url_t));
	    url_found->line = textline;
	    url_found->start = url_match.rm_so + offset;
	    url_found->end = url_match.rm_eo + offset;
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
	gtk_text_insert(text, font, dflt, NULL, p, -1);
    g_free(buf);
}

/* set the gtk_text widget's cursor to a vertical bar
   fix event mask so that pointer motions are reported (if necessary) */
static gboolean
fix_text_widget(GtkWidget *widget, gpointer data)
{
    GdkWindow *w = GTK_TEXT(widget)->text_area;
    
    if (data)
	gdk_window_set_events(w, gdk_window_get_events(w) | GDK_POINTER_MOTION_MASK);
    if (!url_cursor_normal || !url_cursor_over_url) {
	url_cursor_normal = gdk_cursor_new(GDK_XTERM);
	url_cursor_over_url = gdk_cursor_new(GDK_HAND2);
    }
    gdk_window_set_cursor(w, url_cursor_normal);
    return FALSE;
}

static void
calc_text_end(const gchar *buf, gint *xpos, gint *linepos, GdkFont *fnt,
	      gint winwidth, gboolean is_last, GList **hotarea_list,
	      message_url_t *ref)
{
    gint width, rmargin;
    hotarea_t *new_area;

    rmargin = winwidth - LINE_WRAP_ROOM;
    gdk_string_extents(fnt, buf, NULL, NULL, &width, NULL, NULL);

    if (*xpos + width > rmargin) {
	gchar *rempart, *p;
	rempart = p = g_strdup(buf);
	
	while (*xpos + width > rmargin) {
	    gchar *test;
	    gint n;

	    test = g_strdup(rempart);
	    n = strlen(rempart);
	    do {
		n--;
		test [n] = 0;
		gdk_string_extents(fnt, test, NULL, NULL, &width, NULL, NULL);
	    } while (n && *xpos + width > winwidth - LINE_WRAP_ROOM);
	    g_free(test);
	    rempart += n;
	    
	    if (ref) {
		/* save this area */
		new_area = g_malloc(sizeof(hotarea_t));
		new_area->xul = *xpos;
		new_area->yul = *linepos;
		new_area->xlr = *xpos + width;
		new_area->ylr = *linepos + fnt->ascent + fnt->descent;
		new_area->url = ref;
		*hotarea_list = g_list_append(*hotarea_list, new_area);
	    }
	    *xpos = 0;
	    *linepos += fnt->ascent + fnt->descent;
	    gdk_string_extents(fnt, rempart, NULL, NULL, &width, NULL, NULL);
	}
	g_free(p);
    } 

    if (ref) {
	/* save this area */
	new_area = g_malloc(sizeof(hotarea_t));
	new_area->xul = *xpos;
	new_area->yul = *linepos;
	new_area->xlr = *xpos + width;
	new_area->ylr = *linepos + fnt->ascent + fnt->descent;
	new_area->url = ref;
	*hotarea_list = g_list_append(*hotarea_list, new_area);
    }
    *xpos += width;
}

/* Upon text (re)draw, collect the URL positions and types in pixel coords */
static gboolean
mail_text_draw(GtkWidget *widget, GdkRectangle *area, gpointer data)
{
    guint winwidth, linepos, textline;
    gchar **l = NULL, **lines = NULL, *buf;
    GdkFont *fnt = (GdkFont *)data;
    GList *hotarea_list = 
	(GList *)gtk_object_get_data(GTK_OBJECT(widget), "hotarea-list");
    GList *url_list = 
	(GList *)gtk_object_get_data(GTK_OBJECT(widget), "url-list");

    g_return_val_if_fail(url_list, FALSE); /* this should not happen... */

    gtk_object_set_data(GTK_OBJECT(widget), "hotarea-list", NULL);
    free_hotarea_list(hotarea_list);
    hotarea_list = NULL;
    
    gdk_window_get_size(GTK_TEXT(widget)->text_area, &winwidth, NULL);
    linepos = 0;
    textline = 0;

    buf = gtk_editable_get_chars(GTK_EDITABLE(widget), 0, -1);
    lines = l = g_strsplit(buf, "\n", -1);
    g_free(buf);
    
    for (buf = *lines; buf && url_list; buf = *(++lines), textline++) {
	gint xpos = 0, last_end = 0;
	gchar *part;

	/* check if there is an URI (left) in this line */
	while (url_list && 
	       ((message_url_t *)url_list->data)->line == textline) {
	    message_url_t *current_url = (message_url_t *)url_list->data;

	    /* handle text in front of the uri */
	    part = g_strndup(buf + last_end, current_url->start - last_end);
	    calc_text_end(part, &xpos, &linepos, fnt, winwidth, FALSE, NULL,
			  NULL);
	    g_free(part);

	    /* handle the uri itself */
	    part = g_strndup(buf + current_url->start, 
			     current_url->end - current_url->start);
	    calc_text_end(part, &xpos, &linepos, fnt, winwidth, 
			  current_url->end == strlen(buf), &hotarea_list, 
			  current_url);
	    g_free(part);

	    last_end = current_url->end;

	    url_list = g_list_next(url_list);
	}

	/* wrap the remaining part of the line */
	part = g_strdup(buf + last_end);
	calc_text_end(part, &xpos, &linepos, fnt, winwidth, TRUE, NULL, NULL);
	g_free(part);
	linepos += fnt->ascent + fnt->descent;
    }

    g_strfreev(l);

    gtk_object_set_data(GTK_OBJECT(widget), "hotarea-list", hotarea_list);
    return FALSE;
}

/* check if we are over an url and change the cursor in this case */
static gboolean
check_over_url(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    GList *hotarea_list = 
	(GList *)gtk_object_get_data(GTK_OBJECT(widget), "hotarea-list");
    gint x, y;
    GdkModifierType mask;
    static gboolean was_over_url = FALSE;
    static hotarea_t *current_url = NULL;

    if (!hotarea_list)
	return FALSE;

    gdk_window_get_pointer(GTK_TEXT(widget)->text_area, &x, &y, &mask);
    while (hotarea_list) {
	hotarea_t *hotarea_data = (hotarea_t *)hotarea_list->data;

	if (hotarea_data->xul <= x && x <= hotarea_data->xlr &&
	    hotarea_data->yul <= y && y <= hotarea_data->ylr) {
	    if (!url_cursor_normal || !url_cursor_over_url) {
		url_cursor_normal = gdk_cursor_new(GDK_LEFT_PTR);
		url_cursor_over_url = gdk_cursor_new(GDK_HAND2);
	    }
	    if (!was_over_url) {
		gdk_window_set_cursor(GTK_TEXT(widget)->text_area,
				      url_cursor_over_url);
		was_over_url = TRUE;
	    }
	    if (hotarea_data != current_url) {
		if (current_url)
		    balsa_gtk_html_on_url(NULL, NULL);
		balsa_gtk_html_on_url(NULL, hotarea_data->url->url);
		current_url = hotarea_data;
	    }
	    return FALSE;
	}
	hotarea_list = g_list_next(hotarea_list);
    }

    if (was_over_url) {
	gdk_window_set_cursor(GTK_TEXT(widget)->text_area, url_cursor_normal);
	balsa_gtk_html_on_url(NULL, NULL);
	was_over_url = FALSE;
	current_url = NULL;
    }

    return FALSE;
}

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
	gnome_appbar_set_status(balsa_app.appbar, notice);
	g_free(notice);
	gnome_url_show(url->url);
    }
}

/* store the coordinates at which the button was pressed */
static gint stored_x=-1, stored_y=-1;
static GdkModifierType stored_mask=-1;

static gboolean
store_button_coords(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
       gdk_window_get_pointer(GTK_TEXT(widget)->text_area,
                              &stored_x, &stored_y, &stored_mask);
       /* Take this button press out of the mask, so it won't interfere
        * with the comparison in check_call_url()
        * FIXME Is the mask comparison necessary?  Or should it be
        * there, but only compare shift, ctrl, and mod1-mod5?
        */
       stored_mask &= ~(GDK_BUTTON_PRESS_MASK);
    }
    return FALSE;
}

/* if the mouse button was released over an URL, and the mouse hasn't
 * moved since the button was pressed, try to call the URL */
static gboolean 
check_call_url(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    GList *hotarea_list;
    gint x, y;
    GdkModifierType mask;

    if ( !(event->type == GDK_BUTTON_RELEASE && event->button == 1) ) 
        return FALSE;

    hotarea_list = gtk_object_get_data(GTK_OBJECT(widget), "hotarea-list");
    
    if (!hotarea_list)
	return FALSE;

    gdk_window_get_pointer(GTK_TEXT(widget)->text_area, &x, &y, &mask);
    /* FIXME Allow some slop here?
     * Eg, if cursor has moved less than 2 pixels, follow the link:
     *   if ((x >= stored_x-2) && (x <= stored_x+2) &&
     *       (y >= stored_y-2) && (y <= stored_y+2) &&
     *       (mask == stored_mask))
     *
     * Is there some gnome setting that can be used where the
     * +/- 2 (or whatever) appears?
     */
    if (x == stored_x && y == stored_y && mask == stored_mask) {
        while (hotarea_list) {
            hotarea_t *hotarea_data = (hotarea_t *)hotarea_list->data;

            if (hotarea_data->xul <= x && x <= hotarea_data->xlr &&
                hotarea_data->yul <= y && y <= hotarea_data->ylr) {

                handle_url(hotarea_data->url);
                break;
            }
            hotarea_list = g_list_next(hotarea_list);
        }
    }
    return FALSE;
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
    gchar **l = NULL, **lines = NULL, *line = NULL;
    gint quote_level = 0;

    /* one-time compilation of a constant url_str expression */
    if (!url_reg) {
	url_reg = g_malloc(sizeof(regex_t));
	if (regcomp(url_reg, url_str, REG_EXTENDED|REG_ICASE) != 0)
	    g_warning
		("part_info_init_mimetext: url regex compilation failed.");
    }

    /* proper code */
    if (!libbalsa_message_body_save_temporary(info->body, NULL)) {
	balsa_information
	    (LIBBALSA_INFORMATION_ERROR, NULL,
	     _("Error writing to temporary file %s.\n"
	       "Check the directory permissions."),
	     info->body->temp_filename);
	return;
    }

    if ((fp = fopen(info->body->temp_filename, "r")) == NULL) {
	balsa_information(LIBBALSA_INFORMATION_ERROR, NULL,
			  _("Cannot open temporary file %s."),
			  info->body->temp_filename);
	return;
    }
    
    alloced = libbalsa_readfile(fp, &ptr);
    if (!ptr) return;

    content_type = libbalsa_message_body_get_content_type(info->body);
    ishtml = (g_strcasecmp(content_type, "text/html") == 0);
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
    } else { /* not html */
	regex_t rex;
	GtkWidget *item = NULL;
	GdkFont *fnt = NULL;
	GList *url_list = NULL;
	
	fnt = find_body_font(info->body);

	if (bm->wrap_text) {
	    if (balsa_app.recognize_rfc2646_format_flowed
		&& libbalsa_flowed_rfc2646(info->body)) {
		ptr =
		    libbalsa_wrap_rfc2646(ptr, balsa_app.browse_wrap_length,
					  FALSE, TRUE);
	    } else
		libbalsa_wrap_string(ptr, balsa_app.browse_wrap_length);
	}

	if (!fnt)
	    fnt = gdk_fontset_load(balsa_app.message_font);
	
	item = gtk_text_new(NULL, NULL);
	
	/* get the widget's default font for those people who did not set up a 
	   custom one */
	if (!fnt)
	    fnt = item->style->font;

	gtk_signal_connect(GTK_OBJECT(item), "key_press_event",
			   (GtkSignalFunc)balsa_message_key_press_event,
			   (gpointer) bm);
	gtk_signal_connect(GTK_OBJECT(item), "focus_in_event",
			   (GtkSignalFunc)balsa_message_focus_in_part,
			   (gpointer) bm);
	gtk_signal_connect(GTK_OBJECT(item), "focus_out_event",
			   (GtkSignalFunc)balsa_message_focus_out_part,
			   (gpointer) bm);
	gtk_signal_connect(GTK_OBJECT(item), "size_request",
			   (GtkSignalFunc)balsa_gtk_text_size_request,
			   (gpointer) bm);
	
	allocate_quote_colors(GTK_WIDGET(bm), balsa_app.quoted_color,
			      0, MAX_QUOTED_COLOR - 1);
	/* Grab colour from the Theme.
	   style = gtk_widget_get_style (GTK_WIDGET (bm));
	   color = (GdkColor) style->text[GTK_STATE_PRELIGHT];
	*/
	if (regcomp(&rex, balsa_app.quote_regex, REG_EXTENDED) != 0) {
	    g_warning
		("part_info_init_mimetext: quote regex compilation failed.");
	    gtk_text_insert(GTK_TEXT(item), fnt, NULL, NULL, ptr, -1);
	} else {
	    gint ypos = 0;

	    lines = l = g_strsplit(ptr, "\n", -1);
	    for (line = *lines; line != NULL; line = *(++lines), ypos++) {
		line = g_strconcat(line, "\n", NULL);
		quote_level = is_a_quote(line, &rex);
		if (quote_level != 0) {
		    /* Modulus the quote level by the max,
		     * ie, always have "1 <= quote level <= MAX"
		     * this allows cycling through the possible
		     * quote colors over again as the quote level
		     * grows arbitrarily deep. */
		    quote_level = (quote_level-1)%MAX_QUOTED_COLOR;
		    gtk_text_insert_with_url(GTK_TEXT(item), fnt,
					     &balsa_app.quoted_color[quote_level],
					     line, url_reg, &url_list, ypos);
		} else
		    gtk_text_insert_with_url(GTK_TEXT(item), fnt, NULL, 
					     line, url_reg, &url_list, ypos);
		g_free(line);
	    }
	    g_strfreev(l);
	    regfree(&rex);
	}
	
	gtk_signal_connect_after(GTK_OBJECT(item), "realize",
				 (GtkSignalFunc)fix_text_widget, url_list);
	if (url_list) {
	    gtk_object_set_data(GTK_OBJECT(item), "url-list", 
				(gpointer) url_list);
	    gtk_object_set_data(GTK_OBJECT(item), "hotarea-list", 
				(gpointer) NULL);	
            gtk_signal_connect_after(GTK_OBJECT(item), "button_press_event",
                                     (GtkSignalFunc)store_button_coords, 
                                     NULL);
	    gtk_signal_connect_after(GTK_OBJECT(item), "button_release_event",
				     (GtkSignalFunc)check_call_url, NULL);
	    gtk_signal_connect(GTK_OBJECT(item), "motion-notify-event",
			       (GtkSignalFunc)check_over_url, NULL);
 	    gtk_signal_connect_after(GTK_OBJECT(item), "draw",
 				     (GtkSignalFunc)mail_text_draw, fnt);
	}

	gtk_text_set_editable(GTK_TEXT(item), FALSE);
	
	gtk_widget_show(item);
	info->focus_widget = item;
	info->widget = item;
	info->can_display = TRUE;
    }
    g_free(ptr);
    
    fclose(fp);
}

#ifdef HAVE_GTKHTML
static void
part_info_init_html(BalsaMessage * bm, BalsaPartInfo * info, gchar * ptr,
		    size_t len)
{
    GtkHTMLStream *stream;
    GtkWidget *html;

    html = gtk_html_new();
    gtk_signal_connect(GTK_OBJECT(html), "url_requested",
 		     (GtkSignalFunc)balsa_gtk_html_url_requested, bm->message);

    stream = gtk_html_begin(GTK_HTML(html));
    gtk_html_write(GTK_HTML(html), stream, ptr, len);
    gtk_html_end(GTK_HTML(html), stream, GTK_HTML_STREAM_OK);
    gtk_html_set_editable(GTK_HTML(html), FALSE);
    
    gtk_signal_connect(GTK_OBJECT(html), "size_request",
		       (GtkSignalFunc) balsa_gtk_html_size_request,
		       (gpointer) bm);
    gtk_signal_connect(GTK_OBJECT(html), "link_clicked",
		       GTK_SIGNAL_FUNC(balsa_gtk_html_link_clicked),
		       bm);
    gtk_signal_connect(GTK_OBJECT(html), "on_url",
		       GTK_SIGNAL_FUNC(balsa_gtk_html_on_url),
		       bm);
    gtk_signal_connect(GTK_OBJECT(html), "key_press_event",
		       (GtkSignalFunc)balsa_message_key_press_event,
		       (gpointer) bm);
    gtk_signal_connect(GTK_OBJECT(html), "focus_in_event",
		       (GtkSignalFunc)balsa_message_focus_in_part,
		       (gpointer) bm);
    gtk_signal_connect(GTK_OBJECT(html), "focus_out_event",
		       (GtkSignalFunc)balsa_message_focus_out_part,
		       (gpointer) bm);
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
    if (info->widget)
	gtk_object_ref(GTK_OBJECT(info->widget));

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
       g_strcasecmp(content_type, "multipart/mixed")==0 ||
       g_strcasecmp(content_type, "multipart/alternative")==0) {
    bm->part_count++;
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

#ifdef HAVE_GNOME_VFS
static void add_vfs_menu_item(BalsaPartInfo *info, 
			      const GnomeVFSMimeApplication *app)
{
    gchar *menu_label = g_strdup_printf(_("Open with %s"), app->name);
    GtkWidget *menu_item = gtk_menu_item_new_with_label (menu_label);
    
    gtk_object_set_data (GTK_OBJECT (menu_item), "mime_action", 
			 g_strdup(app->id));
    gtk_signal_connect (GTK_OBJECT (menu_item), "activate",
			GTK_SIGNAL_FUNC (part_context_menu_vfs_cb),
			(gpointer) info);
    gtk_menu_append (GTK_MENU (info->popup_menu), menu_item);
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
#endif


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
#ifdef HAVE_GNOME_VFS
    GnomeVFSMimeApplication *def_app, *app;
#endif
    
    info->popup_menu = gtk_menu_new ();
    
    content_type = libbalsa_message_body_get_content_type (info->body);
    key_list = list = gnome_mime_get_keys (content_type);
#ifdef HAVE_GNOME_VFS
    gdk_threads_leave(); /*FIXME: this hangs balsa */
    app_list = gnome_vfs_mime_get_short_list_applications(content_type);
    gdk_threads_enter(); /* FIXME: this hangs balsa */

    if((def_app=gnome_vfs_mime_get_default_application(content_type))) {
	add_vfs_menu_item(info, def_app);
    }
#endif
    

    while (list) {
        key = list->data;

        if (key && g_strcasecmp (key, "icon-filename") 
	    && g_strncasecmp (key, "fm-", 3)
	    /* Get rid of additional GnomeVFS entries: */
	    && (!strstr(key, "_") || strstr(key, "."))
	    && g_strncasecmp(key, "description", 11)) {
	    
            if ((cmd = gnome_mime_get_value (content_type, key)) != NULL
#ifdef HAVE_GNOME_VFS
                && !in_gnome_vfs(def_app, app_list, cmd)
#endif
               ) {
                if (g_strcasecmp (key, "open") == 0 || 
                    g_strcasecmp (key, "view") == 0 || 
                    g_strcasecmp (key, "edit") == 0 ||
                    g_strcasecmp (key, "ascii-view") == 0) {
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
                gtk_object_set_data (GTK_OBJECT (menu_item), "mime_action", 
                                     key);
                gtk_signal_connect (GTK_OBJECT (menu_item), "activate",
                                    GTK_SIGNAL_FUNC (part_context_menu_cb),
                                    (gpointer) info);
                gtk_menu_append (GTK_MENU (info->popup_menu), menu_item);
                g_free (menu_label);
            }
        }
        list = g_list_next (list);
    }

#ifdef HAVE_GNOME_VFS
    list=app_list;

    while (list) {
	app=list->data;

	if(app && (!def_app || strcmp(app->name, def_app->name)!=0)) {
	    add_vfs_menu_item(info, app);
	}

        list = g_list_next (list);
    }
    gnome_vfs_mime_application_free(def_app);
#endif
    

    menu_item = gtk_menu_item_new_with_label (_("Save..."));
    gtk_signal_connect (GTK_OBJECT (menu_item), "activate",
                        GTK_SIGNAL_FUNC (part_context_menu_save),
                        (gpointer) info);
    gtk_menu_append (GTK_MENU (info->popup_menu), menu_item);

    gtk_widget_show_all (info->popup_menu);

    g_list_free (key_list);
#ifdef HAVE_GNOME_VFS
    gnome_vfs_mime_application_list_free (app_list);
#endif
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
	    gtk_object_get_data(GTK_OBJECT(info->widget), "url-list");
 	free_url_list(widget_list);
	widget_list = 
	    gtk_object_get_data(GTK_OBJECT(info->widget), "hotarea-list");
 	free_hotarea_list(widget_list);
	gtk_object_unref(GTK_OBJECT(info->widget));
    }
    if (info->popup_menu)
	gtk_object_unref(GTK_OBJECT(info->popup_menu));

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
    gchar *url = gtk_object_get_data (GTK_OBJECT (menu_item), "call_url");
    
    g_return_if_fail(url);
    gnome_url_show(url);
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
    gtk_object_destroy(GTK_OBJECT(message));    
}


static void
part_context_menu_cb(GtkWidget * menu_item, BalsaPartInfo * info)
{
    gchar *content_type, *fpos;
    const gchar *cmd;
    gchar* key;


    content_type = libbalsa_message_body_get_content_type(info->body);
    key = gtk_object_get_data (GTK_OBJECT (menu_item), "mime_action");

    if (key != NULL && (cmd = gnome_mime_get_value(content_type, key)) != NULL) {
	if (!libbalsa_message_body_save_temporary(info->body, NULL)) {
	    balsa_information(LIBBALSA_INFORMATION_WARNING, NULL,
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


#ifdef HAVE_GNOME_VFS
static void
part_context_menu_vfs_cb(GtkWidget * menu_item, BalsaPartInfo * info)
{
    gchar *id;
    
    if(id = gtk_object_get_data (GTK_OBJECT (menu_item), "mime_action")) {
	GnomeVFSMimeApplication *app=
	    gnome_vfs_mime_application_new_from_id(id);
	if(app) {
	    if (libbalsa_message_body_save_temporary(info->body, NULL)) {
		gchar *exe_str=
		    g_strdup_printf("%s %s%s", app->command,
				    (app->expects_uris==GNOME_VFS_MIME_APPLICATION_ARGUMENT_TYPE_URIS?"file:":""),
				    info->body->temp_filename);
		
		gnome_execute_shell(NULL, exe_str);
		fprintf(stderr, "Executed: %s\n", exe_str);
		g_free (exe_str);
	    } else {
		balsa_information(LIBBALSA_INFORMATION_WARNING, NULL,
				  _("could not create temporary file %s"),
				  info->body->temp_filename);
	    }
	    gnome_vfs_mime_application_free(app);    
	} else {
	    fprintf(stderr, "lookup for application %s returned NULL\n", id);
	}
    }
}
#endif



void
balsa_message_next_part(BalsaMessage * bmessage)
{
    GnomeIconList *gil;
    gint index = 0;

    g_return_if_fail(bmessage != NULL);
    g_return_if_fail(bmessage->part_list != NULL);

    gil = GNOME_ICON_LIST(bmessage->part_list);
    if (gil->icons == 0 || gil->icons == 1)
	return;

    if (gil->selection) {
	index = (gint) (gil->selection->data);
	if (++index >= gil->icons)
	    index = 0;
    }

    gnome_icon_list_select_icon(gil, index);
    select_part(bmessage, index);
}

void
balsa_message_previous_part(BalsaMessage * bmessage)
{
    GnomeIconList *gil;
    gint index = 0;

    g_return_if_fail(bmessage != NULL);
    g_return_if_fail(bmessage->part_list != NULL);

    gil = GNOME_ICON_LIST(bmessage->part_list);
    if (gil->icons == 0 || gil->icons == 1)
	return;

    if (gil->selection) {
	index = (gint) (gil->selection->data);

	if (--index < 0)
	    index = gil->icons - 1;
    }

    gnome_icon_list_select_icon(gil, index);
    select_part(bmessage, index);
}

static LibBalsaMessageBody*
preferred_part(LibBalsaMessageBody *parts)
{
    /* TODO: Consult preferences and/or previous selections */

    LibBalsaMessageBody *body, *html_body = NULL;
    gchar *content_type;

#ifdef HAVE_GTKHTML
    for(body=parts; body; body=body->next) {
	content_type = libbalsa_message_body_get_content_type(body);

	if(g_strcasecmp(content_type, "text/html")==0) {
	    if (balsa_app.display_alt_plain)
		html_body = body;
	    else {
		g_free(content_type);
		return body;
	    }
	}
	g_free(content_type);
    }
#endif /* HAVE_GTKHTML */

    for(body=parts; body; body=body->next) {
	content_type = libbalsa_message_body_get_content_type(body);

	if(g_strcasecmp(content_type, "text/plain")==0) {
	    g_free(content_type);
	    return body;
	}
	g_free(content_type);
    }


    if (html_body)
	return html_body;
    else
	return parts;
}



static gint part_icon_no(BalsaMessage *bm, const LibBalsaMessageBody *body)
{
    const BalsaPartInfo *info;
    int part;

    for(part=0; part<bm->part_count; part++) {
	info = (const BalsaPartInfo *) gnome_icon_list_get_icon_data
	    (GNOME_ICON_LIST(bm->part_list), part);
	if(info->body==body)
	    return part;
    }
    return -1;
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
	if(g_strcasecmp(content_type, "multipart/related")==0) {
	    /* Add the first part */
	    add_body(bm, parent->parts);
	} else if(g_strcasecmp(content_type, "multipart/alternative")==0) {
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



static BalsaPartInfo *add_part(BalsaMessage *bm, gint part)
{
    BalsaPartInfo *info=NULL;

    if (part != -1) {
	LibBalsaMessageBodyType type;
	
	info = (BalsaPartInfo *) gnome_icon_list_get_icon_data
	    (GNOME_ICON_LIST(bm->part_list), part);

	g_assert(info != NULL);

	gnome_icon_list_select_icon(GNOME_ICON_LIST(bm->part_list), part);

	if (info->widget == NULL)
	    part_info_init(bm, info);

	if (info->widget) {
	    gtk_container_add(GTK_CONTAINER(bm->content), info->widget);
	    gtk_widget_show(info->widget);
	}
	add_multipart(bm, info->body);
    }
    
    return info;
}

static void hide_all_parts(BalsaMessage *bm)
{
    if(bm->current_part) {
	gint part;

	for(part=0; part<bm->part_count; part++) {
	    BalsaPartInfo *current_part=(BalsaPartInfo *) gnome_icon_list_get_icon_data
		(GNOME_ICON_LIST(bm->part_list), part);
	    
	    if(current_part && current_part->widget && 
	       GTK_WIDGET_VISIBLE(current_part->widget)) {
		if(GTK_IS_EDITABLE(current_part->widget) && 
		   GTK_WIDGET_REALIZED(current_part->widget))
		    gtk_editable_claim_selection(
						 GTK_EDITABLE(current_part->widget), FALSE, 
						 GDK_CURRENT_TIME);
		gtk_widget_hide(current_part->widget);
		gtk_container_remove(GTK_CONTAINER(bm->content),
				     current_part->widget);
	}
	    gnome_icon_list_unselect_icon(GNOME_ICON_LIST(bm->part_list), part);
    }
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

    bm->current_part = add_part(bm, part);

    if(bm->current_part)
	gtk_signal_emit(GTK_OBJECT(bm),
			balsa_message_signals[SELECT_PART]);

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

    gtk_signal_emit_by_name(GTK_OBJECT(adj), "value_changed");
}

static void
scroll_change(GtkAdjustment * adj, gint diff)
{
    gfloat upper;

    adj->value += diff;

    upper = adj->upper - adj->page_size;
    adj->value = MIN(adj->value, upper);
    adj->value = MAX(adj->value, 0.0);

    gtk_signal_emit_by_name(GTK_OBJECT(adj), "value_changed");
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

static void
balsa_gtk_text_size_request(GtkWidget * widget,
			    GtkRequisition * requisition, gpointer data)
{
    GtkText *text;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(GTK_IS_TEXT(widget));
    g_return_if_fail(requisition != NULL);

    text = GTK_TEXT(widget);

    requisition->width = (widget->style->klass->xthickness + 1) * 2;
    requisition->height = (widget->style->klass->ythickness + 1) * 2;

    requisition->width += text->hadj->upper;
    requisition->height += text->vadj->upper;

}

#ifdef HAVE_GTKHTML
/* balsa_gtk_html_size_request:
   report the requested size of the HTML widget.

   FIXME: this is not 100% right. The code includes an empirical
   (hehe) term -1 (marked with EMP) which is NOT the right way to
   go. The right solution requires some study of size_request signal
   handling code.  
*/
static void
balsa_gtk_html_size_request(GtkWidget * widget,
			    GtkRequisition * requisition, gpointer data)
{
    g_return_if_fail(widget != NULL);
    g_return_if_fail(GTK_IS_HTML(widget));
    g_return_if_fail(requisition != NULL);

    requisition->width  = GTK_LAYOUT(widget)->hadjustment->upper;
    requisition->height = GTK_LAYOUT(widget)->vadjustment->upper;
}

static gboolean
balsa_gtk_html_url_requested(GtkWidget *html, const gchar *url,
			     GtkHTMLStream* stream, LibBalsaMessage* msg)
{
    FILE* f;
    int i;
    char buf[4096];

    if(strncmp(url,"cid:",4)) {
	printf("non-local URL request ignored: %s\n", url);
	return FALSE;
    }
    if( (f=libbalsa_message_get_part_by_id(msg,url+4)) == NULL)
	return FALSE;

    while ((i = fread (buf, 1, sizeof(buf), f)) != 0)
	gtk_html_stream_write (stream, buf, i);
    gtk_html_stream_close(stream, GTK_HTML_STREAM_OK);
    fclose (f);
    
    return TRUE;
}

static void
balsa_gtk_html_link_clicked(GtkWidget *html, const gchar *url)
{
    gnome_url_show(url);
}
#endif
static void
balsa_gtk_html_on_url(GtkWidget *html, const gchar *url)
{
    static gboolean url_pushed = FALSE;

    if( url ) {
	if (url_pushed) {
	    gnome_appbar_set_status(balsa_app.appbar, url);
	} else {
	    gnome_appbar_push(balsa_app.appbar, url);
	    url_pushed = TRUE;
	}
    } else {
	if (url_pushed) {
	    gnome_appbar_pop(balsa_app.appbar);
	    url_pushed = FALSE;
	} else {
	    gnome_appbar_set_status(balsa_app.appbar, "");
	}
    }
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
	(GTK_CONTAINER(widget)->border_width +
	 widget->style->klass->xthickness + 1) * 2;
    requisition->height =
	(GTK_CONTAINER(widget)->border_width +
	 widget->style->klass->ythickness + 1) * 2;

    /* requisition->width = gil->hadj->upper; */
    requisition->height += gil->adj->upper;

}

/*
 * This function informs the caller if the currently selected part 
 * supports selection/copying etc. Currently only the GtkEditable derived 
 * widgets are supported for this (GtkHTML could be, but I don't have a 
 * working build right now)
 */
gboolean balsa_message_can_select(BalsaMessage * bmessage)
{
    g_return_val_if_fail(bmessage != NULL, FALSE);

    if (bmessage->current_part == NULL)
	return FALSE;
    if (bmessage->current_part->focus_widget == NULL)
	return FALSE;

    if (GTK_IS_EDITABLE(bmessage->current_part->focus_widget))
	return TRUE;
    else
	return FALSE;
}

void
balsa_message_copy_clipboard(BalsaMessage * bmessage)
{
    g_return_if_fail(bmessage != NULL);
    g_return_if_fail(bmessage->current_part != NULL);
    g_return_if_fail(bmessage->current_part->focus_widget != NULL);

    if (!GTK_IS_EDITABLE(bmessage->current_part->focus_widget))
	return;

    gtk_editable_copy_clipboard(GTK_EDITABLE
				(bmessage->current_part->focus_widget));
}

void
balsa_message_select_all(BalsaMessage * bmessage)
{
    g_return_if_fail(bmessage != NULL);
    g_return_if_fail(bmessage->current_part != NULL);
    g_return_if_fail(bmessage->current_part->focus_widget != NULL);

    if (!GTK_IS_EDITABLE(bmessage->current_part->focus_widget))
	return;

    gtk_editable_select_region(GTK_EDITABLE
			       (bmessage->current_part->focus_widget), 0,
			       -1);
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
	g_strcasecmp (a_atptr, b_atptr)) {
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
	gtk_object_destroy(GTK_OBJECT(mdn));
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

static GtkWidget* create_mdn_dialog (gchar *sender, gchar *mdn_to_address, 
				     LibBalsaMessage *send_msg)
{
  GtkWidget *mdn_dialog;
  GtkWidget *dialog_vbox;
  GtkWidget *hbox;
  GtkWidget *pixmap;
  GtkWidget *label;
  GtkWidget *dialog_action_area;
  GtkWidget *button_no;
  GtkWidget *button_yes;
  gchar *l_text;

  mdn_dialog = gnome_dialog_new (_("reply to MDN?"), NULL);
  gtk_object_set_user_data (GTK_OBJECT (mdn_dialog), send_msg);

  dialog_vbox = GNOME_DIALOG (mdn_dialog)->vbox;

  hbox = gtk_hbox_new (FALSE, 10);
  gtk_box_pack_start (GTK_BOX (dialog_vbox), hbox, TRUE, TRUE, 0);

  pixmap = gnome_pixmap_new_from_file (GNOME_DATA_PREFIX "/pixmaps/gnome-question.png");
  gtk_box_pack_start (GTK_BOX (hbox), pixmap, TRUE, TRUE, 0);

  l_text = g_strdup_printf(
      _("The sender of this mail, %s, requested \n"
	"a Message Disposition Notification (MDN) to be returned to `%s'.\n"
	"Do you want to send this notification?"),
      sender, mdn_to_address);
  label = gtk_label_new (l_text);
  g_free (l_text);
  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);

  dialog_action_area = GNOME_DIALOG (mdn_dialog)->action_area;
  gtk_button_box_set_layout (GTK_BUTTON_BOX (dialog_action_area), 
			     GTK_BUTTONBOX_END);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (dialog_action_area), 8);

  gnome_dialog_append_button (GNOME_DIALOG (mdn_dialog), 
			      GNOME_STOCK_BUTTON_NO);
  button_no = g_list_last (GNOME_DIALOG (mdn_dialog)->buttons)->data;
  GTK_WIDGET_SET_FLAGS (button_no, GTK_CAN_DEFAULT);
  
  gnome_dialog_append_button (GNOME_DIALOG (mdn_dialog), 
			      GNOME_STOCK_BUTTON_YES);
  button_yes = g_list_last (GNOME_DIALOG (mdn_dialog)->buttons)->data;
  GTK_WIDGET_SET_FLAGS (button_yes, GTK_CAN_DEFAULT);

  gtk_signal_connect (GTK_OBJECT (mdn_dialog), "delete_event",
                      GTK_SIGNAL_FUNC (mdn_dialog_delete), NULL);
  gtk_signal_connect (GTK_OBJECT (button_no), "clicked",
                      GTK_SIGNAL_FUNC (no_mdn_reply), mdn_dialog);
  gtk_signal_connect (GTK_OBJECT (button_yes), "clicked",
                      GTK_SIGNAL_FUNC (send_mdn_reply), mdn_dialog);

  gtk_widget_grab_focus (button_no);
  gtk_widget_grab_default (button_no);
  return mdn_dialog;
}

static void mdn_dialog_delete (GtkWidget *dialog, GdkEvent *event, 
			       gpointer user_data)
{
    LibBalsaMessage *send_msg;

    send_msg = 
	LIBBALSA_MESSAGE(gtk_object_get_user_data (GTK_OBJECT (dialog)));
    gtk_object_destroy(GTK_OBJECT(send_msg));
    gtk_widget_hide (dialog);
    gtk_object_destroy(GTK_OBJECT(dialog));
}

static void no_mdn_reply (GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog = GTK_WIDGET (user_data);
    LibBalsaMessage *send_msg;

    send_msg = 
	LIBBALSA_MESSAGE(gtk_object_get_user_data (GTK_OBJECT (dialog)));
    gtk_object_destroy(GTK_OBJECT(send_msg));
    gtk_widget_hide (dialog);
    gtk_object_destroy(GTK_OBJECT(dialog));
}


static void send_mdn_reply (GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog = GTK_WIDGET (user_data);
    LibBalsaMessage *send_msg;

    send_msg = 
	LIBBALSA_MESSAGE(gtk_object_get_user_data (GTK_OBJECT (dialog)));
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
    gtk_object_destroy(GTK_OBJECT(send_msg));
    gtk_widget_hide (dialog);
    gtk_object_destroy(GTK_OBJECT(dialog));
}
