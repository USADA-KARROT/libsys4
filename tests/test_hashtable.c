/* Tests for the integer-keyed hash table API, in particular ht_remove_int.
 *
 * Covers: present key, absent key, double removal, bucket compaction
 * (colliding keys stay reachable after a removal), remove-then-reinsert,
 * and table-wide integrity under many insertions/removals.
 */
#include <assert.h>
#include <stdbool.h> // hashtable.h uses bool but is not self-contained
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "system4/hashtable.h"

#define SENTINEL ((void*)(intptr_t)-1)

static void *val(int i)
{
	return (void*)(intptr_t)(i + 1000);
}

static void test_basic_put_get_remove(void)
{
	struct hash_table *ht = ht_create(64);
	for (int i = 0; i < 100; i++) {
		struct ht_slot *slot = ht_put_int(ht, i, NULL);
		slot->value = val(i);
	}
	for (int i = 0; i < 100; i++)
		assert(ht_get_int(ht, i, SENTINEL) == val(i));

	// remove a present key
	ht_remove_int(ht, 50);
	assert(ht_get_int(ht, 50, SENTINEL) == SENTINEL);
	// neighbours unaffected
	assert(ht_get_int(ht, 49, SENTINEL) == val(49));
	assert(ht_get_int(ht, 51, SENTINEL) == val(51));

	// remove an absent key: no effect, no crash
	ht_remove_int(ht, 9999);
	// double removal: no effect, no crash
	ht_remove_int(ht, 50);
	assert(ht_get_int(ht, 50, SENTINEL) == SENTINEL);

	// re-insert a removed key
	struct ht_slot *slot = ht_put_int(ht, 50, NULL);
	slot->value = val(50);
	assert(ht_get_int(ht, 50, SENTINEL) == val(50));

	ht_free_int(ht);
	printf("test_basic_put_get_remove: OK\n");
}

static void test_bucket_compaction(void)
{
	// ht_create rounds up to 64 buckets; keys spaced 64 apart that hash to
	// the same bucket exercise the fill-hole-with-last-slot path.
	struct hash_table *ht = ht_create(64);
	// int_hash is identity-like for small ints (hash & (nr_buckets-1)),
	// so k, k+64, k+128, ... collide.
	int keys[8];
	for (int i = 0; i < 8; i++) {
		keys[i] = 7 + i * 64;
		ht_put_int(ht, keys[i], val(keys[i]));
	}
	// remove from the middle of the bucket
	ht_remove_int(ht, keys[3]);
	assert(ht_get_int(ht, keys[3], SENTINEL) == SENTINEL);
	// every other colliding key must still be reachable
	for (int i = 0; i < 8; i++) {
		if (i == 3)
			continue;
		assert(ht_get_int(ht, keys[i], SENTINEL) == val(keys[i]));
	}
	// remove the (current) last slot and the first slot too
	ht_remove_int(ht, keys[0]);
	ht_remove_int(ht, keys[7]);
	assert(ht_get_int(ht, keys[0], SENTINEL) == SENTINEL);
	assert(ht_get_int(ht, keys[7], SENTINEL) == SENTINEL);
	for (int i = 1; i < 7; i++) {
		if (i == 3)
			continue;
		assert(ht_get_int(ht, keys[i], SENTINEL) == val(keys[i]));
	}
	ht_free_int(ht);
	printf("test_bucket_compaction: OK\n");
}

static void test_churn(void)
{
	// many insert/remove cycles; verifies no stale slots accumulate and
	// lookups stay correct (memory safety is checked by ASan builds)
	struct hash_table *ht = ht_create(64);
	for (int round = 0; round < 50; round++) {
		for (int i = 0; i < 256; i++)
			ht_put_int(ht, i, val(i));
		for (int i = 0; i < 256; i += 2)
			ht_remove_int(ht, i);
		for (int i = 0; i < 256; i++) {
			void *expect = (i % 2 == 0) ? SENTINEL : val(i);
			assert(ht_get_int(ht, i, SENTINEL) == expect);
		}
		for (int i = 1; i < 256; i += 2)
			ht_remove_int(ht, i);
		for (int i = 0; i < 256; i++)
			assert(ht_get_int(ht, i, SENTINEL) == SENTINEL);
	}
	ht_free_int(ht);
	printf("test_churn: OK\n");
}

int main(void)
{
	test_basic_put_get_remove();
	test_bucket_compaction();
	test_churn();
	printf("test_hashtable: all tests passed\n");
	return 0;
}
