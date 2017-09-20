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

libimap_compile_args = ['-I' + meson.build_root(),
                        '-I' + meson.current_build_dir(),
                        '-I' + meson.source_root(),
                        '-I' + join_paths(meson.source_root(), 'libbalsa'),
                        '-I' + join_paths(meson.source_root(), 'libnetclient'),
                        '-I' + join_paths(meson.source_root(), 'libbalsa', 'imap'),
                        '-ansi']
libimap_compile_dep  = declare_dependency(compile_args : libimap_compile_args)

# genmarshal
#

libimap_marshal_h = custom_target('libimap_marshal_h',
  input   : 'libimap-marshal.list',
  output  : 'libimap-marshal.h',
  command : [
    'sh', '-c',
    glib_genmarshal +
    ' --header' +
    ' --prefix=libimap' +
    ' @INPUT@' +
    ' > libbalsa/imap/libimap-marshal.h'
  ]
)

libimap_marshal_c = custom_target('libimap_marshal_c',
  input   : 'libimap-marshal.list',
  output  : 'libimap-marshal.c',
  command : [
    'sh', '-c',
    'echo \'#include "libimap-marshal.h"\' > libbalsa/imap/libimap-marshal.c && ' +
    glib_genmarshal +
    ' --body' +
    ' --prefix=libimap' +
    ' @INPUT@' +
    ' >> libbalsa/imap/libimap-marshal.c'
  ]
)

libimap_a = static_library('imap', libimap_a_sources,
                           libimap_marshal_c, libimap_marshal_h,
                           dependencies : balsa_deps + [libimap_compile_dep],
                           install      : false)

imap_tst_sources = 'imap_tst.c'

imap_tst = executable('imap_tst', imap_tst_sources,
                      link_with    : [libimap_a, libnetclient_a],
                      dependencies : balsa_deps,
                      install      : false)
