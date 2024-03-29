# Makefile.am
# Copyright (C) 2002 g10 Code GmbH, Klarälvdalens Datakonsult AB
# Copyright (C) 2008, 2015 g10 Code GmbH
#
# This file is part of PINENTRY.
#
# PINENTRY is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# PINENTRY is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses/>.
# SPDX-License-Identifier: GPL-2.0+

## Process this file with automake to produce Makefile.in

bin_PROGRAMS = pinentry-qt5

EXTRA_DIST = pinentryrc.qrc

SUBDIRS = icons


if FALLBACK_CURSES
ncurses_include = $(NCURSES_INCLUDE)
libcurses = ../pinentry/libpinentry-curses.a $(LIBCURSES) $(LIBICONV)
else
ncurses_include =
libcurses =
endif


AM_CPPFLAGS = $(COMMON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/secmem \
	$(ncurses_include) -I$(top_srcdir)/pinentry
AM_CXXFLAGS = $(PINENTRY_QT5_CFLAGS)
pinentry_qt5_LDADD = \
	../pinentry/libpinentry.a $(top_builddir)/secmem/libsecmem.a \
	$(COMMON_LIBS) $(PINENTRY_QT5_LIBS) $(libcurses)
pinentry_qt5_LDFLAGS = $(PINENTRY_QT5_LDFLAGS)

if BUILD_PINENTRY_QT5
BUILT_SOURCES = \
	pinentryconfirm.moc pinentrydialog.moc pinlineedit.moc capslock.moc \
	focusframe.moc \
	keyboardfocusindication.moc \
	pinentryrc.cpp
endif

CLEANFILES = $(BUILT_SOURCES)

if HAVE_W32_SYSTEM
pinentry_qt5_platform_SOURCES = capslock_win.cpp
else
pinentry_qt5_platform_SOURCES = capslock_unix.cpp
endif

pinentry_qt5_SOURCES = pinentrydialog.h pinentrydialog.cpp \
	main.cpp pinentryconfirm.cpp pinentryconfirm.h \
	pinlineedit.h pinlineedit.cpp capslock.cpp capslock.h capslock_p.h \
	pinentry_debug.cpp pinentry_debug.h util.h accessibility.cpp \
	accessibility.h qti18n.cpp pinentryrc.qrc \
	focusframe.h focusframe.cpp \
	keyboardfocusindication.h keyboardfocusindication.cpp \
	$(pinentry_qt5_platform_SOURCES)

nodist_pinentry_qt5_SOURCES = $(BUILT_SOURCES)

.h.moc:
	$(MOC5) `test -f '$<' || echo '$(srcdir)/'`$< -o $@

pinentryrc.cpp: pinentryrc.qrc
	$(RCC5) `test -f '$<' || echo '$(srcdir)/'`$< -o $@

desktopdir = $(datadir)/applications
desktop_DATA = org.gnupg.pinentry-qt5.desktop
