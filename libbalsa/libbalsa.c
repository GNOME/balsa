/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#define _XOPEN_SOURCE 500

#include <glib.h>
#include <gnome.h>

#include <string.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <stdarg.h>
#include <unistd.h>

#if ENABLE_LDAP
#include <ldap.h>
#endif

#include "address-book-gpe.h"
#include "libbalsa.h"
#include "misc.h"

#ifdef BALSA_USE_THREADS
static pthread_t main_thread_id;
#endif

#define POP_SERVER "pop"
#define IMAP_SERVER "mx"
#define LDAP_SERVER "ldap"


static gchar *qualified_hostname(const char *name);

void
libbalsa_message(const char *fmt, ...)
{
    va_list va_args;

    va_start(va_args, fmt);
    libbalsa_information_varg(NULL, LIBBALSA_INFORMATION_MESSAGE,
                              fmt, va_args);
    va_end(va_args);
}

void
libbalsa_init(LibBalsaInformationFunc information_callback)
{
    struct utsname utsname;


#ifdef BALSA_USE_THREADS
    if (!g_thread_supported()) {
	g_error("Threads have not been initialised.");
    }
    main_thread_id = pthread_self();
#endif

    uname(&utsname);

    libbalsa_real_information_func = information_callback;

    g_mime_init(0);

    /* Register our types */
    /* So that libbalsa_mailbox_new_from_config will work... */
    LIBBALSA_TYPE_MAILBOX_LOCAL;
    LIBBALSA_TYPE_MAILBOX_POP3;
    LIBBALSA_TYPE_MAILBOX_IMAP;
    LIBBALSA_TYPE_MAILBOX_MBOX;
    LIBBALSA_TYPE_MAILBOX_MH;
    LIBBALSA_TYPE_MAILBOX_MAILDIR;

    LIBBALSA_TYPE_ADDRESS_BOOK_VCARD;
    LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN;
    LIBBALSA_TYPE_ADDRESS_BOOK_LDIF;
#if ENABLE_LDAP
    LIBBALSA_TYPE_ADDRESS_BOOK_LDAP;
#endif
#if HAVE_SQLITE
    LIBBALSA_TYPE_ADDRESS_BOOK_GPE;
#endif
}


/* libbalsa_rot:
   return rot13'ed string.
*/
gchar *
libbalsa_rot(const gchar * pass)
{
    gchar *buff;
    gint len = 0, i = 0;

    /*PKGW: let's do the assert() BEFORE we coredump... */

    len = strlen(pass);
    buff = g_strdup(pass);

    for (i = 0; i < len; i++) {
	if ((buff[i] <= 'M' && buff[i] >= 'A')
	    || (buff[i] <= 'm' && buff[i] >= 'a'))
	    buff[i] += 13;
	else if ((buff[i] <= 'Z' && buff[i] >= 'N')
		 || (buff[i] <= 'z' && buff[i] >= 'n'))
	    buff[i] -= 13;
    }
    return buff;
}


/* libbalsa_guess_email_address:
   Email address can be determined in four ways:
   1. Using the environment variable 'EMAIL'

   2. The file '/etc/mailname' should contain the external host
      address for the host. Prepend the username (`username`@`cat
      /etc/mailname`).

   3. Append the domainname to the user name.
   4. Append the hostname to the user name.

*/
gchar*
libbalsa_guess_email_address(void)
{
    /* Q: Find this location with configure? or at run-time? */
    static const gchar* MAILNAME_FILE = "/etc/mailname";
    char hostbuf[512];

    gchar* preset, *domain;
    if(g_getenv("EMAIL") != NULL){                  /* 1. */
        preset = g_strdup(g_getenv("EMAIL"));
    } else if(access(MAILNAME_FILE, F_OK) == 0){    /* 2. */
        FILE *mailname_in = fopen(MAILNAME_FILE, "r");
        fgets(hostbuf, 511, mailname_in);
        hostbuf[strlen(hostbuf)-1] = '\0';
        fclose(mailname_in);
        preset = g_strconcat(g_get_user_name(), "@", hostbuf, NULL);
        
    }else if((domain = libbalsa_get_domainname())){ /* 3. */
        preset = g_strconcat(g_get_user_name(), "@", domain, NULL);
        g_free(domain);    
    } else {                                        /* 4. */
        gethostname(hostbuf, 511);
        preset = g_strconcat(g_get_user_name(), "@", hostbuf, NULL);
    }
    return preset;
}

/* libbalsa_guess_mail_spool

   Returns an allocated gchar * with our best guess of the user's
   mail spool file.
*/
gchar *
libbalsa_guess_mail_spool(void)
{
    int i;
    gchar *env;
    gchar *spool;
    static const gchar *guesses[] = {
	"/var/spool/mail/",
	"/var/mail/",
	"/usr/spool/mail/",
	"/usr/mail/",
	NULL
    };

    if ((env = getenv("MAIL")) != NULL)
	return g_strdup(env);

    if ((env = getenv("USER")) != NULL) {
	for (i = 0; guesses[i] != NULL; i++) {
	    spool = g_strconcat(guesses[i], env, NULL);

	    if (g_file_test(spool, G_FILE_TEST_EXISTS))
		return spool;

	    g_free(spool);
	}
    }

    /* libmutt's configure.in indicates that this 
     * ($HOME/mailbox) exists on
     * some systems, and it's a good enough default if we
     * can't guess it any other way. */
    return gnome_util_prepend_user_home("mailbox");
}

/* Some more "guess" functions symmetric to libbalsa_guess_mail_spool()... */


static gchar *qualified_hostname(const char *name)
{
    gchar *domain=libbalsa_get_domainname();

    if(domain) {
	gchar *host=g_strdup_printf("%s.%s", name, domain);
	
	g_free(domain);

	return host;
    } else
	return g_strdup(name);
}


gchar *libbalsa_guess_pop_server()
{
    return qualified_hostname(POP_SERVER);
}

gchar *libbalsa_guess_imap_server()
{
    return qualified_hostname(IMAP_SERVER);
}

gchar *libbalsa_guess_ldap_server()
{
    return qualified_hostname(LDAP_SERVER);
}

gchar *libbalsa_guess_imap_inbox()
{
    gchar *server = libbalsa_guess_imap_server();

    if(server) {
	gchar *url = g_strdup_printf("imap://%s/INBOX", server);
	
	g_free(server);

	return url;
    }

    return NULL;
}

gchar *libbalsa_guess_ldap_base()
{
    gchar *server = libbalsa_guess_ldap_server();

    /* Note: Assumes base dn is "o=<domain name>". Somewhat speculative... */
    if(server) {
	gchar *base=NULL, *domain;

	if((domain=strchr(server, '.')))
	   base = g_strdup_printf("o=%s", domain+1);
	
	g_free(server);

	return base;
    }
    return NULL;
}

gchar *libbalsa_guess_ldap_name()
{
    gchar *base = libbalsa_guess_ldap_base();

    if(base) {
	gchar *name = strchr(base, '=');
	gchar *dir_name = g_strdup_printf(_("LDAP Directory for %s"), 
					  (name?name+1:base));
	g_free(base);

	return dir_name;
    } 

    return NULL;
}

gchar *libbalsa_guess_ldif_file()
{
    int i;
    gchar *ldif;

    static const gchar *guesses[] = {
	"address.ldif",
	".address.ldif",
	"address-book.ldif",
	".address-book.ldif",
	".addressbook.ldif",
	NULL
    };

    for (i = 0; guesses[i] != NULL; i++) {
	ldif = gnome_util_prepend_user_home(guesses[i]);
	
	if (g_file_test(ldif, G_FILE_TEST_EXISTS))
	     return ldif;
	  
	g_free(ldif);
    }
    return  gnome_util_prepend_user_home(guesses[0]); /* *** Or NULL */
    
}

gboolean libbalsa_ldap_exists(const gchar *server)
{
#if ENABLE_LDAP
    LDAP *ldap = ldap_open(server, LDAP_PORT);

    if(ldap) {
	ldap_unbind(ldap);

	return TRUE;
    }
#endif /* #if ENABLE_LDAP */

    return FALSE;
}

void
libbalsa_assure_balsa_dir(void)
{
    gchar* dir = g_strconcat(g_get_home_dir(), "/.balsa", NULL);
    mkdir(dir, S_IRUSR|S_IWUSR|S_IXUSR);
    g_free(dir);
}

gchar*
libbalsa_date_to_gchar(const time_t *date, const gchar *date_string)
{
    struct tm footime;
    gchar rettime[128];

    g_return_val_if_fail(date != NULL, NULL);
    g_return_val_if_fail(date_string != NULL, NULL);

    localtime_r(date, &footime);

    strftime(rettime, sizeof(rettime), date_string, &footime);

    return g_strdup(rettime);
}

LibBalsaMessageStatus
libbalsa_get_icon_from_flags(LibBalsaMessageFlag flags)
{
    LibBalsaMessageStatus icon;
    if (flags & LIBBALSA_MESSAGE_FLAG_DELETED)
	icon = LIBBALSA_MESSAGE_STATUS_DELETED;
    else if (flags & LIBBALSA_MESSAGE_FLAG_NEW)
	icon = LIBBALSA_MESSAGE_STATUS_UNREAD;
    else if (flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)
	icon = LIBBALSA_MESSAGE_STATUS_FLAGGED;
    else if (flags & LIBBALSA_MESSAGE_FLAG_REPLIED)
	icon = LIBBALSA_MESSAGE_STATUS_REPLIED;
    else
	icon = LIBBALSA_MESSAGE_STATUS_ICONS_NUM;
    return icon;
}


#if defined(USE_SSL)
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/err.h>
static int libbalsa_ask_for_cert_acceptance(X509 *cert,
					    const char *explanation);
static char*
asn1time_to_string(ASN1_UTCTIME *tm)
{
    char buf[64];
    int cnt;
    BIO *bio  = BIO_new(BIO_s_mem());
    strncpy(buf, _("Invalid date"), sizeof(buf)); buf[sizeof(buf)-1]='\0';

    if(ASN1_TIME_print(bio, tm)) {
        cnt = BIO_read(bio, buf, sizeof(buf)-1);
        buf[cnt] = '\0';
    }
    BIO_free(bio);
    return g_strdup(buf);
}

static char*
x509_get_part (char *line, const char *ndx)
{
    static char ret[256];
    char *c, *c2;
    
    strncpy (ret, _("Unknown"), sizeof (ret)); ret[sizeof(ret)-1]='\0';
    
    c = strstr(line, ndx);
    if (c) {
        c += strlen (ndx);
        c2 = strchr (c, '/');
        if (c2)
            *c2 = '\0';
        strncpy (ret, c, sizeof (ret));
        if (c2)
            *c2 = '/';
    }
    
    return ret;
}
static void
x509_fingerprint (char *s, unsigned len, X509 * cert)
{
    unsigned j, i, n, c;
    char md[EVP_MAX_MD_SIZE];


    X509_digest(cert, EVP_md5(), md, &n);
    if(len<3*n) n = len/3;
    for (j=i=0; j<n; j++) {
        c = (md[j] >>4) & 0xF; s[i++] = c<10 ? c + '0' : c+'A'-10;
        c = md[j] & 0xF;       s[i++] = c<10 ? c + '0' : c+'A'-10;
        if(j<n-1) s[i++] = ':';
    }
    s[i] = '\0';
}

static GList *accepted_certs = NULL; /* certs accepted for this session */

#ifdef BALSA_USE_THREADS
static pthread_mutex_t certificate_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK_CERTIFICATES   pthread_mutex_lock(&certificate_lock)
#define UNLOCK_CERTIFICATES pthread_mutex_unlock(&certificate_lock)
#else
#define LOCK_CERTIFICATES
#define UNLOCK_CERTIFICATES
#endif

void
libbalsa_certs_destroy(void)
{
    LOCK_CERTIFICATES;
    g_list_foreach(accepted_certs, (GFunc)X509_free, NULL);
    g_list_free(accepted_certs);
    accepted_certs = NULL;
    UNLOCK_CERTIFICATES;
}

/* compare Example 10-7 in the OpenSSL book */
gboolean
libbalsa_is_cert_known(X509* cert, long vfy_result)
{
    X509 *tmpcert = NULL;
    FILE *fp;
    gchar *cert_name;
    gboolean res = FALSE;
    GList *lst;

    LOCK_CERTIFICATES;
    for(lst = accepted_certs; lst; lst = lst->next) {
        int res = X509_cmp(cert, lst->data);
        if(res == 0) {
	    UNLOCK_CERTIFICATES;
            return TRUE;
	}
    }
    
    cert_name = g_strconcat(g_get_home_dir(), "/.balsa/certificates", NULL);

    fp = fopen(cert_name, "rt");
    g_free(cert_name);
    if(fp) {
        /* 
        printf("Looking for cert: %s\n", 
               X509_NAME_oneline(X509_get_subject_name (cert),
                                 buf, sizeof (buf)));
        */
        res = FALSE;
        while ((tmpcert = PEM_read_X509(fp, NULL, NULL, NULL)) != NULL) {
            res = X509_cmp(cert, tmpcert)==0;
            X509_free(tmpcert);
            if(res) break;
        }
        ERR_clear_error();
        fclose(fp);
    }
    UNLOCK_CERTIFICATES;
    
    if(!res) {
	const char *reason = X509_verify_cert_error_string(vfy_result);
	res = libbalsa_ask_for_cert_acceptance(cert, reason);
	LOCK_CERTIFICATES;
	if(res == 2) {
	    cert_name = g_strconcat(g_get_home_dir(),
				    "/.balsa/certificates", NULL);
            libbalsa_assure_balsa_dir();
	    fp = fopen(cert_name, "a");
	    if (fp) {
		if(PEM_write_X509 (fp, cert))
		    res = TRUE;
		fclose(fp);
	    }
	    g_free(cert_name);
	}
	if(res == 1)
	    accepted_certs = 
		g_list_prepend(accepted_certs, X509_dup(cert));
	UNLOCK_CERTIFICATES;
    }

    return res;
}

/* libbalsa_ask_for_cert_acceptance():
   returns:
   OP_EXIT on reject.
   OP_SAVE - on accept and save.
   OP_MAX - on accept once.
   TODO: check treading issues.

*/
static int
ask_cert_real(X509 *cert, const char *explanation)
{

    char *part[] =
        {"/CN=", "/Email=", "/O=", "/OU=", "/L=", "/ST=", "/C="};
    char buf[256]; /* fingerprint requires EVP_MAX_MD_SIZE*3 */
    char *name = NULL, *c, *valid_from, *valid_until;
    GtkWidget* dialog, *label;
    unsigned i;

    GString* str = g_string_new("");

    g_string_printf(str, _("Authenticity of this certificate "
                           "could not be verified.\n"
                           "<b>Reason:</b> %s\n"
                           "<b>This certificate belongs to:</b>\n"),
                    explanation);

    name = X509_NAME_oneline(X509_get_subject_name (cert), buf, sizeof (buf));
    for (i = 0; i < ELEMENTS(part); i++) {
        g_string_append(str, x509_get_part (name, part[i]));
        g_string_append_c(str, '\n');
    }

    g_string_append(str, _("\n<b>This certificate was issued by:</b>\n"));
    name = X509_NAME_oneline(X509_get_issuer_name(cert), buf, sizeof (buf));
    for (i = 0; i < ELEMENTS(part); i++) {
        g_string_append(str, x509_get_part (name, part[i]));
        g_string_append_c(str, '\n');
    }

    buf[0] = '\0';
    x509_fingerprint (buf, sizeof (buf), cert);
    valid_from  = asn1time_to_string(X509_get_notBefore(cert));
    valid_until = asn1time_to_string(X509_get_notAfter(cert)),
    c = g_strdup_printf(_("<b>This certificate is valid</b>\n"
                          "from %s\n"
                          "to %s\n"
                          "<b>Fingerprint:</b> %s"),
                        valid_from, valid_until,
                        buf);
    g_string_append(str, c); g_free(c);
    g_free(valid_from); g_free(valid_until);

    dialog = gtk_dialog_new_with_buttons(_("SSL/TLS certificate"), NULL,
                                         GTK_DIALOG_MODAL,
                                         _("_Accept Once"), 0,
                                         _("Accept&_Save"), 1,
                                         _("_Reject"), GTK_RESPONSE_CANCEL, 
                                         NULL);
    gtk_window_set_wmclass(GTK_WINDOW(dialog), "tls_cert_dialog", "Balsa");
    label = gtk_label_new(str->str);
    g_string_free(str, TRUE);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
                       label, TRUE, TRUE, 1);
    gtk_widget_show(label);

    switch(gtk_dialog_run(GTK_DIALOG(dialog))) {
    case 0: i = 1; break;
    case 1: i = 2; break;
    case GTK_RESPONSE_CANCEL:
    default: i=0; break;
    }
    gtk_widget_destroy(dialog);
    /* Process some events to let the window disappear:
     * not really necessary but helps with debugging. */
   while(gtk_events_pending()) 
        gtk_main_iteration_do(FALSE);
    printf("%s returns %d\n", __FUNCTION__, i);
    return i;
}

#ifdef BALSA_USE_THREADS
#include <pthread.h>
typedef struct {
    pthread_cond_t cond;
    X509 *cert;
    const char *expl;
    int res;
} AskCertData;
/* ask_cert_idle:
   called in MT mode by the main thread.
 */
static gboolean
ask_cert_idle(gpointer data)
{
    AskCertData* acd = (AskCertData*)data;
    gdk_threads_enter();
    acd->res = ask_cert_real(acd->cert, acd->expl);
    gdk_threads_leave();
    pthread_cond_signal(&acd->cond);
    return FALSE;
}
/* libmutt_ask_for_cert_acceptance:
   executed with GDK UNLOCKED. see mailbox_imap_open() and
   imap_dir_cb()/imap_folder_imap_dir().
*/
static int
libbalsa_ask_for_cert_acceptance(X509 *cert, const char *explanation)
{
    static pthread_mutex_t ask_cert_lock = PTHREAD_MUTEX_INITIALIZER;
    AskCertData acd;

    if (pthread_self() == main_thread_id)
	return ask_cert_real(cert, explanation);

    pthread_mutex_lock(&ask_cert_lock);
    pthread_cond_init(&acd.cond, NULL);
    acd.cert = cert;
    acd.expl = explanation;
    g_idle_add(ask_cert_idle, &acd);
    pthread_cond_wait(&acd.cond, &ask_cert_lock);
    
    pthread_cond_destroy(&acd.cond);
    pthread_mutex_unlock(&ask_cert_lock);
    return acd.res;
}
#else /* BALSA_USE_THREADS */
static int
libbalsa_ask_for_cert_acceptance(X509 *cert, const char *explanation)
{
    return ask_cert_real(cert, explanation);
}
#endif /* BALSA_USE_THREADS */

#endif /* WITH_SSL */


#ifdef BALSA_USE_THREADS
pthread_t
libbalsa_get_main_thread(void)
{
    return main_thread_id;
}

gboolean
libbalsa_am_i_subthread(void)
{
    return pthread_self() != main_thread_id;
}
#endif /* BALSA_USE_THREADS */

#ifdef BALSA_USE_THREADS
#include "libbalsa_private.h"	/* for prototypes */
pthread_mutex_t mailbox_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mailbox_cond = PTHREAD_COND_INITIALIZER;

/* Lock/unlock a mailbox; no argument checking--we'll assume the caller
 * took care of that. 
 * We assume that this function is called for helper threads without gdk lock
 * since they usually do not have it but WITH for the main lock - which 
 * usually has it.
 */
void
libbalsa_lock_mailbox(LibBalsaMailbox * mailbox)
{
    pthread_t thread_id = pthread_self();
    gboolean is_main_thread =
	(thread_id == main_thread_id);

    pthread_mutex_lock(&mailbox_lock);
    while (mailbox->lock && mailbox->thread_id != thread_id) {
        if(is_main_thread) gdk_threads_leave();
	pthread_cond_wait(&mailbox_cond, &mailbox_lock);
        if(is_main_thread) gdk_threads_enter();
    }
    /* We'll assume that no-one would destroy a mailbox while we've been
     * trying to lock it. If they have, we have larger problems than
     * this reference! */
    mailbox->lock++;
    mailbox->thread_id = thread_id;
    pthread_mutex_unlock(&mailbox_lock);
}

void
libbalsa_unlock_mailbox(LibBalsaMailbox * mailbox)
{
    pthread_mutex_lock(&mailbox_lock);
    if(!--mailbox->lock)
        pthread_cond_broadcast(&mailbox_cond);
    pthread_mutex_unlock(&mailbox_lock);
}
#endif				/* BALSA_USE_THREADS */
