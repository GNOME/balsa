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

#include <gnome.h>

#include "config.h"
#include "identity.h"

#define gnome_stock_button_with_label(p,l) gtk_button_new_with_label(l)

static GtkObjectClass* parent_class;
static const gchar* default_ident_name = N_("Default");
static const guint padding = 4;

static void libbalsa_identity_class_init(LibBalsaIdentityClass* klass);
static void libbalsa_identity_init(LibBalsaIdentity* ident);

static void libbalsa_identity_destroy(GtkObject* object);

static void new_ident_cb(GtkButton* , gpointer);
static void edit_ident_cb(GtkButton* , gpointer);
static void set_current_ident_cb(GtkButton* , gpointer);
static void delete_ident_cb(GtkWidget* , gpointer);
static void delete_confirm_cb(gint reply, gpointer user_data);
static void identity_list_update(GtkCList* clist);
static void config_frame_button_select_cb(GtkCList* clist, 
                                          gint row, 
                                          gint column, 
                                          GdkEventButton* event, 
                                          gpointer user_data);
static void config_frame_button_unselect_cb(GtkCList* clist, 
                                            gint row, 
                                            gint column, 
                                            GdkEventButton* event, 
                                            gpointer user_data);

static GtkWidget* setup_ident_dialog(GtkWindow* parent, gboolean createp, 
				     LibBalsaIdentity*, 
				     GtkSignalFunc, gpointer);
static void ident_dialog_add_cb(GtkButton*, gpointer);
static void ident_dialog_edit_cb(GtkButton*, gpointer);
static void ident_dialog_cancel_cb(GtkButton*, gpointer);
static int ident_dialog_close_cb(GnomeDialog*, gpointer);
static void ident_dialog_add_checkbutton(GnomeDialog*, const gchar*, 
                                         const gchar*, gboolean);
static GtkWidget* ident_dialog_add_entry(GnomeDialog*, const gchar*,
					 const gchar*, gchar*);
static gchar* ident_dialog_get_text(GnomeDialog*, const gchar*);
static gboolean ident_dialog_get_bool(GnomeDialog*, const gchar*);
static gboolean ident_dialog_update(GnomeDialog*);
static void config_dialog_ok_cb(GtkButton* button, gpointer user_data);
static void config_dialog_select_cb(GtkCList* clist, gint row, gint column,
                                    GdkEventButton* event, gpointer user_data);

static GtkWidget* libbalsa_identity_display_frame(void);
static void  display_frame_add_field(GtkFrame* frame, GtkBox* box, 
                                     const gchar* name, const gchar* key);
static void display_frame_add_boolean(GtkFrame* frame, GtkBox* box,
                                      const gchar* name, const gchar* key);
static void display_frame_update(GtkFrame* frame, LibBalsaIdentity* ident);
static void display_frame_set_field(GtkFrame* frame, const gchar* key, 
                                    const gchar* value);
static void display_frame_set_boolean(GtkFrame* frame, const gchar* key, 
                                      gboolean value);


static void select_dialog_row_cb(GtkCList* clist, 
                                 gint row, 
                                 gint column, 
                                 GdkEventButton* event, 
                                 gpointer user_data);





GtkType
libbalsa_identity_get_type()
{
    static GtkType libbalsa_identity_type = 0;
    
    if (!libbalsa_identity_type) {
        static const GtkTypeInfo libbalsa_identity_info = 
            {
                "LibBalsaIdentity",
                sizeof(LibBalsaIdentity),
                sizeof(LibBalsaIdentityClass),
                (GtkClassInitFunc) libbalsa_identity_class_init,
                (GtkObjectInitFunc) libbalsa_identity_init,
                (GtkArgSetFunc) NULL,
                (GtkArgGetFunc) NULL
            };
        
        libbalsa_identity_type = 
            gtk_type_unique(gtk_object_get_type(), &libbalsa_identity_info);
    }
    
    return libbalsa_identity_type;
}


static void
libbalsa_identity_class_init(LibBalsaIdentityClass* klass)
{
    GtkObjectClass* object_class;

    parent_class = gtk_type_class(gtk_object_get_type());

    object_class = GTK_OBJECT_CLASS(klass);
    object_class->destroy = libbalsa_identity_destroy;
}


/* 
 * Class inititialization function, set defaults for new objects
 */
static void
libbalsa_identity_init(LibBalsaIdentity* ident)
{
    ident->identity_name = NULL;
    ident->address = libbalsa_address_new();
    ident->replyto = NULL;
    ident->domain = NULL;
    ident->bcc = NULL;
    ident->reply_string = g_strdup(_("Re:"));
    ident->forward_string = g_strdup(_("Fwd:"));
    ident->signature_path = NULL;
    ident->sig_sending = TRUE;
    ident->sig_whenforward = TRUE;
    ident->sig_whenreply = TRUE;
    ident->sig_separator = TRUE;
    ident->sig_prepend = FALSE;
}


/* 
 * Create a new object with the identity name "Default".  Does not add
 * it to the list of identities for the application.
 */
GtkObject* 
libbalsa_identity_new(void) 
{
    LibBalsaIdentity* ident;
    
    ident = LIBBALSA_IDENTITY(gtk_type_new(LIBBALSA_TYPE_IDENTITY));
    ident->identity_name = g_strdup(default_ident_name);
    return GTK_OBJECT(ident);
}


/*
 * Create a new object with the specified identity name.  Does not add
 * it to the list of identities for the application.
 */
GtkObject*
libbalsa_identity_new_with_name(const gchar* ident_name)
{
    LibBalsaIdentity* ident;
    
    ident = LIBBALSA_IDENTITY(gtk_type_new(LIBBALSA_TYPE_IDENTITY));
    ident->identity_name = g_strdup(ident_name);
    return GTK_OBJECT(ident);
}


/* 
 * Destroy the object, freeing all the values in the process.
 */
static void
libbalsa_identity_destroy(GtkObject* object)
{
    LibBalsaIdentity* ident;
    
    g_return_if_fail(object != NULL);
    
    ident = LIBBALSA_IDENTITY(object);

    gtk_object_destroy(GTK_OBJECT(ident->address));
    g_free(ident->identity_name);
    g_free(ident->replyto);
    g_free(ident->domain);
    g_free(ident->bcc);
    g_free(ident->reply_string);
    g_free(ident->forward_string);
    g_free(ident->signature_path);

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (object);
}

void
libbalsa_identity_set_identity_name(LibBalsaIdentity* ident, const gchar* name)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->identity_name);
    ident->identity_name = g_strdup(name);
}


void
libbalsa_identity_set_address(LibBalsaIdentity* ident, LibBalsaAddress* ad)
{
    g_return_if_fail(ident != NULL);
    
    gtk_object_unref(GTK_OBJECT(ident->address));
    gtk_object_ref(GTK_OBJECT(ad));
    gtk_object_sink(GTK_OBJECT(ad));
    ident->address = ad;
}


void
libbalsa_identity_set_replyto(LibBalsaIdentity* ident, const gchar* address)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->replyto);
    ident->replyto = g_strdup(address);
}


void
libbalsa_identity_set_domain(LibBalsaIdentity* ident, const gchar* dom)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->domain);
    ident->domain = g_strdup(dom);
}


void 
libbalsa_identity_set_bcc(LibBalsaIdentity* ident, const gchar* bcc)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->bcc);
    ident->bcc = g_strdup(bcc);
}


void 
libbalsa_identity_set_reply_string(LibBalsaIdentity* ident, const gchar* reply)
{
    g_return_if_fail(ident != NULL);

    g_free(ident->reply_string);
    ident->reply_string = g_strdup(reply);
}


void 
libbalsa_identity_set_forward_string(LibBalsaIdentity* ident, const gchar* forward)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->forward_string);
    ident->forward_string = g_strdup(forward);
}


void
libbalsa_identity_set_signature_path(LibBalsaIdentity* ident, const gchar* path)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->signature_path);
    ident->signature_path = g_strdup(path);
}


void
libbalsa_identity_set_sig_sending(LibBalsaIdentity* ident, gboolean sig_sending)
{
    g_return_if_fail(ident != NULL);
    ident->sig_sending = sig_sending;
}


void
libbalsa_identity_set_sig_whenforward(LibBalsaIdentity* ident, gboolean forward)
{
    g_return_if_fail(ident != NULL);
    ident->sig_whenforward = forward;
}


void 
libbalsa_identity_set_sig_whenreply(LibBalsaIdentity* ident, gboolean reply)
{
    g_return_if_fail(ident != NULL);
    ident->sig_whenreply = reply;
}


void
libbalsa_identity_set_sig_separator(LibBalsaIdentity* ident, gboolean separator)
{
    g_return_if_fail(ident != NULL);
    ident->sig_separator = separator;
}


void 
libbalsa_identity_set_sig_prepend(LibBalsaIdentity* ident, gboolean prepend)
{
    g_return_if_fail(ident != NULL);
    ident->sig_prepend = prepend;
}


LibBalsaIdentity*
libbalsa_identity_select_dialog(GtkWindow* parent, const gchar* prompt,
				GList** identities, LibBalsaIdentity** current)
{
    LibBalsaIdentity* ident = NULL;
    GtkWidget* frame1 = gtk_frame_new(NULL);
    GtkWidget* clist = gtk_clist_new(2);
    GtkWidget* dialog;
    gint choice = 1;


    gtk_object_set_data(GTK_OBJECT(clist), "parent-window", parent);
    gtk_object_set_data(GTK_OBJECT(clist), "identities", identities);
    gtk_object_set_data(GTK_OBJECT(clist), "current-id", current);
    dialog = gnome_dialog_new(prompt, 
                              GNOME_STOCK_BUTTON_OK,
                              GNOME_STOCK_BUTTON_CANCEL,
                              NULL);

    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(dialog)->vbox), 
                       frame1, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(frame1), clist);
    gtk_container_set_border_width(GTK_CONTAINER(frame1), padding);
    gtk_widget_show_all(frame1);

    gtk_clist_column_titles_hide(GTK_CLIST(clist));
    gtk_clist_set_row_height(GTK_CLIST(clist), 0);
    gtk_clist_set_column_min_width(GTK_CLIST(clist), 0, 20);
    gtk_clist_set_column_min_width(GTK_CLIST(clist), 1, 200);
    gtk_clist_set_column_auto_resize(GTK_CLIST(clist), 1, TRUE);
    gtk_widget_set_usize(GTK_WIDGET(clist), 200, 200);
    gtk_clist_set_selection_mode(GTK_CLIST(clist), GTK_SELECTION_BROWSE);
    identity_list_update(GTK_CLIST(clist));

    gtk_signal_connect(GTK_OBJECT(clist), "select-row",
                       GTK_SIGNAL_FUNC(select_dialog_row_cb),
                       &ident);

    gnome_dialog_set_close(GNOME_DIALOG(dialog), TRUE);
    gnome_dialog_set_parent(GNOME_DIALOG(dialog), parent);
    gnome_dialog_set_default(GNOME_DIALOG(dialog), 1);
    
    choice = gnome_dialog_run(GNOME_DIALOG(dialog));
    
    if (choice == 1) /* what about cancel? */
        return NULL;
    
    return ident;
}


static void
select_dialog_row_cb(GtkCList* clist, 
                     gint row, 
                     gint column, 
                     GdkEventButton* event, 
                     gpointer user_data)
{
    LibBalsaIdentity** selected_ident = (LibBalsaIdentity**) user_data;
   
    *selected_ident = LIBBALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
}



/*
 * Create and return a frame containing a list of the identities in
 * the application and a number of buttons to edit, create, and delete
 * identities.  Also provides a way to set the current identity.
 */
GtkWidget* 
libbalsa_identity_config_frame(gboolean with_buttons, GList** identities,
			       LibBalsaIdentity** current)
{

    GtkWidget* config_frame = gtk_frame_new(NULL);
    GtkWidget* hbox = gtk_hbox_new(FALSE, padding);
    GtkWidget* vbox;
    GtkWidget* clist = gtk_clist_new(2);
    GtkWidget* buttons[4];
    gint i = 0;

    gtk_container_set_border_width(GTK_CONTAINER(config_frame), 0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), padding);
    gtk_container_add(GTK_CONTAINER(config_frame), hbox);
    gtk_box_pack_start(GTK_BOX(hbox), clist, TRUE, TRUE, 0);
    
    if (with_buttons) {
        vbox = gtk_vbox_new(FALSE, padding);
        gtk_box_pack_end(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

        buttons[0] = gnome_stock_button(GNOME_STOCK_PIXMAP_NEW);
        gtk_signal_connect(GTK_OBJECT(buttons[0]), "clicked",
                           GTK_SIGNAL_FUNC(new_ident_cb), clist);
        
        buttons[1] = 
	    gnome_stock_button_with_label(GNOME_STOCK_PIXMAP_PROPERTIES, 
					  _("Edit"));
        gtk_signal_connect(GTK_OBJECT(buttons[1]), "clicked",
                           GTK_SIGNAL_FUNC(edit_ident_cb), clist);
        
        buttons[2] = gnome_stock_button_with_label(GNOME_STOCK_PIXMAP_ADD, 
						   _("Set Current"));
        gtk_signal_connect(GTK_OBJECT(buttons[2]), "clicked",
                           GTK_SIGNAL_FUNC(set_current_ident_cb), clist);
        
        buttons[3] = gnome_stock_button(GNOME_STOCK_PIXMAP_REMOVE);
        gtk_signal_connect(GTK_OBJECT(buttons[3]), "clicked",
                           GTK_SIGNAL_FUNC(delete_ident_cb), clist);
        
        gtk_box_pack_start(GTK_BOX(vbox), buttons[0], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), buttons[1], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), buttons[2], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), buttons[3], FALSE, FALSE, 0);

        for (i = 1; i < 4; ++i) {
            gtk_signal_connect(GTK_OBJECT(clist), "select-row",
                               GTK_SIGNAL_FUNC(config_frame_button_select_cb),
                               buttons[i]);
            gtk_signal_connect(GTK_OBJECT(clist), "select_row",
                               GTK_SIGNAL_FUNC(config_frame_button_unselect_cb),
                               buttons[i]);
        }
    }

    gtk_clist_column_titles_hide(GTK_CLIST(clist));
    gtk_clist_set_row_height(GTK_CLIST(clist), 0);
    gtk_clist_set_column_min_width(GTK_CLIST(clist), 0, 20);
    gtk_clist_set_column_min_width(GTK_CLIST(clist), 1, 200);
    gtk_clist_set_column_auto_resize(GTK_CLIST(clist), 1, TRUE);
    gtk_widget_set_usize(GTK_WIDGET(clist), 200, 200);
    gtk_clist_set_selection_mode(GTK_CLIST(clist), GTK_SELECTION_BROWSE);

    gtk_object_set_data(GTK_OBJECT(clist), "identities", identities);
    gtk_object_set_data(GTK_OBJECT(clist), "current-id", current);
    identity_list_update(GTK_CLIST(clist));
    gtk_clist_select_row(GTK_CLIST(clist), 0, -1);
    gtk_object_set_data(GTK_OBJECT(config_frame), "clist", clist);

    return config_frame;
}

static void
identity_list_select_cb(GtkCList* clist, gint row, gint column, 
                        GdkEventButton* event, gpointer user_data)
{
    void* ident;
    GtkFrame* frame = GTK_FRAME(user_data);
    
    ident = LIBBALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    g_return_if_fail(ident);
    display_frame_update(frame, ident);
}



/* 
 * Update the list of identities in the config frame, displaying the
 * available identities in the application, and which is current.
 * we need to tempararily switch to selection mode single to avoid row 
 * autoselection when there is no data attached to it yet.
 */
static void
identity_list_update(GtkCList* clist)
{
    LibBalsaIdentity* ident;
    GdkPixmap* pixmap = NULL;
    GdkPixmap* bitmap = NULL;
    GList** identities, *list;
    LibBalsaIdentity** current;
    gchar* text[2];
    gint i = 0;

    text[0] = NULL;

    identities = gtk_object_get_data(GTK_OBJECT(clist), "identities");
    current    = gtk_object_get_data(GTK_OBJECT(clist), "current-id");

    gtk_clist_set_selection_mode(clist, GTK_SELECTION_SINGLE);
    gtk_clist_freeze(clist);
    gtk_clist_clear(clist);
    
    for (list = *identities; list; list = g_list_next(list)) {
        ident = LIBBALSA_IDENTITY(list->data);
        text[1] = ident->identity_name;
        i = gtk_clist_append(clist, text);
        gtk_clist_set_row_data(clist, i, ident);
        
        if (ident == *current) {
            /* do something to indicate it is the active style */
            gnome_stock_pixmap_gdk(GNOME_STOCK_MENU_FORWARD, 
                                   GNOME_STOCK_PIXMAP_REGULAR, 
                                   &pixmap, &bitmap);
            gtk_clist_set_pixmap(clist, i, 0, pixmap, bitmap);
            gtk_clist_select_row(clist, i, -1);
        } else {
            gnome_stock_pixmap_gdk(GNOME_STOCK_MENU_BLANK,
                                   GNOME_STOCK_PIXMAP_REGULAR,
                                   &pixmap, &bitmap);
            gtk_clist_set_pixmap(clist, i, 0, pixmap, bitmap);
        }

    }

    gtk_clist_set_selection_mode(clist, GTK_SELECTION_BROWSE);
    gtk_clist_sort(clist);
    gtk_clist_thaw(clist);
}


static void
config_frame_button_select_cb(GtkCList* clist, gint row, gint column,
                              GdkEventButton* event, gpointer user_data)
{
    GtkWidget* button = GTK_WIDGET(user_data);
    
    gtk_widget_set_sensitive(button, TRUE);
}


static void
config_frame_button_unselect_cb(GtkCList* clist, gint row, gint column,
                                GdkEventButton* event, gpointer user_data)
{
    GtkWidget* button = GTK_WIDGET(user_data);
    
    gtk_widget_set_sensitive(button, FALSE);
}




/*
 * Create a new identity dialog
 */
static void
new_ident_cb(GtkButton* button, gpointer user_data)
{
    LibBalsaIdentity* ident;
    GtkWidget* dialog;
    GtkWindow* parent;

    ident = LIBBALSA_IDENTITY(libbalsa_identity_new());
    parent = gtk_object_get_data(GTK_OBJECT(user_data), "parent-window");
    dialog = setup_ident_dialog(parent, TRUE, ident, ident_dialog_add_cb, 
				user_data);
    gnome_dialog_run(GNOME_DIALOG(dialog));
}

/*
 * Edit the selected identity in a new dialog.
 * there is always exactly one identity selected - _BROWSE selection mode.
 */
static void 
edit_ident_cb(GtkButton* button, gpointer user_data)
{
    GtkWidget* dialog;
    GtkWindow* parent;
    LibBalsaIdentity* ident;
    GtkCList* clist = GTK_CLIST(user_data);
    GList* list = clist->selection;
    gint row;

    g_return_if_fail(list);
    
    row = GPOINTER_TO_INT(list->data);
    ident = LIBBALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    parent = gtk_object_get_data(GTK_OBJECT(clist), "parent-window");
    dialog = setup_ident_dialog(parent, FALSE, ident, ident_dialog_edit_cb, 
				clist);
    gnome_dialog_run(GNOME_DIALOG(dialog));
}


/*
 * Put the required GtkEntries, Labels, and Checkbuttons in the dialog
 * for creating/editing identities.
 */
static GtkWidget*
setup_ident_dialog(GtkWindow* parent, 
		   gboolean createp, LibBalsaIdentity* ident, 
		   GtkSignalFunc ok_signal_cb, gpointer clist)
{
     
    GtkWidget* frame = gtk_frame_new(NULL);
    GtkWidget* vbox = gtk_vbox_new(FALSE, padding);
    GtkWidget* main_box, *w;
    GnomeDialog* dialog;
    dialog = GNOME_DIALOG(gnome_dialog_new(createp 
					   ? _("Create Identity")  
					   : _("Edit Identity"), 
					   createp 
					   ? GNOME_STOCK_BUTTON_OK 
					   : GNOME_STOCK_BUTTON_APPLY,
					   GNOME_STOCK_BUTTON_CANCEL,
					   NULL));
    main_box = dialog->vbox;
    gtk_object_set_data(GTK_OBJECT(dialog), "clist", clist);
    gtk_object_set_data(GTK_OBJECT(dialog), "identity", ident);
    gtk_object_set_data(GTK_OBJECT(dialog), "identity-edit", 
			(gpointer)!createp);
    gnome_dialog_button_connect(dialog, 0, ok_signal_cb, dialog);

    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gnome_dialog_set_close(dialog, FALSE);
    gnome_dialog_set_default(dialog, 1);
    gnome_dialog_set_parent(dialog, parent);
    
    gtk_box_pack_start(GTK_BOX(main_box), frame, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(frame), vbox);

    gtk_container_set_border_width(GTK_CONTAINER(frame), padding);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), padding);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), padding);
    gtk_object_set_data(GTK_OBJECT(dialog), "box", vbox);

    gnome_dialog_button_connect(dialog, 1, 
                                GTK_SIGNAL_FUNC(ident_dialog_cancel_cb),
                                dialog);

    gtk_signal_connect(GTK_OBJECT(dialog), "close", 
                       GTK_SIGNAL_FUNC(ident_dialog_close_cb),
                       dialog);

    w = ident_dialog_add_entry(dialog, _("Identity Name:"), 
			       "identity-name",
			       ident->identity_name);
    gtk_widget_grab_focus(w);

    ident_dialog_add_entry(dialog, _("Full Name:"), 
                           "identity-fullname",
                           ident->address->full_name);
    
    if (ident->address->address_list != NULL) {
        ident_dialog_add_entry(dialog, _("Mailing Address:"), 
                               "identity-address",
                               (gchar*) ident->address->address_list->data);
    } else {
        ident_dialog_add_entry(dialog, _("Mailing Address:"),
                               "identity-address",
                               NULL);
    }
    
    ident_dialog_add_entry(dialog, _("Reply To:"), 
                           "identity-replyto",
                           ident->replyto);
    ident_dialog_add_entry(dialog, _("Domain:"), 
                           "identity-domain",
                           ident->domain);
    ident_dialog_add_entry(dialog, _("Bcc:"), 
                           "identity-bcc",
                           ident->bcc);
    ident_dialog_add_entry(dialog, _("Reply String:"), 
                           "identity-replystring",
                           ident->reply_string);
    ident_dialog_add_entry(dialog, _("Forward String:"), 
                           "identity-forwardstring",
                           ident->forward_string);
    ident_dialog_add_entry(dialog, _("Signature Path:"), 
                           "identity-sigpath",
                           ident->signature_path);
    
    ident_dialog_add_checkbutton(dialog, _("Append Signature"), 
                                 "identity-sigappend", 
                                 ident->sig_sending);
    ident_dialog_add_checkbutton(dialog, 
                                 _("Append Signature When Forwarding"),
                                 "identity-whenforward", 
                                 ident->sig_whenforward);
    ident_dialog_add_checkbutton(dialog, _("Append Signature When Replying"),
                                 "identity-whenreply", 
                                 ident->sig_whenreply);
    ident_dialog_add_checkbutton(dialog, _("Include Signature Separator"),
                                 "identity-sigseparator", 
                                 ident->sig_separator);
    ident_dialog_add_checkbutton(dialog, _("Prepend Signature"),
                                 "identity-sigprepend",
                                 ident->sig_prepend);    
    gtk_widget_show_all(main_box);
    return GTK_WIDGET(dialog);
}


/*
 * Create and add a GtkCheckButton to the given dialog with caption
 * and add a pointer to it stored under the given key.  The check
 * button is initialized to the given value.
 */
static void
ident_dialog_add_checkbutton(GnomeDialog* dialog, const gchar* check_label,
                             const gchar* check_key, gboolean init_value)
{
    GtkWidget* check;
    GtkBox* box;

    
    box = GTK_BOX(gtk_object_get_data(GTK_OBJECT(dialog), "box"));
    check = gtk_check_button_new_with_label(check_label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), init_value);
    gtk_box_pack_start(box, check, FALSE, FALSE, 0);
    gtk_object_set_data(GTK_OBJECT(dialog), check_key, check);
    gtk_widget_show(check);
}


/*
 * Add a GtkEntry to the given dialog with a label next to it
 * explaining the contents.  A reference to the entry is stored as
 * object data attached to the dialog with the given key.  The entry
 * is initialized to the init_value given.
 */
static GtkWidget*
ident_dialog_add_entry(GnomeDialog* dialog, const gchar* label_name, 
                       const gchar* entry_key, gchar* init_value)
{
    GtkBox* box;
    GtkWidget* label;
    GtkWidget* entry;
    GtkWidget* hbox;


    box = GTK_BOX(gtk_object_get_data(GTK_OBJECT(dialog), "box"));
    label = gtk_label_new(label_name);
    entry = gtk_entry_new();
    hbox = gtk_hbox_new(FALSE, 4);
    
    if (init_value != NULL)
        gtk_entry_set_text(GTK_ENTRY(entry), init_value);

    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    gtk_box_pack_start(box, hbox, FALSE, FALSE, 4);
    gtk_object_set_data(GTK_OBJECT(dialog), entry_key, entry);
    gtk_widget_show(label);
    gtk_widget_show(entry);
    gtk_widget_show(hbox);
    return entry;
}


/*
 * Adds the identity to the application, then closes the edit dialog.
 */
static void
ident_dialog_add_cb(GtkButton* button, gpointer user_data)
{
    GnomeDialog* dialog = GNOME_DIALOG(user_data);
    GtkCList* clist;    
    LibBalsaIdentity* ident, **current;
    GList** identities;
    gint row;

    if(!ident_dialog_update(dialog))
        return;

    ident = gtk_object_get_data(GTK_OBJECT(dialog), "identity");
    clist = GTK_CLIST(gtk_object_get_data(GTK_OBJECT(dialog), "clist"));
    current = gtk_object_get_data(GTK_OBJECT(clist), "current-id");
    identities = gtk_object_get_data(GTK_OBJECT(clist), "identities");
    *identities = g_list_append(*identities, ident);
    gtk_object_set_data(GTK_OBJECT(dialog), "identity", NULL);

    identity_list_update(clist);

    row = gtk_clist_find_row_from_data(clist, ident);
    gtk_clist_select_row(clist, row, -1);
    
    gnome_dialog_close(dialog);
}


/*
 * Commits the edited changes of the identity to the application
 */
static void
ident_dialog_edit_cb(GtkButton* button, gpointer user_data)
{
    GnomeDialog* dialog = GNOME_DIALOG(user_data);
    GtkCList* clist;

    if(!ident_dialog_update(dialog))
	return;

    clist = GTK_CLIST(gtk_object_get_data(GTK_OBJECT(dialog), "clist"));
    identity_list_update(clist);

#if 0
    ident = gtk_object_get_data(GTK_OBJECT(dialog), "identity");
    row = gtk_clist_find_row_from_data(clist, ident);
    gtk_clist_select_row(clist, row, -1);
    gtk_object_set_data(GTK_OBJECT(dialog), "identity", NULL);
#endif
    gnome_dialog_close(dialog);
}


/* 
 * Cancels the creation or editing of an identity, closes the dialog.
 */
static void
ident_dialog_cancel_cb(GtkButton* button, gpointer user_data)
{
    ident_dialog_close_cb(GNOME_DIALOG(user_data), user_data);
    gnome_dialog_close(GNOME_DIALOG(user_data));
}


/*
 * The close callback for the editing/creation dialog.
 * if we create new identity and the dialog gets closed, we need to destroy
 * the half-created object.
 */
static int
ident_dialog_close_cb(GnomeDialog* dialog, gpointer user_data)
{
    gboolean edit;
    gpointer tmpptr;

    edit = (gboolean) gtk_object_get_data(GTK_OBJECT(dialog), 
                                          "identity-edit");
    if (!edit) {
        tmpptr = gtk_object_get_data(GTK_OBJECT(dialog), "identity");
        
        if (tmpptr != NULL) {
            gtk_object_destroy(GTK_OBJECT(tmpptr));
	    g_warning("Object destroyed.\n");
	}
    }

    return FALSE;
}


/* 
 * Update the identity object associated with the edit/new dialog,
 * validating along the way.  Correct validation results in a true
 * return value, otherwise it returns false.
 */
static gboolean
ident_dialog_update(GnomeDialog* dlg)
{
    LibBalsaIdentity* id;
    LibBalsaIdentity* exist_ident;
    LibBalsaAddress* address;
    GtkWidget* error, *clist;
    GList **identities, *list;
    gchar* text;
    
    id = gtk_object_get_data(GTK_OBJECT(dlg), "identity");
    clist      = gtk_object_get_data(GTK_OBJECT(dlg), "clist");
    identities = gtk_object_get_data(GTK_OBJECT(clist), "identities");

    text = ident_dialog_get_text(dlg, "identity-name");
    g_return_val_if_fail(text != NULL, FALSE);

    if (g_strcasecmp("", text) == 0) {
        error = gnome_error_dialog(_("Error: The identity does not have a name"));
        gnome_dialog_run_and_close(GNOME_DIALOG(error));
        return FALSE;
    }

    for (list=*identities; list; list = g_list_next(list)) {
        exist_ident = LIBBALSA_IDENTITY(list->data);
        
        if (g_strcasecmp(exist_ident->identity_name, text) == 0 && 
	    id != exist_ident) {
            error = gnome_error_dialog_parented(_("Error: An identity with that name already exists"), GTK_WINDOW(&dlg->window));
            gnome_dialog_run_and_close(GNOME_DIALOG(error));
            return FALSE;
        }
    }

    id->identity_name = text;

    text = ident_dialog_get_text(dlg, "identity-fullname");
    g_return_val_if_fail(text != NULL, FALSE);
    address = libbalsa_address_new();
    address->full_name = text;
    
    text = ident_dialog_get_text(dlg, "identity-address");
    address->address_list = g_list_append(address->address_list, text);
    libbalsa_identity_set_address(id, address);

    id->replyto         = ident_dialog_get_text(dlg, "identity-replyto");
    id->domain          = ident_dialog_get_text(dlg, "identity-domain");    
    id->bcc             = ident_dialog_get_text(dlg, "identity-bcc");
    id->reply_string    = ident_dialog_get_text(dlg, "identity-replystring");
    id->forward_string  = ident_dialog_get_text(dlg, "identity-forwardstring");
    id->signature_path  = ident_dialog_get_text(dlg, "identity-sigpath");
    id->sig_sending     = ident_dialog_get_bool(dlg, "identity-sigappend");
    id->sig_whenforward = ident_dialog_get_bool(dlg, "identity-whenforward");
    id->sig_whenreply   = ident_dialog_get_bool(dlg, "identity-whenreply");
    id->sig_separator   = ident_dialog_get_bool(dlg, "identity-sigseparator");
    id->sig_prepend     = ident_dialog_get_bool(dlg, "identity-sigprepend");
    
    return TRUE;
}


/* 
 * Get the text from an entry in the editing/creation dialog.  The
 * given key accesses the entry using object data.
 */
static gchar*
ident_dialog_get_text(GnomeDialog* dialog, const gchar* key)
{
    GtkEntry* entry;
    gchar* text;
    
    entry = GTK_ENTRY(gtk_object_get_data(GTK_OBJECT(dialog), key));
    text = gtk_editable_get_chars(GTK_EDITABLE(entry), 0, -1);

    return text;
}


/*
 * Get the value of a check button from the editing dialog.  The key
 * is used to retreive the reference to the check button using object
 * data
 */
static gboolean
ident_dialog_get_bool(GnomeDialog* dialog, const gchar* key)
{
    GtkCheckButton* button;
    gboolean value;
    
    button = GTK_CHECK_BUTTON(gtk_object_get_data(GTK_OBJECT(dialog), key));
    value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
    
    return value;
}

/* 
 * Set the current selected identity to the default 
 */
static void
set_current_ident_cb(GtkButton* button, gpointer user_data)
{
    LibBalsaIdentity* ident, **current;
    GtkCList* clist = GTK_CLIST(user_data);
    GList* list = clist->selection;
    gint row;
    
    
    /* should never be true with the _BROWSE selection mode */
    g_return_if_fail (list);

    row = GPOINTER_TO_INT(list->data);
    current = gtk_object_get_data(GTK_OBJECT(clist), "current-id");
    ident = LIBBALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    g_return_if_fail(ident);
    *current = ident;
    identity_list_update(clist);
    gtk_clist_select_row(clist, row, -1);
}


/* 
 * Delete the currently selected identity after confirming. 
 */
static void
delete_ident_cb(GtkWidget* button, gpointer user_data)
{
    LibBalsaIdentity* ident, **current;
    GtkCList* clist = GTK_CLIST(user_data);
    GtkWidget* error;
    GtkWindow* parent;
    GList* select = clist->selection;
    gint row;
    GList *list;

    /* should never be true with the _BROWSE selection mode */
    g_return_if_fail (list);
    
    row = GPOINTER_TO_INT(select->data);
    ident = LIBBALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    current = gtk_object_get_data(GTK_OBJECT(clist), "current-id");
    parent = gtk_object_get_data(GTK_OBJECT(clist), "parent-window");
    if (ident == *current) {
        error = gnome_ok_dialog_parented(
            _("Cannot delete current profile, please make another current first."), 
            parent);
        gnome_dialog_run_and_close(GNOME_DIALOG(error));
        return;
    }
        
    gnome_question_dialog_modal_parented(
        _("Do you really want to delete the selected identity?"), 
        delete_confirm_cb,
        user_data,
	parent);
}


/*
 * Confirm the deletion of an identity, do the actual deletion here,
 * and close the dialog.
 */
static void
delete_confirm_cb(gint reply, gpointer user_data)
{
    if (reply == 0) {
	LibBalsaIdentity* ident;
	GList **identities;
	GtkCList* clist = GTK_CLIST(user_data);
	GList* select = clist->selection;
        gint row = GPOINTER_TO_INT(select->data);

	ident = LIBBALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
	identities = gtk_object_get_data(GTK_OBJECT(clist), "identities");
        *identities = g_list_remove(*identities, ident);
        identity_list_update(clist);
    } 
}


GtkWidget*
libbalsa_identity_config_dialog(GtkWindow* parent, GList**identities,
				LibBalsaIdentity**current)
{
    GtkWidget* dialog;
    GtkWidget* frame = libbalsa_identity_config_frame(FALSE, identities, 
						      current);
    GtkWidget* display_frame = libbalsa_identity_display_frame();
    GtkWidget* hbox = gtk_hbox_new(FALSE, padding);
    GtkWidget* clist;
    const gchar* button_names[] = {
	N_("New"),
	N_("Edit"),
	N_("Set Current"),
	N_("Delete"),
	NULL
    };
    const gchar* button_pixmaps[] = {
	GNOME_STOCK_PIXMAP_NEW,
	GNOME_STOCK_PIXMAP_PROPERTIES,
	GNOME_STOCK_PIXMAP_ADD,
	GNOME_STOCK_PIXMAP_REMOVE,
	NULL
    };
    
    dialog = gnome_dialog_new(_("Manage Identities"),
                              GNOME_STOCK_BUTTON_CLOSE,
                              NULL);
    gnome_dialog_append_buttons_with_pixmaps(GNOME_DIALOG(dialog),
                                             button_names,
                                             button_pixmaps);
    gtk_window_set_modal(GTK_WINDOW(dialog), FALSE);
    gnome_dialog_set_default(GNOME_DIALOG(dialog), 0);
    gnome_dialog_set_close(GNOME_DIALOG(dialog), FALSE);
    gnome_dialog_set_parent(GNOME_DIALOG(dialog), parent);

    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(dialog)->vbox), 
                       hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), display_frame, TRUE, TRUE, 0);

    clist = GTK_WIDGET(gtk_object_get_data(GTK_OBJECT(frame), "clist"));
    gtk_object_set_data(GTK_OBJECT(frame), "clist", NULL);
    gtk_object_set_data(GTK_OBJECT(clist), "parent-window", parent);
    
    gtk_signal_connect(GTK_OBJECT(clist), "select-row",
                       GTK_SIGNAL_FUNC(config_dialog_select_cb),
                       dialog);
    gtk_signal_connect(GTK_OBJECT(clist), "select-row",
                       GTK_SIGNAL_FUNC(identity_list_select_cb),
                       display_frame);
    gtk_clist_select_row(GTK_CLIST(clist), 0, -1);

    gnome_dialog_button_connect(GNOME_DIALOG(dialog), 0,
                                GTK_SIGNAL_FUNC(config_dialog_ok_cb),
                                dialog);
    gnome_dialog_button_connect(GNOME_DIALOG(dialog), 1,
                                GTK_SIGNAL_FUNC(new_ident_cb),
                                clist);
    gnome_dialog_button_connect(GNOME_DIALOG(dialog), 2,
                                GTK_SIGNAL_FUNC(edit_ident_cb),
                                clist);
    gnome_dialog_button_connect(GNOME_DIALOG(dialog), 3,
                                GTK_SIGNAL_FUNC(set_current_ident_cb),
                                clist);
    gnome_dialog_button_connect(GNOME_DIALOG(dialog), 4,
                                GTK_SIGNAL_FUNC(delete_ident_cb),
                                clist);    

    gtk_widget_show_all(GTK_WIDGET(GNOME_DIALOG(dialog)->vbox));

    return dialog;
}


static void
config_dialog_ok_cb(GtkButton* button, gpointer user_data)
{
    gnome_dialog_close(GNOME_DIALOG(user_data));
}

static void
config_dialog_select_cb(GtkCList* clist, gint row, gint column,
                        GdkEventButton* event, gpointer user_data)
{
    LibBalsaIdentity* ident, **current;
    GnomeDialog* dialog = GNOME_DIALOG(user_data);

    gnome_dialog_set_sensitive(dialog, 2, TRUE);
    gnome_dialog_set_sensitive(dialog, 4, TRUE);
    
    ident = LIBBALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    
    /* disable current identity deletion */
    current    = gtk_object_get_data(GTK_OBJECT(clist), "current-id");
    gnome_dialog_set_sensitive(dialog, 3, ident != *current);
}

static GtkWidget*
libbalsa_identity_display_frame(void)
{
    GtkWidget* frame1 = gtk_frame_new(NULL);
    GtkWidget* vbox1 = gtk_vbox_new(FALSE, padding/2);
    
    
    gtk_container_add(GTK_CONTAINER(frame1), vbox1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox1), padding);

    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Identity Name:"), "identity-name");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Full Name:"), "identity-fullname");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Mailing Address:"), "identity-address");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Reply To:"), "identity-replyto");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Domain:"), "identity-domain");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Bcc:"), "identity-bcc");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Reply String:"), "identity-replystring");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Forward String:"), "identity-forwardstring");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Signature Path:"), "identity-sigpath");

    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Append Signature"), "identity-sigappend");
    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Append Signature When Forwarding"), 
                              "identity-whenforward");
    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Append Signature When Replying"), 
                              "identity-whenreply");
    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Include Signature Separator"), 
                              "identity-sigseparator");
    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Prepend Signature"), 
                              "identity-sigprepend");
    
    gtk_widget_show_all(frame1);

    return frame1;
}


static void 
display_frame_add_field(GtkFrame* frame, GtkBox* box, 
                        const gchar* name, const gchar* key)
{
    GtkWidget* hbox = gtk_hbox_new(FALSE, 4);
    GtkWidget* label1 = gtk_label_new(name);
    GtkWidget* label2 = gtk_label_new(NULL);
    
    gtk_box_pack_start(box, hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label2, FALSE, FALSE, 0);
    gtk_object_set_data(GTK_OBJECT(frame), key, label2);
}


static void
display_frame_add_boolean(GtkFrame* frame, GtkBox* box,
                        const gchar* name, const gchar* key)
{
    GtkWidget* hbox = gtk_hbox_new(FALSE, 4);
    GtkWidget* label1 = gtk_label_new(NULL);
    GtkWidget* label2 = gtk_label_new(name);
    
    gtk_box_pack_start(box, hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label2, FALSE, FALSE, 0);
    gtk_object_set_data(GTK_OBJECT(frame), key, label1);
}


static void 
display_frame_update(GtkFrame* frame, LibBalsaIdentity* ident)
{
    display_frame_set_field(frame, "identity-name", ident->identity_name);
    display_frame_set_field(frame, "identity-fullname", 
                            ident->address->full_name);
    if (ident->address->address_list)
        display_frame_set_field(frame, "identity-address", 
                                (gchar*)ident->address->address_list->data);
    else
        display_frame_set_field(frame, "identity-address", NULL);
    
    display_frame_set_field(frame, "identity-replyto", ident->replyto);
    display_frame_set_field(frame, "identity-domain", ident->domain);
    display_frame_set_field(frame, "identity-bcc", ident->bcc);
    display_frame_set_field(frame, "identity-replystring", 
                            ident->reply_string);
    display_frame_set_field(frame, "identity-forwardstring", 
                            ident->forward_string);
    display_frame_set_field(frame, "identity-sigpath", ident->signature_path);
    display_frame_set_boolean(frame, "identity-sigappend", ident->sig_sending);
    display_frame_set_boolean(frame, "identity-whenforward", 
                              ident->sig_whenforward);
    display_frame_set_boolean(frame, "identity-whenreply", 
                              ident->sig_whenreply);
    display_frame_set_boolean(frame, "identity-sigseparator", 
                              ident->sig_separator);    
    display_frame_set_boolean(frame, "identity-sigprepend", 
                              ident->sig_prepend);    
}


static void
display_frame_set_field(GtkFrame* frame, const gchar* key, const gchar* value)
{
    GtkLabel* label = GTK_LABEL(gtk_object_get_data(GTK_OBJECT(frame), key));
    
    gtk_label_set_text(label, value);
}

static void
display_frame_set_boolean(GtkFrame* frame, const gchar* key, gboolean value)
{
    GtkLabel* label = GTK_LABEL(gtk_object_get_data(GTK_OBJECT(frame), key));
    
    gtk_label_set_text(label, value ? _("Do") : _("Do Not"));
}


/* libbalsa_identity_new_config:
   factory-type method creating new Identity object from given
   configuration data.
*/
LibBalsaIdentity*
libbalsa_identity_new_config(const gchar* prefix, const gchar* name)
{
    LibBalsaIdentity* ident;
    gchar* tmpstr;
    
    g_return_val_if_fail(prefix != NULL, NULL);

    gnome_config_push_prefix(prefix);

    ident = LIBBALSA_IDENTITY(libbalsa_identity_new_with_name(name));
    ident->address->full_name = gnome_config_get_string("FullName");
    ident->address->address_list = 
        g_list_append(ident->address->address_list, 
                      gnome_config_get_string("Address"));
    ident->replyto = gnome_config_get_string("ReplyTo");
    ident->domain = gnome_config_get_string("Domain");
    ident->bcc = gnome_config_get_string("Bcc");

    /* 
     * these two have defaults, so we need to use the appropriate
     * functions to manage the memory. 
     */
    if ((tmpstr = gnome_config_get_string("ReplyString"))) {
        g_free(ident->reply_string);
        ident->reply_string = tmpstr;
    }
    
    if ((tmpstr = gnome_config_get_string("ForwardString"))) {
        g_free(ident->forward_string);
        ident->forward_string = tmpstr;
    }
    
    ident->signature_path = gnome_config_get_string("SignaturePath");

    ident->sig_sending = gnome_config_get_bool("SigSending");
    ident->sig_whenforward = gnome_config_get_bool("SigForward");
    ident->sig_whenreply = gnome_config_get_bool("SigReply");
    ident->sig_separator = gnome_config_get_bool("SigSeparator");
    ident->sig_prepend = gnome_config_get_bool("SigPrepend");
    gnome_config_pop_prefix();

    return ident;
}

void 
libbalsa_identity_save(LibBalsaIdentity* ident, const gchar* prefix)
{
    g_return_if_fail(ident);

    gnome_config_push_prefix(prefix);
    gnome_config_set_string("FullName", ident->address->full_name);
    
    if (ident->address->address_list != NULL)
        gnome_config_set_string("Address", ident->address->address_list->data);

    gnome_config_set_string("ReplyTo", ident->replyto);
    gnome_config_set_string("Domain", ident->domain);
    gnome_config_set_string("Bcc", ident->bcc);
    gnome_config_set_string("ReplyString", ident->reply_string);
    gnome_config_set_string("ForwardString", ident->forward_string);
    gnome_config_set_string("SignaturePath", ident->signature_path);

    gnome_config_set_bool("SigSending", ident->sig_sending);
    gnome_config_set_bool("SigForward", ident->sig_whenforward);
    gnome_config_set_bool("SigReply", ident->sig_whenreply);
    gnome_config_set_bool("SigSeparator", ident->sig_separator);
    gnome_config_set_bool("SigPrepend", ident->sig_prepend);

    gnome_config_pop_prefix();
}

