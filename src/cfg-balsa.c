/* Balsa E-Mail Client
 *
 * This file handles Balsa's configuration information.
 *
 * This file is Copyright (C) 1998-1999 Nat Friedman
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

#include <unistd.h>
#include <sys/param.h> /*MAXHOSTNAMELEN*/

#include "libbalsa/misc.h"
#include "balsa-app.h"
#include "cfg-backend.h"
#include "cfg-engine.h"
#include "cfg-offsets.h" /*OFFSET HEADER <- do not edit this*/
#include "cfg-memory-widgets.h"

/* OFFSET DEF[[ #include "libbalsa.h" ]] */
/* OFFSET DEF[[ #include "balsa-app.h" ]] */
/* OFFSET DEF[[ #include "sm-balsa.h" ]] */
/* OFFSET DEF[[ typedef struct BalsaApplication BalsaApp; ]] */

/* Implement all of our metatypes. These are mostly support functions that
 * wrap around calls to cfg_group_{read,write}, but some transform their
 * data.
 *
 * The two important types are BalsaApp and MailboxArray. BalsaApp records
 * balsa_app and all the user's preferences. MailboxArray writes the 
 * current mailbox setup, recording all POP3, local, and IMAP servers.
 *
 * MailboxArray is really only meant to be read on startup. Reading it
 * in the middle of a session will be BAD. Very bad.
 *
 * We provide some other functions: cfg_mailbox_{read,write,delete} take
 * care of any mailbox, wrapping around the cfg_Mailbox{Local,POP3,IMAP}_
 * functions. cfg_save and cfg_load write Balsa's complete state. The
 * cfg_mailbox_*_simple functions don't require the location parameter,
 * and instead assume that the mailboxes are stored below /Mailboxes.
 */

/* ************************************************************************ */

gboolean cfg_meta_GdkColor_write( const GdkColor *color, const cfg_location_t *loc, gpointer typedata );
gboolean cfg_meta_GdkColor_read( GdkColor *color, const cfg_location_t *loc, gpointer typedata );

gboolean cfg_meta_Printing_t_write( const Printing_t *pt, const cfg_location_t *loc, gpointer typedata );
gboolean cfg_meta_Printing_t_read( Printing_t *pt, const cfg_location_t *loc, gpointer typedata );

gboolean cfg_meta_Address_write( const Address *addy, const cfg_location_t *loc, gpointer typedata );
gboolean cfg_meta_Address_read( Address *addy, const cfg_location_t *loc, gpointer typedata );

gboolean cfg_meta_ServerType_write( const ServerType *serv, const cfg_location_t *loc, gpointer typedata );
gboolean cfg_meta_ServerType_read( ServerType *serv, const cfg_location_t *loc, gpointer typedata );

gboolean cfg_meta_NonObvious_write( const gchar **str, const cfg_location_t *loc, gpointer typedata );
gboolean cfg_meta_NonObvious_read( gchar **str, const cfg_location_t *loc, gpointer typedata );

gboolean cfg_meta_Server_write( const Server *serv, const cfg_location_t *loc, gpointer typedata );
gboolean cfg_meta_Server_read( Server *serv, const cfg_location_t *loc, gpointer typedata );

gboolean cfg_meta_Mailbox_write( const Mailbox *mb, const cfg_location_t *loc, gpointer typedata );
gboolean cfg_meta_Mailbox_read( Mailbox *mb, const cfg_location_t *loc, gpointer typedata );

gboolean cfg_meta_MailboxIMAP_write( const MailboxIMAP *mb, const cfg_location_t *loc, gpointer typedata );
gboolean cfg_meta_MailboxIMAP_read( MailboxIMAP *mb, const cfg_location_t *loc, gpointer typedata );

gboolean cfg_meta_MailboxPOP3_write( const MailboxPOP3 *mb, const cfg_location_t *loc, gpointer typedata );
gboolean cfg_meta_MailboxPOP3_read( MailboxPOP3 *mb, const cfg_location_t *loc, gpointer typedata );

gboolean cfg_meta_MailboxLocal_write( const MailboxLocal *mb, const cfg_location_t *loc, gpointer typedata );
gboolean cfg_meta_MailboxLocal_read( MailboxLocal *mb, const cfg_location_t *loc, gpointer typedata );

gboolean cfg_meta_MailboxArray_write( const gpointer data, const cfg_location_t *loc, gpointer typedata );
gboolean cfg_meta_MailboxArray_read( const gpointer data, const cfg_location_t *loc, gpointer typedata );

gboolean cfg_meta_BalsaApp_write( const struct BalsaApplication *bapp, const cfg_location_t *loc, gpointer typedata );
gboolean cfg_meta_BalsaApp_read( struct BalsaApplication *bapp, const cfg_location_t *loc, gpointer typedata );

gboolean cfg_meta_mblist_write( const GSList **list, const cfg_location_t *loc, gpointer typedata );
gboolean cfg_meta_mblist_read( GSList **list, const cfg_location_t *loc, gpointer typedata );

gboolean cfg_meta_state_write( const sm_state_t *state, const cfg_location_t *loc, gpointer typedata );
gboolean cfg_meta_state_read( sm_state_t *state, const cfg_location_t *loc, gpointer typedata );

gboolean cfg_mailbox_write( Mailbox *mb, const cfg_location_t *top );
Mailbox *cfg_mailbox_read( const gchar *name, const cfg_location_t *top );
gboolean cfg_mailbox_delete( Mailbox *mb, const cfg_location_t *top );

gboolean cfg_mailbox_write_simple( Mailbox *mb );
Mailbox *cfg_mailbox_read_simple( const gchar *name );
gboolean cfg_mailbox_delete_simple( Mailbox *mb );

gboolean cfg_save( void );
gboolean cfg_load( void );

/* ************************************************************************ */

const gchar mb_key[] = "Mailboxes";

/* ** GdkColor (in a very ugly manner) ************************************ */

/* GdkColor uses gshorts, and we don't want to write another basic type,
 * so we wrap GdkColor into our own structure that's all gints, so we
 * know it can be written.
 */

static const cfg_parm_t parms_BalsaFudgeColor[] = {
	{ "PixelValue", BALSA_OFFSET_BalsaFudgeColor_ELEM_pixel, CPT_NUM, NULL, cfg_type_const_init_num( 0, 0, 0, 0 ) },
	{ "Red", BALSA_OFFSET_BalsaFudgeColor_ELEM_red, CPT_NUM, NULL, cfg_type_const_init_num( 0, 0, 65536, CPNF_USEMIN | CPNF_USEMAX ) },
	{ "Green", BALSA_OFFSET_BalsaFudgeColor_ELEM_green, CPT_NUM, NULL, cfg_type_const_init_num( 0, 0, 65536, CPNF_USEMIN | CPNF_USEMAX ) },
	{ "Blue", BALSA_OFFSET_BalsaFudgeColor_ELEM_blue, CPT_NUM, NULL, cfg_type_const_init_num( 0, 0, 65536, CPNF_USEMIN | CPNF_USEMAX ) },
	cfg_parm_null
};

gboolean cfg_meta_GdkColor_write( const GdkColor *color, const cfg_location_t *loc, gpointer typedata )
{
	BalsaFudgeColor fc;

	fc.pixel = color->pixel;
	fc.red = color->red;
	fc.green = color->green;
	fc.blue = color->blue;

	return cfg_group_write( &fc, parms_BalsaFudgeColor, loc );
}

gboolean cfg_meta_GdkColor_read( GdkColor *color, const cfg_location_t *loc, gpointer typedata )
{
	BalsaFudgeColor fc;

	if( cfg_group_read( &fc, parms_BalsaFudgeColor, loc ) )
		return TRUE;

	color->pixel = fc.pixel;
	color->red = fc.red;
	color->green = fc.green;
	color->blue = fc.blue;
	return FALSE;
}

const cfg_metatype_spec_t spec_GdkColor = { (meta_writer) cfg_meta_GdkColor_write, (meta_reader) cfg_meta_GdkColor_read };

/* ** Printing_t ********************************************************** */

/* Just writes the structure. */

static const cfg_parm_t parms_Printing_t[] = {
	{ "Breakline", BALSA_OFFSET_Printing_t_ELEM_breakline, CPT_NUM, NULL, cfg_type_const_init_num( 0, 0, 1, CPNF_USEMIN | CPNF_USEMAX ) },
	{ "Linesize", BALSA_OFFSET_Printing_t_ELEM_linesize, CPT_NUM, NULL, cfg_type_const_init_num( DEFAULT_LINESIZE, 0, 0, CPNF_USEMIN ) },
	{ "PrintCommand", BALSA_OFFSET_Printing_t_ELEM_PrintCommand, CPT_STR, NULL, cfg_type_const_init_str( DEFAULT_PRINTCOMMAND, NULL, NULL ) },
	cfg_parm_null
};

gboolean cfg_meta_Printing_t_write( const Printing_t *pt, const cfg_location_t *loc, gpointer typedata )
{
	return cfg_group_write( (gconstpointer) pt, parms_Printing_t, loc );
}

gboolean cfg_meta_Printing_t_read( Printing_t *pt, const cfg_location_t *loc, gpointer typedata )
{
	return cfg_group_read( (gpointer) pt, parms_Printing_t, loc );
}

const cfg_metatype_spec_t spec_Printing_t = { (meta_writer) cfg_meta_Printing_t_write, (meta_reader) cfg_meta_Printing_t_read };

/* ** Address ************************************************************* */

/* Just writes the structure. Supplies defaults for its parameters. */

static gchar *fetch_realname( const gchar *parmname );
static gchar *fetch_emailaddy( const gchar *parmname );

static const cfg_parm_t parms_Address[] = {
	{ "PersonalName", BALSA_OFFSET_Address_ELEM_personal, CPT_STR, NULL, cfg_type_const_init_str( NULL, fetch_realname, NULL ) },
	{ "Email", BALSA_OFFSET_Address_ELEM_mailbox, CPT_STR, NULL, cfg_type_const_init_str( NULL, fetch_emailaddy, NULL ) },
	cfg_parm_null
};

static gchar *fetch_realname( const gchar *parmname )
{
	return g_strdup( g_get_real_name() );
}

static gchar *fetch_emailaddy( const gchar *parmname )
{
	char hostname[MAXHOSTNAMELEN], domainname[MAXHOSTNAMELEN];

	gethostname( hostname, MAXHOSTNAMELEN );
	getdomainname( domainname, MAXHOSTNAMELEN );
	return g_strconcat( g_get_user_name(), "@", hostname, ".", domainname, NULL );
}

gboolean cfg_meta_Address_write( const Address *addy, const cfg_location_t *loc, gpointer typedata )
{
	return cfg_group_write( (gconstpointer) addy, parms_Address, loc );
}

gboolean cfg_meta_Address_read( Address *addy, const cfg_location_t *loc, gpointer typedata )
{
	return cfg_group_read( (gpointer) addy, parms_Address, loc );
}

const cfg_metatype_spec_t spec_Address = { (meta_writer) cfg_meta_Address_write, (meta_reader) cfg_meta_Address_read };

/* ** ServerType ********************************************************** */

/* Typecast the enum to a gint so we know its size. */

gboolean cfg_meta_ServerType_write( const ServerType *serv, const cfg_location_t *loc, gpointer typedata )
{
	return cfg_location_put_num( loc, "Value", (gint) (*serv) );
}

gboolean cfg_meta_ServerType_read( ServerType *serv, const cfg_location_t *loc, gpointer typedata )
{
	int val = 0;

	if( cfg_location_get_num( loc, "Value", &val, 0 ) )
		return TRUE;
	(*serv) = (ServerType) val;
	return FALSE;
}
	
const cfg_metatype_spec_t spec_ServerType = { (meta_writer) cfg_meta_ServerType_write, (meta_reader) cfg_meta_ServerType_read };

/* ** NonObvious (rot-13 string) ****************************************** */

/* Write a string in a 'nonobvious' manner. This is NOT encryption, nor
 * is it supposed to be. It's intended to prevent accidental discovery
 * of passwords. Use chmod() to prevent people from reading your config
 * files!
 */

static gchar *nonobvious_xform( const gchar *val );

gboolean cfg_meta_NonObvious_write( const gchar **str, const cfg_location_t *loc, gpointer typedata )
{
	gchar *xformed;
	gboolean ret;

	xformed = nonobvious_xform( (*str) );
	ret = cfg_location_put_str( loc, "Value", xformed );
	g_free( xformed );
	return ret;
}

gboolean cfg_meta_NonObvious_read( gchar **str, const cfg_location_t *loc, gpointer typedata )
{
	gchar *xformed;

	if( cfg_location_get_str( loc, "Value", &xformed, "" ) )
		return TRUE;
	
	(*str) = nonobvious_xform( xformed );
	g_free( xformed );
	return FALSE;
}

static gchar *nonobvious_xform( const gchar *val )
{
	int len;
	int i;
	gchar *out;

	if( val == NULL )
		return g_strdup( "" );

	len = strlen( val );

	if( len == 0 )
		return g_strdup( "" );

	out = g_new( gchar, len );

	/* We assume 8-bit cleanliness.... */
	for( i = 0; i < len; i++ )
		out[i] = ~( val[ len - ( i + 1 ) ] );

	out[len] = '\0';
	return out;
}

const cfg_metatype_spec_t spec_NonObvious = { (meta_writer) cfg_meta_NonObvious_write, (meta_reader) cfg_meta_NonObvious_read };

/* ** Server ************************************************************** */

/* Just writes the structure. */

static const cfg_parm_t parms_Server[] = {
	/* This appears to be unused
	 * { "Name", BALSA_OFFSET_Server_ELEM_name, CPT_STR, NULL, cfg_type_const_init_str( NULL, NULL, NULL ) }, 
	 */
	{ "Host", BALSA_OFFSET_Server_ELEM_host, CPT_STR, NULL, cfg_type_const_init_str( NULL, NULL, NULL ) },
	{ "Port", BALSA_OFFSET_Server_ELEM_port, CPT_NUM, NULL, cfg_type_const_init_num( 0, 0, 65536, CPNF_USEMAX ) },
	{ "Username", BALSA_OFFSET_Server_ELEM_user, CPT_STR, NULL, cfg_type_const_init_str( NULL, NULL, NULL ) },
	{ "Type", BALSA_OFFSET_Server_ELEM_type, CPT_META, NULL, cfg_type_const_init_meta( &spec_ServerType, NULL, FALSE ) },
	{ "Password", BALSA_OFFSET_Server_ELEM_passwd, CPT_META, NULL, cfg_type_const_init_meta( &spec_NonObvious, NULL, FALSE ) },
	cfg_parm_null
};

gboolean cfg_meta_Server_write( const Server *serv, const cfg_location_t *loc, gpointer typedata )
{
	return cfg_group_write( (gconstpointer) serv, parms_Server, loc );
}

gboolean cfg_meta_Server_read( Server *serv, const cfg_location_t *loc, gpointer typedata )
{
	return cfg_group_read( (gpointer) serv, parms_Server, loc );
}

const cfg_metatype_spec_t spec_Server = { (meta_writer) cfg_meta_Server_write, (meta_reader) cfg_meta_Server_read };


/* ** Mailbox ************************************************************* */

/* Writes the data generic to all mailboxes.
 *
 * The 'special nature' stuff records whether this mailbox is one
 * of our special mailboxes (inbox, outbox, ...). If so, when
 * reading in the mailbox we set the appropriate fields in balsa_app.
 * 
 * Also inserts the box into mailbox_nodes if need be.
 */

typedef enum mailbox_special_mojo_e {
	MSM_INBOX =    (1 << 0),
	MSM_OUTBOX =   (1 << 1),
	MSM_SENTBOX =  (1 << 2),
	MSM_DRAFTBOX = (1 << 3),
	MSM_TRASH =    (1 << 4)
} mailbox_special_mojo_t;

gboolean cfg_meta_Mailbox_write( const Mailbox *mb, const cfg_location_t *loc, gpointer typedata )
{
	gint specialness = 0;

	if( cfg_location_put_str( loc, "Name", mb->name ) )
		return TRUE;

	if( balsa_app.inbox == mb )
		specialness |= MSM_INBOX;
	if( balsa_app.outbox == mb )
		specialness |= MSM_OUTBOX;
	if( balsa_app.sentbox == mb )
		specialness |= MSM_SENTBOX;
	if( balsa_app.draftbox == mb )
		specialness |= MSM_DRAFTBOX;
	if( balsa_app.trash == mb )
		specialness |= MSM_TRASH;

	if( cfg_location_put_num( loc, "SpecialNature", specialness ) )
		return TRUE;

	return FALSE;
}

gboolean cfg_meta_Mailbox_read( Mailbox *mb, const cfg_location_t *loc, gpointer typedata )
{
	gint specialness;

	if( cfg_location_get_str( loc, "Name", &( mb->name ), "[Unnamed mailbox]" ) )
		return TRUE;

	if( cfg_location_get_num( loc, "SpecialNature", &specialness, 0 ) )
		return TRUE;

	if( specialness == 0 && mb->type != MAILBOX_POP3 ) {
		GNode *node;

		if( mb->type == MAILBOX_MH )
			node = g_node_new( mailbox_node_new( g_strdup( mb->name ), mb, TRUE ) );
		else
			node = g_node_new( mailbox_node_new( g_strdup( mb->name ), mb, FALSE ) );

		g_node_append( balsa_app.mailbox_nodes, node );
	} else {
		if( specialness & MSM_INBOX )
			balsa_app.inbox = mb;
		if( specialness & MSM_OUTBOX )
			balsa_app.outbox = mb;
		if( specialness & MSM_SENTBOX )
			balsa_app.sentbox = mb;
		if( specialness & MSM_DRAFTBOX )
			balsa_app.draftbox = mb;
		if( specialness & MSM_TRASH )
			balsa_app.trash = mb;
	}

	return FALSE;
}

const cfg_metatype_spec_t spec_Mailbox = { (meta_writer) cfg_meta_Mailbox_write, (meta_reader) cfg_meta_Mailbox_read };

/* ** MailboxIMAP ********************************************************* */

/* Write the structure, and the generic mailbox data. */

static const cfg_parm_t parms_MailboxIMAP[] = {
	{ "Path", BALSA_OFFSET_MailboxIMAP_ELEM_path, CPT_STR, NULL, cfg_type_const_init_str( "INBOX", NULL, NULL ) },
	{ "TempDir", BALSA_OFFSET_MailboxIMAP_ELEM_tmp_file_path, CPT_STR, NULL, cfg_type_const_init_str( NULL, NULL, NULL ) },
	{ "Server", BALSA_OFFSET_MailboxIMAP_ELEM_server, CPT_META, NULL, cfg_type_const_init_meta( &spec_Server, NULL, TRUE ) },
	{ "MailboxData", 0, CPT_META, NULL, cfg_type_const_init_meta( &spec_Mailbox, NULL, FALSE ) },
	cfg_parm_null
};
	
gboolean cfg_meta_MailboxIMAP_write( const MailboxIMAP *mb, const cfg_location_t *loc, gpointer typedata )
{
	return cfg_group_write( (gconstpointer) mb, parms_MailboxIMAP, loc );
}

gboolean cfg_meta_MailboxIMAP_read( MailboxIMAP *mb, const cfg_location_t *loc, gpointer typedata )
{
	return cfg_group_read( (gpointer) mb, parms_MailboxIMAP, loc );
}

const cfg_metatype_spec_t spec_MailboxIMAP = { (meta_writer) cfg_meta_MailboxIMAP_write, (meta_reader) cfg_meta_MailboxIMAP_read };

/* ** MailboxPOP3 ********************************************************* */

/* Write the structure, and the generic mailbox data. 
 *
 * Adds the box to balsa_app.inbox_input.
 */

static const cfg_parm_t parms_MailboxPOP3[] = {
	{ "Check", BALSA_OFFSET_MailboxPOP3_ELEM_check, CPT_BOOL, NULL, cfg_type_const_init_bool( TRUE ) },
	{ "DeleteFromServer", BALSA_OFFSET_MailboxPOP3_ELEM_delete_from_server, CPT_BOOL, NULL, cfg_type_const_init_bool( FALSE ) },
	{ "LastPoppedUID", BALSA_OFFSET_MailboxPOP3_ELEM_last_popped_uid, CPT_STR, NULL, cfg_type_const_init_str( NULL, NULL, NULL ) },
	{ "Server", BALSA_OFFSET_MailboxPOP3_ELEM_server, CPT_META, NULL, cfg_type_const_init_meta( &spec_Server, NULL, TRUE ) },
	{ "MailboxData", 0, CPT_META, NULL, cfg_type_const_init_meta( &spec_Mailbox, NULL, FALSE ) },
	cfg_parm_null
};
	
gboolean cfg_meta_MailboxPOP3_write( const MailboxPOP3 *mb, const cfg_location_t *loc, gpointer typedata )
{
	return cfg_group_write( (gconstpointer) mb, parms_MailboxPOP3, loc );
}

gboolean cfg_meta_MailboxPOP3_read( MailboxPOP3 *mb, const cfg_location_t *loc, gpointer typedata )
{
	if( cfg_group_read( (gpointer) mb, parms_MailboxPOP3, loc ) )
		return TRUE;

	balsa_app.inbox_input = g_list_append( balsa_app.inbox_input, mb );
	return FALSE;
}

const cfg_metatype_spec_t spec_MailboxPOP3 = { (meta_writer) cfg_meta_MailboxPOP3_write, (meta_reader) cfg_meta_MailboxPOP3_read };

/* ** MailboxLocal ********************************************************* */

/* Write the structure, and the generic mailbox data. */

static const cfg_parm_t parms_MailboxLocal[] = {
	{ "Path", BALSA_OFFSET_MailboxLocal_ELEM_path, CPT_STR, NULL, cfg_type_const_init_str( "/bad/configuration/save", NULL, NULL ) },
	{ "MailboxData", 0, CPT_META, NULL, cfg_type_const_init_meta( &spec_Mailbox, NULL, FALSE ) },
	cfg_parm_null
};
	
gboolean cfg_meta_MailboxLocal_write( const MailboxLocal *mb, const cfg_location_t *loc, gpointer typedata )
{
	return cfg_group_write( (gconstpointer) mb, parms_MailboxLocal, loc );
}

gboolean cfg_meta_MailboxLocal_read( MailboxLocal *mb, const cfg_location_t *loc, gpointer typedata )
{
	return cfg_group_read( (gpointer) mb, parms_MailboxLocal, loc );
}

const cfg_metatype_spec_t spec_MailboxLocal = { (meta_writer) cfg_meta_MailboxLocal_write, (meta_reader) cfg_meta_MailboxLocal_read };

/* ** MailboxArray ******************************************************** */

/* Write: write the state of all of our mailboxes, looping through balsa_app.inbox etc,
 * mailbox_nodes, and inbox_input. This metatype doesn't correspond to a structure.
 *
 * Read: enumerate through the section to find all the mailboxes we saved. Create
 * the structure with mailbox_new, peeking at the path field of local mailboxes to
 * determine the type. The meta-mailbox functions will insert the new boxes into
 * Balsa's internal structures as needed.
 */

static void write_foreach( gpointer mb, gpointer data );
static gboolean write_traverse( GNode *node, gpointer data );
static void read_cb( const cfg_location_t *where, const gchar *name, const gboolean is_group, gpointer user_data );

gboolean cfg_meta_MailboxArray_write( const gpointer data, const cfg_location_t *loc, gpointer typedata )
{
	cfg_location_t *down;

	down = cfg_location_godown( loc, mb_key );

	/* Without this, no Mailboxes section is created, only subsections, and the
	 * test for the existence of [Mailboxes] fails. */
	cfg_location_put_bool( down, "DummyValue", FALSE );

	cfg_mailbox_write( balsa_app.inbox, down );
	cfg_mailbox_write( balsa_app.outbox, down );
	cfg_mailbox_write( balsa_app.sentbox, down );
	cfg_mailbox_write( balsa_app.draftbox, down );
	cfg_mailbox_write( balsa_app.trash, down );

	g_node_traverse( balsa_app.mailbox_nodes, G_IN_ORDER, G_TRAVERSE_ALL, -1, write_traverse, (gpointer) down );
	g_list_foreach( balsa_app.inbox_input, write_foreach, (gpointer) down );
	cfg_location_free( down );
	return FALSE;
}

gboolean cfg_meta_MailboxArray_read( const gpointer data, const cfg_location_t *loc, gpointer typedata )
{
	cfg_location_t *down;
       
	down = cfg_location_godown( loc, mb_key );
	cfg_location_enumerate( down, read_cb, NULL );
	cfg_location_free( down );
	return FALSE;
}

static void write_foreach( gpointer mb, gpointer data )
{
	if( !BALSA_IS_MAILBOX( mb ) )
		return;

	cfg_mailbox_write( MAILBOX( mb ), (const cfg_location_t *) data );
}

static gboolean write_traverse( GNode *node, gpointer data )
{
	MailboxNode *mbn;

	if( !node )
		return TRUE;

	mbn = node->data;

	if( mbn && mbn->mailbox )
		cfg_mailbox_write( mbn->mailbox, (const cfg_location_t *) data );
	return TRUE;
}

static void read_cb( const cfg_location_t *where, const gchar *name, const gboolean is_group, gpointer user_data )
{
	if( is_group == FALSE )
		return;

	cfg_mailbox_read( name, where );
}

const cfg_metatype_spec_t spec_MailboxArray = { (meta_writer) cfg_meta_MailboxArray_write, (meta_reader) cfg_meta_MailboxArray_read };

/* ** BalsaApp ************************************************************ */

/* Writes and reads the balsa_app, but is reponsible for managing
 * all user preferences. They're currently all inside balsa_app,
 * so this just writes and reads the structure.
 */

static gchar *fetch_replyto( const gchar *parmname );
static gchar *fetch_localmaildir( const gchar *parmname );
static gchar *fetch_sigpath( const gchar *parmname );

static const cfg_parm_t parms_BalsaApp[] = {
	{ "ReplyTo", BALSA_OFFSET_BalsaApp_ELEM_replyto, CPT_STR, NULL, cfg_type_const_init_str( NULL, fetch_replyto, NULL ) },
	{ "BCC", BALSA_OFFSET_BalsaApp_ELEM_bcc, CPT_STR, NULL, cfg_type_const_init_str( "", NULL, NULL ) },
	{ "LocalMailDir", BALSA_OFFSET_BalsaApp_ELEM_local_mail_directory, CPT_STR, NULL, cfg_type_const_init_str( NULL, fetch_localmaildir, NULL ) },
	{ "SMTPServer", BALSA_OFFSET_BalsaApp_ELEM_smtp_server, CPT_STR, NULL, cfg_type_const_init_str( DEFAULT_SMTP_SERVER, NULL, NULL ) },
	{ "SigOnSend", BALSA_OFFSET_BalsaApp_ELEM_sig_sending, CPT_BOOL, NULL, cfg_type_const_init_bool( TRUE ) },
	{ "SigOnForward", BALSA_OFFSET_BalsaApp_ELEM_sig_whenforward, CPT_BOOL, NULL, cfg_type_const_init_bool( TRUE ) },
	{ "SigOnReply", BALSA_OFFSET_BalsaApp_ELEM_sig_whenreply, CPT_BOOL, NULL, cfg_type_const_init_bool( TRUE ) },
	{ "UseSigSeparator", BALSA_OFFSET_BalsaApp_ELEM_sig_separator, CPT_BOOL, NULL, cfg_type_const_init_bool( TRUE ) },
	{ "SignatureFile", BALSA_OFFSET_BalsaApp_ELEM_signature_path, CPT_STR, NULL, cfg_type_const_init_str( NULL, fetch_sigpath, NULL ) },
	{ "AutoCheckMail", BALSA_OFFSET_BalsaApp_ELEM_check_mail_auto, CPT_BOOL, NULL, cfg_type_const_init_bool( TRUE ) },

#ifdef BALSA_SHOW_INFO
	{ "MBListInfoShow", BALSA_OFFSET_BalsaApp_ELEM_mblist_show_mb_content_info, CPT_BOOL, NULL, cfg_type_const_init_bool( TRUE ) },
#endif

	{ "PWindowSetting", BALSA_OFFSET_BalsaApp_ELEM_pwindow_option, CPT_NUM, NULL, 
	  cfg_type_const_init_num( WHILERETR, WHILERETR, NEVER, CPNF_USEMIN | CPNF_USEMAX ) },
	{ "WordwrapEnabled", BALSA_OFFSET_BalsaApp_ELEM_wordwrap, CPT_BOOL, NULL, cfg_type_const_init_bool( TRUE ) },
	{ "WordwrapLineLen", BALSA_OFFSET_BalsaApp_ELEM_wraplength, CPT_NUM, NULL, cfg_type_const_init_num( DEFAULT_WRAPLENGTH, 1, 0, CPNF_USEMIN ) },
	{ "PreviewWrapEnabled", BALSA_OFFSET_BalsaApp_ELEM_browse_wrap, CPT_BOOL, NULL, cfg_type_const_init_bool( TRUE ) },
	{ "HeadersToShow", BALSA_OFFSET_BalsaApp_ELEM_selected_headers, CPT_STR, NULL, cfg_type_const_init_str( DEFAULT_SELECTED_HDRS, NULL, NULL ) },
	{ "HeaderShowMode", BALSA_OFFSET_BalsaApp_ELEM_shown_headers, CPT_NUM, NULL, 
	  cfg_type_const_init_num( HEADERS_SELECTED, HEADERS_NONE, HEADERS_ALL, CPNF_USEMIN | CPNF_USEMAX ) },
	{ "ToolbarStyle", BALSA_OFFSET_BalsaApp_ELEM_toolbar_style, CPT_NUM, NULL,
	  cfg_type_const_init_num( GTK_TOOLBAR_BOTH, GTK_TOOLBAR_ICONS, GTK_TOOLBAR_BOTH, CPNF_USEMIN | CPNF_USEMAX ) },
	{ "ShowMBList", BALSA_OFFSET_BalsaApp_ELEM_show_mblist, CPT_BOOL, NULL, cfg_type_const_init_bool( TRUE ) },
	{ "ShowTabs", BALSA_OFFSET_BalsaApp_ELEM_show_notebook_tabs, CPT_BOOL, NULL, cfg_type_const_init_bool( FALSE ) },
	{ "EmptyTrashOnExit", BALSA_OFFSET_BalsaApp_ELEM_empty_trash_on_exit, CPT_BOOL, NULL, cfg_type_const_init_bool( TRUE ) },
	{ "ShowPreview", BALSA_OFFSET_BalsaApp_ELEM_previewpane, CPT_BOOL, NULL, cfg_type_const_init_bool( TRUE ) },
	{ "DebugMode", BALSA_OFFSET_BalsaApp_ELEM_debug, CPT_BOOL, NULL, cfg_type_const_init_bool( FALSE ) },
	{ "UseSMTP", BALSA_OFFSET_BalsaApp_ELEM_smtp, CPT_BOOL, NULL, cfg_type_const_init_bool( FALSE ) },
	{ "QuotePrefix", BALSA_OFFSET_BalsaApp_ELEM_quote_str, CPT_STR, NULL, cfg_type_const_init_str( DEFAULT_QUOTE, NULL, NULL ) },
	{ "MessageFont", BALSA_OFFSET_BalsaApp_ELEM_message_font, CPT_STR, NULL, cfg_type_const_init_str( DEFAULT_MESSAGE_FONT, NULL, NULL ) },
	{ "EncodingStyle", BALSA_OFFSET_BalsaApp_ELEM_encoding_style, CPT_NUM, NULL, cfg_type_const_init_num( 2, 0, 0, CPNF_USEMIN ) },
	{ "CharacterSet", BALSA_OFFSET_BalsaApp_ELEM_charset, CPT_STR, NULL, cfg_type_const_init_str( DEFAULT_CHARSET, NULL, NULL ) },
	{ "DateFormat", BALSA_OFFSET_BalsaApp_ELEM_date_string, CPT_STR, NULL, cfg_type_const_init_str( DEFAULT_DATE_FORMAT, NULL, NULL ) },
	{ "ShownComposeHeaders", BALSA_OFFSET_BalsaApp_ELEM_compose_headers, CPT_STR, NULL, cfg_type_const_init_str( DEFAULT_COMPOSE_HEADERS, NULL, NULL ) },
	{ "UserAddress", BALSA_OFFSET_BalsaApp_ELEM_address, CPT_META, NULL, cfg_type_const_init_meta( &spec_Address, NULL, TRUE ) },
	{ "PrintSettings", BALSA_OFFSET_BalsaApp_ELEM_PrintCommand, CPT_META, NULL, cfg_type_const_init_meta( &spec_Printing_t, NULL, FALSE ) },
	{ "UnreadMsgColor", BALSA_OFFSET_BalsaApp_ELEM_mblist_unread_color, CPT_META, NULL, cfg_type_const_init_meta( &spec_GdkColor, NULL, FALSE ) },
	cfg_parm_null
};

static gchar *fetch_replyto( const gchar *parmname )
{
	if( balsa_app.address->mailbox )
		return g_strdup( balsa_app.address->mailbox );

	return fetch_emailaddy( parmname );
}

static gchar *fetch_localmaildir( const gchar *parmname )
{
	return gnome_util_prepend_user_home( "mail" );
}

static gchar *fetch_sigpath( const gchar *parmname )
{
	return gnome_util_prepend_user_home( ".signature" );
}

gboolean cfg_meta_BalsaApp_write( const struct BalsaApplication *bapp, const cfg_location_t *loc, gpointer typedata )
{
	cfg_location_t *down;
	gboolean ret;

	down = cfg_location_godown( loc, "Preferences" );
	ret = cfg_group_write( (gpointer) bapp, parms_BalsaApp, down );
	cfg_location_free( down );
	return ret;
}

gboolean cfg_meta_BalsaApp_read( struct BalsaApplication *bapp, const cfg_location_t *loc, gpointer typedata )
{
	cfg_location_t *down;
	gboolean ret;

	if( bapp->address == NULL )
		bapp->address = g_new0( Address, 1 );

	down = cfg_location_godown( loc, "Preferences" );
	ret = cfg_group_read( (gpointer) bapp, parms_BalsaApp, down );
	cfg_location_free( down );
	return ret;
}

const cfg_metatype_spec_t spec_BalsaApp = { (meta_writer) cfg_meta_BalsaApp_write, (meta_reader) cfg_meta_BalsaApp_read };

/* ** List of mailboxes for balsa_state *********************************** */

gboolean cfg_meta_mblist_write( const GSList **list, const cfg_location_t *loc, gpointer typedata )
{
	int i;
	const GSList *iter;
	gchar key[16];
	i = 0;

	for( iter = (*list); iter; iter = iter->next ) {
		g_snprintf( key, 16, "Mb-%d", i );
		if( cfg_location_put_str( loc, key, (gchar *) (iter->data) ) )
			return TRUE;
		i++;
	}
	
	if( cfg_location_put_num( loc, "Count", i ) )
		return TRUE;

	return FALSE;
}

gboolean cfg_meta_mblist_read( GSList **list, const cfg_location_t *loc, gpointer typedata )
{
	int i, count;
	gchar key[16];
	gchar *mbname;
	i = 0;

	if( cfg_location_get_num( loc, "Count", &count, 0 ) )
		return TRUE;

	for( i = 0; i < count; i++ ) {
		g_snprintf( key, 16, "Mb-%d", i );
		
		if( cfg_location_get_str( loc, key, &mbname, "Inbox" ) )
			return TRUE;

		(*list) = g_slist_prepend( (*list), mbname );
	}
	
	return FALSE;
}

const cfg_metatype_spec_t spec_mblist = { (meta_writer) cfg_meta_mblist_write, (meta_reader) cfg_meta_mblist_read };

/* ** sm_state_t ********************************************************** */

static const cfg_parm_t parms_sm_state_t[] = {
	{ "AskedAboutAutosave", BALSA_OFFSET_sm_state_t_ELEM_asked_about_autosave, CPT_BOOL, NULL, cfg_type_const_init_bool( FALSE ) },
	{ "DoAutosave", BALSA_OFFSET_sm_state_t_ELEM_do_autosave, CPT_BOOL, NULL, cfg_type_const_init_bool( FALSE ) },
	{ "OpenFirstUnreadMailbox", BALSA_OFFSET_sm_state_t_ELEM_open_unread_mailbox, CPT_BOOL, NULL, cfg_type_const_init_bool( TRUE ) },
	{ "CheckMailOnStartup", BALSA_OFFSET_sm_state_t_ELEM_checkmail, CPT_BOOL, NULL, cfg_type_const_init_bool( FALSE ) },
	{ "StartAsComposer", BALSA_OFFSET_sm_state_t_ELEM_compose_mode, CPT_BOOL, NULL, cfg_type_const_init_bool( FALSE ) },
	{ "MailboxesToOpen", BALSA_OFFSET_sm_state_t_ELEM_mbs_to_open, CPT_META, NULL, cfg_type_const_init_meta( &spec_mblist, NULL, FALSE ) },
	cfg_parm_null
};

gboolean cfg_meta_state_write( const sm_state_t *state, const cfg_location_t *loc, gpointer typedata )
{
	return cfg_group_write( (gpointer) state, parms_sm_state_t, loc );
}

gboolean cfg_meta_state_read( sm_state_t *state, const cfg_location_t *loc, gpointer typedata )
{
	return cfg_group_read( (gpointer) state, parms_sm_state_t, loc );
}

const cfg_metatype_spec_t spec_sm_state_t = { (meta_writer) cfg_meta_state_write, (meta_reader) cfg_meta_state_read };

/* ** Generic Mailboxes *************************************************** */

/* Write: write a mailbox, wrapping around the different types. Check for
 * if the mailbox has been renamed, as this will change the section name
 * of the mailbox, which would result in duplicating the mailbox, under
 * the old name and the new name.
 *
 * Read: read the mailbox, peeking into the local mailboxes' Path element
 * to determine its type. Store the location of this mailbox for checking
 * on write for the duplication special case.
 *
 * Delete: nuke the mailbox from the config system... not from Balsa's
 * internal structures.
 *
 * *_simple: as above, defaulting the location parameter.
 */

static gchar *mbname( Mailbox *mb );

gboolean cfg_mailbox_write( Mailbox *mb, const cfg_location_t *top )
{
	cfg_location_t *down;
	gchar *name;
	gboolean ret;

	name = mbname( mb );
	down = cfg_location_godown( top, name );
	g_free( name );

	/* Don't accidentally duplicate this mailbox if it is
	 * renamed. */
	if( mb->cfgloc && cfg_location_cmp( mb->cfgloc, down ) ) {
		cfg_location_del( mb->cfgloc );
		cfg_location_free( mb->cfgloc );
		mb->cfgloc = cfg_location_dup( down );
	}

	switch( mb->type ) {
	case MAILBOX_IMAP:
		ret = cfg_meta_MailboxIMAP_write( BALSA_MAILBOX_IMAP( mb ), down, NULL );
		break;

	case MAILBOX_POP3:
		ret = cfg_meta_MailboxPOP3_write( BALSA_MAILBOX_POP3( mb ), down, NULL );
		break;

	case MAILBOX_MBOX:
	case MAILBOX_MH:
	case MAILBOX_MAILDIR:
		ret = cfg_meta_MailboxLocal_write( BALSA_MAILBOX_LOCAL( mb ), down, NULL );
		break;

	default:
		g_warning( "Uhoh, unknown mailbox type %d or corrupted pointers in cfg_mailbox_write (mailbox %s)!",
			   (int) MAILBOX( mb )->type, MAILBOX( mb )->name );
		ret = TRUE;
		break;
	}

	cfg_location_free( down );
	return ret;
}

Mailbox *cfg_mailbox_read( const gchar *name, const cfg_location_t *top )
{
	Mailbox *mb = NULL;
	cfg_location_t *down;
	gboolean ret;

	down = cfg_location_godown( top, name );

	if( strncmp( name, "IMAP", 4 ) == 0 ) {
		mb = MAILBOX( mailbox_new( MAILBOX_IMAP ) );
		ret = cfg_meta_MailboxIMAP_read( BALSA_MAILBOX_IMAP( mb ), down, NULL );
	} else if( strncmp( name, "POP3", 4 ) == 0 ) {
		mb = MAILBOX( mailbox_new( MAILBOX_POP3 ) );
		ret = cfg_meta_MailboxPOP3_read( BALSA_MAILBOX_POP3( mb ), down, NULL );
	} else if( strncmp( name, "Local", 5 ) == 0 ) {
		MailboxType type;
		gchar *path;

		if( cfg_location_get_str( down, "Path", &path, "error" ) ) {
			g_warning( "Cannot query local mailbox in config seciton \"%s\"!", name );
			return NULL;
		}

		if( strcmp( path, "error" ) == 0 ) {
			g_free( path );
			g_warning( "Badly saved local mailbox in config section \"%s\"!", name );
			return NULL;
		}

		type = mailbox_valid( path );
		g_free( path );

		if( type == MAILBOX_UNKNOWN ) {
			g_warning( "Cannot determine type of local mailbox in config section \"%s\"!", name );
			return NULL;
		}

		mb = MAILBOX( mailbox_new( type ) );
		ret = cfg_meta_MailboxLocal_read( BALSA_MAILBOX_LOCAL( mb ), down, NULL );
	} else {
		g_warning( "Unknown mailbox section name \"%s\"!", name );
		cfg_location_free( down );
		return NULL;
	}

	if( ret ) {
		cfg_location_free( down );
		gtk_object_destroy( GTK_OBJECT( mb ) );
		return NULL;
	}

	mb->cfgloc = cfg_location_dup( down );
	cfg_location_free( down );
	return mb;
}

gboolean cfg_mailbox_delete( Mailbox *mb, const cfg_location_t *top )
{
	gchar *name;
	cfg_location_t *down;

	name = mbname( mb );
	down = cfg_location_godown( top, name );
	g_free( name );

	if( cfg_location_del( down ) )
		return TRUE;

	cfg_location_free( down );
	return FALSE;
}

/* **************************************** */

static gboolean write_cb( const cfg_location_t *loc, gpointer user_data );
static gboolean write_cb( const cfg_location_t *loc, gpointer user_data )
{
	cfg_location_t *down = cfg_location_godown( loc, mb_key );
	gboolean ret;

	ret = cfg_mailbox_write( MAILBOX( user_data ), down );
	cfg_location_free( down );
	return ret;
}

gboolean cfg_mailbox_write_simple( Mailbox *mb )
{
	return balsa_sm_save_global_event( write_cb, mb );
}

typedef struct read_info_s {
	const gchar *name;
	Mailbox *result;
} read_info;

static gboolean simple_read_cb( const cfg_location_t *loc, gpointer user_data );
static gboolean simple_read_cb( const cfg_location_t *loc, gpointer user_data )
{
	read_info *ri = (read_info *) user_data;
	cfg_location_t *down = cfg_location_godown( loc, mb_key );

	ri->result = cfg_mailbox_read( ri->name, down );
	cfg_location_free( down );
	return FALSE;
}

Mailbox *cfg_mailbox_read_simple( const gchar *name )
{
	read_info ri;

	ri.name = name;
	if( balsa_sm_load_global_event( simple_read_cb, &ri ) )
		return NULL;
	return ri.result;
}

static gboolean delete_cb( const cfg_location_t *loc, gpointer user_data );
static gboolean delete_cb( const cfg_location_t *loc, gpointer user_data )
{
	gboolean ret;
	cfg_location_t *down = cfg_location_godown( loc, mb_key );

	ret = cfg_mailbox_delete( MAILBOX( user_data ), down );
	cfg_location_free( down );
	return ret;
}

gboolean cfg_mailbox_delete_simple( Mailbox *mb )
{
	return balsa_sm_save_global_event( delete_cb, mb );
}

/* **************************************** */

static gchar *mbname( Mailbox *mb )
{
	gchar *name;

	switch( mb->type ) {
	case MAILBOX_IMAP:
		name = g_strdup_printf( "IMAP Mailbox %s", MAILBOX( mb )->name );
		break;

	case MAILBOX_POP3:
		name = g_strdup_printf( "POP3 Mailbox %s", MAILBOX( mb )->name );		
		break;

	case MAILBOX_MBOX:
	case MAILBOX_MH:
	case MAILBOX_MAILDIR:
		name = g_strdup_printf( "Local Mailbox %s", MAILBOX( mb )->name );
		break;

	default:
		g_warning( "Uhoh, unknown mailbox type %d or corrupted pointers in cfg_mailbox_write (mailbox %s)!",
			   (int) MAILBOX( mb )->type, MAILBOX( mb )->name );
		name = g_strdup_printf( "Unknown Mailbox %s", MAILBOX( mb )->name );
		break;
	}

	return name;
}
