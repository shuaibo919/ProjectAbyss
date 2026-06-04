#!/usr/bin/env python
import os
import sys

# You can find documentation for SCons and SConstruct files at:
# https://scons.org/documentation.html

# This lets SCons know that we're using godot-cpp, from the godot-cpp folder.
env = SConscript("godot-cpp/SConstruct")

# Force MSVC English output — avoids CP936 mojibake in UTF-8 terminals.
# Must go through scons ENV dict so child processes (cl.exe, link.exe) inherit it.
env['ENV']['VSLANG'] = '1033'

# Configures the 'src' directory as a source for header files.
env.Append(CPPPATH=["Source/"])

# Collects all .cpp files in the 'Source' folder as compile targets.
sources = Glob("Source/*.cpp") + Glob("Source/*/*.cpp")

# The filename for the dynamic library for this GDExtension.
# $SHLIBPREFIX is a platform specific prefix for the dynamic library ('lib' on Unix, '' on Windows).
# $SHLIBSUFFIX is the platform specific suffix for the dynamic library (for example '.dll' on Windows).
# env["suffix"] includes the build's feature tags (e.g. '.windows.template_debug.x86_64')
# (see https://docs.godotengine.org/en/stable/tutorials/export/feature_tags.html).
# The final path should match a path in the '.gdextension' file.
lib_filename = "{}abyss{}{}".format(env.subst('$SHLIBPREFIX'), env["suffix"], env.subst('$SHLIBSUFFIX'))

# Creates a SCons target for the path with our sources.
library = env.SharedLibrary(
    "Game/bin/{}".format(lib_filename),
    source=sources,
)

# Selects the shared library as the default target.
Default(library)
