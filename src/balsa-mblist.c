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

#include <gnome.h>
#include <gtk/gtkfeatures.h>
#include <string.h>
#include <gdk/gdkprivate.h>

#include "mailbox-conf.h"
#include "save-restore.h"
#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-mblist.h"

#include "libbalsa.h"

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#include "threads.h"
#endif

enum {
    SELECT_MAILBOX,
    LAST_SIGNAL
};

/* object arguments */
enum {
    ARG_0,
    ARG_SHOW_CONTENT_INFO
};

/* Drag and Drop stuff */
enum {
    TARGET_MESSAGES
};

static GtkTargetEntry mblist_drop_types[] = {
    {"x-application/x-message-list", GTK_TARGET_SAME_APP, TARGET_MESSAGES}
};

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

static gint balsa_mblist_signals[LAST_SIGNAL] = { 0 };

static BalsaMBListClass *parent_class = NULL;

static void select_mailbox(GtkCTree * ctree, GtkCTreeNode * row,
			   gint column);
static void button_event_press_cb(GtkCTree * ctree, GdkEventButton * event,
				  gpointer user_data);

static void balsa_mblist_unread_messages_changed_cb(LibBalsaMailbox *
						    mailbox, gboolean flag,
						    BalsaMBList * mblist);

static void balsa_mblist_disconnect_mailbox_signals(GtkCTree * tree,
						    GtkCTreeNode * node,
						    gpointer data);

/* callbacks */
static gboolean mailbox_nodes_to_ctree(GtkCTree *, guint, GNode *,
				       GtkCTreeNode *, gpointer);
static void balsa_mblist_class_init(BalsaMBListClass * class);
static void balsa_mblist_init(BalsaMBList * tree);
static void balsa_mblist_set_arg(GtkObject * object, GtkArg * arg,
				 guint arg_id);
static void balsa_mblist_get_arg(GtkObject * object, GtkArg * arg,
				 guint arg_id);
static void balsa_mblist_column_resize(GtkCList * clist, gint column,
				       gint size, gpointer data);
static void balsa_mblist_column_click(GtkCList * clist, gint column,
				      gpointer data);

static void mailbox_tree_expand(GtkCTree *, GList *, gpointer);
static void mailbox_tree_collapse(GtkCTree *, GList *, gpointer);
static void balsa_mblist_mailbox_style(GtkCTree * ctree,
				       GtkCTreeNode * node,
				       MailboxNode * mbnode);

static void balsa_mblist_folder_style(GtkCTree * ctree,
				      GtkCTreeNode * node, gpointer data);
GdkFont *balsa_widget_get_bold_font(GtkWidget * widget);
static void balsa_mblist_set_style(BalsaMBList * mblist);

static gint numeric_compare(GtkCList * clist, gconstpointer ptr1,
			    gconstpointer ptr2);
static gint mblist_mbnode_compare(gconstpointer a, gconstpointer b);
static void mblist_drag_cb (GtkWidget* widget,
                            GdkDragContext* context, gint x, gint y,
                            GtkSelectionData* selection_data,
                            guint info, guint32 time, gpointer data);


guint
balsa_mblist_get_type(void)
{
    static guint mblist_type = 0;

    if (!mblist_type) {
	GtkTypeInfo mblist_info = {
	    "BalsaMBList",
	    sizeof(BalsaMBList),
	    sizeof(BalsaMBListClass),
	    (GtkClassInitFunc) balsa_mblist_class_init,
	    (GtkObjectInitFunc) balsa_mblist_init,
	    (GtkArgSetFunc) NULL,
	    (GtkArgGetFunc) NULL,
	};

	mblist_type = gtk_type_unique(gtk_ctree_get_type(), &mblist_info);
    }
    return mblist_type;
}

GtkWidget *
balsa_mblist_new()
{
    BalsaMBList *new;

    new = gtk_type_new(balsa_mblist_get_type());
    return GTK_WIDGET(new);
}

static void
balsa_mblist_destroy(GtkObject * obj)
{
    BalsaMBList *del;

    del = BALSA_MBLIST(obj);
    /* chain up ... */
    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(del));
}


static void
balsa_mblist_class_init(BalsaMBListClass * klass)
{
    GtkObjectClass *object_class;
    GtkCTreeClass *tree_class;

    object_class = (GtkObjectClass *) klass;
    tree_class = GTK_CTREE_CLASS(klass);

    balsa_mblist_signals[SELECT_MAILBOX] =
	gtk_signal_new("select_mailbox",
		       GTK_RUN_LAST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(BalsaMBListClass, select_mailbox),
		       gtk_marshal_NONE__POINTER_POINTER_POINTER,
		       GTK_TYPE_NONE, 3, GTK_TYPE_POINTER,
		       GTK_TYPE_POINTER, GTK_TYPE_GDK_EVENT);

    gtk_object_class_add_signals(object_class, balsa_mblist_signals,
				 LAST_SIGNAL);

    object_class->destroy = balsa_mblist_destroy;
    parent_class = gtk_type_class(gtk_ctree_get_type());
    object_class->set_arg = balsa_mblist_set_arg;
    object_class->get_arg = balsa_mblist_get_arg;

    gtk_object_add_arg_type("BalsaMBList::show_content_info",
			    GTK_TYPE_BOOL, GTK_ARG_READWRITE,
			    ARG_SHOW_CONTENT_INFO);

    klass->select_mailbox = NULL;
}

static void
balsa_mblist_set_arg(GtkObject * object, GtkArg * arg, guint arg_id)
{
    BalsaMBList *bmbl;

    bmbl = BALSA_MBLIST(object);

    switch (arg_id) {
    case ARG_SHOW_CONTENT_INFO:
	bmbl->display_content_info = GTK_VALUE_BOOL(*arg);
	balsa_mblist_repopulate(bmbl);
	break;

    default:
	break;
    }
}

static void
balsa_mblist_get_arg(GtkObject * object, GtkArg * arg, guint arg_id)
{
    BalsaMBList *bmbl;

    bmbl = BALSA_MBLIST(object);

    switch (arg_id) {
    case ARG_SHOW_CONTENT_INFO:
	GTK_VALUE_BOOL(*arg) = bmbl->display_content_info;
	break;
    default:
	break;

    }
}

static LibBalsaMailbox*
mblist_get_selected_mailbox(BalsaMBList *mbl)
{
    GtkCTreeNode *node;
    MailboxNode *mbnode;

    g_assert(mbl != NULL);

    if (!GTK_CLIST(mbl)->selection) return NULL;

    node = GTK_CTREE_NODE(GTK_CLIST(mbl)->selection->data);
    mbnode = gtk_ctree_node_get_row_data(GTK_CTREE(mbl), node);
    return mbnode->mailbox;
}

/* mbox_is_unread: 
   NOTE mbnode->mailbox == NULL for directories 
*/
static gboolean
mbox_is_unread(gconstpointer a, gconstpointer b)
{
    MailboxNode *mbnode = (MailboxNode *) a;
    g_assert(mbnode != NULL);
    return !(mbnode->mailbox && mbnode->mailbox->has_unread_messages);
}

/* mblist_find_all_unread_mboxes:
   find all nodes and translate them to mailbox list 
*/
GList *
mblist_find_all_unread_mboxes(void)
{
    GList *res = NULL, *r, *i;
    MailboxNode *mbnode;
    g_assert(balsa_app.mblist != NULL);

    r = gtk_ctree_find_all_by_row_data_custom(GTK_CTREE(balsa_app.mblist), 
					      NULL, NULL, mbox_is_unread);

    for (i = g_list_first(r); i; i = g_list_next(i)) {
	mbnode =
	    gtk_ctree_node_get_row_data(GTK_CTREE(balsa_app.mblist), i->data);
	res = g_list_append(res, mbnode->mailbox);
    }
    g_list_free(r);
    return res;
}


static void
add_menu_entry(GtkWidget * menu, const gchar * label, GtkSignalFunc cb,
	       LibBalsaMailbox * mailbox)
{
    GtkWidget *menuitem;

    if (label)
	menuitem = gtk_menu_item_new_with_label(label);
    else
	menuitem = gtk_menu_item_new();

    if (cb)
	gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
			   GTK_SIGNAL_FUNC(cb), mailbox);

    gtk_menu_append(GTK_MENU(menu), menuitem);
    gtk_widget_show(menuitem);
}

static void
mb_open_cb(GtkWidget * widget, LibBalsaMailbox * mailbox)
{
    mblist_open_mailbox(mailbox);
}

static void
mb_close_cb(GtkWidget * widget, LibBalsaMailbox * mailbox)
{
    balsa_window_close_mailbox(balsa_app.main_window, mailbox);
    balsa_mblist_have_new(balsa_app.mblist);
}

static void
mb_conf_cb(GtkWidget * widget, LibBalsaMailbox * mailbox)
{
    mailbox_conf_edit (mailbox);
}

static void
mb_del_cb(GtkWidget * widget, LibBalsaMailbox * mailbox)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    mailbox_conf_delete(mailbox);
}


/* mb_inbox_cb:
   sets the given mailbox as inbox.
*/
static void
mb_inbox_cb(GtkWidget * widget, LibBalsaMailbox * mailbox)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    config_mailbox_set_as_special(mailbox, SPECIAL_INBOX);
    balsa_mblist_repopulate(BALSA_MBLIST(balsa_app.mblist));
}

static void
mb_sentbox_cb(GtkWidget * widget, LibBalsaMailbox * mailbox)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    config_mailbox_set_as_special(mailbox, SPECIAL_SENT);
    balsa_mblist_repopulate(BALSA_MBLIST(balsa_app.mblist));
}

static void
mb_trash_cb(GtkWidget * widget, LibBalsaMailbox * mailbox)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    config_mailbox_set_as_special(mailbox, SPECIAL_TRASH);
    balsa_mblist_repopulate(BALSA_MBLIST(balsa_app.mblist));
}

static void
mb_draftbox_cb(GtkWidget * widget, LibBalsaMailbox * mailbox)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    config_mailbox_set_as_special(mailbox, SPECIAL_DRAFT);
    balsa_mblist_repopulate(BALSA_MBLIST(balsa_app.mblist));
}

static GtkWidget *
mblist_create_context_menu(GtkCTree * ctree, LibBalsaMailbox * mailbox)
{
    GtkWidget *menu;
    GtkWidget *submenu;
    GtkWidget *menuitem;

    /*  g_return_val_if_fail(mailbox != NULL, NULL); */

    menu = gtk_menu_new();

    submenu = gtk_menu_new();
    add_menu_entry(submenu, _("Local Mbox Mailbox..."), mblist_menu_add_mbox_cb, NULL);
    add_menu_entry(submenu, _("Local Maildir Mailbox..."), mblist_menu_add_maildir_cb, NULL);
    add_menu_entry(submenu, _("Local MH Mailbox..."), mblist_menu_add_mh_cb, NULL);
    add_menu_entry(submenu, _("Remote IMAP Mailbox..."), mblist_menu_add_imap_cb, NULL);
    gtk_widget_show(submenu);
    
    menuitem = gtk_menu_item_new_with_label(_("New"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem),
			      submenu);
    gtk_widget_show(menuitem);
    
    gtk_menu_append(GTK_MENU(menu), menuitem);
    
    /* If we didn't click on a mailbox then there is only one option. */
    if (mailbox == NULL)
	return menu;

    add_menu_entry(menu, NULL, NULL, mailbox);

    if (mailbox->open_ref == 0)
	add_menu_entry(menu, _("Open"), mb_open_cb, mailbox);
    else
	add_menu_entry(menu, _("Close"), mb_close_cb, mailbox);

    add_menu_entry(menu, _("Properties..."), mb_conf_cb, mailbox);

    add_menu_entry(menu, _("Delete"), mb_del_cb, mailbox);

    add_menu_entry(menu, NULL, NULL, mailbox);

    add_menu_entry(menu, _("Mark as Inbox"), mb_inbox_cb, mailbox);
    add_menu_entry(menu, _("Mark as Sentbox"), mb_sentbox_cb, mailbox);
    add_menu_entry(menu, _("Mark as Trash"), mb_trash_cb, mailbox);
    add_menu_entry(menu, _("Mark as Draftbox"), mb_draftbox_cb, mailbox);

    return menu;
}

/* mblist_open_mailbox
 * 
 * Description: This checks to see if the mailbox is already on a different
 * mailbox page, or if a new page needs to be created and the mailbox
 * parsed.
 */
void
mblist_open_mailbox(LibBalsaMailbox * mailbox)
{
    GtkWidget *page = NULL;
    int i, c;

    c = gtk_notebook_get_current_page(GTK_NOTEBOOK(balsa_app.notebook));

    /* If we currently have a page open, update the time last visited */
    if (c != -1) {
	page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), c);
	page = gtk_object_get_data(GTK_OBJECT(page), "indexpage");
	g_get_current_time(&BALSA_INDEX_PAGE(page)->last_use);
    }
    
    i = balsa_find_notebook_page_num(mailbox);
    if (i != -1) {
	gtk_notebook_set_page(GTK_NOTEBOOK(balsa_app.notebook), i);
	page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i);
	page = gtk_object_get_data(GTK_OBJECT(page), "indexpage");
	g_get_current_time(&BALSA_INDEX_PAGE(page)->last_use);
	
/* This nasty looking piece of code is for the column resizing patch, it needs
   to change the size of the columns before they get displayed.  To do this we
   need to get the reference to the index page, which gives us a reference to
   the index, which gives us the clist, which we then reference.  Looks ugly
   but works well. */
	gtk_clist_set_column_width(GTK_CLIST
				   ((BALSA_INDEX_PAGE(page)->index)),
				   0, balsa_app.index_num_width);
	gtk_clist_set_column_width(GTK_CLIST
				   ((BALSA_INDEX_PAGE(page)->index)),
				   1, balsa_app.index_status_width);
	gtk_clist_set_column_width(GTK_CLIST
				   ((BALSA_INDEX_PAGE(page)->index)),
				   2,
				   balsa_app.index_attachment_width);
	gtk_clist_set_column_width(GTK_CLIST
				   ((BALSA_INDEX_PAGE(page)->index)),
				   3, balsa_app.index_from_width);
	gtk_clist_set_column_width(GTK_CLIST
				   ((BALSA_INDEX_PAGE(page)->index)),
				   4, balsa_app.index_subject_width);
	gtk_clist_set_column_width(GTK_CLIST
				   ((BALSA_INDEX_PAGE(page)->index)),
				   5, balsa_app.index_date_width);
    } else { /* page with mailbox not found, open it */
	balsa_window_open_mailbox(balsa_app.main_window, mailbox);

	if (balsa_app.mblist->display_content_info)
	    balsa_mblist_update_mailbox(balsa_app.mblist, mailbox);
    }
    
    balsa_mblist_have_new(balsa_app.mblist);
}

void
mblist_menu_add_mbox_cb(GtkWidget * widget, gpointer data)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_MBOX);
}

void
mblist_menu_add_maildir_cb(GtkWidget * widget, gpointer data)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_MAILDIR);
}

void
mblist_menu_add_mh_cb(GtkWidget * widget, gpointer data)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_MH);
}

void
mblist_menu_add_imap_cb(GtkWidget * widget, gpointer data)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_IMAP);
}

void
mblist_menu_edit_cb(GtkWidget * widget, gpointer data)
{
    LibBalsaMailbox *mailbox = mblist_get_selected_mailbox(balsa_app.mblist);

    if (mailbox == NULL) {
	GtkWidget *err_dialog =
	    gnome_error_dialog(_("No mailbox selected."));
	gnome_dialog_run(GNOME_DIALOG(err_dialog));
	return;
    }

    mailbox_conf_edit(mailbox);
}


void
mblist_menu_delete_cb(GtkWidget * widget, gpointer data)
{
    LibBalsaMailbox *mailbox = mblist_get_selected_mailbox(balsa_app.mblist);

    if (mailbox == NULL) {
	GtkWidget *err_dialog =
	    gnome_error_dialog(_("No mailbox selected."));
	gnome_dialog_run(GNOME_DIALOG(err_dialog));
	gtk_widget_destroy(GTK_WIDGET(err_dialog));
	return;
    }

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    mailbox_conf_delete(mailbox);
}

static void
size_allocate_cb(GtkWidget * widget, GtkAllocation * alloc)
{
    if (balsa_app.show_mblist)
	balsa_app.mblist_width = widget->parent->allocation.width;
}

static gboolean
mblist_button_press_cb(GtkWidget * widget, GdkEventButton * event,
		       gpointer user_data)
{
    BalsaMBList *bmbl;
    GtkCList *clist;
    GtkCTree *ctree;

    gint row, column;
    gint on_mailbox;
    MailboxNode *mbnode;
    LibBalsaMailbox *mailbox;
    GtkCTreeNode *node;

    bmbl = BALSA_MBLIST(widget);
    clist = GTK_CLIST(widget);
    ctree = GTK_CTREE(widget);

    on_mailbox = gtk_clist_get_selection_info(clist, event->x, event->y, 
					      &row, &column);

    if (on_mailbox) {
	node = gtk_ctree_node_nth(ctree, row);
	mbnode = gtk_ctree_node_get_row_data(ctree, node);
	mailbox = mbnode->mailbox;

	if (mbnode->IsDir)  return FALSE;

	g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

	switch(event->button) {
	case 1:
	    mblist_open_mailbox(mailbox);
	    break;
	case 3:
	    if (node) gtk_ctree_select(ctree, node);
	    gtk_menu_popup(GTK_MENU(
		mblist_create_context_menu(GTK_CTREE(bmbl), mailbox)), NULL,
			   NULL, NULL, NULL, event->button, event->time);
	    break;
	}
    } else {			/* not on_mailbox */
	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
	    /* simple click on right button */
	    gtk_menu_popup(
		GTK_MENU(mblist_create_context_menu(GTK_CTREE(bmbl), NULL)), 
		NULL, NULL, NULL, NULL, event->button, event->time);
	}
    }

    return FALSE;
}


static gboolean
mblist_key_press_cb(GtkWidget * widget, GdkEventKey * event, gpointer data)
{
    GtkCTreeNode *node;
    MailboxNode *mbnode;
    LibBalsaMailbox *mailbox;

    if (event->keyval == GDK_Return) {
	node =
	    gtk_ctree_node_nth(GTK_CTREE(widget),
			       GTK_CLIST(widget)->focus_row);
	g_return_val_if_fail(node, FALSE);
	mbnode = gtk_ctree_node_get_row_data(GTK_CTREE(widget), node);
	if (mbnode->IsDir)
	    return FALSE;

	mailbox = mbnode->mailbox;
	g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
	mblist_open_mailbox(mailbox);
	gtk_ctree_select(GTK_CTREE(widget), node);
	return TRUE;
    }
    return FALSE;

}

/* Set up the mail box list, including the tree's appearance and the callbacks */
static void
balsa_mblist_init(BalsaMBList * tree)
{
    GtkStyle* style;
    GdkFont* font;
    gint text_height;
    int i;
    char *titles[3] = { N_("Mailbox"), N_("Unread"), N_("Total") };
    for (i = 0; i < ELEMENTS(titles); i++) titles[i] = _(titles[i]);

    gtk_ctree_construct(GTK_CTREE(tree), 3, 0, titles);

    gtk_signal_connect(GTK_OBJECT(tree), "tree_expand",
		       GTK_SIGNAL_FUNC(mailbox_tree_expand), NULL);
    gtk_signal_connect(GTK_OBJECT(tree), "tree_collapse",
		       GTK_SIGNAL_FUNC(mailbox_tree_collapse), NULL);

    gtk_signal_connect(GTK_OBJECT(tree), "size_allocate",
		       GTK_SIGNAL_FUNC(size_allocate_cb), NULL);

    gtk_object_set (GTK_OBJECT (tree), "show_content_info", 
                    balsa_app.mblist_show_mb_content_info, NULL);

    /* gtk_ctree_set_show_stub(GTK_CTREE(tree), FALSE);
       gtk_clist_set_row_height(GTK_CLIST(tree), 16); */
    style = gtk_widget_get_style (GTK_WIDGET (tree));
    font = style->font;
    text_height = font->ascent + font->descent;
    
    if (text_height <= 16)
        gtk_clist_set_row_height (GTK_CLIST (tree), 16);
    else
        gtk_clist_set_row_height (GTK_CLIST (tree), text_height);

    gtk_clist_set_column_width(GTK_CLIST(tree), 0,
			       balsa_app.mblist_name_width);

    gtk_clist_set_column_width(GTK_CLIST(tree), 1,
			       balsa_app.mblist_newmsg_width);
    gtk_clist_set_column_justification(GTK_CLIST(tree), 1,
				       GTK_JUSTIFY_RIGHT);

    gtk_clist_set_column_width(GTK_CLIST(tree), 2,
			       balsa_app.mblist_totalmsg_width);
    gtk_clist_set_column_justification(GTK_CLIST(tree), 2,
				       GTK_JUSTIFY_RIGHT);

    gtk_clist_set_sort_column(GTK_CLIST(tree), 0);
    gtk_clist_set_sort_type(GTK_CLIST(tree), GTK_SORT_ASCENDING);
    gtk_clist_set_compare_func(GTK_CLIST(tree), NULL);

    if (!tree->display_content_info) {
	gtk_clist_set_column_visibility(GTK_CLIST(tree), 1, FALSE);
	gtk_clist_set_column_visibility(GTK_CLIST(tree), 2, FALSE);
	gtk_clist_column_titles_hide(GTK_CLIST(tree));
    } else gtk_clist_column_titles_show(GTK_CLIST(tree));

    gtk_signal_connect(GTK_OBJECT(tree), "tree_select_row",
		       GTK_SIGNAL_FUNC(select_mailbox), (gpointer) NULL);

    gtk_signal_connect(GTK_OBJECT(tree),
		       "button_press_event",
		       (GtkSignalFunc) button_event_press_cb,
		       (gpointer) NULL);

    gtk_signal_connect(GTK_OBJECT(tree),
		       "resize_column",
		       GTK_SIGNAL_FUNC(balsa_mblist_column_resize),
		       (gpointer) NULL);

    gtk_signal_connect(GTK_OBJECT(tree),
		       "click_column",
		       GTK_SIGNAL_FUNC(balsa_mblist_column_click),
		       (gpointer) tree);

    gtk_drag_dest_set (GTK_WIDGET (tree), GTK_DEST_DEFAULT_ALL,
                       mblist_drop_types,
                       ELEMENTS(mblist_drop_types),
                       GDK_ACTION_DEFAULT | GDK_ACTION_COPY | GDK_ACTION_MOVE);
    gtk_signal_connect (GTK_OBJECT (tree),"drag-data-received",
                        GTK_SIGNAL_FUNC (mblist_drag_cb), NULL);
    balsa_mblist_repopulate(tree);
}

/* balsa_mblist_insert_mailbox 
 *   
 * mblist:  The mailbox list where the mailbox is to be inserted
 * mailbox:  The mailbox to be inserted
 * icon:  The icon to be shown next to the mailbox name
 *
 * Description: This function is for inserting one of the several main
 * mailboxes.  It get's inserted under the main node, and a mailbox
 * watcher is added to the mailbox.
 * */
static void
balsa_mblist_insert_mailbox(BalsaMBList * mblist,
			    LibBalsaMailbox * mailbox, BalsaIconName icon)
{
    GtkCTreeNode *ctnode;
    MailboxNode *mbnode;
    gchar *text[3];

    g_return_if_fail(mblist  != NULL);
    g_return_if_fail(mailbox != NULL);

    text[0] = mailbox->name;
    text[1] = "";
    text[2] = "";

    gtk_signal_connect(GTK_OBJECT(mailbox), "set-unread-messages-flag",
		       GTK_SIGNAL_FUNC
		       (balsa_mblist_unread_messages_changed_cb),
		       (gpointer) mblist);

    ctnode = gtk_ctree_insert_node(GTK_CTREE(mblist),
				   NULL, NULL, text, 4,
				   balsa_icon_get_pixmap(icon),
				   balsa_icon_get_bitmap(icon),
				   NULL, NULL, TRUE, FALSE);
    mbnode = mailbox_node_new(mailbox->name, mailbox, FALSE);
    gtk_ctree_node_set_row_data_full(GTK_CTREE(mblist), ctnode, mbnode,
				     (GtkDestroyNotify)
				     mailbox_node_destroy);

}

/* balsa_mblist_disconnect_mailbox_signals
 *
 * Remove the signals we attached to the mailboxes.
 */
static void
balsa_mblist_disconnect_mailbox_signals(GtkCTree * tree,
					GtkCTreeNode * node, gpointer data)
{
    MailboxNode *mbnode = gtk_ctree_node_get_row_data(tree, node);

    if (mbnode->mailbox) {
	gtk_signal_disconnect_by_func(GTK_OBJECT(mbnode->mailbox),
				      balsa_mblist_unread_messages_changed_cb,
				      (gpointer) tree);
    }
}

/* balsa_mblist_repopulate 
 *
 * bmbl:  the BalsaMBList that needs recreating.
 *
 * Description: Called whenever a new mailbox is added to the mailbox
 * list, it clears the ctree, and draws all the entire tree from
 * scratch.
 * */
void
balsa_mblist_repopulate(BalsaMBList * bmbl)
{
    GtkCTree *ctree;

    g_return_if_fail(BALSA_IS_MBLIST(bmbl));

    ctree = GTK_CTREE(bmbl);

    gtk_ctree_post_recursive(GTK_CTREE(bmbl), NULL,
			     balsa_mblist_disconnect_mailbox_signals,
			     NULL);

    gtk_clist_freeze(GTK_CLIST(ctree));
    gtk_clist_clear(GTK_CLIST(ctree));

    if (bmbl->display_content_info) {
	gtk_clist_column_titles_show(GTK_CLIST(ctree));
	gtk_clist_set_column_visibility(GTK_CLIST(ctree), 1, TRUE);
	gtk_clist_set_column_visibility(GTK_CLIST(ctree), 2, TRUE);
    } else {
	gtk_clist_column_titles_hide(GTK_CLIST(ctree));
	gtk_clist_set_column_visibility(GTK_CLIST(ctree), 1, FALSE);
	gtk_clist_set_column_visibility(GTK_CLIST(ctree), 2, FALSE);
    }

    balsa_mblist_insert_mailbox(bmbl, balsa_app.inbox, BALSA_ICON_INBOX);
    balsa_mblist_insert_mailbox(bmbl, balsa_app.outbox, BALSA_ICON_OUTBOX);
    balsa_mblist_insert_mailbox(bmbl, balsa_app.sentbox,
				BALSA_ICON_TRAY_EMPTY);
    balsa_mblist_insert_mailbox(bmbl, balsa_app.draftbox,
				BALSA_ICON_TRAY_EMPTY);
    balsa_mblist_insert_mailbox(bmbl, balsa_app.trash, BALSA_ICON_TRASH);

    if (balsa_app.mailbox_nodes) {
	GNode *walk;
	GtkCTreeNode *node;

	walk = g_node_last_child(balsa_app.mailbox_nodes);
	for(walk = g_node_last_child(balsa_app.mailbox_nodes);
	    walk; walk = walk->prev) {
	    node =
		gtk_ctree_insert_gnode(ctree, NULL, NULL, walk,
				       mailbox_nodes_to_ctree, NULL);

	    if (node) {
		gtk_ctree_node_set_text(ctree, node, 1, "");
		gtk_ctree_node_set_text(ctree, node, 2, "");
	    }
	}
    }
    gtk_ctree_sort_recursive(ctree, NULL);
    balsa_mblist_have_new(bmbl);
    gtk_clist_thaw(GTK_CLIST(ctree));
}

/* mailbox_nodes_to_ctree
 * 
 * Description: For adding GNodes to the mailbox list, also does
 * checking for new messages.
 * */
static gboolean
mailbox_nodes_to_ctree(GtkCTree * ctree, guint depth, GNode * gnode,
		       GtkCTreeNode * cnode, gpointer data)
{
    MailboxNode *mbnode = NULL;
    g_return_val_if_fail(gnode, FALSE);

    if ( (mbnode = gnode->data) == NULL) return FALSE;

    if (mbnode->mailbox) {
	mbnode->IsDir = FALSE;
	
	if (LIBBALSA_IS_MAILBOX_POP3(mbnode->mailbox))
	    return FALSE;
	else {
	    BalsaIconName in = (mbnode->mailbox->new_messages > 0)
		? BALSA_ICON_TRAY_FULL : BALSA_ICON_TRAY_EMPTY;
	    gtk_ctree_set_node_info(ctree, cnode,
				    mbnode->mailbox->name, 5,
				    balsa_icon_get_pixmap(in),
				    balsa_icon_get_bitmap(in),
				    NULL, NULL,
				    G_NODE_IS_LEAF(gnode), FALSE);
	}
    
	gtk_ctree_node_set_row_data(ctree, cnode, mbnode);
	gtk_signal_connect(GTK_OBJECT(mbnode->mailbox),
			   "set-unread-messages-flag",
			   GTK_SIGNAL_FUNC
			   (balsa_mblist_unread_messages_changed_cb),
			   (gpointer) ctree);
    }


    if (mbnode->name && !mbnode->mailbox) {
	/* new directory, but not a mailbox */
	gtk_ctree_set_node_info(ctree, cnode, g_basename(mbnode->name), 5,
				balsa_icon_get_pixmap
				(BALSA_ICON_DIR_CLOSED),
				balsa_icon_get_bitmap
				(BALSA_ICON_DIR_CLOSED),
				balsa_icon_get_pixmap(BALSA_ICON_DIR_OPEN),
				balsa_icon_get_bitmap(BALSA_ICON_DIR_OPEN),
				G_NODE_IS_LEAF(gnode), mbnode->expanded);
	/* Make sure this gets set */
	mbnode->IsDir = TRUE;
	gtk_ctree_node_set_row_data(ctree, cnode, mbnode);
	gtk_ctree_node_set_selectable(ctree, cnode, FALSE);
    }
    return TRUE;
}

static void
button_event_press_cb(GtkCTree * ctree, GdkEventButton * event,
		      gpointer user_data)
{
    gint row, column;
    GtkCTreeNode *ctrow;
    MailboxNode *mbnode;

    if (!event || event->button != 3)
	return;

    if (event->button == 1 && event->type == GDK_2BUTTON_PRESS) {
	gtk_clist_get_selection_info(GTK_CLIST(ctree), event->x, event->y,
				     &row, &column);
	ctrow = gtk_ctree_node_nth(ctree, row);
	mbnode = gtk_ctree_node_get_row_data(ctree, ctrow);

	gtk_ctree_select(ctree, ctrow);

	if (mbnode->IsDir || !LIBBALSA_IS_MAILBOX(mbnode->mailbox))
	    return;

	if (mbnode->mailbox)
	    gtk_signal_emit(GTK_OBJECT(BALSA_MBLIST(ctree)),
			    balsa_mblist_signals[SELECT_MAILBOX],
			    mbnode->mailbox, ctrow, event);
    }
}

/* select_mailbox
 *
 * This function is called when the user clicks on the mailbox list,
 * propogates the select mailbox signal on to the mailboxes.
 * */
static void
select_mailbox(GtkCTree * ctree, GtkCTreeNode * row, gint column)
{
    BalsaMBList *bmbl;
    GdkEventButton *bevent = (GdkEventButton *) gtk_get_current_event();
    MailboxNode *mbnode;

    bmbl = BALSA_MBLIST(ctree);

    mbnode = gtk_ctree_node_get_row_data(ctree, row);

    g_return_if_fail(mbnode != NULL);
    if (mbnode->IsDir || !LIBBALSA_IS_MAILBOX(mbnode->mailbox))
	return;

    if (bevent && bevent->button == 1) {
	if (mbnode->mailbox)
	    gtk_signal_emit(GTK_OBJECT(bmbl),
			    balsa_mblist_signals[SELECT_MAILBOX],
			    mbnode->mailbox, row, bevent);
    }
}

/* mailbox_tree_expand
 * mailbox_tree_collapse
 *
 * These are callbacks that sets the expanded flag on the mailbox node, we use
 * this to save whether the folder was expanded or collapsed between
 * sessions. 
 * */
static void
mailbox_tree_expand(GtkCTree * ctree, GList * node, gpointer data)
{
    MailboxNode *mbnode;

    mbnode = gtk_ctree_node_get_row_data(ctree, GTK_CTREE_NODE(node));
    mbnode->expanded = TRUE;
}

static void
mailbox_tree_collapse(GtkCTree * ctree, GList * node, gpointer data)
{
    MailboxNode *mbnode;

    mbnode = gtk_ctree_node_get_row_data(ctree, GTK_CTREE_NODE(node));
    mbnode->expanded = FALSE;
}

/* balsa_mblist_column_resize [MBG]
 *
 * clist: The clist (in this case ctree), that is having it's columns resized.
 * column: The column being resized
 * size:  The new size of the column
 * data:  The data passed on to the callback when it was connected (NULL)
 *
 * Description: This callback assigns the new column widths to the balsa_app,
 * so they can be saved and restored between sessions.
 * */
static void
balsa_mblist_column_resize(GtkCList * clist, gint column,
			   gint size, gpointer data)
{
    switch (column) {
    case 0:
	balsa_app.mblist_name_width = size;
	break;
    case 1:
	balsa_app.mblist_newmsg_width = size;
	break;
    case 2:
	balsa_app.mblist_totalmsg_width = size;
	break;
    default:
	if (balsa_app.debug)
	    fprintf(stderr, "** Error: Unknown column resize\n");
    }
}

/* balsa_mblist_column_click [MBG]
 *  
 *  Description: This causes the columns to be sorted depending on
 *  which columns are clicked.  If a column is already selected as the
 *  sorting column when clicked, the order of sorting is changed.
 * */
static void
balsa_mblist_column_click(GtkCList * clist, gint column, gpointer data)
{
    if (clist->sort_column == column) {
	if (clist->sort_type == GTK_SORT_DESCENDING) {
	    gtk_clist_set_sort_type(clist, GTK_SORT_ASCENDING);
	} else {
	    gtk_clist_set_sort_type(clist, GTK_SORT_DESCENDING);
	}
    } else {
	gtk_clist_set_sort_column(clist, column);
	gtk_clist_set_sort_type(clist, GTK_SORT_ASCENDING);
    }

    if (column == 0)
	gtk_clist_set_compare_func(clist, NULL);
    else {
	gtk_clist_set_compare_func(clist, numeric_compare);
    }

    gtk_ctree_sort_recursive(GTK_CTREE(data), NULL);
}

/* balsa_mblist_set_style [MBG]
 * 
 * mblist:  The mailbox list to set the style for
 * 
 * Description: This is simply a function that sets the style for the
 * unread mailboxes and folders in a mailbox list.  It uses the colour
 * stored in balsa_app, and sets the font to bold.  It should be
 * called before calling balsa_mblist_have_unread,
 * balsa_mblist_update_mailbox, or any function that uses
 * balsa_mblist_mailbox_style, or folder_style.
 * */
static void
balsa_mblist_set_style(BalsaMBList * mblist)
{
    GdkColor color;
    GdkFont *font;
    GtkStyle *style;

    g_return_if_fail(mblist);

    /* Get the base style the user is using */
    style = gtk_style_copy(gtk_widget_get_style(GTK_WIDGET(mblist)));

    /* Attempt to set the font to bold */
    font = balsa_widget_get_bold_font(GTK_WIDGET(mblist));
    gdk_font_unref(style->font);
    style->font = font;		/*Now refed in get_bold_font */

    /* Get and attempt to allocate the colour */
    color = balsa_app.mblist_unread_color;

    /* colormap = gdk_window_get_colormap (GTK_WIDGET (&balsa_app.main_window->window)->window); */
    if (!gdk_colormap_alloc_color(balsa_app.colormap, &color, FALSE, TRUE)) {
	fprintf(stderr,
		"Couldn't allocate colour for unread mailboxes!\n");
	gdk_color_black(balsa_app.colormap, &color);
    }

    /* Just put it in the normal state for now */
    style->fg[GTK_STATE_NORMAL] = color;

    /* Unref the old style (if it's already been set, and point to the
     * new style */
    if (mblist->unread_mailbox_style != NULL)
	gtk_style_unref(mblist->unread_mailbox_style);
    mblist->unread_mailbox_style = style;
}


void
mblist_default_signal_bindings(BalsaMBList * tree)
{
    gtk_signal_connect(GTK_OBJECT(tree), "button_press_event",
		       GTK_SIGNAL_FUNC(mblist_button_press_cb), NULL);
    gtk_signal_connect(GTK_OBJECT(tree), "key_press_event",
		       GTK_SIGNAL_FUNC(mblist_key_press_cb), NULL);
}

/* balsa_mblist_have_new [MBG]
 * 
 * bmbl:  The BalsaMBList that you want to check
 * 
 * Description: This function is a wrapper for the recursive call
 * neccessary to check which mailboxes have unread messages, and
 * change the style of them in the mailbox list.
 * Since the mailbox checking can take some time, we implement it as an 
 * idle function.
 * */
void
balsa_mblist_have_new(BalsaMBList * bmbl)
{
    g_return_if_fail(bmbl != NULL);
    balsa_mblist_set_style(bmbl);

    gtk_clist_freeze(GTK_CLIST(bmbl));

    /* Update the folder styles based on the mailbox styles */
    gtk_ctree_post_recursive(GTK_CTREE(bmbl), NULL,
			     balsa_mblist_folder_style, NULL);
    gtk_clist_thaw(GTK_CLIST(bmbl));

}

/* balsa_mblist_update_mailbox [MBG]
 * 
 * mblist: the mailbox list that contains the mailbox
 * mbnode:  the mailbox node that you wish to update
 * 
 * Description: the function looks for the mailbox in the mblist, if
 * it's there it changes the style (and fills the info columns)
 * depending on the mailbox variables unread_messages and
 * total_messages. Selects the mailbox (important when previous mailbox
 * was closed).
 * */
void
balsa_mblist_update_mailbox(BalsaMBList * mblist,
			    LibBalsaMailbox * mailbox)
{
    GtkCTreeNode *node;
    gchar *desc;

    g_return_if_fail(mblist);

    /* try and find the mailbox in both sub trees */
    node = gtk_ctree_find_by_row_data_custom(GTK_CTREE(mblist), NULL,
					     mailbox,
					     mblist_mbnode_compare);

    if (node == NULL)
	return;

    /* Set the style for the next batch of formatting */
    balsa_mblist_set_style(mblist);

    /* We want to freeze here to speed things up and prevent ugly
     * flickering */
    gtk_clist_freeze(GTK_CLIST(mblist));
    balsa_mblist_mailbox_style(GTK_CTREE(mblist),
			       node,
			       gtk_ctree_node_get_row_data(GTK_CTREE
							   (mblist),
							   node));

    /* Do the folder styles as well */
    gtk_ctree_post_recursive(GTK_CTREE(mblist), NULL,
			     balsa_mblist_folder_style, NULL);

    gtk_clist_thaw(GTK_CLIST(mblist));

    desc =
	g_strdup_printf(_("Shown mailbox: %s with %ld messages, %ld new"),
			mailbox->name, mailbox->total_messages,
			mailbox->unread_messages);

    gnome_appbar_set_default(balsa_app.appbar, desc);
    g_free(desc);
}

/* balsa_mblist_mailbox_style [MBG]
 * 
 * ctree:  The ctree containing the mailbox
 * node:  The ctreenode that is associated with the mailbox
 * mailbox:  The mailbox that is to have it's style changed
 * display_info:  whether or not to display the columns
 * 
 * Description: A function to actually do the changing of the style,
 * and is called by both balsa_mblist_update_mailbox, and
 * balsa_mblist_check_new, hence the (slightly) strange arguments.
 *
 * NOTES: ignore special mailboxes.
 * */
static void
balsa_mblist_mailbox_style(GtkCTree * ctree, GtkCTreeNode * node,
			   MailboxNode * mbnode)
{
    BalsaMBList *mblist;
    LibBalsaMailbox *mailbox;
    GtkStyle *style;
    BalsaIconName icon;
    gboolean tmp_is_leaf, tmp_expanded;
    gchar *text;

    if (node == NULL)
	return;

    mblist = BALSA_MBLIST(ctree);
    mailbox = mbnode->mailbox;

    if (mailbox == balsa_app.sentbox || mailbox == balsa_app.outbox ||
	mailbox == balsa_app.draftbox || mailbox == balsa_app.trash)
	return;

    if (mailbox->has_unread_messages) {

	/* set the style of the unread maibox list, even if it's already 
	 * set... in case the user has changed the colour or font since the
	 * last style update */
	gtk_ctree_node_set_row_style(ctree, node,
				     mblist->unread_mailbox_style);

	tmp_is_leaf = GTK_CTREE_ROW(node)->is_leaf;
	tmp_expanded = GTK_CTREE_ROW(node)->expanded;

	if (mailbox == balsa_app.trash)
	    icon = BALSA_ICON_TRASH;
	else
	    icon = BALSA_ICON_TRAY_FULL;

	gtk_ctree_set_node_info(ctree, node, mailbox->name, 5,
				balsa_icon_get_pixmap(icon),
				balsa_icon_get_bitmap(icon),
				NULL, NULL, tmp_is_leaf, tmp_expanded);

	mbnode->style |= MBNODE_STYLE_ICONFULL;


	/* If we have a count of the unread messages, and we are showing
	 * columns, put the number in the unread column */
	if (mblist->display_content_info && mailbox->has_unread_messages
	    && mailbox->unread_messages > 0) {
	    text = g_strdup_printf("%ld", mailbox->unread_messages);
	    gtk_ctree_node_set_text(ctree, node, 1, text);
	    g_free(text);

	    mbnode->style |= MBNODE_STYLE_UNREAD_MESSAGES;
	}

    } else {
	/* If the clist entry currently has the unread messages icon, set
	 * it back, otherwise we can ignore this. */
	if (mbnode->style & MBNODE_STYLE_ICONFULL) {
	    style = gtk_widget_get_style(GTK_WIDGET(ctree));
	    gtk_ctree_node_set_row_style(ctree, node, style);
	    /* this unref is not needed, since the style is unref'd in */
	    /* gtk_ctree_node_set_row_style, and we have made no additional */
	    /* ref's of our own */
	    /*  gtk_style_unref (style); */

	    tmp_is_leaf = GTK_CTREE_ROW(node)->is_leaf;
	    tmp_expanded = GTK_CTREE_ROW(node)->expanded;

	    if (mailbox == balsa_app.inbox) {
		icon = BALSA_ICON_INBOX;
	    } else if (mailbox == balsa_app.outbox) {
		icon = BALSA_ICON_OUTBOX;
	    } else if (mailbox == balsa_app.trash) {
		icon = BALSA_ICON_TRASH;
	    } else {
		icon = BALSA_ICON_TRAY_EMPTY;
	    }

	    gtk_ctree_set_node_info(ctree, node, mailbox->name, 5,
				    balsa_icon_get_pixmap(icon),
				    balsa_icon_get_bitmap(icon),
				    NULL, NULL, tmp_is_leaf, tmp_expanded);

	    mbnode->style &= ~MBNODE_STYLE_ICONFULL;
	}

	/* If we're showing unread column info, get rid of whatever's
	 * there Also set the flag */
	if (mblist->display_content_info) {
	    gtk_ctree_node_set_text(ctree, node, 1, "");
	    mbnode->style &= ~MBNODE_STYLE_UNREAD_MESSAGES;
	}
    }

    /* We only want to do this if the mailbox is open, otherwise leave
     * the message numbers untouched in the display */
    if (mblist->display_content_info && mailbox->open_ref
	&& mailbox->total_messages >= 0) {
	if (mailbox->total_messages > 0) {
	    text = g_strdup_printf("%ld", mailbox->total_messages);
	    gtk_ctree_node_set_text(ctree, node, 2, text);
	    g_free(text);

	    mbnode->style |= MBNODE_STYLE_TOTAL_MESSAGES;
	} else {
	    gtk_ctree_node_set_text(ctree, node, 2, "");
	    mbnode->style &= ~MBNODE_STYLE_TOTAL_MESSAGES;
	}
    }
}

/* balsa_mblist_folder_style [MBG]
 * 
 * Description: This is meant to be called as a post recursive
 * function after the mailbox list has been updated style wise to
 * reflect the presence of new messages.  This function will attempt
 * to change the style of any folders to appropriately reflect that of
 * it's sub mailboxes (i.e. if it has one mailbox with unread messages
 * it will be shown as bold.
 * */
static void
balsa_mblist_folder_style(GtkCTree * ctree, GtkCTreeNode * node,
			  gpointer data)
{
    BalsaMBList *mblist;
    MailboxNode *mbnode;
    GtkCTreeNode *parent;
    GtkStyle *style;
    static guint32 has_unread;

    parent = GTK_CTREE_ROW(node)->parent;
    mbnode = gtk_ctree_node_get_row_data(ctree, node);
    mblist = BALSA_MBLIST(ctree);

    /* If we're on a leaf, just see if it's displayed as unread */
    if (GTK_CTREE_ROW(node)->is_leaf && !(mbnode->IsDir)) {
	if (mbnode->style & MBNODE_STYLE_ICONFULL)
	    has_unread |= 1 << (GTK_CTREE_ROW(node)->level);
	return;

    } else {

	if (!mbnode->IsDir)
	    return;

	/* We're on a folder here, see if any of the leaves were displayed
	 * as having unread messages, change the style accordingly */
	if (has_unread & (1 << (GTK_CTREE_ROW(node)->level + 1))) {

	    gtk_ctree_node_set_row_style(ctree, node,
					 mblist->unread_mailbox_style);

	    mbnode->style |= MBNODE_STYLE_ICONFULL;

	    /* If we've reached the top of the tree, reset the counter for
	     * the next branch */
	    if (parent == NULL) {
		has_unread = 0;
	    } else {
		has_unread |= (1 << (GTK_CTREE_ROW(node)->level));
		has_unread &= ~(1 << (GTK_CTREE_ROW(node)->level + 1));
	    }
	    return;

	} else if (mbnode->style & MBNODE_STYLE_ICONFULL) {
	    /* This folder's style needs to be reset to the vanilla style */
	    style = gtk_widget_get_style(GTK_WIDGET(ctree));
	    gtk_ctree_node_set_row_style(ctree, node, style);
	    /* this unref is not needed, since the style is unref'd in */
	    /* gtk_ctree_node_set_row_style, and we have made no additional */
	    /* ref's of our own */
	    /*  gtk_style_unref (style); */

	    mbnode->style &= ~MBNODE_STYLE_ICONFULL;
	    return;
	}
    }
}

/* numeric_compare [MBG]
 * 
 * Description: this is for sorting mailboxes by number of unread or
 * total messages.
 * */
static gint
numeric_compare(GtkCList * clist, gconstpointer ptr1, gconstpointer ptr2)
{
    MailboxNode *mb1;
    MailboxNode *mb2;
    LibBalsaMailbox *m1;
    LibBalsaMailbox *m2;
    glong t1, t2;

    GtkCListRow *row1 = (GtkCListRow *) ptr1;
    GtkCListRow *row2 = (GtkCListRow *) ptr2;

    mb1 = row1->data;
    mb2 = row2->data;

    m1 = mb1->mailbox;
    m2 = mb2->mailbox;

    if (!m1 || !m2)
	return 0;

    if (clist->sort_column == 1) {
	t1 = m1->unread_messages;
	t2 = m2->unread_messages;
	if (t1 < t2)
	    return 1;
	if (t1 > t2)
	    return -1;
    } else if (clist->sort_column == 2) {
	t1 = m1->total_messages;
	t2 = m2->total_messages;
	if (t1 < t2)
	    return 1;
	if (t1 > t2)
	    return -1;
    }

    return 0;
}

/* mblist_mbnode_compare [MBG]
 *   (GtkCTreeCompareFunc)
 * 
 * Description: This is for finding a mailbox within a mailbox list.
 * */
static gint
mblist_mbnode_compare(gconstpointer a, gconstpointer b)
{
    MailboxNode *mbnode = (MailboxNode *) a;
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(b);

    if (mailbox == mbnode->mailbox)
	return 0;
    else
	return 1;
}

gboolean
balsa_mblist_focus_mailbox(BalsaMBList * bmbl, LibBalsaMailbox * mailbox)
{
    GtkCTreeNode *node;

    g_return_val_if_fail(bmbl, FALSE);
    if (!mailbox)
	return FALSE;

    node = gtk_ctree_find_by_row_data_custom(GTK_CTREE(&bmbl->ctree), NULL,
					     mailbox,
					     mblist_mbnode_compare);
    if (node != NULL) {
	gtk_ctree_select(GTK_CTREE(&bmbl->ctree), node);

	/* moving selection to the respective mailbox.
	   this is neccessary when the previous mailbox was closed and
	   redundant if the mailboxes were switched (notebook_switch_page)
	   or the mailbox is checked for the new mail arrival
	 */

	if (gtk_ctree_node_is_visible(GTK_CTREE(&bmbl->ctree), node) !=
	    GTK_VISIBILITY_FULL) {
	    gtk_ctree_node_moveto(GTK_CTREE(&bmbl->ctree), node, 0, 1.0,
				  0.0);
	}

	return TRUE;
    } else
	return FALSE;
}

/* balsa_widget_get_bold_font [MBG]
 * 
 * Description: This function takes a widget and returns a bold
 * version of font that it is currently using.  If it fails, it simply
 * returns the default font of the widget.  
 * This function references the fonts now (change of behavior).
 * */
GdkFont *
balsa_widget_get_bold_font(GtkWidget * widget)
{
    gchar *new_xlfd;
    gchar *old_xlfd;
    gchar **temp_xlfd;
    GdkFont *font;
    GtkStyle *style;
    GSList *list;
    gint i = 0;

    style = gtk_widget_get_style(widget);
    font = style->font;

    /* Get the current font XLFD */
    list = ((GdkFontPrivate *) font)->names;
    old_xlfd = (gchar *) list->data;

    /* Split the XLFD into it's components */
    temp_xlfd = g_strsplit(old_xlfd, "-", 14);
    while (i < 4 && temp_xlfd[i])
	i++;
    if (i > 3) {
	/* Change the weight to bold */
	g_free(temp_xlfd[3]);
	temp_xlfd[3] = g_strdup("bold");

	/* Reassemble the XLFD */
	new_xlfd = g_strjoinv("-", temp_xlfd);
	g_strfreev(temp_xlfd);

	/* Try to load it, if it doesn't succeed, re-load the old font */
	font = gdk_font_load(new_xlfd);
	g_free(new_xlfd);
    } else
	font = NULL;

    if (font == NULL) {
	font = gdk_font_load(old_xlfd);

	if (font == NULL) {
	    font = style->font;
	}
    }

    gdk_font_ref(font);
    return font;
}

static void
balsa_mblist_unread_messages_changed_cb(LibBalsaMailbox * mailbox,
					gboolean flag,
					BalsaMBList * mblist)
{
    GtkCTreeNode *node;

    g_return_if_fail(mblist);
    g_return_if_fail(mailbox);
    /* try and find the mailbox in both sub trees */
    node = gtk_ctree_find_by_row_data_custom(GTK_CTREE(mblist), NULL,
					     mailbox,
					     mblist_mbnode_compare);

    if (node == NULL)
	return;

    /* We want to freeze here to speed things up and prevent ugly
     * flickering */
    gtk_clist_freeze(GTK_CLIST(mblist));
    balsa_mblist_mailbox_style(GTK_CTREE(mblist),
			       node,
			       gtk_ctree_node_get_row_data(GTK_CTREE
							   (mblist),
							   node));
    gtk_clist_thaw(GTK_CLIST(mblist));

}


/* mblist_drag_cb
 * 
 * Description: This is the drag_data_recieved signal handler for the
 * BalsaMBList.  It retrieves a list of messages terminated by a NULL
 * pointer, converts it to a GList, then transfers it to the selected
 * mailbox.  Depending on what key is held down when the message(s)
 * are dropped they are either copied or moved.  The default action is
 * to copy.
 * */
static void
mblist_drag_cb (GtkWidget* widget, GdkDragContext* context, 
                gint x, gint y, GtkSelectionData* selection_data, 
                guint info, guint32 time, gpointer data)
{
    BalsaMBList* bmbl;
    GtkCTree* ctree;
    GtkCTreeNode* node;
    LibBalsaMailbox* mailbox;
    LibBalsaMailbox* orig_mailbox;
    GList* messages = NULL;
    MailboxNode* mbnode;
    gint row, column;
    gint i = 0;
    LibBalsaMessage** message_array;


    bmbl = BALSA_MBLIST (widget);
    ctree = &bmbl->ctree;
    message_array = (LibBalsaMessage**) selection_data->data;

    /* convert pointer array to GList */
    while (message_array[i] != NULL) {
        messages = g_list_append (messages, message_array[i]);
        i++;
    }
    
    orig_mailbox = ((LibBalsaMessage*) messages->data)->mailbox;

    /* find the node and mailbox */
    if (gtk_clist_get_selection_info (GTK_CLIST (ctree), x, y, 
                                      &row, &column)) {
        node = gtk_ctree_node_nth (ctree, row - 1);
        mbnode = gtk_ctree_node_get_row_data (ctree, node);

        /* cannot transfer to a directory */
        if (!mbnode->IsDir) {
            mailbox = mbnode->mailbox;
        } else {
            g_list_free (messages);
            return;
        }

        /* cannot transfer to the originating mailbox */
        if (mailbox != NULL && mailbox != orig_mailbox) {
            switch (context->suggested_action) {
            case GDK_ACTION_MOVE:
                libbalsa_messages_move (messages, mailbox);
                context->action = context->suggested_action;
                break;

            case GDK_ACTION_DEFAULT:
            case GDK_ACTION_COPY:
            default:
                libbalsa_messages_copy (messages, mailbox);
                context->action = context->suggested_action;
                break;
            }
            
            libbalsa_mailbox_commit_changes (orig_mailbox);
        }
    }

    g_list_free (messages);
}
