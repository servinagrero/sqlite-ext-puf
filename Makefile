CC = gcc
CFLAGS = -Wall -Wpedantic -O2 -fPIC -I./include

SRCDIR = ./src
OBJDIR = ./obj

OBJS = $(OBJDIR)/functions.o $(OBJDIR)/sqlite_wrapper.o

all: sqlite_puf.so

sqlite_puf.so: $(OBJS)
	$(CC) -shared -fPIC -flto -o $@ $^ -lm

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -flto -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

format:
	find . -regex '.*\.[hc]' -exec clang-format --verbose -i --style=file --sort-includes {} \;

clean:
	rm -rf $(OBJDIR) sqlite_puf.so

.PHONY: format clean
