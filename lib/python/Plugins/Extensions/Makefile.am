installdir = $(pkglibdir)/python/Plugins/Extensions

SUBDIRS = LDteam OpenIPTV TuxboxPlugins CutListEditor PicturePlayer MediaScanner MediaPlayer SocketMMI DVDBurn Modem Volume_adjust RCUSelect

if HAVE_LIBDDVD
SUBDIRS += DVDPlayer
endif

if HAVE_WETEK_XBMC
SUBDIRS +=  StartKodi
endif

install_PYTHON = __init__.py
