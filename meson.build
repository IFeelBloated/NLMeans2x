project('NLMeans2x',
        'cpp',
        default_options : ['cpp_std=c++14', 'buildtype=release'],
        license : 'GPL-3.0',
        version : '1')

# Dependencies
vapoursynth = dependency('vapoursynth')

# Sources
src = [
    'src/Source.cpp',
    'src/Helpers.hpp']

# Libs
library(
    'nlmeans2x',
    src,
    dependencies : vapoursynth,
    install_dir : join_paths(get_option('prefix'), get_option('libdir'), 'vapoursynth'),
    install : true
)
