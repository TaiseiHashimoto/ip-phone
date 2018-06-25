CC     	= gcc
CFLAGS 	= `pkg-config --cflags gtk+-3.0 gmodule-2.0`
LIBS   	= `pkg-config --libs gtk+-3.0 gmodule-2.0` -lportaudio -lm
WALL  	= -Wall
BINDIR 	= bin
PROGRAM = $(BINDIR)/phone_v4
SOURCES = $(wildcard *.c)
OBJECTS = $(addprefix $(BINDIR)/, $(SOURCES:.c=.o))
HEADER  = phone.h

.PHONY: clean

all: $(PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CC) $(WALL) -o $@ $^ $(LIBS)

$(BINDIR)/%.o: %.c $(HEADER)
	$(CC) $(WALL) $(CFLAGS) -o $@ -c $<

clean:
	rm -f $(BINDIR)/*.o $(PROGRAM)
