/*
 * PG0 library
 *
 * string_funcs.h
 *
 * Functions of the string library, one LIB_FUNC(name) per function (keep in sync with string.def).
 * Included by lib_static.c with LIB_FUNC defined; no include guard on purpose.
 */

LIB_FUNC(trim)
LIB_FUNC(to_lower)
LIB_FUNC(to_upper)
LIB_FUNC(str_match)
LIB_FUNC(substring)
LIB_FUNC(in_string)
LIB_FUNC(split)
