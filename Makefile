CC = gcc

sqlite_puf.so: sqlite_puf.c
	$(CC) -shared -lm -fPIC -o $@ $^

format:
	find . -regex '.*\.[hc]' -exec clang-format --verbose -i --style=file --sort-includes {} \;

.PHONY: format
