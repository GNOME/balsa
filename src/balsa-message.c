/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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
#include "mailbackend.h"
#include "balsa-message.h"
#include "mime.h"
#include "misc.h"

#ifdef USE_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

#ifdef HAVE_GTKHTML
#include <gtkhtml/gtkhtml.h>
#endif

#include "quote-color.h"

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

enum {
    SELECT_PART,
    LAST_SIGNAL,
};

struct _BalsaPartInfo {
    LibBalsaMessage *message;
    LibBalsaMessageBody *body;

    /* The widget to add to the container */
    GtkWidget *widget;

    /* The widget to give focus to */
    GtkWidget *focus_widget;

    /* The contect menu */
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
static void select_part(BalsaMessage * bm, gint part);
static void free_icon_data(gpointer data);
static void part_context_menu_save(GtkWidget * menu_item,
				   BalsaPartInfo * info);
/* static void part_context_menu_view(GtkWidget * menu_item, */
/* 				   BalsaPartInfo * info); */

static void add_header_gchar(BalsaMessage * bm, gchar * header,
			     gchar * label, gchar * value);
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
#endif
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
static GtkWidget* part_info_mime_button (BalsaPartInfo* info, const gchar* content_type, const gchar* key);
static void part_context_menu_cb(GtkWidget * menu_item, BalsaPartInfo * info);
static void part_create_menu (BalsaPartInfo* info);

static GtkViewportClass *parent_class = NULL;

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
				       GTK_SELECTION_SINGLE);
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
    balsa_message_set(BALSA_MESSAGE(object), NULL);
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
save_part(BalsaPartInfo * info)
{
    gchar *filename;

    GtkWidget *save_dialog;
    GtkWidget *file_entry;
    gint button;

    g_return_if_fail(info != 0);

    save_dialog = gnome_dialog_new(_("Save MIME Part"),
				   _("Save"), _("Cancel"), NULL);
    file_entry = gnome_file_entry_new("Balsa_MIME_Saver",
				      _("Save MIME Part"));

    if (info->body->filename)
	gtk_entry_set_text(GTK_ENTRY
			   (gnome_file_entry_gtk_entry
			    (GNOME_FILE_ENTRY(file_entry))),
			   info->body->filename);

    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(save_dialog)->vbox),
		       gtk_label_new(_
				     ("Please choose a filename to save this part of the message as:")),
		       FALSE, FALSE, 10);

    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(save_dialog)->vbox),
		       file_entry, FALSE, FALSE, 10);

    gnome_dialog_set_parent(GNOME_DIALOG(save_dialog),
			    GTK_WINDOW(balsa_app.main_window));
    gtk_window_set_modal(GTK_WINDOW(save_dialog), TRUE);
    gtk_window_set_wmclass(GTK_WINDOW(save_dialog), "save", "Balsa");

    gtk_widget_show_all(save_dialog);
    gtk_widget_grab_focus(gnome_file_entry_gtk_entry
			  (GNOME_FILE_ENTRY(file_entry)));
    button = gnome_dialog_run(GNOME_DIALOG(save_dialog));

    /* button 0 == OK */
    if (button == 0) {

	gtk_widget_hide(GTK_WIDGET(save_dialog));

	filename =
	    gtk_entry_get_text(GTK_ENTRY
			       (gnome_file_entry_gtk_entry
				(GNOME_FILE_ENTRY(file_entry))));

	if (!libbalsa_message_body_save(info->body, NULL, filename)) {
	    gchar *msg;
	    GtkWidget *msgbox;

	    msg =
		g_strdup_printf(_(" Could not save %s:%s "), filename,
				strerror(errno));

	    msgbox =
		gnome_error_dialog_parented(msg,
					    GTK_WINDOW
					    (balsa_app.main_window));

	    g_free(msg);

	    gnome_dialog_run_and_close(GNOME_DIALOG(msgbox));

	}
    }

    gtk_object_destroy(GTK_OBJECT(save_dialog));
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

void
balsa_message_set(BalsaMessage * bm, LibBalsaMessage * message)
{
    gboolean had_focus;

    g_return_if_fail(bm != NULL);

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
	return;
    }

    /* mark message as read; no-op if it was read so don't worry.
       and this is the right place to do the marking.
     */
    libbalsa_message_read(message);

    bm->message = message;

    gtk_signal_connect(GTK_OBJECT(message), "destroy",
		       GTK_SIGNAL_FUNC(message_destroyed_cb),
		       (gpointer) bm);

    display_headers(bm);

    libbalsa_message_body_ref(bm->message);

    display_content(bm);

    /*
     * FIXME: This is a workaround for what may or may not be a libmutt bug.
     *
     * If the Content-Type: header is  multipart/alternative; boundary="XXX" 
     * and no parts are found then mutt produces a message with no parts, even 
     * if there is a single unmarked part (ie a normal email).
     */
    if (bm->part_count == 0) {
	gtk_widget_hide(bm->part_list);

	/* This is really annoying if you are browsing, since you keep getting a dialog... */
	/* balsa_information(LIBBALSA_INFORMATION_WARNING, _("Message contains no parts!")); */
	return;
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
add_header_gchar(BalsaMessage * bm, gchar * header, gchar * label,
		 gchar * value)
{
    /*    GtkWidget *w; */
    GdkFont *fnt;
    gchar pad[] = "                ";
    gchar cr[] = "\n";
    gchar *line_start, *line_end;
    gchar *wrapped_value;

    if (!(bm->shown_headers == HEADERS_ALL || libbalsa_find_word(header, balsa_app.selected_headers))) 
	return;

    if (strcmp(header, "subject") != 0)
	fnt = gdk_font_load(balsa_app.message_font);
    else
	fnt = gdk_font_load(balsa_app.subject_font);

    gtk_text_insert(GTK_TEXT(bm->header_text), fnt, NULL, NULL, label, -1);

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

    add_header_gchar(bm, "subject", _("Subject:"), message->subject);

    date = libbalsa_message_date_to_gchar(message, balsa_app.date_string);
    add_header_gchar(bm, "date", _("Date:"), date);
    g_free(date);

    if (message->from) {
	gchar *from = libbalsa_address_to_gchar(message->from);
	add_header_gchar(bm, "from", _("From:"), from);
	g_free(from);
    }

    add_header_glist(bm, "to", _("To:"), message->to_list);
    add_header_glist(bm, "cc", _("Cc:"), message->cc_list);
    add_header_glist(bm, "bcc", _("Bcc:"), message->bcc_list);

    if (message->fcc_mailbox)
	add_header_gchar(bm, "fcc", _("Fcc:"), message->fcc_mailbox);

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
#ifndef USE_PIXBUF
    GdkImlibImage *im;
#else
    GdkPixbuf *pb;
#endif

    GdkPixmap *pixmap;
    GdkBitmap *mask;

    GtkWidget *image;

    libbalsa_message_body_save_temporary(info->body, NULL);

#ifndef USE_PIXBUF
    if (!(im = gdk_imlib_load_image(info->body->temp_filename))) {
	g_print(_("Could not load image. It is most likely corrupted."));
	return;
    }

    if (!gdk_imlib_render(im, im->rgb_width, im->rgb_height)) {
	g_print(_("Couldn't render image\n"));
    }

    pixmap = gdk_imlib_copy_image(im);
    mask = gdk_imlib_copy_mask(im);

    gdk_imlib_destroy_image(im);

#else
    pb = gdk_pixbuf_new_from_file(info->body->temp_filename);
    gdk_pixbuf_render_pixmap_and_mask(pb, &pixmap, &mask, 0);
    gdk_pixbuf_unref(pb);
#endif

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

static void
part_info_init_message(BalsaMessage * bm, BalsaPartInfo * info)
{
    g_print("TODO: part_info_init_message\n");
    part_info_init_unknown(bm, info);
}

static void
part_info_init_unknown(BalsaMessage * bm, BalsaPartInfo * info)
{
    GtkWidget *vbox;
    GtkWidget *button;
    gchar *msg;
    const gchar *cmd;
    gchar *content_type;
    

    vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    content_type = libbalsa_message_body_get_content_type(info->body);
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
    g_return_val_if_fail(charset >= 0, NULL);

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
     * extra space for dwo dashes and '\0' */
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
    else {
	strncpy(res, "*", 1);
	len = 1;
    }

    res[len] = '-';
    strcpy(res + len + 1, postfix);
    return res;
}

/* balsa_get_font_by_charset:
   returns font or fontset as specified by general base and charset.
   Follows code from gfontsel.
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

    if (xfs && (xfs->min_byte1 != 0 || xfs->max_byte1 != 0))
    {
	gchar *tmp_name;
	g_print("balsa_get_font_by_charset: using fontset\n");
	gdk_font_unref (result);
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
    gint ln;

    *l = str + pos;

    while (*l > str && !(**l == '\n' && *(*l - 1) == '\n'))
	(*l)--;
    if (*l + 1 <= str + pos && **l == '\n')
	(*l)++;

    *u = str + pos;
    ln = 0;
    while (**u && !(ln && **u == '\n'))
	ln = *(*u)++ == '\n';
    if (ln)
	(*u)--;
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


/* END OF HELPER FUNCTIONS ----------------------------------------------- */

static void
part_info_init_mimetext(BalsaMessage * bm, BalsaPartInfo * info)
{
    FILE *fp;

    gchar *ptr = 0;
    size_t alloced;
    gchar **l = NULL;
    gchar **lines = NULL;
    gchar *line = NULL;
    /*
       GdkColor color[MAX_QUOTED_COLOR];
       GdkColormap * colormap;
       GtkStyle * style = NULL;
     */
    gint quote_level = 0;

    if (!libbalsa_message_body_save_temporary(info->body, NULL)) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  "Error writing to temporary file %s.\nCheck the directory permissions.",
			  info->body->temp_filename);
	return;
    }

    if ((fp = fopen(info->body->temp_filename, "r")) == NULL) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  "Cannot open temporary file %s.",
			  info->body->temp_filename);
	return;
    }

    alloced = libbalsa_readfile(fp, &ptr);

    if (ptr) {
	gboolean ishtml;
	gchar *content_type;

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
	} else {
	    regex_t rex;

	    GtkWidget *item = NULL;
	    GdkFont *fnt = NULL;

	    if (regcomp(&rex, balsa_app.quote_regex, REG_EXTENDED) != 0)
		g_warning
		    ("part_info_init_mimetext: quote regex compilation failed.");
	    fnt = find_body_font(info->body);
	    if (bm->wrap_text)
		libbalsa_wrap_string(ptr, balsa_app.wraplength);

	    if (!fnt)
		fnt = gdk_fontset_load(balsa_app.message_font);

	    item = gtk_text_new(NULL, NULL);

	    gtk_signal_connect(GTK_OBJECT(item), "key_press_event",
			       (GtkSignalFunc)
			       balsa_message_key_press_event,
			       (gpointer) bm);
	    gtk_signal_connect(GTK_OBJECT(item), "focus_in_event",
			       (GtkSignalFunc) balsa_message_focus_in_part,
			       (gpointer) bm);
	    gtk_signal_connect(GTK_OBJECT(item), "focus_out_event",
			       (GtkSignalFunc)
			       balsa_message_focus_out_part,
			       (gpointer) bm);
	    gtk_signal_connect(GTK_OBJECT(item), "size_request",
			       (GtkSignalFunc) balsa_gtk_text_size_request,
			       (gpointer) bm);

	    allocate_quote_colors(GTK_WIDGET(bm), balsa_app.quoted_color,
				  0, MAX_QUOTED_COLOR - 1);
	    /* Grab colour from the Theme.
	       style = gtk_widget_get_style (GTK_WIDGET (bm));
	       color = (GdkColor) style->text[GTK_STATE_PRELIGHT];
	     */

	    lines = l = g_strsplit(ptr, "\n", -1);
	    for (line = *lines; line != NULL; line = *(++lines)) {
		line = g_strconcat(line, "\n", NULL);
		quote_level = is_a_quote(line, &rex);
		if (quote_level > MAX_QUOTED_COLOR)
		    quote_level = MAX_QUOTED_COLOR;
		if (quote_level != 0)
		    gtk_text_insert(GTK_TEXT(item), fnt,
				    &balsa_app.quoted_color[quote_level -
							    1], NULL, line,
				    -1);
		else
		    gtk_text_insert(GTK_TEXT(item), fnt, NULL, NULL, line,
				    -1);
		g_free(line);
	    }
	    g_strfreev(l);
	    regfree(&rex);

	    gtk_text_set_editable(GTK_TEXT(item), FALSE);

	    gtk_widget_show(item);
	    info->focus_widget = item;
	    info->widget = item;
	    info->can_display = TRUE;
	}
	g_free(ptr);

	fclose(fp);

    }

}

#ifdef HAVE_GTKHTML
static void
part_info_init_html(BalsaMessage * bm, BalsaPartInfo * info, gchar * ptr,
		    size_t len)
{
    GtkHTMLStream *stream;
    GtkWidget *html, *scroll;

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_NEVER, GTK_POLICY_NEVER);

    html = gtk_html_new();

    stream = gtk_html_begin(GTK_HTML(html));
    gtk_html_write(GTK_HTML(html), stream, ptr, len);
    gtk_html_end(GTK_HTML(html), stream, GTK_HTML_STREAM_OK);
    gtk_html_set_editable(GTK_HTML(html), FALSE);

    gtk_signal_connect(GTK_OBJECT(html), "size_request",
		       (GtkSignalFunc) balsa_gtk_html_size_request,
		       (gpointer) bm);

    gtk_container_add(GTK_CONTAINER(scroll), html);

    gtk_widget_show(html);
    gtk_widget_show(scroll);

    info->focus_widget = html;
    info->widget = scroll;
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
	g_print("Got a TYPEMULTIPART in part2canvas!\n");
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

    /* The widget is unref'd in free_icon_data */
    if (info->widget)
	gtk_object_ref(GTK_OBJECT(info->widget));

    return;
}

static void
display_part(BalsaMessage * bm, LibBalsaMessageBody * body)
{
    BalsaPartInfo *info = NULL;
    gchar *pix = NULL;
    gchar *content_type = NULL;
    gchar *icon_title = NULL;
    gint pos;


    if (libbalsa_message_body_is_multipart(body)) {
	if (balsa_app.debug)
	    fprintf(stderr, "part: multipart\n");
	display_multipart(bm, body);
	if (balsa_app.debug)
	    fprintf(stderr, "part end: multipart\n");
	return;			/* we don't want a multipart icon */
    }

    bm->part_count++;

    info = (BalsaPartInfo *) g_new0(BalsaPartInfo, 1);
    info->body = body;
    info->message = bm->message;
    info->can_display = FALSE;

    content_type = libbalsa_message_body_get_content_type(body);

    if (body->filename)
	icon_title =
	    g_strdup_printf("%s (%s)", body->filename, content_type);
    else
	icon_title = g_strdup_printf("(%s)", content_type);

    pix = libbalsa_icon_finder(content_type, body->filename);

    part_create_menu (info);
    pos = gnome_icon_list_append(GNOME_ICON_LIST(bm->part_list),
				 pix, icon_title);

    gnome_icon_list_set_icon_data_full(GNOME_ICON_LIST(bm->part_list), pos,
				       info, free_icon_data);

    g_free(content_type);
    g_free(icon_title);
    g_free(pix);
}

static void
display_content(BalsaMessage * bm)
{
    LibBalsaMessageBody *body;

    body = bm->message->body_list;
    while (body) {
	display_part(bm, body);
	body = body->next;
    }
}


static void
part_create_menu (BalsaPartInfo* info) 
{
    GtkWidget* menu_item;
    GList* list;
    GList* key_list;
    gchar* content_type;
    gchar* key;
    const gchar* cmd;
    gchar* menu_label;
    gchar** split_key;
    gint i;
    
    info->popup_menu = gtk_menu_new ();
    
    content_type = libbalsa_message_body_get_content_type (info->body);
    key_list = list = gnome_mime_get_keys (content_type);
    
    while (list) {
        key = list->data;

        if (key && g_strcasecmp (key, "icon-filename") 
	    && g_strncasecmp (key, "fm-", 3)) {
            if ((cmd = gnome_mime_get_value (content_type, key)) != NULL) {
                if (g_strcasecmp (key, "open") == 0 || 
                    g_strcasecmp (key, "view") == 0 || 
                    g_strcasecmp (key, "edit") == 0 ||
                    g_strcasecmp (key, "ascii-view") == 0) {
                    
                    /* uppercase first letter, make label */
                    menu_label = g_strdup (key);
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

    menu_item = gtk_menu_item_new_with_label (_("Save..."));
    gtk_signal_connect (GTK_OBJECT (menu_item), "activate",
                        GTK_SIGNAL_FUNC (part_context_menu_save),
                        (gpointer) info);
    gtk_menu_append (GTK_MENU (info->popup_menu), menu_item);

    gtk_widget_show_all (info->popup_menu);

    g_list_free (key_list);
    g_free (content_type);
}


static void
free_icon_data(gpointer data)
{
    BalsaPartInfo *info = (BalsaPartInfo *) data;

    if (info == NULL)
	return;

    if (info->widget)
	gtk_object_unref(GTK_OBJECT(info->widget));

    if (info->popup_menu)
	gtk_object_unref(GTK_OBJECT(info->popup_menu));

    /*    if ( info->hadj )  */
    /*      gtk_object_unref(GTK_OBJECT(info->hadj)); */
    /*    if ( info->vadj ) */
    /*      gtk_object_unref(GTK_OBJECT(info->vadj)); */

    g_free(data);
}

static void
part_context_menu_save(GtkWidget * menu_item, BalsaPartInfo * info)
{
    save_part(info);
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
	    fprintf(stderr, "Executed\n");
            g_free (cps);
            g_free (exe_str);
	}
    } else
	fprintf(stderr, "view for %s returned NULL\n", content_type);

    g_free(content_type);
}


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

/* 
 * If part == -1 then change to no part
 * must release selection before hiding a text widget.
 */
static void
select_part(BalsaMessage * bm, gint part)
{
    BalsaPartInfo *info;

    if (bm->current_part && bm->current_part->widget) {
	if(GTK_IS_EDITABLE(bm->current_part->widget) && 
	   GTK_WIDGET_REALIZED(bm->current_part->widget))
	    gtk_editable_claim_selection(
		GTK_EDITABLE(bm->current_part->widget), FALSE, 
		GDK_CURRENT_TIME);
	gtk_widget_hide(bm->current_part->widget);
	gtk_container_remove(GTK_CONTAINER(bm->table),
			     bm->current_part->widget);
    }

    if (part != -1) {
	info = (BalsaPartInfo *) gnome_icon_list_get_icon_data
	    (GNOME_ICON_LIST(bm->part_list), part);

	g_assert(info != NULL);

	if (info->widget == NULL)
	    part_info_init(bm, info);

	if (info->widget) {
	    gtk_widget_show(info->widget);
	    gtk_table_attach(GTK_TABLE(bm->table), info->widget, 0, 1, 1,
			     2, GTK_EXPAND | GTK_FILL,
			     GTK_EXPAND | GTK_FILL, 0, 1);
	} else {
	    /* HACK! This is a spacer, so that the attachment icons
	     * will stay at the bottom of the window.
	     */
	    GtkWidget *box;

	    box = gtk_hbox_new(FALSE, FALSE);
	    gtk_widget_show(box);
	    gtk_table_attach(GTK_TABLE(bm->table), box, 0, 1, 1, 2,
			     GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL,
			     0, 0);
	}
	bm->current_part = info;
	gtk_signal_emit(GTK_OBJECT(bm),
			balsa_message_signals[SELECT_PART]);
    } else {
	bm->current_part = NULL;
    }


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

    viewport = GTK_VIEWPORT(bm);

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
		      -viewport->vadjustment->page_increment);
	break;
    case GDK_Page_Down:
	scroll_change(viewport->vadjustment,
		      viewport->vadjustment->page_increment);
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
static void
balsa_gtk_html_size_request(GtkWidget * widget,
			    GtkRequisition * requisition, gpointer data)
{
    g_return_if_fail(widget != NULL);
    g_return_if_fail(GTK_IS_HTML(widget));
    g_return_if_fail(requisition != NULL);

    requisition->width = (widget->style->klass->xthickness + 1) * 2;
    requisition->height = (widget->style->klass->ythickness + 1) * 2;

    requisition->width += GTK_LAYOUT(widget)->hadjustment->upper;
    requisition->height += GTK_LAYOUT(widget)->vadjustment->upper;

}
#endif

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
