/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * Balsa E-Mail Client
 *
 * gpgme key related widgets and display functions
 * Copyright (C) 2017 Albrecht Dreß <albrecht.dress@arcor.de>
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


#include "libbalsa-gpgme-widgets.h"
#include <string.h>
#include <glib/gi18n.h>
#include "rfc3156.h"


#ifdef G_LOG_DOMAIN
#   undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "crypto"



#define BULLET_STR      "\302\240\342\200\242\302\240"


static gchar *create_status_str(gboolean revoked,
                                gboolean expired,
                                gboolean disabled,
                                gboolean invalid)
G_GNUC_WARN_UNUSED_RESULT;
static gchar *create_uid_str(const gpgme_user_id_t uid,
                             gboolean             *warn)
G_GNUC_WARN_UNUSED_RESULT;

static gint create_key_grid_row(GtkGrid     *grid,
                                gint         row,
                                const gchar *key,
                                const gchar *value,
                                gboolean     warn);
static GtkWidget *create_key_uid_widget(const gpgme_user_id_t uid)
G_GNUC_WARN_UNUSED_RESULT;
static GtkWidget *create_key_label_with_warn(const gchar *text,
                                             gboolean     warn)
G_GNUC_WARN_UNUSED_RESULT;
static GtkWidget *create_subkey_widget(gpgme_subkey_t subkey)
G_GNUC_WARN_UNUSED_RESULT;
static gchar *create_purpose_str(gboolean can_sign,
                                 gboolean can_encrypt,
                                 gboolean can_certify,
                                 gboolean can_auth)
G_GNUC_WARN_UNUSED_RESULT;
static gchar *create_subkey_type_str(gpgme_subkey_t subkey)
G_GNUC_WARN_UNUSED_RESULT;


/* documentation: see header file */
GtkWidget *
libbalsa_gpgme_key(gpgme_key_t          key,
                   const gchar         *fingerprint,
                   lb_gpg_subkey_capa_t subkey_capa,
                   gboolean             expanded)
{
    GtkWidget *key_data;
    gchar *status_str;
    gchar *uid_readable;
    gint row = 0;

    g_return_val_if_fail(key != NULL, NULL);

    key_data = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(key_data), 2);
    gtk_grid_set_column_spacing(GTK_GRID(key_data), 6);

    /* print a warning for a bad key status */
    status_str = create_status_str(key->expired != 0U,
                                   key->revoked != 0U,
                                   key->disabled != 0U,
                                   key->invalid != 0U);
    if (strlen(status_str) > 0U) {
        row = create_key_grid_row(GTK_GRID(key_data), row, _("Key status:"), status_str, TRUE);
    }
    g_free(status_str);

    /* primary User ID */
    if (key->uids == NULL) {
        row = create_key_grid_row(GTK_GRID(key_data), row, _("User ID:"), _("None"), FALSE);
    } else {
        GtkWidget *label;

        if (key->uids->next != NULL) {
            label = gtk_label_new(_("Primary user ID:"));
        } else {
            label = gtk_label_new(_("User ID:"));
        }
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(key_data), label, 0, row, 1, 1);

        gtk_grid_attach(GTK_GRID(key_data), create_key_uid_widget(key->uids), 1, row++, 1, 1);
    }

    /* owner trust is valid for OpenPGP only */
    if (key->protocol == GPGME_PROTOCOL_OpenPGP) {
        row = create_key_grid_row(GTK_GRID(key_data), row, _("Key owner trust:"),
                                  libbalsa_gpgme_validity_to_gchar_short(
                                      key->owner_trust), FALSE);
    }

    /* add additional UID's (if any) */
    if ((key->uids != NULL) && (key->uids->next != NULL)) {
        GtkWidget *uid_expander;
        GtkWidget *uid_box;
        gpgme_user_id_t uid;

        uid_expander = gtk_expander_new(_("Additional User IDs"));
        gtk_expander_set_expanded(GTK_EXPANDER(uid_expander), expanded);
        gtk_grid_attach(GTK_GRID(key_data), uid_expander, 0, row++, 2, 1);

        uid_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_margin_start(uid_box, 12);
        gtk_container_add(GTK_CONTAINER(uid_expander), uid_box);
        for (uid = key->uids->next; uid != NULL; uid = uid->next) {
            gtk_box_pack_end(GTK_BOX(uid_box), create_key_uid_widget(uid));
        }
    }

    /* add the issuer information for CMS only */
    if (key->protocol == GPGME_PROTOCOL_CMS) {
        GtkWidget *issuer_expander;
        GtkWidget *issuer_grid;
        gint issuer_row = 0;

        issuer_expander = gtk_expander_new(_("Issuer"));
        gtk_expander_set_expanded(GTK_EXPANDER(issuer_expander), expanded);
        gtk_grid_attach(GTK_GRID(key_data), issuer_expander, 0, row++, 2, 1);

        issuer_grid = gtk_grid_new();
        gtk_widget_set_margin_start(issuer_grid, 12);
        gtk_grid_set_column_spacing(GTK_GRID(issuer_grid), 6);
        gtk_container_add(GTK_CONTAINER(issuer_expander), issuer_grid);

        if (key->issuer_name != NULL) {
            uid_readable = libbalsa_cert_subject_readable(key->issuer_name);
            issuer_row = create_key_grid_row(GTK_GRID(issuer_grid), issuer_row, _(
                                                 "Name:"), uid_readable, FALSE);
            g_free(uid_readable);
        }
        if (key->issuer_serial != NULL) {
            issuer_row =
                create_key_grid_row(GTK_GRID(issuer_grid), issuer_row, _(
                                        "Serial number:"), key->issuer_serial, FALSE);
        }
        if (key->chain_id != NULL) {
            (void) create_key_grid_row(GTK_GRID(issuer_grid), issuer_row, _(
                                           "Chain ID:"), key->chain_id, FALSE);
        }
    }

    /* subkey information */
    if (((fingerprint != NULL) || (subkey_capa != 0U)) && (key->subkeys != NULL)) {
        GtkWidget *subkey_expander;
        GtkWidget *subkey_box;
        gpgme_subkey_t subkey;

        if (fingerprint != NULL) {
            subkey_expander = gtk_expander_new(_("Subkey used"));
        } else if (subkey_capa != GPG_SUBKEY_CAP_ALL) {
            gchar *capa_str;
            gchar *label_str;

            /* indicate that we show only subkeys with certain capabilities */
            capa_str =
                create_purpose_str((subkey_capa & GPG_SUBKEY_CAP_SIGN) != 0U,
                                   (subkey_capa & GPG_SUBKEY_CAP_ENCRYPT) != 0U,
                                   (subkey_capa & GPG_SUBKEY_CAP_CERTIFY) != 0U,
                                   (subkey_capa & GPG_SUBKEY_CAP_AUTH) != 0U);
            label_str = g_strdup_printf(_("Subkeys (%s only)"), capa_str);
            subkey_expander = gtk_expander_new(label_str);
            g_free(label_str);
            g_free(capa_str);
        } else {
            subkey_expander = gtk_expander_new(_("Subkeys"));
        }
        gtk_expander_set_expanded(GTK_EXPANDER(subkey_expander), expanded);
        gtk_grid_attach(GTK_GRID(key_data), subkey_expander, 0, row++, 2, 1);

        subkey_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_margin_start(subkey_box, 12);
        gtk_container_add(GTK_CONTAINER(subkey_expander), subkey_box);

        for (subkey = key->subkeys; subkey != NULL; subkey = subkey->next) {
            if (fingerprint != NULL) {
                if (strcmp(fingerprint, subkey->fpr) == 0) {
                    gtk_box_pack_end(GTK_BOX(subkey_box), create_subkey_widget(subkey));
                }
            } else if ((((subkey_capa & GPG_SUBKEY_CAP_SIGN) != 0U) &&
                        (subkey->can_sign != 0)) ||
                       (((subkey_capa & GPG_SUBKEY_CAP_ENCRYPT) != 0U) &&
                        (subkey->can_encrypt != 0)) ||
                       (((subkey_capa & GPG_SUBKEY_CAP_CERTIFY) != 0U) &&
                        (subkey->can_certify != 0)) ||
                       (((subkey_capa & GPG_SUBKEY_CAP_AUTH) != 0U) &&
                        (subkey->can_authenticate != 0))) {
                gtk_box_pack_end(GTK_BOX(subkey_box), create_subkey_widget(subkey));
            } else {
                /* do not print this subkey */
            }
        }
    }

    return key_data;
}


/* documentation: see header file */
gchar *
libbalsa_gpgme_key_to_gchar(gpgme_key_t  key,
                            const gchar *fingerprint)
{
    GString *result;
    gchar *status_str;

    g_return_val_if_fail((key != NULL) && (fingerprint != NULL), NULL);

    result = g_string_new(NULL);

    /* print a warning for a bad key status */
    status_str = create_status_str(key->expired != 0U,
                                   key->revoked != 0U,
                                   key->disabled != 0U,
                                   key->invalid != 0U);
    if (strlen(status_str) > 0U) {
        g_string_append_printf(result, "%s %s\n", _("Key status:"), status_str);
    }
    g_free(status_str);

    /* primary User ID */
    if (key->uids == NULL) {
        g_string_append_printf(result, "%s %s", _("User ID:"), _("None"));
    } else {
        gchar *uid_str;

        uid_str = create_uid_str(key->uids, NULL);
        if (key->uids->next != NULL) {
            g_string_append_printf(result, "%s %s", _("Primary user ID:"), uid_str);
        } else {
            g_string_append_printf(result, "%s %s", _("User ID:"), uid_str);
        }
        g_free(uid_str);
    }

    /* owner trust is valid for OpenPGP only */
    if (key->protocol == GPGME_PROTOCOL_OpenPGP) {
        g_string_append_printf(result, "\n%s %s", _(
                                   "Key owner trust:"),
                               libbalsa_gpgme_validity_to_gchar_short(key->owner_trust));
    }

    /* add additional UID's (if any) */
    if ((key->uids != NULL) && (key->uids->next != NULL)) {
        gpgme_user_id_t uid;

        g_string_append_printf(result, "\n%s", _("Additional User IDs"));
        for (uid = key->uids->next; uid != NULL; uid = uid->next) {
            gchar *uid_str;

            uid_str = create_uid_str(uid, NULL);
            g_string_append_printf(result, "\n" BULLET_STR "%s", uid_str);
            g_free(uid_str);
        }
    }

    /* add the issuer information for CMS only */
    if (key->protocol == GPGME_PROTOCOL_CMS) {
        g_string_append_printf(result, "\n%s", _("Issuer"));

        if (key->issuer_name != NULL) {
            gchar *issuer_readable;

            issuer_readable = libbalsa_cert_subject_readable(key->issuer_name);
            g_string_append_printf(result, "\n" BULLET_STR "%s %s", _("Name:"),
                                   issuer_readable);
            g_free(issuer_readable);
        }
        if (key->issuer_serial != NULL) {
            g_string_append_printf(result, "\n" BULLET_STR "%s %s", _(
                                       "Serial number:"), key->issuer_serial);
        }
        if (key->chain_id != NULL) {
            g_string_append_printf(result, "\n" BULLET_STR "%s %s", _(
                                       "Chain ID:"), key->chain_id);
        }
    }

    /* subkey information */
    if (key->subkeys != NULL) {
        gpgme_subkey_t subkey;

        g_string_append_printf(result, "\n%s", _("Subkey used"));
        for (subkey = key->subkeys; subkey != NULL; subkey = subkey->next) {
            if (strcmp(fingerprint, subkey->fpr) == 0) {
                gchar *details_str;
                gchar *timebuf;

                details_str = create_status_str(subkey->expired != 0U,
                                                subkey->revoked != 0U,
                                                subkey->disabled != 0U,
                                                subkey->invalid != 0U);
                if (strlen(details_str) > 0U) {
                    g_string_append_printf(result, "\n" BULLET_STR "%s %s", _(
                                               "Status:"), details_str);
                }
                g_free(details_str);

                g_string_append_printf(result, "\n" BULLET_STR "%s %s", _(
                                           "Fingerprint:"), subkey->fpr);

                details_str = create_subkey_type_str(subkey);
                g_string_append_printf(result, "\n" BULLET_STR "%s %s", _("Type:"),
                                       details_str);
                g_free(details_str);

                details_str = create_purpose_str(subkey->can_sign != 0U,
                                                 subkey->can_encrypt != 0,
                                                 subkey->can_certify != 0U,
                                                 subkey->can_authenticate != 0U);
                if (strlen(details_str) > 0U) {
                    g_string_append_printf(result, "\n" BULLET_STR "%s %s", _(
                                               "Capabilities:"), details_str);
                }
                g_free(details_str);

                if (subkey->timestamp == -1) {
                    timebuf = g_strdup(_("invalid timestamp"));
                } else if (subkey->timestamp == 0) {
                    timebuf = g_strdup(_("not available"));
                } else {
                    timebuf = libbalsa_date_to_utf8(subkey->timestamp, "%x %X");
                }
                g_string_append_printf(result, "\n" BULLET_STR "%s %s", _("Created:"), timebuf);
                g_free(timebuf);

                if (subkey->expires == 0) {
                    timebuf = g_strdup(_("never"));
                } else {
                    timebuf = libbalsa_date_to_utf8(subkey->expires, "%x %X");
                }
                g_string_append_printf(result, "\n" BULLET_STR "%s %s", _("Expires:"), timebuf);
                g_free(timebuf);
            }
        }
    }

    return g_string_free(result, FALSE);
}


/* documentation: see header file */
GtkWidget *
libbalsa_key_dialog(GtkWindow           *parent,
                    GtkButtonsType       buttons,
                    gpgme_key_t          key,
                    lb_gpg_subkey_capa_t subkey_capa,
                    const gchar         *message1,
                    const gchar         *message2)
{
    GtkWidget *dialog;
    GtkWidget *hbox;
    GtkWidget *icon;
    GtkWidget *vbox;
    GtkWidget *label;
    GtkWidget *key_data;
    GtkWidget *scrolledw;

    g_return_val_if_fail(key != NULL, NULL);

    switch (buttons) {
    case GTK_BUTTONS_CLOSE:
        dialog = gtk_dialog_new_with_buttons(NULL,
                                             parent,
                                             GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
                                             _("Close"),
                                             GTK_RESPONSE_CLOSE,
                                             NULL);
        break;

    case GTK_BUTTONS_YES_NO:
        dialog = gtk_dialog_new_with_buttons(NULL,
                                             parent,
                                             GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
                                             _("No"),
                                             GTK_RESPONSE_NO,
                                             _("Yes"),
                                             GTK_RESPONSE_YES,
                                             NULL);
        break;

    default:
        g_error("%s: buttons type %d not yet implemented", __func__, buttons);
    }
    gtk_window_set_resizable(GTK_WINDOW(dialog), TRUE);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    g_object_set(G_OBJECT(hbox), "margin", 6, NULL);
    gtk_widget_set_vexpand(hbox, TRUE);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), hbox);
    gtk_box_set_homogeneous(GTK_BOX(hbox), FALSE);

    /* standard key icon; "application-certificate" would be an alternative... */
    icon = gtk_image_new_from_icon_name("dialog-password");
    gtk_box_pack_start(GTK_BOX(hbox), icon);
    gtk_widget_set_valign(icon, GTK_ALIGN_START);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), vbox);
    gtk_box_set_homogeneous(GTK_BOX(vbox), FALSE);

    if (message1 != NULL) {
        char *markup;

        label = gtk_label_new(NULL);
        markup = g_markup_printf_escaped("<b><big>%s</big></b>", message1);
        gtk_label_set_markup(GTK_LABEL(label), markup);
        g_free(markup);
        gtk_box_pack_start(GTK_BOX(vbox), label);
        gtk_label_set_line_wrap(GTK_LABEL(label), FALSE);
    }

    if (message2 != NULL) {
        label = gtk_label_new(message2);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(vbox), label);
        gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    }

    scrolledw = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolledw, TRUE);
    gtk_widget_set_margin_top(scrolledw, 6);
    gtk_box_pack_start(GTK_BOX(vbox), scrolledw);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(
                                       scrolledw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolledw), 120);

    key_data = libbalsa_gpgme_key(key, NULL, subkey_capa, TRUE);
    gtk_container_add(GTK_CONTAINER(scrolledw), key_data);

    return dialog;
}


/* ---- local stuff ---------------------------------------------------- */

/** \brief Create a status string for an item
 *
 * \param revoked TRUE if the item is revoked
 * \param expired TRUE if the item is expired
 * \param disabled TRUE if the item is disabled
 * \param invalid TRUE if the item is invalid
 * \return a human-readable string containing the states which may be empty
 *
 * \todo Add the is_qualified ([sub]key can be used for qualified signatures according to local
 * government regulations) flag?
 */
static gchar *
create_status_str(gboolean revoked,
                  gboolean expired,
                  gboolean disabled,
                  gboolean invalid)
{
    GString *status;

    status = g_string_new(NULL);
    if (revoked) {
        status = g_string_append(status, _("revoked"));
    }
    if (expired) {
        g_string_append_printf(status, "%s%s", (status->len > 0U) ? ", " : "", _("expired"));
    }
    if (disabled) {
        g_string_append_printf(status, "%s%s", (status->len > 0U) ? ", " : "", _("disabled"));
    }
    if (invalid) {
        g_string_append_printf(status, "%s%s", (status->len > 0U) ? ", " : "", _("invalid"));
    }
    return g_string_free(status, FALSE);
}


/** \brief Create a UID string
 *
 * \param uid UID
 * \param warn filled with TRUE if the UID is revoked or invalid
 * \return a newly allocated string containing the UID string
 *
 * Create a string containing the passed UID subject.  If the UID is revoked or invalid, the
 * status information is added in '(…)'.
 *
 * \todo Do we want to add more details from the gpgme_user_id_t data?
 */
static gchar *
create_uid_str(const gpgme_user_id_t uid,
               gboolean             *warn)
{
    gchar *uid_readable;
    gchar *uid_status;
    gchar *result;
    gboolean do_warn;

    uid_readable = libbalsa_cert_subject_readable(uid->uid);
    uid_status = create_status_str(uid->revoked != 0U, FALSE, FALSE, uid->invalid != 0U);
    if (uid_status[0] != '\0') {
        result = g_strdup_printf("%s (%s)", uid_readable, uid_status);
        g_free(uid_readable);
        g_free(uid_status);
        do_warn = TRUE;
    } else {
        result = uid_readable;
        do_warn = FALSE;
    }

    if (warn != NULL) {
        *warn = do_warn;
    }

    return result;
}


/** \brief Add a grid row
 *
 * \param grid target grid
 * \param row grid row index
 * \param key key string in the 1st grid column
 * \param value value string in the 2nd grid column
 * \param warn prepend a warning icon and print \em value in red if TRUE
 * \return the next grid row index
 */
static gint
create_key_grid_row(GtkGrid     *grid,
                    gint         row,
                    const gchar *key,
                    const gchar *value,
                    gboolean     warn)
{
    GtkWidget *label;

    label = gtk_label_new(key);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

    label = create_key_label_with_warn(value, warn);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_grid_attach(GTK_GRID(grid), label, 1, row, 1, 1);

    return row + 1;
}


/** \brief Create a key's UID widget
 *
 * \param uid UID
 * \return the uid widget
 *
 * Create a widget containing the passed UID subject.  If the UID is revoked or invalid, a
 * warning icon is prepended, and the
 * status information is added.
 *
 * \sa create_uid_str()
 *
 * \todo We might want to show the TOFU data (requires gpgme >= 1.7.0, and
 * GPGME_KEYLIST_MODE_WITH_TOFU in key listing options).
 *       Maybe also add an expander?
 */
static GtkWidget *
create_key_uid_widget(const gpgme_user_id_t uid)
{
    gchar *buf;
    gboolean warn;
    GtkWidget *result;

    buf = create_uid_str(uid, &warn);
    result = create_key_label_with_warn(buf, warn);
    g_free(buf);
    return result;
}


/** \brief Create a label with warning information
 *
 * \param text label text
 * \param warn TRUE if a visible warning shall be included
 * \return the widget
 *
 * If \em warn is FALSE, the returned item is simply a GtkLabel.  Otherwise, it is a box,
 * containing the label printed in red, with
 * a warning icon prepended.  In both cases, the label is start-justified, selectable and
 * line-wrappable.
 */
static GtkWidget *
create_key_label_with_warn(const gchar *text,
                           gboolean     warn)
{
    GtkWidget *result;

    if (warn) {
        GtkWidget *icon;
        GtkWidget *label;
        gchar *buf;

        result = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        icon = gtk_image_new_from_icon_name("gtk-dialog-warning");
        gtk_box_pack_start(GTK_BOX(result), icon);
        buf = g_markup_printf_escaped("<span fgcolor=\"red\">%s</span>", text);
        label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(label), buf);
        g_free(buf);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_hexpand(label, TRUE);
        gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_selectable(GTK_LABEL(label), TRUE);
        gtk_box_pack_start(GTK_BOX(result), label);
    } else {
        result = gtk_label_new(text);
        gtk_widget_set_halign(result, GTK_ALIGN_START);
        gtk_widget_set_hexpand(result, TRUE);
        gtk_label_set_selectable(GTK_LABEL(result), TRUE);
        gtk_label_set_line_wrap(GTK_LABEL(result), TRUE);
    }

    return result;
}


/** \brief Create a widget with subkey information
 *
 * \param subkey the subkey which shall be added
 * \return a nes GtkGrid containing the subkey information
 *
 * \note Time stamps are formatted using "%x %X" (preferred locale's date and time format), so
 * we can avoid passing Balsa's
 *       user-defined format string down here.  We might want to change this.
 */
static GtkWidget *
create_subkey_widget(gpgme_subkey_t subkey)
{
    GtkWidget *subkey_grid;
    gint subkey_row = 0;
    gchar *details_str;
    gchar *timebuf;

    subkey_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(subkey_grid), 6);

    /* print a warning for a bad subkey status */
    details_str = create_status_str(subkey->expired != 0U,
                                    subkey->revoked != 0U,
                                    subkey->disabled != 0U,
                                    subkey->invalid != 0U);
    if (strlen(details_str) > 0U) {
        subkey_row = create_key_grid_row(GTK_GRID(subkey_grid), subkey_row, _(
                                             "Status:"), details_str, TRUE);
    }
    g_free(details_str);

    subkey_row = create_key_grid_row(GTK_GRID(subkey_grid), subkey_row, _(
                                         "Fingerprint:"), subkey->fpr, FALSE);

    details_str = create_subkey_type_str(subkey);
    subkey_row = create_key_grid_row(GTK_GRID(subkey_grid), subkey_row, _(
                                         "Type:"), details_str, FALSE);
    g_free(details_str);

    details_str = create_purpose_str(subkey->can_sign != 0U,
                                     subkey->can_encrypt != 0,
                                     subkey->can_certify != 0U,
                                     subkey->can_authenticate != 0U);
    if (strlen(details_str) > 0U) {
        subkey_row = create_key_grid_row(GTK_GRID(subkey_grid), subkey_row, _(
                                             "Capabilities:"), details_str, FALSE);
    }
    g_free(details_str);

    if (subkey->timestamp == -1) {
        timebuf = g_strdup(_("invalid timestamp"));
    } else if (subkey->timestamp == 0) {
        timebuf = g_strdup(_("not available"));
    } else {
        timebuf = libbalsa_date_to_utf8(subkey->timestamp, "%x %X");
    }
    subkey_row = create_key_grid_row(GTK_GRID(subkey_grid), subkey_row, _(
                                         "Created:"), timebuf, FALSE);
    g_free(timebuf);

    if (subkey->expires == 0) {
        timebuf = g_strdup(_("never"));
    } else {
        timebuf = libbalsa_date_to_utf8(subkey->expires, "%x %X");
    }
    (void) create_key_grid_row(GTK_GRID(subkey_grid), subkey_row, _("Expires:"), timebuf,
                               FALSE);
    g_free(timebuf);

    return subkey_grid;
}


/** \brief Create a subkey purpose string
 *
 * \param can_sign indicates that the subkey can be used to create data signatures
 * \param can_encrypt indicates that the subkey can be used for encryption
 * \param can_certify indicates that the subkey can be used to create key certificates
 * \param can_auth indicates that the subkey can be used for authentication
 * \return a string indicating whether a subkey can be used for signing, encryption,
 * certification or authentication
 *
 * \todo Add the is_qualified (subkey can be used for qualified signatures according to local
 * government regulations) and is_de_vs
 *       (complies with the rules for classified information in Germany at the restricted level,
 * VS-NfD, requires gpgme >= 1.9.0)
 *       flags?
 */
static gchar *
create_purpose_str(gboolean can_sign,
                   gboolean can_encrypt,
                   gboolean can_certify,
                   gboolean can_auth)
{
    GString *purpose;

    purpose = g_string_new(NULL);

    if (can_sign) {
        purpose = g_string_append(purpose, _("sign"));
    }
    if (can_encrypt) {
        g_string_append_printf(purpose, "%s%s", (purpose->len > 0U) ? ", " : "", _("encrypt"));
    }
    if (can_certify) {
        g_string_append_printf(purpose, "%s%s", (purpose->len > 0U) ? ", " : "", _("certify"));
    }
    if (can_auth) {
        g_string_append_printf(purpose, "%s%s", (purpose->len > 0U) ? ", " : "",
                               _("authenticate"));
    }
    return g_string_free(purpose, FALSE);
}


/** \brief Create a subkey type string
 *
 * \param subkey the subkey
 * \return a newly allocated string containing a human-readable description of the subkey type
 *
 * Create a string containing the length of the subkey in bits, the public key algorithm
 * supported by it and for ECC algorithms the
 * name of the curve.
 */
static gchar *
create_subkey_type_str(gpgme_subkey_t subkey)
{
    GString *type_str;
    const gchar *algo;

    type_str = g_string_new(NULL);
    g_string_append_printf(type_str, ngettext("%u bit",
                                              "%u bits",
                                              subkey->length), subkey->length);
    algo = gpgme_pubkey_algo_name(subkey->pubkey_algo);
    if (algo != NULL) {
        g_string_append_printf(type_str, " %s", algo);
    }
    if (subkey->curve != NULL) {
        g_string_append_printf(type_str, _(" curve “%s”"), subkey->curve);
    }

    return g_string_free(type_str, FALSE);
}
