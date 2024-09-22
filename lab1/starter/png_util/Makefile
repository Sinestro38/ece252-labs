# Makefile, ECE252  
# Yiqing Huang

CC = gcc       # compiler
CFLAGS = -Wall -g -std=c99 # compilation flags
LD = gcc       # linker
LDFLAGS = -g   # debugging symbols in build
LDLIBS = -lz   # link with libz

# Directories
OBJDIR = _tmp
DEPDIR = _tmp

# For students 
LIB_UTIL = $(OBJDIR)/zutil.o $(OBJDIR)/crc.o
SRCS   = main.c crc.c zutil.c pnginfo.c find_png.c
OBJS   = $(OBJDIR)/main.o $(OBJDIR)/find_png.o $(LIB_UTIL)

TARGETS = main.out findpng.out test_pnginfo.out

all: $(TARGETS)

main.out: $(OBJDIR)/main.o $(LIB_UTIL)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

findpng.out: $(OBJDIR)/find_png.o $(OBJDIR)/pnginfo.o $(LIB_UTIL)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

test_pnginfo.out: $(OBJDIR)/test_pnginfo.o $(OBJDIR)/pnginfo.o $(LIB_UTIL)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -I. -c $< -o $@

$(DEPDIR)/%.d: %.c | $(DEPDIR)
	$(CC) -MM -MF $@ -I. $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(DEPDIR):
	mkdir -p $(DEPDIR)

-include $(SRCS:%.c=$(DEPDIR)/%.d)

.PHONY: clean
clean:
	rm -rf $(OBJDIR) $(TARGETS)
