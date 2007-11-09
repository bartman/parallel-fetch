#ifndef __included__pf_bitops_h__
#define __included__pf_bitops_h__

#include <asm/types.h>

// these are lifted from linux kernel asm/bitops.h

#ifndef BITS_PER_LONG
#define BITS_PER_LONG (sizeof(long)*8)
#endif

#define BITOP_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))
#define BITOP_WORD(nr)		((nr) / BITS_PER_LONG)

/**
 * test_bit - Determine whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 */
static inline int test_bit(int nr, const volatile unsigned long *addr)
{
	return 1UL & (addr[BITOP_WORD(nr)] >> (nr & (BITS_PER_LONG-1)));
}

/**
 * __set_bit - Set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Unlike set_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static inline void set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);

	*p  |= mask;
}

static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);

	*p &= ~mask;
}

/**
 * find_first_bit - find the first set bit in a memory region
 * @data: The address to start the search at
 * @max: The maximum size to search
 *
 * Returns the bit-number of the first set bit, not the number of the byte
 * containing a bit.
 */
static inline unsigned int my_find_first_bit (unsigned long *data, size_t max)
{
	size_t n;
	for (n=0; n<max; n++) {
		if (test_bit (n, data))
			return n;
	}
	return max;
}

static inline unsigned int my_find_first_zero_bit (unsigned long *data, size_t max)
{
	size_t n;
	for (n=0; n<max; n++) {
		if (! test_bit (n, data))
			return n;
	}
	return max;
}

#endif // __included__pf_bitops_h__
