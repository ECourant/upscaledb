/**
 * Copyright 2005, 2006 Christoph Rupp (chris@crupp.de)
 * see file LICENSE for license and copyright information
 *
 *
 */

#include <string.h>
#include <ham/hamsterdb.h>
#include <ham/config.h>
#include "error.h"
#include "cache.h"
#include "freelist.h"
#include "mem.h"
#include "os.h"
#include "db.h"
#include "btree.h"
#include "version.h"
#include "txn.h"
#include "blob.h"
#include "extkeys.h"

static ham_status_t 
my_write_page(ham_db_t *db, ham_page_t *page)
{
    ham_status_t st;

    /*
     * !!!
     * one day, we'll have to protect these file IO-operations
     * with a mutex
     */
    ham_assert(!(db_get_flags(db)&HAM_IN_MEMORY_DB), 
            "can't fetch a page from in-memory-db", 0);
    ham_assert(page_get_pers(page)!=0, 
            "writing page 0x%llx, but page has no buffer", 
            page_get_self(page));

    st=os_pwrite(db_get_fd(db), page_get_self(page), 
            (void *)page_get_pers(page), db_get_pagesize(db));
    if (st) {
        ham_log("os_pwrite failed with status %d (%s)", st, ham_strerror(st));
        return (db_set_error(db, HAM_IO_ERROR));
    }

    page_set_dirty(page, 0);
    return (0);
}

static ham_status_t 
my_read_page(ham_db_t *db, ham_offset_t address, ham_page_t *page)
{
    ham_status_t st;

    /*
     * !!!
     * one day, we'll have to protect these file IO-operations
     * with a mutex
     */

    ham_assert(!(db_get_flags(db)&HAM_IN_MEMORY_DB), 
            "can't fetch a page from in-memory-db", 0);

    if (db_get_flags(db)&DB_USE_MMAP) {
        ham_u8_t *buffer;
        st=os_mmap(db_get_fd(db), address, db_get_pagesize(db),
            &buffer);
        if (st) {
            ham_log("os_mmap failed with status %d (%s)", st, ham_strerror(st));
            db_set_error(db, HAM_IO_ERROR);
            return (HAM_IO_ERROR);
        }
        page_set_pers(page, (union page_union_t *)buffer);
    }
    else {
        st=os_pread(db_get_fd(db), address, (void *)page_get_pers(page), 
                db_get_pagesize(db));
        if (st) {
            ham_log("os_pread failed with status %d (%s)", st, 
                    ham_strerror(st));
            return (db_set_error(db, HAM_IO_ERROR));
        }
    }

    return (0);
}

static ham_page_t *
my_alloc_page(ham_db_t *db, ham_bool_t need_pers)
{
    ham_page_t *page;
    ham_status_t st;

    (void)need_pers;

    /*
     * allocate one page of memory, if we have room for one more page
     */
    if (cache_can_add_page(db_get_cache(db))) {
        page=db_alloc_page_struct(db);
        if (!page) {
            ham_log("db_alloc_page_struct failed", 0);
            return (0);
        }
    }

    /*
     * otherwise: replace a page
     */
    else {
        page=cache_get_unused(db_get_cache(db));
        if (!page) {
            db_set_error(db, HAM_CACHE_FULL);
            return (0);
        }

        if (page_is_dirty(page) && !(db_get_flags(db)&HAM_IN_MEMORY_DB)) { 
            st=my_write_page(db, page);
            if (st) {
                db_set_error(db, st);
                return (0);
            }
        }

        if (!(page_get_npers_flags(page)&PAGE_NPERS_MALLOC)) {
            st=os_munmap(page_get_pers(page), db_get_pagesize(db));
            if (st) {
                db_set_error(db, st);
                return (0);
            }
        }
        else {
            ham_mem_free(page_get_pers(page));
        }
        memset(page, 0, sizeof(ham_page_t));
        page_set_owner(page, db);
    }

    /* 
     * for in-memory-databases and if we use read(2) for I/O, we need 
     * a second page buffer for the file data
     */
    if (!(db_get_flags(db)&DB_USE_MMAP) && !page_get_pers(page)) {
        page_set_pers(page, (union page_union_t *)ham_mem_alloc(
                    db_get_pagesize(db)));
        if (!page_get_pers(page)) {
            ham_log("page_new failed - out of memory", 0);
            db_set_error(db, HAM_OUT_OF_MEMORY);
            return (0);
        }
        page_set_npers_flags(page, 
                page_get_npers_flags(page)|PAGE_NPERS_MALLOC);
    }

    /* TODO wenn wir in dieser funktion mit einem fehler rausgehen, 
     * haben wir memory leaks! */

    return (page);
}

ham_page_t *
db_alloc_page_struct(ham_db_t *db)
{
    ham_page_t *page;

    page=(ham_page_t *)ham_mem_alloc(sizeof(ham_page_t));
    if (!page) {
        db_set_error(db, HAM_OUT_OF_MEMORY);
        return (0);
    }

    memset(page, 0, sizeof(*page));
    page_set_owner(page, db);
    /* temporarily initialize the cache counter, just to be on the safe side */
    page_set_cache_cntr(page, 20);

    if (!(db_get_flags(db)&DB_USE_MMAP)) {
        page_set_pers(page, (union page_union_t *)ham_mem_alloc(
                    db_get_pagesize(db)));
        if (!page_get_pers(page)) {
            ham_log("page_set_pers failed - out of memory", 0);
            db_set_error(db, HAM_OUT_OF_MEMORY);
            return (0);
        }
        page_set_npers_flags(page, 
                page_get_npers_flags(page)|PAGE_NPERS_MALLOC);
    }

    return (page);
}

void
db_free_page_struct(ham_page_t *page)
{
    ham_db_t *db=page_get_owner(page);

    /*
     * make sure that the page is removed from the cache
     */
    (void)cache_remove_page(db_get_cache(db), page);

    /*
     * if we have extended keys: remove all extended keys from the 
     * cache
     * TODO move this to the backend!
     */
    if (!(page_get_npers_flags(page)&PAGE_NPERS_DELETE_PENDING) && 
        (page_get_type(page)==PAGE_TYPE_B_ROOT || 
         page_get_type(page)==PAGE_TYPE_B_INDEX)) {
        ham_size_t i;
        ham_offset_t blobid;
        key_t *bte;
        btree_node_t *node=ham_page_get_btree_node(page);
        extkey_cache_t *c=db_get_extkey_cache(page_get_owner(page));

        if (btree_node_is_leaf(node)) {
            for (i=0; i<btree_node_get_count(node); i++) {
                bte=btree_node_get_key(db, node, i);
                if (key_get_flags(bte)&KEY_IS_EXTENDED) {
                    blobid=*(ham_offset_t *)(key_get_key(bte)+
                            (db_get_keysize(db)-sizeof(ham_offset_t)));
                    if (db_get_flags(db)&HAM_IN_MEMORY_DB) 
                        (void)blob_free(db, 0, blobid, 0);
                    else if (c)
                        (void)extkey_cache_remove(c, blobid);
                }
            }
        }
    }

    /*
     * free the memory
     */
    if (page_get_pers(page)) {
        if (page_get_npers_flags(page)&PAGE_NPERS_MALLOC) 
            ham_mem_free(page_get_pers(page));
        else 
            (void)os_munmap(page_get_pers(page), db_get_pagesize(db));
    }

    ham_mem_free(page);
}

ham_status_t
db_write_page_to_device(ham_page_t *page)
{
    return (my_write_page(page_get_owner(page), page));
}

ham_status_t
db_fetch_page_from_device(ham_page_t *page, ham_offset_t address)
{
    page_set_self(page, address);
    return (my_read_page(page_get_owner(page), address, page));
}

ham_status_t
db_alloc_page_device(ham_page_t *page, ham_u32_t flags)
{
    ham_status_t st;
    ham_offset_t tellpos=0, resize=0;
    ham_db_t *db=page_get_owner(page);

    /* 
     * if this is not an in-memory-db: set the memory to 0 and leave
     */
    if (db_get_flags(db)&HAM_IN_MEMORY_DB) {
        page_set_self(page, (ham_offset_t)page);
        memset(page_get_pers(page), 0, sizeof(struct page_union_header_t));
        return (0);
    }

    /* first, we ask the freelist for a page */
    if (!(flags&PAGE_IGNORE_FREELIST)) {
        tellpos=freel_alloc_area(db, db_get_pagesize(db), 0);
        if (tellpos) 
            page_set_self(page, tellpos);
    }

    /* otherwise: move to the end of the file */
    if (!tellpos) {
        st=os_seek(db_get_fd(db), 0, HAM_OS_SEEK_END);
        if (st) 
            return (st);

        /* get the current file position */
        st=os_tell(db_get_fd(db), &tellpos);
        if (st) 
            return (st);

        /* and write it to disk */
        st=os_truncate(db_get_fd(db), tellpos+resize+db_get_pagesize(db));
        if (st) 
            return (st);

        /*
         * if we're using MMAP: when allocating a new page, we need
         * memory for the persistent buffer
         */
        if ((db_get_flags(db)&DB_USE_MMAP) && !page_get_pers(page)) {

            st=my_read_page(db, tellpos, page);
            if (st) /* TODO memleaks? */
                return (st);
        }
    }

    if (page_get_npers_flags(page)&PAGE_NPERS_MALLOC) 
        memset(page_get_pers(page), 0, sizeof(struct page_union_header_t));

    page_set_self(page, tellpos);
    page_set_dirty(page, 0);

    return (0);
}

int 
db_default_prefix_compare(const ham_u8_t *lhs, ham_size_t lhs_length, 
                   ham_size_t lhs_real_length,
                   const ham_u8_t *rhs, ham_size_t rhs_length,
                   ham_size_t rhs_real_length)
{
    int m;
    ham_size_t min_length=lhs_length<rhs_length?lhs_length:rhs_length;

    m=memcmp(lhs, rhs, min_length);
    if (m<0)
        return (-1);
    if (m>0)
        return (+1);
    return (HAM_PREFIX_REQUEST_FULLKEY);
}

int 
db_default_compare(const ham_u8_t *lhs, ham_size_t lhs_length, 
                   const ham_u8_t *rhs, ham_size_t rhs_length)
{
    int m;

    /* 
     * the default compare uses memcmp
     *
     * treat shorter strings as "higher"
     */
    if (lhs_length<rhs_length) {
        m=memcmp(lhs, rhs, lhs_length);
        if (m<0)
            return (-1);
        if (m>0)
            return (+1);
        return (-1);
    }

    else if (rhs_length<lhs_length) {
        m=memcmp(lhs, rhs, rhs_length);
        if (m<0)
            return (-1);
        if (m>0)
            return (+1);
        return (+1);
    }

    m=memcmp(lhs, rhs, lhs_length);
    if (m<0)
        return (-1);
    if (m>0)
        return (+1);
    return (0);
}

int
db_compare_keys(ham_db_t *db, ham_txn_t *txn, ham_page_t *page,
                long lhs_idx, ham_u32_t lhs_flags, 
                const ham_u8_t *lhs, ham_size_t lhs_length, 
                long rhs_idx, ham_u32_t rhs_flags, 
                const ham_u8_t *rhs, ham_size_t rhs_length)
{
    int cmp=HAM_PREFIX_REQUEST_FULLKEY;
    ham_compare_func_t foo=db_get_compare_func(db);
    ham_prefix_compare_func_t prefoo=db_get_prefix_compare_func(db);
    ham_status_t st;
    ham_record_t lhs_record, rhs_record;
    ham_u8_t *plhs=0, *prhs=0;
    ham_size_t temp;
    ham_bool_t alloc1=HAM_FALSE, alloc2=HAM_FALSE;

    db_set_error(db, 0);

    /*
     * need prefix compare? 
     */
    if (!(lhs_flags&KEY_IS_EXTENDED) && !(rhs_flags&KEY_IS_EXTENDED)) {
        /*
         * no!
         */
        return (foo(lhs, lhs_length, rhs, rhs_length));
    }

    /*
     * yes! - run prefix comparison, but only if we have a prefix
     * comparison function
     */
    if (prefoo) {
        ham_size_t lhsprefixlen, rhsprefixlen;

        if (lhs_flags&KEY_IS_EXTENDED) 
            lhsprefixlen=db_get_keysize(db)-sizeof(ham_offset_t);
        else
            lhsprefixlen=lhs_length;

        if (rhs_flags&KEY_IS_EXTENDED) 
            rhsprefixlen=db_get_keysize(db)-sizeof(ham_offset_t);
        else
            rhsprefixlen=rhs_length;
        
        cmp=prefoo(lhs, lhsprefixlen, lhs_length, rhs, 
                rhsprefixlen, rhs_length);
        if (db_get_error(db))
            return (0);
    }

    if (cmp==HAM_PREFIX_REQUEST_FULLKEY) {
        /*
         * make sure that we have an extended key-cache
         * 
         * in in-memory-db, the extkey-cache doesn't lead to performance
         * advantages; it only duplicates the data and wastes memory. 
         * therefore we don't use it.
         */
        if (!(db_get_flags(db)&HAM_IN_MEMORY_DB)) {
            if (!db_get_extkey_cache(db)) {
                db_set_extkey_cache(db, extkey_cache_new(db));
                if (!db_get_extkey_cache(db))
                    return (db_get_error(db));
            }
        }

        /*
         * 1. load the first key, if needed
         */
        if (lhs_flags&KEY_IS_EXTENDED) {
            ham_offset_t blobid;

            blobid=*(ham_offset_t *)(lhs+(db_get_keysize(db)-
                    sizeof(ham_offset_t)));

            /* fetch from the cache */
            if (!(db_get_flags(db)&HAM_IN_MEMORY_DB)) {
                st=extkey_cache_fetch(db_get_extkey_cache(db), blobid, 
                        &temp, &plhs);
                if (!st) 
                    ham_assert(temp==lhs_length, "invalid key length", 0);
            }
            else
                st=HAM_KEY_NOT_FOUND;

            if (st) {
                if (st!=HAM_KEY_NOT_FOUND) {
                    db_set_error(db, st);
                    return (st);
                }
                /* not cached - fetch from disk */
                memset(&lhs_record, 0, sizeof(lhs_record));

                st=blob_read(db, txn, blobid, &lhs_record, 0);
                if (st) {
                    db_set_error(db, st);
                    goto bail;
                }

                plhs=(ham_u8_t *)ham_mem_alloc(lhs_record.size+
                        db_get_keysize(db));
                if (!plhs) {
                    db_set_error(db, HAM_OUT_OF_MEMORY);
                    goto bail;
                }
                memcpy(plhs, lhs, db_get_keysize(db)-sizeof(ham_offset_t));
                memcpy(plhs+(db_get_keysize(db)-sizeof(ham_offset_t)),
                        lhs_record.data, lhs_record.size);

                /* insert the FULL key in the cache */
                if (!(db_get_flags(db)&HAM_IN_MEMORY_DB)) {
                    (void)extkey_cache_insert(db_get_extkey_cache(db), 
                            blobid, lhs_length, plhs);
                }
                alloc1=HAM_TRUE;
            }
        }
        
        /*
         * 2. load the second key, if needed
         */
        if (rhs_flags&KEY_IS_EXTENDED) {
            ham_offset_t blobid;

            blobid=*(ham_offset_t *)(rhs+(db_get_keysize(db)-
                    sizeof(ham_offset_t)));

            /* fetch from the cache */
            if (!(db_get_flags(db)&HAM_IN_MEMORY_DB)) {
                st=extkey_cache_fetch(db_get_extkey_cache(db), blobid, 
                        &temp, &prhs);
                if (!st) 
                    ham_assert(temp==rhs_length, "invalid key length", 0);
            }
            else
                st=HAM_KEY_NOT_FOUND;

            if (st) {
                if (st!=HAM_KEY_NOT_FOUND) {
                    db_set_error(db, st);
                    return (st);
                }
                /* not cached - fetch from disk */
                memset(&rhs_record, 0, sizeof(rhs_record));

                st=blob_read(db, txn, blobid, &rhs_record, 0);
                if (st) {
                    db_set_error(db, st);
                    goto bail;
                }

                prhs=(ham_u8_t *)ham_mem_alloc(rhs_record.size+
                        db_get_keysize(db));
                if (!prhs) {
                    db_set_error(db, HAM_OUT_OF_MEMORY);
                    goto bail;
                }

                memcpy(prhs, rhs, db_get_keysize(db)-sizeof(ham_offset_t));
                memcpy(prhs+(db_get_keysize(db)-sizeof(ham_offset_t)),
                        rhs_record.data, rhs_record.size);

                /* insert the FULL key in the cache */
                if (!(db_get_flags(db)&HAM_IN_MEMORY_DB)) {
                    (void)extkey_cache_insert(db_get_extkey_cache(db), 
                            blobid, rhs_length, prhs);
                }
                alloc2=HAM_TRUE;
            }
        }

        /*
         * 3. run the comparison function
         */
        cmp=foo(plhs ? plhs : lhs, lhs_length, prhs ? prhs : rhs, rhs_length);
    }

bail:
    if (alloc1 && plhs) 
        ham_mem_free(plhs);
    if (alloc2 && prhs)
        ham_mem_free(prhs);

    return (cmp);
}

ham_backend_t *
db_create_backend(ham_db_t *db, ham_u32_t flags)
{
    ham_backend_t *be;
    ham_status_t st;

    /*
     * hash tables are not yet supported
     */
    if (flags&HAM_USE_HASH) {
        ham_log("hash indices are not yet supported", 0);
        return (0);
    }

    /* 
     * the default backend is the BTREE
     *
     * create a ham_backend_t with the size of a ham_btree_t
     */
    be=(ham_backend_t *)ham_mem_alloc(sizeof(ham_btree_t));
    if (!be) {
        ham_log("out of memory", 0);
        return (0);
    }

    /* initialize the backend */
    st=btree_create((ham_btree_t *)be, db, flags);
    if (st) {
        ham_log("failed to initialize backend: 0x%s", st);
        return (0);
    }

    return (be);
}

ham_page_t *
db_fetch_page(ham_db_t *db, ham_txn_t *txn, ham_offset_t address, 
        ham_u32_t flags)
{
    ham_page_t *page;
    ham_status_t st;

    /*
     * first, check if the page is in the txn
     */
    if (txn) {
        page=txn_get_page(txn, address);
        if (page)
            return (page);
    }

    /*
     * if we have a cache: fetch the page from the cache
     */
    if (db_get_cache(db)) {
        page=cache_get(db_get_cache(db), address);
        if (page) {
            st=txn_add_page(txn, page);
            if (st) {
                db_set_error(db, st);
                return (0);
            }
            return (page);
        }
    }
    
    if (flags&DB_ONLY_FROM_CACHE)
        return (0);

    /* 
     * check if the cache allows us to allocate another page
     */
    if (!cache_can_add_page(db_get_cache(db))) {
        ham_trace("cache is full! resize the cache", 0);
        db_set_error(db, HAM_CACHE_FULL);
        return (0);
    }

    /*
     * otherwise allocate memory for the page
     */
    page=my_alloc_page(db, HAM_FALSE);
    if (!page)
        return (0);

    /*
     * and read the page, either with mmap or read
     */
    st=my_read_page(db, address, page);
    if (st) {
        db_set_error(db, st);
        (void)db_free_page_struct(page);
        return (0);
    }
    page_set_self(page, address);

    /*
     * add the page to the transaction
     */
    if (txn) {
        st=txn_add_page(txn, page);
        if (st) {
            db_set_error(db, st);
            (void)db_free_page_struct(page);
            return (0);
        }
    }

    /*
     * add the page to the cache
     */
    st=cache_put(db_get_cache(db), page);
    if (st) {
        db_set_error(db, st);
        return (0);
    }

    return (page);
}

ham_status_t
db_flush_page(ham_db_t *db, ham_txn_t *txn, ham_page_t *page,
        ham_u32_t flags)
{
    ham_status_t st;

    (void)txn;

    /* write the page, if it's dirty and if write-through is enabled */
    if ((db_get_flags(db)&HAM_WRITE_THROUGH) && page_is_dirty(page)) {
        st=my_write_page(db, page);
        if (st)
            return (st);
    }

    return (cache_put(db_get_cache(db), page));
}

ham_status_t
db_flush_all(ham_db_t *db, ham_txn_t *txn, ham_u32_t flags)
{
    (void)txn;

    return (cache_flush_and_delete(db_get_cache(db), flags));
}

ham_page_t *
db_alloc_page(ham_db_t *db, ham_u32_t type, ham_txn_t *txn, ham_u32_t flags)
{
    ham_status_t st;
    ham_page_t *page;

    /* allocate memory for the page */
    page=my_alloc_page(db, HAM_TRUE);
    if (!page)
        return (0);

    ham_assert(cache_can_add_page(db_get_cache(db)), 0, 0);

    /* allocate storage on the device */
    st=db_alloc_page_device(page, flags);
    if (st) {
        db_set_error(db, st); /* TODO memleak! */
        return (0);
    }

    ham_assert(cache_can_add_page(db_get_cache(db)), 0, 0);

    /* set the page type */
    page_set_type(page, type);

    /* add the page to the transaction */
    if (txn) {
        st=txn_add_page(txn, page);
        if (st) {
            db_set_error(db, st);
            return (0);
        }
    }
    /* if there's no txn, set the "in_use"-flag - otherwise the cache
     * might purge it immediately */
    else {
        page_set_inuse(page, 1);
    }

    ham_assert(cache_can_add_page(db_get_cache(db)), 0, 0);

    /* store the page in the cache */
    st=cache_put(db_get_cache(db), page);
    if (st) {
        db_set_error(db, st); /* TODO memleak! */
        return (0);
    }

    return (page);

    /* TODO avoid memory leak! auch nach anderen 
        aufrufen von my_alloc_page() */
}

ham_status_t
db_free_page(ham_db_t *db, ham_txn_t *txn, ham_page_t *page, 
        ham_u32_t flags)
{
    (void)txn;
    (void)flags;

    ham_assert(!(page_get_npers_flags(page)&PAGE_NPERS_DELETE_PENDING), 
            "deleting a page which is already deleted", 0);

    /*
     * if we have extended keys: remove all extended keys from the 
     * cache
     * TODO move this to the backend!
     */
    if ((page_get_type(page)==PAGE_TYPE_B_ROOT || 
         page_get_type(page)==PAGE_TYPE_B_INDEX)) {
        ham_size_t i;
        ham_offset_t blobid;
        key_t *bte;
        btree_node_t *node=ham_page_get_btree_node(page);
        extkey_cache_t *c=db_get_extkey_cache(page_get_owner(page));

        if (btree_node_is_leaf(node)) {
            for (i=0; i<btree_node_get_count(node); i++) {
                bte=btree_node_get_key(db, node, i);
                if (key_get_flags(bte)&KEY_IS_EXTENDED) {
                    blobid=*(ham_offset_t *)(key_get_key(bte)+
                            (db_get_keysize(db)-sizeof(ham_offset_t)));
                    if (db_get_flags(db)&HAM_IN_MEMORY_DB) 
                        (void)blob_free(db, txn, blobid, 0);
                    else if (c)
                        (void)extkey_cache_remove(c, blobid);
                }
            }
        }
    }

    page_set_npers_flags(page, 
            page_get_npers_flags(page)|PAGE_NPERS_DELETE_PENDING);

    return (0);
}

ham_status_t 
db_write_page_and_delete(ham_db_t *db, ham_page_t *page, ham_u32_t flags)
{
    /*
     * write page to disk
     */
    if (page_is_dirty(page) && !(db_get_flags(db)&HAM_IN_MEMORY_DB))
        (void)my_write_page(db, page);

    /* 
     * free the memory of the page
     */
    if (!(flags&DB_FLUSH_NODELETE)) 
        db_free_page_struct(page);

    return (0);
}
