#ifndef BTREE_H_
#define BTREE_H_

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define B 6

/* Change the BTreeMap allocator */
#define ALLOC(size) malloc(size)
#define DEALLOC(ptr) free(ptr)
/* Define what to do if the allocator returns a NULL pointer */
#define OOM()                                                                  \
  ({                                                                           \
    fprintf(stderr, "Out of memory\n");                                        \
    exit(1);                                                                   \
  })

#ifndef K
#define K int
#endif

#ifndef V
#define V int
#endif

#ifndef COMPARE
char compare(const K *x, const K *y) { return *x == *y ? 0 : *x < *y ? 1 : -1; }
#define COMPARE compare
#endif

typedef struct BTreeMap {
  /* The size of the BTreeMap.

     If `size` is non-zero `root`'s node must be non-null.
   */
  size_t size;

  /* The root node of the tree */
  void *root;
  /* The height of root node */
  size_t height;
} BTreeMap;

/* Create and initialize a new BTreeMap. This function doesn't allocate. */
static BTreeMap btree_map_new(void) {
  /* root node is only null when size = 0. */
  BTreeMap map;
  map.size = 0;
  map.root = NULL;
  /* height is left uninitialized */
  return map;
}

/* Returns the value associated to key `key` if any, or `NULL` if not found. */
V *btree_map_get(BTreeMap *map, K *const key);
void btree_map_insert(BTreeMap *map, K key, V value);

void btree_map_clear(BTreeMap *map);
/**/
void btree_map_dealloc(BTreeMap *map);

typedef struct BTreeMapIter {

} BTreeMapIter;

/* Whether the iterator is complete. */
bool btree_map_done(BTreeMapIter *it);
/* Get the next item on the iterator. */
void btree_map_next(BTreeMapIter *it, K *key, V *value);

#endif /* BTREE_H_ */
