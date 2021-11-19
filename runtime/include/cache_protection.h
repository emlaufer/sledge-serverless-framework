#pragma once

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "flush.h"

enum CACHE_PROTECTION
{
	CACHE_PROTECTION_NONE = 0,
	CACHE_PROTECTION_FLUSH  = 1
};

extern enum CACHE_PROTECTION cache_protection;

static inline char *
cache_protection_print(enum CACHE_PROTECTION variant)
{
	switch (variant) {
	case CACHE_PROTECTION_NONE:
		return "NONE";
	case CACHE_PROTECTION_FLUSH:
		return "FLUSH";
	}
}

/**
 * Called during a sandbox context switch to flush the cache. 
 * Requires the 'cool' kernel module to be installed on the system.
 */
static inline void
cache_protection_flush()
{
    switch (cache_protection) {
        case CACHE_PROTECTION_FLUSH:
#ifdef CACHE_FLUSH_LOG
            printf("Flushing the cache!\n");
#endif
            btb_flush();
            cflush();
            break;
        case CACHE_PROTECTION_NONE:
#ifdef CACHE_FLUSH_LOG
            printf("Skipping cache flush!\n");
#endif
            break;
    }
}
