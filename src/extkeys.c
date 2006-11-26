/**
 * Copyright (C) 2005, 2006 Christoph Rupp (chris@crupp.de)
 * see file LICENSE for licence information
 *
 */

#include <string.h>
#include <ham/hamsterdb.h>
#include "extkeys.h"
#include "db.h"
#include "mem.h"
#include "error.h"

#define EXTKEY_CACHE_BUCKETSIZE         128

extkey_cache_t *
extkey_cache_new(ham_db_t *db)
{
    extkey_cache_t *c;
    int memsize;

    memsize=sizeof(extkey_cache_t)+(EXTKEY_CACHE_BUCKETSIZE-1)*sizeof(void *);
    c=(extkey_cache_t *)ham_mem_alloc(memsize);
    if (!c) {
        db_set_error(db, HAM_OUT_OF_MEMORY);
        return (0);
    }

    memset(c, 0, memsize);

    extkey_cache_set_db(c, db);
    extkey_cache_set_bucketsize(c, EXTKEY_CACHE_BUCKETSIZE);

    return (c);
}

void
extkey_cache_destroy(extkey_cache_t *cache)
{
    /*
     * DEBUG build: make sure that all entries are empty
     */
#ifndef HAM_RELEASE
    ham_size_t i;

    for (i=0; i<extkey_cache_get_bucketsize(cache); i++) {
        ham_assert(extkey_cache_get_bucket(cache, i)==0, 
                "extended key cache bucket %d is not empty!", (int)i);
    }
#endif

    ham_mem_free(cache);
}

#define my_calc_hash(cache, o)                                              \
    (extkey_cache_get_bucketsize(cache)==0                                  \
        ? 0                                                                 \
        : (((o)&(cache_get_bucketsize(cache)-1))))

ham_status_t
extkey_cache_insert(extkey_cache_t *cache, ham_offset_t blobid, 
            ham_size_t size, const ham_u8_t *data)
{
    ham_size_t h=my_calc_hash(cache, blobid);
    extkey_t *e;
    ham_db_t *db=extkey_cache_get_db(cache);

    /*
     * DEBUG build: make sure that the item is not inserted twice!
     */
#ifndef HAM_RELEASE
    e=extkey_cache_get_bucket(cache, h);
    while (e) {
        ham_assert(extkey_get_blobid(e)!=blobid, 
                "extkey (blob id %llu) is already in the cache!", 
                (unsigned long long)blobid);
        e=extkey_get_next(e);
    }
#endif

    /*
     * enough cache capacity to insert the key?
     */
    if (cache_get_usedsize(db_get_cache(db))+
            extkey_cache_get_usedsize(cache)+size >
            cache_get_cachesize(db_get_cache(db))) {
        return (HAM_CACHE_FULL);
    }

    e=(extkey_t *)ham_mem_alloc(SIZEOF_EXTKEY_T+size);
    if (!e)
        return (HAM_OUT_OF_MEMORY);
    extkey_set_blobid(e, blobid);
    extkey_set_next(e, extkey_cache_get_bucket(cache, h));
    extkey_set_size(e, size);
    memcpy(extkey_get_data(e), data, size);

    extkey_cache_set_bucket(cache, h, e);
    extkey_cache_set_usedsize(cache, extkey_cache_get_usedsize(cache)+size);

    return (0);
}

ham_status_t
extkey_cache_remove(extkey_cache_t *cache, ham_offset_t blobid)
{
    ham_size_t h=my_calc_hash(cache, blobid);
    extkey_t *e, *prev=0;

    e=extkey_cache_get_bucket(cache, h);
    while (e) {
        if (extkey_get_blobid(e)==blobid)
            break;
        prev=e;
        e=extkey_get_next(e);
    }

    if (!e)
        return (HAM_KEY_NOT_FOUND);

    if (prev)
        extkey_set_next(prev, extkey_get_next(e));
    else
        extkey_cache_set_bucket(cache, h, extkey_get_next(e));

    extkey_cache_set_usedsize(cache, 
            extkey_cache_get_usedsize(cache)-extkey_get_size(e));
    ham_mem_free(e);

    return (0);
}

ham_status_t
extkey_cache_fetch(extkey_cache_t *cache, ham_offset_t blobid, 
            ham_size_t *size, ham_u8_t **data)
{
    ham_size_t h=my_calc_hash(cache, blobid);
    extkey_t *e;

    e=extkey_cache_get_bucket(cache, h);
    while (e) {
        if (extkey_get_blobid(e)==blobid)
            break;
        e=extkey_get_next(e);
    }

    if (!e)
        return (HAM_KEY_NOT_FOUND);

    *size=extkey_get_size(e);
    *data=extkey_get_data(e);

    return (0);
}

