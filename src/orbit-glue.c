/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/*
 * Copyright (C) 1998-2000 Free Software Foundation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * (c) 1997-2000 Stuart Parmenter and others, see AUTHORS for a list of people
 *
 */

/*
 *
 * FIXME: this is a skel, generated with orbit-idl --skeleton-impl
 * it lacks the good stuff and the generating idl is likely to change
 * anyway. Do not hack on this yet please till i get a clue about corba :)
 */


#include "balsa.h"

/*** App-specific servant structures ***/

typedef struct
{
   POA_GNOME_Balsa_Message servant;
   PortableServer_POA poa;
   CORBA_char *attr_to;

   CORBA_char *attr_cc;

   CORBA_char *attr_subject;

   GNOME_Balsa_MultiPart attr_body;

   GNOME_Balsa_AttachmentList attr_attachments;

}
impl_POA_GNOME_Balsa_Message;

typedef struct
{
   POA_GNOME_Balsa_MessageList servant;
   PortableServer_POA poa;

}
impl_POA_GNOME_Balsa_MessageList;

typedef struct
{
   POA_GNOME_Balsa_FolderBrowser servant;
   PortableServer_POA poa;
   CORBA_long attr_unread_messages;

}
impl_POA_GNOME_Balsa_FolderBrowser;

typedef struct
{
   POA_GNOME_Balsa_App servant;
   PortableServer_POA poa;

}
impl_POA_GNOME_Balsa_App;

/*** Implementation stub prototypes ***/

static void impl_GNOME_Balsa_Message__destroy(impl_POA_GNOME_Balsa_Message *
					      servant,

					      CORBA_Environment * ev);
static CORBA_char
   *impl_GNOME_Balsa_Message__get_to(impl_POA_GNOME_Balsa_Message * servant,
				     CORBA_Environment * ev);
static void impl_GNOME_Balsa_Message__set_to(impl_POA_GNOME_Balsa_Message *
					     servant, CORBA_char * value,
					     CORBA_Environment * ev);

static CORBA_char
   *impl_GNOME_Balsa_Message__get_cc(impl_POA_GNOME_Balsa_Message * servant,
				     CORBA_Environment * ev);
static void impl_GNOME_Balsa_Message__set_cc(impl_POA_GNOME_Balsa_Message *
					     servant, CORBA_char * value,
					     CORBA_Environment * ev);

static CORBA_char
   *impl_GNOME_Balsa_Message__get_subject(impl_POA_GNOME_Balsa_Message *
					  servant, CORBA_Environment * ev);
static void impl_GNOME_Balsa_Message__set_subject(impl_POA_GNOME_Balsa_Message
						  * servant,
						  CORBA_char * value,
						  CORBA_Environment * ev);

static GNOME_Balsa_MultiPart
impl_GNOME_Balsa_Message__get_body(impl_POA_GNOME_Balsa_Message * servant,
				   CORBA_Environment * ev);
static void
impl_GNOME_Balsa_Message__set_body(impl_POA_GNOME_Balsa_Message * servant,
				   GNOME_Balsa_MultiPart value,
				   CORBA_Environment * ev);

static GNOME_Balsa_AttachmentList
   *impl_GNOME_Balsa_Message__get_attachments(impl_POA_GNOME_Balsa_Message *
					      servant,

					      CORBA_Environment * ev);
static void
impl_GNOME_Balsa_Message__set_attachments(impl_POA_GNOME_Balsa_Message *
					  servant,
					  GNOME_Balsa_AttachmentList * value,
					  CORBA_Environment * ev);

static void
impl_GNOME_Balsa_Message_add_attachment(impl_POA_GNOME_Balsa_Message *
					servant,
					GNOME_Balsa_Attachment * attach,
					CORBA_Environment * ev);

static CORBA_boolean
impl_GNOME_Balsa_Message_send_message(impl_POA_GNOME_Balsa_Message * servant,
				      CORBA_Environment * ev);

static void
impl_GNOME_Balsa_MessageList__destroy(impl_POA_GNOME_Balsa_MessageList *
				      servant, CORBA_Environment * ev);
static void
impl_GNOME_Balsa_MessageList_select_message(impl_POA_GNOME_Balsa_MessageList *
					    servant, CORBA_long index,
					    CORBA_Environment * ev);

static void
impl_GNOME_Balsa_MessageList_open_message(impl_POA_GNOME_Balsa_MessageList *
					  servant, CORBA_long message_number,
					  CORBA_Environment * ev);

static GNOME_Balsa_Message
impl_GNOME_Balsa_MessageList_get_message(impl_POA_GNOME_Balsa_MessageList *
					 servant, CORBA_long index,
					 CORBA_Environment * ev);

static void
impl_GNOME_Balsa_FolderBrowser__destroy(impl_POA_GNOME_Balsa_FolderBrowser *
					servant, CORBA_Environment * ev);
static CORBA_long
impl_GNOME_Balsa_FolderBrowser__get_unread_messages
(impl_POA_GNOME_Balsa_FolderBrowser * servant, CORBA_Environment * ev);
static void
impl_GNOME_Balsa_FolderBrowser__set_unread_messages
(impl_POA_GNOME_Balsa_FolderBrowser * servant, CORBA_long value,

CORBA_Environment * ev);

static void
impl_GNOME_Balsa_FolderBrowser_open_folder(impl_POA_GNOME_Balsa_FolderBrowser
					   * servant, CORBA_char * path,
					   CORBA_Environment * ev);

static CORBA_boolean
impl_GNOME_Balsa_FolderBrowser_put_message(impl_POA_GNOME_Balsa_FolderBrowser
					   * servant,
					   GNOME_Balsa_Message message,
					   CORBA_Environment * ev);

static void
impl_GNOME_Balsa_FolderBrowser_close_folder(impl_POA_GNOME_Balsa_FolderBrowser
					    * servant,

					    CORBA_Environment * ev);

static GNOME_Balsa_MessageList
impl_GNOME_Balsa_FolderBrowser_get_unread_message_list
   (impl_POA_GNOME_Balsa_FolderBrowser * servant, CORBA_Environment * ev);

static GNOME_Balsa_MessageList
impl_GNOME_Balsa_FolderBrowser_get_message_list
(impl_POA_GNOME_Balsa_FolderBrowser * servant, CORBA_Environment * ev);

static void
impl_GNOME_Balsa_FolderBrowser_synch(impl_POA_GNOME_Balsa_FolderBrowser *
				     servant, CORBA_Environment * ev);

static void impl_GNOME_Balsa_App__destroy(impl_POA_GNOME_Balsa_App * servant,
					  CORBA_Environment * ev);
static CORBA_long
impl_GNOME_Balsa_App_fetch_pop(impl_POA_GNOME_Balsa_App * servant,
			       CORBA_Environment * ev);

static void
impl_GNOME_Balsa_App_open_compose_window(impl_POA_GNOME_Balsa_App * servant,
					 CORBA_Environment * ev);

static GNOME_Balsa_Message
impl_GNOME_Balsa_App_new_message(impl_POA_GNOME_Balsa_App * servant,
				 CORBA_Environment * ev);

static CORBA_char
   *impl_GNOME_Balsa_App_get_folder_list(impl_POA_GNOME_Balsa_App * servant,
					 CORBA_Environment * ev);

static GNOME_Balsa_FolderBrowser
impl_GNOME_Balsa_App_open_unread_mailbox(impl_POA_GNOME_Balsa_App * servant,
					 CORBA_Environment * ev);

/*** epv structures ***/

static PortableServer_ServantBase__epv impl_GNOME_Balsa_Message_base_epv = {
   NULL,			/* _private data */
   NULL,			/* finalize routine */
   NULL,			/* default_POA routine */
};
static POA_GNOME_Balsa_Message__epv impl_GNOME_Balsa_Message_epv = {
   NULL,			/* _private */
   (gpointer) & impl_GNOME_Balsa_Message__get_to,
   (gpointer) & impl_GNOME_Balsa_Message__set_to,

   (gpointer) & impl_GNOME_Balsa_Message__get_cc,
   (gpointer) & impl_GNOME_Balsa_Message__set_cc,

   (gpointer) & impl_GNOME_Balsa_Message__get_subject,
   (gpointer) & impl_GNOME_Balsa_Message__set_subject,

   (gpointer) & impl_GNOME_Balsa_Message__get_body,
   (gpointer) & impl_GNOME_Balsa_Message__set_body,

   (gpointer) & impl_GNOME_Balsa_Message__get_attachments,
   (gpointer) & impl_GNOME_Balsa_Message__set_attachments,

   (gpointer) & impl_GNOME_Balsa_Message_add_attachment,

   (gpointer) & impl_GNOME_Balsa_Message_send_message,

};
static PortableServer_ServantBase__epv impl_GNOME_Balsa_MessageList_base_epv = {
   NULL,			/* _private data */
   NULL,			/* finalize routine */
   NULL,			/* default_POA routine */
};
static POA_GNOME_Balsa_MessageList__epv impl_GNOME_Balsa_MessageList_epv = {
   NULL,			/* _private */
   (gpointer) & impl_GNOME_Balsa_MessageList_select_message,

   (gpointer) & impl_GNOME_Balsa_MessageList_open_message,

   (gpointer) & impl_GNOME_Balsa_MessageList_get_message,

};
static PortableServer_ServantBase__epv impl_GNOME_Balsa_FolderBrowser_base_epv
   = {
   NULL,			/* _private data */
   NULL,			/* finalize routine */
   NULL,			/* default_POA routine */
};
static POA_GNOME_Balsa_FolderBrowser__epv impl_GNOME_Balsa_FolderBrowser_epv = {
   NULL,			/* _private */
   (gpointer) & impl_GNOME_Balsa_FolderBrowser__get_unread_messages,
   (gpointer) & impl_GNOME_Balsa_FolderBrowser__set_unread_messages,

   (gpointer) & impl_GNOME_Balsa_FolderBrowser_open_folder,

   (gpointer) & impl_GNOME_Balsa_FolderBrowser_put_message,

   (gpointer) & impl_GNOME_Balsa_FolderBrowser_close_folder,

   (gpointer) & impl_GNOME_Balsa_FolderBrowser_get_unread_message_list,

   (gpointer) & impl_GNOME_Balsa_FolderBrowser_get_message_list,

   (gpointer) & impl_GNOME_Balsa_FolderBrowser_synch,

};
static PortableServer_ServantBase__epv impl_GNOME_Balsa_App_base_epv = {
   NULL,			/* _private data */
   NULL,			/* finalize routine */
   NULL,			/* default_POA routine */
};
static POA_GNOME_Balsa_App__epv impl_GNOME_Balsa_App_epv = {
   NULL,			/* _private */
   (gpointer) & impl_GNOME_Balsa_App_fetch_pop,

   (gpointer) & impl_GNOME_Balsa_App_open_compose_window,

   (gpointer) & impl_GNOME_Balsa_App_new_message,

   (gpointer) & impl_GNOME_Balsa_App_get_folder_list,

   (gpointer) & impl_GNOME_Balsa_App_open_unread_mailbox,

};

/*** vepv structures ***/

static POA_GNOME_Balsa_Message__vepv impl_GNOME_Balsa_Message_vepv = {
   &impl_GNOME_Balsa_Message_base_epv,
   &impl_GNOME_Balsa_Message_epv,
};
static POA_GNOME_Balsa_MessageList__vepv impl_GNOME_Balsa_MessageList_vepv = {
   &impl_GNOME_Balsa_MessageList_base_epv,
   &impl_GNOME_Balsa_MessageList_epv,
};
static POA_GNOME_Balsa_FolderBrowser__vepv impl_GNOME_Balsa_FolderBrowser_vepv
   = {
   &impl_GNOME_Balsa_FolderBrowser_base_epv,
   &impl_GNOME_Balsa_FolderBrowser_epv,
};
static POA_GNOME_Balsa_App__vepv impl_GNOME_Balsa_App_vepv = {
   &impl_GNOME_Balsa_App_base_epv,
   &impl_GNOME_Balsa_App_epv,
};

/*** Stub implementations ***/

static GNOME_Balsa_Message
impl_GNOME_Balsa_Message__create(PortableServer_POA poa,
				 CORBA_Environment * ev)
{
   GNOME_Balsa_Message retval;
   impl_POA_GNOME_Balsa_Message *newservant;
   PortableServer_ObjectId *objid;

   newservant = g_new0(impl_POA_GNOME_Balsa_Message, 1);
   newservant->servant.vepv = &impl_GNOME_Balsa_Message_vepv;
   newservant->poa = poa;
   POA_GNOME_Balsa_Message__init((PortableServer_Servant) newservant, ev);
   objid = PortableServer_POA_activate_object(poa, newservant, ev);
   CORBA_free(objid);
   retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);

   return retval;
}

static void
impl_GNOME_Balsa_Message__destroy(impl_POA_GNOME_Balsa_Message * servant,
				  CORBA_Environment * ev)
{
   PortableServer_ObjectId *objid;

   objid = PortableServer_POA_servant_to_id(servant->poa, servant, ev);
   PortableServer_POA_deactivate_object(servant->poa, objid, ev);
   CORBA_free(objid);

   POA_GNOME_Balsa_Message__fini((PortableServer_Servant) servant, ev);
   g_free(servant);
}

static CORBA_char *
impl_GNOME_Balsa_Message__get_to(impl_POA_GNOME_Balsa_Message * servant,
				 CORBA_Environment * ev)
{
   CORBA_char *retval;

   retval = CORBA_string_dup( servant->attr_to );
   
   return retval;
}

static void
impl_GNOME_Balsa_Message__set_to(impl_POA_GNOME_Balsa_Message * servant,
				 CORBA_char * value, CORBA_Environment * ev)
{
  servant->attr_to = CORBA_string_dup( value );  
}

static CORBA_char *
impl_GNOME_Balsa_Message__get_cc(impl_POA_GNOME_Balsa_Message * servant,
				 CORBA_Environment * ev)
{
   CORBA_char *retval;

   retval = CORBA_string_dup( servant->attr_cc );

   return retval;
}

static void
impl_GNOME_Balsa_Message__set_cc(impl_POA_GNOME_Balsa_Message * servant,
				 CORBA_char * value, CORBA_Environment * ev)
{
   servant->attr_cc = CORBA_string_dup( value );
}

static CORBA_char *
impl_GNOME_Balsa_Message__get_subject(impl_POA_GNOME_Balsa_Message * servant,
				      CORBA_Environment * ev)
{
   CORBA_char *retval;

   retval = CORBA_string_dup( servant->attr_subject );

   return retval;
}

static void
impl_GNOME_Balsa_Message__set_subject(impl_POA_GNOME_Balsa_Message * servant,
				      CORBA_char * value,
				      CORBA_Environment * ev)
{
   servant->attr_subject = CORBA_string_dup( value );
}

static GNOME_Balsa_MultiPart
impl_GNOME_Balsa_Message__get_body(impl_POA_GNOME_Balsa_Message * servant,
				   CORBA_Environment * ev)
{
   GNOME_Balsa_MultiPart retval;

   /* FIXME: what do i do here ? */

   return retval;
}

static void
impl_GNOME_Balsa_Message__set_body(impl_POA_GNOME_Balsa_Message * servant,
				   GNOME_Balsa_MultiPart value,
				   CORBA_Environment * ev)
{
   /* FIXME: what do i do here ? */
}

static GNOME_Balsa_AttachmentList *
impl_GNOME_Balsa_Message__get_attachments(impl_POA_GNOME_Balsa_Message *
					  servant, CORBA_Environment * ev)
{
   GNOME_Balsa_AttachmentList *retval;

   return retval;
}

static void
impl_GNOME_Balsa_Message__set_attachments(impl_POA_GNOME_Balsa_Message *
					  servant,
					  GNOME_Balsa_AttachmentList * value,
					  CORBA_Environment * ev)
{
  /* this shouldn't be used */
}

static void
impl_GNOME_Balsa_Message_add_attachment(impl_POA_GNOME_Balsa_Message *
					servant,
					GNOME_Balsa_Attachment * attach,
					CORBA_Environment * ev)
{
}

static CORBA_boolean
impl_GNOME_Balsa_Message_send_message(impl_POA_GNOME_Balsa_Message * servant,
				      CORBA_Environment * ev)
{
   CORBA_boolean retval;

   return retval;
}

static GNOME_Balsa_MessageList
impl_GNOME_Balsa_MessageList__create(PortableServer_POA poa,
				     CORBA_Environment * ev)
{
   GNOME_Balsa_MessageList retval;
   impl_POA_GNOME_Balsa_MessageList *newservant;
   PortableServer_ObjectId *objid;

   newservant = g_new0(impl_POA_GNOME_Balsa_MessageList, 1);
   newservant->servant.vepv = &impl_GNOME_Balsa_MessageList_vepv;
   newservant->poa = poa;
   POA_GNOME_Balsa_MessageList__init((PortableServer_Servant) newservant, ev);
   objid = PortableServer_POA_activate_object(poa, newservant, ev);
   CORBA_free(objid);
   retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);

   return retval;
}

static void
impl_GNOME_Balsa_MessageList__destroy(impl_POA_GNOME_Balsa_MessageList *
				      servant, CORBA_Environment * ev)
{
   PortableServer_ObjectId *objid;

   objid = PortableServer_POA_servant_to_id(servant->poa, servant, ev);
   PortableServer_POA_deactivate_object(servant->poa, objid, ev);
   CORBA_free(objid);

   POA_GNOME_Balsa_MessageList__fini((PortableServer_Servant) servant, ev);
   g_free(servant);
}

static void
impl_GNOME_Balsa_MessageList_select_message(impl_POA_GNOME_Balsa_MessageList *
					    servant, CORBA_long index,
					    CORBA_Environment * ev)
{
}

static void
impl_GNOME_Balsa_MessageList_open_message(impl_POA_GNOME_Balsa_MessageList *
					  servant, CORBA_long message_number,
					  CORBA_Environment * ev)
{
}

static GNOME_Balsa_Message
impl_GNOME_Balsa_MessageList_get_message(impl_POA_GNOME_Balsa_MessageList *
					 servant, CORBA_long index,
					 CORBA_Environment * ev)
{
   GNOME_Balsa_Message retval;

   return retval;
}

static GNOME_Balsa_FolderBrowser
impl_GNOME_Balsa_FolderBrowser__create(PortableServer_POA poa,
				       CORBA_Environment * ev)
{
   GNOME_Balsa_FolderBrowser retval;
   impl_POA_GNOME_Balsa_FolderBrowser *newservant;
   PortableServer_ObjectId *objid;

   newservant = g_new0(impl_POA_GNOME_Balsa_FolderBrowser, 1);
   newservant->servant.vepv = &impl_GNOME_Balsa_FolderBrowser_vepv;
   newservant->poa = poa;
   POA_GNOME_Balsa_FolderBrowser__init((PortableServer_Servant) newservant,
				       ev);
   objid = PortableServer_POA_activate_object(poa, newservant, ev);
   CORBA_free(objid);
   retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);

   return retval;
}

static void
impl_GNOME_Balsa_FolderBrowser__destroy(impl_POA_GNOME_Balsa_FolderBrowser *
					servant, CORBA_Environment * ev)
{
   PortableServer_ObjectId *objid;

   objid = PortableServer_POA_servant_to_id(servant->poa, servant, ev);
   PortableServer_POA_deactivate_object(servant->poa, objid, ev);
   CORBA_free(objid);

   POA_GNOME_Balsa_FolderBrowser__fini((PortableServer_Servant) servant, ev);
   g_free(servant);
}

static CORBA_long
impl_GNOME_Balsa_FolderBrowser__get_unread_messages
   (impl_POA_GNOME_Balsa_FolderBrowser * servant, CORBA_Environment * ev)
{
   CORBA_long retval;

   return retval;
}

static void
impl_GNOME_Balsa_FolderBrowser__set_unread_messages
   (impl_POA_GNOME_Balsa_FolderBrowser * servant, CORBA_long value,
    CORBA_Environment * ev)
{
}

static void
impl_GNOME_Balsa_FolderBrowser_open_folder(impl_POA_GNOME_Balsa_FolderBrowser
					   * servant, CORBA_char * path,
					   CORBA_Environment * ev)
{
}

static CORBA_boolean
impl_GNOME_Balsa_FolderBrowser_put_message(impl_POA_GNOME_Balsa_FolderBrowser
					   * servant,
					   GNOME_Balsa_Message message,
					   CORBA_Environment * ev)
{
   CORBA_boolean retval;

   return retval;
}

static void
impl_GNOME_Balsa_FolderBrowser_close_folder(impl_POA_GNOME_Balsa_FolderBrowser
					    * servant, CORBA_Environment * ev)
{
}

static GNOME_Balsa_MessageList
impl_GNOME_Balsa_FolderBrowser_get_unread_message_list
   (impl_POA_GNOME_Balsa_FolderBrowser * servant, CORBA_Environment * ev)
{
   GNOME_Balsa_MessageList retval;

   return retval;
}

static GNOME_Balsa_MessageList
impl_GNOME_Balsa_FolderBrowser_get_message_list
   (impl_POA_GNOME_Balsa_FolderBrowser * servant, CORBA_Environment * ev)
{
   GNOME_Balsa_MessageList retval;

   return retval;
}

static void
impl_GNOME_Balsa_FolderBrowser_synch(impl_POA_GNOME_Balsa_FolderBrowser *
				     servant, CORBA_Environment * ev)
{
}

static GNOME_Balsa_App
impl_GNOME_Balsa_App__create(PortableServer_POA poa, CORBA_Environment * ev)
{
   GNOME_Balsa_App retval;
   impl_POA_GNOME_Balsa_App *newservant;
   PortableServer_ObjectId *objid;

   newservant = g_new0(impl_POA_GNOME_Balsa_App, 1);
   newservant->servant.vepv = &impl_GNOME_Balsa_App_vepv;
   newservant->poa = poa;
   POA_GNOME_Balsa_App__init((PortableServer_Servant) newservant, ev);
   objid = PortableServer_POA_activate_object(poa, newservant, ev);
   CORBA_free(objid);
   retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);

   return retval;
}

static void
impl_GNOME_Balsa_App__destroy(impl_POA_GNOME_Balsa_App * servant,
			      CORBA_Environment * ev)
{
   PortableServer_ObjectId *objid;

   objid = PortableServer_POA_servant_to_id(servant->poa, servant, ev);
   PortableServer_POA_deactivate_object(servant->poa, objid, ev);
   CORBA_free(objid);

   POA_GNOME_Balsa_App__fini((PortableServer_Servant) servant, ev);
   g_free(servant);
}

static CORBA_long
impl_GNOME_Balsa_App_fetch_pop(impl_POA_GNOME_Balsa_App * servant,
			       CORBA_Environment * ev)
{
   CORBA_long retval;

   return retval;
}

static void
impl_GNOME_Balsa_App_open_compose_window(impl_POA_GNOME_Balsa_App * servant,
					 CORBA_Environment * ev)
{

    

}

static GNOME_Balsa_Message
impl_GNOME_Balsa_App_new_message(impl_POA_GNOME_Balsa_App * servant,
				 CORBA_Environment * ev)
{
   GNOME_Balsa_Message retval;

   return retval;
}

static CORBA_char *
impl_GNOME_Balsa_App_get_folder_list(impl_POA_GNOME_Balsa_App * servant,
				     CORBA_Environment * ev)
{
   CORBA_char *retval;

   return retval;
}

static GNOME_Balsa_FolderBrowser
impl_GNOME_Balsa_App_open_unread_mailbox(impl_POA_GNOME_Balsa_App * servant,
					 CORBA_Environment * ev)
{
   GNOME_Balsa_FolderBrowser retval;

   return retval;
}



void
balsa_corba_init( int *argc, char **argv, CORBA_Environment *ev)
{
    PortableServer_ObjectId objid = {0, sizeof("balsa"), "balsa" };
    PortableServer_POA the_poa;
    GNOME_Pilot_Daemon acc;
    static gboolean object_initialised = FALSE;

    if ( object_initialised ) return;
    
    object_initialised = TRUE;
    
    
    CORBA_exception_init(ev); /* FIXME */
    
    orb = gnome_CORBA_init(PACKAGE,VERSION,
			   argc,argv,
			   GNORBA_INIT_SERVER_FUNC,ev);
    g_return_if_fail(ev->_major == CORBA_NO_EXCEPTION);
    
    the_poa = (PortableServer_POA)CORBA_ORB_resolve_initial_references(orb, "RootPOA", ev);
    g_return_if_fail(ev->_major == CORBA_NO_EXCEPTION);

    
    POA_GNOME_Balsa_App__init(&daemon_servant, ev); /*FIXME?*/
    g_return_if_fail(ev->_major == CORBA_NO_EXCEPTION);
 

    PortableServer_POAManager_activate(PortableServer_POA__get_the_POAManager(the_poa, ev), ev);
    g_return_if_fail(ev->_major == CORBA_NO_EXCEPTION);

    
    PortableServer_POA_activate_object_with_id(the_poa, &objid, &daemon_servant, ev);
    g_return_if_fail(ev->_major == CORBA_NO_EXCEPTION);
    
    acc = PortableServer_POA_servant_to_reference(the_poa, &daemon_servant, ev);
    g_return_if_fail(ev->_major == CORBA_NO_EXCEPTION);

    
    switch(goad_server_register(CORBA_OBJECT_NIL,acc,"gpilotd","object",ev)) {
    case -2:
	/* FIXME: better handling here */
	g_message(_("The gnome-pilot daemon is already running"));
	exit (0);
	break;
    case -1:
	/* FIXME: better handling here */
	g_print("GOAD Name server failure\n");
	exit (1);
	break;
    }
    g_return_if_fail(ev->_major == CORBA_NO_EXCEPTION);
    
    
    CORBA_Object_release(acc, ev);
    g_return_if_fail(ev->_major == CORBA_NO_EXCEPTION);
    
    ORBit_custom_run_setup(orb, ev);
    g_return_if_fail(ev->_major == CORBA_NO_EXCEPTION);
	
}
