CC = gcc
PARSER_LIB_PATH = /afs/cs.cmu.edu/academic/class/15213-f22/www/labs/proxylab
CFLAGS = -g -O2 -std=c99 -Wall -Werror -Wextra -D_FORTIFY_SOURCE=2 -D_XOPEN_SOURCE=700 -I..
# This flag includes the Pthreads library on a Linux box.
# Others systems will probably require something different.
LDLIBS = -lpthread
LDLIBS += -Wl,-rpath,$(PARSER_LIB_PATH)
LDLIBS += -L$(PARSER_LIB_PATH) -lhttp_parser

FILES = tiny tiny-static cgi-bin/adder

all: $(FILES)

tiny: tiny.c csapp.o
tiny-static: tiny-static.c csapp.o
cgi-bin/adder: cgi-bin/adder.c

tar:
	(cd ..; tar cvf tiny.tar tiny)

clean:
	rm -f *.o *~ $(FILES)
