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

#include "mailbackend.h"
#include "libbalsa.h"
#include "libbalsa_private.h"
#include "rfc3156.h"

#include <gpgme.h>

#ifdef BALSA_USE_THREADS
#  include <pthread.h>
#  include "misc.h"
#endif

#include "padlock-keyhole.xpm"


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
    gpgme_ctx_t ctx;
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
static gboolean gpgme_check_set_protocol(gpgme_ctx_t ctx, gpgme_protocol_t proto);
static const gchar *libbalsa_gpgme_validity_to_gchar_short(gpgme_validity_t validity);
static gpgme_error_t get_passphrase_cb(void *opaque, const char *uid_hint,
				       const char *passph_info, int prev_wasbad,
				       int fd);
static gchar *gpgme_signature(MailDataMBox *mailData, const gchar *sign_for,
			      gchar **micalg, GtkWindow *parent);
static gchar *gpgme_encrypt_file(MailDataMBox *mailData, GList *encrypt_for);
static ssize_t read_cb_MailDataMBox(void *hook, char *buffer, size_t count);
static ssize_t read_cb_MailDataBuffer(void *hook, char *buffer, size_t count);
static void set_decrypt_file(LibBalsaMessageBody *body, const gchar *fname);
static gboolean gpgme_add_signer(gpgme_ctx_t ctx, const gchar *signer);
static gpgme_key_t *gpgme_build_recipients(gpgme_ctx_t ctx, GList *rcpt_list);
static void get_sig_info_from_ctx(LibBalsaSignatureInfo* info, gpgme_ctx_t ctx);
static void release_keylist(gpgme_key_t *keylist);
static void cb_data_release(void * handle);


/* ==== public functions =================================================== */

gboolean
libbalsa_check_crypto_engine(gpgme_protocol_t protocol)
{
    gpgme_error_t err;
    
    err = gpgme_engine_check_version(protocol);
    if (gpgme_err_code(err) != GPG_ERR_NO_ERROR) {
	gpgme_engine_info_t info;
	GString * message = g_string_new("");
	
	err = gpgme_get_engine_info(&info);
	if (err == GPG_ERR_NO_ERROR) {
	    while (info && info->protocol != protocol)
		info = info->next;
	    if (!info)
		g_string_append_printf(message, 
				       _("Gpgme has been compiled without support for protocol %s."),
				       gpgme_get_protocol_name(protocol));
	    else if (info->file_name && !info->version)
		g_string_append_printf(message,
				       _("Crypto engine %s is not installed properly."),
				       info->file_name);
	    else if (info->file_name && info->version && info->req_version)
		g_string_append_printf(message,
				       _("Crypto engine %s version %s is installed, but at least version %s is required."),
				       info->file_name, info->version, info->req_version);
	    else
		g_string_append_printf(message,
				       _("Unknown problem with engine for protocol %s."),
				       gpgme_get_protocol_name(protocol));
	} else
	    g_string_append_printf(message,
				   _("%s: could not retreive crypto engine information: %s."),
				   gpgme_strsource(err), gpgme_strerror(err));
	g_string_append_printf(message,
			       _("\nDisable support for protocol %s."),
			       gpgme_get_protocol_name(protocol));
	libbalsa_information(LIBBALSA_INFORMATION_ERROR, message->str);
	g_string_free(message, TRUE);
	return FALSE;
    } else
	return TRUE;
}


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
    if (info->issuer_serial)
	g_free(info->issuer_serial);
    if (info->issuer_name)
	g_free(info->issuer_name);
    if (info->chain_id)
	g_free(info->chain_id);
    if (info->sign_uid)
	g_free(info->sign_uid);
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

    switch (info->protocol) {
    case GPGME_PROTOCOL_OpenPGP:
	msg = g_string_new(_("PGP Signature: "));
	break;
    case GPGME_PROTOCOL_CMS:
	msg = g_string_new(_("S/MIME Signature: "));
	break;
    default:
	msg = g_string_new(_("(unknown protocol) "));
    }
    msg = g_string_append(msg, libbalsa_gpgme_sig_stat_to_gchar(info->status));

    if (info->sign_uid && strlen(info->sign_uid))
	g_string_append_printf(msg, _("\nUser ID: %s"), info->sign_uid);
    else if (info->sign_name && strlen(info->sign_name)) {
	g_string_append_printf(msg, _("\nSigned by: %s"), info->sign_name);
	if (info->sign_email && strlen(info->sign_email))
	    g_string_append_printf(msg, " <%s>", info->sign_email);
    } else if (info->sign_email && strlen(info->sign_email))
	g_string_append_printf(msg, _("\nMail address: %s"),
			       info->sign_email);

    if (info->sign_time) {
	gchar buf[128];
	strftime(buf, 127, date_string, localtime(&info->sign_time));
	g_string_append_printf(msg, _("\nSigned on: %s"), buf);
    }
    g_string_append_printf(msg, _("\nValidity: %s"),
			   libbalsa_gpgme_validity_to_gchar(info->validity));
    if (info->protocol == GPGME_PROTOCOL_OpenPGP)
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
    if (info->issuer_name)
	g_string_append_printf(msg, _("\nIssuer name: %s"),
			       info->issuer_name);
    if (info->issuer_serial)
	g_string_append_printf(msg, _("\nIssuer serial number: %s"),
			       info->issuer_serial);
    if (info->chain_id)
	g_string_append_printf(msg, _("\nChain ID: %s"),
			       info->chain_id);

    retval = msg->str;
    g_string_free(msg, FALSE);
    return retval;
}


/*
 * Check if body (and eventually its subparts) are RFC 2633 or RFC 3156 signed
 * or encrypted.
 */
gint
libbalsa_message_body_protection(LibBalsaMessageBody *body)
{
    gint result = 0;
    BODY *mb_body;

    g_return_val_if_fail(body != NULL, 0);
    g_return_val_if_fail(body->mutt_body != NULL, 0);

    libbalsa_lock_mutt();
    mb_body = body->mutt_body;

    if (mb_body->type == TYPEMULTIPART) {
	const gchar *protocol;

	protocol = mutt_get_parameter("protocol", mb_body->parameter);
	if (!g_ascii_strcasecmp("signed", mb_body->subtype)) {
	    result = LIBBALSA_PROTECT_SIGN;
	    if (protocol && body->parts && body->parts->mutt_body &&
		body->parts->next && body->parts->next->mutt_body) {
		const gchar *micalg;
		BODY *mb_sig;
		    
		micalg = mutt_get_parameter("micalg", mb_body->parameter);
		mb_sig = body->parts->next->mutt_body;
		if (!g_ascii_strcasecmp("application/pgp-signature", protocol)) {
		    result |= LIBBALSA_PROTECT_RFC3156;
		    if (!micalg || g_ascii_strncasecmp("pgp-", micalg, 4) ||
			mb_sig->type != TYPEAPPLICATION ||
			g_ascii_strcasecmp("pgp-signature", mb_sig->subtype))
			result |= LIBBALSA_PROTECT_ERROR;
		} else if (!g_ascii_strcasecmp("application/pkcs7-signature", protocol)) {
		    result |= LIBBALSA_PROTECT_SMIMEV3;
		    if (!micalg || mb_sig->type != TYPEAPPLICATION ||
			g_ascii_strcasecmp("pkcs7-signature", mb_sig->subtype))
			result |= LIBBALSA_PROTECT_ERROR;
		} else
		    result |= LIBBALSA_PROTECT_ERROR;
	    } else
		result |= LIBBALSA_PROTECT_ERROR;
	} else if (!g_ascii_strcasecmp("encrypted", mb_body->subtype)) {
	    result = LIBBALSA_PROTECT_ENCRYPT | LIBBALSA_PROTECT_RFC3156;
	    if (protocol && 
		!g_ascii_strcasecmp("application/pgp-encrypted", protocol) &&
		body->parts && body->parts->mutt_body &&
		body->parts->next && body->parts->next->mutt_body &&
		!body->parts->next->next) {
		BODY *mb_enc1, *mb_enc2;

		mb_enc1 = body->parts->mutt_body;
		mb_enc2 = body->parts->next->mutt_body;
		if (mb_enc1->type != TYPEAPPLICATION ||
		    g_ascii_strcasecmp("pgp-encrypted", mb_enc1->subtype) ||
		    mb_enc2->type != TYPEAPPLICATION ||
		    g_ascii_strcasecmp("octet-stream", mb_enc2->subtype))
		    result |= LIBBALSA_PROTECT_ERROR;
	    } else
		result |= LIBBALSA_PROTECT_ERROR;
	}
    } else if (mb_body->type == TYPEAPPLICATION &&
	       !g_ascii_strcasecmp("pkcs7-mime", mb_body->subtype)) {
	const gchar *smimetype;

	result |= LIBBALSA_PROTECT_SMIMEV3;
	smimetype = mutt_get_parameter("smime-type", mb_body->parameter);
	if (!g_ascii_strcasecmp("enveloped-data", smimetype))
	    result |= LIBBALSA_PROTECT_ENCRYPT;
	else if (!g_ascii_strcasecmp("signed-data", smimetype))
	    result |= LIBBALSA_PROTECT_SIGN;
	else
	    result |= LIBBALSA_PROTECT_ERROR;
    }

    libbalsa_unlock_mutt();
    return result;
}


gboolean
libbalsa_body_check_signature(LibBalsaMessageBody* body)
{
    gpgme_ctx_t ctx;
    gpgme_error_t err;
    gpgme_data_t sig, plain;
    MailDataMBox plainStream;
    FILE *sigStream;
    LibBalsaSignatureInfo *sig_status;
    BODY *msg_body, *sign_body;
    LibBalsaMessage *msg;
    struct gpgme_data_cbs cbs = 
	{ (gpgme_data_read_cb_t)read_cb_MailDataMBox, /* read method */
	  NULL,                                       /* write method */
	  NULL,                                       /* seek method */
	  cb_data_release };                          /* release method */

    g_return_val_if_fail(body, FALSE);
    g_return_val_if_fail(body->next, FALSE);
    g_return_val_if_fail(body->message, FALSE);
    msg = body->message;
    g_return_val_if_fail(!CLIENT_CONTEXT_CLOSED(msg->mailbox), FALSE);

    /* the 2nd part for RFC 3156 MUST be application/pgp-signature */
    if (!body->next || !body->next->mutt_body ||
	body->next->mutt_body->type != TYPEAPPLICATION ||
	g_ascii_strcasecmp(body->next->mutt_body->subtype, "pgp-signature"))
	return FALSE;

    libbalsa_signature_info_destroy(body->next->sig_info);
    body->next->sig_info = sig_status = g_new0(LibBalsaSignatureInfo, 1);
    sig_status->status = GPG_ERR_GENERAL;
    

    /* try to create a gpgme context */
    if ((err = gpgme_new(&ctx)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create context: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	return FALSE;
    }
    if (!gpgme_check_set_protocol(ctx, GPGME_PROTOCOL_OpenPGP)) {
	gpgme_release(ctx);
	sig_status->status = GPG_ERR_INV_ENGINE;
	return FALSE;
    }
	
    
    /* create the body data stream */
    if (body->decrypt_file)
	plainStream.mailboxFile = safe_fopen(body->decrypt_file, "r");
    else
	plainStream.mailboxFile = 
	    libbalsa_mailbox_get_message_stream(msg->mailbox, msg);
    msg_body = body->mutt_body;
    fseek(plainStream.mailboxFile, msg_body->hdr_offset, 0);
    plainStream.bytes_left = msg_body->offset - msg_body->hdr_offset +
	msg_body->length;
    plainStream.last_was_cr = FALSE;
    if ((err = gpgme_data_new_from_cbs(&plain, &cbs, &plainStream)) !=
	GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data from mailbox file: %s"), 
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_release(ctx);
	fclose(plainStream.mailboxFile);
	return FALSE;
    }

    /* create the signature data stream */
    if (body->next->decrypt_file)
	sigStream = safe_fopen(body->next->decrypt_file, "r");
    else
	sigStream = 
	    libbalsa_mailbox_get_message_stream(msg->mailbox, msg);
    sign_body = body->next->mutt_body;
    if ((err = gpgme_data_new_from_filepart(&sig, NULL, sigStream,
					    sign_body->offset,
					    sign_body->length)) != 
	GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data from mailbox file: %s"), 
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_data_release(plain);
	gpgme_release(ctx);
	fclose(plainStream.mailboxFile);
	fclose(sigStream);
	return FALSE;
    }

    /* verify the signature */
    if ((err = gpgme_op_verify(ctx, sig, plain, NULL))
	!= GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: signature verification failed: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	sig_status->status = err;
    } else
	get_sig_info_from_ctx(sig_status, ctx);

    gpgme_data_release(sig);
    gpgme_data_release(plain);
    gpgme_release(ctx);
    fclose(plainStream.mailboxFile);
    fclose(sigStream);

    return TRUE;
}


/*
 * Signs for rfc822_for and returns the mic algorithm in micalg. If successful,
 * returns in sign_body the following chain
 *
 *         |
 *         +---- original sign_body (sign_body->next == NULL)
 *         +---- application/pgp-signature
 *
 * or
 *
 *         |
 *         +---- multipart/mixed
 *         |          +---- original sign_body chain (sign_body->next != NULL)
 *         +---- application/pgp-signature
 */
gboolean
libbalsa_sign_mutt_body(MuttBody **sign_body, const gchar *rfc822_for,
			gchar **micalg, GtkWindow *parent)
{
    MailDataMBox mailData;
    gchar fname[PATH_MAX];
    BODY *result, *sigbdy;
    gchar *signature;
    gboolean sign_multipart;

    /* paranoia checks */
    g_return_val_if_fail(rfc822_for != NULL, FALSE);
    g_return_val_if_fail(sign_body != NULL, FALSE);
    g_return_val_if_fail(micalg != NULL, FALSE);

    /* save the header and body(s) to sign in a file */
    mutt_mktemp(fname);
    mailData.mailboxFile = safe_fopen(fname, "w+");

    libbalsa_lock_mutt();
    sign_multipart = *sign_body && (*sign_body)->next;
    if (sign_multipart)
	/* create a new multipart... */
	result = mutt_make_multipart(*sign_body, NULL);
    else
	result = *sign_body;

    mutt_write_mime_header(result, mailData.mailboxFile);
    fputc('\n', mailData.mailboxFile);
    mutt_write_mime_body(result, mailData.mailboxFile);
    fflush(mailData.mailboxFile);
    rewind(mailData.mailboxFile);
    mailData.bytes_left = INT_MAX;   /* process complete file */
    mailData.last_was_cr = FALSE;
    libbalsa_unlock_mutt();

    /* call gpgme to create the signature */
    signature = gpgme_signature(&mailData, rfc822_for, micalg, parent);
    fclose(mailData.mailboxFile);
    unlink(fname);
    if (!signature) {
	/* destroy result if sign_body was a new multipart */
	if (sign_multipart) {
	    result->parts = NULL;
	    mutt_free_body(&result);
	}
	if (*micalg)
	    g_free(*micalg);
	*micalg = NULL;
	return FALSE;
    }

    /* build the new mutt BODY chain */
    libbalsa_lock_mutt();

    /* append the signature BODY */
    sigbdy = mutt_new_body();
    sigbdy->type = TYPEAPPLICATION;
    sigbdy->subtype = g_strdup("pgp-signature");
    sigbdy->unlink = 1;
    sigbdy->use_disp = 0;
    sigbdy->disposition = DISPINLINE;
    sigbdy->encoding = ENC7BIT;
    sigbdy->filename = signature;

    result->next = sigbdy;
    libbalsa_unlock_mutt();

    *sign_body = result;

    return TRUE;
}


gboolean
libbalsa_sign_encrypt_mutt_body(MuttBody **se_body, const gchar *rfc822_signer,
				GList *rfc822_for, GtkWindow *parent)
{
    BODY *sigbdy;
    gboolean sign_multipart;
    gchar *micalg;

    /* paranoia checks */
    g_return_val_if_fail(rfc822_signer != NULL, FALSE);
    g_return_val_if_fail(rfc822_for != NULL, FALSE);
    g_return_val_if_fail(se_body != NULL, FALSE);

    sign_multipart = *se_body && (*se_body)->next;

    /* according to rfc3156, we try to sign first... */
    if (!libbalsa_sign_mutt_body(se_body, rfc822_signer, &micalg, parent))
	return FALSE;

    /* the signed stuff is put into a multipart/signed container */
    sigbdy = mutt_make_multipart(*se_body, "signed");
    mutt_set_parameter("micalg", micalg, &sigbdy->parameter);
    g_free(micalg);
    mutt_set_parameter("protocol", "application/pgp-signature",
		       &sigbdy->parameter);

    /* encrypt the multipart/signed stuff */
    if (!libbalsa_encrypt_mutt_body(&sigbdy, rfc822_for)) {
	/* destroy the stuff left over from signing */
	if (sign_multipart) {
	    *se_body = sigbdy->parts->parts;
	    sigbdy->parts->parts = NULL;
	} else {
	    *se_body = sigbdy->parts;
	    sigbdy->parts = NULL;
	    mutt_free_body(&(*se_body)->next);
	}
	mutt_free_body(&sigbdy);
	return FALSE;
    }
    *se_body = sigbdy;

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
    LibBalsaMessage *message;
    gpgme_ctx_t ctx;
    gpgme_error_t err;
    gpgme_data_t cipher, plain;
    PassphraseCB cb_data;
    BODY *cipher_body, *result;
    gchar fname[PATH_MAX];
    FILE *tempfp, *src;
    gchar *plainData;
    size_t plainSize;

    /* paranoia checks */
    g_return_val_if_fail(body != NULL, NULL);
    g_return_val_if_fail(body->message != NULL, NULL);
    message = body->message;

    /* get the encrypted message stream */
    if (body->decrypt_file)
	src = safe_fopen(body->decrypt_file, "r");
    else
	src = libbalsa_mailbox_get_message_stream(message->mailbox, message);
    
    /* try to create a gpgme context */
    if ((err = gpgme_new(&ctx)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create context: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	return body;
    }
    if (!gpgme_check_set_protocol(ctx, GPGME_PROTOCOL_OpenPGP)) {
	gpgme_release(ctx);
	return body;
    }
    cb_data.ctx = ctx;
    cb_data.parent = parent;
    cb_data.title = _("Enter passphrase to decrypt message");
    gpgme_set_passphrase_cb(ctx, get_passphrase_cb, &cb_data);
    
    /* create the cipher data stream */
    cipher_body = body->next->mutt_body;
    if ((err = gpgme_data_new_from_filepart(&cipher, NULL, src, 
					    cipher_body->offset,
					    cipher_body->length)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data from mailbox file: %s"), 
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_release(ctx);
	fclose(src);
	return body;
    }

    /* create the plain data stream */
    if ((err = gpgme_data_new(&plain)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create new data object: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_data_release(cipher);
	gpgme_release(ctx);
	fclose(src);
	return body;
    }

    /* try to decrypt */
    if ((err = gpgme_op_decrypt(ctx, cipher, plain)) != GPG_ERR_NO_ERROR) {
	if (err != GPG_ERR_CANCELED)
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("%s: decryption failed: %s"),
				 gpgme_strsource(err), gpgme_strerror(err));
	gpgme_data_release(plain);
	gpgme_data_release(cipher);
	gpgme_release(ctx);
	fclose(src);
	return body;
    }
    fclose(src);
    gpgme_data_release(cipher);

    /* save the decrypted data to a file */
    if (!(plainData = gpgme_data_release_and_get_mem(plain, &plainSize))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get decrypted data: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_release(ctx);
	return body;
    }
	
    mutt_mktemp(fname);
    tempfp = safe_fopen(fname, "w+");
    fwrite(plainData, 1, plainSize, tempfp);
    rewind(tempfp);
	
    result = mutt_read_mime_header(tempfp, 0);
    result->length = plainSize - result->offset;
    mutt_parse_part(tempfp, result);

    fclose(tempfp);

    g_free(plainData);
    gpgme_release(ctx);

    /* release the current body chain and create a new one */
    libbalsa_message_body_free(body);
    body = libbalsa_message_body_new(message);
    libbalsa_message_body_set_mutt_body(body, result);
    set_decrypt_file(body, fname);
    
    return body;
}


/*
 * Encrypts for rfc822_for. If successful, returns in encrypt_body the following
 * chain
 *
 *         |
 *         +---- application/pgp-encrypted
 *         +---- application/octet-stream, containing the original encrypt_body
 *               (encrypt_body->next == NULL) or the encrypt_body chain put
 *               into a multipart/mixed (encrypt_body->next != NULL)
 */
gboolean
libbalsa_encrypt_mutt_body(MuttBody **encrypt_body, GList *rfc822_for)
{
    MailDataMBox mailData;
    gchar fname[PATH_MAX];
    FILE *tempfp;
    BODY *result, *encbdy;
    gchar *encfile;
    gboolean encr_multipart;

    /* paranoia checks */
    g_return_val_if_fail(rfc822_for != NULL, FALSE);
    g_return_val_if_fail(encrypt_body != NULL, FALSE);

    /* save the header and body(s) to encrypt in a file */
    mutt_mktemp(fname);
    mailData.mailboxFile = safe_fopen(fname, "w+");

    libbalsa_lock_mutt();
    encr_multipart = *encrypt_body && (*encrypt_body)->next;
    if (encr_multipart)
	/* create a new multipart... */
	encbdy = mutt_make_multipart(*encrypt_body, NULL);
    else
	encbdy = *encrypt_body;

    mutt_write_mime_header(encbdy, mailData.mailboxFile);
    fputc('\n', mailData.mailboxFile);
    mutt_write_mime_body(encbdy, mailData.mailboxFile);
    fflush(mailData.mailboxFile);
    rewind(mailData.mailboxFile);
    mailData.bytes_left = INT_MAX;   /* process complete file */
    mailData.last_was_cr = FALSE;
    libbalsa_unlock_mutt();

    /* call gpgme to encrypt */
    encfile = gpgme_encrypt_file(&mailData, rfc822_for);
    fclose(mailData.mailboxFile);
    unlink(fname);
    if (!encfile) {
	/* destroy encbdy if encr_body was a new multipart */
	if (encr_multipart) {
	    encbdy->parts = NULL;
	    mutt_free_body(&encbdy);
	}
	return FALSE;
    }

    /* destroy the original (unencrypted) body */
    libbalsa_lock_mutt();
    mutt_free_body(&encbdy);

    /* build the new mutt BODY chain - part 1 */
    result = mutt_new_body();
    result->type = TYPEAPPLICATION;
    result->subtype = g_strdup("pgp-encrypted");
    result->unlink = 1;
    result->use_disp = 0;
    result->disposition = DISPINLINE;
    result->encoding = ENC7BIT;

    mutt_mktemp(fname);
    result->filename = g_strdup(fname);
    tempfp = safe_fopen(result->filename, "w+");
    fputs("Version: 1\n", tempfp);
    fclose(tempfp);
    
    /* part 2 */
    result->next = encbdy = mutt_new_body();
    encbdy->type = TYPEAPPLICATION;
    encbdy->subtype = g_strdup("octet-stream");
    encbdy->unlink = 1;
    encbdy->use_disp = 0;
    encbdy->disposition = DISPINLINE;
    encbdy->encoding = ENC7BIT;
    encbdy->filename = encfile;

    libbalsa_unlock_mutt();

    *encrypt_body = result;

    return TRUE;
}


const gchar *
libbalsa_gpgme_sig_stat_to_gchar(gpgme_error_t stat)
{
    switch (stat)
	{
	case GPG_ERR_NO_ERROR:
	    return _("The signature is valid.");
	case GPG_ERR_SIG_EXPIRED:
	    return _("The signature is valid but expired.");
	case GPG_ERR_KEY_EXPIRED:
	    return _("The signature is valid but the key used to verify the signature has expired.");
	case GPG_ERR_BAD_SIGNATURE:
	    return _("The signature is invalid.");
	case GPG_ERR_NO_PUBKEY:
	    return _("The signature could not be verified due to a missing key.");
	case GPG_ERR_NO_DATA:
	    return _("This part is not a real PGP signature.");
	case GPG_ERR_INV_ENGINE:
	    return _("The signature could not be verified due to an invalid crypto engine");
	default:
	    return _("An error prevented the signature verification.");
	}
}

const gchar *
libbalsa_gpgme_validity_to_gchar(gpgme_validity_t validity)
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
gint
libbalsa_rfc2440_check_buffer(const gchar *buffer)
{
    g_return_val_if_fail(buffer != NULL, 0);

    if (!strncmp(buffer, "-----BEGIN PGP MESSAGE-----", 27) &&
	strstr(buffer, "-----END PGP MESSAGE-----"))
	return LIBBALSA_PROTECT_OPENPGP | LIBBALSA_PROTECT_ENCRYPT;
    else if (!strncmp(buffer, "-----BEGIN PGP SIGNED MESSAGE-----", 34) &&
	     strstr(buffer, "-----BEGIN PGP SIGNATURE-----") &&
	     strstr(buffer, "-----END PGP SIGNATURE-----"))
	return LIBBALSA_PROTECT_OPENPGP | LIBBALSA_PROTECT_SIGN;
    else
	return 0;    
}


/*
 * sign the data in buffer for sender *rfc822_for and return the signed and
 * properly escaped new buffer on success or NULL on error
 */
gchar *
libbalsa_rfc2440_sign_buffer(const gchar *buffer, const gchar *sign_for,
			     GtkWindow *parent)
{
    gpgme_ctx_t ctx;
    gpgme_error_t err;
    gpgme_data_t in, out;
    MailDataBuffer inbuf;
    PassphraseCB cb_data;
    gchar *signed_buffer, *result, *inp, *outp;
    size_t datasize;
    struct gpgme_data_cbs cbs = 
	{ (gpgme_data_read_cb_t)read_cb_MailDataBuffer, /* read method */
	  NULL,                                         /* write method */
	  NULL,                                         /* seek method */
	  cb_data_release };                            /* release method */

    /* paranoia checks */
    g_return_val_if_fail(sign_for != NULL, NULL);
    g_return_val_if_fail(buffer != NULL, NULL);

    /* try to create a gpgme context */
    if ((err = gpgme_new(&ctx)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create context: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	return NULL;
    }
    if (!gpgme_check_set_protocol(ctx, GPGME_PROTOCOL_OpenPGP)) {
	gpgme_release(ctx);
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
    if ((err = gpgme_data_new_from_cbs(&in, &cbs, &inbuf)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data from buffer: %s"), 
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_release(ctx);
	return NULL;
    }
    if ((err = gpgme_data_new(&out)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create new data object: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_data_release(in);
	gpgme_release(ctx);
	return NULL;
    }
    if ((err = gpgme_op_sign(ctx, in, out, GPGME_SIG_MODE_CLEAR)) != 
	GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: signing failed: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_data_release(out);
	gpgme_data_release(in);
	gpgme_release(ctx);
	return NULL;
    }

    /* get the result */
    gpgme_data_release(in);
    if (!(signed_buffer = gpgme_data_release_and_get_mem(out, &datasize))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
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
 * touched and the routine returns GPG_ERR_GENERAL. If charset is not
 * NULL and not `utf-8', the routine converts *buffer from utf-8 to charset
 * before checking and back afterwards.
 */
gpgme_error_t
libbalsa_rfc2440_check_signature(gchar **buffer, const gchar *charset,
				 gboolean append_info,
				 LibBalsaSignatureInfo **sig_info,
				 const gchar *date_string)
{
    gpgme_ctx_t ctx;
    gpgme_error_t err, retval;
    gpgme_data_t in, out;
    gchar *checkbuf, *plain_buffer, *info, *result;
    size_t datasize;
    LibBalsaSignatureInfo *tmp_siginfo;

    /* paranoia checks */
    g_return_val_if_fail(buffer != NULL, GPG_ERR_GENERAL);
    g_return_val_if_fail(*buffer != NULL, GPG_ERR_GENERAL);

    /* try to create a gpgme context */
    if ((err = gpgme_new(&ctx)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create context: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	return GPG_ERR_GENERAL;
    }
    if (!gpgme_check_set_protocol(ctx, GPGME_PROTOCOL_OpenPGP)) {
	gpgme_release(ctx);
	return GPG_ERR_INV_ENGINE;
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
	GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data from buffer: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	g_free(checkbuf);
	gpgme_release(ctx);
	return GPG_ERR_GENERAL;
    }

    /* verify the signature */
    if ((err = gpgme_data_new(&out)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create new data object: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	g_free(checkbuf);
	gpgme_data_release(in);
	gpgme_release(ctx);
	return GPG_ERR_GENERAL;
    }
    if ((err = gpgme_op_verify(ctx, in, NULL, out))
	!= GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: signature verification failed: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_data_release(out);
	gpgme_data_release(in);
	g_free(checkbuf);
	gpgme_release(ctx);
	return GPG_ERR_GENERAL;
    }
    gpgme_data_release(in);
    g_free(checkbuf);

    /* get some information about the signature if requested... */
    info = NULL;
    tmp_siginfo = g_new0(LibBalsaSignatureInfo, 1);
	
    get_sig_info_from_ctx(tmp_siginfo, ctx);
    retval = tmp_siginfo->status;
    if (append_info)
	info = libbalsa_signature_info_to_gchar(tmp_siginfo, date_string);
    if (sig_info) {
	libbalsa_signature_info_destroy(*sig_info);
	*sig_info = tmp_siginfo;
    } else
	libbalsa_signature_info_destroy(tmp_siginfo);

    if (!(plain_buffer = gpgme_data_release_and_get_mem(out, &datasize))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	g_free(info);
        gpgme_release(ctx);
        return GPG_ERR_GENERAL;
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
  
    return retval;
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
    gpgme_ctx_t ctx;
    gpgme_error_t err;
    gpgme_key_t *rcpt;
    gpgme_data_t in, out;
    PassphraseCB cb_data;
    gchar *databuf, *result;
    size_t datasize;

    /* paranoia checks */
    g_return_val_if_fail(buffer != NULL, NULL);
    g_return_val_if_fail(encrypt_for != NULL, NULL);

    /* try to create a gpgme context */
    if ((err = gpgme_new(&ctx)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create context: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
        return NULL;
    }
    if (!gpgme_check_set_protocol(ctx, GPGME_PROTOCOL_OpenPGP)) {
	gpgme_release(ctx);
	return NULL;
    }

    /* build the list of recipients */
    if (!(rcpt = gpgme_build_recipients(ctx, encrypt_for))) {
        gpgme_release(ctx);
        return NULL;
    }

    /* add the signer and a passphrase callback if necessary */
    if (sign_for) {
	if (!gpgme_add_signer(ctx, sign_for)) {
	    release_keylist(rcpt);
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
	!= GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data from buffer: %s"), 
			     gpgme_strsource(err), gpgme_strerror(err));
        release_keylist(rcpt);
	gpgme_release(ctx);
	return NULL;
    }
    if ((err = gpgme_data_new(&out)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create new data object: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
        gpgme_data_release(in);
        release_keylist(rcpt);
        gpgme_release(ctx);
        return NULL;
    }    
    if (sign_for)
	err = gpgme_op_encrypt_sign(ctx, rcpt, GPGME_ENCRYPT_ALWAYS_TRUST, in, out);
    else
	err = gpgme_op_encrypt(ctx, rcpt, GPGME_ENCRYPT_ALWAYS_TRUST, in, out);
    if (err != GPG_ERR_NO_ERROR) {
	if (sign_for)
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("%s: signing and encryption failed: %s"),
				 gpgme_strsource(err), gpgme_strerror(err));
	else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("%s: encryption failed: %s"),
				 gpgme_strsource(err), gpgme_strerror(err));
        gpgme_data_release(out);
        gpgme_data_release(in);
        release_keylist(rcpt);
        gpgme_release(ctx);
        return NULL;
    }

    /* get the result */
    gpgme_data_release(in);
    release_keylist(rcpt);
    if (!(databuf = gpgme_data_release_and_get_mem(out, &datasize))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
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
 * is not touched and the routine returns GPG_ERR_GENERAL.
 */
gpgme_error_t
libbalsa_rfc2440_decrypt_buffer(gchar **buffer, const gchar *charset,
				gboolean fallback, LibBalsaCodeset codeset,
				gboolean append_info, 
				LibBalsaSignatureInfo **sig_info,
				const gchar *date_string, GtkWindow *parent)
{
    gpgme_ctx_t ctx;
    gpgme_error_t err, retval;
    gpgme_data_t in, out;
    PassphraseCB cb_data;
    gchar *plain_buffer, *info, *result;
    size_t datasize;
    LibBalsaSignatureInfo *tmp_siginfo;

    /* paranoia checks */
    g_return_val_if_fail(buffer != NULL, GPG_ERR_GENERAL);
    g_return_val_if_fail(*buffer != NULL, GPG_ERR_GENERAL);

    /* try to create a gpgme context */
    if ((err = gpgme_new(&ctx)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create context: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	return GPG_ERR_GENERAL;
    }
    if (!gpgme_check_set_protocol(ctx, GPGME_PROTOCOL_OpenPGP)) {
	gpgme_release(ctx);
	return GPG_ERR_INV_ENGINE;
    }
    
    /* create the body data stream */
    if ((err = gpgme_data_new_from_mem(&in, *buffer, strlen(*buffer), 0)) !=
	GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data from buffer: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_release(ctx);
	return GPG_ERR_GENERAL;
    }

    /* decrypt and (if possible) verify the signature */
    if ((err = gpgme_data_new(&out)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create new data object: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_data_release(in);
	gpgme_release(ctx);
	return GPG_ERR_GENERAL;
    }
    cb_data.ctx = ctx;
    cb_data.title = _("Enter passsphrase to decrypt message part");
    cb_data.parent = parent;
    gpgme_set_passphrase_cb(ctx, get_passphrase_cb, &cb_data);
    if ((err = gpgme_op_decrypt_verify(ctx, in, out))
	!= GPG_ERR_NO_ERROR) {
	if (err != GPG_ERR_CANCELED)
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("%s: decryption and signature verification failed: %s"),
				 gpgme_strsource(err), gpgme_strerror(err));
	gpgme_data_release(out);
	gpgme_data_release(in);
	gpgme_release(ctx);
	return err;
    }
    gpgme_data_release(in);

    /* get some information about the signature if requested... */
    info = NULL;
    tmp_siginfo = g_new0(LibBalsaSignatureInfo, 1);
	
    get_sig_info_from_ctx(tmp_siginfo, ctx);
    retval = tmp_siginfo->status;
    if (tmp_siginfo->status == GPG_ERR_USER_16) {
	libbalsa_signature_info_destroy(tmp_siginfo);
	tmp_siginfo = NULL;
    }
    if (tmp_siginfo && append_info)
	info = libbalsa_signature_info_to_gchar(tmp_siginfo, date_string);
    if (sig_info) {
	libbalsa_signature_info_destroy(*sig_info);
	*sig_info = tmp_siginfo;
    } else
	libbalsa_signature_info_destroy(tmp_siginfo);

    if (!(plain_buffer = gpgme_data_release_and_get_mem(out, &datasize))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	g_free(info);
        gpgme_release(ctx);
        return GPG_ERR_GENERAL;
    }
    gpgme_release(ctx);

    /* convert the result back to utf-8 if necessary */
    /* FIXME: remove \r's if the message came from a winbloze mua? */
    if (charset && g_ascii_strcasecmp(charset, "utf-8")) {
	if (!g_ascii_strcasecmp(charset, "us-ascii")) {
	    gchar const *target = NULL;
	    result = g_strndup(plain_buffer, datasize);

	    if (!libbalsa_utf8_sanitize(&result, fallback, codeset, &target))
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
  
    return retval;
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
gpg_run_import_key(const gchar *fingerprint, GtkWindow *parent)
{
    gchar **argv;
    spawned_gpg_T *spawned_gpg;
    gboolean spawnres;

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
    g_timeout_add(250, check_gpg_child, spawned_gpg);

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


/* 
 * helper: check & remember if protocol proto is available. If it's available,
 * set it and return TRUE, and FALSE otherwise
 */
static gboolean
gpgme_check_set_protocol(gpgme_ctx_t ctx, gpgme_protocol_t proto)
{
    static gint has_proto_openpgp = -1;
    static gint has_proto_cms = -1;

    switch (proto) {
    case GPGME_PROTOCOL_OpenPGP:
	if (has_proto_openpgp < 0)
	    has_proto_openpgp = 
		(gpgme_engine_check_version(proto) == GPG_ERR_NO_ERROR) ? TRUE : FALSE;
	if (has_proto_openpgp)
	    gpgme_set_protocol(ctx, proto);
	return has_proto_openpgp;
    case GPGME_PROTOCOL_CMS:
	if (has_proto_cms < 0)
	    has_proto_cms = 
		(gpgme_engine_check_version(proto) == GPG_ERR_NO_ERROR) ? TRUE : FALSE;
	if (has_proto_cms)
	    gpgme_set_protocol(ctx, proto);
	return has_proto_cms;
    default:
	return FALSE;
    }
}


static const gchar *
libbalsa_gpgme_validity_to_gchar_short(gpgme_validity_t validity)
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
get_sig_info_from_ctx(LibBalsaSignatureInfo* info, gpgme_ctx_t ctx)
{
    gpgme_verify_result_t result;
    gpgme_key_t key;
    gpgme_user_id_t uid;
	
    info->status = GPG_ERR_USER_16; /* no signature available */

    if (!(result = gpgme_op_verify_result(ctx)) || result->signatures == NULL)
	return;
    info->fingerprint = g_strdup(result->signatures->fpr);
    info->sign_time = result->signatures->timestamp;
    info->status = gpgme_err_code(result->signatures->status);

    gpgme_get_key(ctx, info->fingerprint, &key, 0);
    if (key == NULL)
	return;
    info->protocol = key->protocol;
    info->issuer_serial = g_strdup(key->issuer_serial);
    info->issuer_name = g_strdup(key->issuer_name);
    info->chain_id = g_strdup(key->chain_id);
    info->trust = key->owner_trust;
    uid = key->uids;
    if (uid)
	info->validity = uid->validity;
    while (uid) {
	if (!info->sign_name && uid->name && strlen(uid->name))
	    info->sign_name = g_strdup(uid->name);
	if (!info->sign_email && uid->email && strlen(uid->email))
	    info->sign_email = g_strdup(uid->email);
	if (!info->sign_email && uid->email && strlen(uid->email))
	    info->sign_email = g_strdup(uid->email);
	if (!info->sign_uid && uid->uid && strlen(uid->uid))
	    info->sign_uid = g_strdup(uid->uid);
	uid = uid->next;
    }
    if (key->subkeys)
	info->key_created = key->subkeys->timestamp;
    gpgme_key_unref(key);
}


/* stuff to get a key fingerprint from a selection list */
enum {
    GPG_KEY_USER_ID_COLUMN = 0,
    GPG_KEY_ID_COLUMN,
    GPG_KEY_VALIDITY_COLUMN,
    GPG_KEY_TRUST_COLUMN,
    GPG_KEY_LENGTH_COLUMN,
    GPG_KEY_PTR_COLUMN,
    GPG_KEY_NUM_COLUMNS
};

static gchar *col_titles[] =
    { N_("User ID"), N_("Key ID"), N_("Validity"), N_("Owner trust"),
      N_("Length") };


/* callback function if a new row is selected in the list */
static void
key_selection_changed_cb (GtkTreeSelection *selection, gpgme_key_t *key)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    
    if (gtk_tree_selection_get_selected (selection, &model, &iter))
	gtk_tree_model_get (model, &iter, GPG_KEY_PTR_COLUMN, key, -1);
}


/*
 * Select a key for the mail address for_address from the gpgme_key_t's in keys
 * and return either the selected key or NULL if the dialog was cancelled.
 * secret_only controls the dialog message.
 */
static gpgme_key_t
select_key_from_list(int secret_only, const gchar *for_address, GList *keys,
		     GtkWindow *parent)
{
    GtkWidget *dialog;
    GtkWidget *vbox;
    GtkWidget *label;
    GtkWidget *scrolled_window;
    GtkWidget *tree_view;
    GtkTreeStore *model;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    gint i;
    gchar *prompt;
    gpgme_key_t use_key = NULL;

    /* FIXME: create dialog according to the Gnome HIG */
    dialog = gtk_dialog_new_with_buttons(_("Select key"),
					 parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_STOCK_OK, GTK_RESPONSE_OK,
					 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					 NULL);
    vbox = gtk_vbox_new(FALSE, 12);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), vbox);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    if (secret_only)
	prompt = g_strdup_printf(_("Select the private key for the signer %s"),
				 for_address);
    else
	prompt = g_strdup_printf(_("Select the public key for the recipient %s"),
				 for_address);
    label = gtk_label_new(prompt);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    g_free(prompt);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);

    scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
					 GTK_SHADOW_ETCHED_IN);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    model = gtk_tree_store_new (GPG_KEY_NUM_COLUMNS, G_TYPE_STRING,
				G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				G_TYPE_INT, G_TYPE_POINTER);

    tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
    g_signal_connect (G_OBJECT (selection), "changed",
		      G_CALLBACK (key_selection_changed_cb),
		      &use_key);

    /* add the keys */
    while (keys) {
	gpgme_key_t key = (gpgme_key_t)keys->data;

	if (key->subkeys) {
	    gtk_tree_store_append (GTK_TREE_STORE (model), &iter, NULL);
	    gtk_tree_store_set (GTK_TREE_STORE (model), &iter,
				GPG_KEY_USER_ID_COLUMN,
				key->uids && key->uids->uid ? key->uids->uid : "",
				GPG_KEY_ID_COLUMN,
				key->subkeys->keyid,
				GPG_KEY_VALIDITY_COLUMN, 
				libbalsa_gpgme_validity_to_gchar_short(key->uids->validity),
				GPG_KEY_TRUST_COLUMN, 
				libbalsa_gpgme_validity_to_gchar_short(key->owner_trust),
				GPG_KEY_LENGTH_COLUMN,
				key->subkeys->length,
				GPG_KEY_PTR_COLUMN,
				key,
				-1);
	}
	keys = g_list_next(keys);
    }
  
    g_object_unref (G_OBJECT (model));

    for (i = 0; i < GPG_KEY_PTR_COLUMN; i++) {
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

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK)
	use_key = NULL;
    gtk_widget_destroy(dialog);

    return use_key;
}


/*
 * Get a key for name. If necessary, a dialog is shown to select
 * a key from a list. Return NULL if something failed.
 */
#define KEY_IS_OK(k)   (!((k)->expired || (k)->revoked || \
                          (k)->disabled || (k)->invalid))
static gpgme_key_t
get_key_from_name(gpgme_ctx_t ctx, const gchar *name, int secret_only,
		  GtkWindow *parent)
{
    GList *keys = NULL;
    gpgme_key_t key;
    gpgme_error_t err;

    if ((err = gpgme_op_keylist_start(ctx, name, secret_only)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not list keys for %s: %s"),
			     gpgme_strsource(err), name, gpgme_strerror(err));
	return NULL;
    }
    while ((err = gpgme_op_keylist_next(ctx, &key)) == GPG_ERR_NO_ERROR)
	if (KEY_IS_OK(key) && key->subkeys && KEY_IS_OK(key->subkeys))
	    keys = g_list_append(keys, key);
    if (gpg_err_code(err) != GPG_ERR_EOF) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not retrieve key for %s: %s"),
			     gpgme_strsource(err), name, gpgme_strerror(err));
	gpgme_op_keylist_end(ctx);
	g_list_foreach(keys, (GFunc)gpgme_key_unref, NULL);
	g_list_free(keys);
	return NULL;
    }
    gpgme_op_keylist_end(ctx);
    
    if (!keys) 
	return NULL;

    if (g_list_length (keys) > 1) {
	key = select_key_from_list(secret_only, name, keys, parent);
	if (key)
	    gpgme_key_ref(key);
	g_list_foreach(keys, (GFunc)gpgme_key_unref, NULL);
    } else
	key = (gpgme_key_t)keys->data;

    g_list_free(keys);
    return key;
} 


/*
 * Add signer to ctx's list of signers and return TRUE on success or FALSE
 * on error.
 */
static gboolean
gpgme_add_signer(gpgme_ctx_t ctx, const gchar *signer)
{
    gpgme_key_t key = NULL;
    
    if (!(key = get_key_from_name(ctx, signer, 1, NULL))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not get a private key for %s"), signer);
	return FALSE;
    }

    /* set the key (the previous operation guaranteed that it exists...) */
    gpgme_signers_add(ctx, key);
    gpgme_key_unref(key);

    return TRUE;
}


/*
 * Build a NULL-terminated array of keys for all recipients in rcpt_list and
 * return it. The caller has to take care that it's released. If something
 * goes wrong, NULL is returned.
 */
static gpgme_key_t *
gpgme_build_recipients(gpgme_ctx_t ctx, GList *rcpt_list)
{
    gpgme_key_t *rcpt = g_new0(gpgme_key_t, g_list_length(rcpt_list) + 1);
    gint num_rcpts = 0;

    /* try to find the public key for every recipient */
    while (rcpt_list) {
        gchar *name = (gchar *)rcpt_list->data;
	gpgme_key_t key;

        if (!(key = get_key_from_name(ctx, name, 0, NULL))) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("could not get a public key for %s"), name);
            release_keylist(rcpt);
            return NULL;
        }

        /* set the recipient */
	rcpt[num_rcpts++] = key;
	libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
			     "encrypt for %s with fpr %s", name, 
			     key->subkeys->fpr);

        rcpt_list = g_list_next(rcpt_list);
    }
    libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
			 "encrypting for %d recipient(s)", num_rcpts);
    
    return rcpt;
}


/* 
 * Called by gpgme to get a new chunk of data. We read from the file in
 * hook (which is actually a MailDataMBox *) and convert every single '\n'
 * into a '\r\n' sequence as requested by rfc3156.
 */
static ssize_t
read_cb_MailDataMBox(void *hook, char *buffer, size_t count)
{
    MailDataMBox *inFile = (MailDataMBox *)hook;
    ssize_t nread = 0;

    if (inFile->bytes_left == 0 || feof(inFile->mailboxFile))
	return 0;

    /* try to fill buffer */
    while (count > 1 && inFile->bytes_left > 0 && !feof(inFile->mailboxFile) &&
	   !ferror(inFile->mailboxFile)) {
	int c = fgetc(inFile->mailboxFile);
	inFile->bytes_left--;

	if (c != EOF) {
	    if (c == '\n' && !inFile->last_was_cr) {
		*buffer++ = '\r';
		nread += 1;
		count -= 1;
	    }
	    inFile->last_was_cr = (c == '\r');
	    *buffer++ = c;
	    nread += 1;
	    count -= 1;
	}
    }

    return ferror(inFile->mailboxFile) ? -1 : nread;
}


/* 
 * Called by gpgme to get a new chunk of data. We read from the gchar ** in
 * hook and convert every single '\n' into a '\r\n' sequence.
 */
static ssize_t
read_cb_MailDataBuffer(void *hook, char *buffer, size_t count)
{
    MailDataBuffer *inbuf = (MailDataBuffer *)hook;
    ssize_t nread = 0;

    if (*(inbuf->ptr) == '\0')
        return 0;

    /* try to fill buffer */
    while (count > 1 && *(inbuf->ptr) != '\0') {
	if (*(inbuf->ptr) == '\n' && !inbuf->last_was_cr) {
	    *buffer++ = '\r';
	    nread += 1;
	    count -= 1;
	}
	inbuf->last_was_cr = (*(inbuf->ptr) == '\r');
	*buffer++ = *(inbuf->ptr);
	inbuf->ptr += 1;
	nread += 1;
	count -= 1;
    }
    return nread;
}


#ifdef ENABLE_PCACHE
/* helper functions for the passphrase cache */
/*
 * destroy a pcache_elem_T object by first overwriting the encrypted data
 * with random crap and then freeing all allocated stuff
 */
static void
bf_destroy(pcache_elem_T * pcache_elem)
{
    bf_crypt_T * pf = pcache_elem->passphrase;
    gint n;

    fprintf(stderr, "%s for (pcache_elem_T *)%p\n", __FUNCTION__, pcache_elem);
    if (pf) {
	unsigned char *p = pf->buf;

	if (p) {
	    while (pf->len--)
		*p++ = random();
	    g_free(pf->buf);
	}
	g_free(pf);
	pcache_elem->passphrase = NULL;
    }
    for (n = 0; n < MD5_DIGEST_LENGTH; n++)
	pcache_elem->name[n] = random();
    g_free(pcache_elem);
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
	    GList *next = g_list_next(list);

	    cache->cache = g_list_remove(cache->cache, elem);
	    bf_destroy(elem);
	    list = next;
	} else
	    list = g_list_next(list);
    }
	
    return TRUE;
}


/*
 * Check if the passphrase requested for uid_hint is already in cache. If it
 * is there and gpgme complained about a bad passphrase, erase it and return
 * NULL, otherwise return the passphrase. If it is not in the cache, the
 * routine also returns NULL.
 */
static gchar *
check_cache(pcache_T *cache, const gchar *uid_hint, int prev_was_bad)
{
    unsigned char tofind[MD5_DIGEST_LENGTH];
    GList *list;

    /* exit immediately if no data is present */
    if (!cache->enable || !cache->cache)
	return NULL;

    /* try to find the passphrase in the cache */
    MD5(uid_hint, strlen(uid_hint), tofind);
    list = cache->cache;
    while (list) {
	pcache_elem_T *elem = (pcache_elem_T *)list->data;

	if (!memcmp(tofind, elem->name, MD5_DIGEST_LENGTH)) {
	    /* check if the last entry was bad */
	    if (prev_was_bad) {
		cache->cache = g_list_remove(cache->cache, elem);
		bf_destroy(elem);
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

	fprintf(stderr, "caught signal %d, destroy passphrase cache keys...\n",
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
 * destroy all cached passphrases and the cache itself (called upon exiting
 * the main gtk loop)
 */
static gint
clear_pcache(pcache_T *cache)
{
    gint n;

    if (cache) {
	fprintf(stderr, "erasing password cache at (pcache_T *)%p\n", cache);
	if (cache->cache) {
	    g_list_foreach(cache->cache, (GFunc)bf_destroy, NULL);
	    g_list_free(cache->cache);
	    cache->cache = NULL;
	}

	for (n = 0; n < 16; n++)
	    cache->bf_key.key[n] = random();
	for (n = 0; n < 8; n++)
	    cache->bf_key.iv[n] = random();
    }

    return 0;
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

	/* destroy the cache when exiting the application */
	gtk_quit_add(0, (GtkFunction)clear_pcache, cache);
	
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
get_passphrase_real(PassphraseCB *cb_data, const gchar *uid_hint,
		    int prev_was_bad, pcache_T *pcache)
#else
get_passphrase_real(PassphraseCB *cb_data, const gchar *uid_hint,
		    int prev_was_bad)
#endif
{
    static GdkPixbuf * padlock_keyhole = NULL;
    GtkWidget *dialog, *entry, *vbox, *hbox;
    gchar *prompt, *passwd;
#ifdef ENABLE_PCACHE
    GtkWidget *cache_but = NULL, *cache_min = NULL;
#endif
    
    /* FIXME: create dialog according to the Gnome HIG */
    dialog = gtk_dialog_new_with_buttons(cb_data->title,
                                         cb_data->parent,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         NULL);
    
    hbox = gtk_hbox_new(FALSE, 12);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 12);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), hbox);

    vbox = gtk_vbox_new(FALSE, 12);
    gtk_container_add(GTK_CONTAINER(hbox), vbox);
    if (!padlock_keyhole)
	padlock_keyhole = gdk_pixbuf_new_from_xpm_data(padlock_keyhole_xpm);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_image_new_from_pixbuf (padlock_keyhole), FALSE, FALSE, 0);

    vbox = gtk_vbox_new(FALSE, 12);
    gtk_container_add(GTK_CONTAINER(hbox), vbox);
    if (prev_was_bad)
	prompt = 
	    g_strdup_printf(_("The passphrase for this key was bad, please try again!\n\nKey: %s"),
			    uid_hint);
    else
	prompt = 
	    g_strdup_printf(_("Please enter the passphrase for the secret key!\n\nKey: %s"),
			    uid_hint);
    gtk_container_add(GTK_CONTAINER(vbox), gtk_label_new(prompt));
    g_free(prompt);
    entry = gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(vbox), entry);

#ifdef ENABLE_PCACHE
    if (pcache->enable) {
	hbox = gtk_hbox_new(FALSE, 12);
	gtk_container_add(GTK_CONTAINER(vbox), hbox);
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
	    pcache_elem_T *elem = g_new(pcache_elem_T, 1);

	    MD5(uid_hint, strlen(uid_hint), elem->name);
	    elem->passphrase = bf_encrypt(passwd, &pcache->bf_key);
	    elem->expires = time(NULL) + 
		60 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(cache_min));
	    pcache->cache = g_list_append(pcache->cache, elem);
	}
    }
#endif

    gtk_widget_destroy(dialog);

    return passwd;
}


#ifdef BALSA_USE_THREADS
/* FIXME: is this really necessary? */
typedef struct {
    pthread_cond_t cond;
    PassphraseCB *cb_data;
    const gchar *desc;
    gint was_bad;
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
    apd->res = get_passphrase_real(apd->cb_data, apd->desc, apd->was_bad,
				   apd->pcache);
#else
    apd->res = get_passphrase_real(apd->cb_data, apd->desc, apd->was_bad);
#endif
    gdk_threads_leave();
    pthread_cond_signal(&apd->cond);
    return FALSE;
}
#endif


/*
 * Helper function: overwrite a sting in memory with random data
 */
static inline void
wipe_string(gchar *password)
{
    while (*password)
	*password++ = random();
}


/*
 * Called by gpgme to get the passphrase for a key.
 */
static gpgme_error_t
get_passphrase_cb(void *opaque, const char *uid_hint, const char *passph_info,
		  int prev_was_bad, int fd)
{
    PassphraseCB *cb_data = (PassphraseCB *)opaque;
    gchar *passwd = NULL;

    if (cb_data == NULL || cb_data->ctx == NULL) {
	write(fd, "\n", 1);
	return GPG_ERR_USER_1;
    }
    
#ifdef ENABLE_PCACHE    
    if (!pcache)
	pcache = init_pcache();

    /* check if we have the passphrase already cached... */
    if ((passwd = check_cache(pcache, uid_hint, prev_was_bad))) {
	write(fd, passwd, strlen(passwd));
	write(fd, "\n", 1);
	wipe_string(passwd);
	g_free(passwd);
	return GPG_ERR_NO_ERROR;
    }
#endif

#ifdef BALSA_USE_THREADS
    if (pthread_self() == libbalsa_get_main_thread())
#ifdef ENABLE_PCACHE
	passwd = get_passphrase_real(cb_data, uid_hint, prev_was_bad, pcache);
#else
	passwd = get_passphrase_real(cb_data, uid_hint, prev_was_bad);
#endif
    else {
	static pthread_mutex_t get_passphrase_lock = PTHREAD_MUTEX_INITIALIZER;
	AskPassphraseData apd;

	pthread_mutex_lock(&get_passphrase_lock);
	pthread_cond_init(&apd.cond, NULL);
	apd.cb_data = cb_data;
	apd.desc = uid_hint;
	apd.was_bad = prev_was_bad;
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
    passwd = get_passphrase_real(cb_data, uid_hint, prev_was_bad, pcache);
#else
    passwd = get_passphrase_real(cb_data, uid_hint, prev_was_bad);
#endif /* ENABLE_PCACHE */
#endif /* BALSA_USE_THREADS */

    if (!passwd) {
	write(fd, "\n", 1);
	return GPG_ERR_CANCELED;
    }

    /* send the passphrase and erase the string */
    write(fd, passwd, strlen(passwd));
    wipe_string(passwd);
    g_free(passwd);
    write(fd, "\n", 1);
    return GPG_ERR_NO_ERROR;
}


/*
 * Signs the data in mailData (the file is assumed to be open and rewinded)
 * for sign_for. Return either a signature block or NULL on error. Upon
 * success, micalg is set to the used mic algorithm (must be freed).
 */
static gchar *
gpgme_signature(MailDataMBox *mailData, const gchar *sign_for, gchar **micalg,
		GtkWindow *parent)
{
    gpgme_ctx_t ctx;
    gpgme_error_t err;
    gpgme_data_t in, out;
    size_t datasize;
    gchar *signature_buffer;
    gpgme_sign_result_t sign_result;
    PassphraseCB cb_data;
    gchar fname[PATH_MAX];
    FILE *tempfp;
    struct gpgme_data_cbs cbs = 
	{ (gpgme_data_read_cb_t)read_cb_MailDataMBox, /* read method */
	  NULL,                                       /* write method */
	  NULL,                                       /* seek method */
	  cb_data_release };                          /* release method */

    *micalg = NULL;

    /* try to create a gpgme context */
    if ((err = gpgme_new(&ctx)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create context: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	return NULL;
    }
    if (!gpgme_check_set_protocol(ctx, GPGME_PROTOCOL_OpenPGP)) {
	gpgme_release(ctx);
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
    cb_data.parent = parent;
    cb_data.title = _("Enter passsphrase to sign message");
    gpgme_set_passphrase_cb(ctx, get_passphrase_cb, &cb_data);
    if ((err = gpgme_data_new_from_cbs(&in, &cbs, mailData)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data from file: %s"), 
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_release(ctx);
	return NULL;
    }
    if ((err = gpgme_data_new(&out)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create new data: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_data_release(in);
	gpgme_release(ctx);
	return NULL;
    }
    if ((err = gpgme_op_sign(ctx, in, out, GPGME_SIG_MODE_DETACH)) != 
	GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: signing failed: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_data_release(out);
	gpgme_data_release(in);
	gpgme_release(ctx);
	return NULL;
    }

    /* get the info about the used hash algorithm */
    sign_result = gpgme_op_sign_result(ctx);
    g_free(*micalg);
    *micalg = 
	g_strdup_printf("PGP-%s", gpgme_hash_algo_name(sign_result->signatures->hash_algo));

    gpgme_data_release(in);
    if (!(signature_buffer = gpgme_data_release_and_get_mem(out, &datasize))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_release(ctx);
	return NULL;
    }

    mutt_mktemp(fname);
    tempfp = safe_fopen(fname, "w+");
    fwrite(signature_buffer, 1, datasize, tempfp);
    fclose(tempfp);
    g_free(signature_buffer);

    gpgme_release(ctx);
    return g_strdup(fname);
}


/*
 * Encrypts the data in mailData (the file is assumed to be open and rewinded)
 * for the recipients in the list encrypt_for. Return either the name of the
 * file containing the encrypted data or NULL on error.
 */
static gchar *
gpgme_encrypt_file(MailDataMBox *mailData, GList *encrypt_for)
{
    gpgme_ctx_t ctx;
    gpgme_error_t err;
    gpgme_key_t *rcpt;
    gpgme_data_t in, out;
    gchar *databuf;
    size_t datasize;
    gchar fname[PATH_MAX];
    FILE *outFile;
    struct gpgme_data_cbs cbs = 
	{ (gpgme_data_read_cb_t)read_cb_MailDataMBox, /* read method */
	  NULL,                                       /* write method */
	  NULL,                                       /* seek method */
	  cb_data_release };                          /* release method */

    /* try to create a gpgme context */
    if ((err = gpgme_new(&ctx)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create context: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	return NULL;
    }
    if (!gpgme_check_set_protocol(ctx, GPGME_PROTOCOL_OpenPGP)) {
	gpgme_release(ctx);
	return NULL;
    }

    /* build the list of recipients */
    if (!(rcpt = gpgme_build_recipients(ctx, encrypt_for))) {
        gpgme_release(ctx);
        return NULL;
    }
    
    /* let gpgme encrypt the data */
    gpgme_set_armor(ctx, 1);
    if ((err = gpgme_data_new_from_cbs(&in, &cbs, mailData)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data from file: %s"), 
			     gpgme_strsource(err), gpgme_strerror(err));
	release_keylist(rcpt);
	gpgme_release(ctx);
	return NULL;
    }
    if ((err = gpgme_data_new(&out)) != GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not create new data: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_data_release(in);
	release_keylist(rcpt);
	gpgme_release(ctx);
	return NULL;
    }
    if ((err = gpgme_op_encrypt(ctx, rcpt, GPGME_ENCRYPT_ALWAYS_TRUST, in, out))
	!= GPG_ERR_NO_ERROR) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: encryption failed: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_data_release(out);
	gpgme_data_release(in);
	release_keylist(rcpt);
	gpgme_release(ctx);
	return NULL;
    }

    /* save the result to a file and return its name */
    gpgme_data_release(in);
    release_keylist(rcpt);
    if (!(databuf = gpgme_data_release_and_get_mem(out, &datasize))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("%s: could not get data: %s"),
			     gpgme_strsource(err), gpgme_strerror(err));
	gpgme_release(ctx);
	return NULL;
    }
    
    mutt_mktemp(fname);
    outFile = safe_fopen(fname, "w+");
    fwrite(databuf, 1, datasize, outFile);
    fclose(outFile);
    g_free(databuf);
    gpgme_release(ctx);
    
    return g_strdup(fname);
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


/*
 * helper function: unref all keys in the NULL-terminated array keylist
 * and finally release the array itself
 */
static void
release_keylist(gpgme_key_t *keylist)
{
    gpgme_key_t *key = keylist;
    
    while (*key) {
	gpgme_key_unref(*key);
	key++;
    }
    g_free(keylist);
}


/*
 * dummy function for callback based gpgme data objects
 */
static void
cb_data_release(void * handle)
{
    /* must just be present... */
}


#endif /* HAVE_GPGME */
