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
#include "balsa-icons.h"
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

enum {
    PART_INFO_COLUMN = 0,
    MIME_ICON_COLUMN,
    MIME_TYPE_COLUMN,
    NUM_COLUMNS
};

typedef struct _BalsaPartInfoClass BalsaPartInfoClass;

struct _BalsaPartInfo {
    GObject parent_object;

    LibBalsaMessageBody *body;

    /* The widget to add to the container; referenced */
    GtkWidget *widget;

    /* The widget to give focus to; just an pointer */
    GtkWidget *focus_widget;

    /* The contect menu; referenced */
    GtkWidget *popup_menu;

    /* True if balsa knows how to display this part */
    gboolean can_display;

    /* the path in the tree view */
    GtkTreePath *path;
};

struct _BalsaPartInfoClass {
    GObjectClass parent_class;
};

static GType balsa_part_info_get_type();

#define TYPE_BALSA_PART_INFO          \
        (balsa_part_info_get_type ())
#define BALSA_PART_INFO(obj)          \
        (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_BALSA_PART_INFO, BalsaPartInfo))
#define IS_BALSA_PART_INFO(obj)       \
        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_BALSA_PART_INFO))

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
static void display_embedded_headers(BalsaMessage * bm,
				     LibBalsaMessageBody * body,
				     GtkWidget *emb_hdr_view);

static void display_part(BalsaMessage * bm, LibBalsaMessageBody * body,
			 GtkTreeIter * parent_iter);
static void display_multipart(BalsaMessage * bm,
			      LibBalsaMessageBody * body,
			      GtkTreeIter * parent_iter);

static void save_part(BalsaPartInfo * info);

static BalsaPartInfo *add_part(BalsaMessage *bm, BalsaPartInfo *info);
static void add_multipart(BalsaMessage *bm, LibBalsaMessageBody *parent);
static void select_part(BalsaMessage * bm, BalsaPartInfo *info);
static void part_context_menu_save(GtkWidget * menu_item,
				   BalsaPartInfo * info);
/* static void part_context_menu_view(GtkWidget * menu_item, */
/* 				   BalsaPartInfo * info); */
static void tree_activate_row_cb(GtkTreeView *treeview, GtkTreePath *arg1,
				 GtkTreeViewColumn *arg2, gpointer user_data);
static gboolean tree_menu_popup_key_cb(GtkWidget *widget, gpointer user_data);
static gboolean tree_button_press_cb(GtkWidget * widget, GdkEventButton * event,
				     gpointer data);

static void add_header_gchar(BalsaMessage * bm, GtkTextView * view,
			     const gchar *header, const gchar *label,
			     const gchar *value);
static void add_header_glist(BalsaMessage * bm, GtkTextView * view,
			     gchar * header, gchar * label, GList * list);

static void scroll_set(GtkAdjustment * adj, gint value);
static void scroll_change(GtkAdjustment * adj, gint diff);

#ifdef HAVE_GTKHTML
static void balsa_gtk_html_size_request(GtkWidget * widget,
					GtkRequisition * requisition,
					gpointer data);
static gboolean balsa_gtk_html_url_requested(GtkWidget *html, const gchar *url,
					     HtmlStream* stream,
					     LibBalsaMessage* msg);
static void balsa_gtk_html_link_clicked(GObject * obj, 
					const gchar *url);
#endif
static void balsa_gtk_html_on_url(GtkWidget *html, const gchar *url);

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
static void part_context_save_all_cb(GtkWidget * menu_item, GList * info_list);
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

static void balsa_part_info_init(GObject *object, gpointer data);
static BalsaPartInfo* balsa_part_info_new(LibBalsaMessageBody* body);
static void balsa_part_info_free(GObject * object);
static GtkTextTag * quote_tag(GtkTextBuffer * buffer, gint level);
static gboolean resize_idle(GtkWidget * widget);
static void prepare_url_offsets(GtkTextBuffer * buffer, GList * url_list);


static void
balsa_part_info_class_init(BalsaPartInfoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    
    object_class->finalize = balsa_part_info_free;
}

static GType
balsa_part_info_get_type()
{
    static GType balsa_part_info_type = 0 ;

    if (!balsa_part_info_type) {
	static const GTypeInfo balsa_part_info_info =
	    {
		sizeof (BalsaPartInfoClass),
		(GBaseInitFunc) NULL,
		(GBaseFinalizeFunc) NULL,
		(GClassInitFunc) balsa_part_info_class_init,
		(GClassFinalizeFunc) NULL,
		NULL,
		sizeof(BalsaPartInfo),
		0,
		(GInstanceInitFunc) balsa_part_info_init
	    };
        balsa_part_info_type = 
	   g_type_register_static (G_TYPE_OBJECT, "BalsaPartInfo",
				   &balsa_part_info_info, 0);
    }
    return balsa_part_info_type;
}

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
    GtkWidget *scroll;
    GtkWidget *label;
    GtkTreeStore *model;
    GtkCellRenderer *renderer;
    GtkTreeSelection *selection;

    /* Notebook to hold content + structure */
    bm->notebook = gtk_notebook_new();
    gtk_notebook_set_show_border(GTK_NOTEBOOK(bm->notebook), FALSE);
    gtk_widget_show(bm->notebook);
    gtk_container_add(GTK_CONTAINER(bm), bm->notebook);

    /* scrolled window for the contents */
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    label = gtk_label_new(_("Content"));
    gtk_widget_show(label);
    gtk_notebook_append_page(GTK_NOTEBOOK(bm->notebook), scroll, label);
    gtk_widget_show(scroll);

    /* The vbox widget */
    bm->vbox = gtk_vbox_new(FALSE, 1);
    gtk_widget_show(bm->vbox);
    bm->cont_viewport = gtk_viewport_new(NULL, NULL);
    gtk_widget_show(bm->cont_viewport);
    gtk_container_add(GTK_CONTAINER(bm->cont_viewport), bm->vbox);
    gtk_container_add(GTK_CONTAINER(scroll), bm->cont_viewport);

    /* Widget to hold headers */
    bm->header_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(bm->header_text), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(bm->header_text), 2);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(bm->header_text), 15);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(bm->header_text), GTK_WRAP_WORD);
    g_signal_connect(G_OBJECT(bm->header_text), "key_press_event",
		     G_CALLBACK(balsa_message_key_press_event),
		     (gpointer) bm);
    gtk_box_pack_start(GTK_BOX(bm->vbox), bm->header_text, FALSE, FALSE, 0);

    /* Widget to hold content */
    bm->content = gtk_vbox_new(FALSE, 1);
    gtk_box_pack_start(GTK_BOX(bm->vbox), bm->content, TRUE, TRUE, 0);
    gtk_widget_show(bm->content);

    /* structure view */
    model = gtk_tree_store_new (NUM_COLUMNS,
				TYPE_BALSA_PART_INFO,
				GDK_TYPE_PIXBUF,
				G_TYPE_STRING);
    bm->treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL(model));
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW (bm->treeview));
    g_signal_connect(bm->treeview, "row-collapsed",
		     G_CALLBACK(gtk_tree_view_expand_all), NULL);
    g_signal_connect(bm->treeview, "row-activated",
		     G_CALLBACK(tree_activate_row_cb), bm);    
    g_signal_connect(bm->treeview, "button_press_event",
		     G_CALLBACK(tree_button_press_cb), bm);
    g_signal_connect(bm->treeview, "popup-menu",
		     G_CALLBACK(tree_menu_popup_key_cb), bm);
    g_object_unref (G_OBJECT (model));
    gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (bm->treeview), TRUE);
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (bm->treeview), FALSE);

    /* column for type icon */
    renderer = gtk_cell_renderer_pixbuf_new ();
    g_object_set (G_OBJECT (renderer), "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (bm->treeview),
						 -1, NULL,
						 renderer, "pixbuf",
						 MIME_ICON_COLUMN,
						 NULL);
    
    /* column for mime type */
    renderer = gtk_cell_renderer_text_new ();
    g_object_set (G_OBJECT (renderer), "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (bm->treeview),
						 -1, NULL,
						 renderer, "text",
						 MIME_TYPE_COLUMN,
						 NULL);

    /* scrolled window for the tree view */
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    label = gtk_label_new(_("Message parts"));
    gtk_widget_show(label);
    gtk_notebook_append_page(GTK_NOTEBOOK(bm->notebook), scroll, label);
    gtk_widget_show(scroll);
    gtk_widget_show(bm->treeview);
    gtk_container_add(GTK_CONTAINER(scroll), bm->treeview);

    bm->current_part = NULL;
    bm->message = NULL;
    bm->info_count = 0;
    bm->save_all_list = NULL;
    bm->save_all_popup = NULL;

    bm->wrap_text = balsa_app.browse_wrap;
    bm->shown_headers = balsa_app.shown_headers;
    bm->show_all_headers = FALSE;
}

static void
balsa_message_destroy(GtkObject * object)
{
    BalsaMessage* bm = BALSA_MESSAGE(object);

    if (bm->treeview) {
        balsa_message_set(bm, NULL);
        gtk_widget_destroy(bm->treeview);
        bm->treeview = NULL;
    }
    g_list_free(bm->save_all_list);
    if (bm->save_all_popup)
	gtk_widget_destroy(bm->save_all_popup);

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
	result = libbalsa_message_body_save(info->body, NULL, filename);
	if (!result)
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("Could not save %s: %s"),
                              filename, strerror(errno));
    }
}

static void
save_part(BalsaPartInfo * info)
{
    gchar *cont_type, *title, *filename;
    GtkWidget *save_dialog;
    
    g_return_if_fail(info != 0);

    cont_type = libbalsa_message_body_get_content_type(info->body);
    title = g_strdup_printf(_("Save %s MIME Part"), cont_type);
    save_dialog = gtk_file_selection_new(title);
    g_free(title);
    g_free(cont_type);
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

static BalsaPartInfo *
tree_next_valid_part_info(GtkTreeModel * model, GtkTreeIter * iter)
{
    BalsaPartInfo *info = NULL;

    do {
	GtkTreeIter child;

	/* check if there is a valid info */
	gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);
	if (info) {
	    g_object_unref(G_OBJECT(info));
	    return info;
	}

	/* if there are children, check the childs */
	if (gtk_tree_model_iter_children (model, &child, iter))
	    if ((info = tree_next_valid_part_info(model, &child)))
		return info;

	/* switch to the next iter on the same level */
	if (!gtk_tree_model_iter_next(model, iter))
	    return NULL;
    } while (1);
    /* never reached */
    return NULL;
}

static void 
tree_activate_row_cb(GtkTreeView *treeview, GtkTreePath *arg1,
		     GtkTreeViewColumn *arg2, gpointer user_data)
{
    BalsaMessage * bm = (BalsaMessage *)user_data;
    GtkTreeModel * model = gtk_tree_view_get_model(treeview);
    GtkTreeIter sel_iter;
    BalsaPartInfo *info = NULL;

    g_return_if_fail(bm);
    
    /* get the info of the activated part */
    if (!gtk_tree_model_get_iter(model, &sel_iter, arg1))
	return;
    gtk_tree_model_get(model, &sel_iter, PART_INFO_COLUMN, &info, -1);
    if (info)
	g_object_unref(G_OBJECT(info));
    
    /* if it's not displayable (== no info), get the next one... */
    if (!info) {
	info = tree_next_valid_part_info(model, &sel_iter);
	
	if (!info) {
	    gtk_tree_model_get_iter_first(model, &sel_iter);
	    gtk_tree_model_get(model, &sel_iter, PART_INFO_COLUMN, &info, -1);
	    if (info)
		g_object_unref(G_OBJECT(info));
	    else
		info = tree_next_valid_part_info(model, &sel_iter);
	}
    }

    gtk_notebook_set_current_page(GTK_NOTEBOOK(bm->notebook), 0);
    select_part(bm, info);
}

static void
collect_selected_info(GtkTreeModel * model, GtkTreePath * path,
		      GtkTreeIter * iter, gpointer data)
{
    GList **info_list = (GList **)data;
    BalsaPartInfo *info;

    gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);
    if (info) {
	g_object_unref(info);
	*info_list = g_list_append(*info_list, info);
    }
}

static void
tree_mult_selection_popup(BalsaMessage * bm, GdkEventButton * event,
			  GtkTreeSelection * selection)
{
    gint selected;

    /* destroy left-over select list and popup... */
    g_list_free(bm->save_all_list);
    bm->save_all_list = NULL;
    if (bm->save_all_popup) {
	gtk_widget_destroy(bm->save_all_popup);
	bm->save_all_popup = NULL;
    }

    /* collect all selected info blocks */
    gtk_tree_selection_selected_foreach(selection,
					collect_selected_info,
					&bm->save_all_list);
    
    /* For a single part, display it's popup, for multiple parts a "save all"
     * popup. If nothing with an info block is selected, do nothing */
    selected = g_list_length(bm->save_all_list);
    if (selected == 1) {
	BalsaPartInfo *info = BALSA_PART_INFO(bm->save_all_list->data);
	if (info->popup_menu) {
	    if (event)
		gtk_menu_popup(GTK_MENU(info->popup_menu), NULL, NULL, NULL,
			       NULL, event->button, event->time);
	    else
		gtk_menu_popup(GTK_MENU(info->popup_menu), NULL, NULL, NULL,
			       NULL, 0, gtk_get_current_event_time());
	}
	g_list_free(bm->save_all_list);
	bm->save_all_list = NULL;
    } else if (selected > 1) {
	GtkWidget *menu_item;
	
	bm->save_all_popup = gtk_menu_new ();
	menu_item = 
	    gtk_menu_item_new_with_label (_("Save selected..."));
	gtk_widget_show(menu_item);
	g_signal_connect (G_OBJECT (menu_item), "activate",
			  GTK_SIGNAL_FUNC (part_context_save_all_cb),
			  (gpointer) bm->save_all_list);
	gtk_menu_shell_append (GTK_MENU_SHELL (bm->save_all_popup), menu_item);
	if (event)
	    gtk_menu_popup(GTK_MENU(bm->save_all_popup), NULL, NULL, NULL,
			   NULL, event->button, event->time);
	else
	    gtk_menu_popup(GTK_MENU(bm->save_all_popup), NULL, NULL, NULL,
			   NULL, 0, gtk_get_current_event_time());
    }
}

static gboolean
tree_menu_popup_key_cb(GtkWidget *widget, gpointer user_data)
{
    BalsaMessage * bm = (BalsaMessage *)user_data;

    g_return_val_if_fail(bm, FALSE);
    tree_mult_selection_popup(bm, NULL,
			      gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)));
    return TRUE;
}

static gboolean 
tree_button_press_cb(GtkWidget * widget, GdkEventButton * event,
		     gpointer data)
{
    BalsaMessage * bm = (BalsaMessage *)data;
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
    GtkTreePath *path;

    g_return_val_if_fail(bm, FALSE);
    g_return_val_if_fail(event, FALSE);
    if (event->type != GDK_BUTTON_PRESS || event->button != 3
        || event->window != gtk_tree_view_get_bin_window(tree_view))
        return FALSE;

    /* If the part which received the click is already selected, don't change
     * the selection and check if more than on part is selected. Pop up the
     * "save all" menu in this case and the "normal" popup otherwise.
     * If the receiving part is not selected, select (only) this part and pop
     * up its menu. 
     */
    if (gtk_tree_view_get_path_at_pos(tree_view, event->x, event->y,
                                      &path, NULL, NULL, NULL)) {
	GtkTreeIter iter;
        GtkTreeSelection * selection =
            gtk_tree_view_get_selection(tree_view);
	GtkTreeModel * model = gtk_tree_view_get_model(tree_view);

        if (!gtk_tree_selection_path_is_selected(selection, path)) {
	    BalsaPartInfo *info = NULL;

	    gtk_tree_selection_unselect_all(selection);
	    gtk_tree_selection_select_path(selection, path);
	    gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree_view), path, NULL,
				     FALSE);
	    if (gtk_tree_model_get_iter (model, &iter, path)) {
		gtk_tree_model_get(model, &iter, PART_INFO_COLUMN, &info, -1);
		if (info) {
		    if (info->popup_menu)
			gtk_menu_popup(GTK_MENU(info->popup_menu), NULL, NULL,
				       NULL, NULL, event->button, event->time);
		    g_object_unref(info);
		}
	    }
	} else
	    tree_mult_selection_popup(bm, event, selection);
	gtk_tree_path_free(path);
    }

    return TRUE;
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

/* Helpers:
 */
static gboolean
bm_content_has_focus(BalsaMessage * bm)
{
    GList *children, *list;
    gboolean has_focus = FALSE;

    children = gtk_container_get_children(GTK_CONTAINER(bm->content));
    for (list = children; list; list = g_list_next(list)) {
        GtkWidget *child = list->data;
        if (GTK_WIDGET_HAS_FOCUS(child)) {
            has_focus = TRUE;
            break;
        }
    }
    g_list_free(children);

    return has_focus;
}

static void
bm_focus_on_first_child(BalsaMessage * bm)
{
    GList *children, *list;

    children = gtk_container_get_children(GTK_CONTAINER(bm->content));
    for (list = children; list; list = g_list_next(list)) {
        GtkWidget *child = list->data;
        if (GTK_WIDGET_CAN_FOCUS(child)) {
            gtk_widget_grab_focus(child);
            break;
        }
    }
    g_list_free(children);
}

#ifdef HAVE_GPGME
static gint
balsa_message_scan_signatures(LibBalsaMessageBody *body, LibBalsaMessage * message)
{
    gint result = LIBBALSA_MESSAGE_SIGNATURE_UNKNOWN;
    gchar *sender;
    gchar *subject = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(message));

    g_return_val_if_fail(message->headers != NULL, result);

    sender = message->headers->from
        ? libbalsa_address_to_gchar(message->headers->from, -1)
        : g_strdup(_("(No sender)"));

    for (; body; body = body->next) {
	gint signres = libbalsa_is_pgp_signed(body);
	
	libbalsa_utf8_sanitize(&subject, balsa_app.convert_unknown_8bit, 
			       balsa_app.convert_unknown_8bit_codeset, NULL);

	if (signres > 0) {
	    LibBalsaSignatureInfo *checkResult;
	    if (!body->parts->next->sig_info)
		libbalsa_body_check_signature(body->parts);
	    checkResult = body->parts->next->sig_info;

	    if (checkResult) {
		if (checkResult->status == GPGME_SIG_STAT_GOOD) {
		    /* check if we trust this signature at least marginally */
		    if (checkResult->validity >= GPGME_VALIDITY_MARGINAL &&
			checkResult->trust >= GPGME_VALIDITY_MARGINAL) {
			if (result <= LIBBALSA_MESSAGE_SIGNATURE_GOOD)
			    result = LIBBALSA_MESSAGE_SIGNATURE_GOOD;
			libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
					     _("detected a good signature"));
		    } else {
			if (result <= LIBBALSA_MESSAGE_SIGNATURE_NOTRUST)
			    result = LIBBALSA_MESSAGE_SIGNATURE_NOTRUST;
			libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
					     _("detected a good signature with insufficient validity/trust"));
		    }
		} else {
		    result = LIBBALSA_MESSAGE_SIGNATURE_BAD;

#ifdef HAVE_GPG
		    if (checkResult->status == GPGME_SIG_STAT_NOKEY) {
			gchar *msg = 
			    g_strdup_printf(_("Checking the signature of the message sent by %s with subject \"%s\" returned:\n%s"),
					    sender, subject,
					    libbalsa_gpgme_sig_stat_to_gchar(checkResult->status));
			gpg_ask_import_key(msg, GTK_WINDOW(balsa_app.main_window), 
					   checkResult->fingerprint);
			g_free(msg);
		    } else
#endif
		    libbalsa_information(LIBBALSA_INFORMATION_WARNING,
					 _("Checking the signature of the message sent by %s with subject \"%s\" returned:\n%s"),
					 sender, subject,
					 libbalsa_gpgme_sig_stat_to_gchar(checkResult->status));
		}
	    } else {
		result = LIBBALSA_MESSAGE_SIGNATURE_BAD;
		libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				     _("Checking the signature of the message sent by %s with subject \"%s\" failed with an error!"),
				     sender, subject);
	    }
	} else if (signres < 0) {
	    libbalsa_information(LIBBALSA_INFORMATION_WARNING,
				 _("The message sent by %s with subject \"%s\" contains a \"multipart/signed\" part, but it's structure is invalid. The signature, if there is any, can not be checked."),
				 sender, subject);
	}	    

	/* scan embedded messages */
	if (body->parts) {
	    gint sub_result =
		balsa_message_scan_signatures(body->parts, message);
	    if (sub_result >= result)
		result = sub_result;
	}
    }

    g_free(subject);
    g_free(sender);

    return result;
}
#endif

static void
balsa_message_clear_tree(BalsaMessage * bm)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;

    g_return_if_fail(bm != NULL);

    selection = 
	gtk_tree_view_get_selection(GTK_TREE_VIEW(bm->treeview));
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(bm->treeview));
    gtk_tree_store_clear(GTK_TREE_STORE(model));
    bm->info_count = 0;
}

gboolean
balsa_message_set(BalsaMessage * bm, LibBalsaMessage * message)
{
    gboolean is_new;
    gboolean has_focus;
    GtkTreeIter iter;
    BalsaPartInfo *info;

    g_return_val_if_fail(bm != NULL, FALSE);

    /* Leave this out. When settings (eg wrap) are changed it is OK to 
       call message_set with the same messagr */
    /*    if (bm->message == message) */
    /*      return; */

    /* find out whether the content has the keyboard focus */
    has_focus = bm_content_has_focus(bm);

    select_part(bm, NULL);
    if (bm->message != NULL) {
        g_object_weak_unref(G_OBJECT(bm->message),
	   	            (GWeakNotify) bm_message_weak_ref_cb,
		            (gpointer) bm);
	libbalsa_message_body_unref(bm->message);
        bm->message = NULL;
    }
    balsa_message_clear_tree(bm);

    if (message == NULL) {
	gtk_widget_hide(bm->header_text);
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(bm->notebook), FALSE);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(bm->notebook), 0);
	return TRUE;
    }

    bm->message = message;

    g_object_weak_ref(G_OBJECT(message),
		      (GWeakNotify) bm_message_weak_ref_cb,
		      (gpointer) bm);

    is_new = LIBBALSA_MESSAGE_IS_UNREAD(message);
    if(!libbalsa_message_body_ref(bm->message, TRUE)) 
	return FALSE;

#ifdef HAVE_GPGME
    /* FIXME: not checking for body_ref == 1 leads to a crash if we have both
     * the encrypted and the unencrypted version open as the body chain of the
     * first one will be unref'd. */
    if (message->body_ref == 1) {
	gint encrres = 	libbalsa_is_pgp_encrypted(message->body_list);

	if (encrres > 0)
	    /* try to decrypt the message... */
	    message->body_list->parts =
		libbalsa_body_decrypt(message->body_list->parts, NULL);
	else if (encrres < 0) {
	    gchar *sender = message->headers && message->headers->from
                ? libbalsa_address_to_gchar(message->headers->from, -1)
                : g_strdup(_("(No sender)"));
	    gchar *subject = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(message));
	
	    libbalsa_utf8_sanitize(&subject, balsa_app.convert_unknown_8bit, 
				   balsa_app.convert_unknown_8bit_codeset, NULL);
	    
	    libbalsa_information(LIBBALSA_INFORMATION_WARNING,
				 _("The message sent by %s with subject \"%s\" contains a \"multipart/encrypted\" part, but it's structure is invalid."),
				 sender, subject);
	    g_free(subject);
	    g_free(sender);
	}
    }
    
    /* scan the message for signatures */
    message->sig_state = 
 	balsa_message_scan_signatures(message->body_list, message);
    if (message->sig_state != LIBBALSA_MESSAGE_SIGNATURE_UNKNOWN) {
 	GList *notify = NULL;
 	notify = g_list_append(notify, message);
 	/* send the message the signal to update the signature status icon.
 	   The flag value must not match anything in src/balsa-index.c,
 	   mailbox_messages_changed_status().  */
 	g_signal_emit_by_name(G_OBJECT(message->mailbox), 
 			      "messages-status-changed", notify, 42);
    }
#endif

    display_headers(bm);
    display_content(bm);

    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(bm->notebook), bm->info_count > 1);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(bm->notebook), 0);

    /*
     * At this point we check if (a) a message was new (its not new
     * any longer) and (b) a Disposition-Notification-To header line is
     * present.
     *
     */
    if (is_new && message->headers->dispnotify_to)
	handle_mdn_request (message);

    /*
     * FIXME: This is a workaround for what may or may not be a libmutt bug.
     *
     * If the Content-Type: header is  multipart/alternative; boundary="XXX" 
     * and no parts are found then mutt produces a message with no parts, even 
     * if there is a single unmarked part (ie a normal email).     */
    if (!gtk_tree_model_get_iter_first (gtk_tree_view_get_model(GTK_TREE_VIEW(bm->treeview)),
					&iter)) {
	/* This is really annoying if you are browsing, since you keep
           getting a dialog... */
	/* balsa_information(LIBBALSA_INFORMATION_WARNING, _("Message
           contains no parts!")); */
	return TRUE;
    }
    
    info = 
	tree_next_valid_part_info(gtk_tree_view_get_model(GTK_TREE_VIEW(bm->treeview)),
				  &iter);
    select_part(bm, info);

    /* restore keyboard focus to the content, if it was there before */
    if (has_focus)
        bm_focus_on_first_child(bm);

    return TRUE;
}

void
balsa_message_save_current_part(BalsaMessage * bm)
{
    g_return_if_fail(bm != NULL);

    if (bm->current_part)
	save_part(bm->current_part);
}

static gboolean
balsa_message_set_embedded_hdr(GtkTreeModel * model, GtkTreePath * path,
			       GtkTreeIter *iter, gpointer data)
{
    BalsaPartInfo *info = NULL;
    BalsaMessage * bm = BALSA_MESSAGE(data);

    gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);
    if (info) {
	if (info->body && info->body->embhdrs)
	    display_embedded_headers(bm, info->body, info->widget);
	g_object_unref(G_OBJECT(info));
    }
    
    return FALSE;
}

void
balsa_message_set_displayed_headers(BalsaMessage * bmessage,
				    ShownHeaders sh)
{
    g_return_if_fail(bmessage != NULL);
    g_return_if_fail(sh >= HEADERS_NONE && sh <= HEADERS_ALL);

    bmessage->shown_headers = sh;

    if (bmessage->message) {
	display_headers(bmessage);
	gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(bmessage->treeview)),
			       balsa_message_set_embedded_hdr, bmessage);
    }
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

/* Indents in pixels: */
#define BALSA_ONE_CHAR     7
#define BALSA_INDENT_CHARS 15
#define BALSA_TAB1         (BALSA_ONE_CHAR * BALSA_INDENT_CHARS)
#define BALSA_TAB2         (BALSA_TAB1 + BALSA_ONE_CHAR)

static void
add_header_gchar(BalsaMessage * bm, GtkTextView *view, const gchar * header,
                 const gchar * label, const gchar * value)
{
    PangoTabArray *tab;
    GtkTextBuffer *buffer;
    GtkTextTag *font_tag;
    GtkTextIter insert;

    if (!(bm->show_all_headers || bm->shown_headers == HEADERS_ALL ||
          libbalsa_find_word(header, balsa_app.selected_headers)))
        return;

    tab = pango_tab_array_new_with_positions(2, TRUE,
                                             PANGO_TAB_LEFT, BALSA_TAB1,
                                             PANGO_TAB_LEFT, BALSA_TAB2);
    gtk_text_view_set_tabs(view, tab);
    pango_tab_array_free(tab);

    /* always display the label in the predefined font */
    buffer = gtk_text_view_get_buffer(view);
    font_tag = NULL;
    if (strcmp(header, "subject") == 0)
        font_tag =
            gtk_text_buffer_create_tag(buffer, NULL,
                                       "font", balsa_app.subject_font,
                                       NULL);

    gtk_text_buffer_get_iter_at_mark(buffer, &insert,
                                     gtk_text_buffer_get_insert(buffer));
    if (gtk_text_buffer_get_char_count(buffer))
        gtk_text_buffer_insert(buffer, &insert, "\n", 1);
    gtk_text_buffer_insert_with_tags(buffer, &insert,
                                     label, -1, font_tag, NULL);

    if (value && *value != '\0') {
        GtkTextTagTable *table;
        GtkTextTag *indent_tag;
        gchar *wrapped_value;

        table = gtk_text_buffer_get_tag_table(buffer);
        indent_tag = gtk_text_tag_table_lookup(table, "indent");
        if (!indent_tag)
            indent_tag =
                gtk_text_buffer_create_tag(buffer, "indent",
                                           "indent", BALSA_TAB1, NULL);

        gtk_text_buffer_insert(buffer, &insert, "\t", 1);
        wrapped_value = g_strdup(value);
        libbalsa_wrap_string(wrapped_value,
                             balsa_app.wraplength - BALSA_INDENT_CHARS);
        libbalsa_utf8_sanitize(&wrapped_value, balsa_app.convert_unknown_8bit, balsa_app.convert_unknown_8bit_codeset, NULL);
        gtk_text_buffer_insert_with_tags(buffer, &insert,
                                         wrapped_value, -1,
                                         indent_tag, font_tag, NULL);
        g_free(wrapped_value);
    }
}

static void
add_header_glist(BalsaMessage * bm, GtkTextView * view, gchar * header,
		 gchar * label, GList * list)
{
    gchar *value;

    if (list == NULL)
	return;

    if (!(bm->show_all_headers || bm->shown_headers == HEADERS_ALL || 
	  libbalsa_find_word(header, balsa_app.selected_headers))) 
	return;

    value = libbalsa_make_string_from_list(list);

    add_header_gchar(bm, view, header, label, value);

    g_free(value);
}

#ifdef HAVE_GPGME
static void
add_header_sigstate(BalsaMessage * bm, GtkTextView *view,
		    LibBalsaSignatureInfo *siginfo)
{
    GtkTextBuffer *buffer;
    GtkTextIter insert;
    GtkTextTag *color_tag;
    GdkColor sigStateCol;
    
    buffer = gtk_text_view_get_buffer(view);
    gtk_text_buffer_get_iter_at_mark(buffer, &insert,
                                     gtk_text_buffer_get_insert(buffer));
    if (gtk_text_buffer_get_char_count(buffer))
        gtk_text_buffer_insert(buffer, &insert, "\n", 1);
    /* FIXME: do we want to have these colors selectable by the user? */
    if (siginfo->status == GPGME_SIG_STAT_GOOD) {
	sigStateCol.red = 0x0;
	sigStateCol.green = 0x8000;
	sigStateCol.blue = 0x0;
    } else {
	sigStateCol.red = 0xf000;
	sigStateCol.green = 0x0;
	sigStateCol.blue = 0x0;
    }
    color_tag = gtk_text_buffer_create_tag(buffer, NULL,
					   "foreground-gdk", 
					   &sigStateCol,
					   NULL);
    gtk_text_buffer_insert_with_tags(buffer, &insert,
                                     libbalsa_gpgme_sig_stat_to_gchar(siginfo->status),
				     -1, color_tag, NULL);
}
#endif

static void
display_headers_real(BalsaMessage * bm, LibBalsaMessageHeaders * headers,
		     LibBalsaMessageBody * sig_body, const gchar * subject,
		     GtkTextView * view)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
    GList *p;
    gchar *date;

    gtk_text_buffer_set_text(buffer, "", 0);
    g_return_if_fail(headers);
 
    if (!bm->show_all_headers && bm->shown_headers == HEADERS_NONE) {
	gtk_widget_hide(GTK_WIDGET(view));
	return;
    } else {
	gtk_widget_show(GTK_WIDGET(view));
    }

    add_header_gchar(bm, view, "subject", _("Subject:"), subject);

    date = libbalsa_message_headers_date_to_gchar(headers, balsa_app.date_string);
    add_header_gchar(bm, view, "date", _("Date:"), date);
    g_free(date);

    if (headers->from) {
	gchar *from = libbalsa_address_to_gchar(headers->from, 0);
	add_header_gchar(bm, view, "from", _("From:"), from);
	g_free(from);
    }

    if (headers->reply_to) {
	gchar *reply_to = libbalsa_address_to_gchar(headers->reply_to, 0);
	add_header_gchar(bm, view, "reply-to", _("Reply-To:"), reply_to);
	g_free(reply_to);
    }
    add_header_glist(bm, view, "to", _("To:"), headers->to_list);
    add_header_glist(bm, view, "cc", _("Cc:"), headers->cc_list);
    add_header_glist(bm, view, "bcc", _("Bcc:"), headers->bcc_list);

    if (headers->fcc_url)
	add_header_gchar(bm, view, "fcc", _("Fcc:"), headers->fcc_url);

    if (headers->dispnotify_to) {
	gchar *mdn_to = libbalsa_address_to_gchar(headers->dispnotify_to, 0);
	add_header_gchar(bm, view, "disposition-notification-to", 
			 _("Disposition-Notification-To:"), mdn_to);
	g_free(mdn_to);
    }

    /* remaining headers */
    for (p = g_list_first(headers->user_hdrs); p; p = g_list_next(p)) {
	gchar **pair = p->data;
	gchar *hdr;

	hdr = g_strconcat(pair[0], ":", NULL);
	add_header_gchar(bm, view, pair[0], hdr, pair[1]);
	g_free(hdr);
    }

#ifdef HAVE_GPGME
    if (sig_body && libbalsa_is_pgp_signed(sig_body) > 0) {
	if (sig_body->parts->next->sig_info)
	    add_header_sigstate(bm, view, sig_body->parts->next->sig_info);
    }
#endif

    gtk_widget_queue_resize(GTK_WIDGET(view));
}

static void
display_headers(BalsaMessage * bm)
{
    display_headers_real(bm, bm->message->headers, bm->message->body_list,
			 LIBBALSA_MESSAGE_GET_SUBJECT(bm->message),
			 GTK_TEXT_VIEW(bm->header_text));
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

#ifdef HAVE_GPGME
static void
part_info_init_pgp_signature(BalsaMessage * bm, BalsaPartInfo * info)
{
    gchar *infostr;
    GtkWidget *hbox;

    if (!info->body->sig_info) {
	part_info_init_unknown(bm, info);
	return;
    }

    infostr =
	libbalsa_signature_info_to_gchar(info->body->sig_info,
					 balsa_app.date_string);
    
    hbox = gtk_hbox_new(FALSE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 10);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(infostr), FALSE, FALSE, 0);
    g_free(infostr);
    gtk_widget_show_all(hbox);
    info->widget = hbox;
    info->focus_widget = hbox;
    info->can_display = TRUE;
}
#endif

static void
part_info_init_application(BalsaMessage * bm, BalsaPartInfo * info)
{
#ifdef HAVE_GPGME
    gchar *body_type = libbalsa_message_body_get_content_type(info->body);

    if (!g_ascii_strcasecmp("application/pgp-signature", body_type)) {
	part_info_init_pgp_signature(bm, info);
	g_free(body_type);
	return;
    }
    g_free(body_type);
#endif
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
display_embedded_headers(BalsaMessage * bm, LibBalsaMessageBody *body,
			 GtkWidget *emb_hdr_view)
{
    display_headers_real(bm, body->embhdrs, body->parts, body->embhdrs->subject,
			 GTK_TEXT_VIEW(emb_hdr_view));
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
    } else if (!g_ascii_strcasecmp("message/rfc822", body_type)) {
	GtkWidget *emb_hdrs = gtk_text_view_new();
	
	gtk_text_view_set_editable(GTK_TEXT_VIEW(emb_hdrs), FALSE);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(emb_hdrs), 2);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(emb_hdrs), 2);
	gtk_widget_modify_font(emb_hdrs,
			       pango_font_description_from_string
			       (balsa_app.message_font));
	display_embedded_headers(bm, info->body, emb_hdrs);
	
	info->focus_widget = emb_hdrs;
	info->widget = emb_hdrs;
	info->can_display = FALSE;
    } else {
	g_print("TODO: part_info_init_message\n");
	part_info_init_mimetext(bm, info);
    }
    g_free(body_type);
}

static void
part_info_init_unknown(BalsaMessage * bm, BalsaPartInfo * info)
{
    GtkWidget *vbox;
    GtkWidget *hbox;
    GtkWidget *button = NULL;
    gchar *msg;
    const gchar *content_desc;
    gchar *content_type;


    vbox = gtk_vbox_new(FALSE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    if (info->body->filename) {
        msg = g_strdup_printf(_("File name: %s"), info->body->filename);
        gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new(msg), FALSE, FALSE,
                           0);
        g_free(msg);
    }

    content_type = libbalsa_message_body_get_content_type(info->body);
    if ((content_desc = gnome_vfs_mime_get_description(content_type)))
        msg = g_strdup_printf(_("Type: %s (%s)"), content_desc,
                              content_type);
    else
        msg = g_strdup_printf(_("Content Type: %s"), content_type);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new(msg), FALSE, FALSE, 0);
    g_free(msg);

    hbox = gtk_hbox_new(TRUE, 6);
    if ((button = part_info_mime_button_vfs(info, content_type))
        || (button = part_info_mime_button(info, content_type, "view"))
        || (button = part_info_mime_button(info, content_type, "open")))
        gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
    else
        gtk_box_pack_start(GTK_BOX(vbox),
                           gtk_label_new(_("No open or view action "
                                           "defined in GNOME MIME "
                                           "for this content type")),
                           FALSE, FALSE, 0);
    g_free(content_type);

    button = gtk_button_new_with_label(_("Save part"));
    gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
                     G_CALLBACK(part_context_menu_save), (gpointer) info);

    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 2);
    gtk_widget_show_all(vbox);

    info->focus_widget = vbox;
    info->widget = vbox;
    info->can_display = FALSE;
}


static GtkWidget *
part_info_mime_button(BalsaPartInfo * info, const gchar * content_type,
                      const gchar * key)
{
    GtkWidget *button = NULL;
    gchar *msg;
    const gchar *cmd =
        gnome_vfs_mime_get_value(content_type, (char *) key);

    if (cmd) {
        msg = g_strdup_printf(_("View part with %s"), cmd);
        button = gtk_button_new_with_label(msg);
        g_object_set_data(G_OBJECT(button), "mime_action", (gpointer) key);
        g_free(msg);

        g_signal_connect(G_OBJECT(button), "clicked",
                         G_CALLBACK(part_context_menu_cb),
                         (gpointer) info);
    }

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
display_multipart(BalsaMessage * bm, LibBalsaMessageBody * body,
		  GtkTreeIter *parent_iter)
{
    LibBalsaMessageBody *part;

    for (part = body->parts; part; part = part->next) {
	display_part(bm, part, parent_iter);
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
    GtkTextMark *end_mark;
    gint start, end;             /* pos in the buffer */
    gchar *url;                  /* the link */
} message_url_t;

static void handle_url(const message_url_t* url);
static void pointer_over_url(GtkWidget * widget, message_url_t * url,
                             gboolean set);
static message_url_t *find_url(GtkWidget * widget, gint x, gint y,
                               GList * url_list);

/* the cursors which are displayed over URL's and normal message text */
static GdkCursor *url_cursor_normal = NULL;
static GdkCursor *url_cursor_over_url = NULL;

static void
free_url_list(GList * url_list)
{
    GList *list;

    for (list = url_list; list; list = g_list_next(list)) {
        message_url_t *url_data = (message_url_t *) list->data;

        g_free(url_data->url);
        g_free(url_data);
    }
    g_list_free(url_list);
}

static void
url_found_cb(GtkTextBuffer * buffer, GtkTextIter * iter,
             const gchar * buf, gpointer data)
{
    GList **url_list = data;
    message_url_t *url_found;

    url_found = g_new(message_url_t, 1);
    url_found->end_mark =
        gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
    url_found->url = g_strdup(buf);       /* gets freed later... */
    *url_list = g_list_append(*url_list, url_found);
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
        gdk_window_set_events(w,
                              gdk_window_get_events(w) |
                              GDK_POINTER_MOTION_MASK |
                              GDK_LEAVE_NOTIFY_MASK);
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
    if (event->type == GDK_LEAVE_NOTIFY)
        url = NULL;
    else {
        /* FIXME: why can't we just use
         * x = event->x;
         * y = event->y;
         * ??? */
        gdk_window_get_pointer(window, &x, &y, &mask);
        url = find_url(widget, x, y, url_list);
    }

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
    gdk_threads_enter();
    gnome_appbar_refresh(balsa_app.appbar);
    gdk_threads_leave();
    return FALSE;
}
#define SCHEDULE_BAR_REFRESH()	g_timeout_add(5000, status_bar_refresh, NULL);

static void
handle_url(const message_url_t* url)
{
    if (!g_ascii_strncasecmp(url->url, "mailto:", 7)) {
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

static gint resize_idle_id;

static void
part_info_init_mimetext(BalsaMessage * bm, BalsaPartInfo * info)
{
    FILE *fp;
    gboolean ishtml;
    gchar *content_type;
    gchar *ptr = NULL;
    size_t alloced;

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
	const gchar *target_cs;
#ifdef HAVE_GPGME
	LibBalsaMessageBodyRFC2440Mode rfc2440mode;
#endif

        if (!libbalsa_utf8_sanitize(&ptr, balsa_app.convert_unknown_8bit,
				    balsa_app.convert_unknown_8bit_codeset, &target_cs)) {
	    gchar *from = bm->message->headers && bm->message->headers->from
                ? libbalsa_address_to_gchar(bm->message->headers->from, 0)
                : g_strdup(_("(No sender)"));
	    gchar *subject = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(bm->message));
	
	    libbalsa_utf8_sanitize(&subject, balsa_app.convert_unknown_8bit, 
				   balsa_app.convert_unknown_8bit_codeset, NULL);
	    libbalsa_information(LIBBALSA_INFORMATION_WARNING,
				 _("The message sent by %s with subject \"%s\" contains 8-bit characters, but no header describing the used codeset (converted to %s)"),
				 from, subject,
				 target_cs ? target_cs : "\"?\"");
	    g_free(subject);
	    g_free(from);
	}

#ifdef HAVE_GPGME
	/* check if this is a RFC2440 part */
	rfc2440mode = libbalsa_rfc2440_check_buffer(ptr);
	if (rfc2440mode != LIBBALSA_BODY_RFC2440_NONE) {
	    gchar *charset = 
		libbalsa_message_body_get_parameter(info->body, "charset");
	    GpgmeSigStat sig_res;
	    GdkPixbuf * content_icon;
	    
	    /* do the rfc2440 stuff */
	    if (rfc2440mode == LIBBALSA_BODY_RFC2440_SIGNED)
		sig_res = 
		    libbalsa_rfc2440_check_signature(&ptr, charset, 
						     TRUE, &info->body->sig_info,
						     balsa_app.date_string);
	    else
		sig_res = 
		    libbalsa_rfc2440_decrypt_buffer(&ptr, charset, 
						    balsa_app.convert_unknown_8bit,
						    balsa_app.convert_unknown_8bit_codeset,
						    TRUE, &info->body->sig_info,
						    balsa_app.date_string, NULL);

	    if (sig_res == GPGME_SIG_STAT_GOOD) {
		if (info->body->sig_info->validity >= GPGME_VALIDITY_MARGINAL &&
		    info->body->sig_info->trust >= GPGME_VALIDITY_MARGINAL) {
		    content_icon =
			gtk_widget_render_icon(GTK_WIDGET(balsa_app.main_window),
					       BALSA_PIXMAP_INFO_SIGN_GOOD,
					       GTK_ICON_SIZE_MENU, NULL);
			libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
					     _("detected a good signature"));
		} else {
		    content_icon =
			gtk_widget_render_icon(GTK_WIDGET(balsa_app.main_window),
					       BALSA_PIXMAP_INFO_SIGN_NOTRUST,
					       GTK_ICON_SIZE_MENU, NULL);
			libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
					     _("detected a good signature with insufficient validity/trust"));
		}
	    } else if (sig_res != GPGME_SIG_STAT_NONE) {
		gchar *sender = bm->message->headers && bm->message->headers->from 
                    ? libbalsa_address_to_gchar(bm->message->headers->from, -1)
                    : g_strdup(_("(No sender)"));
		gchar *subject = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(bm->message));
	
		libbalsa_utf8_sanitize(&subject, balsa_app.convert_unknown_8bit, 
				       balsa_app.convert_unknown_8bit_codeset, NULL);
		
#ifdef HAVE_GPG
		if (sig_res == GPGME_SIG_STAT_NOKEY) {
		    gchar *msg = 
			g_strdup_printf(_("Checking the signature of the message sent by %s with subject \"%s\" returned:\n%s"),
					sender, subject,
					libbalsa_gpgme_sig_stat_to_gchar(sig_res));
		    gpg_ask_import_key(msg, GTK_WINDOW(balsa_app.main_window), 
				       info->body->sig_info->fingerprint);
		    g_free(msg);
		} else
#endif
		libbalsa_information(LIBBALSA_INFORMATION_WARNING,
				     _("Checking the signature of the message sent by %s with subject \"%s\" returned:\n%s"),
				     sender, subject,
				     libbalsa_gpgme_sig_stat_to_gchar(sig_res));
		g_free(subject);
		g_free(sender);
		content_icon =
		    gtk_widget_render_icon(GTK_WIDGET(balsa_app.main_window),
					   BALSA_PIXMAP_INFO_SIGN_BAD,
					   GTK_ICON_SIZE_MENU, NULL);
	    } else if (rfc2440mode == LIBBALSA_BODY_RFC2440_ENCRYPTED)
		content_icon =
		    gtk_widget_render_icon(GTK_WIDGET(balsa_app.main_window),
					   BALSA_PIXMAP_INFO_ENCR,
					   GTK_ICON_SIZE_MENU, NULL);
	    else
		content_icon = NULL;

	    if (content_icon) {
		GtkTreeModel * model;
		GtkTreeIter iter;

		model = 
		    gtk_tree_view_get_model(GTK_TREE_VIEW(bm->treeview));
		if (gtk_tree_model_get_iter (model, &iter, info->path))
		    gtk_tree_store_set (GTK_TREE_STORE(model), &iter, 
					MIME_ICON_COLUMN, content_icon, -1);
		g_object_unref(content_icon);
	    }

	    /* overwrite the tmp buffer */
	    fp = freopen(info->body->temp_filename, "w+", fp);
	    fwrite(ptr, strlen(ptr), 1, fp);
	    fflush(fp);
	    g_free(charset);
	}
#endif

        if (libbalsa_message_body_is_flowed(info->body)) {
            /* Parse, but don't wrap. */
	    gboolean delsp = libbalsa_message_body_is_delsp(info->body);
            ptr = libbalsa_wrap_rfc2646(ptr, G_MAXINT, FALSE, TRUE, delsp);
        } else if (bm->wrap_text)
            libbalsa_wrap_string(ptr, balsa_app.browse_wrap_length);

        item = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(item), FALSE);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(item), 2);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(item), 15);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(item), GTK_WRAP_WORD);

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

        buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(item));
        gtk_text_buffer_create_tag(buffer, "soft", NULL);
        allocate_quote_colors(GTK_WIDGET(bm), balsa_app.quoted_color,
                              0, MAX_QUOTED_COLOR - 1);
        if (regcomp(&rex, balsa_app.quote_regex, REG_EXTENDED) != 0) {
            g_warning
                ("part_info_init_mimetext: quote regex compilation failed.");
            gtk_text_buffer_insert_at_cursor(buffer, ptr, -1);
        } else {
            gchar **lines;
            gchar **l = g_strsplit(ptr, "\n", -1);

            gtk_text_buffer_create_tag(buffer, "url",
                                       "foreground-gdk",
                                       &balsa_app.url_color, NULL);
            gtk_text_buffer_create_tag(buffer, "emphasize", 
                                       "foreground", "red",
                                       "underline", PANGO_UNDERLINE_SINGLE,
                                       NULL);

            for (lines = l; *lines; ++lines) {
                gint quote_level = is_a_quote(*lines, &rex);
                GtkTextTag *tag = quote_tag(buffer, quote_level);

                /* tag is NULL if the line isn't quoted, but it causes
                 * no harm */
                libbalsa_insert_with_url(buffer, *lines, tag,
                                         url_found_cb, &url_list);
                gtk_text_buffer_insert_at_cursor(buffer, "\n", 1);
            }
            g_strfreev(l);
            regfree(&rex);
        }

        if (libbalsa_message_body_is_flowed(info->body))
            libbalsa_wrap_view(GTK_TEXT_VIEW(item),
                               balsa_app.browse_wrap_length);
        prepare_url_offsets(buffer, url_list);

        g_signal_connect_after(G_OBJECT(item), "realize",
                               G_CALLBACK(fix_text_widget), url_list);
        if (url_list) {
            g_signal_connect(G_OBJECT(item), "button_press_event",
                             G_CALLBACK(store_button_coords), NULL);
            g_signal_connect(G_OBJECT(item), "button_release_event",
                             G_CALLBACK(check_call_url), url_list);
            g_signal_connect(G_OBJECT(item), "motion-notify-event",
                             G_CALLBACK(check_over_url), url_list);
            g_signal_connect(G_OBJECT(item), "leave-notify-event",
                             G_CALLBACK(check_over_url), url_list);
        }

        g_free(ptr);

        gtk_widget_show(item);
        info->focus_widget = item;
        info->widget = item;
        info->can_display = TRUE;
        /* size allocation may not be correct, so we'll check back later
         */
        resize_idle_id = g_idle_add((GSourceFunc) resize_idle, item);
    }

    fclose(fp);
}
#ifdef HAVE_GTKHTML
static gboolean
balsa_gtk_html_popup(HtmlView * view)
{
    GtkWidget *menu, *menuitem;

    menu = gtk_menu_new();
    menuitem = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_IN, NULL);
    g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
			     G_CALLBACK(html_view_zoom_in), view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    menuitem = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_OUT, NULL);
    g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
			     G_CALLBACK(html_view_zoom_out), view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    menuitem = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_100, NULL);
    g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
			     G_CALLBACK(html_view_zoom_reset), view);
    g_signal_connect(G_OBJECT(menu), "selection-done",
                     G_CALLBACK(gtk_widget_destroy), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    gtk_widget_show_all(menu);
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
	           0, gtk_get_current_event_time());
    return TRUE;
}

static gboolean
balsa_gtk_html_button_press_cb(HtmlView * view, GdkEventButton * event,
			       BalsaMessage * bm)
{
    return ((event->type == GDK_BUTTON_PRESS && event->button == 3)
	    ? balsa_gtk_html_popup(view) : FALSE);
}

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
    g_signal_connect(G_OBJECT(document), "request_url",
		     G_CALLBACK(balsa_gtk_html_url_requested), bm->message);
    html_document_write_stream(document, ptr, len);
    html_document_close_stream (document);

    g_signal_connect(G_OBJECT(html), "size_request",
		     G_CALLBACK(balsa_gtk_html_size_request),
                     (gpointer) bm);
    g_signal_connect(G_OBJECT(document), "link_clicked",
		     G_CALLBACK(balsa_gtk_html_link_clicked), NULL);
    g_signal_connect(G_OBJECT(html), "on_url",
		     G_CALLBACK(balsa_gtk_html_on_url), bm);
    g_signal_connect(G_OBJECT(html), "popup-menu",
		     G_CALLBACK(balsa_gtk_html_popup), bm);
    g_signal_connect(G_OBJECT(html), "button_press_event",
		     G_CALLBACK(balsa_gtk_html_button_press_cb), bm);

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

static GdkPixbuf *
gdk_pixbuf_new_from_file_scaled(const gchar *filename, gint width, gint height,
				GdkInterpType interp_type, GError **error)
{
    GdkPixbuf *tmp, *dest;

    if (!(tmp = gdk_pixbuf_new_from_file(filename, error)))
	return NULL;

    dest = gdk_pixbuf_scale_simple(tmp, width, height, interp_type);
    g_object_unref(tmp);
    return dest;
}

static inline gchar *
mpart_content_name(const gchar *content_type)
{
    if (g_ascii_strcasecmp(content_type, "multipart/mixed") == 0)
	return g_strdup(_("mixed parts"));
    else if (g_ascii_strcasecmp(content_type, "multipart/alternative") == 0)
	return g_strdup(_("alternative parts"));
    else if (g_ascii_strcasecmp(content_type, "multipart/signed") == 0)
	return g_strdup(_("signed parts"));
    else if (g_ascii_strcasecmp(content_type, "multipart/encrypted") == 0)
	return g_strdup(_("encrypted parts"));
    else if (g_ascii_strcasecmp(content_type, "message/rfc822") == 0)
	return g_strdup(_("rfc822 message"));
    else
	return g_strdup_printf(_("\"%s\" parts"), 
			       strchr(content_type, '/') + 1);
}

static void
display_part(BalsaMessage * bm, LibBalsaMessageBody * body,
	     GtkTreeIter *parent_iter)
{
    BalsaPartInfo *info = NULL;
    gchar *pix = NULL;
    gchar *content_type = libbalsa_message_body_get_content_type(body);
    gchar *icon_title = NULL;
    gboolean is_multipart=libbalsa_message_body_is_multipart(body);
    GtkTreeModel * model;
    GtkTreeIter iter;
    GdkPixbuf *content_icon;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(bm->treeview));
    gtk_tree_store_append (GTK_TREE_STORE(model), &iter, parent_iter);
    pix = libbalsa_icon_finder(content_type, body->filename, NULL);
	
    if(!is_multipart ||
       g_ascii_strcasecmp(content_type, "message/rfc822")==0 ||
       g_ascii_strcasecmp(content_type, "multipart/signed")==0 ||
       g_ascii_strcasecmp(content_type, "multipart/encrypted")==0 ||
       g_ascii_strcasecmp(content_type, "multipart/mixed")==0 ||
       g_ascii_strcasecmp(content_type, "multipart/alternative")==0) {

	info = balsa_part_info_new(body);
	bm->info_count++;

	if (g_ascii_strcasecmp(content_type, "message/rfc822") == 0 &&
	    body->embhdrs) {
	    gchar *from = libbalsa_address_to_gchar(body->embhdrs->from, 0);
	    icon_title = 
		g_strdup_printf(_("rfc822 message (from %s, subject \"%s\")"),
				from, body->embhdrs->subject);
	    g_free(from);
	} else if (is_multipart)
	    icon_title = mpart_content_name(content_type);
	else if (body->filename)
	    icon_title =
		g_strdup_printf("%s (%s)", body->filename, content_type);
	else
	    icon_title = g_strdup_printf("(%s)", content_type);
	
	part_create_menu (info);
	info->path = gtk_tree_model_get_path(model, &iter);

	/* add to the tree view */
#ifdef HAVE_GPGME
	if (libbalsa_is_pgp_encrypted(body))
	    content_icon =
		gtk_widget_render_icon(GTK_WIDGET(balsa_app.main_window),
				       BALSA_PIXMAP_INFO_ENCR,
				       GTK_ICON_SIZE_MENU, NULL);
	else if (body->sig_info) {
	    if (body->sig_info->status == GPGME_SIG_STAT_GOOD) {
		if (body->sig_info->validity >= GPGME_VALIDITY_MARGINAL &&
		    body->sig_info->trust >= GPGME_VALIDITY_MARGINAL)
		    content_icon =
			gtk_widget_render_icon(GTK_WIDGET(balsa_app.main_window),
					       BALSA_PIXMAP_INFO_SIGN_GOOD,
					       GTK_ICON_SIZE_MENU, NULL);
		else
		    content_icon =
			gtk_widget_render_icon(GTK_WIDGET(balsa_app.main_window),
					       BALSA_PIXMAP_INFO_SIGN_NOTRUST,
					       GTK_ICON_SIZE_MENU, NULL);
	    } else
		content_icon =
		    gtk_widget_render_icon(GTK_WIDGET(balsa_app.main_window),
					   BALSA_PIXMAP_INFO_SIGN_BAD,
					   GTK_ICON_SIZE_MENU, NULL);
	} else
#endif
	    content_icon =
		gdk_pixbuf_new_from_file_scaled(pix, 16, 16, GDK_INTERP_BILINEAR, NULL);
	gtk_tree_store_set (GTK_TREE_STORE(model), &iter, 
			    PART_INFO_COLUMN, info,
			    MIME_ICON_COLUMN, content_icon,
			    MIME_TYPE_COLUMN, icon_title, -1);
	
	g_object_unref(info);
	g_free(icon_title);
    } else {
	content_icon =
	    gdk_pixbuf_new_from_file_scaled(pix, 16, 16, GDK_INTERP_BILINEAR, NULL);
	gtk_tree_store_set (GTK_TREE_STORE(model), &iter, 
			    PART_INFO_COLUMN, NULL,
			    MIME_ICON_COLUMN, content_icon,
			    MIME_TYPE_COLUMN, content_type, -1);
    }
	
    g_object_unref(G_OBJECT(content_icon));
    g_free(pix);
    if (is_multipart) {
	if (balsa_app.debug)
	    fprintf(stderr, "part: multipart\n");
	display_multipart(bm, body, &iter);
	if (balsa_app.debug)
	    fprintf(stderr, "part end: multipart\n");
    }
    g_free(content_type);
}

static void
display_content(BalsaMessage * bm)
{
    LibBalsaMessageBody *body;

    balsa_message_clear_tree(bm);
    for (body = bm->message->body_list; body; body = body->next)
	display_part(bm, body, NULL);
    gtk_tree_view_columns_autosize(GTK_TREE_VIEW(bm->treeview));
    gtk_tree_view_expand_all(GTK_TREE_VIEW(bm->treeview));
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


static void
balsa_part_info_init(GObject *object, gpointer data)
{
    BalsaPartInfo * info = BALSA_PART_INFO(object);
    
    info->body = NULL;
    info->widget = NULL;
    info->focus_widget = NULL;
    info->popup_menu = NULL;
    info->can_display = FALSE;
    info->path = NULL;
}

static BalsaPartInfo*
balsa_part_info_new(LibBalsaMessageBody* body) 
{
    BalsaPartInfo * info = g_object_new(TYPE_BALSA_PART_INFO, NULL);
    info->body = body;
    return info;
}

static void
balsa_part_info_free(GObject * object)
{
    BalsaPartInfo * info;
    GObjectClass *parent_class;

    g_return_if_fail(object != NULL);
    g_return_if_fail(IS_BALSA_PART_INFO(object));
    info = BALSA_PART_INFO(object);

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

    gtk_tree_path_free(info->path);

    parent_class = g_type_class_peek_parent(G_OBJECT_GET_CLASS(object));
    parent_class->finalize(object);    
}

static void
part_context_save_all_cb(GtkWidget * menu_item, GList * info_list)
{
    while (info_list) {
	save_part(BALSA_PART_INFO(info_list->data));
	info_list = g_list_next(info_list);
    }
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
    message->headers->from = libbalsa_address_new_from_string(data);
    g_free (data);

    data = libbalsa_message_body_get_parameter(info->body, "subject");
    if (data)
	LIBBALSA_MESSAGE_SET_SUBJECT(message, data);

    data = libbalsa_message_body_get_parameter(info->body, "server");
    message->headers->to_list = libbalsa_address_new_list_from_string(data);
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
			      _("Could not create temporary file %s"),
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

typedef struct _selFirst_T {
    GtkTreeIter sel_iter;
    gboolean found;
} selFirst_T;

static void
tree_selection_get_first(GtkTreeModel * model, GtkTreePath * path,
			 GtkTreeIter * iter, gpointer data)
{
    selFirst_T *sel = (selFirst_T *)data;

    if (!sel->found) {
	sel->found = TRUE;
	memcpy (&sel->sel_iter, iter, sizeof(GtkTreeIter));
    }
}

void
balsa_message_next_part(BalsaMessage * bmessage)
{
    selFirst_T sel;
    GtkTreeView *gtv;
    GtkTreeModel *model;
    BalsaPartInfo *info;

    g_return_if_fail(bmessage != NULL);
    g_return_if_fail(bmessage->treeview != NULL);
    
    gtv = GTK_TREE_VIEW(bmessage->treeview);
    model = gtk_tree_view_get_model(gtv);

    /* get the info of the first selected part */
    sel.found = FALSE;
    gtk_tree_selection_selected_foreach(gtk_tree_view_get_selection(gtv),
					tree_selection_get_first, &sel);
    if (!sel.found) {
	/* select the first part if nothing is selected */
	if (!gtk_tree_model_get_iter_first(model, &sel.sel_iter))
	    return;
    } else {
	GtkTreeIter child;

	/* If the first selected iter has a child, select it, otherwise take
	   next on the same level. If there is no next, don't move & beep */
	if (gtk_tree_model_iter_children (model, &child, &sel.sel_iter))
	    memcpy (&sel.sel_iter, &child, sizeof(GtkTreeIter));
	else if (!gtk_tree_model_iter_next (model, &sel.sel_iter)) {
	    gdk_beep();
	    return;
	}
    }
    
    gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(gtv));
    info = tree_next_valid_part_info(model, &sel.sel_iter);
    select_part(bmessage, info);
}

void
balsa_message_previous_part(BalsaMessage * bmessage)
{
    selFirst_T sel;
    GtkTreeView *gtv;
    GtkTreeModel *model;
    BalsaPartInfo *info;

    g_return_if_fail(bmessage != NULL);
    g_return_if_fail(bmessage->treeview != NULL);
    
    gtv = GTK_TREE_VIEW(bmessage->treeview);
    model = gtk_tree_view_get_model(gtv);

    /* get the info of the first selected part */
    sel.found = FALSE;
    gtk_tree_selection_selected_foreach(gtk_tree_view_get_selection(gtv),
					tree_selection_get_first, &sel);
    if (!sel.found) {
	/* select the first part if nothing is selected */
	if (!gtk_tree_model_get_iter_first(model, &sel.sel_iter))
	    return;
    } else {
	GtkTreePath * path = gtk_tree_model_get_path(model, &sel.sel_iter);

	/* find the previous element with a valid info block */
	do {
	    if (!gtk_tree_path_prev (path)) {
		if (gtk_tree_path_get_depth (path) <= 1) {
		    gdk_beep();
		    gtk_tree_path_free(path);
		    return;
		}
		gtk_tree_path_up(path);
	    }
	    gtk_tree_model_get_iter(model, &sel.sel_iter, path);
	    gtk_tree_model_get(model, &sel.sel_iter, PART_INFO_COLUMN, &info, -1);
	} while (!info);
	g_object_unref(G_OBJECT(info));
	gtk_tree_path_free(path);
    }
	    
    gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(gtv));
    select_part(bmessage, info);
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

	if(g_ascii_strcasecmp(content_type, "text/html")==0) {
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

	if(g_ascii_strcasecmp(content_type, "text/plain")==0) {
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

typedef struct _treeSearchT {
    const LibBalsaMessageBody *body;
    BalsaPartInfo *info;
} treeSearchT;

static gboolean 
treeSearch_Func(GtkTreeModel * model, GtkTreePath *path,
		GtkTreeIter * iter, gpointer data)
{
    treeSearchT *search = (treeSearchT *)data;
    BalsaPartInfo *info = NULL;

    gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);
    if (info) {
	g_object_unref(G_OBJECT(info));
	if (info->body == search->body) {
	    search->info = info;
	    return TRUE;
	}
    }

    return FALSE;    
}

static BalsaPartInfo *
part_info_from_body(BalsaMessage *bm, const LibBalsaMessageBody *body)
{
    treeSearchT search;

    search.body = body;
    search.info = NULL;

    gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(bm->treeview)),
			   treeSearch_Func, &search);
    return search.info;
}


static void add_body(BalsaMessage *bm, 
		     LibBalsaMessageBody *body)
{
    if(body) {
	BalsaPartInfo *info = part_info_from_body(bm, body);
	
	if (info)
	    add_part(bm, part_info_from_body(bm, body));
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
        g_source_remove(resize_idle_id);
    resize_idle_id = g_idle_add((GSourceFunc) resize_idle, widget);
}

static BalsaPartInfo *add_part(BalsaMessage *bm, BalsaPartInfo *info)
{
    if (info) {
	GtkTreeSelection *selection = 
	    gtk_tree_view_get_selection(GTK_TREE_VIEW(bm->treeview));

	if (info->path && 
	    !gtk_tree_selection_path_is_selected (selection, info->path))
	    gtk_tree_selection_select_path(selection, info->path);

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


static gboolean
gtk_tree_hide_func(GtkTreeModel * model, GtkTreePath * path,
		   GtkTreeIter * iter, gpointer data)
{
    BalsaPartInfo *info;
    BalsaMessage * bm = (BalsaMessage *)data;

    gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);
    if (info) {
	if (info->widget && info->widget->parent)
	    gtk_container_remove(GTK_CONTAINER(bm->content),
				 info->widget);
	g_object_unref(G_OBJECT(info));
    }
    
    return FALSE;
}

static void
hide_all_parts(BalsaMessage * bm)
{
    if (bm->current_part) {
	gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(bm->treeview)),
			       gtk_tree_hide_func, bm);
	gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(bm->treeview)));
        bm->current_part = NULL;
    }
}

/* 
 * If part == -1 then change to no part
 * must release selection before hiding a text widget.
 */
static void
select_part(BalsaMessage * bm, BalsaPartInfo *info)
{
    hide_all_parts(bm);
    gtk_widget_modify_font(bm->header_text,
                           pango_font_description_from_string
                           (balsa_app.message_font));

    bm->current_part = add_part(bm, info);

    if(bm->current_part)
	g_signal_emit(G_OBJECT(bm), balsa_message_signals[SELECT_PART], 0);

    scroll_set(GTK_VIEWPORT(bm->cont_viewport)->hadjustment, 0);
    scroll_set(GTK_VIEWPORT(bm->cont_viewport)->vadjustment, 0);

    gtk_widget_queue_resize(bm->cont_viewport);
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

    viewport = GTK_VIEWPORT(bm->cont_viewport);

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
    
    requisition->width  = GTK_LAYOUT(widget)->hadjustment->upper;
    requisition->height = GTK_LAYOUT(widget)->vadjustment->upper;
}

static gboolean
balsa_gtk_html_url_requested(GtkWidget *html, const gchar *url,
			     HtmlStream* stream, LibBalsaMessage* msg)
{
    FILE* f;
    int i;
    char buf[4096];

    if(strncmp(url,"cid:",4)) {
	printf("non-local URL request ignored: %s\n", url);
	return FALSE;
    }
    if( (f=libbalsa_message_get_part_by_id(msg,url+4)) == NULL) {
	gchar *s = g_strconcat("<",url+4,">",NULL);
	
	if( s == NULL )
	    return FALSE;

	f = libbalsa_message_get_part_by_id(msg,s);
	g_free(s);
	if( f == NULL )
	    return FALSE;
    }

    while ((i = fread (buf, 1, sizeof(buf), f)) != 0)
	html_stream_write (stream, buf, i);
    html_stream_close(stream);
    fclose (f);
    
    return TRUE;
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

/*
 * This function informs the caller if the currently selected part 
 * supports selection/copying etc. Currently only the GtkEditable derived 
 * widgets
 * and GtkTextView
 * are supported for this (GtkHTML could be, but I don't have a 
 * working build right now)
 */
gboolean
balsa_message_can_select(BalsaMessage * bmessage)
{
    GtkWidget *w;

    g_return_val_if_fail(bmessage != NULL, FALSE);

    if (bmessage->current_part == NULL
        || (w = bmessage->current_part->focus_widget) == NULL)
	return FALSE;

    return GTK_IS_EDITABLE(w) || GTK_IS_TEXT_VIEW(w);
}

gboolean
balsa_message_grab_focus(BalsaMessage * bmessage)
{
    GtkWidget *w;

    g_return_val_if_fail(bmessage != NULL, FALSE);
    g_return_val_if_fail(bmessage->current_part != NULL, FALSE);
    g_return_val_if_fail((w =
                          bmessage->current_part->focus_widget) != NULL,
                         FALSE);

    gtk_widget_grab_focus(w);
    return TRUE;
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
    if (message->headers->reply_to)
	use_from = message->headers->reply_to;
    else if (message->headers->from)
	use_from = message->headers->from;
    else if (message->sender)
	use_from = message->sender;
    else
	use_from = NULL;
    suspicious =
	!rfc2298_address_equal (message->headers->dispnotify_to, use_from);
    
    if (!suspicious) {
	/* Try to find "my" address first in the to, then in the cc list */
	list = g_list_first(message->headers->to_list);
	found = FALSE;
	while (list && !found) {
	    addr = list->data;
	    found = rfc2298_address_equal (balsa_app.current_ident->address, addr);
	    list = list->next;
	}
	if (!found) {
	    list = g_list_first(message->headers->cc_list);
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
	reply_to = 
	    libbalsa_address_to_gchar (message->headers->dispnotify_to, -1);
	gtk_widget_show_all (create_mdn_dialog (sender, reply_to, mdn));
	g_free (reply_to);
	g_free (sender);
    } else {
#if ENABLE_ESMTP
	libbalsa_message_send(mdn, balsa_app.outbox, NULL,
			      balsa_app.encoding_style,  
			      balsa_app.smtp_server,
			      balsa_app.smtp_authctx,
			      balsa_app.smtp_tls_mode, TRUE);
#else
	libbalsa_message_send(mdn, balsa_app.outbox, NULL,
			      balsa_app.encoding_style, TRUE);
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
    gchar **params;

    /* create a message with the header set from the incoming message */
    message = libbalsa_message_new();
    dummy = libbalsa_address_to_gchar(balsa_app.current_ident->address, 0);
    message->headers->from = libbalsa_address_new_from_string(dummy);
    g_free (dummy);
    LIBBALSA_MESSAGE_SET_SUBJECT(message,
				 g_strdup("Message Disposition Notification"));
    dummy = libbalsa_address_to_gchar(for_msg->headers->dispnotify_to, 0);
    message->headers->to_list = libbalsa_address_new_list_from_string(dummy);
    g_free (dummy);

    /* RFC 2298 requests this mime type... */
    message->subtype = g_strdup("report");
    params = g_new(gchar *, 3);
    params[0] = g_strdup("report-type");
    params[1] = g_strdup("disposition-notification");
    params[2] = NULL;
    message->parameters = g_list_prepend(message->parameters, params);
    
    /* the first part of the body is an informational note */
    body = libbalsa_message_body_new(message);
    date = libbalsa_message_date_to_gchar(for_msg, balsa_app.date_string);
    dummy = libbalsa_make_string_from_list(for_msg->headers->to_list);
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
    gtk_window_set_title(GTK_WINDOW(mdn_dialog), _("Reply to MDN?"));
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
                              balsa_app.smtp_tls_mode, TRUE);
#else
        libbalsa_message_send(send_msg, balsa_app.outbox, NULL,
                              balsa_app.encoding_style, TRUE);
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

        if (!tag) {
            tag =
                gtk_text_buffer_create_tag(buffer, name, "foreground-gdk",
                                           &balsa_app.quoted_color[level],
                                           NULL);
            /* Set a low priority, so we can set both quote color and
             * URL color, and URL color will take precedence. */
            gtk_text_tag_set_priority(tag, 0);
        }
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
    GtkTextTag *tag;
    GtkTextIter start, end;

    if (!url)
        return;

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
    table = gtk_text_buffer_get_tag_table(buffer);
    tag = gtk_text_tag_table_lookup(table, "emphasize");

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

    for (; url_list; url_list = g_list_next(url_list)) {
        url = (message_url_t *) url_list->data;
        if (url->start <= offset && offset < url->end)
            return url;
    }

    return NULL;
}

/* After wrapping the buffer, populate the start and end offsets for
 * each url. */
static void
prepare_url_offsets(GtkTextBuffer * buffer, GList * url_list)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *url_tag = gtk_text_tag_table_lookup(table, "url");

    for (; url_list; url_list = g_list_next(url_list)) {
        message_url_t *url = url_list->data;
        GtkTextIter iter;

        gtk_text_buffer_get_iter_at_mark(buffer, &iter, url->end_mark);
        url->end = gtk_text_iter_get_offset(&iter);
#ifdef BUG_102711_FIXED
        gtk_text_iter_backward_to_tag_toggle(&iter, url_tag);
#else
        while (gtk_text_iter_backward_char(&iter))
            if (gtk_text_iter_begins_tag(&iter, url_tag))
                break;
#endif                          /* BUG_102711_FIXED */
        url->start = gtk_text_iter_get_offset(&iter);
    }
}

#ifdef HAVE_GTKHTML
/* Does the current part support zoom? */
gboolean
balsa_message_can_zoom(BalsaMessage * bm)
{
    return (bm && bm->current_part
	    && HTML_IS_VIEW(bm->current_part->widget));
}

/* Zoom an HtmlView item. */
void
balsa_message_zoom(BalsaMessage * bm, gint in_out)
{
    if (!balsa_message_can_zoom(bm))
	return;

    switch (in_out) {
    case +1:
	html_view_zoom_in(HTML_VIEW(bm->current_part->widget));
	break;
    case -1:
	html_view_zoom_out(HTML_VIEW(bm->current_part->widget));
	break;
    case 0:
	html_view_zoom_reset(HTML_VIEW(bm->current_part->widget));
	break;
    default:
	break;
    }
}
#endif /* HAVE_GTKHTML */
