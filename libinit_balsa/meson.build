# libinit_balsa/meson.build

libinit_balsa_sources = [
  'assistant_page_defclient.c',
  'assistant_page_defclient.h',
  'assistant_page_directory.c',
  'assistant_page_directory.h',
  'assistant_page_finish.c',
  'assistant_page_finish.h',
  'assistant_page_user.c',
  'assistant_page_user.h',
  'assistant_page_welcome.c',
  'assistant_page_welcome.h',
  'assistant_init.c',
  'assistant_init.h',
  'assistant_helper.c',
  'assistant_helper.h'
  ]

libinit_balsa_compile_args = ['-I' + meson.build_root(),
                              '-I' + join_paths(meson.source_root(), 'libbalsa'),
                              '-I' + join_paths(meson.source_root(), 'libnetclient'),
                              '-I' + join_paths(meson.source_root(), 'src')]
libinit_balsa_compile_dep = declare_dependency(compile_args : libinit_balsa_compile_args)

libinit_balsa_a = static_library('init_balsa', libinit_balsa_sources,
                                 dependencies : balsa_deps + [libinit_balsa_compile_dep],
                                 install      : false)
