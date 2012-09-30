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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "libbalsa.h"

#include <glib.h>

#include <string.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h>

#ifdef HAVE_NOTIFY
#include <libnotify/notify.h>
#endif

#if ENABLE_LDAP
#include <ldap.h>
#endif

#if HAVE_COMPFACE
#include <compface.h>
#endif                          /* HAVE_COMPFACE */

#if HAVE_GTKSOURCEVIEW
#include <gtksourceview/gtksourceview.h>
#include <gtksourceview/gtksourcebuffer.h>
/* note GtkSourceview 1 and 2 have a slightly different API */
#if (HAVE_GTKSOURCEVIEW == 1)
#  include <gtksourceview/gtksourcetag.h>
#  include <gtksourceview/gtksourcetagstyle.h>
#else
#  include <gtksourceview/gtksourcelanguage.h>
#  include <gtksourceview/gtksourcelanguagemanager.h>
#  include <gtksourceview/gtksourcestylescheme.h>
#  include <gtksourceview/gtksourcestyleschememanager.h>
#endif
#endif

#include "misc.h"
#include "missing.h"
#include <glib/gi18n.h>

#ifdef BALSA_USE_THREADS
static pthread_t main_thread_id;
static pthread_t libbalsa_threads_id;
#endif


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

#ifdef HAVE_NOTIFY
    notify_init("Basics");
#endif

#ifdef BALSA_USE_THREADS
    if (!g_thread_supported()) {
	g_error("Threads have not been initialised.");
    }
    main_thread_id = pthread_self();
#endif

    uname(&utsname);

    libbalsa_real_information_func = information_callback;

    g_mime_init(GMIME_ENABLE_RFC2047_WORKAROUNDS);

    GMIME_TYPE_DATA_WRAPPER;
    GMIME_TYPE_FILTER;
    GMIME_TYPE_FILTER_CRLF;
    GMIME_TYPE_PARSER;
    GMIME_TYPE_STREAM;
    GMIME_TYPE_STREAM_BUFFER;
    GMIME_TYPE_STREAM_MEM;
    GMIME_TYPE_STREAM_NULL;

    /* Register our types to avoid possible race conditions. See
       output of "valgrind --tool=helgrind --log-file=balsa.log balsa"
       Mailbox type registration is needed also for
       libbalsa_mailbox_new_from_config() to work. */
    LIBBALSA_TYPE_MAILBOX_LOCAL;
    LIBBALSA_TYPE_MAILBOX_POP3;
    LIBBALSA_TYPE_MAILBOX_IMAP;
    LIBBALSA_TYPE_MAILBOX_MBOX;
    LIBBALSA_TYPE_MAILBOX_MH;
    LIBBALSA_TYPE_MAILBOX_MAILDIR;
    LIBBALSA_TYPE_MESSAGE;

    LIBBALSA_TYPE_ADDRESS_BOOK_VCARD;
    LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN;
    LIBBALSA_TYPE_ADDRESS_BOOK_LDIF;
#if ENABLE_LDAP
    LIBBALSA_TYPE_ADDRESS_BOOK_LDAP;
#endif
#if HAVE_SQLITE
    LIBBALSA_TYPE_ADDRESS_BOOK_GPE;
#endif
#if HAVE_RUBRICA
    LIBBALSA_TYPE_ADDRESS_BOOK_RUBRICA;
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
    FILE *mailname_in = NULL;

    gchar* preset, *domain;
    if(g_getenv("EMAIL") != NULL){                  /* 1. */
        preset = g_strdup(g_getenv("EMAIL"));
    } else if( (mailname_in = fopen(MAILNAME_FILE, "r")) != NULL
              && fgets(hostbuf, sizeof(hostbuf)-1, mailname_in)){ /* 2. */
        hostbuf[sizeof(hostbuf)-1] = '\0';
        preset = g_strconcat(g_get_user_name(), "@", hostbuf, NULL);
        
    }else if((domain = libbalsa_get_domainname())){ /* 3. */
        preset = g_strconcat(g_get_user_name(), "@", domain, NULL);
        g_free(domain);    
    } else {                                        /* 4. */
        gethostname(hostbuf, 511);
        preset = g_strconcat(g_get_user_name(), "@", hostbuf, NULL);
    }
    if (mailname_in)
        fclose(mailname_in);
    return preset;
}

/* libbalsa_guess_mail_spool

   Returns an allocated gchar * with our best guess of the user's
   mail spool file.
*/
gchar *
libbalsa_guess_mail_spool(void)
{
    gchar *env;
    gchar *spool;
    static const gchar *guesses[] = {
	"/var/mail/",
	"/var/spool/mail/",
	"/usr/spool/mail/",
	"/usr/mail/",
	NULL
    };

    if ((env = getenv("MAIL")) != NULL)
	return g_strdup(env);

    if ((env = getenv("USER")) != NULL) {
        int i;

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
    return g_strconcat(g_get_home_dir(), "/mailbox", NULL);
}


gboolean libbalsa_ldap_exists(const gchar *server)
{
#if ENABLE_LDAP
    LDAP *ldap;
    ldap_initialize(&ldap, server);

    if(ldap) {
	ldap_unbind_ext(ldap, NULL, NULL);
	return TRUE;
    }
#endif /* #if ENABLE_LDAP */

    return FALSE;
}

gchar*
libbalsa_date_to_utf8(const time_t *date, const gchar *date_string)
{
    struct tm footime;
    gchar rettime[128];

    g_return_val_if_fail(date != NULL, NULL);
    g_return_val_if_fail(date_string != NULL, NULL);

    if (!*date)
        /* Missing "Date:" field?  It is required by RFC 2822. */
        return NULL;

    localtime_r(date, &footime);

    strftime(rettime, sizeof(rettime), date_string, &footime);

    return g_locale_to_utf8(rettime, -1, NULL, NULL, NULL);
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


#ifdef BALSA_USE_THREADS
#include <pthread.h>
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t condvar;
    int (*cb)(void *arg);
    void *arg;
    int res;
} AskData;

/* ask_cert_idle:
   called in MT mode by the main thread.
 */
static gboolean
ask_idle(gpointer data)
{
    AskData* ad = (AskData*)data;
    printf("ask_idle: ENTER %p\n", data);
    gdk_threads_enter();
    ad->res = (ad->cb)(ad->arg);
    gdk_threads_leave();
    pthread_cond_signal(&ad->condvar);
    printf("ask_idle: LEAVE %p\n", data);
    return FALSE;
}

/* libbalsa_ask_mt:
   executed with GDK UNLOCKED. see mailbox_imap_open() and
   imap_dir_cb()/imap_folder_imap_dir().
*/
static int
libbalsa_ask(gboolean (*cb)(void *arg), void *arg)
{
    AskData ad;

    if (pthread_self() == main_thread_id) {
        int ret;
        printf("Main thread asks the following question.\n");
        gdk_threads_enter();
        ret = cb(arg);
        gdk_threads_leave();
        return ret;
    }
    printf("Side thread asks the following question.\n");
    pthread_mutex_init(&ad.lock, NULL);
    pthread_cond_init(&ad.condvar, NULL);
    ad.cb  = cb;
    ad.arg = arg;

    pthread_mutex_lock(&ad.lock);
    pthread_cond_init(&ad.condvar, NULL);
    g_idle_add(ask_idle, &ad);
    pthread_cond_wait(&ad.condvar, &ad.lock);

    pthread_cond_destroy(&ad.condvar);
    pthread_mutex_unlock(&ad.lock);
    return ad.res;
}
#else /* BALSA_USE_THREADS */
static gboolean
libbalsa_ask(gboolean (*cb)(void *arg), void *arg)
{
    return cb(arg);
}
#endif /* BALSA_USE_THREADS */


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
    BIO *bio  = BIO_new(BIO_s_mem());
    strncpy(buf, _("Invalid date"), sizeof(buf)); buf[sizeof(buf)-1]='\0';

    if(ASN1_TIME_print(bio, tm)) {
        int cnt;
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
    char *c;

    strncpy (ret, _("Unknown"), sizeof (ret)); ret[sizeof(ret)-1]='\0';

    c = strstr(line, ndx);
    if (c) {
        char *c2;

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
    unsigned char md[EVP_MAX_MD_SIZE];


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
struct AskCertData {
    X509 *certificate;
    const char *explanation;
};

static int
ask_cert_real(void *data)
{
    static const char *part[] =
        {"/CN=", "/Email=", "/O=", "/OU=", "/L=", "/ST=", "/C="};

    struct AskCertData *acd = (struct AskCertData*)data;
    X509 *cert = acd->certificate;
    char buf[256]; /* fingerprint requires EVP_MAX_MD_SIZE*3 */
    char *name = NULL, *c, *valid_from, *valid_until;
    GtkWidget* dialog, *label;
    unsigned i;

    GString* str = g_string_new("");

    g_string_printf(str, _("Authenticity of this certificate "
                           "could not be verified.\n"
                           "<b>Reason:</b> %s\n"
                           "<b>This certificate belongs to:</b>\n"),
                    acd->explanation);

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

    /* This string uses markup, so we must replace "&" with "&amp;" */
    c = str->str;
    while ((c = strchr(c, '&'))) {
        gssize pos;

        pos = (c - str->str) + 1;
        g_string_insert(str, pos, "amp;");
        c = str->str + pos;
    }

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
    gtk_box_pack_start(GTK_BOX
                       (gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
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

static int
libbalsa_ask_for_cert_acceptance(X509 *cert, const char *explanation)
{
    struct AskCertData acd;
    acd.certificate = cert;
    acd.explanation = explanation;
    return libbalsa_ask(ask_cert_real, &acd);
}
#endif /* WITH_SSL */


static int
ask_timeout_real(void *data)
{
    const char *host = (const char*)data;
    GtkWidget* dialog;
    int i;

    dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                                    GTK_BUTTONS_YES_NO,
                                    _("Connection to %s timed out. Abort?"),
                                    host);
    gtk_window_set_wmclass(GTK_WINDOW(dialog), "timeout_dialog", "Balsa");
    switch(gtk_dialog_run(GTK_DIALOG(dialog))) {
    case GTK_RESPONSE_YES: i = 1; break;
    case GTK_RESPONSE_NO: i = 0; break;
    default: printf("Unknown response. Defaulting to 'yes'.\n");
        i = 1;
    }
    gtk_widget_destroy(dialog);
    /* Process some events to let the window disappear:
     * not really necessary but helps with debugging. */
   while(gtk_events_pending()) 
        gtk_main_iteration_do(FALSE);
    printf("%s returns %d\n", __FUNCTION__, i);
    return i;
}

gboolean
libbalsa_abort_on_timeout(const char *host)
{  /* It appears not to be entirely thread safe... Some locks do not
      get released as they should be. */
    char *hostname;

    hostname = g_alloca (strlen (host) + 1);
    strcpy (hostname, host);
    
    return libbalsa_ask(ask_timeout_real, hostname) != 0; 
}


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
static pthread_mutex_t mailbox_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  mailbox_cond  = PTHREAD_COND_INITIALIZER;

/* Lock/unlock a mailbox; no argument checking--we'll assume the caller
 * took care of that. 
 */
#define LIBBALSA_DEBUG_THREADS FALSE
void
libbalsa_lock_mailbox(LibBalsaMailbox * mailbox)
{
    pthread_t thread_id = pthread_self();
    gint count = 0;

    if (thread_id == libbalsa_threads_id
        && thread_id == mailbox->thread_id
        && mailbox->lock > 0) {
        /* We already have both locks, so we'll just hold on to both of
         * them. */
        ++mailbox->lock;
#if LIBBALSA_DEBUG_THREADS
        g_message("Avoided temporary gdk_threads_leave!!!");
#endif                          /* LIBBALSA_DEBUG_THREADS */
        return;
    }

    while (thread_id == libbalsa_threads_id) {
        ++count;
#if LIBBALSA_DEBUG_THREADS
        g_message("Temporary gdk_threads_leave!!!");
#endif                          /* LIBBALSA_DEBUG_THREADS */
        gdk_threads_leave();
    }

    pthread_mutex_lock(&mailbox_mutex);

    while (mailbox->lock && mailbox->thread_id != thread_id)
        pthread_cond_wait(&mailbox_cond, &mailbox_mutex);

    /* We'll assume that no-one would destroy a mailbox while we've been
     * trying to lock it. If they have, we have larger problems than
     * this reference! */
    mailbox->lock++;
    mailbox->thread_id = thread_id;

    pthread_mutex_unlock(&mailbox_mutex);

    while (--count >= 0) {
        gdk_threads_enter();
#if LIBBALSA_DEBUG_THREADS
        g_message("...and gdk_threads_enter!!!");
#endif                          /* LIBBALSA_DEBUG_THREADS */
    }
}

void
libbalsa_unlock_mailbox(LibBalsaMailbox * mailbox)
{
    pthread_t self;

    self = pthread_self();

    pthread_mutex_lock(&mailbox_mutex);

    if (mailbox->lock == 0 || self != mailbox->thread_id) {
	g_warning("Not holding mailbox lock!!!");
        pthread_mutex_unlock(&mailbox_mutex);
	return;
    }

    if(--mailbox->lock == 0) {
        pthread_cond_broadcast(&mailbox_cond);
        mailbox->thread_id = 0;
    }

    pthread_mutex_unlock(&mailbox_mutex);
}

/* Recursive mutex for gdk_threads_{enter,leave}. */
static pthread_mutex_t libbalsa_threads_mutex;
static guint libbalsa_threads_lock;

static void
libbalsa_threads_enter(void)
{
    pthread_t self;

    self = pthread_self();

    if (self != libbalsa_threads_id) {
        pthread_mutex_lock(&libbalsa_threads_mutex);
        libbalsa_threads_id = self;
    }
    ++libbalsa_threads_lock;
}

static void
libbalsa_threads_leave(void)
{
    pthread_t self;

    self = pthread_self();

    if (libbalsa_threads_lock == 0 || self != libbalsa_threads_id) {
        g_warning("%s: Not holding gdk lock!!!", __func__);
	return;
    }

    if (--libbalsa_threads_lock == 0) {
	if (self != main_thread_id)
	    gdk_display_flush(gdk_display_get_default());
        libbalsa_threads_id = 0;
        pthread_mutex_unlock(&libbalsa_threads_mutex);
    }
}

void
libbalsa_threads_init(void)
{
    pthread_mutex_init(&libbalsa_threads_mutex, NULL);
    gdk_threads_set_lock_functions(G_CALLBACK(libbalsa_threads_enter),
                                   G_CALLBACK(libbalsa_threads_leave));
}

void
libbalsa_threads_destroy(void)
{
    pthread_mutex_destroy(&libbalsa_threads_mutex);
}

gboolean
libbalsa_threads_has_lock(void)
{
    return libbalsa_threads_lock > 0
        && libbalsa_threads_id == pthread_self();
}

#endif				/* BALSA_USE_THREADS */

/* Initialized by the front end. */
void (*libbalsa_progress_set_text) (LibBalsaProgress * progress,
                                    const gchar * text, guint total);
void (*libbalsa_progress_set_fraction) (LibBalsaProgress * progress,
                                        gdouble fraction);
void (*libbalsa_progress_set_activity) (gboolean set, const gchar * text);

/*
 * Face and X-Face header support.
 */
gchar *
libbalsa_get_header_from_path(const gchar * header, const gchar * path,
                              gsize * size, GError ** err)
{
    gchar *buf, *content;
    size_t name_len;
    gchar *p, *q;

    if (!g_file_get_contents(path, &buf, size, err))
        return NULL;

    content = buf;
    name_len = strlen(header);
    if (g_ascii_strncasecmp(content, header, name_len) == 0)
        /* Skip header and trailing colon: */
        content += name_len + 1;

    /* Unfold. */
    for (p = q = content; *p; p++)
        if (*p != '\r' && *p != '\n')
            *q++ = *p;
    *q = '\0';

    content = g_strdup(content);
    g_free(buf);

    return content;
}

GtkWidget *
libbalsa_get_image_from_face_header(const gchar * content, GError ** err)
{
    GMimeStream *stream;
    GMimeStream *stream_filter;
    GMimeFilter *filter;
    GByteArray *array;
    GtkWidget *image = NULL;

    stream = g_mime_stream_mem_new();
    stream_filter = g_mime_stream_filter_new(stream);

    filter = g_mime_filter_basic_new(GMIME_CONTENT_ENCODING_BASE64, FALSE);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter), filter);
    g_object_unref(filter);

    g_mime_stream_write_string(stream_filter, content);
    g_object_unref(stream_filter);

    array = GMIME_STREAM_MEM(stream)->buffer;
    if (array->len == 0)
        g_set_error(err, LIBBALSA_IMAGE_ERROR,
                    LIBBALSA_IMAGE_ERROR_NO_DATA, _("No image data"));
    else {
        GdkPixbufLoader *loader =
            gdk_pixbuf_loader_new_with_type("png", NULL);

        gdk_pixbuf_loader_write(loader, array->data, array->len, err);
        gdk_pixbuf_loader_close(loader, *err ? NULL : err);

        if (!*err)
            image = gtk_image_new_from_pixbuf(gdk_pixbuf_loader_get_pixbuf
                                              (loader));
        g_object_unref(loader);
    }
    g_object_unref(stream);

    return image;
}

#if HAVE_COMPFACE
GtkWidget *
libbalsa_get_image_from_x_face_header(const gchar * content, GError ** err)
{
    gchar buf[2048];
    GdkPixbuf *pixbuf;
    guchar *pixels;
    gint lines;
    const gchar *p;
    GtkWidget *image = NULL;

    strncpy(buf, content, sizeof buf - 1);

    switch (uncompface(buf)) {
    case -1:
        g_set_error(err, LIBBALSA_IMAGE_ERROR, LIBBALSA_IMAGE_ERROR_FORMAT,
                    _("Invalid input format"));
        return image;
    case -2:
        g_set_error(err, LIBBALSA_IMAGE_ERROR, LIBBALSA_IMAGE_ERROR_BUFFER,
                    _("Internal buffer overrun"));
        return image;
    }

    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 48, 48);
    pixels = gdk_pixbuf_get_pixels(pixbuf);

    p = buf;
    for (lines = 48; lines > 0; --lines) {
        guint x[3];
        gint j, k;
        guchar *q;

        if (sscanf(p, "%8x,%8x,%8x,", &x[0], &x[1], &x[2]) != 3) {
            g_set_error(err, LIBBALSA_IMAGE_ERROR,
                        LIBBALSA_IMAGE_ERROR_BAD_DATA,
                        /* Translators: please do not translate Face. */
                        _("Bad X-Face data"));
            g_object_unref(pixbuf);
            return image;
        }
        for (j = 0, q = pixels; j < 3; j++)
            for (k = 15; k >= 0; --k){
                guchar c = x[j] & (1 << k) ? 0x00 : 0xff;
                *q++ = c;       /* red   */
                *q++ = c;       /* green */
                *q++ = c;       /* blue  */
            }
        p = strchr(p, '\n') + 1;
        pixels += gdk_pixbuf_get_rowstride(pixbuf);
    }

    image = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);

    return image;
}
#endif                          /* HAVE_COMPFACE */

#if HAVE_GTKSOURCEVIEW
GtkWidget *
libbalsa_source_view_new(gboolean highlight_phrases)
{
    GtkSourceBuffer *sbuffer;
    GtkWidget *sview;


    static GtkSourceLanguageManager * lm = NULL;
    static GtkSourceStyleScheme * scheme = NULL;
    static GtkSourceLanguage * src_lang = NULL;

    /* initialise the source language manager if necessary */
    if (!lm) {
	const gchar * const * lm_dpaths;

	if ((lm = gtk_source_language_manager_new()) &&
	    (lm_dpaths = gtk_source_language_manager_get_search_path(lm))) {
	    gchar ** lm_rpaths;
	    gint n;

	    /* add the balsa share path to the language manager's paths - we
	     * cannot simply replace it as it still wants to see the
	     * RelaxNG schema... */
	    for (n = 0; lm_dpaths[n]; n++);
	    lm_rpaths = g_new0(gchar *, n + 2);
	    for (n = 0; lm_dpaths[n]; n++)
		lm_rpaths[n] = g_strdup(lm_dpaths[n]);
	    lm_rpaths[n] = g_strdup(BALSA_DATA_PREFIX "/gtksourceview-2.0");
	    gtk_source_language_manager_set_search_path(lm, lm_rpaths);
	    g_strfreev(lm_rpaths);

	    /* try to load the language */
	    if ((src_lang =
		 gtk_source_language_manager_get_language(lm, "balsa"))) {
		GtkSourceStyleSchemeManager *smgr =
		    gtk_source_style_scheme_manager_new();
		gchar * sm_paths[] = {
		    BALSA_DATA_PREFIX "/gtksourceview-2.0",
		    NULL };
	    
		/* try to load the colouring scheme */
		gtk_source_style_scheme_manager_set_search_path(smgr, sm_paths);
		scheme = gtk_source_style_scheme_manager_get_scheme(smgr, "balsa-mail");
	    }
	}
    }

    /* create a new buffer and set the language and scheme */
    sbuffer = gtk_source_buffer_new(NULL);
    if (src_lang)
	gtk_source_buffer_set_language(sbuffer, src_lang);
    if (scheme)
	gtk_source_buffer_set_style_scheme(sbuffer, scheme);
    gtk_source_buffer_set_highlight_syntax(sbuffer, TRUE);
    gtk_source_buffer_set_highlight_matching_brackets(sbuffer, FALSE);

    /* create & return the source view */
    sview = gtk_source_view_new_with_buffer(sbuffer);
    g_object_unref(sbuffer);

    return sview;
}
#endif  /* HAVE_GTKSOURCEVIEW */

/*
 * Error domains for GError:
 */

GQuark
libbalsa_scanner_error_quark(void)
{
    static GQuark quark = 0;
    if (quark == 0)
        quark = g_quark_from_static_string("libbalsa-scanner-error-quark");
    return quark;
}

GQuark
libbalsa_mailbox_error_quark(void)
{
    static GQuark quark = 0;
    if (quark == 0)
        quark = g_quark_from_static_string("libbalsa-mailbox-error-quark");
    return quark;
}

GQuark
libbalsa_image_error_quark(void)
{
    static GQuark quark = 0;
    if (quark == 0)
        quark = g_quark_from_static_string("libbalsa-image-error-quark");
    return quark;
}
