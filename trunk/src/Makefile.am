## Process this file with automake to produce Makefile.in

INCLUDES = \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	@PACKAGE_CFLAGS@ @MTP_CFLAGS@

bin_PROGRAMS = mtpsync

mtpsync_SOURCES = \
	main.c main.h \
	id3read.c id3read.h \
	support.c support.h \
	interface.c interface.h \
	callbacks.c callbacks.h

mtpsync_LDADD = @PACKAGE_LIBS@ @MTP_LIBS@ $(INTLLIBS) -lreadline

