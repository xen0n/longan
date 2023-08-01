project(
  'longan',
  'c',
  version: '0.0.1',
  license: 'GPL-3.0-or-later',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
  meson_version: '>=1.0.0',
)

deps = [
]

cflags = [
  '-D_GNU_SOURCE',
  '-D_XOPEN_SOURCE=500',
]
add_project_arguments(cflags, language: 'c')

# main executable
longan = executable(
  'longan',

  'src/main.c',

  dependencies: deps,
  install: true,
)
