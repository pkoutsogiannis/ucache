/*
   +----------------------------------------------------------------------------------------------+
   | Windows Cache for PHP                                                                        |
   +----------------------------------------------------------------------------------------------+
   | Module: ucache_compat.h                                                                      |
   +----------------------------------------------------------------------------------------------+
   | Zend API differences between PHP 7.2 and PHP 8.x that the zval cache (ucache_zvcache.c)      |
   | depends on.  Every threshold below holds for all the versions in between as well.            |
   +----------------------------------------------------------------------------------------------+
*/

#ifndef _UCACHE_COMPAT_H_
#define _UCACHE_COMPAT_H_

/*
 * zend_refcounted header.
 * 7.2: GC_REFCOUNT()/GC_FLAGS()/GC_INFO() are lvalues (gc.refcount, gc.u.v.flags, gc.u.v.gc_info).
 * 7.3: they became rvalue accessors; GC_SET_REFCOUNT/GC_ADDREF/GC_DELREF/GC_ADD_FLAGS/GC_DEL_FLAGS
 *      were added and the HashTable flags got the HT_FLAGS() accessor.
 */
#if PHP_VERSION_ID < 70300
# define GC_SET_REFCOUNT(p, rc)      (GC_REFCOUNT(p) = (rc))
# define GC_ADDREF(p)                (++GC_REFCOUNT(p))
# define GC_DELREF(p)                (--GC_REFCOUNT(p))
# define GC_ADD_FLAGS(p, flags)      do { GC_FLAGS(p) |= (flags); } while (0)
# define GC_DEL_FLAGS(p, flags)      do { GC_FLAGS(p) &= ~(flags); } while (0)
# define HT_FLAGS(ht)                (ht)->u.flags
#endif

/*
 * Type info of a fresh zend_reference (what ZVAL_NEW_REF() stores).
 * 7.x: IS_REFERENCE.  8.0+: GC_REFERENCE (IS_REFERENCE | GC_NOT_COLLECTABLE).
 */
#if PHP_VERSION_ID < 80000
# define GC_REFERENCE                IS_REFERENCE
#endif

/*
 * "Initialized" HashTable flag: HASH_FLAG_INITIALIZED (1<<3) up to 7.3,
 * inverted into HASH_FLAG_UNINITIALIZED (1<<3) + HT_IS_INITIALIZED() in 7.4.
 */
#if PHP_VERSION_ID < 70400
# define HT_IS_INITIALIZED(ht)       ((HT_FLAGS(ht) & HASH_FLAG_INITIALIZED) != 0)
#endif

/*
 * HashTable flag bits that a copy of a table may keep, and those it adds (what zend_array_dup()
 * does).
 * 7.3+: keeps the low byte (HASH_FLAG_MASK), which drops nIteratorsCount, and adds nothing:
 *       recursion protection is GC_PROTECTED, which works on every array that is not immutable.
 * 7.2:  keeps the low byte minus HASH_FLAG_PERSISTENT, which drops nApplyCount and
 *       nIteratorsCount, and adds HASH_FLAG_APPLY_PROTECTION, which opcache clears on the arrays
 *       it makes immutable: without it, var_dump(), print_r(), var_export(), json_encode(),
 *       count() and == do not detect a recursion built in the copy, and overflow the stack.
 */
#if PHP_VERSION_ID >= 70300
# define UCACHE_HT_FLAGS_KEEP        HASH_FLAG_MASK
# define UCACHE_HT_FLAGS_ADD         0
#else
# define UCACHE_HT_FLAGS_KEEP        (0xff & ~HASH_FLAG_PERSISTENT)
# define UCACHE_HT_FLAGS_ADD         HASH_FLAG_APPLY_PROTECTION
#endif

/*
 * Packed arrays: since 8.2 the data part is a zval vector (arPacked, 16 bytes per element)
 * instead of a Bucket vector (32 bytes per element).  The hash part in front of it is
 * always HT_MIN_MASK (2 slots).
 */
#if PHP_VERSION_ID >= 80200
# define UCACHE_HT_PACKED_USED_SIZE(ht)  HT_PACKED_USED_SIZE(ht)
# define UCACHE_HT_PACKED_SIZE(ht)       HT_PACKED_SIZE(ht)
# define UCACHE_HT_PACKED_ZVAL(ht, idx)  (&(ht)->arPacked[(idx)])
#else
# define UCACHE_HT_PACKED_USED_SIZE(ht)  HT_USED_SIZE(ht)
# define UCACHE_HT_PACKED_SIZE(ht)       HT_SIZE(ht)
# define UCACHE_HT_PACKED_ZVAL(ht, idx)  (&(ht)->arData[(idx)].val)
#endif

/*
 * Hash part compaction when a sparse hash table is copied into the cache.
 * Since 7.3 the hash part has 2 * nTableSize slots, and main/config.w32.h defines __SSE2__,
 * which makes HT_HASH_RESET() a 64-byte SIMD loop that requires >= 16 slots (a multiple of
 * 64 bytes); the engine runs it on our tables again in zend_array_dup()/zend_hash_rehash().
 * Use the opcache 7.3+ rule (zend_persist.c, zend_hash_persist): compact only when
 * nNumUsed > HT_MIN_SIZE and nNumUsed < slots / 4, and stop halving at slots / 4 <= nNumUsed,
 * which always leaves >= 32 slots.  The rule also produces valid tables on 7.2.
 */
#define UCACHE_HT_HASH_SLOTS(ht)        ((uint32_t)(-(int32_t)(ht)->nTableMask))
#define UCACHE_HT_SHOULD_COMPACT(ht) \
    ((ht)->nNumUsed > HT_MIN_SIZE && (ht)->nNumUsed < UCACHE_HT_HASH_SLOTS(ht) / 4)

/*
 * zvcache_over_memory_limit(): needed where the runtime can have "delayed OOM", which the
 * October 2026 security releases brought to the 8.2 to 8.5 branches (8.2.35, 8.3.36, 8.4.27,
 * 8.5.12).  Older versions do not have it, and PHP 7.2, 7.3, 7.4.0 to 7.4.20 and 8.0.0 to 8.0.7
 * let ini_set() lower memory_limit below the memory already in use, after which the check would
 * fail every long copy for nothing; 7.4.21, 8.0.8 and later refuse such a limit ("Failed to set
 * memory limit").
 */
#if PHP_VERSION_ID >= 80200
# define UCACHE_MEMORY_LIMIT_CHECK   1
#else
# define UCACHE_MEMORY_LIMIT_CHECK   0
#endif

#endif /* _UCACHE_COMPAT_H_ */
