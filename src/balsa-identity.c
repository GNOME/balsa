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
#include "balsa-app.h"
#include "balsa-identity.h"
#include "gnome.h"


static GtkObjectClass* parent_class;
static const gchar* default_ident_name = N_("Default");
static const guint padding = 4;

static void balsa_identity_class_init(BalsaIdentityClass* klass);
static void balsa_identity_init(BalsaIdentity* ident);

static void balsa_identity_destroy(GtkObject* object);

static void new_ident_cb(GtkButton* , gpointer);
static void edit_ident_cb(GtkButton* , gpointer);
static void set_current_ident_cb(GtkButton* , gpointer);
static void delete_ident_cb(GtkButton* , gpointer);
static void delete_confirm_cb(gint reply, gpointer user_data);
static void identity_list_update(GtkCList*);
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


static void setup_ident_dialog(GnomeDialog*, BalsaIdentity*);
static void ident_dialog_add_cb(GtkButton*, gpointer);
static void ident_dialog_edit_cb(GtkButton*, gpointer);
static void ident_dialog_cancel_cb(GtkButton*, gpointer);
static int ident_dialog_close_cb(GnomeDialog*, gpointer);
static void ident_dialog_add_checkbutton(GnomeDialog*, const gchar*, 
                                         const gchar*, gboolean);
static void ident_dialog_add_entry(GnomeDialog*, const gchar*,
                                   const gchar*, gchar*);
static gchar* ident_dialog_get_text(GnomeDialog*, const gchar*);
static gboolean ident_dialog_get_boolean(GnomeDialog*, const gchar*);
static gboolean ident_dialog_update(GnomeDialog*);
static void config_dialog_ok_cb(GtkButton* button, gpointer user_data);
static void config_dialog_select_cb(GtkCList* clist, gint row, gint column,
                                    GdkEventButton* event, gpointer user_data);
static void config_dialog_unselect_cb(GtkCList* clist, gint row, gint column,
                                      GdkEventButton* event, 
                                      gpointer user_data);

static GtkWidget* balsa_identity_display_frame(void);
static void  display_frame_add_field(GtkFrame* frame, GtkBox* box, 
                                     const gchar* name, const gchar* key);
static void display_frame_add_boolean(GtkFrame* frame, GtkBox* box,
                                      const gchar* name, const gchar* key);
static void display_frame_update(GtkFrame* frame, BalsaIdentity* ident);
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
balsa_identity_get_type()
{
    static GtkType balsa_identity_type = 0;
    
    if (!balsa_identity_type) {
        static const GtkTypeInfo balsa_identity_info = 
            {
                "BalsaIdentity",
                sizeof(BalsaIdentity),
                sizeof(BalsaIdentityClass),
                (GtkClassInitFunc) balsa_identity_class_init,
                (GtkObjectInitFunc) balsa_identity_init,
                (GtkArgSetFunc) NULL,
                (GtkArgGetFunc) NULL
            };
        
        balsa_identity_type = 
            gtk_type_unique(gtk_object_get_type(), &balsa_identity_info);
    }
    
    return balsa_identity_type;
}


static void
balsa_identity_class_init(BalsaIdentityClass* klass)
{
    GtkObjectClass* object_class;

    parent_class = gtk_type_class(gtk_object_get_type());

    object_class = GTK_OBJECT_CLASS(klass);
    object_class->destroy = balsa_identity_destroy;
}


/* 
 * Class inititialization function, set defaults for new objects
 */
static void
balsa_identity_init(BalsaIdentity* ident)
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
balsa_identity_new(void) 
{
    BalsaIdentity* ident;
    
    ident = BALSA_IDENTITY(gtk_type_new(BALSA_TYPE_IDENTITY));
    ident->identity_name = g_strdup(default_ident_name);
    return GTK_OBJECT(ident);
}


/*
 * Create a new object with the specified identity name.  Does not add
 * it to the list of identities for the application.
 */
GtkObject*
balsa_identity_new_with_name(const gchar* ident_name)
{
    BalsaIdentity* ident;
    
    ident = BALSA_IDENTITY(gtk_type_new(BALSA_TYPE_IDENTITY));
    ident->identity_name = g_strdup(ident_name);
    return GTK_OBJECT(ident);
}


/* 
 * Destroy the object, freeing all the values in the process.
 */
static void
balsa_identity_destroy(GtkObject* object)
{
    BalsaIdentity* ident;
    
    g_return_if_fail(object != NULL);
    
    ident = BALSA_IDENTITY(object);

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


/*
 * Set the specified identity to be the current default.
 */
void
balsa_identity_set_current(BalsaIdentity* ident) 
{
    g_return_if_fail(ident != NULL);
    
    balsa_app.current_ident = ident;

    balsa_app.address = ident->address;
    balsa_app.replyto = ident->replyto;
    balsa_app.domain = ident->domain;
    balsa_app.bcc = ident->bcc;
    balsa_app.reply_string = ident->reply_string;
    balsa_app.forward_string = ident->forward_string;

    balsa_app.signature_path = ident->signature_path;
    balsa_app.sig_sending = ident->sig_sending;
    balsa_app.sig_whenforward = ident->sig_whenforward;
    balsa_app.sig_whenreply = ident->sig_whenreply;
    balsa_app.sig_separator = ident->sig_separator;
    balsa_app.sig_prepend = ident->sig_prepend;
}



void
balsa_identity_set_identity_name(BalsaIdentity* ident, const gchar* name)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->identity_name);
    ident->identity_name = g_strdup(name);
}


void
balsa_identity_set_address(BalsaIdentity* ident, LibBalsaAddress* ad)
{
    g_return_if_fail(ident != NULL);
    
    gtk_object_destroy(GTK_OBJECT(ident->address));
    ident->address = ad;
    
    if (ident == balsa_app.current_ident)
        balsa_app.address = ident->address;
}


void
balsa_identity_set_replyto(BalsaIdentity* ident, const gchar* address)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->replyto);
    ident->replyto = g_strdup(address);
    
    if (ident == balsa_app.current_ident) 
        balsa_app.replyto = ident->replyto;
}


void
balsa_identity_set_domain(BalsaIdentity* ident, const gchar* dom)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->domain);
    ident->domain = g_strdup(dom);

    if (ident == balsa_app.current_ident) 
        balsa_app.domain = ident->domain;
}


void 
balsa_identity_set_bcc(BalsaIdentity* ident, const gchar* bcc)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->bcc);
    ident->bcc = g_strdup(bcc);
    
    if (ident == balsa_app.current_ident)
        balsa_app.bcc = ident->bcc;
}


void 
balsa_identity_set_reply_string(BalsaIdentity* ident, const gchar* reply)
{
    g_return_if_fail(ident != NULL);

    g_free(ident->reply_string);
    ident->reply_string = g_strdup(reply);
    
    if (ident == balsa_app.current_ident)
        balsa_app.reply_string = ident->reply_string;
}


void 
balsa_identity_set_forward_string(BalsaIdentity* ident, const gchar* forward)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->forward_string);
    ident->forward_string = g_strdup(forward);
    
    if (ident == balsa_app.current_ident)
        balsa_app.forward_string = ident->forward_string;
}


void
balsa_identity_set_signature_path(BalsaIdentity* ident, const gchar* path)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->signature_path);
    ident->signature_path = g_strdup(path);
    
    if (ident == balsa_app.current_ident)
        balsa_app.signature_path = ident->signature_path;
}


void
balsa_identity_set_sig_sending(BalsaIdentity* ident, gboolean sig_sending)
{
    g_return_if_fail(ident != NULL);
    
    ident->sig_sending = sig_sending;
    
    if (ident == balsa_app.current_ident)
        balsa_app.sig_sending = ident->sig_sending;
}


void
balsa_identity_set_sig_whenforward(BalsaIdentity* ident, gboolean forward)
{
    g_return_if_fail(ident != NULL);
    
    ident->sig_whenforward = forward;
    
    if (ident == balsa_app.current_ident)
        balsa_app.sig_whenforward = ident->sig_whenforward;
}


void 
balsa_identity_set_sig_whenreply(BalsaIdentity* ident, gboolean reply)
{
    g_return_if_fail(ident != NULL);
    
    ident->sig_whenreply = reply;
    
    if (ident == balsa_app.current_ident) 
        balsa_app.sig_whenreply = ident->sig_whenreply;
}


void
balsa_identity_set_sig_separator(BalsaIdentity* ident, gboolean separator)
{
    g_return_if_fail(ident != NULL);
    
    ident->sig_separator = separator;
    
    if (ident == balsa_app.current_ident)
        balsa_app.sig_separator = ident->sig_separator;
}


void 
balsa_identity_set_sig_prepend(BalsaIdentity* ident, gboolean prepend)
{
    g_return_if_fail(ident != NULL);
    
    ident->sig_prepend = prepend;
    
    if (ident == balsa_app.current_ident)
        balsa_app.sig_prepend = ident->sig_prepend;
}


BalsaIdentity*
balsa_identity_select_dialog(const gchar* prompt)
{
    BalsaIdentity* ident = NULL;
    GtkWidget* frame1 = gtk_frame_new(NULL);
    GtkWidget* clist = gtk_clist_new(2);
    GtkWidget* dialog;
    gint choice = 1;


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
    gtk_clist_set_selection_mode(GTK_CLIST(clist), GTK_SELECTION_SINGLE);
    identity_list_update(GTK_CLIST(clist));

    gtk_signal_connect(GTK_OBJECT(clist), "select-row",
                       GTK_SIGNAL_FUNC(select_dialog_row_cb),
                       &ident);

    gnome_dialog_set_close(GNOME_DIALOG(dialog), TRUE);
    gnome_dialog_set_default(GNOME_DIALOG(dialog), 1);
    
    choice = gnome_dialog_run(GNOME_DIALOG(dialog));
    
    if (choice == 1) {
        return NULL;
    } 
    
    return ident;
}


static void
select_dialog_row_cb(GtkCList* clist, 
                     gint row, 
                     gint column, 
                     GdkEventButton* event, 
                     gpointer user_data)
{
    BalsaIdentity** selected_ident = (BalsaIdentity**) user_data;
    BalsaIdentity* row_data;
    

    row_data = BALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    *selected_ident = row_data;
}



/*
 * Create and return a frame containing a list of the identities in
 * the application and a number of buttons to edit, create, and delete
 * identities.  Also provides a way to set the current identity.
 */
GtkWidget* 
balsa_identity_config_frame(gboolean with_buttons)
{

    GtkWidget* config_frame = gtk_frame_new(NULL);
    GtkWidget* hbox = gtk_hbox_new(FALSE, padding);
    GtkWidget* vbox;
    GtkWidget* clist = gtk_clist_new(2);
    GtkWidget* buttons[4];
    GtkWidget* button1;
    GtkWidget* button2;
    GtkWidget* button3;
    GtkWidget* button4;
    gint i = 0;



    gtk_container_set_border_width(GTK_CONTAINER(config_frame), 0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), padding);
    gtk_container_add(GTK_CONTAINER(config_frame), hbox);
    gtk_box_pack_start(GTK_BOX(hbox), clist, TRUE, TRUE, 0);
    
    if (with_buttons) {
        vbox = gtk_vbox_new(FALSE, padding);
        gtk_box_pack_end(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

        button1 = gnome_stock_button_with_label(GNOME_STOCK_PIXMAP_NEW, 
                                                _("New"));
        gtk_signal_connect(GTK_OBJECT(button1), "clicked",
                           GTK_SIGNAL_FUNC(new_ident_cb), clist);
        
        button2 = gnome_stock_button_with_label(GNOME_STOCK_PIXMAP_PROPERTIES, 
                                                _("Edit"));
        gtk_signal_connect(GTK_OBJECT(button2), "clicked",
                           GTK_SIGNAL_FUNC(edit_ident_cb), clist);
        
        button3 = gnome_stock_button_with_label(GNOME_STOCK_PIXMAP_ADD, 
                                                _("Set Current"));
        gtk_signal_connect(GTK_OBJECT(button3), "clicked",
                           GTK_SIGNAL_FUNC(set_current_ident_cb), clist);
        
        button4 = gnome_stock_button_with_label(GNOME_STOCK_PIXMAP_REMOVE, 
                                                _("Delete"));
        gtk_signal_connect(GTK_OBJECT(button4), "clicked",
                           GTK_SIGNAL_FUNC(delete_ident_cb), clist);

        buttons[0] = button1;
        buttons[1] = button2;
        buttons[2] = button3;
        buttons[3] = button4;
        
        gtk_box_pack_start(GTK_BOX(vbox), button1, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), button2, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), button3, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), button4, FALSE, FALSE, 0);

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
    gtk_clist_set_selection_mode(GTK_CLIST(clist), GTK_SELECTION_SINGLE);
    identity_list_update(GTK_CLIST(clist));
    gtk_clist_select_row(GTK_CLIST(clist), 0, -1);
    gtk_object_set_data(GTK_OBJECT(config_frame), "clist", clist);

    return config_frame;
}


static void
identity_list_select_cb(GtkCList* clist, gint row, gint column, 
                        GdkEventButton* event, gpointer user_data)
{
    BalsaIdentity* ident;
    GtkFrame* frame = GTK_FRAME(user_data);
    
    ident = BALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    display_frame_update(frame, ident);
}



/* 
 * Update the list of identities in the config frame, displaying the
 * available identities in the application, and which is current.
 */
static void
identity_list_update(GtkCList* clist)
{
    BalsaIdentity* ident;
    GdkPixmap* pixmap = NULL;
    GdkPixmap* bitmap = NULL;
    GList* list = balsa_app.identities;
    gchar* text[2];
    gint i = 0;

    text[0] = NULL;

    gtk_clist_freeze(clist);
    gtk_clist_clear(clist);
    
    while (list != NULL) {
        ident = BALSA_IDENTITY(list->data);
        text[1] = ident->identity_name;
        i = gtk_clist_append(clist, text);
        gtk_clist_set_row_data(clist, i, ident);
        
        if (ident == balsa_app.current_ident) {
            /* do something to indicate it is the active style */
            gnome_stock_pixmap_gdk(GNOME_STOCK_MENU_FORWARD, 
                                   GNOME_STOCK_PIXMAP_REGULAR, 
                                   &pixmap, &bitmap);
            gtk_clist_set_pixmap(clist, i, 0, pixmap, bitmap);
        } else {
            gnome_stock_pixmap_gdk(GNOME_STOCK_MENU_BLANK,
                                   GNOME_STOCK_PIXMAP_REGULAR,
                                   &pixmap, &bitmap);
            gtk_clist_set_pixmap(clist, i, 0, pixmap, bitmap);
        }

        list = g_list_next(list);
    }

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
    /* create a new identity dialog */
    BalsaIdentity* ident;
    GtkWidget* dialog;


    dialog = gnome_dialog_new(_("Create Identity"), 
                              GNOME_STOCK_BUTTON_OK,
                              GNOME_STOCK_BUTTON_CANCEL,
                              NULL); 
    ident = BALSA_IDENTITY(balsa_identity_new());
    setup_ident_dialog(GNOME_DIALOG(dialog), ident);
    gnome_dialog_button_connect(GNOME_DIALOG(dialog), 0,
                                GTK_SIGNAL_FUNC(ident_dialog_add_cb),
                                dialog);
    gtk_object_set_data(GTK_OBJECT(dialog), "identity", ident);
    gtk_object_set_data(GTK_OBJECT(dialog), "identity-edit", NULL);
    gtk_object_set_data(GTK_OBJECT(dialog), "clist", user_data);

    gnome_dialog_run(GNOME_DIALOG(dialog));
}


/*
 * Edit the selected identity in a new dialog
 */
static void 
edit_ident_cb(GtkButton* button, gpointer user_data)
{
    /* create a dialog for editing the selected identity */
    GtkWidget* dialog;
    BalsaIdentity* ident;
    GtkWidget* error;
    GtkCList* clist = GTK_CLIST(user_data);
    GList* list = clist->selection;
    gint row;
    

    if (list == NULL) {
        error = gnome_error_dialog_parented(_("No identity selected"), 
                                            GTK_WINDOW(balsa_app.main_window));
        gnome_dialog_run_and_close(GNOME_DIALOG(error));
        return;
    }
    
    dialog = gnome_dialog_new(_("Edit Identity"), 
                              GNOME_STOCK_BUTTON_APPLY,
                              GNOME_STOCK_BUTTON_CANCEL,
                              NULL);
    
    row = GPOINTER_TO_INT(list->data);
    ident = BALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    setup_ident_dialog(GNOME_DIALOG(dialog), ident);
    gnome_dialog_button_connect(GNOME_DIALOG(dialog), 0, 
                                GTK_SIGNAL_FUNC(ident_dialog_edit_cb),
                                dialog);
    gtk_object_set_data(GTK_OBJECT(dialog), "identity", ident);
    gtk_object_set_data(GTK_OBJECT(dialog), "identity-edit", (gpointer)TRUE);
    gtk_object_set_data(GTK_OBJECT(dialog), "clist", user_data);
    
    gnome_dialog_run(GNOME_DIALOG(dialog));
}


/*
 * Put the required GtkEntries, Labels, and Checkbuttons in the dialog
 * for creating/editing identities
 */
static void 
setup_ident_dialog(GnomeDialog* dialog, BalsaIdentity* ident)
{
     
    GtkWidget* frame = gtk_frame_new(NULL);
    GtkWidget* vbox = gtk_vbox_new(FALSE, padding);
    GtkWidget* main_box = dialog->vbox;


    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gnome_dialog_set_close(dialog, FALSE);
    gnome_dialog_set_default(dialog, 1);
    gnome_dialog_set_parent(dialog, GTK_WINDOW(balsa_app.main_window));
    
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

    ident_dialog_add_entry(dialog, _("Identity Name:"), 
                           "identity-name",
                           ident->identity_name);
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
static void
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
}


/*
 * Adds the identity to the application, then closes the edit dialog.
 */
static void
ident_dialog_add_cb(GtkButton* button, gpointer user_data)
{
    GnomeDialog* dialog = GNOME_DIALOG(user_data);
    GtkCList* clist;    
    BalsaIdentity* ident;
    gboolean updated;
    gint row;


    updated = ident_dialog_update(dialog);

    if (updated == FALSE) {
        return;
    } 

    ident = gtk_object_get_data(GTK_OBJECT(dialog), "identity");
    balsa_app.identities = g_list_append(balsa_app.identities, ident);
    gtk_object_set_data(GTK_OBJECT(dialog), "identity", NULL);
    clist = GTK_CLIST(gtk_object_get_data(GTK_OBJECT(dialog), "clist"));
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
    gpointer ident;
    gboolean updated;
    gint row;
    
    
    updated = ident_dialog_update(dialog);

    if (updated == FALSE) {
        return;
    } 
    

    clist = GTK_CLIST(gtk_object_get_data(GTK_OBJECT(dialog), "clist"));
    identity_list_update(clist);

    ident = gtk_object_get_data(GTK_OBJECT(dialog), "identity");
    row = gtk_clist_find_row_from_data(clist, ident);
    gtk_clist_select_row(clist, row, -1);
    gtk_object_set_data(GTK_OBJECT(dialog), "identity", NULL);

    gnome_dialog_close(dialog);
}


/* 
 * Cancels the creation or editing of an identity, closes the dialog.
 */
static void
ident_dialog_cancel_cb(GtkButton* button, gpointer user_data)
{
    gnome_dialog_close(GNOME_DIALOG(user_data));
}


/*
 * The close callback for the editing/creation dialog
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
ident_dialog_update(GnomeDialog* dialog)
{
    BalsaIdentity* ident;
    BalsaIdentity* exist_ident;
    LibBalsaAddress* address;
    GtkWidget* error;
    GList* list = balsa_app.identities;
    gchar* text;
    gboolean value;

    
    ident = gtk_object_get_data(GTK_OBJECT(dialog), "identity");

    text = ident_dialog_get_text(dialog, "identity-name");
    g_return_val_if_fail(text != NULL, FALSE);

    if (g_strcasecmp("", text) == 0) {
        error = gnome_error_dialog(_("Error: The identity does not have a name"));
        gnome_dialog_run_and_close(GNOME_DIALOG(error));
        return FALSE;
    }

    while (list != NULL) {
        exist_ident = BALSA_IDENTITY(list->data);
        
        if (g_strcasecmp(exist_ident->identity_name, text) == 0) {
            error = gnome_error_dialog_parented(_("Error: An identity with that name already exists"), GTK_WINDOW(balsa_app.main_window));
            gnome_dialog_run_and_close(GNOME_DIALOG(error));
            return FALSE;
        }

        list = g_list_next(list);
    }

    balsa_identity_set_identity_name(ident, text);
    g_free(text);

    text = ident_dialog_get_text(dialog, "identity-fullname");
    g_return_val_if_fail(text != NULL, FALSE);
    address = libbalsa_address_new();
    address->full_name = text;
    
    text = ident_dialog_get_text(dialog, "identity-address");
    address->address_list = g_list_append(address->address_list, text);
    balsa_identity_set_address(ident, address);

    text = ident_dialog_get_text(dialog, "identity-replyto");
    balsa_identity_set_replyto(ident, text);
    g_free(text);

    text = ident_dialog_get_text(dialog, "identity-domain");
    balsa_identity_set_domain(ident, text);
    g_free(text);
    
    text = ident_dialog_get_text(dialog, "identity-bcc");
    balsa_identity_set_bcc(ident, text);
    g_free(text);
    
    text = ident_dialog_get_text(dialog, "identity-replystring");
    balsa_identity_set_reply_string(ident, text);
    g_free(text);
    
    text = ident_dialog_get_text(dialog, "identity-forwardstring");
    balsa_identity_set_forward_string(ident, text);
    g_free(text);

    text = ident_dialog_get_text(dialog, "identity-sigpath");
    balsa_identity_set_signature_path(ident, text);
    g_free(text);
    
    value = ident_dialog_get_boolean(dialog, "identity-sigappend");
    balsa_identity_set_sig_sending(ident, value);

    value = ident_dialog_get_boolean(dialog, "identity-whenforward");
    balsa_identity_set_sig_whenforward(ident, value);
    
    value = ident_dialog_get_boolean(dialog, "identity-whenreply");
    balsa_identity_set_sig_whenreply(ident, value);
    
    value = ident_dialog_get_boolean(dialog, "identity-sigseparator");
    balsa_identity_set_sig_separator(ident, value);
    
    value = ident_dialog_get_boolean(dialog, "identity-sigprepend");
    balsa_identity_set_sig_prepend(ident, value);
    
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
ident_dialog_get_boolean(GnomeDialog* dialog, const gchar* key)
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
    BalsaIdentity* ident;
    GtkCList* clist = GTK_CLIST(user_data);
    GtkWidget* error;
    GList* list = clist->selection;
    gint row;
    
    
    if (list == NULL) {
        error = gnome_error_dialog_parented(_("No identity selected"), 
                                            GTK_WINDOW(balsa_app.main_window));
        gnome_dialog_run_and_close(GNOME_DIALOG(error));
        return;
    }
        
    row = GPOINTER_TO_INT(list->data);
    ident = BALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    balsa_identity_set_current(ident);
    identity_list_update(clist);
    gtk_clist_select_row(clist, row, -1);
}


/* 
 * Delete the currently selected identity after confirming. 
 */
static void
delete_ident_cb(GtkButton* button, gpointer user_data)
{
    BalsaIdentity* ident;
    GtkCList* clist = GTK_CLIST(user_data);
    GtkWidget* error;
    GList* select = clist->selection;
    gint row;


    if (select == NULL) {
        error = gnome_error_dialog_parented(_("No identity selected"), 
                                            GTK_WINDOW(balsa_app.main_window));
        gnome_dialog_run_and_close(GNOME_DIALOG(error));
        return;
    }
    
    row = GPOINTER_TO_INT(select->data);
    ident = BALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    if (ident == balsa_app.current_ident) {
        error = gnome_ok_dialog_parented(
            _("Cannot delete current profile, please make another current first."), 
            GTK_WINDOW(balsa_app.main_window));
        gnome_dialog_run_and_close(GNOME_DIALOG(error));
        return;
    }
        
    gnome_question_dialog_modal_parented(
        _("Do you really want to delete the selected identity?"), 
        delete_confirm_cb,
        user_data,
        GTK_WINDOW(balsa_app.main_window));
}


/*
 * Confirm the deletion of an identity, do the actual deletion here,
 * and close the dialog.
 */
static void
delete_confirm_cb(gint reply, gpointer user_data)
{
    BalsaIdentity* ident;
    GtkCList* clist = GTK_CLIST(user_data);
    GList* select = clist->selection;
    gint row;
    

    if (reply == 0) {
        row = GPOINTER_TO_INT(select->data);
        ident = BALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
        balsa_app.identities = g_list_remove(balsa_app.identities, ident);
        identity_list_update(clist);
    } 
}



GtkWidget*
balsa_identity_config_dialog(void)
{
     
    GtkWidget* dialog;
    GtkWidget* frame = balsa_identity_config_frame(FALSE);
    GtkWidget* display_frame = balsa_identity_display_frame();
    GtkWidget* hbox = gtk_hbox_new(FALSE, padding);
    GtkWidget* clist;
    const gchar* button_names[] = 
        {
            N_("New"),
            N_("Edit"),
            N_("Set Current"),
            N_("Delete"),
            NULL
        };
    const gchar* button_pixmaps[] =
        {
            GNOME_STOCK_PIXMAP_NEW,
            GNOME_STOCK_PIXMAP_PROPERTIES,
            GNOME_STOCK_PIXMAP_ADD,
            GNOME_STOCK_PIXMAP_REMOVE,
            NULL
        };

    dialog = gnome_dialog_new(_("Manage Identities"),
                              GNOME_STOCK_BUTTON_OK,
                              NULL);
    gnome_dialog_append_buttons_with_pixmaps(GNOME_DIALOG(dialog),
                                             button_names,
                                             button_pixmaps);
    gtk_window_set_modal(GTK_WINDOW(dialog), FALSE);
    gnome_dialog_set_default(GNOME_DIALOG(dialog), 0);
    gnome_dialog_set_close(GNOME_DIALOG(dialog), FALSE);
    gnome_dialog_set_parent(GNOME_DIALOG(dialog), 
                            GTK_WINDOW(balsa_app.main_window));

    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(dialog)->vbox), 
                       hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), display_frame, TRUE, TRUE, 0);

    clist = GTK_WIDGET(gtk_object_get_data(GTK_OBJECT(frame), "clist"));
    gtk_object_set_data(GTK_OBJECT(frame), "clist", NULL);

    gtk_signal_connect(GTK_OBJECT(clist), "select-row",
                       GTK_SIGNAL_FUNC(config_dialog_select_cb),
                       dialog);
    gtk_signal_connect(GTK_OBJECT(clist), "unselect-row",
                       GTK_SIGNAL_FUNC(config_dialog_unselect_cb),
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
    BalsaIdentity* ident;
    GnomeDialog* dialog = GNOME_DIALOG(user_data);

    
    gnome_dialog_set_sensitive(dialog, 2, TRUE);
    gnome_dialog_set_sensitive(dialog, 4, TRUE);
    
    ident = BALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    
    if (ident == balsa_app.current_ident)
        gnome_dialog_set_sensitive(dialog, 3, FALSE);
    else
        gnome_dialog_set_sensitive(dialog, 3, TRUE);
}


static void
config_dialog_unselect_cb(GtkCList* clist, gint row, gint column,
                          GdkEventButton* event, gpointer user_data)
{
    GnomeDialog* dialog = GNOME_DIALOG(user_data);

    
    gnome_dialog_set_sensitive(dialog, 2, FALSE);
    gnome_dialog_set_sensitive(dialog, 3, FALSE);
    gnome_dialog_set_sensitive(dialog, 4, FALSE);
}


static GtkWidget*
balsa_identity_display_frame(void)
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
display_frame_update(GtkFrame* frame, BalsaIdentity* ident)
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
    
    if (value) {
        gtk_label_set_text(label, _("Do"));
    } else {
        gtk_label_set_text(label, _("Do Not"));
    }
}

