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

#ifdef HAVE_XML2
#  include <libxml/xmlmemory.h>
#  include <libxml/parser.h>
#endif

typedef struct _MailDataMBox MailDataMBox;
typedef struct _PassphraseCB PassphraseCB;

struct _MailDataMBox {
    FILE *mailboxFile;
    size_t bytes_left;
    gboolean last_was_cr;
};

struct _PassphraseCB {
    GpgmeCtx ctx;
    gchar *title;
    GtkWindow *parent;
};
    
/* local prototypes */
static const gchar *get_passphrase_cb(void *opaque, const char *desc,
				      void **r_hd);
static gchar *gpgme_signature(MailDataMBox *mailData, const gchar *sign_for,
			      gchar **micalg, GtkWindow *parent);
static gchar *gpgme_encrypt_file(MailDataMBox *mailData, GList *encrypt_for);
static int read_cb_MailDataMBox(void *hook, char *buffer, size_t count,
				size_t *nread);
static void set_decrypt_file(LibBalsaMessageBody *body, const gchar *fname);


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


gboolean
libbalsa_is_pgp_signed(LibBalsaMessageBody *body)
{
    BODY *mb_body, *mb_sig;
    const gchar *micalg;
    const gchar *protocol;
    gboolean result;

    g_return_val_if_fail(body != NULL, FALSE);
    g_return_val_if_fail(body->mutt_body != NULL, FALSE);
    if (!body->parts || !body->parts->next || !body->parts->next->mutt_body)
	return FALSE;

    libbalsa_lock_mutt();
    mb_body = body->mutt_body;
    mb_sig = body->parts->next->mutt_body;

    micalg = mutt_get_parameter("micalg", mb_body->parameter);
    protocol = mutt_get_parameter("protocol", mb_body->parameter);

    result = mb_body->type == TYPEMULTIPART && 
	!g_ascii_strcasecmp("signed", mb_body->subtype) &&
	micalg && !g_ascii_strncasecmp("pgp-", micalg, 4) &&
	protocol && !g_ascii_strcasecmp("application/pgp-signature", protocol) &&
	mb_sig->type == TYPEAPPLICATION &&
	!g_ascii_strcasecmp("pgp-signature", mb_sig->subtype);

    libbalsa_unlock_mutt();
    
    return result;
}


gboolean
libbalsa_is_pgp_encrypted(LibBalsaMessageBody *body)
{
    const gchar *protocol;
    LibBalsaMessageBody *cparts;
    gboolean result;

    g_return_val_if_fail(body != NULL, FALSE);
    g_return_val_if_fail(body->mutt_body != NULL, FALSE);

    libbalsa_lock_mutt();
    protocol = mutt_get_parameter("protocol", body->mutt_body->parameter);
    cparts = body->parts;
    /* FIXME: verify that body contains "Version: 1" */
    result = protocol &&
	!g_ascii_strcasecmp("application/pgp-encrypted", protocol) &&
	cparts && cparts->next && !cparts->next->next &&
	cparts->mutt_body->type == TYPEAPPLICATION && 
	!g_ascii_strcasecmp("pgp-encrypted", cparts->mutt_body->subtype) &&
	cparts->next->mutt_body->type == TYPEAPPLICATION &&
	!g_ascii_strcasecmp("octet-stream", cparts->next->mutt_body->subtype);

    libbalsa_unlock_mutt();
    
    return result;
}


gboolean
libbalsa_body_check_signature(LibBalsaMessageBody* body)
{
    GpgmeCtx ctx;
    GpgmeError err;
    GpgmeData sig, plain;
    MailDataMBox plainStream;
    FILE *sigStream;
    LibBalsaSignatureInfo *sig_status;
    BODY *msg_body, *sign_body;
    LibBalsaMessage *msg;

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
    sig_status->status = GPGME_SIG_STAT_ERROR;
    

    /* try to create a gpgme context */
    if ((err = gpgme_new(&ctx)) != GPGME_No_Error) {
	g_warning("could not create gpgme context: %s", gpgme_strerror(err));
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
    if ((err = gpgme_data_new_with_read_cb(&plain, read_cb_MailDataMBox, &plainStream)) !=
	GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not get data from mailbox file: %s"), 
			     gpgme_strerror(err));
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
	GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not get data from mailbox file: %s"), 
			     gpgme_strerror(err));
	gpgme_data_release(plain);
	gpgme_release(ctx);
	fclose(plainStream.mailboxFile);
	fclose(sigStream);
	return FALSE;
    }

    /* verify the signature */
    if ((err = gpgme_op_verify(ctx, sig, plain, &sig_status->status))
	!= GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme signature verification failed: %s"),
			     gpgme_strerror(err));
	sig_status->status = GPGME_SIG_STAT_ERROR;
    } else {
	/* get more information about this key */
	GpgmeSigStat stat;
	GpgmeKey key;
	
	sig_status->fingerprint =
	    g_strdup(gpgme_get_sig_status(ctx, 0, &stat, &sig_status->sign_time));
	gpgme_get_sig_key(ctx, 0, &key);
	sig_status->validity =
	    gpgme_key_get_ulong_attr(key, GPGME_ATTR_VALIDITY, NULL, 0);
	sig_status->sign_name =
	    g_strdup(gpgme_key_get_string_attr(key, GPGME_ATTR_NAME, NULL, 0));
	sig_status->sign_email =
	    g_strdup(gpgme_key_get_string_attr(key, GPGME_ATTR_EMAIL, NULL, 0));
	sig_status->key_created =
	    gpgme_key_get_ulong_attr(key, GPGME_ATTR_CREATED, NULL, 0);
	gpgme_key_unref(key);
    }

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
    g_return_val_if_fail(micalg != NULL, FALSE);

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
    GpgmeCtx ctx;
    GpgmeError err;
    GpgmeData cipher, plain;
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
    if ((err = gpgme_new(&ctx)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not create gpgme context: %s"),
			     gpgme_strerror(err));
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
					    cipher_body->length)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not get data from mailbox file: %s"), 
			     gpgme_strerror(err));
	gpgme_release(ctx);
	fclose(src);
	return body;
    }

    /* create the plain data stream */
    if ((err = gpgme_data_new(&plain)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not create new data object: %s"),
			     gpgme_strerror(err));
	gpgme_data_release(cipher);
	gpgme_release(ctx);
	fclose(src);
	return body;
    }

    /* try to decrypt */
    if ((err = gpgme_op_decrypt(ctx, cipher, plain)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme decryption failed: %s"),
			     gpgme_strerror(err));
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
			     _("could not get gpgme decrypted data: %s"),
			     gpgme_strerror(err));
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


/* stuff to get a key fingerprint from a selection list */
enum {
    GPG_KEY_FP_COLUMN = 0,
    GPG_KEY_ID_COLUMN,
    GPG_KEY_VALIDITY_COLUMN,
    GPG_KEY_LENGTH_COLUMN,
    GPG_KEY_NUM_COLUMNS
};

static gchar *col_titles[] =
    { N_("Fingerprint"), N_("Key ID"), N_("Trust"), N_("Length") };


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
				G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);

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
 * Called by gpgme to get a new chunk of data. We read from the file in
 * hook (which is actually a MailDataMBox *) and convert every single '\n'
 * into a '\r\n' sequence as requested by rfc3156.
 */
static int
read_cb_MailDataMBox(void *hook, char *buffer, size_t count, size_t *nread)
{
    MailDataMBox *inFile = (MailDataMBox *)hook;

    *nread = 0;
    if (inFile->bytes_left == 0 || feof(inFile->mailboxFile))
	return -1;

    /* try to fill buffer */
    while (count > 1 && inFile->bytes_left > 0 && !feof(inFile->mailboxFile) &&
	   !ferror(inFile->mailboxFile)) {
	int c = fgetc(inFile->mailboxFile);
	inFile->bytes_left--;

	if (c != EOF) {
	    if (c == '\n' && !inFile->last_was_cr) {
		*buffer++ = '\r';
		*nread += 1;
		count -= 1;
	    }
	    inFile->last_was_cr = c == '\r';
	    *buffer++ = c;
	    *nread += 1;
	    count -= 1;
	}
    }

    return ferror(inFile->mailboxFile) ? -1 : 0;
}


/*
 * Called by gpgme to get the passphrase for a key.
 */
static const gchar *
get_passphrase_cb(void *opaque, const char *desc, void **r_hd)
{
    PassphraseCB *cb_data = (PassphraseCB *)opaque;
    GtkWidget *dialog, *entry;
    gchar *prompt, *passwd = NULL;
    gchar **desc_parts;
    gboolean was_bad;

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
    
    desc_parts = g_strsplit(desc, "\n", 3);
    was_bad = !strcmp(desc_parts[0], "TRY_AGAIN");

    /* FIXME: create dialog according to the Gnome HIG */
    dialog = gtk_dialog_new_with_buttons(cb_data->title,
                                         cb_data->parent,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         NULL);
    if (was_bad)
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
    g_strfreev(desc_parts);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
                      entry = gtk_entry_new());
    gtk_widget_show_all(GTK_WIDGET(GTK_DIALOG(dialog)->vbox));
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 40);
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);

    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_grab_focus (entry);

    if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
        passwd = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
    gtk_widget_destroy(dialog);

    if (!passwd)
	gpgme_cancel(cb_data->ctx);

    *r_hd = passwd;
    return passwd;
}


#ifdef HAVE_XML2
/*
 * Parse the XML output gpgme_op_info generated by gpgme_get_op_info for
 * element in GnupgOperationInfo->signature and return it. If the result
 * is not NULL, it has to be freed.
 */
static gchar *
get_gpgme_parameter(const gchar *gpgme_op_info, const gchar *element)
{
    xmlDocPtr doc;
    xmlNodePtr cur, child;
    xmlChar *result = NULL;

    /* get the xml doc from the supplied data */
    if (!(doc = xmlParseMemory(gpgme_op_info, strlen(gpgme_op_info))))
	return NULL;

    /* check if the root is correct */
    if (!(cur = xmlDocGetRootElement(doc))) {
	xmlFreeDoc(doc);
	return NULL;
    }

    /* check if the root is "GnupgOperationInfo" */
    if (xmlStrcmp(cur->name, (const xmlChar *)"GnupgOperationInfo")) {
	xmlFreeDoc(doc);
	return NULL;
    }

    /* parse childs for "signature" info */
    child = cur->xmlChildrenNode;
    while (child && !result) {
	if ((!xmlStrcmp(child->name, (const xmlChar *)"signature"))) {
	    xmlNodePtr cchild = child->xmlChildrenNode;
	    while (cchild && !result) {
		if ((!xmlStrcmp(cchild->name, (const xmlChar *)element)))
		    result = 
			xmlNodeListGetString(doc, cchild->xmlChildrenNode, 1);
		cchild = cchild->next;
	    }
	}
	child = child->next;
    }
    
    xmlFreeDoc(doc);
    return (gchar *)result;
}

#else

static gchar *
get_gpgme_parameter(const gchar *gpgme_op_info, const gchar *element)
{
    gchar *p, *q;

    if (!(p = strstr(gpgme_op_info, element)))
	return NULL;
    p += strlen(element) + 1;
    q = p + 1;

    while (*q && *q != '<')
	q++;
    *q = '\0';
    return g_strdup(p);
}
#endif


/*
 * Signs the data in mailData (the file is assumed to be open and rewinded)
 * for sign_for. Return either a signature block or NULL on error. Upon
 * success, micalg is set to the used mic algorithm (must be freed).
 */
static gchar *
gpgme_signature(MailDataMBox *mailData, const gchar *sign_for, gchar **micalg,
		GtkWindow *parent)
{
    GpgmeCtx ctx;
    GpgmeError err;
    GpgmeData in, out;
    size_t datasize;
    gchar *signature_buffer, *signature_info;
    PassphraseCB cb_data;
    gchar fname[PATH_MAX];
    FILE *tempfp;

    *micalg = NULL;

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
    cb_data.parent = parent;
    cb_data.title = _("Enter passsphrase to sign message");
    gpgme_set_passphrase_cb(ctx, get_passphrase_cb, &cb_data);
    if ((err = gpgme_data_new_with_read_cb(&in, read_cb_MailDataMBox,
					   mailData)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not get data from file: %s"), 
			     gpgme_strerror(err));
	gpgme_release(ctx);
	return NULL;
    }
    if ((err = gpgme_data_new(&out)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not create new data: %s"),
			     gpgme_strerror(err));
	gpgme_data_release(in);
	gpgme_release(ctx);
	return NULL;
    }
    if ((err = gpgme_op_sign(ctx, in, out, GPGME_SIG_MODE_DETACH)) != 
	GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme signing failed: %s"),
			     gpgme_strerror(err));
	gpgme_data_release(out);
	gpgme_data_release(in);
	gpgme_release(ctx);
	return NULL;
    }

    /* get the info about the used hash algorithm */
    signature_info = gpgme_get_op_info(ctx, 0);
    if (signature_info) {
	*micalg = get_gpgme_parameter(signature_info, "micalg");
	g_free(signature_info);
    }

    gpgme_data_release(in);
    if (!(signature_buffer = gpgme_data_release_and_get_mem(out, &datasize))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not get gpgme data: %s"),
			     gpgme_strerror(err));
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
    GpgmeCtx ctx;
    GpgmeError err;
    GpgmeRecipients rcpt;
    GpgmeData in, out;
    gchar *databuf;
    size_t datasize;
    gchar fname[PATH_MAX];
    FILE *outFile;

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
    
    /* let gpgme encrypt the data */
    gpgme_set_armor(ctx, 1);
    if ((err = gpgme_data_new_with_read_cb(&in, read_cb_MailDataMBox,
					   mailData)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not get data from file: %s"), 
			     gpgme_strerror(err));
	gpgme_recipients_release(rcpt);
	gpgme_release(ctx);
	return NULL;
    }
    if ((err = gpgme_data_new(&out)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme could not create new data: %s"),
			     gpgme_strerror(err));
	gpgme_data_release(in);
	gpgme_recipients_release(rcpt);
	gpgme_release(ctx);
	return NULL;
    }
    if ((err = gpgme_op_encrypt(ctx, rcpt, in, out)) != GPGME_No_Error) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("gpgme encryption failed: %s"),
			     gpgme_strerror(err));
	gpgme_data_release(out);
	gpgme_data_release(in);
	gpgme_recipients_release(rcpt);
	gpgme_release(ctx);
	return NULL;
    }

    /* save the result to a file and return its name */
    gpgme_data_release(in);
    gpgme_recipients_release(rcpt);
    if (!(databuf = gpgme_data_release_and_get_mem(out, &datasize))) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("could not get gpgme data: %s"),
			     gpgme_strerror(err));
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


#endif /* HAVE_GPGME */
