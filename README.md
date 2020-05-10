# hkd: hotkey deamon
hkd allows to define system-wide hotkeys, independently of graphical environment,
this means, that hotkeys will work in x11 waylnad and even tty, as long as the
actions specified can run you are good to go.

## Compatibility
hkd works using the evdev interface so the following systems work:
* linux
* FreeBSD
* dragonflyBSD

## Compiling
hkd doesn't require any libraries and has no dependency other than a standard C
library, `musl` should work.\
run `make` to compile, `make clean` to remove the mess and `make debug` to compile
a debug binary to use with gdb.

## Similar projects
[triggerhappy](https://github.com/wertarbyte/triggerhappy)
