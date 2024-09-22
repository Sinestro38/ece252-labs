# Compiler  and flags
CC = gcc
CFLAGS = -Wall -I/usr/include/libxml2 -std=gnu99 -DDEBUG1_
LDFLAGS = -lxml2 -lcurl -lz -pthread

# Object files for findpng3 and other necessary files
FINDPNG2_OBJS = findpng3.o queue.o http_utils.o
TIMING_OBJS = timing.o

# Dependency files
DEPS = http_utils.d queue.d findpng3.d

# Target executable names
TARGETS = findpng3 timing

# Rules
all: $(TARGETS)

findpng3: $(FINDPNG2_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

timing: $(TIMING_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.d: %.c
	$(CC) -MM $< > $@

-include $(DEPS)

clean:
	rm -f $(FINDPNG2_OBJS) $(TIMING_OBJS) $(DEPS) $(TARGETS)

.PHONY: clean all
