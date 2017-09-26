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
  'siobuf.c',
  'siobuf.h',
  'util.c',
  'util.h'
  ]

# genmarshal
#

if use_gnome_genmarshal
  libimap_marshal_files = gnome.genmarshal('libimap_marshal',
                                           sources : 'libimap-marshal.list')
else # use_gnome_genmarshal
  libimap_marshal_h = custom_target('libimap_marshal_h',
    input   : 'libimap-marshal.list',
    output  : 'libimap-marshal.h',
    command : [
      glib_genmarshal,
      '--header',
      '--prefix=libimap',
      '--output=@OUTPUT@',
      '@INPUT@'
    ]
  )

  libimap_marshal_c = custom_target('libimap_marshal_c',
    input   : 'libimap-marshal.list',
    output  : 'libimap-marshal.c',
    command : [
      glib_genmarshal,
      '--header',
      '--body',
      '--prefix=libimap',
      '--output=@OUTPUT@',
      '@INPUT@'
    ]
  )

  libimap_marshal_files = [libimap_marshal_c, libimap_marshal_h]
endif # use_gnome_genmarshal

libimap_compile_arg = '-ansi'
libimap_link_arg    = '-lz'
libimap_compile_dep  = declare_dependency(compile_args : libimap_compile_arg,
                                          link_args    : libimap_link_arg)

libimap_a = static_library('imap', libimap_a_sources, libimap_marshal_files,
                           dependencies        : balsa_deps + [libimap_compile_dep],
                           include_directories : [top_include, libnetclient_include],
                           install             : false)

imap_tst_sources = 'imap_tst.c'

imap_tst = executable('imap_tst', imap_tst_sources,
                      link_with    : [libimap_a, libnetclient_a],
                      dependencies : balsa_deps,
                      install      : false)
