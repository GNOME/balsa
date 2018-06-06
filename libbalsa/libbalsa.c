/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "libbalsa.h"

#include <glib.h>

#include <string.h>
#include <stdlib.h>
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
#include <gtksourceview/gtksource.h>
#endif

#if HAVE_GCR
#define GCR_API_SUBJECT_TO_CHANGE
#include <gcr/gcr.h>
#else
#include <gnutls/x509.h>
#endif

#include "misc.h"
#include "missing.h"
#include <glib/gi18n.h>

static GThread *main_thread_id;


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
#ifdef HAVE_NOTIFY
    notify_init("Basics");
#endif

    main_thread_id = g_thread_self();

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
#if HAVE_OSMO
    LIBBALSA_TYPE_ADDRESS_BOOK_OSMO;
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
    gchar* preset, *domain;
    gchar *mailname;

    if(g_getenv("EMAIL") != NULL){                  /* 1. */
        preset = g_strdup(g_getenv("EMAIL"));
    } else if (g_file_get_contents(MAILNAME_FILE, &mailname, NULL, NULL)) { /* 2. */
    	gchar *newline;

    	newline = strchr(mailname, '\n');
    	if (newline != NULL) {
    		newline[0] = '\0';
    	}
        preset = g_strconcat(g_get_user_name(), "@", mailname, NULL);
        g_free(mailname);
    }else if((domain = libbalsa_get_domainname())){ /* 3. */
        preset = g_strconcat(g_get_user_name(), "@", domain, NULL);
        g_free(domain);    
    } else {                                        /* 4. */
        char hostbuf[512];

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
libbalsa_date_to_utf8(const time_t date, const gchar *date_string)
{
	gchar *result;

    g_return_val_if_fail(date_string != NULL, NULL);

    if (date == (time_t) 0) {
        /* Missing "Date:" field?  It is required by RFC 2822. */
        result = NULL;
    } else {
    	GDateTime *footime;

    	footime = g_date_time_new_from_unix_local(date);
    	result = g_date_time_format(footime, date_string);
    	g_date_time_unref(footime);
    }
    return result;
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


typedef struct {
    GMutex lock;
    GCond condvar;
    int (*cb)(void *arg);
    void *arg;
    gboolean done;
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
    ad->res = (ad->cb)(ad->arg);
    ad->done = TRUE;
    g_cond_signal(&ad->condvar);
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

    if (!libbalsa_am_i_subthread()) {
        int ret;
        printf("Main thread asks the following question.\n");
        ret = cb(arg);
        return ret;
    }
    printf("Side thread asks the following question.\n");
    g_mutex_init(&ad.lock);
    g_cond_init(&ad.condvar);
    ad.cb  = cb;
    ad.arg = arg;
    ad.done = FALSE;

    g_mutex_lock(&ad.lock);
    g_idle_add(ask_idle, &ad);
    while (!ad.done) {
    	g_cond_wait(&ad.condvar, &ad.lock);
    }

    g_cond_clear(&ad.condvar);
    g_mutex_unlock(&ad.lock);
    return ad.res;
}


static int libbalsa_ask_for_cert_acceptance(GTlsCertificate      *cert,
											GTlsCertificateFlags  errors);

static GList *accepted_certs = NULL; /* GTlsCertificate items accepted for this session */
static GMutex certificate_lock;

void
libbalsa_certs_destroy(void)
{
	g_mutex_lock(&certificate_lock);
    g_list_free_full(accepted_certs, g_object_unref);
    accepted_certs = NULL;
    g_mutex_unlock(&certificate_lock);
}


#define CERT_ACCEPT_NO			0
#define CERT_ACCEPT_SESSION		1
#define CERT_ACCEPT_PERMANENT	2


gboolean
libbalsa_is_cert_known(GTlsCertificate      *cert,
					   GTlsCertificateFlags  errors)
{
	gchar *cert_file;
	GList *cert_db;
	gboolean cert_ok;
	GList *lst;

	/* check the list of accepted certificates for this session */
	g_mutex_lock(&certificate_lock);
	for (lst = accepted_certs; lst; lst = lst->next) {
		if (g_tls_certificate_is_same(cert, G_TLS_CERTIFICATE(lst->data))) {
			g_mutex_unlock(&certificate_lock);
			return TRUE;
		}
	}

	/* check the database of accepted certificates */
	cert_file = g_build_filename(g_get_home_dir(), ".balsa", "certificates", NULL);
	cert_db = g_tls_certificate_list_new_from_file(cert_file, NULL);
	g_mutex_unlock(&certificate_lock);

	cert_ok = FALSE;
	for (lst = cert_db; !cert_ok && (lst != NULL); lst = g_list_next(lst)) {
		if (g_tls_certificate_is_same(cert, G_TLS_CERTIFICATE(lst->data))) {
			cert_ok = TRUE;
		}
	}
	g_list_free_full(cert_db, g_object_unref);

	/* ask the user if the certificate is not in the data base */
	if (!cert_ok) {
		int accepted;

		accepted = libbalsa_ask_for_cert_acceptance(cert, errors);
		g_mutex_lock(&certificate_lock);
		if (accepted == CERT_ACCEPT_SESSION) {
			accepted_certs = g_list_prepend(accepted_certs, g_object_ref(cert));
			cert_ok = TRUE;
		} else if (accepted == CERT_ACCEPT_PERMANENT) {
			gchar *pem_data;
			FILE *fd;

			g_object_get(G_OBJECT(cert), "certificate-pem", &pem_data, NULL);
			fd = fopen(cert_file, "a");
			if (fd != NULL) {
				fputs(pem_data, fd);
				fclose(fd);
			}
			g_free(pem_data);
			cert_ok = TRUE;
		} else {
			/* nothing to do */
		}
		g_mutex_unlock(&certificate_lock);
	}
	g_free(cert_file);

	return cert_ok;
}

/* libbalsa_ask_for_cert_acceptance():
   returns:
   OP_EXIT on reject.
   OP_SAVE - on accept and save.
   OP_MAX - on accept once.
   TODO: check treading issues.

*/
struct AskCertData {
    GTlsCertificate *certificate;
    const char *explanation;
};

#if HAVE_GCR

static int
ask_cert_real(void *data)
{
    struct AskCertData *acd = (struct AskCertData*)data;
    GtkWidget *dialog;
    GtkWidget *cert_widget;
    GString *str;
    unsigned i;
    GByteArray *cert_der;
    GcrCertificate *gcr_cert;
    GtkWidget *label;

    dialog = gtk_dialog_new_with_buttons(_("SSL/TLS certificate"),
                                         NULL, /* FIXME: NULL parent */
                                         GTK_DIALOG_MODAL |
                                         libbalsa_dialog_flags(),
                                         _("_Accept Once"), 0,
                                         _("Accept & _Save"), 1,
                                         _("_Reject"), GTK_RESPONSE_CANCEL,
                                         NULL);
    gtk_window_set_role(GTK_WINDOW(dialog), "tls_cert_dialog");
    g_object_get(G_OBJECT(acd->certificate), "certificate", &cert_der, NULL);
    gcr_cert = gcr_simple_certificate_new(cert_der->data, cert_der->len);
    g_byte_array_unref(cert_der);
    cert_widget = GTK_WIDGET(gcr_certificate_widget_new(gcr_cert));
    g_object_unref(G_OBJECT(gcr_cert));

    str = g_string_new("");
    g_string_printf(str, _("<big><b>Authenticity of this certificate "
                           "could not be verified.</b></big>\n"
                           "Reason: %s"),
                    acd->explanation);
    label = gtk_label_new(str->str);
    g_string_free(str, TRUE);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       label, FALSE, FALSE, 1);
    gtk_widget_show(label);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       cert_widget, TRUE, TRUE, 1);
    gtk_widget_show(cert_widget);

    switch(gtk_dialog_run(GTK_DIALOG(dialog))) {
    case 0:
    	i = CERT_ACCEPT_SESSION;
    	break;
    case 1:
    	i = CERT_ACCEPT_PERMANENT;
    	break;
    case GTK_RESPONSE_CANCEL:
    default:
    	i = CERT_ACCEPT_NO;
    	break;
    }
    gtk_widget_destroy(dialog);
    return i;
}

#else

static gnutls_x509_crt_t G_GNUC_WARN_UNUSED_RESULT
get_gnutls_cert(GTlsCertificate *cert)
{
	gnutls_x509_crt_t res_crt;
    int gnutls_res;

    gnutls_res = gnutls_x509_crt_init(&res_crt);
    if (gnutls_res == GNUTLS_E_SUCCESS) {
    	GByteArray *cert_der;

    	g_object_get(G_OBJECT(cert), "certificate", &cert_der, NULL);
    	if (cert_der != NULL) {
    		gnutls_datum_t data;

    		data.data = cert_der->data;
    		data.size = cert_der->len;
    		gnutls_res = gnutls_x509_crt_import(res_crt, &data, GNUTLS_X509_FMT_DER);
    		if (gnutls_res != GNUTLS_E_SUCCESS) {
    			gnutls_x509_crt_deinit(res_crt);
    			res_crt = NULL;
    		}
    		g_byte_array_unref(cert_der);
    	}
    } else {
    	res_crt = NULL;
    }
    return res_crt;
}

static gchar * G_GNUC_WARN_UNUSED_RESULT
gnutls_get_dn(gnutls_x509_crt_t cert, int (*load_fn)(gnutls_x509_crt_t cert, char *buf, size_t *buf_size))
{
    size_t buf_size;
    gchar *str_buf;

    buf_size = 0U;
    (void) load_fn(cert, NULL, &buf_size);
    str_buf = g_malloc0(buf_size + 1U);
    if (load_fn(cert, str_buf, &buf_size) != GNUTLS_E_SUCCESS) {
    	g_free(str_buf);
    	str_buf = NULL;
    } else {
    	libbalsa_utf8_sanitize(&str_buf, TRUE, NULL);
    }
    return str_buf;
}

static gchar * G_GNUC_WARN_UNUSED_RESULT
x509_fingerprint(gnutls_x509_crt_t cert)
{
    size_t buf_size;
    guint8 sha1_buf[20];
    gchar *str_buf;
    gint n;

    buf_size = 20U;
    g_message("%d", gnutls_x509_crt_get_fingerprint(cert, GNUTLS_DIG_SHA1, sha1_buf, &buf_size));
    str_buf = g_malloc0(60U);
    for (n = 0; n < 20; n++) {
    	sprintf(&str_buf[3 * n], "%02x:", sha1_buf[n]);
    }
    str_buf[59] = '\0';
    return str_buf;
}

static int
ask_cert_real(void *data)
{
    struct AskCertData *acd = (struct AskCertData*)data;
    gnutls_x509_crt_t cert;
    gchar *name, *c, *valid_from, *valid_until;
    GtkWidget* dialog, *label;
    unsigned i;
    GString* str;

    cert = get_gnutls_cert(acd->certificate);
    if (cert == NULL) {
    	g_warning("%s: unable to create gnutls cert", __func__);
    	return CERT_ACCEPT_NO;
    }

    str = g_string_new("");
    g_string_printf(str, _("Authenticity of this certificate "
                           "could not be verified.\n"
                           "<b>Reason:</b> %s\n"
                           "<b>This certificate belongs to:</b>\n"),
                    acd->explanation);

    name = gnutls_get_dn(cert, gnutls_x509_crt_get_dn);
    g_string_append(str, name);
    g_free(name);

    g_string_append(str, _("\n<b>This certificate was issued by:</b>\n"));
    name = gnutls_get_dn(cert, gnutls_x509_crt_get_issuer_dn);
    g_string_append_printf(str, "%s\n", name);
    g_free(name);

    name = x509_fingerprint(cert);
    valid_from  = libbalsa_date_to_utf8(gnutls_x509_crt_get_activation_time(cert), "%x %X");
    valid_until = libbalsa_date_to_utf8(gnutls_x509_crt_get_expiration_time(cert), "%x %X");
    g_string_append_printf(str, _("<b>This certificate is valid</b>\n"
    							  "from %s\n"
    							  "to %s\n"
                         		  "<b>Fingerprint:</b> %s"),
                        	valid_from, valid_until, name);
    g_free(name);
    g_free(valid_from);
    g_free(valid_until);
    gnutls_x509_crt_deinit(cert);

    /* This string uses markup, so we must replace "&" with "&amp;" */
    c = str->str;
    while ((c = strchr(c, '&'))) {
        gssize pos;

        pos = (c - str->str) + 1;
        g_string_insert(str, pos, "amp;");
        c = str->str + pos;
    }

    dialog = gtk_dialog_new_with_buttons(_("SSL/TLS certificate"),
                                         NULL, /* FIXME: NULL parent */
                                         GTK_DIALOG_MODAL |
                                         libbalsa_dialog_flags(),
                                         _("_Accept Once"), 0,
                                         _("Accept & _Save"), 1,
                                         _("_Reject"), GTK_RESPONSE_CANCEL, 
                                         NULL);
    gtk_window_set_role(GTK_WINDOW(dialog), "tls_cert_dialog");
    label = gtk_label_new(str->str);
    g_string_free(str, TRUE);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX
                       (gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       label, TRUE, TRUE, 1);
    gtk_widget_show(label);

    switch(gtk_dialog_run(GTK_DIALOG(dialog))) {
    case 0: i = CERT_ACCEPT_SESSION; break;
    case 1: i = CERT_ACCEPT_PERMANENT; break;
    case GTK_RESPONSE_CANCEL:
    default: i=CERT_ACCEPT_NO; break;
    }
    gtk_widget_destroy(dialog);
    /* Process some events to let the window disappear:
     * not really necessary but helps with debugging. */
   while(gtk_events_pending()) 
        gtk_main_iteration_do(FALSE);
    printf("%s returns %d\n", __FUNCTION__, i);
    return i;
}

#endif	/* HAVE_GCR */

static int
libbalsa_ask_for_cert_acceptance(GTlsCertificate      *cert,
								 GTlsCertificateFlags  errors)
{
    struct AskCertData acd;
    acd.certificate = cert;
    if ((errors & G_TLS_CERTIFICATE_UNKNOWN_CA) == G_TLS_CERTIFICATE_UNKNOWN_CA) {
    	acd.explanation = _("the signing certificate authority is not known");
    } else if ((errors & G_TLS_CERTIFICATE_BAD_IDENTITY) == G_TLS_CERTIFICATE_BAD_IDENTITY) {
    	acd.explanation = _("the certificate does not match the expected identity of the site that it was retrieved from");
    } else if ((errors & G_TLS_CERTIFICATE_NOT_ACTIVATED) == G_TLS_CERTIFICATE_NOT_ACTIVATED) {
    	acd.explanation = _("the certificate’s activation time is still in the future");
    } else if ((errors & G_TLS_CERTIFICATE_EXPIRED) == G_TLS_CERTIFICATE_EXPIRED) {
    	acd.explanation = _("the certificate has expired");
    } else if ((errors & G_TLS_CERTIFICATE_REVOKED) == G_TLS_CERTIFICATE_REVOKED) {
    	acd.explanation = _("the certificate has been revoked ");
    } else if ((errors & G_TLS_CERTIFICATE_INSECURE) == G_TLS_CERTIFICATE_INSECURE) {
    	acd.explanation = _("the certificate’s algorithm is considered insecure");
    } else {
    	acd.explanation = _("an error occurred validating the certificate");
    }
    return libbalsa_ask(ask_cert_real, &acd);
}


GThread *
libbalsa_get_main_thread(void)
{
    return main_thread_id;
}

gboolean
libbalsa_am_i_subthread(void)
{
    return g_thread_self() != main_thread_id;
}


#include "libbalsa_private.h"	/* for prototypes */
static GMutex mailbox_mutex;
static GCond  mailbox_cond;

/* Lock/unlock a mailbox; no argument checking--we'll assume the caller
 * took care of that. 
 */
#define LIBBALSA_DEBUG_THREADS FALSE
void
libbalsa_lock_mailbox(LibBalsaMailbox * mailbox)
{
	GThread *thread_id = g_thread_self();

    g_mutex_lock(&mailbox_mutex);

    if (mailbox->thread_id && mailbox->thread_id != thread_id)
        while (mailbox->lock)
            g_cond_wait(&mailbox_cond, &mailbox_mutex);

    /* We'll assume that no-one would destroy a mailbox while we've been
     * trying to lock it. If they have, we have larger problems than
     * this reference! */
    mailbox->lock++;
    mailbox->thread_id = thread_id;

    g_mutex_unlock(&mailbox_mutex);
}

void
libbalsa_unlock_mailbox(LibBalsaMailbox * mailbox)
{
	GThread *self;

    self = g_thread_self();

    g_mutex_lock(&mailbox_mutex);

    if (mailbox->lock == 0 || self != mailbox->thread_id) {
	g_warning("Not holding mailbox lock!!!");
        g_mutex_unlock(&mailbox_mutex);
	return;
    }

    if(--mailbox->lock == 0) {
        mailbox->thread_id = 0;
        g_cond_broadcast(&mailbox_cond);
    }

    g_mutex_unlock(&mailbox_mutex);
}


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
	    lm_rpaths[n] = g_strdup(BALSA_DATA_PREFIX "/gtksourceview-3.0");
	    gtk_source_language_manager_set_search_path(lm, lm_rpaths);
	    g_strfreev(lm_rpaths);

	    /* try to load the language */
	    if ((src_lang =
		 gtk_source_language_manager_get_language(lm, "balsa"))) {
		GtkSourceStyleSchemeManager *smgr =
		    gtk_source_style_scheme_manager_get_default();
		gchar * sm_paths[] = {
		    BALSA_DATA_PREFIX "/gtksourceview-3.0",
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

#if GTK_CHECK_VERSION(3, 12, 0)
GtkDialogFlags
libbalsa_dialog_flags(void)
{
	static GtkDialogFlags dialog_flags = GTK_DIALOG_USE_HEADER_BAR;
	static gint check_done = 0;

	if (g_atomic_int_get(&check_done) == 0) {
		const gchar *dialog_env;

		dialog_env = g_getenv("BALSA_DIALOG_HEADERBAR");
		if ((dialog_env != NULL) && (atoi(dialog_env) == 0)) {
			dialog_flags = (GtkDialogFlags) 0;
		}
		g_atomic_int_set(&check_done, 1);
	}
	return dialog_flags;
}
#endif
