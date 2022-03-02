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
  {                                                                            \
    fprintf(stderr, "Out of memory\n");                                        \
    exit(1);                                                                   \
  }

#define DEALLOC_KEY(key) DEALLOC(key)
#define DEALLOC_VALUE(value)
#define IS_DEALLOC_ELEMENT 1

/* The type of the keys, make sure you also modify COMPARE if you use anything
   other than integers. */
#ifndef K
#define K char *
#endif

/* The type of the values */
#ifndef V
#define V int
#endif

#ifndef COMPARE
#define COMPARE(x, y) strcmp(*x, *y)
#endif

struct btree_map {
  /* The size of the BTreeMap.

     If `size` is non-zero `root`'s node must be non-null. */
  size_t size;

  /* An opaque pointer to the root node of the tree. */
  struct leaf_node *root;
  /* The height of root node. */
  size_t height;
};

/* Create and initialize a new BTreeMap. This function doesn't allocate. */
static struct btree_map btree_map_new(void) {
  /* root node is only null when size = 0. */
  struct btree_map map;
  map.size = 0;
  map.root = NULL;
  /* height is left uninitialized */
  return map;
}

/* Returns a pointer to the value associated to key `key` if any, or `NULL`
   if not found. Note that insert calls may invalidate the pointer. */
V *btree_map_get(struct btree_map *map, K const *key);

/* Insert or update a value in the tree. May invalidate pointers returned by
   btree_map_get.
   **Note:** Don't call this function while iterating or you might invalidate
   the iterator. */
void btree_map_insert(struct btree_map *map, K key, V value);

/* Remove a key and its associated value from the map.
   **Note:** Don't call this function while iterating or you might invalidate
   the iterator. */
void btree_map_remove(struct btree_map *map, K const *key);

/* Remove all elements from the map. */
void btree_map_clear(struct btree_map *map);

/* Deallocate the memory the map is using. */
void btree_map_dealloc(struct btree_map *map);

struct btree_map_iter {
  void *node;
  void **parents;
  unsigned short *indexes;
  size_t max_height;
  size_t height;
  unsigned short index;
};

/* Iterate through the map in sorted order (sorted by key). While iterating it
   is allowed to modify the keys and values since they're just pointers to their
   values in the map, but not in a way that would change the order of the keys.
   While iterating btree_map_insert or btree_map_remove can cause errors.
   **Note:** the iterator has to allocate a small amount of memory to store
   pointers, so it must be deallocated using btree_map_iter_dealloc. */
struct btree_map_iter btree_map_iter(struct btree_map *map);

/* Get the next item on the iterator. Returns true if there are more elements to
   iter through. Calling this function again after it returned false will keep
   returning false. */
bool btree_map_iter_next(struct btree_map_iter *it, K **key, V **value);

/* Reset the iterator, starts over from the smallest element in the map. */
void btree_map_iter_reset(struct btree_map_iter *it);

void btree_map_iter_dealloc(struct btree_map_iter *it);

#endif /* BTREE_H_ */
