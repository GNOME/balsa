# libbalsa/meson.build

if gpgmecfg == 'true'
  libbalsa_gpgme_extra = [
    'libbalsa-gpgme.h',
    'libbalsa-gpgme.c',
    'libbalsa-gpgme-cb.h',
    'libbalsa-gpgme-cb.c',
    'libbalsa-gpgme-keys.h',
    'libbalsa-gpgme-keys.c',
    'libbalsa-gpgme-widgets.h',
    'libbalsa-gpgme-widgets.c',
    'gmime-multipart-crypt.h',
    'gmime-multipart-crypt.c',
    'gmime-part-rfc2440.h',
    'gmime-part-rfc2440.c',
    'gmime-gpgme-signature.h',
    'gmime-gpgme-signature.c',
    'gmime-application-pkcs7.h',
    'gmime-application-pkcs7.c']
else
  libbalsa_gpgme_extra = []
endif

libbalsa_a_sources = [
  'abook-completion.c',
  'abook-completion.h',
  'address-book-extern.c',
  'address-book-extern.h',
  'address-book-gpe.c',
  'address-book-gpe.h',
  'address-book-ldap.c',
  'address-book-ldap.h',
  'address-book-ldif.c',
  'address-book-ldif.h',
  'address-book-osmo.c',
  'address-book-osmo.h',
  'address-book-rubrica.c',
  'address-book-rubrica.h',
  'address-book-text.c',
  'address-book-text.h',
  'address-book-vcard.c',
  'address-book-vcard.h',
  'address-book.c',
  'address-book.h',
  'address-view.c',
  'address-view.h',
  'address.c',
  'address.h',
  'application-helpers.c',
  'application-helpers.h',
  'body.c',
  'body.h',
  'cell-renderer-button.c',
  'cell-renderer-button.h',
  'completion.c',
  'completion.h',
  'files.c',
  'files.h',
  'filter-error.c',
  'filter-file.c',
  'filter-file.h',
  'filter-funcs.c',
  'filter-funcs.h',
  'filter-private.h',
  'filter.c',
  'filter.h',
  'folder-scanners.c',
  'folder-scanners.h',
  'gmime-filter-header.c',
  'gmime-filter-header.h',
  'html.c',
  'html.h',
  'identity.c',
  'identity.h',
  'imap-server.c',
  'imap-server.h',
  'information.c',
  'information.h',
  'libbalsa-conf.c',
  'libbalsa-conf.h',
  'libbalsa-progress.c',
  'libbalsa-progress.h',
  'macosx-helpers.c',
  'macosx-helpers.h',
  'missing.h',
  'missing_time.c',
  'libbalsa.c',
  'libbalsa.h',
  'libbalsa_private.h',
  'libbalsa-vfs.c',
  'libbalsa-vfs.h',
  'mailbackend.h',
  'mailbox-filter.c',
  'mailbox-filter.h',
  'mailbox.c',
  'mailbox.h',
  'mailbox_imap.c',
  'mailbox_imap.h',
  'mailbox_local.c',
  'mailbox_local.h',
  'mailbox_maildir.c',
  'mailbox_maildir.h',
  'mailbox_mbox.c',
  'mailbox_mbox.h',
  'mailbox_mh.c',
  'mailbox_mh.h',
  'mailbox_pop3.c',
  'mailbox_pop3.h',
  'mailbox_remote.c',
  'mailbox_remote.h',
  'message.c',
  'message.h',
  'mime.c',
  'mime.h',
  'mime-stream-shared.c',
  'mime-stream-shared.h',
  'misc.c',
  'misc.h',
  'rfc2445.c',
  'rfc2445.h',
  'rfc3156.c',
  'rfc3156.h',
  'rfc6350.c',
  'rfc6350.h',
  'send.c',
  'send.h',
  'server.c',
  'server.h',
  'smtp-server.c',
  'smtp-server.h',
  'source-viewer.c',
  'url.c',
  'url.h']

libbalsa_a_sources += libbalsa_gpgme_extra

# genmarshal
#

if use_gnome_genmarshal
  libbalsa_marshal_files = gnome.genmarshal('libbalsa-marshal',
                                            prefix  : 'libbalsa',
                                            sources : 'libbalsa-marshal.list')
else # use_gnome_genmarshal
  libbalsa_marshal_h = custom_target('libbalsa_marshal_h',
    input   : 'libbalsa-marshal.list',
    output  : 'libbalsa-marshal.h',
    command : [
      glib_genmarshal,
      '--header',
      '--prefix=libbalsa',
      '--output=@OUTPUT@',
      '@INPUT@'
    ]
  )

  libbalsa_marshal_c = custom_target('libbalsa_marshal_c',
    input   : 'libbalsa-marshal.list',
    output  : 'libbalsa-marshal.c',
    command : [
      glib_genmarshal,
      '--header',
      '--body',
      '--prefix=libbalsa',
      '--output=@OUTPUT@',
      '@INPUT@'
    ]
  )

  libbalsa_marshal_files = [libbalsa_marshal_c, libbalsa_marshal_h]
endif # use_gnome_genmarshal

libimap_include = include_directories('imap')

libbalsa_a = static_library('balsa', libbalsa_a_sources, libbalsa_marshal_files,
                            dependencies        : balsa_deps,
                            include_directories : [top_include,
                                                   libnetclient_include,
                                                   libimap_include],
                            install             : false)

subdir('imap')
