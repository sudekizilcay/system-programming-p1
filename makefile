CC       := gcc
CFLAGS   := -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude -Wall -Wextra -Werror -g
LDFLAGS  :=
LDLIBS   :=

TARGET        := watchdog
WORKER_TARGET := workers/worker

SRCS := \
	src/main.c \
	src/signal_manager.c \
	src/process_manager.c \
	src/logger.c

OBJS := $(SRCS:.c=.o)

WORKER_SRC := workers/worker.c
WORKER_OBJ := $(WORKER_SRC:.c=.o)

.PHONY: all clean test

all: $(TARGET) $(WORKER_TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(WORKER_TARGET): $(WORKER_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

workers/%.o: workers/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: all
	bash tests/test_scenarios.sh

clean:
	rm -f $(OBJS) $(WORKER_OBJ) $(TARGET) $(WORKER_TARGET) watchdog.log
