/**
 * Copyright (C) 2005-2007 Christoph Rupp (chris@crupp.de).
 * All rights reserved. See file LICENSE for licence and copyright
 * information.
 *
 * this file describes the configuration of hamster - serial number, 
 * enabled features etc. 
 *
 */

#ifndef CONFIG_H__
#define CONFIG_H__

#include <ham/types.h>

/*
 * include autoconf header file; otherwise, assume sane default values
 */
#ifdef HAVE_CONFIG_H
#   include "../config.h"
#else
#	if UNDER_CE
#		define HAVE_MMAP                0
#		define HAVE_UNMMAP              0
#	else
#		define HAVE_MMAP                1
#		define HAVE_UNMMAP              1
#	endif
#   define HAVE_PREAD                   1
#   define HAVE_PWRITE                  1
#endif

/*
 * check for a valid build
 */
#if (!defined(HAM_DEBUG))
#   if (defined(_DEBUG) || defined(DEBUG))
#       define HAM_DEBUG 1
#   endif
#endif

/* 
 * the serial number; for non-commercial versions, this is always
 * 0x0; commercial versions get a serial number from the vendor
 */
#define HAM_SERIALNO               0x0

/* 
 * the endian-architecture of the host computer; set this to 
 * HAM_LITTLE_ENDIAN or HAM_BIG_ENDIAN 
 */
#ifndef HAM_LITTLE_ENDIAN
#   ifndef HAM_BIG_ENDIAN
#       error "neither HAM_LITTLE_ENDIAN nor HAM_BIG_ENDIAN defined"
#   endif
#endif

/*
 * feature list; describes the features that are enabled or 
 * disabled
 */
#define HAM_HAS_BTREE              1
#define HAM_HAS_HASHDB             1

/*
 * the default cache size is 256
 */
#define HAM_DEFAULT_CACHESIZE      (1024*256)


#endif /* CONFIG_H__ */
