/* Balsa E-Mail Client
 * Copyright (C) 1998-1999 Stuart Parmenter
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

/* PKGW 11/17/1999: Major overhaul to use GnomeDruid. Huzzah! 

   However, what follows may not be suitable for younger, more
   impressionable programmers. It is sooo, sooo, sooo ugly. To
   make it clean would require deriving GnomeDruidPages for my
   various special pages, no small task. However, if you delete
   your .balsarc and run the setter-upper -- wow!

   TODO: some GtkLabels aren't repainted??
*/

#include "config.h"

#include <gnome.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "libbalsa.h"
#include "main.h"
#include "misc.h"
#include "balsa-app.h"
#include "balsa-init.h"
#include "save-restore.h"

/* ********************************************************************** */

void initialize_balsa( void );
void balsa_init_window_new (void);

/* ********************************************************************** */

enum UserSetMask {
    SET_REALNAME = (1 << 0),
    SET_EMAIL =    (1 << 1),
    SET_SMTP =     (1 << 2),
    SET_ALL_DONE = 0x0007
};

typedef struct DirConfState_s {
    gchar *uf_name;
    gchar *path;
    struct DirConfState_s *next;
} DirConfState;

static DirConfState trash =    { N_("Trash"), NULL, NULL };
static DirConfState draftbox = { N_("Draftbox"), NULL, &trash };
static DirConfState sentbox =  { N_("Sentbox"), NULL, &draftbox };
static DirConfState outbox =   { N_("Outbox"), NULL, &sentbox };
static DirConfState inbox =    { N_("Inbox"), NULL, &outbox };

/* ********************************************************************** */

static gboolean generic_next_cb( GnomeDruidPage *page, GnomeDruid *druid );
static gboolean generic_back_cb( GnomeDruidPage *page, GnomeDruid *druid );

static void     user_prepare_cb( GnomeDruidPage *user, GnomeDruid *druid );
static gboolean user_next_cb( GnomeDruidPage *user, GnomeDruid *druid );
static void     entry_changed_cb( GtkEntry *entry );

static gboolean folder_next_cb( GnomeDruidPage *user, GnomeDruid *druid );
static gboolean create_folder_next_cb( GnomeDruidPage *user, GnomeDruid *druid );

static void     dirsel_prepare_cb( GnomeDruidPage *page, GnomeDruid *druid );
static gboolean dirsel_next_cb( GnomeDruidPage *page, GnomeDruid *druid );
static gboolean dirsel_back_cb( GnomeDruidPage *page, GnomeDruid *druid );

static void     create_mbox_prepare_cb( GnomeDruidPage *page, GnomeDruid *druid );
static gboolean create_mbox_next_cb( GnomeDruidPage *page, GnomeDruid *druid );

static void     druid_destroy_cb( GnomeDruid *druid );
static void     druid_cancel_cb( GnomeDruid *druid );
static void     finish_cb( GnomeDruidPage *page, GnomeDruid *druid );
static void     really_cb( gint reply, GnomeDruid *druid );

static GtkWidget *new_user_table( GnomeDruidPage *page, GnomeDruid *druid );
static GtkWidget *new_folder_table( GnomeDruidPage *page, GnomeDruid *druid );
static GdkImlibImage *get_png( const gchar *file );
static gboolean dir_exists( gchar *path, gchar **buf );
static void insert_page_after( GnomeDruidPage *cur, GnomeDruidPage *next, GnomeDruid *druid );

/* ********************************************************************** */

void
initialize_balsa (void)
{
  balsa_init_window_new ();
}

void
balsa_init_window_new (void)
{
    GtkWidget *window;
    GtkWidget *druid;
    GtkWidget *page;
    GtkWidget *widget;
    GdkImlibImage *logo;
    GdkImlibImage *watermark;

    logo = get_png( "balsa/balsa-logo.png" );
    watermark = get_png( "balsa/balsa-watermark.png" );

    window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_title( GTK_WINDOW( window ), _("Configure Balsa") );

    druid = gnome_druid_new();
    gtk_signal_connect( GTK_OBJECT( druid ), "cancel", GTK_SIGNAL_FUNC( druid_cancel_cb ), NULL );
    gtk_object_set_data( GTK_OBJECT( druid ), "logo_imlib", logo );

    /* Start page **************************************** */
    /*This text we must linebreak manually. */
    page = gnome_druid_page_start_new_with_vals( _("Welcome to Balsa!"),
						 _("You seem to be running Balsa for the first time. The following\n"
						   "steps will set up Balsa by asking a few simple questions. Once\n"
						   "you have completed these steps, you can always change them later\n"
						   "in Balsa's preferences. Please check the about box in Balsa's\n"
						   "main window for more information about contacting the authors or\n"
						   "reporting bugs."),
						 logo, watermark ); 
    gtk_signal_connect( GTK_OBJECT( page ), "next", GTK_SIGNAL_FUNC( generic_next_cb ), NULL );
    gnome_druid_append_page( GNOME_DRUID( druid ), GNOME_DRUID_PAGE( page ) );
    gnome_druid_set_page( GNOME_DRUID( druid ), GNOME_DRUID_PAGE( page ) );
    widget = page;

    /* User info **************************************** */
    page = gnome_druid_page_standard_new_with_vals( _("User Settings"), logo );
    gtk_object_set_data( GTK_OBJECT( widget ), "next", page ); /* Set previous page's next page. Sigh. */
    gtk_signal_connect( GTK_OBJECT( page ), "next", GTK_SIGNAL_FUNC( user_next_cb ), NULL );
    gtk_signal_connect( GTK_OBJECT( page ), "back", GTK_SIGNAL_FUNC( generic_back_cb ), NULL );
    gtk_signal_connect( GTK_OBJECT( page ), "prepare", GTK_SIGNAL_FUNC( user_prepare_cb ), NULL );

    widget = gtk_label_new( _("Please enter information about yourself. If you don't\n"
			      "use an SMTP server, enter \"localhost\" into the box.") );
    gtk_box_pack_start( GTK_BOX( (GNOME_DRUID_PAGE_STANDARD( page ))->vbox ), GTK_WIDGET( widget ), TRUE, TRUE, 4 );

    widget = new_user_table( GNOME_DRUID_PAGE( page ), GNOME_DRUID( druid ) );
    gtk_box_pack_start( GTK_BOX( (GNOME_DRUID_PAGE_STANDARD( page ))->vbox ), GTK_WIDGET( widget ), TRUE, TRUE, 4 );
    gtk_object_set_data( GTK_OBJECT( page ), "table", widget );

    gnome_druid_append_page( GNOME_DRUID( druid ), GNOME_DRUID_PAGE( page ) );
    widget = page;

    /* Folders **************************************** */

    page = gnome_druid_page_standard_new_with_vals( _("Mail Directory Settings"), logo );
    gtk_object_set_data( GTK_OBJECT( widget ), "next", page ); /* Set previous page's next page. Sigh. */
    gtk_signal_connect( GTK_OBJECT( page ), "back", GTK_SIGNAL_FUNC( generic_back_cb ), NULL );
    gtk_signal_connect( GTK_OBJECT( page ), "next", GTK_SIGNAL_FUNC( folder_next_cb ), NULL );

    widget = new_folder_table( GNOME_DRUID_PAGE( page ), GNOME_DRUID( druid ) );
    gtk_box_pack_start( GTK_BOX( (GNOME_DRUID_PAGE_STANDARD( page ))->vbox ), GTK_WIDGET( widget ), TRUE, TRUE, 4 );

    gnome_druid_append_page( GNOME_DRUID( druid ), GNOME_DRUID_PAGE( page ) );
    widget = page;

    /* Specific folder setup **************************************** */
    page = gnome_druid_page_standard_new_with_vals( "OOPS BUG", logo );
    gtk_object_set_data( GTK_OBJECT( widget ), "next", page ); /* Set previous page's next page. Sigh. */
    gtk_object_set_data( GTK_OBJECT( widget ), "success_next", page ); /* Double sigh. */
    gtk_object_set_data( GTK_OBJECT( page ), "state", &inbox );
    gtk_object_set_data( GTK_OBJECT( page ), "prev_orig", widget );
    gtk_signal_connect( GTK_OBJECT( page ), "prepare", GTK_SIGNAL_FUNC( dirsel_prepare_cb ), NULL );
    gtk_signal_connect( GTK_OBJECT( page ), "back", GTK_SIGNAL_FUNC( dirsel_back_cb ), NULL );
    gtk_signal_connect( GTK_OBJECT( page ), "next", GTK_SIGNAL_FUNC( dirsel_next_cb ), NULL );

    widget = gtk_label_new( "OOPS BUG" );
    gtk_box_pack_start( GTK_BOX( (GNOME_DRUID_PAGE_STANDARD( page ))->vbox ), GTK_WIDGET( widget ), TRUE, TRUE, 4 );
    /*gtk_label_set_line_wrap( GTK_LABEL( widget ), TRUE );*/
    gtk_object_set_data( GTK_OBJECT( page ), "label", widget );
    gtk_widget_show( GTK_WIDGET( widget ) );

    widget = gtk_entry_new();
    gtk_box_pack_start( GTK_BOX( (GNOME_DRUID_PAGE_STANDARD( page ))->vbox ), GTK_WIDGET( widget ), FALSE, FALSE, 4 );
    gtk_object_set_data( GTK_OBJECT( page ), "entry", widget );
    gtk_widget_show( GTK_WIDGET( widget ) );

    gtk_widget_show_all( GTK_WIDGET( page ) );
    gnome_druid_append_page( GNOME_DRUID( druid ), GNOME_DRUID_PAGE( page ) );
    widget = page;

    /* Finish **************************************** */
    page = gnome_druid_page_finish_new_with_vals( _("All done!"),
						  _("You've successfully set up Balsa. Have fun!\n"
						    "   -- The Balsa development team"),
						  logo, watermark );
    gtk_object_set_data( GTK_OBJECT( widget ), "next", page ); /* Set previous page's next page. Sigh. */
    gtk_object_set_data( GTK_OBJECT( widget ), "next_done", page ); /* Set previous page's next page. Sigh. */
    gtk_signal_connect( GTK_OBJECT( page ), "finish", GTK_SIGNAL_FUNC( finish_cb ), NULL );
    gnome_druid_append_page( GNOME_DRUID( druid ), GNOME_DRUID_PAGE( page ) );

    /* All done **************************************** */
    gtk_container_add( GTK_CONTAINER( window ), GTK_WIDGET( druid ) );
    gtk_widget_show_all( GTK_WIDGET( window ) );

    /* start up the gtk_main for the initialize window */
    gtk_main ();
}

/* ********************************************************************** */

static gboolean generic_next_cb( GnomeDruidPage *page, GnomeDruid *druid )
{
    GtkWidget *next = gtk_object_get_data( GTK_OBJECT( page ), "next" );
    gtk_object_set_data( GTK_OBJECT( next ), "prev", page );
    gnome_druid_set_page( GNOME_DRUID( druid ), GNOME_DRUID_PAGE( next ) );
    return TRUE;
}

static gboolean generic_back_cb( GnomeDruidPage *page, GnomeDruid *druid )
{
    GtkWidget *back = gtk_object_get_data( GTK_OBJECT( page ), "prev" );
    gtk_object_set_data( GTK_OBJECT( back ), "next", page );
    gnome_druid_set_page( GNOME_DRUID( druid ), GNOME_DRUID_PAGE( back ) );
    return TRUE;
}

/* ********************************************************************** */

static void user_prepare_cb( GnomeDruidPage *user, GnomeDruid *druid )
{
    guint pagemask = GPOINTER_TO_INT( gtk_object_get_data( GTK_OBJECT( user ), "mask" ) );

    if( (pagemask & SET_ALL_DONE) == SET_ALL_DONE ) {
	gnome_druid_set_buttons_sensitive( GNOME_DRUID( druid ), TRUE, TRUE, TRUE );
    } else {
	gnome_druid_set_buttons_sensitive( GNOME_DRUID( druid ), TRUE, FALSE, TRUE );
    }
}

static gboolean user_next_cb( GnomeDruidPage *user, GnomeDruid *druid )
{
    #define PAININASS( key ) (g_strdup( gtk_entry_get_text( GTK_ENTRY( \
	gtk_object_get_data( GTK_OBJECT(gtk_object_get_data( GTK_OBJECT(user), "table" )), key )) ) ))

    if( balsa_app.address->personal )
	g_free( balsa_app.address->personal );
    balsa_app.address->personal = PAININASS( "name" );
    
    if( balsa_app.address->mailbox )
	g_free( balsa_app.address->mailbox );
    balsa_app.address->mailbox = PAININASS( "email" );

    if( balsa_app.smtp_server );
	g_free( balsa_app.smtp_server );
    balsa_app.smtp_server = PAININASS( "smtp" );

    #undef PAININASS

    return generic_next_cb( user, druid );
}

/* ********************************************************************** */

static gboolean folder_next_cb( GnomeDruidPage *folder, GnomeDruid *druid )
{
    GtkWidget *maildir;
    gchar *path, *buf;

    maildir = gtk_object_get_data( GTK_OBJECT( folder ), "maildir" );
    path = gtk_entry_get_text( GTK_ENTRY( maildir ) ); /*Reference, not copy*/

    if( !dir_exists( path, &buf ) ) {
	GtkWidget *widget;
	GnomeDruidPageStandard *next;

	next = (GnomeDruidPageStandard *)
	    gnome_druid_page_standard_new_with_vals( _("Create Mail Directory?"), 
						     (GdkImlibImage *) (gtk_object_get_data( GTK_OBJECT( druid ), "logo_imlib" )) );
	
	widget = gtk_label_new( _("The mailbox directory you specified either does not exist"
				  " or is not properly accessible. If you click the \"Next\""
				  " button, I will attempt to create it. If you click \"Back\","
				  " you can change the directory. If you think the directory should"
				  " work, check the error message below, fix the problem, click"
				  " \"Back\", then try \"Next\" again." ) );
	gtk_label_set_line_wrap( GTK_LABEL( widget ), TRUE );
	gtk_box_pack_start( GTK_BOX( (GNOME_DRUID_PAGE_STANDARD( next ))->vbox ), widget, TRUE, TRUE, 8 );

	if( !buf ) {
	    widget = gtk_label_new( _("There is a serious internal error.\n") );
	} else {
	    widget = gtk_label_new( buf );
	    g_free( buf );
	}
	gtk_label_set_line_wrap( GTK_LABEL( widget ), TRUE );
	gtk_box_pack_start( GTK_BOX( (GNOME_DRUID_PAGE_STANDARD( next ))->vbox ), widget, TRUE, TRUE, 8 );

	insert_page_after( GNOME_DRUID_PAGE( folder ), GNOME_DRUID_PAGE( next ), GNOME_DRUID( druid ) );
	gtk_signal_connect( GTK_OBJECT( next ), "next", GTK_SIGNAL_FUNC( create_folder_next_cb ), NULL );
	gtk_object_set_data( GTK_OBJECT( next ), "maildir", maildir );
    } else {
	gtk_object_set_data( GTK_OBJECT( folder ), "next", gtk_object_get_data( GTK_OBJECT( folder ), "success_next" ) );
	balsa_app.local_mail_directory = g_strdup( path );
    }

    return generic_next_cb( folder, druid );
}

/* ********************************************************************** */

static gboolean create_folder_next_cb( GnomeDruidPage *page, GnomeDruid *druid )
{
    GtkWidget *maildir;
    gchar *path;

    maildir = gtk_object_get_data( GTK_OBJECT( page ), "maildir" );
    path = gtk_entry_get_text( GTK_ENTRY( maildir ) );

    if( mkdir( path, 0700 ) < 0 ) {
	GnomeDruidPageStandard *next;
	GtkWidget *widget;
	gchar *buf;

	next = (GnomeDruidPageStandard *)
	    gnome_druid_page_standard_new_with_vals( _("Error Creating Directory"),
						     (GdkImlibImage *) gtk_object_get_data( GTK_OBJECT( druid ), "logo_imlib" ) );

	widget = gtk_label_new( _("There was an error creating the folder, detailed below."
				  " You cannot continue unless your mail folder exists."
				  " Either go back and choose a new folder or cancel this"
				  " setup and fix the problem." ) );
	gtk_label_set_line_wrap( GTK_LABEL( widget ), TRUE );
	gtk_box_pack_start( GTK_BOX( (GNOME_DRUID_PAGE_STANDARD( next ))->vbox ), widget, TRUE, TRUE, 4 );

	buf = g_strdup_printf( "Errno: %d (%s)", errno, g_strerror( errno ) );
	widget = gtk_label_new( buf );
	gtk_label_set_line_wrap( GTK_LABEL( widget ), TRUE );
	gtk_box_pack_start( GTK_BOX( (GNOME_DRUID_PAGE_STANDARD( next ))->vbox ), widget, TRUE, TRUE, 8 );

	insert_page_after( GNOME_DRUID_PAGE( page ), GNOME_DRUID_PAGE( next ), GNOME_DRUID( druid ) );
	gnome_druid_set_buttons_sensitive( GNOME_DRUID( druid ), TRUE, FALSE, TRUE );
    } else {
	balsa_app.local_mail_directory = g_strdup( path );
    }

    return generic_next_cb( page, druid );
}

/* ********************************************************************** */

static void dirsel_prepare_cb( GnomeDruidPage *page, GnomeDruid *druid )
{
    DirConfState *state;
    gchar *buf;
    GtkWidget *widget;

    state = gtk_object_get_data( GTK_OBJECT( page ), "state" );

    buf = g_strdup_printf( _("Configure %s"), state->uf_name );
    gnome_druid_page_standard_set_title( GNOME_DRUID_PAGE_STANDARD( page ), buf );
    g_free( buf );

    widget = gtk_object_get_data( GTK_OBJECT( page ), "label" );
    buf = g_strdup_printf( _("Please specify a filename for mail folder \"%s\"."
			     " (It will be created if it does not exist.)"), 
			     state->uf_name );
    gtk_widget_hide( GTK_WIDGET( widget ) );
    gtk_label_set_text( GTK_LABEL( widget ), buf );
/*    gtk_widget_queue_draw( GTK_WIDGET( widget ) ); */
    gtk_widget_show( GTK_WIDGET( widget ) );
    g_free( buf );

    widget = gtk_object_get_data( GTK_OBJECT( page ), "entry" );
    if( state->path ) {
	buf = g_strdup( state->path ); 
    } else {
	if( strcmp( state->uf_name, "Inbox" ) == 0 ) { /*AIIEEEEEEEE*/
	    char *spool;
	    spool = getenv( "MAIL" );
	    if( spool )
		buf = g_strdup( spool );
	    else
		buf = NULL;
	} else {
	    buf = g_strdup_printf( "%s/%s", balsa_app.local_mail_directory, state->uf_name );
	}
    }
    gtk_widget_hide( GTK_WIDGET( widget ) );
    gtk_entry_set_text( GTK_ENTRY( widget ), buf );
    gtk_widget_show( GTK_WIDGET( widget ) );
    g_free( buf );

    gtk_widget_show_all( GTK_WIDGET( page ) );
    gtk_object_set_data( GTK_OBJECT( page ), "next_success", page );
}

static gboolean dirsel_next_cb( GnomeDruidPage *page, GnomeDruid *druid )
{
    DirConfState *state;
    GtkWidget *widget;

    state = gtk_object_get_data( GTK_OBJECT( page ), "state" );

    if( state->path )
	g_free( state->path );
    state->path = g_strdup( gtk_entry_get_text( GTK_ENTRY( gtk_object_get_data( GTK_OBJECT( page ), "entry" ) ) ) );

    if( mailbox_valid( state->path ) == MAILBOX_UNKNOWN ) {
	widget = gtk_object_get_data( GTK_OBJECT( page ), "askpage" );
	
	if( !widget ) {
	    GnomeDruidPage *next;
	    GtkWidget *label;

	    next = (GnomeDruidPage *)
		gnome_druid_page_standard_new_with_vals( _("Create Mailbox?"),
						   gtk_object_get_data( GTK_OBJECT( druid ), "logo_imlib" ) );
	    gtk_object_set_data( GTK_OBJECT( next ), "entrypage", page );
	    gtk_object_set_data( GTK_OBJECT( page ), "askpage", next );
	    gtk_signal_connect( GTK_OBJECT( next ), "prepare", GTK_SIGNAL_FUNC( create_mbox_prepare_cb ), NULL );
	    gtk_signal_connect( GTK_OBJECT( next ), "next", GTK_SIGNAL_FUNC( create_mbox_next_cb ), NULL );

	    label = gtk_label_new( _("OOPS BUG" ) );
	    gtk_label_set_line_wrap( GTK_LABEL( label ), TRUE );
	    gtk_box_pack_start( GTK_BOX( (GNOME_DRUID_PAGE_STANDARD( next ))->vbox ), GTK_WIDGET( label ),
				TRUE, TRUE, 4 );
	    gtk_object_set_data( GTK_OBJECT( next ), "label", label );

	    insert_page_after( page, next, druid );
	    widget = GTK_WIDGET( next );
	}
	    
	gtk_object_set_data( GTK_OBJECT( widget ), "state", state );
	gnome_druid_set_page( GNOME_DRUID( druid ), GNOME_DRUID_PAGE( widget ) );
	return TRUE;
    } else {
	MailboxType type;
	Mailbox *mailbox;

	type = mailbox_valid( state->path );
	mailbox = BALSA_MAILBOX( mailbox_new( type ) );
	mailbox->name = g_strdup( state->uf_name );
	(MAILBOX_LOCAL( mailbox ))->path = g_strdup( state->path );
	config_mailbox_add( mailbox, state->uf_name );
	add_mailboxes_for_checking( mailbox );
	gtk_object_destroy( GTK_OBJECT( mailbox ) );
    }

    /* set_page returns if current == page */
    if( state->next ) {
	gtk_object_set_data( GTK_OBJECT( page ), "state", state->next );
	dirsel_prepare_cb( page, druid );
	gtk_widget_queue_draw( GTK_WIDGET( page ) );
    } else {
	gnome_druid_set_page( GNOME_DRUID( druid ), 
			      GNOME_DRUID_PAGE( gtk_object_get_data( GTK_OBJECT( page ), "next_done" ) ) );
    }

    return TRUE;
}

static gboolean dirsel_back_cb( GnomeDruidPage *page, GnomeDruid *druid )
{
    DirConfState *state, *iter;

    state = gtk_object_get_data( GTK_OBJECT( page ), "state" );

    /* Look at my flaming elegance!! */
    for( iter = &inbox; iter; iter = iter->next ) {
	if( iter->next == state )
	    break;
    }

    if( iter ) {
	gtk_object_set_data( GTK_OBJECT( page ), "state", iter );
/*	gnome_druid_set_page( GNOME_DRUID( druid ), GNOME_DRUID_PAGE( page ) );*/
/*	gnome_druid_set_page( druid, page ); */
	dirsel_prepare_cb( page, druid ); /*See above*/
	gtk_widget_queue_draw( GTK_WIDGET( page ) );
    } else {
	gnome_druid_set_page( GNOME_DRUID( druid ), 
			      GNOME_DRUID_PAGE( gtk_object_get_data( GTK_OBJECT( page ), "prev_orig" ) ) );
    }

    return TRUE;
}

/* ********************************************************************** */

static void create_mbox_prepare_cb( GnomeDruidPage *page, GnomeDruid *druid )
{
    GtkWidget *label;
    gchar *buf;
    DirConfState *state;

    state = gtk_object_get_data( GTK_OBJECT( page ), "state" );
    label = gtk_object_get_data( GTK_OBJECT( page ), "label" );

    buf = g_strdup_printf( _("The directory or file \"%s\" does not appear to exist,"
			   " or it is not a mailbox. Should I try to create it as"
			   " a mailbox? Click the \"Next\" button to try and create"
			   " it. Click \"Back\" to choose another filename."),
			   state->uf_name );
    gtk_widget_hide( GTK_WIDGET( label ) );
    gtk_label_set_text( GTK_LABEL( label ), buf );
    gtk_widget_show( GTK_WIDGET( label ) );
    gtk_widget_queue_draw( GTK_WIDGET( label ) );
    g_free( buf );

    gtk_object_set_data( GTK_OBJECT( page ), "next_success", gtk_object_get_data( GTK_OBJECT( page ), "entrypage" ) );
}

static gboolean create_mbox_next_cb( GnomeDruidPage *page, GnomeDruid *druid )
{
    DirConfState *state;
    int handle;

    state = gtk_object_get_data( GTK_OBJECT( page ), "state" );

    handle = creat( state->path, S_IRUSR | S_IWUSR );

    if( handle < 0 ) {
	    GnomeDruidPage *next;
	    GtkWidget *label;
	    gchar *buf;

	    next = (GnomeDruidPage *)
		gnome_druid_page_standard_new_with_vals( _("Mailbox Creation Failed"),
							 gtk_object_get_data( GTK_OBJECT( druid ), "logo_imlib" ) );
	    gtk_signal_connect( GTK_OBJECT( next ), "back", GTK_SIGNAL_FUNC( generic_back_cb ), NULL );

	    buf = g_strdup_printf( _("A mailbox could not be created at the filename \"%s\"."
				     " A more exact error is below. Please determine the error"
				     " and fix the problem, either by going backwards and"
				     " choosing a filename that will work, or exiting this setup"
				     " and fixing the problem elsewhere.\n"
				     "\n"
				     "Errno: %d (%s)" ),
				   state->path, errno, g_strerror( errno ) );
	    label = gtk_label_new( buf );
	    gtk_label_set_line_wrap( GTK_LABEL( label ), TRUE );
	    g_free( buf );
	    gtk_box_pack_start( GTK_BOX( (GNOME_DRUID_PAGE_STANDARD( next ))->vbox ), GTK_WIDGET( label ),
				TRUE, TRUE, 4 );

	    insert_page_after( page, next, druid );
	    gnome_druid_set_buttons_sensitive( druid, TRUE, FALSE, TRUE );
	    gtk_object_set_data( GTK_OBJECT( page ), "next", next );
    } else {
	GtkObject *nextpg = gtk_object_get_data( GTK_OBJECT( page ), "next_success" );
	MailboxType type;
	Mailbox *mailbox;

	close( handle );

	type = mailbox_valid( state->path );
	mailbox = BALSA_MAILBOX( mailbox_new( type ) );
	mailbox->name = g_strdup( state->uf_name );
	(MAILBOX_LOCAL( mailbox ))->path = g_strdup( state->path );
	config_mailbox_add( mailbox, state->uf_name );
	add_mailboxes_for_checking( mailbox );
	gtk_object_destroy( GTK_OBJECT( mailbox ) );

	if( state->next ) {
	    gtk_object_set_data( nextpg, "state", state->next );
	    gtk_object_set_data( GTK_OBJECT( page ), "next", nextpg );
	} else {
	    gtk_object_set_data( GTK_OBJECT( page ), "next", gtk_object_get_data( nextpg, "next_done" ) );
	}
    }

    return generic_next_cb( page, druid );
}

/* ********************************************************************** */

/* See kludges too daring for the mortal man!!! */
static void entry_changed_cb( GtkEntry *entry )
{
    GtkWidget *page;
    guint mymask;
    guint pagemask;

    page = gtk_object_get_data( GTK_OBJECT( entry ), "page" );
    mymask = GPOINTER_TO_INT( gtk_object_get_data( GTK_OBJECT( entry ), "mask" ) );
    pagemask = GPOINTER_TO_INT( gtk_object_get_data( GTK_OBJECT( page ), "mask" ) );
    
    if( (GTK_ENTRY( entry ))->text_length ) {
	pagemask |= mymask;
    } else {
	pagemask &= (~mymask);
    }

    gtk_object_set_data( GTK_OBJECT( page ), "mask", GINT_TO_POINTER( pagemask ) );

    page = gtk_object_get_data( GTK_OBJECT( entry ), "druid" ); /*page is now the druid*/
    if( (pagemask & SET_ALL_DONE) == SET_ALL_DONE ) {
	gnome_druid_set_buttons_sensitive( GNOME_DRUID( page ),
					   TRUE, TRUE, TRUE );
    } else {
	gnome_druid_set_buttons_sensitive( GNOME_DRUID( page ),
					   TRUE, FALSE, TRUE );
    }
}

/* ********************************************************************** */

static void druid_destroy_cb( GnomeDruid *druid )
{
    /* We don't need to set balsa_app data because it's
       set in the various pages' next callbacks. */
    gtk_widget_destroy( GTK_WIDGET( druid ) );
    gtk_main_quit();
}

static void druid_cancel_cb( GnomeDruid *druid )
{
    GtkWidget *dialog;

    dialog = gnome_question_dialog_modal( _("This will exit Balsa. Do you\n"
					    "really want to do this?"), 
					  (GnomeReplyCallback) really_cb, druid );
    gnome_dialog_run( GNOME_DIALOG( dialog ) );
}

static void really_cb( gint reply, GnomeDruid *druid )
{
    if( reply == GNOME_YES ) {
	druid_destroy_cb( druid );
	gtk_exit( 0 );
/*	gtk_main_quit();
	balsa_exit();*/
    }
}

/* ********************************************************************** */

static void finish_cb( GnomeDruidPage *page, GnomeDruid *druid )
{
    druid_destroy_cb( druid );
}

/* ********************************************************************** */

static GtkWidget *new_user_table( GnomeDruidPage *page, GnomeDruid *druid )
{
    GtkWidget *table;
    GtkWidget *widget;
    int i;
    static const gchar *strings[3] = { "Name:", "Email address:", "SMTP server:" };
    static const gchar *ids[3] = { "name", "email", "smtp" };
    gchar *presets[3];

    table = gtk_table_new( 3, 2, FALSE );

    presets[0] = g_strdup( g_get_real_name() );
    {
	char hostbuf[512];
	gethostname( hostbuf, 511 );
	presets[1] = g_strconcat( g_get_user_name(), "@", hostbuf, NULL );
    }
    presets[2] = g_strdup( "localhost" );

    for( i = 0; i < 3; i++ ) {
	widget = gtk_label_new( strings[i] );
	gtk_label_set_justify( GTK_LABEL( widget ), GTK_JUSTIFY_RIGHT );
	gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( widget ), 
			  0, 1, i, i + 1,
			  GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 
			  8, 4 );
	gtk_widget_show( GTK_WIDGET( widget ) );

	widget = gtk_entry_new();
	gtk_entry_set_text( GTK_ENTRY( widget ), presets[i] );
	g_free( presets[i] );
	gtk_signal_connect( GTK_OBJECT( widget ), "changed", entry_changed_cb, NULL );
	gtk_object_set_data( GTK_OBJECT( widget ), "page", page );
	gtk_object_set_data( GTK_OBJECT( widget ), "druid", druid );
	gtk_object_set_data( GTK_OBJECT( widget ), "mask", GINT_TO_POINTER(1 << i) ); /*wowee, this is ugly*/
	gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( widget ),
			  1, 2, i, i + 1,
			  GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL,
			  8, 4 );
	gtk_object_set_data( GTK_OBJECT( table ), ids[i], widget );
	entry_changed_cb( GTK_ENTRY( widget ) );
	gtk_widget_show( GTK_WIDGET( widget ) );
    }

    return table;
}

static GtkWidget *new_folder_table( GnomeDruidPage *page, GnomeDruid *druid )
{
    GtkWidget *table;
    GtkWidget *widget;
    gchar *maildir;

    table = gtk_table_new( 2, 2, FALSE );

    widget = gtk_label_new( _("I would now like you to tell me where your mail"
			      " directories are. The directory below will be contain"
			      " your Balsa mailboxes, and be searched for new mailboxes."
			      " storing your Balsa mail. It does not necessarily"
			      " have to exist (yet).\n"
			      "\n"
			      "There are five folders that you will be asked to"
			      " set up; if they do not exist, they will be created."
			      " However, you must have all five of them by the time"
			      " setup is finished.\n" ) );
    gtk_label_set_line_wrap( GTK_LABEL( widget ), TRUE );

    gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( widget ), 
		      0, 2, 0, 1,
		      GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 
		      2, 8 );
    gtk_widget_show( GTK_WIDGET( widget ) );


    widget = gtk_label_new( _("Your mailbox directory:" ) );
    gtk_label_set_justify( GTK_LABEL( widget ), GTK_JUSTIFY_RIGHT );
    gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( widget ), 
		      0, 1, 1, 2,
		      GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 
		      2, 4 );
    gtk_widget_show( GTK_WIDGET( widget ) );

    widget = gtk_entry_new();
    maildir = g_strconcat( g_get_home_dir(), "/balsa", NULL );
    gtk_entry_set_text( GTK_ENTRY( widget ), maildir );
    g_free( maildir );
    gtk_object_set_data( GTK_OBJECT( page ), "maildir", widget );
    gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( widget ), 
		      1, 2, 1, 2,
		      GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 
		      2, 4 );
    gtk_widget_show( GTK_WIDGET( widget ) );

    return table;
}

static GdkImlibImage *get_png( const gchar *file )
{
    GdkImlibImage *img;
    gchar *path;

    path = gnome_pixmap_file( file );
    if( !path )
	return NULL;
    img = gdk_imlib_load_image( path );
    g_free( path );
    return img;
}

static gboolean dir_exists( gchar *path, gchar **buf )
{
    struct stat sb;
    g_return_val_if_fail( path, FALSE );

    if( lstat( path, &sb ) < 0 ) {
	(*buf) = g_strdup_printf( "The file \"%s\" is not accessible (may not exist). Errno: %d (%s)",
				path, errno, g_strerror( errno ) );
	return FALSE;
    }
   
    if( !S_ISDIR( sb.st_mode ) ) {
	(*buf) = g_strdup_printf( "The file \"%s\" does not appear to be a directory. Mode: 0x%4X",
				   path, sb.st_mode );
	return FALSE;
    }

    if( access( path, R_OK | W_OK | X_OK ) < 0 ) {
	(*buf) = g_strdup_printf( "You do not have read, write, and execute permissions on the directory \"%s\".",
				   path );
	return FALSE;
    }

    (*buf) = NULL;
    return TRUE;
}

static void insert_page_after( GnomeDruidPage *cur, GnomeDruidPage *next, GnomeDruid *druid )
{
    gtk_object_set_data( GTK_OBJECT( next ), "prev", cur );
    gtk_object_set_data( GTK_OBJECT( next ), "next", gtk_object_get_data( GTK_OBJECT( cur ), "next" ) );
    gtk_signal_connect( GTK_OBJECT( next ), "next", GTK_SIGNAL_FUNC( generic_next_cb ), NULL );
    gtk_signal_connect( GTK_OBJECT( next ), "back", GTK_SIGNAL_FUNC( generic_back_cb ), NULL );
    gtk_widget_show_all( GTK_WIDGET( next ) );

    gtk_object_set_data( GTK_OBJECT( cur ), "next", next );

    gnome_druid_append_page( GNOME_DRUID( druid ), next );       	
}

/***********************************************************************
 **********************************************************************
 **********************************************************************/

#if 0

static void
text_realize_handler (GtkWidget * text, gpointer data)
{
  GString *str;

  str = g_string_new (_ ("Welcome to Balsa!\n\n"));

  str = g_string_append (str, _ ("You seem to be running Balsa for the first time.\n"));
  str = g_string_append (str, _ ("The following steps will setup Balsa by asking a few simple questions.  "));
  str = g_string_append (str, _ ("Once you have completed these steps, you can always change them at a later time through Balsa's preferences.  "));
  str = g_string_append (str, _ ("Please check the about box in Balsa's main window for more information on contacting the authors or reporting bugs."));

  gtk_text_freeze (GTK_TEXT (text));
  gtk_text_set_editable (GTK_TEXT (text), FALSE);
  gtk_text_set_word_wrap (GTK_TEXT (text), TRUE);
  gtk_text_insert (GTK_TEXT (text), NULL, NULL, NULL, str->str, strlen (str->str));
  gtk_text_thaw (GTK_TEXT (text));

  g_string_free (str, TRUE);
}

static GtkWidget *
create_welcome_page (void)
{
  GtkWidget *vbox;
  GtkWidget *text;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  text = gtk_text_new (NULL, NULL);
  gtk_box_pack_start (GTK_BOX (vbox), text, FALSE, FALSE, 5);
  gtk_widget_show (text);

  gtk_signal_connect (GTK_OBJECT (text), "realize", GTK_SIGNAL_FUNC (text_realize_handler), NULL);

  return vbox;
}

static GtkWidget *
create_general_page (void)
{
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *label;

  GString *str;
  char *name;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);
  gtk_widget_show (vbox);


  table = gtk_new_user_table (5, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  /* your name */
  label = gtk_label_new (_ ("Your name:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);

  prefs->real_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), prefs->real_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
  gtk_widget_show (prefs->real_name);

  /* email address */
  label = gtk_label_new (_ ("E-Mail address:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  prefs->email = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), prefs->email, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
  gtk_widget_show (prefs->email);

  /* smtp server */
  label = gtk_label_new (_ ("SMTP server:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  prefs->smtp_server = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), prefs->smtp_server, 1, 2, 2, 3,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
  gtk_widget_show (prefs->smtp_server);

  str = g_string_new (g_get_user_name ());
  g_string_append_c (str, '@');

  g_string_append (str, g_get_host_name ());
  gtk_entry_set_text (GTK_ENTRY (prefs->email), str->str);
  g_string_free (str, TRUE);

  name = g_get_real_name ();
  if (name != NULL)
    {
      char *p;

      /* Don't include other fields of the GECOS */
      p = strchr (name, ',');
      if (p != NULL)
	*p = '\0';

      gtk_entry_set_text (GTK_ENTRY (prefs->real_name), name);
    }

  gtk_entry_set_text (GTK_ENTRY (prefs->smtp_server), "localhost");

  return vbox;
}

static GtkWidget *
create_mailboxes_page (void)
{
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *label;
  GString *gs;
  gchar *spool;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  table = gtk_table_nw(5, 2, FALSE);
  gtk_widget_show (table);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);

  label = gtk_label_new (_ ("Inbox Path:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 10, 10);
  prefs->inbox = gtk_entry_new ();
  spool = getenv ("MAIL");
  if (spool)
    gtk_entry_set_text (GTK_ENTRY (prefs->inbox), spool);
  gtk_table_attach (GTK_TABLE (table), prefs->inbox, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  gs = g_string_new (g_get_home_dir ());
  gs = g_string_append (gs, "/Mail/outbox");

  label = gtk_label_new (_ ("Outbox Path:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL, 10, 10);
  prefs->outbox = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (prefs->outbox), gs->str);
  gtk_table_attach (GTK_TABLE (table), prefs->outbox, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  gs = g_string_truncate (gs, 0);
  gs = g_string_append (gs, g_get_home_dir ());
  gs = g_string_append (gs, "/Mail/sentbox");
  label = gtk_label_new (_ ("Sentbox Path:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		    GTK_FILL, GTK_FILL, 10, 10);
  prefs->sentbox = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (prefs->sentbox), gs->str);
  gtk_table_attach (GTK_TABLE (table), prefs->sentbox, 1, 2, 2, 3,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  gs = g_string_truncate (gs, 0);
  gs = g_string_append (gs, g_get_home_dir ());
  gs = g_string_append (gs, "/Mail/draftbox");
  label = gtk_label_new (_ ("Draftbox Path:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
		    GTK_FILL, GTK_FILL, 10, 10);
  prefs->draftbox = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (prefs->draftbox), gs->str);
  gtk_table_attach (GTK_TABLE (table), prefs->draftbox, 1, 2, 3, 4,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  gs = g_string_truncate (gs, 0);
  gs = g_string_append (gs, g_get_home_dir ());
  gs = g_string_append (gs, "/Mail/trash");
  label = gtk_label_new (_ ("Trash Path:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 4, 5,
		    GTK_FILL, GTK_FILL, 10, 10);
  prefs->trash = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (prefs->trash), gs->str);
  gtk_table_attach (GTK_TABLE (table), prefs->trash, 1, 2, 4, 5,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  g_string_free (gs, TRUE);

  label = gtk_label_new (_ ("If you wish to use IMAP for these\nplease change them inside Balsa\n"));
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  gtk_widget_show_all (vbox);

  return vbox;
}

static gint
delete_init_window (GtkWidget * widget, gpointer data)
{
  printf ("we are deleting the window, not saving, lets quit now\n");
  balsa_exit ();
  return FALSE;
}

/* arp
 * We need to check whether the parent directories also exist. If not, they
 * need to be made too. We first try to create the mailbox if it doesn't
 * exist. If that fails, we check how much of the path is missing and
 * create the missing bits before trying to make the mailbox again.
 *
 * Return 0 if all missing parents were created, else !0.
 */
static int
make_parents (gchar * filename)
{
  struct stat st;
  gchar *dir = NULL;
  gchar *pathname = NULL;
  gint i = 1;			/* skip the initial / cause we don't care about it */
  gint len;

  pathname = g_strdup (filename);

  len = strlen (pathname);

  while (i < len)
    {
      if (pathname[i] == '/')
	{
	  dir = g_strndup (pathname, i);
	  if (stat (dir, &st) != 0)
	    {
	      int ret;
	      ret = mkdir (dir, S_IRUSR | S_IWUSR | S_IXUSR);
	      if (ret == -1)
		{
		  g_error ("Error creating directory %s: %s",
			   dir, strerror (errno));
		  return FALSE;
		}
	    }
	}
      i++;
    }

  if (stat (pathname, &st) != 0)
    {
      int ret;
      ret = mkdir (pathname, S_IRUSR | S_IWUSR | S_IXUSR);
      if (ret == -1)
	{
	  g_error ("Error creating directory %s: %s",
		   pathname, strerror (errno));
	  return FALSE;
	}
    }

  g_free (dir);
  g_free (pathname);

  return TRUE;
}


/* Check to see if the specified file exists; if it doesn't, try to
   create it */
static void
create_mailbox_if_not_present (gchar * filename)
{
  gchar *dir;
  dir = g_dirname (filename);

/* Make the as much of the path as required. */
  if (make_parents (dir))
    {
      int fd = creat (filename, S_IRUSR | S_IWUSR);
      if (fd == 1)
	{
	  /* FIXME: Complain fiercely!  */
	}
      else
	close (fd);
    }
  g_free (dir);
}				/* create_mailbox_if_not_present */

static void
check_mailboxes_for_finish (GtkWidget * widget, gpointer data)
{
  GtkWidget *ask;
  GString *str;
  gchar *mbox;
  gint clicked_button;

  str = g_string_new (NULL);

  mbox = gtk_entry_get_text (GTK_ENTRY (prefs->inbox));
  if (mailbox_valid (mbox) == MAILBOX_UNKNOWN)
    {
      g_string_sprintf (str, _("Mailbox \"%s\" is not valid.\n\nWould you like to create it?"), mbox);
      goto BADMAILBOX;
    }

  mbox = gtk_entry_get_text (GTK_ENTRY (prefs->outbox));
  if (mailbox_valid (mbox) == MAILBOX_UNKNOWN)
    {
      g_string_sprintf (str, _("Mailbox \"%s\" is not valid.\n\nWould you like to create it?"), mbox);
      goto BADMAILBOX;
    }

  mbox = gtk_entry_get_text (GTK_ENTRY (prefs->sentbox));
  if (mailbox_valid (mbox) == MAILBOX_UNKNOWN)
    {
      g_string_sprintf (str, _("Mailbox \"%s\" is not valid.\n\nWould you like to create it?"), mbox);
      goto BADMAILBOX;
    }

  mbox = gtk_entry_get_text (GTK_ENTRY (prefs->draftbox));
  if (mailbox_valid (mbox) == MAILBOX_UNKNOWN)
    {
      g_string_sprintf (str, _("Mailbox \"%s\" is not valid.\n\nWould you like to create it?"), mbox);
      goto BADMAILBOX;
    }

  mbox = gtk_entry_get_text (GTK_ENTRY (prefs->trash));
  if (mailbox_valid (mbox) == MAILBOX_UNKNOWN)
    {
      g_string_sprintf (str, _("Mailbox \"%s\" is not valid.\n\nWould you like to create it?"), mbox);
      goto BADMAILBOX;
    }
  else
    {
      g_string_free (str, TRUE);
      complete_cb (widget, data);
      return;
    }


BADMAILBOX:
  ask = gnome_message_box_new (str->str,
			       GNOME_MESSAGE_BOX_QUESTION,
			       GNOME_STOCK_BUTTON_YES,
			       GNOME_STOCK_BUTTON_NO,
			       NULL);
  clicked_button = gnome_dialog_run (GNOME_DIALOG (ask));
  g_string_free (str, TRUE);
  if (clicked_button == 0)
    {
      create_mailbox_if_not_present (mbox);
      check_mailboxes_for_finish (widget, data);
      return;
    }
  else
    {
      ask = gnome_message_box_new (_("Unable to procede without a valid mailbox.  Please try again."),
				   GNOME_MESSAGE_BOX_ERROR,
				   GNOME_STOCK_BUTTON_OK,
				   NULL);
    }
}

static void
next_cb (GtkWidget * widget, gpointer data)
{
  switch (gtk_notebook_get_current_page (GTK_NOTEBOOK (iw->notebook)) + 1)
    {
    case IW_PAGE_FINISHED:
      check_mailboxes_for_finish (widget, data);
      break;

    case IW_PAGE_MBOXS:
      {
	GtkWidget *pixmap;
	gtk_widget_destroy (iw->next);
	pixmap = gnome_stock_pixmap_widget (NULL, GNOME_STOCK_PIXMAP_SAVE);
	iw->next = gnome_pixmap_button (pixmap, _ ("Finish"));
	gtk_container_add (GTK_CONTAINER (iw->bbox), iw->next);
	gtk_signal_connect (GTK_OBJECT (iw->next), "clicked",
			    (GtkSignalFunc) next_cb, NULL);
	gtk_widget_show (iw->next);

	gtk_notebook_next_page (GTK_NOTEBOOK (iw->notebook));
      }
      break;

    default:
      gtk_widget_set_sensitive (iw->prev, TRUE);
      gtk_notebook_next_page (GTK_NOTEBOOK (iw->notebook));
      break;
    }
  return;
}

static void
prev_cb (GtkWidget * widget, gpointer data)
{
  gtk_widget_destroy (iw->next);
  iw->next = gnome_stock_button (GNOME_STOCK_BUTTON_NEXT);
  gtk_container_add (GTK_CONTAINER (iw->bbox), iw->next);
  gtk_signal_connect (GTK_OBJECT (iw->next), "clicked",
		      (GtkSignalFunc) next_cb, NULL);
  gtk_widget_show (iw->next);

  gtk_notebook_prev_page (GTK_NOTEBOOK (iw->notebook));
  gtk_widget_set_sensitive (iw->next, TRUE);
  if (gtk_notebook_get_current_page (GTK_NOTEBOOK (iw->notebook)) == IW_PAGE_WELCOME)
    gtk_widget_set_sensitive (iw->prev, FALSE);
}

static void
complete_cb (GtkWidget * widget, gpointer data)
{
  GString *gs;
  Mailbox *mailbox;
  MailboxType type;

  g_free (balsa_app.address->personal);
  balsa_app.address->personal = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->real_name)));

  g_free (balsa_app.address->mailbox);
  balsa_app.address->mailbox = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->email)));

  g_free (balsa_app.smtp_server);
  balsa_app.smtp_server = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->smtp_server)));

  gs = g_string_new (g_get_home_dir ());
  gs = g_string_append (gs, "/Mail");
  balsa_app.local_mail_directory = g_strdup (gs->str);
  g_string_free (gs, TRUE);

  type = mailbox_valid (gtk_entry_get_text (GTK_ENTRY (prefs->inbox)));

  mailbox = BALSA_MAILBOX(mailbox_new(type));
  mailbox->name = g_strdup (_("Inbox"));
  MAILBOX_LOCAL (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->inbox)));
  config_mailbox_add (mailbox, "Inbox");
  add_mailboxes_for_checking (mailbox);
  gtk_object_destroy(GTK_OBJECT(mailbox));

  type = mailbox_valid (gtk_entry_get_text (GTK_ENTRY (prefs->inbox)));
  mailbox = BALSA_MAILBOX(mailbox_new(type));
  mailbox->name = g_strdup (_("Outbox"));
  MAILBOX_LOCAL (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->outbox)));
  config_mailbox_add (mailbox, "Outbox");
  add_mailboxes_for_checking (mailbox);
  gtk_object_destroy(GTK_OBJECT(mailbox));

  type = mailbox_valid (gtk_entry_get_text (GTK_ENTRY (prefs->sentbox)));
  mailbox = BALSA_MAILBOX(mailbox_new(type));
  mailbox->name = g_strdup (_("Sentbox"));
  MAILBOX_LOCAL (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->sentbox)));
  config_mailbox_add (mailbox, "Sentbox");
  add_mailboxes_for_checking (mailbox);
  gtk_object_destroy(GTK_OBJECT(mailbox));

  type = mailbox_valid (gtk_entry_get_text (GTK_ENTRY (prefs->draftbox)));
  mailbox = BALSA_MAILBOX(mailbox_new(type));
  mailbox->name = g_strdup (_("Draftbox"));
  MAILBOX_LOCAL (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->draftbox)));
  config_mailbox_add (mailbox, "Draftbox");
  add_mailboxes_for_checking (mailbox);
  gtk_object_destroy(GTK_OBJECT(mailbox));

  type = mailbox_valid (gtk_entry_get_text (GTK_ENTRY (prefs->trash)));
  mailbox = BALSA_MAILBOX(mailbox_new(type));
  mailbox->name = g_strdup (_("Trash"));
  MAILBOX_LOCAL (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->trash)));
  config_mailbox_add (mailbox, "Trash");
  add_mailboxes_for_checking (mailbox);
  gtk_object_destroy(GTK_OBJECT(mailbox));

  config_global_save ();

  gtk_widget_destroy (prefs->real_name);
  gtk_widget_destroy (prefs->email);
  gtk_widget_destroy (prefs->smtp_server);

  gtk_widget_destroy (prefs->inbox);
  gtk_widget_destroy (prefs->sentbox);
  gtk_widget_destroy (prefs->draftbox);
  gtk_widget_destroy (prefs->outbox);
  gtk_widget_destroy (prefs->trash);

  g_free (prefs);

  gtk_widget_destroy (iw->next);
  gtk_widget_destroy (iw->prev);
  gtk_widget_destroy (iw->notebook);
  gtk_widget_destroy (iw->window);
  g_free (iw);

  /* end the initialize balsa main loop */
  gtk_main_quit ();
}

#endif
