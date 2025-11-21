# SQLite extensions for Physical Unclonable Functions

The code provided here is a small extension for SQLite to compute metrics related to Physical Unclonable Functions.

The data needs to be stored in BLOBs. All BLOBs should have the same size. As of now there is no check when inserting the data but there are some sanity checks in the functions.

The extension is compiled to a shared library through the Makefile. The header `sqlite3ext.h` is needed. Is available through the `libsqlite3-dev` package, and `sqlite-devel` in Fedora.

```bash
$ make
gcc -shared -lm -fPIC -o sqlite_puf.so sqlite_puf.c
```

The script `test.py` contains a Python example on how to load the extension and test them on generated data.

It is still possible to load the extension in a SQL query by following the SQLite documentation on [Run-Time Loadable Extensions](https://sqlite.org/loadext.html)
