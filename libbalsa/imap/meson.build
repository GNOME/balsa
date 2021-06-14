# libbalsa/imap/meson.build

libimap_a_sources = [
  'auth-cram.c',
  'auth-gssapi.c',
  'imap-auth.c',
  'imap-auth.h',
  'imap-commands.c',
  'imap-commands.h',
  'imap_compress.c',
  'imap_compress.h',
  'imap-handle.c',
  'imap-handle.h',
  'imap_search.c',
  'imap_search.h',
  'imap-tls.c',
  'imap_private.h',
  'libimap.h',
  'siobuf-nc.h',
  'util.c',
  'util.h'
  ]

libimap_link_arg    = '-lz'
libimap_compile_dep  = declare_dependency(link_args    : libimap_link_arg)

libimap_a = static_library('imap', libimap_a_sources,
                           c_args              : '-DG_LOG_DOMAIN="imap"',
                           dependencies        : balsa_deps + [libimap_compile_dep],
                           include_directories : [top_include, libnetclient_include],
                           install             : false)

imap_tst_sources = 'imap_tst.c'

imap_tst = executable('imap_tst', imap_tst_sources,
                      link_with           : [libimap_a, libnetclient_a],
                      dependencies        : balsa_deps,
                      include_directories : [top_include, libnetclient_include],
                      install             : false)
