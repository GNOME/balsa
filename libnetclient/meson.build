# libnetclient/test/meson.build

libnetclient_a_sources = [
  'net-client.c',
  'net-client.h',
  'net-client-pop.c',
  'net-client-pop.h',
  'net-client-smtp.c',
  'net-client-smtp.h',
  'net-client-utils.c',
  'net-client-utils.h'
  ]

libnetclient_a = static_library('netclient', libnetclient_a_sources,
                                c_args              : '-DG_LOG_DOMAIN="libnetclient"',
                                dependencies        : libnetclient_deps,
                                include_directories : top_include,
                                install             : false)

if libnetclient_docs
  doxygen = doxygen_program.path()
  dox_path = join_paths(meson.source_root(), meson.current_source_dir(), 'libnetclient.dox')
  run_command(doxygen, dox_path)
endif

subdir('test')
