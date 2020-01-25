#!/usr/bin/make -f


#MYFLAGS=-O3
MYFLAGS=-g -fsanitize=address -fno-omit-frame-pointer


CC=clang
CFLAGS=`llvm-config --cflags` $(MYFLAGS)

CXX=clang++
CXXFLAGS= `llvm-config --cxxflags` -std=c++17 $(MYFLAGS)

LD=clang++
LDFLAGS=`llvm-config --cxxflags --ldflags --libs all --system-libs` $(MYFLAGS)


#----------------------------------------------------------------------
all: voidc

voidc.c: voidc.pegc
	packcc voidc.pegc


%.o: %.c
	$(CC) $(CFLAGS) -c $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $<


OBJS = \
voidc.o \
voidc_ast.o \
voidc_llvm.o \
voidc_main.o \


voidc: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o voidc


#----------------------------------------------------------------------
clean:
	rm -f *.o
	rm -f *.voidc
	rm -f voidc
	rm -f voidc.c
	rm -f voidc.h
	rm -f .depend


#----------------------------------------------------------------------
.depend: $(wildcard *.cpp *.h) voidc.c Makefile
	$(CC) $(CFLAGS) -MM *.c >.depend
	$(CXX) $(CXXFLAGS) -MM *.cpp >>.depend

include .depend

