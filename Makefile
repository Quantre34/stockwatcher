CC     = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
LIBS   = $(shell pkg-config --libs libcurl 2>/dev/null || echo -lcurl)
INCS   = $(shell pkg-config --cflags libcurl 2>/dev/null)

SRCS = main.c scanner.c keywords.c notifier.c zara.c
OBJS = $(SRCS:.c=.o)

ifeq ($(OS),Windows_NT)
    TARGET  = stockwatcher.exe
    PFLAGS  = -lws2_32
    INSTALL = @echo "Kurulum: stockwatcher.exe dosyasini PATH icindeki bir klasore kopyalayin."
else
    CC      = clang
    TARGET  = stockwatcher
    PFLAGS  =
    INSTALL = mkdir -p /usr/local/bin && cp $(TARGET) /usr/local/bin/stockwatcher && echo "Kuruldu: /usr/local/bin/stockwatcher"
endif

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS) $(PFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

clean:
	rm -f $(OBJS) stockwatcher stockwatcher.exe scanner

install: all
	$(INSTALL)
