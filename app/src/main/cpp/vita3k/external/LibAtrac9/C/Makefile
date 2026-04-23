NAME = libatrac9

CC = gcc
AR = ar
SFLAGS = -O2
CFLAGS = -Wall -Wextra -std=c99
SHARED_SFLAGS = $(SFLAGS) -flto
SHARED_CFLAGS = $(CFLAGS) -fPIC
LFLAGS = -shared -s -Wl,--version-script=libatrac9.version

SRCDIR = src
OBJDIR = obj
BINDIR = bin

STATIC_OBJDIR = $(OBJDIR)_static
SHARED_OBJDIR = $(OBJDIR)_shared
SRCS = $(wildcard $(SRCDIR)/*.c)
STATIC_OBJS = $(SRCS:$(SRCDIR)/%.c=$(STATIC_OBJDIR)/%.o)
SHARED_OBJS = $(SRCS:$(SRCDIR)/%.c=$(SHARED_OBJDIR)/%.o)

STATIC_NAME = $(BINDIR)/$(NAME).a
SHARED_NAME = $(BINDIR)/$(NAME).so

MKDIR = mkdir -p
RM = rm -f
RMDIR = rm -df

all: static shared
static: create_static_dir create_bin_dir $(STATIC_NAME)
shared: create_shared_dir create_bin_dir $(SHARED_NAME)	

create_static_dir:
	@$(MKDIR) $(STATIC_OBJDIR)
	
create_shared_dir:
	@$(MKDIR) $(SHARED_OBJDIR)
	
create_bin_dir:
	@$(MKDIR) $(BINDIR)

$(SHARED_NAME): $(SHARED_OBJS)
	$(CC) $(SHARED_OBJS) $(SHARED_SFLAGS) $(LFLAGS) -o $@

$(SHARED_OBJS): $(SHARED_OBJDIR)/%.o : $(SRCDIR)/%.c
	$(CC) $(SHARED_SFLAGS) $(SHARED_CFLAGS) -c $< -o $@

$(STATIC_NAME): $(STATIC_OBJS)
	$(AR) rcs $@ $^

$(STATIC_OBJS): $(STATIC_OBJDIR)/%.o : $(SRCDIR)/%.c
	$(CC) $(SFLAGS) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(SHARED_OBJS) $(SHARED_NAME) $(STATIC_OBJS) $(STATIC_NAME)
	-@$(RMDIR) $(STATIC_OBJDIR) $(SHARED_OBJDIR) $(BINDIR) 2>/dev/null || true

.PHONY: all static shared create_static_dir create_shared_dir create_bin_dir clean
