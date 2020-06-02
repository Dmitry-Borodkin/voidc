
MYFLAGS=-fcxx-exceptions -IC:/Tools/include
#MYFLAGS=-fsanitize=address -fno-omit-frame-pointer


CXX=clang++
#CXX=g++
#CXXFLAGS=-g `llvm-config --cxxflags` -std=c++17 $(MYFLAGS) -frtti
CXXFLAGS=-O3 `llvm-config --cxxflags` -std=c++17 $(MYFLAGS) -frtti

LD=clang++
#LD=g++
#LDFLAGS=`llvm-config --cxxflags --ldflags --libs all --system-libs` $(MYFLAGS)
#LDFLAGS= -fuse-ld=lld `llvm-config --ldflags --libs all --system-libs`
LDFLAGS= -fuse-ld=lld `llvm-config --ldflags --system-libs` -lLLVM -rdynamic


#----------------------------------------------------------------------
all: voidc.exe

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $<


OBJS = \
voidc_ast.o \
voidc_llvm.o \
voidc_util.o \
voidc_main.o \
vpeg_parser.o \
vpeg_grammar.o \
vpeg_context.o \
vpeg_voidc.o \


voidc.exe: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o voidc


#----------------------------------------------------------------------
clean:
	rm -f *.o
	rm -f *.voidc
	rm -f llvm-c/*.voidc
	rm -f llvm-c/*/*.voidc
	rm -f voidc.exe
	rm -f .depend
	rm -f hello.exe


#----------------------------------------------------------------------
.depend: $(wildcard *.cpp *.h) mingw64.mak
	$(CXX) $(CXXFLAGS) -MM *.cpp >>.depend

include .depend

