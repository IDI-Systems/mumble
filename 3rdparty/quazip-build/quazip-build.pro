include(../../qmake/compiler.pri)

BUILDDIR=$$basename(PWD)
SOURCEDIR=$$replace(BUILDDIR,-build,-src)

!exists(../$$SOURCEDIR/README.md) {
  message("The $$SOURCEDIR/ directory was not found. Please update your submodules (git submodule update --init).")
  error("Aborting configuration")
}

TEMPLATE = lib
CONFIG += qt
CONFIG += debug_and_release
CONFIG += staticlib
QT -= gui
VPATH = ../$$SOURCEDIR
TARGET = quazip
INCLUDEPATH *= ../$$SOURCEDIR/quazip
INCLUDEPATH *= ../zlib-src

DEFINES += QUAZIP_BUILD
DEFINES += QUAZIP_STATIC

!CONFIG(third-party-warnings) {
  # We ignore warnings in third party builds. We won't actually look
  # at them and they clutter out our warnings.
  CONFIG -= warn_on
  CONFIG += warn_off
}

include(../$$SOURCEDIR/quazip/quazip.pri)
