#include "balsa.h"
#include <gnome.h>
#include <libgnorba/gnorba.h>

/*** App-specific servant structures ***/
typedef struct {
   POA_balsa_simple_send servant;
   PortableServer_POA poa;

} impl_POA_balsa_simple_send;

/*** Implementation stub prototypes ***/
static void impl_balsa_simple_send__destroy(impl_POA_balsa_simple_send * servant,
					    CORBA_Environment * ev);

CORBA_long
impl_balsa_simple_send_send_file_as_message(impl_POA_balsa_simple_send * servant,
					    CORBA_char * froM,
					    CORBA_char * to,
					    CORBA_char * filename,
					    CORBA_Environment * ev);

/*** epv structures ***/
static PortableServer_ServantBase__epv impl_balsa_simple_send_base_epv =
{
   NULL,			/* _private data */
   (gpointer) & impl_balsa_simple_send__destroy,	/* finalize routine */
   NULL,			/* default_POA routine */
};
static POA_balsa_simple_send__epv impl_balsa_simple_send_epv =
{
   NULL,			/* _private */
   (gpointer) & impl_balsa_simple_send_send_file_as_message,

};

/*** vepv structures ***/
static POA_balsa_simple_send__vepv impl_balsa_simple_send_vepv =
{
   &impl_balsa_simple_send_base_epv,
   &impl_balsa_simple_send_epv,
};

static const CosNaming_NameComponent nc[3] = {{"GNOME", "subcontext"},
					      {"Servers", "subcontext"},
					      {"balsa", "object"}};
static const CosNaming_Name          nom = {0, 3, nc, CORBA_FALSE};

static GSList *objrefs = NULL;

static void do_ns_unregisters(void)
{
  CORBA_Environment ev;
  CORBA_Object ns = gnome_name_service_get();

  CORBA_exception_init(&ev);

  CosNaming_NamingContext_unbind(ns, &nom, &ev);

  CORBA_Object_release(ns, &ev);

  CORBA_exception_free(&ev);
}

balsa_simple_send 
impl_balsa_simple_send__create(PortableServer_POA poa, CORBA_Environment * ev)
{
   balsa_simple_send retval;
   impl_POA_balsa_simple_send *newservant;
   PortableServer_ObjectId *objid;
   CORBA_Object ns = gnome_name_service_get();

   newservant = g_new0(impl_POA_balsa_simple_send, 1);
   newservant->servant.vepv = &impl_balsa_simple_send_vepv;
   newservant->poa = poa;
   POA_balsa_simple_send__init((PortableServer_Servant) newservant, ev);
   objid = PortableServer_POA_activate_object(poa, newservant, ev);
   CORBA_free(objid);
   retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);

   CosNaming_NamingContext_bind(ns, &nom, retval, ev);

   if(ev->_major == CORBA_NO_EXCEPTION) {
     if(!objrefs)
       g_atexit(do_ns_unregisters);

     objrefs = g_slist_append(objrefs, CORBA_Object_duplicate(retval, ev));
   }

   CORBA_Object_release(ns, ev);

   return retval;
}

/* You shouldn't call this routine directly without first deactivating the servant... */
static void
impl_balsa_simple_send__destroy(impl_POA_balsa_simple_send * servant, CORBA_Environment * ev)
{

   POA_balsa_simple_send__fini((PortableServer_Servant) servant, ev);
   g_free(servant);
}

CORBA_long
impl_balsa_simple_send_send_file_as_message(impl_POA_balsa_simple_send * servant,
					    CORBA_char * from,
					    CORBA_char * to,
					    CORBA_char * filename,
					    CORBA_Environment * ev)
{
   CORBA_long retval;
   retval = send_file_as_msg(from, to, filename);
   return retval;
}
