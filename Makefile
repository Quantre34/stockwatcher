CC      = clang
CFLAGS  = -Wall -Wextra -O2 -std=c11
LIBS    = $(shell pkg-config --libs libcurl 2>/dev/null || echo -lcurl)
INCS    = $(shell pkg-config --cflags libcurl 2>/dev/null)

TARGET  = stockwatcher
SRCS    = main.c scanner.c keywords.c notifier.c zara.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) scanner

install: all
	cp $(TARGET) /usr/local/bin/stockwatcher
	@echo "Kuruldu: /usr/local/bin/stockwatcher"
