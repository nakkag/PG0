/*
 * PG0 library
 *
 * io_funcs.h
 *
 * Functions of the io library, one LIB_FUNC(name) per function (keep in sync with io.def).
 * Included by lib_static.c with LIB_FUNC defined; no include guard on purpose.
 */

LIB_FUNC(println)
LIB_FUNC(wait)
LIB_FUNC(savevalue)
LIB_FUNC(loadvalue)
LIB_FUNC(removevalue)
LIB_FUNC(get_clipboard)
LIB_FUNC(set_clipboard)
