#ifndef PROTO_OPENTLS_H
#define PROTO_OPENTLS_H
/* gfopentls.library. MIT licensed and free. Copyright (c) 2026 AmiCradle Team. */

#include <exec/types.h>
#include <exec/libraries.h>
#include <libraries/gfopentls.h>

#ifndef __NOLIBBASE__
extern struct Library *OpenTLSBase;
#endif

#if defined(__VBCC__)
#include <inline/gfopentls_protos.h>
#elif defined(__GNUC__)
#include <inline/gfopentls.h>
#else
#include <clib/gfopentls_protos.h>
#endif

#endif
