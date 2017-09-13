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

libnetclient_compile_args = '-I' + meson.build_root()
netclient_deps = declare_dependency(compile_args : libnetclient_compile_args,
                                    dependencies : libnetclient_deps)
libnetclient_a = static_library('netclient', libnetclient_a_sources,
                                dependencies : netclient_deps,
                                install      : false)

if with_libnetclient_docs
  doxygen = doxygen_program.path()
  dox_path = join_paths(meson.source_root(), meson.current_source_dir(), 'libnetclient.dox')
  run_command(doxygen, dox_path)
endif

subdir('test')
