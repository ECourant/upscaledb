/*
 * Copyright (C) 2005-2011 Christoph Rupp (chris@crupp.de).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 *
 * See files COPYING.* for License information.
 */

/**
 * @brief a base-"class" for cursors
 *
 * A Cursor is an object which is used to traverse a Database. 
 *
 * A Cursor structure is separated into 3 components:
 * 1. The btree cursor
 *      This cursor can traverse btrees. It is described and implemented
 *      in btree_cursor.h.
 * 2. The txn cursor
 *      This cursor can traverse txn-trees. It is described and implemented
 *      in txn_cursor.h.
 * 3. The upper layer
 *      This layer acts as a kind of dispatcher for both cursors. If 
 *      Transactions are used, then it also uses a duplicate cache for 
 *      consolidating the duplicate keys from both cursors. This layer is
 *      described and implemented in cursor.h (this file.
 * 
 * A Cursor can have several states. It can be 
 * 1. NIL (not in list) - this is the default state, meaning that the Cursor
 *      does not point to any key. If the Cursor was initialized, then it's 
 *      "NIL". If the Cursor was erased (@ref ham_cursor_erase) then it's
 *      also "NIL".
 *
 *      relevant functions:
 *          @ref cursor_is_nil
 *          @ref cursor_set_to_nil
 *
 * 2. Coupled to the txn-cursor - meaning that the Cursor points to a key
 *      that is modified in a Transaction. Technically, the txn-cursor points
 *      to a @ref txn_op_t structure.
 *
 *      relevant functions:
 *          @ref cursor_is_coupled_to_txnop
 *          @ref cursor_couple_to_txnop
 *
 * 3. Coupled to the btree-cursor - meaning that the Cursor points to a key
 *      that is stored in a Btree. A Btree cursor itself can then be coupled
 *      (it directly points to a page in the cache) or uncoupled, meaning that
 *      the page was purged from the cache and has to be fetched from disk when
 *      the Cursor is used again. This is described in btree_cursor.h.
 *
 *      relevant functions:
 *          @ref cursor_is_coupled_to_btree
 *          @ref cursor_couple_to_btree
 *
 * The dupecache is used when information from the btree and the txn-tree 
 * is merged. The btree cursor has its private dupecache. Both will be merged
 * sooner or later. 
 *
 * The cursor interface is used in db.c. Many of the functions in db.c use 
 * a high-level cursor interface (i.e. @ref cursor_create, @ref cursor_clone) 
 * while some directly use the low-level interfaces of btree_cursor.h and
 * txn_cursor.h. Over time i will clean this up, trying to maintain a clear
 * separation of the 3 layers, and only accessing the top-level layer in
 * cursor.h. This is work in progress.
 */

#ifndef HAM_CURSORS_H__
#define HAM_CURSORS_H__

#include <vector>

#include "internal_fwd_decl.h"

#include "error.h"
#include "txn_cursor.h"
#include "btree_cursor.h"
#include "blob.h"


#ifdef __cplusplus
extern "C" {
#endif 

/**
 * A single line in the dupecache structure - can reference a btree
 * record or a txn-op 
 */
class DupeCacheLine 
{
  public:
    DupeCacheLine(ham_bool_t use_btree=true, ham_u64_t btree_dupeidx=0)
    : m_use_btree(use_btree), m_btree_dupeidx(btree_dupeidx), m_op(0) {
        ham_assert(use_btree==true, (""));
    }

    DupeCacheLine(ham_bool_t use_btree, txn_op_t *op)
    : m_use_btree(use_btree), m_btree_dupeidx(0), m_op(op) {
        ham_assert(use_btree==false, (""));
    }

    /** Returns true if this cache entry is a duplicate in the btree */
    ham_bool_t use_btree(void) {
        return (m_use_btree); 
    }

    /** Returns the btree duplicate index */
    ham_offset_t get_btree_dupe_idx(void) {
        ham_assert(m_use_btree==true, (""));
        return (m_btree_dupeidx);
    }

    /** Sets the btree duplicate index */
    void set_btree_dupe_idx(ham_offset_t idx) {
        m_use_btree=true;
        m_btree_dupeidx=idx;
        m_op=0;
    }

    /** Returns the txn-op duplicate */
    txn_op_t *get_txn_op(void) {
        ham_assert(m_use_btree==false, (""));
        return (m_op);
    }

    /** Sets the txn-op duplicate */
    void set_txn_op(txn_op_t *op) {
        m_use_btree=false;
        m_op=op;
        m_btree_dupeidx=0;
    }

  private:
    /** Are we using btree or txn duplicates? */
    ham_bool_t m_use_btree;

    /** The btree duplicate index (of the original btree dupe table) */
    ham_u64_t m_btree_dupeidx;

    /** The txn op structure */
    txn_op_t *m_op;

};



/**
 * The dupecache is a cache for duplicate keys
 */
class DupeCache {
  public:
    /* default constructor - creates an empty dupecache with room for 8 
     * duplicates */
    DupeCache(void) {
        m_elements.reserve(8);
    }

    /** retrieve number of elements in the cache */
    ham_size_t get_count(void) {
        return (m_elements.size());
    }

    /** get an element from the cache */
    DupeCacheLine *get_element(unsigned idx) {
        return (&m_elements[idx]);
    }

    /** get a pointer to the first element from the cache */
    DupeCacheLine *get_first_element(void) {
        return (&m_elements[0]);
    }

    /** Clones this dupe-cache into 'other' */
    void clone(DupeCache *other) {
        other->m_elements=m_elements;
    }

    /**
     * Inserts a new item somewhere in the cache; resizes the 
     * cache if necessary
     */
    void insert(unsigned position, const DupeCacheLine &dcl) {
        m_elements.insert(m_elements.begin()+position, dcl);
    }

    /** append an element to the dupecache */
    void append(const DupeCacheLine &dcl) {
        m_elements.push_back(dcl);
    }

    /** Erases an item */
    void erase(ham_u32_t position) {
        m_elements.erase(m_elements.begin()+position);
    }

    /** Clears the cache; frees all resources */
    void clear(void) {
        m_elements.resize(0);
    }

  private:
    /** The cached elements */
    std::vector<DupeCacheLine> m_elements;

};


/**
 * a generic Cursor structure, which has the same memory layout as
 * all other backends
 */
struct ham_cursor_t
{
    /** Pointer to the Database object */
    ham_db_t *_db;

    /** Pointer to the Transaction */
    ham_txn_t *_txn;

    /** A Cursor which can walk over Transaction trees */
    txn_cursor_t _txn_cursor;

    /** A Cursor which can walk over B+trees */
    btree_cursor_t _btree_cursor;

    /** The remote database handle */
    ham_u64_t _remote_handle;

    /** Linked list of all Cursors in this Database */
    ham_cursor_t *_next, *_previous;

    /** Linked list of Cursors which point to the same page */
    ham_cursor_t *_next_in_page, *_previous_in_page;

    /** A cache for all duplicates of the current key. needed for
     * ham_cursor_move, ham_find and other functions. The cache is
     * used to consolidate all duplicates of btree and txn. */
    DupeCache _dupecache;

    /** The current position of the cursor in the cache. This is a
     * 1-based index. 0 means that the cache is not in use. */
    ham_u32_t _dupecache_index;

    /** The last operation (insert/find or move); needed for
     * ham_cursor_move. Values can be HAM_CURSOR_NEXT,
     * HAM_CURSOR_PREVIOUS or CURSOR_LOOKUP_INSERT */
    ham_u32_t _lastop;

    /** the result of the last compare operation between btree and txn cursor;
     * -1 if btree cursor is smaller, 0 if they are equal, +1 if btree cursor
     * is larger; every other value means that the compare value needs to be
     * updated. only used in cursor_move() */
    int _lastcmp; 

    /** Cursor flags */
    ham_u32_t _flags;
};

/** Get the Cursor flags */
#define cursor_get_flags(c)               (c)->_flags

/** Set the Cursor flags */
#define cursor_set_flags(c, f)            (c)->_flags=(f)

/*
 * the flags have ranges:
 *  0 - 0x1000000-1:      btree_cursor
 *  > 0x1000000:          cursor
 */
/** Cursor flag: cursor is coupled to the Transaction cursor (_txn_cursor) */
#define _CURSOR_COUPLED_TO_TXN            0x1000000

/** Get the 'next' pointer of the linked list */
#define cursor_get_next(c)                (c)->_next

/** Set the 'next' pointer of the linked list */
#define cursor_set_next(c, n)             (c)->_next=(n)

/** Get the 'previous' pointer of the linked list */
#define cursor_get_previous(c)            (c)->_previous

/** Set the 'previous' pointer of the linked list */
#define cursor_set_previous(c, p)         (c)->_previous=(p)

/** Get the 'next' pointer of the linked list */
#define cursor_get_next_in_page(c)       (c)->_next_in_page

/** Set the 'next' pointer of the linked list */
#define cursor_set_next_in_page(c, n)                                        \
    {                                                                        \
        if (n)                                                                \
            ham_assert((c)->_previous_in_page!=(n), (0));                    \
        (c)->_next_in_page=(n);                                                \
    }

/** Get the 'previous' pointer of the linked list */
#define cursor_get_previous_in_page(c)   (c)->_previous_in_page

/** Set the 'previous' pointer of the linked list */
#define cursor_set_previous_in_page(c, p)                                    \
    {                                                                        \
        if (p)                                                                \
            ham_assert((c)->_next_in_page!=(p), (0));                        \
        (c)->_previous_in_page=(p);                                            \
    }

/** Set the Database pointer */
#define cursor_set_db(c, db)            (c)->_db=db

/** Get the Database pointer */
#define cursor_get_db(c)                (c)->_db

/** Get the Transaction handle */
#define cursor_get_txn(c)               (c)->_txn

/** Set the Transaction handle */
#define cursor_set_txn(c, txn)          (c)->_txn=(txn)

/** Get a pointer to the Transaction cursor */
#ifdef HAM_DEBUG
extern txn_cursor_t *
cursor_get_txn_cursor(ham_cursor_t *cursor);
#else
#   define cursor_get_txn_cursor(c)     (&(c)->_txn_cursor)
#endif

/** Get a pointer to the Btree cursor */
#define cursor_get_btree_cursor(c)      (&(c)->_btree_cursor)

/** Get the remote Database handle */
#define cursor_get_remote_handle(c)     (c)->_remote_handle

/** Set the remote Database handle */
#define cursor_set_remote_handle(c, h)  (c)->_remote_handle=(h)

/** Get a pointer to the duplicate cache */
#define cursor_get_dupecache(c)         (&(c)->_dupecache)

/** Get the current index in the dupe cache */
#define cursor_get_dupecache_index(c)   (c)->_dupecache_index

/** Set the current index in the dupe cache */
#define cursor_set_dupecache_index(c, i) (c)->_dupecache_index=i

/** Get the previous operation */
#define cursor_get_lastop(c)            (c)->_lastop

/** Store the current operation; needed for ham_cursor_move */
#define cursor_set_lastop(c, o)         (c)->_lastop=(o)

/** Get the previous compare operation */
#define cursor_get_lastcmp(c)           (c)->_lastcmp

/** Store the current compare operation; needed for cursor_move */
#define cursor_set_lastcmp(c, cmp)      (c)->_lastcmp=(cmp)

/** flag for cursor_set_lastop */
#define CURSOR_LOOKUP_INSERT            0x10000

/**
 * Creates a new cursor
 */
extern ham_status_t
cursor_create(ham_db_t *db, ham_txn_t *txn, ham_u32_t flags,
            ham_cursor_t **pcursor);

/**
 * Clones an existing cursor
 */
extern ham_status_t
cursor_clone(ham_cursor_t *src, ham_cursor_t **dest);

/**
 * Returns true if a cursor is nil (Not In List - does not point to any key)
 *
 * 'what' is one of the flags below
 */
extern ham_bool_t
cursor_is_nil(ham_cursor_t *cursor, int what);

#define CURSOR_BOTH         (CURSOR_BTREE|CURSOR_TXN)
#define CURSOR_BTREE        1
#define CURSOR_TXN          2

/**
 * Sets the cursor to nil
 */
extern void
cursor_set_to_nil(ham_cursor_t *cursor, int what);

/**
 * Returns true if a cursor is coupled to the btree
 */
#define cursor_is_coupled_to_btree(c)                                         \
                                 (!(cursor_get_flags(c)&_CURSOR_COUPLED_TO_TXN))

/**
 * Returns true if a cursor is coupled to a txn-op
 */
#define cursor_is_coupled_to_txnop(c)                                         \
                                    (cursor_get_flags(c)&_CURSOR_COUPLED_TO_TXN)

/**
 * Couples the cursor to a btree key
 */
#define cursor_couple_to_btree(c)                                             \
            (cursor_set_flags(c, cursor_get_flags(c)&(~_CURSOR_COUPLED_TO_TXN)))

/**
 * Couples the cursor to a txn-op
 */
#define cursor_couple_to_txnop(c)                                             \
               (cursor_set_flags(c, cursor_get_flags(c)|_CURSOR_COUPLED_TO_TXN))

/**
 * Erases the key/record pair that the cursor points to. 
 *
 * On success, the cursor is then set to nil. The Transaction is passed 
 * as a separate pointer since it might be a local/temporary Transaction 
 * that was created only for this single operation.
 */
extern ham_status_t
cursor_erase(ham_cursor_t *cursor, ham_txn_t *txn, ham_u32_t flags);

/**
 * Retrieves the number of duplicates of the current key
 *
 * The Transaction is passed as a separate pointer since it might be a 
 * local/temporary Transaction that was created only for this single operation.
 */
extern ham_status_t
cursor_get_duplicate_count(ham_cursor_t *cursor, ham_txn_t *txn, 
            ham_u32_t *pcount, ham_u32_t flags);

/**
 * Overwrites the record of the current key
 *
 * The Transaction is passed as a separate pointer since it might be a 
 * local/temporary Transaction that was created only for this single operation.
 */
extern ham_status_t 
cursor_overwrite(ham_cursor_t *cursor, ham_txn_t *txn, ham_record_t *record,
            ham_u32_t flags);

/**
 * Updates (or builds) the dupecache for a cursor
 *
 * The 'what' parameter specifies if the dupecache is initialized from
 * btree (CURSOR_BTREE), from txn (CURSOR_TXN) or both.
 */
extern ham_status_t
cursor_update_dupecache(ham_cursor_t *cursor, ham_u32_t what);

/**
 * Clear the dupecache and disconnect the Cursor from any duplicate key
 */
extern void
cursor_clear_dupecache(ham_cursor_t *cursor);

/**
 * Couples the cursor to a duplicate in the dupe table
 * dupe_id is a 1 based index!!
 */
extern void
cursor_couple_to_dupe(ham_cursor_t *cursor, ham_u32_t dupe_id);

/**
 * Checks if a btree cursor points to a key that was overwritten or erased
 * in the txn-cursor
 *
 * This is needed in db.c when moving the cursor backwards/forwards and 
 * consolidating the btree and the txn-tree
 */
extern ham_status_t
cursor_check_if_btree_key_is_erased_or_overwritten(ham_cursor_t *cursor);

/**
 * Synchronizes txn- and btree-cursor
 *
 * If txn-cursor is nil then try to move the txn-cursor to the same key
 * as the btree cursor.
 * If btree-cursor is nil then try to move the btree-cursor to the same key
 * as the txn cursor.
 * If both are nil, or both are valid, then nothing happens
 *
 * equal_key is set to true if the keys in both cursors are equal.
 */
extern ham_status_t
cursor_sync(ham_cursor_t *cursor, ham_u32_t flags, ham_bool_t *equal_keys);

/**
 * Moves a Cursor
 */
extern ham_status_t
cursor_move(ham_cursor_t *cursor, ham_key_t *key, ham_record_t *record,
                ham_u32_t flags);

/**
 * flag for cursor_sync: do not use approx matching if the key
 * is not available
 */
#define CURSOR_SYNC_ONLY_EQUAL_KEY            0x200000

/**
 * flag for cursor_sync: do not load the key if there's an approx.
 * match. Only positions the cursor.
 */
#define CURSOR_SYNC_DONT_LOAD_KEY             0x100000

/**
 * Returns the number of duplicates in the duplicate cache
 * The duplicate cache is updated if necessary
 */
extern ham_size_t
cursor_get_dupecache_count(ham_cursor_t *cursor);

/**
 * Closes an existing cursor
 */
extern void
cursor_close(ham_cursor_t *cursor);


#ifdef __cplusplus
} // extern "C"
#endif 

#endif /* HAM_CURSORS_H__ */
