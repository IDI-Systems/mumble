include(../../qmake/compiler.pri)

BUILDDIR=$$basename(PWD)
SOURCEDIR=$$replace(BUILDDIR,-build,-src)

!exists(../$$SOURCEDIR/README) {
  message("The $$SOURCEDIR/ directory was not found. Please update your submodules (git submodule update --init).")
  error("Aborting configuration")
}

TEMPLATE = lib
CONFIG -= qt
CONFIG += debug_and_release
CONFIG += no_include_pwd
VPATH = ../$$SOURCEDIR
TARGET = z

CONFIG += staticlib

!CONFIG(third-party-warnings) {
  # We ignore warnings in third party builds. We won't actually look
  # at them and they clutter out our warnings.
  CONFIG -= warn_on
  CONFIG += warn_off
}

INCLUDEPATH += ../$$SOURCEDIR

msvc {
	DEFINES += _CRT_SECURE_NO_DEPRECATE
	DEFINES += _CRT_NONSTDC_NO_DEPRECATE
}

SOURCES += $$files(../$$SOURCEDIR/*.c, false)
HEADERS += $$files(../$$SOURCEDIR/*.h, false)
