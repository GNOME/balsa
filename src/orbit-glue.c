/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
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

   CORBA_char *attr_body;

   CORBA_char *attr_attachments;

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
   CORBA_boolean attr_has_unread_messages;

}
impl_POA_GNOME_Balsa_FolderBrowser;

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

static CORBA_char
   *impl_GNOME_Balsa_Message__get_body(impl_POA_GNOME_Balsa_Message * servant,
				       CORBA_Environment * ev);
static void impl_GNOME_Balsa_Message__set_body(impl_POA_GNOME_Balsa_Message *
					       servant, CORBA_char * value,
					       CORBA_Environment * ev);

static CORBA_char
   *impl_GNOME_Balsa_Message__get_attachments(impl_POA_GNOME_Balsa_Message *
					      servant,

					      CORBA_Environment * ev);
static void
impl_GNOME_Balsa_Message__set_attachments(impl_POA_GNOME_Balsa_Message *
					  servant, CORBA_char * value,
					  CORBA_Environment * ev);

static void
impl_GNOME_Balsa_Message_add_attachment(impl_POA_GNOME_Balsa_Message *
					servant, CORBA_char * filename,
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
static CORBA_boolean
impl_GNOME_Balsa_FolderBrowser__get_has_unread_messages
(impl_POA_GNOME_Balsa_FolderBrowser * servant, CORBA_Environment * ev);
static void
impl_GNOME_Balsa_FolderBrowser__set_has_unread_messages
(impl_POA_GNOME_Balsa_FolderBrowser * servant, CORBA_boolean value,

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
   (gpointer) & impl_GNOME_Balsa_FolderBrowser__get_has_unread_messages,
   (gpointer) & impl_GNOME_Balsa_FolderBrowser__set_has_unread_messages,

   (gpointer) & impl_GNOME_Balsa_FolderBrowser_open_folder,

   (gpointer) & impl_GNOME_Balsa_FolderBrowser_put_message,

   (gpointer) & impl_GNOME_Balsa_FolderBrowser_close_folder,

   (gpointer) & impl_GNOME_Balsa_FolderBrowser_get_unread_message_list,

   (gpointer) & impl_GNOME_Balsa_FolderBrowser_get_message_list,

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

static CORBA_char *
impl_GNOME_Balsa_Message__get_body(impl_POA_GNOME_Balsa_Message * servant,
				   CORBA_Environment * ev)
{
   CORBA_char *retval;
   
   /* do i really want to do the potentially large body like this ?*/
   retval = CORBA_string_dup( servant->attr_body );

   return retval;
}

static void
impl_GNOME_Balsa_Message__set_body(impl_POA_GNOME_Balsa_Message * servant,
				   CORBA_char * value, CORBA_Environment * ev)
{
  /* do i really want to do the potentially large body like this ?*/
  servant->attr_body = CORBA_string_dup( value );
}

static CORBA_char *
impl_GNOME_Balsa_Message__get_attachments(impl_POA_GNOME_Balsa_Message *
					  servant, CORBA_Environment * ev)
{
   CORBA_char *retval;
  
   /* nice and tidy attach list */
   retval = CORBA_string_dup( servant->attr_attachments );
   
   return retval;
}

static void
impl_GNOME_Balsa_Message__set_attachments(impl_POA_GNOME_Balsa_Message *
					  servant, CORBA_char * value,
					  CORBA_Environment * ev)
{
   /* use add attach please. maybe this shouldn't be an attribute 
      after all*/
}

static void
impl_GNOME_Balsa_Message_add_attachment(impl_POA_GNOME_Balsa_Message *
					servant, CORBA_char * filename,
					CORBA_Environment * ev)
{
  /* here we want to check existance of attachment, and add it to
   attachment list. */
  /* FIXME: look closely at libmutt attachment scheme */
  
}

static CORBA_boolean
impl_GNOME_Balsa_Message_send_message(impl_POA_GNOME_Balsa_Message * servant,
				      CORBA_Environment * ev)
{
   CORBA_boolean retval;

   /* here we take the message and send it via libbalsa funcs*/

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

static CORBA_boolean
impl_GNOME_Balsa_FolderBrowser__get_has_unread_messages
   (impl_POA_GNOME_Balsa_FolderBrowser * servant, CORBA_Environment * ev)
{
   CORBA_boolean retval;

   return retval;
}

static void
impl_GNOME_Balsa_FolderBrowser__set_has_unread_messages
   (impl_POA_GNOME_Balsa_FolderBrowser * servant, CORBA_boolean value,
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
