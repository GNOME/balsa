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
    config_global_save();
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
