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

/* The type of the keys, make sure you also modify COMPARE if you use anything
   other than integers. */
#ifndef K
#define K long long
#endif

/* The type of the values */
#ifndef V
#define V long long
#endif

#ifndef COMPARE
signed char compare(const K *x, const K *y) { return *x == *y ? 0 : *x < *y ? 1 : -1; }
#define COMPARE compare
#endif

typedef struct BTreeMap {
  /* The size of the BTreeMap.

     If `size` is non-zero `root`'s node must be non-null. */
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

/* Returns a pointer to the value associated to key `key` if any, or `NULL`
   if not found. Note that insert calls may invalidate the pointer. */
V *btree_map_get(BTreeMap *map, const K *key);

/* Insert or update a value in the tree. May invalidate pointers returned by  */
void btree_map_insert(BTreeMap *map, K key, V value);

/* Remove a key and its associated value from the map. */
void btree_map_remove(BTreeMap *map, const K *key);

/* Remove all elements from the map */
void btree_map_clear(BTreeMap *map);

/* Deallocate the memory the map is using. */
void btree_map_dealloc(BTreeMap *map);

typedef struct BTreeMapIter {
  unsigned short index;
  size_t height;
} BTreeMapIter;

/* Whether the iterator is complete. */
bool btree_map_done(BTreeMapIter *it);
/* Get the next item on the iterator. */
void btree_map_next(BTreeMapIter *it, K **key, V **value);

#endif /* BTREE_H_ */
