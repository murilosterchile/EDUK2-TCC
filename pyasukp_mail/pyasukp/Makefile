#**************************************************************************#
#                                  PYAsUKP                                #
#    PYAsUKP: Yet Another solver (for the) Unbounded Knapsack Problem     #
#           Vincent Poirriez with Nicola Yanev and Rumen Andonov          #
#                          LAMIH-ROI UMR CNRS 8530                        #
#  Copyright 1998-2005  Vincent Poirriez				   #
# <vincent Dot poirriez At univ-valenciennes Dot fr>                      #
#  This file is part of PYAsUKP.                                          #
#									   #
#  PYAsUKP is free software; you can redistribute it and/or modify        #
#  it under the terms of the GNU General Public License as published by   #
#    the Free Software Foundation; either version 2 of the License, or	   #
#    (at your option) any later version.				   #
#    PYAsUKP is distributed in the hope that it will be useful,	   #
#    but WITHOUT ANY WARRANTY; without even the implied warranty of	   #
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the	   #
#    GNU General Public License for more details.			   #
#									   #
#  You should have received a copy of the GNU General Public License	   #
#  along with Foobar; if not, write to the Free Software		   #
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA#
#**************************************************************************#


# $Id: Makefile,v 1.10 2005/04/28 16:34:45 poirriez Exp $ 

#MAKEFILE to build Eduk

TITLE=pyasukp
INTRO=#Readmecaml
DOCFILE=$(TITLE)Doc
DOCDIR=doc

#The directory where are the sources
CURRENTDIR=pyasukp

#Configuration part depending upon system used
CAMLLIBDIR= /usr/lib/ocaml/3.08

CCUNIXLIB=
CCPTHREADSLIB=
CCGRAPHICSLIB=

#END OF USER CONFIGURATION######################################

#the compilers to use
CAMLCI=ocamlc
CAMLCO=ocamlc
#to pbuild profiled byte code
#CAMLCO=ocamlcp -p film

CAMLCFLAGS = -g
CC = gcc


CAMLOPT=ocamlopt
CAMLDEP=ocamldep
CAMLLEX = ocamllex
CAMLYACC= ocamlyacc
CAMLDOC= ocamldoc
#CAMLDOCFLAGS= -t $(TITLE) -intro $(INTRO) -hide Pervasives
CAMLDOCFLAGS= -t $(TITLE)  -hide Pervasives


#libraries and compilation flags
INCLUDES=
CAMLFLAGS=$(INCLUDES) 
CAMLOPTFLAGS=$(INCLUDES) -unsafe -inline 2048 
WITHGRAPHICS =graphics.cma -cclib -lgraphics -cclib -L/usr/X11R6/lib -cclib -lX11

WITHUNIX = unix.cma -cclib -lunix

WITHSTR = str.cma -cclib -lstr

WITHNUMS = nums.cma -cclib -lnums

WITHTHREADS = threads.cma -cclib -lthreads

WITHDBM = dbm.cma -cclib -lmldbm -cclib -lndbm


#Executable name
EXEC= $(TITLE)

EXAMPLES= $(CURRENTDIR)/example1.ukp $(CURRENTDIR)/ex0nds.ukp $(CURRENTDIR)/ex1nsd.ukp
INTERF= eduk.mli edukio.mli datagen.mli sequences.mli datastruct.mli  chainlist.mli select.mli init.mli


COMMONSOURCES= wintpint.ml wintpfloat.ml wandp.ml astore.ml globals.ml chainlist.ml edukio.ml eratosthen.ml datagen.ml  prioqueue.ml dominance.ml  prepro.ml select.ml sequences.ml printseq.ml datastructtype.ml parsecl.ml datastruct.ml  bounds.ml dynefflist.ml  bandbukp2.ml init.ml  slice.ml  eduk.ml   version.ml 

DOCSOURCES= myocamlweb.tex introduction.tex  dataspec.tex wandp.ml dominance.mli  dominance.ml corealgo.tex eduk.ml concl.tex appendix.tex listtools.mli listtools.ml  itemInfo.mli itemInfo.ml seq.mli seq.ml prioqueue.mli prioqueue.ml  redblacktree.mli redblacktree.ml select.mli select.ml edukio.mli edukio.ml datagen.mli datagen.ml  globals.ml parsecl.ml maineduk.ml version.ml -impl Readme code2c.tex export2c.ml stubc.tex


CSOURCES1=$(COMMONSOURCES:.mly=.ml)
CSOURCES2=$(CSOURCES1:.mll=.ml)

SOURCEST=$(CSOURCES2) timing.ml mainedukt.ml

BCTOBJS=$(SOURCEST:.ml=.cmo)
TOBJS=$(SOURCEST:.ml=.cmx)

FORCOBJS=$(COMMONSOURCES:.ml=.cmx)

#to build byte-code with timing
bct: $(CINTF)  $(BCTOBJS)
	$(CAMLCO) $(CAMLCFLAGS) -o $(EXEC)bct $(CUSTOM) $(LIBS) $(WITHUNIX) $(WITHNUMS) $(CAMLFLAGS) $(BCTOBJS)



#to build executable with timing
edukt: optt

optt:  $(TOBJS)
	$(CAMLOPT) -o $(EXEC)t $(CAMLOPTFLAGS) $(WITHUNIX:.cma=.cmxa) $(WITHNUMS:.cma=.cmxa) $(LIBS:.cma=.cmxa) $(TOBJS)


#To build the data generator

ukpdatgen: wintpint.cmo wintpfloat.cmo wandp.cmo datagen.cmo maingenuk.cmo 
	$(CAMLCO) -o ukpdatgen wintpint.cmo wintpfloat.cmo wandp.cmo datagen.cmo  maingenuk.cmo


#To build pyasukp as a c function

forc: $(FORCOBJS) export2c.cmx
	ocamlopt -output-obj -o $(EXEC)caml.o $(WITHNUMS:.cma=.cmxa) $(FORCOBJS) export2c.cmx
lib.a: forc
	ocamlopt -I $(CAMLLIBDIR) -c stubeduk2c.c
	cp $(CAMLLIBDIR)/libasmrun.a $(EXEC).a
	ar r $(EXEC).a  $(EXEC)caml.o stubeduk2c.o
asc: lib.a
	$(CC) -o $(EXEC)c main.c $(EXEC).a -lm -L/usr/lib/ocaml/3.08/ -lnums  -ldl



#to build documentation
doc: tex dvi ps html

dot:
	ocamldoc -dot $(SOURCEST)  -o $(TITLE).dot

inserttitle: inserttitle.cmo
	$(CAMLCO) -o inserttitle inserttitle.cmo

tex: bct
	$(CAMLDOC) -latex $(CAMLDOCFLAGS) $(INTERF) $(SOURCEST) -d $(DOCDIR)/tex -o $(DOCDIR)/tex/$(DOCFILE).tex 

dvi: doc/tex/$(DOCFILE).tex
	cd doc/tex;latex $(DOCFILE).tex ;bibtex $(DOCFILE) ;latex $(DOCFILE).tex ;latex $(DOCFILE).tex 

ps: doc/tex/$(DOCFILE).dvi
	dvips -o doc/tex/$(DOCFILE).ps doc/tex/$(DOCFILE).dvi

html: bct
	$(CAMLDOC) -html $(CAMLDOCFLAGS) $(INTERF) $(SOURCEST) -d $(DOCDIR)/html  -colorize-code

tgz: clean
	 cd .. && tar cvfz $(EXEC).tgz $(CURRENTDIR)/*.ml $(CURRENTDIR)/*.mli $(CURRENTDIR)/Readme $(CURRENTDIR)/Makefile $(CURRENTDIR)/LICENSE $(CURRENTDIR)/entete.txt $(EXAMPLES) ;cd $(CURRENTDIR)/

.depend:$(SOURCES) $(SOURCEST)
	$(CAMLDEP) *.mli *.ml > .depend

depend:$(SOURCES) $(SOURCEST)
	$(CAMLDEP) *.mli *.ml > .depend

spcase:
	$(CAMLOPT) -o specase2 wintpint.cmx wintpfloat.cmx wandp.cmx astore.cmx globals.cmx chainlist.cmx edukio.cmx datagen.cmx  prioqueue.cmx dominance.cmx redblacktree.cmx select.cmx seq.cmx printseq.cmx datastruct.cmx bounds.cmx init.cmx repartvbound.ml

.SUFFIXES:
.SUFFIXES: .ml .mli .cmo .cmi .cmx .mll .mly

.ml.cmo:
	$(CAMLCO) $(CAMLCFLAGS) -c $<

.mli.cmi:
	$(CAMLCI) -c $<

.ml.cmx:
	$(CAMLOPT) -c $(CAMLOPTFLAGS) $<


.mll.cmo:
	$(CAMLLEX) $<
	$(CAMLCO) -c $*.ml

.mll.cmx:
	$(CAMLLEX) $<
	$(CAMLOPT) -c $*.ml

.mly.cmo:
	$(CAMLYACC) $<
	$(CAMLCO) -c $*.mli
	$(CAMLCO) -c $*.ml

.mly.cmx:
	$(CAMLYACC) $<
	$(CAMLOPT) -c $*.mli
	$(CAMLOPT) -c $*.ml

.mly.cmi:
	$(CAMLYACC) $<
	$(CAMLCI) -c $*.mli

.mll.ml:
	$(CAMLLEX) $<

.mly.ml:
	$(CAMLYACC) $<

clean:
	rm -f *.cm[iox] *~ .*~  *.o *.aux *.log \#*\# *.dot ;\
	cd doc/html; rm -f  *~ .*~  *.o *.aux *.log \#*\# *.html *.dot;\
	cd ../tex; rm -f  *~ .*~  *.o *.aux *.log \#*\# *.tex *.dvi *.sty *.toc

allclean: clean
	rm -f $(EXEC)bct $(EXEC)t

alldepend:: depend

include .depend

