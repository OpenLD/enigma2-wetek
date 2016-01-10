installdir = $(pkglibdir)/python/Plugins/Extensions

SUBDIRS = LDteam OpenIPTV TuxboxPlugins CutListEditor PicturePlayer MediaScanner MediaPlayer SocketMMI DVDBurn Modem Volume_adjust

if HAVE_LIBDDVD
SUBDIRS += DVDPlayer
endif

if WETEK
SUBDIRS += USBsafe RCUSelect
endif

if HAVE_WETEK_XBMC
SUBDIRS +=  StartKodi
endif

install_PYTHON = __init__.py
