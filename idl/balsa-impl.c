#include "balsa.h"

/*** App-specific servant structures ***/
typedef struct {
   POA_Balsa_SendMail servant;
   PortableServer_POA poa;

} impl_POA_Balsa_SendMail;

typedef struct {
   POA_Balsa_MailboxInfo servant;
   PortableServer_POA poa;

} impl_POA_Balsa_MailboxInfo;

/*** Implementation stub prototypes ***/
static void impl_Balsa_SendMail__destroy(impl_POA_Balsa_SendMail * servant,
					  CORBA_Environment * ev);

CORBA_boolean
impl_Balsa_SendMail_To(impl_POA_Balsa_SendMail * servant,
			CORBA_char * to,
			CORBA_Environment * ev);

CORBA_boolean
impl_Balsa_SendMail_Attachment(impl_POA_Balsa_SendMail * servant,
				CORBA_char * to,
				CORBA_char * filename,
				CORBA_Environment * ev);

static void impl_Balsa_MailboxInfo__destroy(impl_POA_Balsa_MailboxInfo * servant,
					     CORBA_Environment * ev);

CORBA_boolean
impl_Balsa_MailboxInfo_NewMail(impl_POA_Balsa_MailboxInfo * servant,
				CORBA_char * path,
				CORBA_Environment * ev);

CORBA_long
impl_Balsa_MailboxInfo_NumMsgs(impl_POA_Balsa_MailboxInfo * servant,
				 CORBA_char * path,
				 CORBA_Environment * ev);

/*** epv structures ***/
static PortableServer_ServantBase__epv impl_Balsa_SendMail_base_epv =
{
   NULL,			/* _private data */
   (gpointer) & impl_Balsa_SendMail__destroy,	/* finalize routine */
   NULL,			/* default_POA routine */
};
static POA_Balsa_SendMail__epv impl_Balsa_SendMail_epv =
{
   NULL,			/* _private */
   (gpointer) & impl_Balsa_SendMail_To,

   (gpointer) & impl_Balsa_SendMail_Attachment,

};
static PortableServer_ServantBase__epv impl_Balsa_MailboxInfo_base_epv =
{
   NULL,			/* _private data */
   (gpointer) & impl_Balsa_MailboxInfo__destroy,	/* finalize routine */
   NULL,			/* default_POA routine */
};
static POA_Balsa_MailboxInfo__epv impl_Balsa_MailboxInfo_epv =
{
   NULL,			/* _private */
   (gpointer) & impl_Balsa_MailboxInfo_NewMail,

   (gpointer) & impl_Balsa_MailboxInfo_NumMsgs,

};

/*** vepv structures ***/
static POA_Balsa_SendMail__vepv impl_Balsa_SendMail_vepv =
{
   &impl_Balsa_SendMail_base_epv,
   &impl_Balsa_SendMail_epv,
};

static POA_Balsa_MailboxInfo__vepv impl_Balsa_MailboxInfo_vepv =
{
   &impl_Balsa_MailboxInfo_base_epv,
   &impl_Balsa_MailboxInfo_epv,
};

/*** Stub implementations ***/
static Balsa_SendMail 
impl_Balsa_SendMail__create(PortableServer_POA poa, CORBA_Environment * ev)
{
   Balsa_SendMail retval;
   impl_POA_Balsa_SendMail *newservant;
   PortableServer_ObjectId *objid;

   newservant = g_new0(impl_POA_Balsa_SendMail, 1);
   newservant->servant.vepv = &impl_Balsa_SendMail_vepv;
   newservant->poa = poa;
   POA_Balsa_SendMail__init((PortableServer_Servant) newservant, ev);
   objid = PortableServer_POA_activate_object(poa, newservant, ev);
   CORBA_free(objid);
   retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);

   return retval;
}

/* You shouldn't call this routine directly without first deactivating the servant... */
static void
impl_Balsa_SendMail__destroy(impl_POA_Balsa_SendMail * servant, CORBA_Environment * ev)
{

   POA_Balsa_SendMail__fini((PortableServer_Servant) servant, ev);
   g_free(servant);
}

CORBA_boolean
impl_Balsa_SendMail_To(impl_POA_Balsa_SendMail * servant,
			CORBA_char * to,
			CORBA_Environment * ev)
{
   CORBA_boolean retval;
   retval = FALSE;
   return retval;
}

CORBA_boolean
impl_Balsa_SendMail_Attachment(impl_POA_Balsa_SendMail * servant,
				CORBA_char * to,
				CORBA_char * filename,
				CORBA_Environment * ev)
{
   CORBA_boolean retval;
   retval = FALSE;
   return retval;
}

static Balsa_MailboxInfo 
impl_Balsa_MailboxInfo__create(PortableServer_POA poa, CORBA_Environment * ev)
{
   Balsa_MailboxInfo retval;
   impl_POA_Balsa_MailboxInfo *newservant;
   PortableServer_ObjectId *objid;

   newservant = g_new0(impl_POA_Balsa_MailboxInfo, 1);
   newservant->servant.vepv = &impl_Balsa_MailboxInfo_vepv;
   newservant->poa = poa;
   POA_Balsa_MailboxInfo__init((PortableServer_Servant) newservant, ev);
   objid = PortableServer_POA_activate_object(poa, newservant, ev);
   CORBA_free(objid);
   retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);

   return retval;
}

/* You shouldn't call this routine directly without first deactivating the servant... */
static void
impl_Balsa_MailboxInfo__destroy(impl_POA_Balsa_MailboxInfo * servant, CORBA_Environment * ev)
{

   POA_Balsa_MailboxInfo__fini((PortableServer_Servant) servant, ev);
   g_free(servant);
}

CORBA_boolean
impl_Balsa_MailboxInfo_NewMail(impl_POA_Balsa_MailboxInfo * servant,
				CORBA_char * path,
				CORBA_Environment * ev)
{
   CORBA_boolean retval;
   retval = FALSE;
   return retval;
}

CORBA_long
impl_Balsa_MailboxInfo_NumMsgs(impl_POA_Balsa_MailboxInfo * servant,
				 CORBA_char * path,
				 CORBA_Environment * ev)
{
   CORBA_long retval;
   retval = 0;
   return retval;
}
