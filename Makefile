#!/usr/bin/make -f


#MYFLAGS=-O3 -Oz -DNDEBUG
MYFLAGS=-g -fsanitize=address -fno-omit-frame-pointer

CXX=clang++
CXXFLAGS= `llvm-config --cxxflags` -std=c++17 $(MYFLAGS)

LD=clang++
LDFLAGS=`llvm-config --cxxflags --ldflags --libs all --system-libs` $(MYFLAGS) -rdynamic


#----------------------------------------------------------------------
all: voidc

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $<


OBJS = \
voidc_ast.o \
voidc_types.o \
voidc_target.o \
voidc_util.o \
voidc_main.o \
voidc_quark.o \
voidc_visitor.o \
voidc_compiler.o \
vpeg_parser.o \
vpeg_grammar.o \
vpeg_context.o \
vpeg_voidc.o \


voidc: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o voidc


#----------------------------------------------------------------------
clean:
	rm -f *.o
	rm -f *.voidc*
	rm -f llvm-c/*.voidc*
	rm -f llvm-c/*/*.voidc*
	rm -f voidc
	rm -f .depend
	rm -f hello


#----------------------------------------------------------------------
.depend: $(wildcard *.cpp *.h) Makefile
	$(CXX) $(CXXFLAGS) -MM *.cpp >>.depend

include .depend

