# Makefile, ECE252  
# Yiqing Huang

CC = gcc       # compiler
CFLAGS = -Wall -g -std=c99 # compilation flags
LD = gcc       # linker
LDFLAGS = -g   # debugging symbols in build
LDLIBS = -lz -pthread -lcurl  # link with libz

# Directories
OBJDIR = _tmp
DEPDIR = _tmp

# For students 
LIB_UTIL = $(OBJDIR)/zutil.o $(OBJDIR)/crc.o
SRCS   = paster.c crc.c zutil.c pnginfo.c findpng.c catpng.c
OBJS   = $(OBJDIR)/paster.o $(OBJDIR)/findpng.o $(OBJDIR)/catpng.o $(LIB_UTIL)

TARGETS = paster

all: $(TARGETS)

paster: $(OBJDIR)/paster.o $(OBJDIR)/catpng.o $(OBJDIR)/pnginfo.o $(LIB_UTIL)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

# findpng: $(OBJDIR)/findpng.o $(OBJDIR)/pnginfo.o $(LIB_UTIL)
# 	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

# test_pnginfo: $(OBJDIR)/test_pnginfo.o $(OBJDIR)/pnginfo.o $(LIB_UTIL)
# 	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

# catpng: $(OBJDIR)/catpng.o $(OBJDIR)/pnginfo.o $(LIB_UTIL)
# 	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)	

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
	rm -f *.png *.o

# # Makefile, ECE252  
# # Yiqing Huang

# CC = gcc       # compiler
# CFLAGS = -Wall -g -std=c99 # compilation flags
# LD = gcc       # linker
# LDFLAGS = -g   # debugging symbols in build
# LDLIBS = -lz -pthread -lcurl  # link with libz

# # Directories
# OBJDIR = _tmp
# DEPDIR = _tmp

# # For students 
# LIB_UTIL = $(OBJDIR)/zutil.o $(OBJDIR)/crc.o
# SRCS   = crc.c zutil.c pnginfo.c findpng.c catpng.c main.c
# OBJS   = $(OBJDIR)/main.o $(OBJDIR)/findpng.o $(OBJDIR)/catpng.o $(LIB_UTIL)

# TARGETS = findpng main.out test_pnginfo catpng 

# all: $(TARGETS)

# test_pnginfo.out: $(OBJDIR)/test_pnginfo.o $(OBJDIR)/pnginfo.o $(LIB_UTIL)
# 	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

# catpng.out: $(OBJDIR)/catpng.o $(OBJDIR)/pnginfo.o $(LIB_UTIL)
# 	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)	

# main.out: $(OBJDIR)/main.o $(LIB_UTIL)
# 	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS) -lz

# $(OBJDIR)/%.o: %.c | $(OBJDIR)
# 	$(CC) $(CFLAGS) -I. -c $< -o $@

# $(DEPDIR)/%.d: %.c | $(DEPDIR)
# 	$(CC) -MM -MF $@ -I. $<

# $(OBJDIR):
# 	mkdir -p $(OBJDIR)

# $(DEPDIR):
# 	mkdir -p $(DEPDIR)

# -include $(SRCS:%.c=$(DEPDIR)/%.d)

# .PHONY: clean
# clean:
# 	rm -rf $(OBJDIR) $(TARGETS)
