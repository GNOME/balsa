/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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

#ifdef HAVE_GPGME

#include <gnome.h>
#include <string.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "rfc3156.h"

#include "gmime-gpgme-context.h"

#ifdef HAVE_XML2
#  include <libxml/xmlmemory.h>
#  include <libxml/parser.h>
#endif
#ifdef BALSA_USE_THREADS
#  include <pthread.h>
#  include "misc.h"
#endif

typedef struct _MailDataMBox MailDataMBox;
typedef struct _MailDataBuffer MailDataBuffer;
typedef struct _PassphraseCB PassphraseCB;

struct _MailDataMBox {
    FILE *mailboxFile;
    size_t bytes_left;
    gboolean last_was_cr;
};

struct _MailDataBuffer {
    gchar *ptr;
    gboolean last_was_cr;
};

struct _PassphraseCB {
    GpgmeCtx ctx;
    gchar *title;
    GtkWindow *parent;
};
    
#define RFC2440_SEPARATOR _("\n--\nThis is an OpenPGP signed message part:\n")

#ifndef USE_SSL
#  undef ENABLE_PCACHE
#else
/* 
 * local stuff for remembering the last passphrase (needs OpenSSL)
 */
#define ENABLE_PCACHE
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <signal.h>

/* FIXME: this is an evil hack to get a public function from libmutt which
 * is never defined in a header file... */
extern int ssl_init(void);

typedef struct _bf_key_T bf_key_T;
typedef struct _bf_crypt_T bf_crypt_T;
typedef struct _pcache_elem_T pcache_elem_T;
typedef struct _pcache_T pcache_T;

struct _bf_key_T {
    unsigned char key[16];   /* blowfish key */
    unsigned char iv[8];
};

struct _bf_crypt_T {
    unsigned char *buf;      /* encrypted buffer */
    gint len;                /* length of buf */
};

struct _pcache_elem_T {
    unsigned char name[MD5_DIGEST_LENGTH];  /* md5sum of name */
    bf_crypt_T *passphrase;  /* encrypted passphrase */
    time_t expires;          /* expiry time */
};

struct _pcache_T {
    gboolean enable;         /* using the cache is allowed */
    gint max_mins;           /* max minutes allowed to cache data */
    bf_key_T bf_key;         /* the (random) blowfish key */
    GList *cache;            /* list of pcache_elem_T elements */
};

static pcache_T *pcache = NULL;
static void (*segvhandler)(int);

#endif


/* local prototypes */
static const gchar *libbalsa_gpgme_validity_to_gchar_short(GpgmeValidity validity);
static const gchar *get_passphrase_cb(void *opaque, const char *desc,
				      void **r_hd);
static GMimeMultipartSigned * gpgme_signature(GMimeObject *mps,
					      const gchar *sign_for,
					      GtkWindow *parent);
static int read_cb_MailDataBuffer(void *hook, char *buffer, size_t count,
				  size_t *nread);
static void set_decrypt_file(LibBalsaMessageBody *body, const gchar *fname);
static gchar * get_key_fingerprint(GpgmeCtx ctx, const gchar *name,
				   int secret_only, GtkWindow *parent);
static gboolean gpgme_add_signer(GpgmeCtx ctx, const gchar *signer);
static gboolean gpgme_build_recipients(GpgmeRecipients *rcpt, GpgmeCtx ctx,
				       GList *rcpt_list);
static void get_sig_info_from_cipher_context(LibBalsaSignatureInfo* info,
				       GMimeGpgMEContext* validity);
static void get_sig_info_from_ctx(LibBalsaSignatureInfo* info, GpgmeCtx ctx);

static GType libbalsa_g_mime_session_get_type (void);
typedef struct _LibBalsaGMimeSession LibBalsaGMimeSession;
struct _LibBalsaGMimeSession {
	GMimeSession parent_object;
	PassphraseCB *data;
	gchar *passwd;
};


/* ==== public functions =================================================== */

LibBalsaSignatureInfo *
libbalsa_signature_info_destroy(LibBalsaSignatureInfo* info)
{
    if (!info)
	return NULL;

    if (info->sign_name)
	g_free(info->sign_name);
    if (info->sign_email)
	g_free(info->sign_email);
    if (info->fingerprint)
	g_free(info->fingerprint);
    g_free(info);
    return NULL;
}


gchar *
libbalsa_signature_info_to_gchar(LibBalsaSignatureInfo * info, 
				 const gchar * date_string)
{
    GString *msg;
    gchar *retval;

    g_return_val_if_fail(info != NULL, NULL);
    g_return_val_if_fail(date_string != NULL, NULL);

    msg = g_string_new(libbalsa_gpgme_sig_stat_to_gchar(info->status));
    if (info->sign_name || info->sign_email || info->fingerprint) {
	if (info->sign_name) {
	    g_string_append_printf(msg, _("\nSigned by: %s"), info->sign_name);
	    if (info->sign_email)
		g_string_append_printf(msg, " <%s>", info->sign_email);
	} else if (info->sign_email)
	    g_string_append_printf(msg, _("Mail address: %s"),
				   info->sign_email);
	g_string_append_printf(msg, _("\nValidity: %s"),
			       libbalsa_gpgme_validity_to_gchar(info->validity));
	g_string_append_printf(msg, _("\nOwner trust: %s"),
			       libbalsa_gpgme_validity_to_gchar_short(info->trust));
	if (info->fingerprint)
	    g_string_append_printf(msg, _("\nKey fingerprint: %s"),
				   info->fingerprint);
	if (info->key_created) {
	    gchar buf[128];
	    strftime(buf, 127, date_string, localtime(&info->key_created));
	    g_string_append_printf(msg, _("\nKey created on: %s"), buf);
	}
	if (info->sign_time) {
	    gchar buf[128];
	    strftime(buf, 127, date_string, localtime(&info->sign_time));
	    g_string_append_printf(msg, _("\nSigned on: %s"), buf);
	}
    }

    retval = msg->str;
    g_string_free(msg, FALSE);
    return retval;
}


/*
 * Return value: >0 body has a correct multipart/signed structure
 *               <0 body is multipart/signed, but not conforming to RFC3156
 *               =0 unsigned
 */

static gint
body_is_type(LibBalsaMessageBody * body, const gchar * type,
	     const gchar * sub_type)
{
    gint retval;

    if (body->mime_part) {
	const GMimeContentType *content_type =
	    g_mime_object_get_content_type(body->mime_part);
	retval = g_mime_content_type_is_type(content_type, type, sub_type);
    } else {
	GMimeContentType *content_type =
	    g_mime_content_type_new_from_string(body->mime_type);
	retval = g_mime_content_type_is_type(content_type, type, sub_type);
	g_mime_content_type_destroy(content_type);
    }

    return retval;
}

gint
libbalsa_is_pgp_signed(LibBalsaMessageBody *body)
{
    gchar *micalg;
    gchar *protocol;
    gint result;

    g_return_val_if_fail(body != NULL, 0);
    g_return_val_if_fail(body->mime_part != NULL ||
	                 body->mime_type != NULL, 0);
    if (!body->parts || !body->parts->next ||
	!(body->parts->next->mime_part || body->parts->next->mime_type))
	return 0;

    micalg = libbalsa_message_body_get_parameter(body, "micalg");
    protocol = libbalsa_message_body_get_parameter(body, "protocol");

    result = body_is_type(body, "multipart", "signed");

    if (result) {
	if (!(micalg && !g_ascii_strncasecmp("pgp-", micalg, 4) &&
	      protocol && !g_ascii_strcasecmp("application/pgp-signature", protocol) &&
	      body_is_type(body->parts->next,
			   "application", "pgp-signature")))
	    result = -1;  /* bad multipart/signed stuff... */
    }

    g_free(micalg);
    g_free(protocol);

    return result;
}


/*
 * Return value: >0 body has a correct multipart/encrypted structure
 *               <0 body is multipart/encrypted, but not conforming to RFC3156
 *               =0 unsigned
 */
gint
libbalsa_is_pgp_encrypted(LibBalsaMessageBody *body)
{
    gchar *protocol;
    gint result;

    g_return_val_if_fail(body != NULL, 0);
    g_return_val_if_fail(body->mime_part != NULL ||
			 body->mime_type != NULL, 0);
    if (body->parts == NULL)
	return 0;
    g_return_val_if_fail(body->parts->mime_part != NULL ||
			 body->parts->mime_type != NULL, 0);

    protocol = libbalsa_message_body_get_parameter(body, "protocol");

    /* FIXME: verify that body contains "Version: 1" */
    result = body_is_type(body, "multipart", "encrypted");

    if (result) {
	LibBalsaMessageBody *cparts = body->parts;
	if (!(protocol &&
	      !g_ascii_strcasecmp("application/pgp-encrypted", protocol) &&
	      body_is_type(cparts, "application", "pgp-encrypted") &&
	      cparts->next &&
	      (cparts->next->mime_part || cparts->next->mime_type) &&
	      body_is_type(cparts->next, "application", "octet-stream")))
	    result = -1;  /* bad multipart/encrypted... */
    }

    g_free(protocol);

    return result;
}


gboolean
libbalsa_body_check_signature(LibBalsaMessageBody* body)
{
    LibBalsaSignatureInfo *sig_status;
    GMimeSession *session;
    GMimeCipherContext *ctx;
    GMimeCipherValidity *valid;
    GError *err=NULL;

    g_return_val_if_fail(body, FALSE);
    g_return_val_if_fail(body->parts, FALSE);
    g_return_val_if_fail(body->parts->next, FALSE);
    g_return_val_if_fail(body->message, FALSE);

    /* the 2nd part for RFC 3156 MUST be application/pgp-signature */
    if (!GMIME_IS_MULTIPART_SIGNED(body->mime_part))
	return FALSE;

    libbalsa_signature_info_destroy(body->parts->next->sig_info);
    body->parts->next->sig_info = sig_status = g_new0(LibBalsaSignatureInfo, 1);
    sig_status->status = GPGME_SIG_STAT_ERROR;


    /* try to create GMimeGpgMEContext */
    session = g_object_new(libbalsa_g_mime_session_get_type(), NULL, NULL);
    ctx = g_mime_gpgme_context_new(session);
    if (ctx == NULL) {
	g_warning("could not create GMimeGPG context");
	return FALSE;
    }

    /* verify the signature */
    valid =
	g_mime_multipart_signed_verify(GMIME_MULTIPART_SIGNED(body->mime_part),
				       ctx, &err);
    if (valid == NULL) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("GMimeGPG signature verification failed: %s"),
			     err->message);
	sig_status->status = GPGME_SIG_STAT_ERROR;
    } else
	get_sig_info_from_cipher_context(sig_status, GMIME_GPGME_CONTEXT(ctx));
    g_mime_cipher_validity_free (valid);

    g_object_unref (ctx);
    g_object_unref (session);

    return TRUE;
}


/*
 * Signs for rfc822_for. If successful, returns in content the following chain
 *
 *      multipart/signed
 *         |
 *         +---- original sign_body
 *         +---- application/pgp-signature
 *
 */
gboolean
libbalsa_sign_mime_object(GMimeObject **content, const gchar *rfc822_for,
			  GtkWindow *parent)
{
    GMimeMultipartSigned *mps;

    /* paranoia checks */
    g_return_val_if_fail(rfc822_for != NULL, FALSE);
    g_return_val_if_fail(content != NULL, FALSE);

    /* call gpgme to create the signature */
    mps = gpgme_signature(*content, rfc822_for, parent);
    if (!mps) {
	return FALSE;
    }

    g_mime_object_unref(GMIME_OBJECT(*content));
    *content = GMIME_OBJECT(mps);

    return TRUE;
}


gboolean
libbalsa_sign_encrypt_mime_object(GMimeObject **content,
				  const gchar *rfc822_signer,
				  GList *rfc822_for, GtkWindow *parent)
{
    GMimeObject *signed_object;

    /* paranoia checks */
    g_return_val_if_fail(rfc822_signer != NULL, FALSE);
    g_return_val_if_fail(rfc822_for != NULL, FALSE);
    g_return_val_if_fail(content != NULL, FALSE);

    /* we want to be able to restore */
    signed_object = *content;
    g_mime_object_ref(GMIME_OBJECT(signed_object));

    if (!libbalsa_sign_mime_object(&signed_object, rfc822_signer, parent))
	return FALSE;

    if (!libbalsa_encrypt_mime_object(&signed_object, rfc822_for)) {
	g_mime_object_unref(GMIME_OBJECT(signed_object));
	return FALSE;
    }
    g_mime_object_unref(GMIME_OBJECT(*content));
    *content = signed_object;

    return TRUE;
}

/*
 * body points to an application/pgp-encrypted body. If decryption is
 * successful, it is freed, and the routine returns a pointer to the chain of
 * decrypted bodies. Otherwise, the original body is returned.
 */
LibBalsaMessageBody *
libbalsa_body_decrypt(LibBalsaMessageBody *body, GtkWindow *parent)
{
    GMimeSession *session;
    GMimeGpgMEContext *ctx;
    PassphraseCB cb_data;
    GMimeMultipartEncrypted *mpe;
    GMimeObject *mime_obj;
    GError *err=NULL;
    LibBalsaMessage *message;

    /* paranoia checks */
    g_return_val_if_fail(body != NULL, NULL);
    g_return_val_if_fail(body->message != NULL, NULL);

    /* try to create GMimeGpgMEContext */
    session = g_object_new(libbalsa_g_mime_session_get_type(), NULL, NULL);
    ctx = GMIME_GPGME_CONTEXT(g_mime_gpgme_context_new(session));
    if (ctx == NULL) {
	g_warning("could not create GMimeGPG context");
	g_object_unref (session);
	return body;
    }

    ((LibBalsaGMimeSession*)session)->data = &cb_data;
//    cb_data.ctx = ctx;
    cb_data.parent = parent;
    cb_data.title = _("Enter passphrase to decrypt message");
    mpe = GMIME_MULTIPART_ENCRYPTED(body->mime_part);
    mime_obj = g_mime_multipart_encrypted_decrypt(mpe,
						  GMIME_CIPHER_CONTEXT(ctx),
						  &err);
    if (mime_obj == NULL) {
	g_object_unref (ctx);
	g_object_unref (session);
	return body;
    }
    g_object_unref (ctx);
    g_object_unref (session);
    message = body->message;
    libbalsa_message_body_free(body);
    body = libbalsa_message_body_new(message);
    libbalsa_message_body_set_mime_body(body, mime_obj);
    g_object_unref(G_OBJECT(mime_obj));	
    return body;
}


/*
 * Encrypts for rfc822_for. If successful, returns in content the following
 * chain
 *
 *      multipart/encrypted
 *         |
 *         +---- application/pgp-encrypted
 *         +---- application/octet-stream, containing the original encrypt_body
 *               (encrypt_body->next == NULL) or the encrypt_body chain put
 *               into a multipart/mixed (encrypt_body->next != NULL)
 */
gboolean
libbalsa_encrypt_mime_object(GMimeObject **content, GList *rfc822_for)
{
    GMimeSession *session;
    GMimeGpgMEContext *ctx;
    GPtrArray *recipients;
    GMimeMultipartEncrypted *mpe;
    GError *err=NULL;

    /* paranoia checks */
    g_return_val_if_fail(rfc822_for != NULL, FALSE);
    g_return_val_if_fail(content != NULL, FALSE);

    /* try to create GMimeGpgMEContext */
    session = g_object_new(libbalsa_g_mime_session_get_type(), NULL, NULL);
    ctx = GMIME_GPGME_CONTEXT(g_mime_gpgme_context_new(session));
    g_object_unref (session);
    if (ctx == NULL) {
	g_warning("could not create GMimeGPG context");
	return FALSE;
    }

    recipients = g_ptr_array_new();
    while (rfc822_for) {
        gchar *name = (gchar *)rfc822_for->data;
	gchar *fingerprint;
	if (!(fingerprint = get_key_fingerprint(ctx->gpgme_ctx,
						(gchar*)name, 0, NULL))) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("could not get a key for %s"), name);
	    g_object_unref (ctx);
	    g_ptr_array_free(recipients, TRUE);
	    return FALSE;
	}
	g_ptr_array_add(recipients, fingerprint);
        rfc822_for = g_list_next(rfc822_for);
    }
    mpe = g_mime_multipart_encrypted_new();
    if (g_mime_multipart_encrypted_encrypt(mpe, *content,
					   GMIME_CIPHER_CONTEXT(ctx),
					   recipients,
					   &err) != 0) {
	g_object_unref (ctx);
	g_object_unref (mpe);
	return FALSE;
    }

    g_mime_object_unref(GMIME_OBJECT(*content));
    *content = GMIME_OBJECT(mpe);
    g_ptr_array_free(recipients, TRUE);
    g_object_unref (ctx);

    return TRUE;
}


const gchar *
libbalsa_gpgme_sig_stat_to_gchar(GpgmeSigStat stat)
{
    switch (stat)
	{
	case GPGME_SIG_STAT_GOOD:
	    return _("The signature is valid.");
	case GPGME_SIG_STAT_GOOD_EXP:
	    return _("The signature is valid but expired.");
	case GPGME_SIG_STAT_GOOD_EXPKEY:
	    return _("The signature is valid but the key used to verify the signature has expired.");
	case GPGME_SIG_STAT_BAD:
	    return _("The signature is invalid.");
	case GPGME_SIG_STAT_NOKEY:
	    return _("The signature could not be verified due to a missing key.");
	case GPGME_SIG_STAT_NOSIG:
	    return _("This part is not a real PGP signature.");
	case GPGME_SIG_STAT_ERROR:
	    return _("An error prevented the signature verification.");
	case GPGME_SIG_STAT_DIFF:
	    return _("This part contains at least two signatures with different status.");
	case GPGME_SIG_STAT_NONE:
	default:
	    return _("bad status");
	}
}

const gchar *
libbalsa_gpgme_validity_to_gchar(GpgmeValidity validity)
{
    switch (validity)
	{
	case GPGME_VALIDITY_UNKNOWN:
	    return _("The user ID is of unknown validity.");
	case GPGME_VALIDITY_UNDEFINED:
	    return _("The validity of the user ID is undefined.");
	case GPGME_VALIDITY_NEVER:
	    return _("The user ID is never valid.");
	case GPGME_VALIDITY_MARGINAL:
	    return _("The user ID is marginally valid.");
	case GPGME_VALIDITY_FULL:
	    return _("The user ID is fully valid.");
	case GPGME_VALIDITY_ULTIMATE:
	    return _("The user ID is ultimately valid.");
	default:
	    return _("bad validity");
	}
}

/* routines dealing with RFC2440 */

/*
 * Checks if buffer appears to be signed or encrypted according to RFC2440
 */
LibBalsaMessageBodyRFC2440Mode 
libbalsa_rfc2440_check_buffer(const gchar *buffer)
{
    g_return_val_if_fail(buffer != NULL, LIBBALSA_BODY_RFC2440_NONE);

    if (!strncmp(buffer, "-----BEGIN PGP MESSAGE-----", 27) &&
	strstr(buffer, "-----END PGP MESSAGE-----"))
	return LIBBALSA_BODY_RFC2440_ENCRYPTED;
    else if (!strncmp(buffer, "-----BEGIN PGP SIGNED MESSAGE-----", 34) &&
	     strstr(buffer, "-----BEGIN PGP SIGNATURE-----") &&
	     strstr(buffer, "-----END PGP SIGNATURE-----"))
	return LIBBALSA_BODY_RFC2440_SIGNED;
    else
	return LIBBALSA_BODY_RFC2440_NONE;    
}


/*
 * sign the data in buffer for sender *rfc822_for and return the signed and
 * properly escaped new buffer on success or NULL on error
 */
gchar *
libbalsa_rfc2440_sign_buffer(const gchar *buffer, const gchar *sign_for,
			     GtkWindow *parent)
{
    GpgmeCtx ctx;
    GpgmeError err;
    GpgmeData in, out;
    MailDataBuffer inbuf;
    PassphraseCB cb_data;
    gchar *signed_buffer, *result, *inp, *outp;
    size_t datasize;

    /* paranoia checks */
    g_return_val_if_fail(sign_for != NULL, NULL);
    g_return_val_if_fail(buffer != NULL, NULL);

    /* try to create a gpgme context */
    if ((err = gpgme_new(&ctx)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not create gpgme context: %s"),
			     gpgme_strerror(err));
	return NULL;
    }
    
    /* find the secret key for the "sign_for" address */
    if (!gpgme_add_signer(ctx, sign_for)) {
	gpgme_release(ctx);
	return NULL;
    }
  
    /* let gpgme create the signature */
    gpgme_set_armor(ctx, 1);
    cb_data.ctx = ctx;
    cb_data.title =
	_("Enter passsphrase to OpenPGP sign the first message part");
    cb_data.parent = parent;
    gpgme_set_passphrase_cb(ctx, get_passphrase_cb, &cb_data);
    inbuf.ptr = (gchar *)buffer;
    inbuf.last_was_cr = FALSE;
    if ((err = gpgme_data_new_with_read_cb(&in, read_cb_MailDataBuffer,
    					   &inbuf)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not get data from buffer: %s"), 
			     gpgme_strerror(err));
	gpgme_release(ctx);
	return NULL;
    }
    if ((err = gpgme_data_new(&out)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not create new data object: %s"),
			     gpgme_strerror(err));
	gpgme_data_release(in);
	gpgme_release(ctx);
	return NULL;
    }
    if ((err = gpgme_op_sign(ctx, in, out, GPGME_SIG_MODE_CLEAR)) != 
	GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme signing failed: %s"),
			     gpgme_strerror(err));
	gpgme_data_release(out);
	gpgme_data_release(in);
	gpgme_release(ctx);
	return NULL;
    }

    /* get the result */
    gpgme_data_release(in);
    if (!(signed_buffer = gpgme_data_release_and_get_mem(out, &datasize))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not get gpgme data: %s"),
			     gpgme_strerror(err));
	gpgme_release(ctx);
	return NULL;
    }
    gpgme_release(ctx);

    /* return a newly allocated buffer, but strip all \r's as they confuse
       libmutt... The extra space should not do any harm */
    result = outp = g_malloc(datasize + 1);
    inp = signed_buffer;
    while (datasize--)
	if (*inp != '\r')
	    *outp++ = *inp++;
	else
	    inp++;
    *outp = '\0';
    g_free(signed_buffer);
    return result;
}


/*
 * Check the signature of *buffer, convert it back from rfc2440 format and
 * return the siganture's status. If append_info is TRUE, append information
 * about the signature to buffer. If sig_info id not NULL, a new structure
 * is allocated and filled with data. If some error occurs, the buffer is not
 * touched and the routine returns GPGME_SIG_STAT_ERROR. If charset is not
 * NULL and not `utf-8', the routine converts *buffer from utf-8 to charset
 * before checking and back afterwards.
 */
GpgmeSigStat
libbalsa_rfc2440_check_signature(gchar **buffer, const gchar *charset,
				 gboolean append_info,
				 LibBalsaSignatureInfo **sig_info,
				 const gchar *date_string)
{
    GpgmeCtx ctx;
    GpgmeError err;
    GpgmeData in, out;
    GpgmeSigStat status;
    gchar *checkbuf, *plain_buffer, *info, *result;
    size_t datasize;

    /* paranoia checks */
    g_return_val_if_fail(buffer != NULL, GPGME_SIG_STAT_ERROR);
    g_return_val_if_fail(*buffer != NULL, GPGME_SIG_STAT_ERROR);

    /* try to create a gpgme context */
    if ((err = gpgme_new(&ctx)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not create gpgme context: %s"),
			     gpgme_strerror(err));
	return GPGME_SIG_STAT_ERROR;
    }
    
    /* if necessary, convert the buffer 2b checked to charset */
    if (charset && g_ascii_strcasecmp(charset, "utf-8"))
	checkbuf = g_convert(*buffer, strlen(*buffer),
			     charset, "utf-8",
			     NULL, NULL, NULL);
    else
	checkbuf = g_strdup(*buffer);

    /* create the body data stream */
    if ((err = gpgme_data_new_from_mem(&in, checkbuf, strlen(*buffer), 0)) !=
	GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not get data from buffer: %s"),
			     gpgme_strerror(err));
	g_free(checkbuf);
	gpgme_release(ctx);
	return GPGME_SIG_STAT_ERROR;
    }

    /* verify the signature */
    if ((err = gpgme_data_new(&out)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not create new data object: %s"),
			     gpgme_strerror(err));
	g_free(checkbuf);
	gpgme_data_release(in);
	gpgme_release(ctx);
	return GPGME_SIG_STAT_ERROR;
    }
    if ((err = gpgme_op_verify(ctx, in, out, &status))
	!= GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme signature verification failed: %s"),
			     gpgme_strerror(err));
	gpgme_data_release(out);
	gpgme_data_release(in);
	g_free(checkbuf);
	gpgme_release(ctx);
	return GPGME_SIG_STAT_ERROR;
    }
    gpgme_data_release(in);
    g_free(checkbuf);

    /* get some information about the signature if requested... */
	info = NULL;
    if (append_info || sig_info) {
	LibBalsaSignatureInfo *tmp_siginfo =
	    g_new0(LibBalsaSignatureInfo, 1);
	
	tmp_siginfo->status = status;
	get_sig_info_from_ctx(tmp_siginfo, ctx);
	if (append_info)
	    info = libbalsa_signature_info_to_gchar(tmp_siginfo, date_string);
	if (sig_info) {
	    libbalsa_signature_info_destroy(*sig_info);
	    *sig_info = tmp_siginfo;
	} else
	    libbalsa_signature_info_destroy(tmp_siginfo);
    }

    if (!(plain_buffer = gpgme_data_release_and_get_mem(out, &datasize))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not get gpgme data: %s"),
			     gpgme_strerror(err));
	g_free(info);
        gpgme_release(ctx);
        return GPGME_SIG_STAT_ERROR;
    }

    /* convert the result back to utf-8 if necessary */
    if (charset && g_ascii_strcasecmp(charset, "utf-8"))
	result = g_convert(plain_buffer, datasize,
			   "utf-8", charset,
			   NULL, NULL, NULL);
    else
	result = g_strndup(plain_buffer, datasize);
    g_free(plain_buffer);
    gpgme_release(ctx);
    g_free(*buffer);

    if (info) {
	*buffer = g_strconcat(result, RFC2440_SEPARATOR, info, NULL);
	g_free(result);
	g_free(info);
    } else
	*buffer = result;
  
    return status;
}


/*
 * Encrypt the data in buffer for all addresses in encrypt_for and return 
 * the new buffer on success or NULL on error. If sign_for is not NULL,
 * buffer is signed before encrypting.
 */
gchar *
libbalsa_rfc2440_encrypt_buffer(const gchar *buffer, const gchar *sign_for,
				GList *encrypt_for, GtkWindow *parent)
{
    GpgmeCtx ctx;
    GpgmeError err;
    GpgmeRecipients rcpt;
    GpgmeData in, out;
    PassphraseCB cb_data;
    gchar *databuf, *result;
    size_t datasize;

    /* paranoia checks */
    g_return_val_if_fail(buffer != NULL, NULL);
    g_return_val_if_fail(encrypt_for != NULL, NULL);

    /* try to create a gpgme context */
    if ((err = gpgme_new(&ctx)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not create gpgme context: %s"),
			     gpgme_strerror(err));
        return NULL;
    }

    /* build the list of recipients */
    if (!gpgme_build_recipients(&rcpt, ctx, encrypt_for)) {
        gpgme_release(ctx);
        return NULL;
    }

    /* add the signer and a passphrase callback if necessary */
    if (sign_for) {
	if (!gpgme_add_signer(ctx, sign_for)) {
	    gpgme_recipients_release(rcpt);
	    gpgme_release(ctx);
	    return NULL;
	}
	cb_data.ctx = ctx;
	cb_data.title =
	    _("Enter passsphrase to OpenPGP sign the first message part");
	cb_data.parent = parent;
	gpgme_set_passphrase_cb(ctx, get_passphrase_cb, &cb_data);
    }
    
    /* Let gpgme encrypt the data. RFC2440 doesn't request to convert line
       endings for encryption, so we can just get the chars from the buffer. */
    gpgme_set_armor(ctx, 1);
    gpgme_set_textmode(ctx, 1);
    if ((err = gpgme_data_new_from_mem(&in, buffer, strlen(buffer), 0))
	!= GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not get data from buffer: %s"), 
			     gpgme_strerror(err));
        gpgme_recipients_release(rcpt);
	gpgme_release(ctx);
	return NULL;
    }
    if ((err = gpgme_data_new(&out)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not create new data object: %s"),
			     gpgme_strerror(err));
        gpgme_data_release(in);
        gpgme_recipients_release(rcpt);
        gpgme_release(ctx);
        return NULL;
    }    
    if (sign_for)
	err = gpgme_op_encrypt_sign(ctx, rcpt, in, out);
    else
	err = gpgme_op_encrypt(ctx, rcpt, in, out);
    if (err != GPGME_No_Error) {
	if (sign_for)
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("gpgme signing and encryption failed: %s"),
				 gpgme_strerror(err));
	else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("gpgme encryption failed: %s"),
				 gpgme_strerror(err));
        gpgme_data_release(out);
        gpgme_data_release(in);
        gpgme_recipients_release(rcpt);
        gpgme_release(ctx);
        return NULL;
    }

    /* get the result */
    gpgme_data_release(in);
    gpgme_recipients_release(rcpt);
    if (!(databuf = gpgme_data_release_and_get_mem(out, &datasize))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not get gpgme data: %s"),
			     gpgme_strerror(err));
	gpgme_release(ctx);
	return NULL;
    }
    gpgme_release(ctx);

    /* return a newly allocated buffer */
    result = g_strndup(databuf, datasize);
    g_free(databuf);
    return result;
}


/*
 * Decrypt the data in *buffer, if possible check its signature and return
 * the signature's status. If append_info is TRUE, append information about
 * the signature (if available) to buffer. If sig_info id not NULL, a new 
 * structure is allocated and filled. If some error occurs, the buffer
 * is not touched and the routine returns GPGME_SIG_STAT_ERROR.
 */
GpgmeSigStat
libbalsa_rfc2440_decrypt_buffer(gchar **buffer, const gchar *charset,
				gboolean fallback, gboolean append_info, 
				LibBalsaSignatureInfo **sig_info,
				const gchar *date_string, GtkWindow *parent)
{
    GpgmeCtx ctx;
    GpgmeError err;
    GpgmeData in, out;
    PassphraseCB cb_data;
    GpgmeSigStat status;
    gchar *plain_buffer, *info, *result;
    size_t datasize;

    /* paranoia checks */
    g_return_val_if_fail(buffer != NULL, GPGME_SIG_STAT_ERROR);
    g_return_val_if_fail(*buffer != NULL, GPGME_SIG_STAT_ERROR);

    /* try to create a gpgme context */
    if ((err = gpgme_new(&ctx)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not create gpgme context: %s"),
			     gpgme_strerror(err));
	return GPGME_SIG_STAT_ERROR;
    }
    
    /* create the body data stream */
    if ((err = gpgme_data_new_from_mem(&in, *buffer, strlen(*buffer), 0)) !=
	GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not get data from buffer: %s"),
			     gpgme_strerror(err));
	gpgme_release(ctx);
	return GPGME_SIG_STAT_ERROR;
    }

    /* decrypt and (if possible) verify the signature */
    if ((err = gpgme_data_new(&out)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not create new data object: %s"),
			     gpgme_strerror(err));
	gpgme_data_release(in);
	gpgme_release(ctx);
	return GPGME_SIG_STAT_ERROR;
    }
    cb_data.ctx = ctx;
    cb_data.title = _("Enter passsphrase to decrypt message part");
    cb_data.parent = parent;
    gpgme_set_passphrase_cb(ctx, get_passphrase_cb, &cb_data);
    if ((err = gpgme_op_decrypt_verify(ctx, in, out, &status))
	!= GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme decryption and signature verification failed: %s"),
			     gpgme_strerror(err));
	gpgme_data_release(out);
	gpgme_data_release(in);
	gpgme_release(ctx);
	return GPGME_SIG_STAT_NONE;
    }
    gpgme_data_release(in);

    /* get some information about the signature if requested... */
	info = NULL;
    if (status != GPGME_SIG_STAT_NONE && (append_info || sig_info)) {
	LibBalsaSignatureInfo *tmp_siginfo =
	    g_new0(LibBalsaSignatureInfo, 1);
	
	tmp_siginfo->status = status;
	get_sig_info_from_ctx(tmp_siginfo, ctx);
	if (append_info)
	    info = libbalsa_signature_info_to_gchar(tmp_siginfo, date_string);
	if (sig_info) {
	    libbalsa_signature_info_destroy(*sig_info);
	    *sig_info = tmp_siginfo;
	} else
	    libbalsa_signature_info_destroy(tmp_siginfo);
    }

    if (!(plain_buffer = gpgme_data_release_and_get_mem(out, &datasize))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not get gpgme data: %s"),
			     gpgme_strerror(err));
	g_free(info);
        gpgme_release(ctx);
        return GPGME_SIG_STAT_ERROR;
    }
    gpgme_release(ctx);

    /* convert the result back to utf-8 if necessary */
    /* FIXME: remove \r's if the message came from a winbloze mua? */
    if (charset && g_ascii_strcasecmp(charset, "utf-8")) {
	if (!g_ascii_strcasecmp(charset, "us-ascii")) {
	    gchar const *target = NULL;
	    result = g_strndup(plain_buffer, datasize);

	    if (!libbalsa_utf8_sanitize(&result, fallback, &target))
		libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
				     _("The OpenPGP encrypted message part contains 8-bit characters, but no header describing the used codeset (converted to %s)"),
				     target ? target : "\"?\"");
	} else {
	    /* be sure to handle wrongly encoded messages... */
	    GError *err = NULL;
	    gsize bytes_read;
	    
	    result = g_convert_with_fallback(plain_buffer, datasize,
					     "utf-8", charset, "?",
					     &bytes_read, NULL, &err);
	    while (err) {
		plain_buffer[bytes_read] = '?';
		g_error_free(err);
		err = NULL;
		result = g_convert_with_fallback(plain_buffer, datasize,
						 "utf-8", charset, "?",
						 &bytes_read, NULL, &err);
	    }
	}
    } else
	result = g_strndup(plain_buffer, datasize);
    g_free(plain_buffer);
    g_free(*buffer);

    if (info) {
	*buffer = g_strconcat(result, RFC2440_SEPARATOR, info, NULL);
	g_free(result);
	g_free(info);
    } else
	*buffer = result;
  
    return status;
}


#ifdef HAVE_GPG

#include <sys/wait.h>
#include <fcntl.h>

/* run gpg asynchronously to import a key */
typedef struct _spawned_gpg_T {
    gint child_pid;
    gint standard_error;
    GString *stderr_buf;
    GtkWindow *parent;
} spawned_gpg_T;

static gboolean check_gpg_child(gpointer data);

gboolean
gpg_ask_import_key(const gchar *message, GtkWindow *parent, 
		   const gchar *fingerprint)
{
    gboolean spawnres;
    GtkWidget *dialog;
    gint dialog_res;
    gchar **argv;
    spawned_gpg_T *spawned_gpg;

    /* display a dialog, asking the user if (s)he wants to import the key */
    dialog =
	gtk_message_dialog_new (parent,
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_QUESTION,
				GTK_BUTTONS_YES_NO,
				_("%s\nDo you want to run gpg to import the public key with fingerprint %s?"),
				message, fingerprint);
    dialog_res = gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    if (dialog_res != GTK_RESPONSE_YES)
	return FALSE;

    /* launch gpg... */
    argv = g_new(gchar *, 5);
    argv[0] = g_strdup(GPG_PATH);
    argv[1] = g_strdup("--no-greeting");
    argv[2] = g_strdup("--recv-keys");
    argv[3] = g_strdup(fingerprint);
    argv[4] = NULL;
    spawned_gpg = g_new0(spawned_gpg_T, 1);
    spawnres =
	g_spawn_async_with_pipes(NULL, argv, NULL,
				 G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL,
				 NULL, NULL, &spawned_gpg->child_pid,
				 NULL, NULL,
				 &spawned_gpg->standard_error,
				 NULL);
    g_strfreev(argv);
    if (spawnres == FALSE) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Could not launch %s to get the public key %s."),
			     GPG_PATH, fingerprint);
	g_free(spawned_gpg);
	return FALSE;
    }

    /* install an idle handler to check if the child returnd successfully. */
    fcntl(spawned_gpg->standard_error, F_SETFL, O_NONBLOCK);
    spawned_gpg->stderr_buf = g_string_new("");
    spawned_gpg->parent = parent;
    g_idle_add(check_gpg_child, spawned_gpg);

    return TRUE;
}


static gboolean
check_gpg_child(gpointer data)
{
    spawned_gpg_T *spawned_gpg  = (spawned_gpg_T *)data;
    int status, bytes_read;
    gchar buffer[1024], *gpg_message;
    GtkWidget *dialog;

    /* read input from the child and append it to the buffer */
    while ((bytes_read =
	    read(spawned_gpg->standard_error, buffer, 1023)) > 0) {
	buffer[bytes_read] = '\0';
	g_string_append(spawned_gpg->stderr_buf, buffer);
    }

    /* check if the child exited */
    if (waitpid(spawned_gpg->child_pid, &status, WNOHANG) !=
	spawned_gpg->child_pid)
	return TRUE;
    
    /* child exited, display some information... */
    close(spawned_gpg->standard_error);

    gpg_message = 
	g_locale_to_utf8(spawned_gpg->stderr_buf->str, -1, NULL,
			 &bytes_read, NULL);
    gdk_threads_enter();
    if (WEXITSTATUS(status) > 0)
	dialog = 
	    gtk_message_dialog_new(spawned_gpg->parent, 
				   GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
				   _("Running gpg failed with return value %d:\n%s"),
				   WEXITSTATUS(status), gpg_message);
    else
	dialog = 
	    gtk_message_dialog_new(spawned_gpg->parent,
				   GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
				   _("Running gpg successful:\n%s"),
				   gpg_message);
    g_free(gpg_message);
    g_string_free(spawned_gpg->stderr_buf, TRUE);
    g_free(spawned_gpg);

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    gdk_threads_leave();

    return FALSE;
}

#endif


/* ==== local stuff ======================================================== */


static const gchar *
libbalsa_gpgme_validity_to_gchar_short(GpgmeValidity validity)
{
    switch (validity)
	{
	case GPGME_VALIDITY_UNKNOWN:
	    return _("unknown");
	case GPGME_VALIDITY_UNDEFINED:
	    return _("undefined");
	case GPGME_VALIDITY_NEVER:
	    return _("never");
	case GPGME_VALIDITY_MARGINAL:
	    return _("marginal");
	case GPGME_VALIDITY_FULL:
	    return _("full");
	case GPGME_VALIDITY_ULTIMATE:
	    return _("ultimate");
	default:
	    return _("bad validity");
	}
}


/*
 * extract the signature info fields from ctx
 */
static void
get_sig_info_from_cipher_context(LibBalsaSignatureInfo* info,
			   GMimeGpgMEContext* ctx)
{
    info->fingerprint = g_strdup(ctx->fingerprint);
    info->validity =
	gpgme_key_get_ulong_attr(ctx->key, GPGME_ATTR_VALIDITY, NULL, 0);
    info->sign_name =
	g_strdup(gpgme_key_get_string_attr(ctx->key, GPGME_ATTR_NAME, NULL, 0));
    info->sign_email =
	g_strdup(gpgme_key_get_string_attr(ctx->key, GPGME_ATTR_EMAIL,
					   NULL, 0));
    info->key_created =
	gpgme_key_get_ulong_attr(ctx->key, GPGME_ATTR_CREATED, NULL, 0);
    info->status = ctx->sig_status;
}


/*
 * extract the signature info fields from ctx
 */
static void
get_sig_info_from_ctx(LibBalsaSignatureInfo* info, GpgmeCtx ctx)
{
    GpgmeSigStat stat;
    GpgmeKey key;
	
    info->fingerprint =
	g_strdup(gpgme_get_sig_status(ctx, 0, &stat, &info->sign_time));
    gpgme_get_sig_key(ctx, 0, &key);
    info->validity =
	gpgme_key_get_ulong_attr(key, GPGME_ATTR_VALIDITY, NULL, 0);
    info->trust =
	gpgme_key_get_ulong_attr(key, GPGME_ATTR_OTRUST, NULL, 0);
    info->sign_name =
	g_strdup(gpgme_key_get_string_attr(key, GPGME_ATTR_NAME, NULL, 0));
    info->sign_email =
	g_strdup(gpgme_key_get_string_attr(key, GPGME_ATTR_EMAIL, NULL, 0));
    info->key_created =
	gpgme_key_get_ulong_attr(key, GPGME_ATTR_CREATED, NULL, 0);
    gpgme_key_unref(key);
}


/* stuff to get a key fingerprint from a selection list */
enum {
    GPG_KEY_FP_COLUMN = 0,
    GPG_KEY_ID_COLUMN,
    GPG_KEY_VALIDITY_COLUMN,
    GPG_KEY_TRUST_COLUMN,
    GPG_KEY_LENGTH_COLUMN,
    GPG_KEY_NUM_COLUMNS
};

static gchar *col_titles[] =
    { N_("Fingerprint"), N_("Key ID"), N_("Validity"), N_("Owner trust"),
      N_("Length") };


/* callback function if a new row is selected in the list */
static void
key_selection_changed_cb (GtkTreeSelection *selection, gchar **fp)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    
    if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
	g_free(*fp);
	gtk_tree_model_get (model, &iter, GPG_KEY_FP_COLUMN, fp, -1);
    }
}


/*
 * Select a key for the mail address for_address from the GpgmeKey's in keys
 * and return either the selected fingerprint or NULL if the dialog was
 * cancelled. secret_only controls the dialog message.
 */
static gchar *
select_key_fp_from_list(int secret_only, const gchar *for_address, GList *keys,
			GtkWindow *parent)
{
    GtkWidget *dialog;
    GtkWidget *scrolled_window;
    GtkWidget *tree_view;
    GtkTreeStore *model;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    gint i;
    gchar *prompt;
    gchar *use_fp = NULL;
  
    /* FIXME: create dialog according to the Gnome HIG */
    dialog = gtk_dialog_new_with_buttons(_("Select key"),
					 parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_STOCK_OK, GTK_RESPONSE_OK,
					 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					 NULL);

    if (secret_only)
	prompt = g_strdup_printf(_("Select the private key for the signer %s"),
				 for_address);
    else
	prompt = g_strdup_printf(_("Select the public key for the recipient %s"),
				 for_address);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
		       gtk_label_new(prompt), FALSE, TRUE, 0);
    g_free(prompt);

    scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
					 GTK_SHADOW_ETCHED_IN);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
		       scrolled_window, TRUE, TRUE, 0);

    model = gtk_tree_store_new (GPG_KEY_NUM_COLUMNS, G_TYPE_STRING,
				G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				G_TYPE_INT);

    tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
    g_signal_connect (G_OBJECT (selection), "changed",
		      G_CALLBACK (key_selection_changed_cb),
		      &use_fp);

    /* add the keys */
    while (keys) {
	GpgmeKey key = (GpgmeKey)keys->data;

	gtk_tree_store_append (GTK_TREE_STORE (model), &iter, NULL);
	gtk_tree_store_set (GTK_TREE_STORE (model), &iter,
			    GPG_KEY_FP_COLUMN,
			    gpgme_key_get_string_attr(key, GPGME_ATTR_FPR, NULL, 0),
			    GPG_KEY_ID_COLUMN, 
			    gpgme_key_get_string_attr(key, GPGME_ATTR_KEYID, NULL, 0),
			    GPG_KEY_VALIDITY_COLUMN, 
			    libbalsa_gpgme_validity_to_gchar_short(gpgme_key_get_ulong_attr(key, GPGME_ATTR_VALIDITY, NULL, 0)),
			    GPG_KEY_TRUST_COLUMN, 
			    libbalsa_gpgme_validity_to_gchar_short(gpgme_key_get_ulong_attr(key, GPGME_ATTR_OTRUST, NULL, 0)),
			    GPG_KEY_LENGTH_COLUMN,
			    gpgme_key_get_ulong_attr(key, GPGME_ATTR_LEN, NULL, 0),
			    -1);

	keys = g_list_next(keys);
    }
  
    g_object_unref (G_OBJECT (model));

    for (i = 0; i < GPG_KEY_NUM_COLUMNS; i++) {
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (col_titles[i],
							   renderer,
							   "text", i,
							   NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);
	gtk_tree_view_column_set_resizable (column, TRUE);
    }

    gtk_container_add (GTK_CONTAINER (scrolled_window), tree_view);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 300);
    gtk_widget_show_all(GTK_DIALOG(dialog)->vbox);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK) {
	g_free(use_fp);
	use_fp = NULL;
    }
    gtk_widget_destroy(dialog);

    return use_fp;
}


/*
 * Get a key finger print for name. If necessary, a dialog is shown to select
 * a key from a list. Return NULL if something failed.
 */
static gchar *
get_key_fingerprint(GpgmeCtx ctx, const gchar *name, int secret_only,
		    GtkWindow *parent)
{
    GList *keys = NULL;
    GpgmeKey key;
    GpgmeError err;
    gchar *fpr;

    if ((err = gpgme_op_keylist_start(ctx, name, secret_only)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not list keys for %s: %s"), name,
			       gpgme_strerror(err));
	return NULL;
    }
    while ((err = gpgme_op_keylist_next(ctx, &key)) == GPGME_No_Error)
	keys = g_list_append(keys, key);
    if (err != GPGME_EOF) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not retrieve key for %s: %s"), name,
			       gpgme_strerror(err));
	gpgme_op_keylist_end(ctx);
	return NULL;
    }
    gpgme_op_keylist_end(ctx);
    
    if (!keys) 
	return NULL;

    if (g_list_length (keys) > 1)
	fpr = select_key_fp_from_list(secret_only, name, keys, parent);
    else
	fpr = g_strdup(gpgme_key_get_string_attr((GpgmeKey)(keys->data),
						 GPGME_ATTR_FPR, NULL, 0));

    /* unref all keys */
    g_list_foreach(keys, (GFunc)gpgme_key_unref, NULL);
    g_list_free(keys);

    return fpr;
} 


/*
 * Add signer to ctx's list of signers and return TRUE on success or FALSE
 * on error.
 */
static gboolean
gpgme_add_signer(GpgmeCtx ctx, const gchar *signer)
{
    GpgmeKey key = NULL;
    gchar *fingerprint;

    
    if (!(fingerprint = get_key_fingerprint(ctx, signer, 1, NULL))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not get a private key for %s"), signer);
	return FALSE;
    }

    /* set the key (the previous operation guaranteed that it exists...) */
    gpgme_op_keylist_start(ctx, fingerprint, 1);
    gpgme_op_keylist_next(ctx, &key);
    gpgme_signers_add(ctx, key);
    gpgme_key_unref(key);
    gpgme_op_keylist_end(ctx);
    g_free(fingerprint);

    return TRUE;
}


/*
 * Build rcpt from rcpt_list and return TRUE on success or FALSE on error.
 */
static gboolean
gpgme_build_recipients(GpgmeRecipients *rcpt, GpgmeCtx ctx, GList *rcpt_list)
{
    GpgmeError err;

    if ((err = gpgme_recipients_new(rcpt)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not create gpgme recipients set: %s"),
			     gpgme_strerror(err));
        return FALSE;
    }

    /* try to find the public key for every recipient */
    while (rcpt_list) {
        gchar *name = (gchar *)rcpt_list->data;
	gchar *fingerprint;

        if (!(fingerprint = get_key_fingerprint(ctx, name, 0, NULL))) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("could not get a key for %s"), name);
            gpgme_recipients_release(*rcpt);
            return FALSE;
        }

        /* set the recipient */
	libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
			     "encrypt for %s with fpr %s", name, fingerprint);
        if ((err = gpgme_recipients_add_name_with_validity(*rcpt, fingerprint, 
                                                           GPGME_VALIDITY_FULL)) != 
            GPGME_No_Error) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("could not add recipient %s: %s"), name, 
				 gpgme_strerror(err));
	    g_free(fingerprint);
            gpgme_recipients_release(*rcpt);
            return FALSE;
        }
	g_free(fingerprint);

        rcpt_list = g_list_next(rcpt_list);
    }
    libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
			 "encrypting for %d recipient(s)",
			 gpgme_recipients_count(*rcpt));
    
    return TRUE;
}


/* 
 * Called by gpgme to get a new chunk of data. We read from the gchar ** in
 * hook and convert every single '\n' into a '\r\n' sequence.
 */
static int
read_cb_MailDataBuffer(void *hook, char *buffer, size_t count, size_t *nread)
{
    MailDataBuffer *inbuf = (MailDataBuffer *)hook;
    
    *nread = 0;
    if (*(inbuf->ptr) == '\0')
        return -1;

    /* try to fill buffer */
    while (count > 1 && *(inbuf->ptr) != '\0') {
	if (*(inbuf->ptr) == '\n' && !inbuf->last_was_cr) {
	    *buffer++ = '\r';
	    *nread += 1;
	    count -= 1;
	}
	inbuf->last_was_cr = (*(inbuf->ptr) == '\r');
	*buffer++ = *(inbuf->ptr);
	inbuf->ptr += 1;
	*nread += 1;
	count -= 1;
    }
    return 0;
}


#ifdef ENABLE_PCACHE
/* helper functions for the passphrase cache */
/*
 * destroy a bf_crypt_T data object by first overwriting the encrypted data
 * with random crap and then freeing all allocated stuff
 */
static bf_crypt_T *
bf_destroy(bf_crypt_T *crypt)
{
    if (crypt) {
	unsigned char *p = crypt->buf;

	if (p) {
	    while (crypt->len--)
		*p++ = random();
	    g_free(crypt->buf);
	}
	g_free(crypt);
    }
    return NULL;
}


/*
 * Encrypt cleartext using the key bf_key and return the encrypted stuff as
 * a pointer to a bf_crypt_T struct or NULL on error
 */
static bf_crypt_T *
bf_encrypt(const gchar *cleartext, bf_key_T *bf_key)
{
    EVP_CIPHER_CTX ctx;
    unsigned char *outbuf;
    gint outlen, tmplen;
    bf_crypt_T *result;

    if (!cleartext)
	return NULL;

    EVP_CIPHER_CTX_init(&ctx);
    EVP_EncryptInit(&ctx, EVP_bf_cbc(), bf_key->key, bf_key->iv);
    outbuf = g_malloc(strlen(cleartext) + 9);  /* FIXME: correct/safe? */
    if (!EVP_EncryptUpdate(&ctx, outbuf, &outlen, (unsigned char *)cleartext,
			  strlen(cleartext) + 1)) {
	g_free(outbuf);
	EVP_CIPHER_CTX_cleanup(&ctx);
	return NULL;
    }
    if (!EVP_EncryptFinal(&ctx, outbuf + outlen, &tmplen)) {
	g_free(outbuf);
	EVP_CIPHER_CTX_cleanup(&ctx);
	return NULL;
    }
    outlen += tmplen;
    EVP_CIPHER_CTX_cleanup(&ctx);

    result = g_malloc(sizeof(bf_crypt_T));
    result->buf = outbuf;
    result->len = outlen;
    return result;
}


/*
 * decrypt crypttext using bf_key and return the string on success or NULL
 * on error
 */
static gchar *
bf_decrypt(bf_crypt_T *crypttext, bf_key_T *bf_key)
{
    EVP_CIPHER_CTX ctx;
    gchar *outbuf;
    gint outlen, tmplen;

    if (!crypttext)
	return NULL;

    EVP_CIPHER_CTX_init(&ctx);
    EVP_DecryptInit(&ctx, EVP_bf_cbc(), bf_key->key, bf_key->iv);
    outbuf = g_malloc(crypttext->len);  /* FIXME: correct/safe? */
    if (!EVP_DecryptUpdate(&ctx, outbuf, &outlen, crypttext->buf,
			  crypttext->len)) {
	g_free(outbuf);
	EVP_CIPHER_CTX_cleanup(&ctx);
	return NULL;
    }
    if (!EVP_DecryptFinal(&ctx, outbuf + outlen, &tmplen))  {
	g_free(outbuf);
	EVP_CIPHER_CTX_cleanup(&ctx);
	return NULL;
    }
    EVP_CIPHER_CTX_cleanup(&ctx);
    
    return outbuf;
}


/*
 * timeout function to check for expired cached passphrases
 */
static gboolean
pcache_timeout(pcache_T *cache)
{
    time_t now = time(NULL);
    GList *list = cache->cache;

    while (list) {
	pcache_elem_T *elem = (pcache_elem_T *)list->data;

	if (elem->expires <= now) {
	    GList *next = list->next;

	    elem->passphrase = bf_destroy(elem->passphrase);
	    g_free(elem);
	    cache->cache = g_list_delete_link(cache->cache, list);
	    list = next;
	} else
	    list = g_list_next(list);
    }
	
    return TRUE;
}


/*
 * Check if the passphrase requested for desc is already in cache. If it is
 * there and gpgme complained about a bad passphrase, erase it and return
 * NULL, otherwise return the passphrase. If it is not in the cache, the
 * routine also returns NULL.
 */
static gchar *
check_cache(pcache_T *cache, const gchar *desc)
{
    gchar *p;
    gchar **desc_parts;
    unsigned char tofind[MD5_DIGEST_LENGTH];
    GList *list;

    /* exit immediately if no data is present */
    if (!cache->enable || !cache->cache)
	return NULL;

    /* try to find the name in the cache */
    desc_parts = g_strsplit(desc, "\n", 3);
    p = strchr(desc_parts[1], ' ');   /* skip the fingerprint */
    if (!p) {
	g_strfreev(desc_parts);
	return NULL;
    }
    MD5(p, strlen(p), tofind);
    g_strfreev(desc_parts);

    list = cache->cache;
    while (list) {
	pcache_elem_T *elem = (pcache_elem_T *)list->data;

	if (!memcmp(tofind, elem->name, MD5_DIGEST_LENGTH)) {
	    /* check if the last entry was bad */
	    if (!strncmp(desc, "TRY_AGAIN", 9)) {
		elem->passphrase = bf_destroy(elem->passphrase);
		g_free(elem);
		cache->cache = g_list_delete_link(cache->cache, list);
		return NULL;
	    } else
		return bf_decrypt(elem->passphrase, &cache->bf_key);
	}
	list = g_list_next(list);
    }

    /* not found */
    return NULL;
}


/*
 * Try to destroy the cache info (called upon SIGSEGV)
 */
static void
destroy_cache(int signo)
{
    if (pcache) {
	int n;

	fprintf(stderr, "caught signal %d, destroy passphrase cache...\n",
		signo);
	for (n = 0; n < 16; n++)
	    pcache->bf_key.key[n] = 0;
	for (n = 0; n < 8; n++)
	    pcache->bf_key.iv[n] = 0;
	pcache->cache = NULL;
    }
    segvhandler(signo);
}


/*
 * initialise the passphrase cache
 */
static pcache_T *
init_pcache()
{
    pcache_T *cache;
    struct stat cfg_stat;

    /* be sure that openssl is initialised */
    ssl_init();  /* from libmutt */

    /* initialise the internal passphrase cache stuff */
    cache = g_new0(pcache_T, 1);

    /* the cfg file must be owned by root and not group/world writable */
    if (stat(BALSA_DATA_PREFIX "/gpg-cache", &cfg_stat) == -1) {
	libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
			     "file %s/gpg-cache not found, disable passphrase cache",
			     BALSA_DATA_PREFIX);
	return cache;
    }
    if (cfg_stat.st_uid != 0 || !S_ISREG(cfg_stat.st_mode) ||
	(cfg_stat.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
	libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
			     "%s/gpg-cache must be a regular file, owened by root with (max) permissions 0644",
			     BALSA_DATA_PREFIX);
	return cache;
    }

    /* get the passphrase cache config */
    gnome_config_push_prefix("=" BALSA_DATA_PREFIX "/gpg-cache=/PassphraseCache/");
    cache->enable =
	gnome_config_get_bool_with_default("enable", &cache->enable);
    cache->max_mins =
	gnome_config_get_int_with_default("minutesMax", &cache->max_mins);
    if (cache->max_mins <= 0)
	cache->enable = FALSE;
    gnome_config_pop_prefix();
    libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
			 "passphrase cache is %s",
			 (cache->enable) ? "enabled" : "disabled");

    if (cache->enable) {
	/* generate a random blowfish key */
	RAND_bytes(cache->bf_key.key, 16);
	RAND_bytes(cache->bf_key.iv, 8);
	
	/* install a segv handler to destroy the cache on crash */
	segvhandler = signal(SIGSEGV, destroy_cache);
	
	/* add a timeout function (called every 5 secs) to erase expired
	   passphrases */
	g_timeout_add(5000, (GSourceFunc)pcache_timeout, cache);
    }

    return cache;
}


/*
 * callback for (de)activating the timeout spinbutton
 */
static void
activate_mins(GtkToggleButton *btn, GtkWidget *target)
{
    gtk_widget_set_sensitive(target, gtk_toggle_button_get_active(btn));
}
#endif


/*
 * display a dialog to read the passphrase
 */
static gchar *
#ifdef ENABLE_PCACHE
get_passphrase_real(PassphraseCB *cb_data, const gchar *desc, pcache_T *pcache)
#else
get_passphrase_real(PassphraseCB *cb_data, const gchar *desc)
#endif
{
    gchar **desc_parts;
    GtkWidget *dialog, *entry;
    gchar *prompt, *passwd;
#ifdef ENABLE_PCACHE
    GtkWidget *cache_but = NULL, *cache_min = NULL;
#endif
    
    desc_parts = g_strsplit(desc, "\n", 3);

    /* FIXME: create dialog according to the Gnome HIG */
    dialog = gtk_dialog_new_with_buttons(cb_data->title,
                                         cb_data->parent,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         NULL);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 12);
    if (!strcmp(desc_parts[0], "TRY_AGAIN"))
	prompt = 
	    g_strdup_printf(_("The passphrase for this key was bad, please try again!\n\nKey: %s"),
			    desc_parts[1]);
    else
	prompt = 
	    g_strdup_printf(_("Please enter the passphrase for the secret key!\n\nKey: %s"),
			    desc_parts[1]);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
                      gtk_label_new(prompt));
    g_free(prompt);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
                      entry = gtk_entry_new());

#ifdef ENABLE_PCACHE
    if (pcache->enable) {
	GtkWidget *hbox;

	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
			  hbox = gtk_hbox_new(FALSE, 12));
	cache_but = 
	    gtk_check_button_new_with_label(_("remember passphrase for"));
	gtk_box_pack_start(GTK_BOX(hbox), cache_but, FALSE, FALSE, 0);
	cache_min = gtk_spin_button_new_with_range(1.0, pcache->max_mins, 1.0);
	gtk_widget_set_sensitive(cache_min, FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), cache_min, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("minutes")),
			   FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(cache_but), "clicked", 
			 (GCallback)activate_mins, cache_min);
    }
#endif

    gtk_widget_show_all(GTK_WIDGET(GTK_DIALOG(dialog)->vbox));
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 40);
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);

    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_grab_focus (entry);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
        passwd = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
    else
	passwd = NULL;

#ifdef ENABLE_PCACHE
    if (pcache->enable) {
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cache_but))) {
	    gchar *p = strchr(desc_parts[1], ' ');
	    
	    if (p) {
		pcache_elem_T *elem = g_new(pcache_elem_T, 1);

		MD5(p, strlen(p), elem->name);
		elem->passphrase = bf_encrypt(passwd, &pcache->bf_key);
		elem->expires = time(NULL) + 
		    60 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(cache_min));
		pcache->cache = g_list_append(pcache->cache, elem);
	    }
	}
    }
#endif

    g_strfreev(desc_parts);
    gtk_widget_destroy(dialog);

    return passwd;
}


#ifdef BALSA_USE_THREADS
/* FIXME: is this really necessary? */
typedef struct {
    pthread_cond_t cond;
    PassphraseCB *cb_data;
    const gchar *desc;
#ifdef ENABLE_PCACHE
    pcache_T *pcache;
#endif
    gchar* res;
} AskPassphraseData;

/* get_passphrase_idle:
   called in MT mode by the main thread.
 */
static gboolean
get_passphrase_idle(gpointer data)
{
    AskPassphraseData* apd = (AskPassphraseData*)data;

    gdk_threads_enter();
#ifdef ENABLE_PCACHE
    apd->res = get_passphrase_real(apd->cb_data, apd->desc, apd->pcache);
#else
    apd->res = get_passphrase_real(apd->cb_data, apd->desc);
#endif
    gdk_threads_leave();
    pthread_cond_signal(&apd->cond);
    return FALSE;
}
#endif


/*
 * Called by gpgme to get the passphrase for a key.
 */
static const gchar *
get_passphrase_cb(void *opaque, const char *desc, void **r_hd)
{
    PassphraseCB *cb_data = (PassphraseCB *)opaque;
    gchar *passwd = NULL;

    g_return_val_if_fail(cb_data != NULL, NULL);
    g_return_val_if_fail(cb_data->ctx != NULL, NULL);

    if (!desc) {
	if (*r_hd) {
	    /* paranoia: wipe passphrase from memory */
	    gchar *p = *r_hd;
	    while (*p)
		*p++ = random();
	    g_free(*r_hd);
	    *r_hd = NULL;
	}
        return NULL;
    }
    
#ifdef ENABLE_PCACHE    
    if (!pcache)
	pcache = init_pcache();

    /* check if we have the passphrase already cached... */
    if ((passwd = check_cache(pcache, desc))) {
	*r_hd = passwd;
	return passwd;
    }
#endif

#ifdef BALSA_USE_THREADS
    if (pthread_self() == libbalsa_get_main_thread())
#ifdef ENABLE_PCACHE
	passwd = get_passphrase_real(cb_data, desc, pcache);
#else
	passwd = get_passphrase_real(cb_data, desc);
#endif
    else {
	static pthread_mutex_t get_passphrase_lock = PTHREAD_MUTEX_INITIALIZER;
	AskPassphraseData apd;

	pthread_mutex_lock(&get_passphrase_lock);
	pthread_cond_init(&apd.cond, NULL);
	apd.cb_data = cb_data;
	apd.desc = desc;
#ifdef ENABLE_PCACHE
	apd.pcache = pcache;
#endif
	g_idle_add(get_passphrase_idle, &apd);
	pthread_cond_wait(&apd.cond, &get_passphrase_lock);
    
	pthread_cond_destroy(&apd.cond);
	pthread_mutex_unlock(&get_passphrase_lock);
	pthread_mutex_destroy(&get_passphrase_lock);
	passwd = apd.res;
    }
#else
#ifdef ENABLE_PCACHE
    passwd = get_passphrase_real(cb_data, desc, pcache);
#else
    passwd = get_passphrase_real(cb_data, desc);
#endif /* ENABLE_PCACHE */
#endif /* BALSA_USE_THREADS */

    if (!passwd)
	gpgme_cancel(cb_data->ctx);

    *r_hd = passwd;
    return passwd;
}


/*
 * Signs the data in mailData (the file is assumed to be open and rewinded)
 * for sign_for. Return either a signature block or NULL on error. Upon
 * success, micalg is set to the used mic algorithm (must be freed).
 */
static GMimeMultipartSigned *
gpgme_signature(GMimeObject *content,
		const gchar *sign_for,
		GtkWindow *parent)
{
    GMimeSession *session;
    GMimeGpgMEContext *ctx;
    PassphraseCB cb_data;
    GMimeMultipartSigned *mps;
    GError *err=NULL;
    gchar *micalg;

    /* try to create GMimeGpgMEContext */
    session = g_object_new(libbalsa_g_mime_session_get_type(), NULL, NULL);
    ctx = GMIME_GPGME_CONTEXT(g_mime_gpgme_context_new(session));
    if (ctx == NULL) {
	g_warning("could not create GMimeGPG context");
	g_object_unref (session);
	return FALSE;
    }

    ((LibBalsaGMimeSession*)session)->data = &cb_data;
//    cb_data.ctx = ctx;
    cb_data.parent = parent;
    cb_data.title = _("Enter passsphrase to sign message");
    /* find the secret key for the "sign_for" address */
    if (!gpgme_add_signer(ctx->gpgme_ctx, sign_for)) {
	g_warning("Setting key for signing");
	g_object_unref (session);
	return FALSE;
    }

    mps = g_mime_multipart_signed_new();
    if (g_mime_multipart_signed_sign(mps, content, GMIME_CIPHER_CONTEXT(ctx),
				     sign_for, GMIME_CIPHER_HASH_DEFAULT,
				     &err) != 0) {
	g_object_unref (ctx);
	g_object_unref (session);
	g_object_unref (mps);
	return NULL;
    }

    micalg = ctx->micalg;
    g_mime_object_set_content_type_parameter(GMIME_OBJECT(mps),
					     "micalg", micalg);

    g_object_unref (ctx);
    g_object_unref (session);

    return mps;
}


static void
set_decrypt_file(LibBalsaMessageBody *body, const gchar *fname)
{
    for (; body; body = body->next) {
	body->decrypt_file = g_strdup(fname);
	if (body->parts)
	    set_decrypt_file(body->parts, fname);
    }
}


/* ==== local functions ==================================================== */
typedef struct _LibBalsaGMimeSessionClass LibBalsaGMimeSessionClass;

struct _LibBalsaGMimeSessionClass {
	GMimeSessionClass parent_class;
	
};

static void libbalsa_g_mime_session_class_init (LibBalsaGMimeSessionClass *klass);

static char *request_passwd (GMimeSession *session, const char *prompt,
			     gboolean secret, const char *item,
			     GError **err);
static void forget_passwd    (GMimeSession *session,
			      const char *item,
			      GError **err);


static GMimeSessionClass *parent_class = NULL;


static GType
libbalsa_g_mime_session_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (LibBalsaGMimeSessionClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) libbalsa_g_mime_session_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (LibBalsaGMimeSession),
			0,    /* n_preallocs */
			NULL, /* object_init */
		};
		
		type = g_type_register_static (GMIME_TYPE_SESSION, "LibBalsaGMimeSession", &info, 0);
	}
	
	return type;
}


static void
libbalsa_g_mime_session_class_init (LibBalsaGMimeSessionClass *klass)
{
/*	GObjectClass *object_class = G_OBJECT_CLASS (klass);*/
	GMimeSessionClass *session_class = GMIME_SESSION_CLASS (klass);
	
	parent_class = g_type_class_ref (GMIME_TYPE_SESSION);
	
	session_class->request_passwd = request_passwd;
	session_class->forget_passwd = forget_passwd;
}

static char *
request_passwd (GMimeSession *session, const char *prompt, gboolean secret, const char *item, GError **err)
{
    PassphraseCB *cb_data = ((LibBalsaGMimeSession *)session)->data;
    char *passwd;
    const char *desc = prompt;
#ifdef BALSA_USE_THREADS
    if (pthread_self() == libbalsa_get_main_thread())
	passwd = get_passphrase_real(cb_data, desc);
    else {
	static pthread_mutex_t get_passphrase_lock = PTHREAD_MUTEX_INITIALIZER;
	AskPassphraseData apd;

	pthread_mutex_lock(&get_passphrase_lock);
	pthread_cond_init(&apd.cond, NULL);
	apd.cb_data = cb_data;
	apd.desc = desc;
	g_idle_add(get_passphrase_idle, &apd);
	pthread_cond_wait(&apd.cond, &get_passphrase_lock);
    
	pthread_cond_destroy(&apd.cond);
	pthread_mutex_unlock(&get_passphrase_lock);
	pthread_mutex_destroy(&get_passphrase_lock);
	passwd = apd.res;
    }
#else
    passwd = get_passphrase_real(cb_data, desc);
#endif
    
    ((LibBalsaGMimeSession *)session)->passwd = passwd;

    return passwd;
}

static void
forget_passwd (GMimeSession *session, const char *item, GError **err)
{
    gchar *passwd = ((LibBalsaGMimeSession *)session)->passwd;
    if (passwd) {
	/* paranoia: wipe passphrase from memory */
	gchar *p = passwd;
	while (*p)
	    *p++ = random();
	g_free(passwd);
    }
}

#endif /* HAVE_GPGME */
