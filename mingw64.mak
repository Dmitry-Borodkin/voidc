
MYFLAGS=
#MYFLAGS=-fsanitize=address -fno-omit-frame-pointer


CC=clang
#CC=gcc
CFLAGS=-g `llvm-config --cflags` $(MYFLAGS)

CXX=clang++
#CXX=g++
CXXFLAGS=-g `llvm-config --cxxflags` -std=c++17 $(MYFLAGS) -frtti

LD=clang++
#LD=g++
#LDFLAGS=`llvm-config --cxxflags --ldflags --libs all --system-libs` $(MYFLAGS)
LDFLAGS= -fuse-ld=lld `llvm-config --ldflags --libs all --system-libs`


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
	rm -f llvm-c/*.voidc
	rm -f voidc
	rm -f .depend

#   rm -f voidc.c
#   rm -f voidc.h


#----------------------------------------------------------------------
.depend: $(wildcard *.cpp *.h) voidc.c mingw64.mak
	$(CC) $(CFLAGS) -MM *.c >.depend
	$(CXX) $(CXXFLAGS) -MM *.cpp >>.depend

include .depend

