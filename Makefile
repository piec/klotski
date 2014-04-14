#CC=clang
CFLAGS+=-std=gnu99 -Wall
#CFLAGS+=-O0 -g -ggdb2 -DDEBUG
CFLAGS+=-O3 -DDEBUG

CFLAGS+=$(shell pkg-config --cflags glib-2.0)
LDFLAGS+=$(shell pkg-config --libs glib-2.0)

LEVEL=levels/red_donkey.klo

all: klo

klo: klo.o

run: klo
	time ./klo $(LEVEL)

valgrind: klo
	valgrind ./klo $(LEVEL)

gdb: klo
	gdb --args ./klo $(LEVEL)

clean:
	rm -f *.o klo

zip:
	mkdir -p klotski/
	cd klotski && ln -sf ../klo.c && ln -sf ../Makefile
	zip -r "klotski-$(shell date +'%Y-%m-%d_%Hh%M').zip" klotski/

.PHONY: all clean run valgrind gdb zip
