#include "balsa.h"

/*** App-specific servant structures ***/
typedef struct {
   POA_balsa_mail_send servant;
   PortableServer_POA poa;

} impl_POA_balsa_mail_send;

typedef struct {
   POA_balsa_mailbox_info servant;
   PortableServer_POA poa;

} impl_POA_balsa_mailbox_info;

/*** Implementation stub prototypes ***/
static void impl_balsa_mail_send__destroy(impl_POA_balsa_mail_send * servant,
					  CORBA_Environment * ev);

CORBA_boolean
impl_balsa_mail_send_to(impl_POA_balsa_mail_send * servant,
			CORBA_char * to,
			CORBA_Environment * ev);

CORBA_boolean
impl_balsa_mail_send_attachment(impl_POA_balsa_mail_send * servant,
				CORBA_char * to,
				CORBA_char * filename,
				CORBA_Environment * ev);

static void impl_balsa_mailbox_info__destroy(impl_POA_balsa_mailbox_info * servant,
					     CORBA_Environment * ev);

CORBA_boolean
impl_balsa_mailbox_info_newmail(impl_POA_balsa_mailbox_info * servant,
				CORBA_char * path,
				CORBA_Environment * ev);

CORBA_long
impl_balsa_mailbox_info_num_msgs(impl_POA_balsa_mailbox_info * servant,
				 CORBA_char * path,
				 CORBA_Environment * ev);

/*** epv structures ***/
static PortableServer_ServantBase__epv impl_balsa_mail_send_base_epv =
{
   NULL,			/* _private data */
   (gpointer) & impl_balsa_mail_send__destroy,	/* finalize routine */
   NULL,			/* default_POA routine */
};
static POA_balsa_mail_send__epv impl_balsa_mail_send_epv =
{
   NULL,			/* _private */
   (gpointer) & impl_balsa_mail_send_to,

   (gpointer) & impl_balsa_mail_send_attachment,

};
static PortableServer_ServantBase__epv impl_balsa_mailbox_info_base_epv =
{
   NULL,			/* _private data */
   (gpointer) & impl_balsa_mailbox_info__destroy,	/* finalize routine */
   NULL,			/* default_POA routine */
};
static POA_balsa_mailbox_info__epv impl_balsa_mailbox_info_epv =
{
   NULL,			/* _private */
   (gpointer) & impl_balsa_mailbox_info_newmail,

   (gpointer) & impl_balsa_mailbox_info_num_msgs,

};

/*** vepv structures ***/
static POA_balsa_mail_send__vepv impl_balsa_mail_send_vepv =
{
   &impl_balsa_mail_send_base_epv,
   &impl_balsa_mail_send_epv,
};

static POA_balsa_mailbox_info__vepv impl_balsa_mailbox_info_vepv =
{
   &impl_balsa_mailbox_info_base_epv,
   &impl_balsa_mailbox_info_epv,
};

/*** Stub implementations ***/
static balsa_mail_send 
impl_balsa_mail_send__create(PortableServer_POA poa, CORBA_Environment * ev)
{
   balsa_mail_send retval;
   impl_POA_balsa_mail_send *newservant;
   PortableServer_ObjectId *objid;

   newservant = g_new0(impl_POA_balsa_mail_send, 1);
   newservant->servant.vepv = &impl_balsa_mail_send_vepv;
   newservant->poa = poa;
   POA_balsa_mail_send__init((PortableServer_Servant) newservant, ev);
   objid = PortableServer_POA_activate_object(poa, newservant, ev);
   CORBA_free(objid);
   retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);

   return retval;
}

/* You shouldn't call this routine directly without first deactivating the servant... */
static void
impl_balsa_mail_send__destroy(impl_POA_balsa_mail_send * servant, CORBA_Environment * ev)
{

   POA_balsa_mail_send__fini((PortableServer_Servant) servant, ev);
   g_free(servant);
}

CORBA_boolean
impl_balsa_mail_send_to(impl_POA_balsa_mail_send * servant,
			CORBA_char * to,
			CORBA_Environment * ev)
{
   CORBA_boolean retval;
   retval = FALSE;
   return retval;
}

CORBA_boolean
impl_balsa_mail_send_attachment(impl_POA_balsa_mail_send * servant,
				CORBA_char * to,
				CORBA_char * filename,
				CORBA_Environment * ev)
{
   CORBA_boolean retval;
   retval = FALSE;
   return retval;
}

static balsa_mailbox_info 
impl_balsa_mailbox_info__create(PortableServer_POA poa, CORBA_Environment * ev)
{
   balsa_mailbox_info retval;
   impl_POA_balsa_mailbox_info *newservant;
   PortableServer_ObjectId *objid;

   newservant = g_new0(impl_POA_balsa_mailbox_info, 1);
   newservant->servant.vepv = &impl_balsa_mailbox_info_vepv;
   newservant->poa = poa;
   POA_balsa_mailbox_info__init((PortableServer_Servant) newservant, ev);
   objid = PortableServer_POA_activate_object(poa, newservant, ev);
   CORBA_free(objid);
   retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);

   return retval;
}

/* You shouldn't call this routine directly without first deactivating the servant... */
static void
impl_balsa_mailbox_info__destroy(impl_POA_balsa_mailbox_info * servant, CORBA_Environment * ev)
{

   POA_balsa_mailbox_info__fini((PortableServer_Servant) servant, ev);
   g_free(servant);
}

CORBA_boolean
impl_balsa_mailbox_info_newmail(impl_POA_balsa_mailbox_info * servant,
				CORBA_char * path,
				CORBA_Environment * ev)
{
   CORBA_boolean retval;
   retval = FALSE;
   return retval;
}

CORBA_long
impl_balsa_mailbox_info_num_msgs(impl_POA_balsa_mailbox_info * servant,
				 CORBA_char * path,
				 CORBA_Environment * ev)
{
   CORBA_long retval;
   retval = 0;
   return retval;
}
