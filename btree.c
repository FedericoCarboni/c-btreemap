#include <assert.h>
#include <string.h>

#include <stdio.h>

#include "btree.h"

#define CAPACITY (2 * B - 1)
#define MIN_LEN_AFTER_SPLIT (B - 1)
#define KV_IDX_CENTER (B - 1)
#define EDGE_IDX_LEFT_OF_CENTER (B - 1)
#define EDGE_IDX_RIGHT_OF_CENTER B

static inline void *_alloc_checked(size_t size) {
  void *ptr = ALLOC(size);
  if (ptr == NULL) {
    OOM();
  }
  return ptr;
}

#define NEW(type) ((type *)_alloc_checked(sizeof(type)))

typedef unsigned short ushort;

struct leaf_node {
  /* The length of the node, i.e. how many keys and values there are. */
  ushort len;
  /* Keys and values, only elements up to `len` are initialized and valid. */
  K keys[CAPACITY];
  V vals[CAPACITY];
};

struct inode {
  /* Any node can be dereferenced as a leaf node. */
  struct leaf_node data;
  /* Only `len + 1` elements of the array are initialized. */
  struct leaf_node *children[CAPACITY + 1];
};

struct node_ref {
  /* A pointer to a leaf or an internal node. It must not be NULL. */
  struct leaf_node *node;
  /* Must be 0 on leaf nodes and must be non zero on internal nodes. */
  size_t height;
};

/* Initialize a leaf node */
static void leaf_node_init(struct leaf_node *node) { node->len = 0; }

/* Allocate and initialize a leaf node */
static struct leaf_node *leaf_node_new(void) {
  struct leaf_node *node = NEW(struct leaf_node);
  leaf_node_init(node);
  return node;
}

/* Allocate and initialize an internal node */
static struct inode *inode_new(void) {
  struct inode *node = NEW(struct inode);
  leaf_node_init(&node->data);
  return node;
}

static struct node_ref node_ref_from_root(BTreeMap *map) {
  assert(map->root != NULL);
  return (struct node_ref){(struct leaf_node *)map->root, map->height};
}

#define inode_cast(node_ref) ((struct inode *)((node_ref).node))

/* Check whether this node_ref references a leaf node */
static inline bool node_ref_is_leaf(struct node_ref node_ref) {
  return node_ref.height == 0;
}

static inline bool node_ref_is_full(struct node_ref node_ref) {
  return node_ref.node->len == CAPACITY;
}

/* Get a reference to a child node.
   Warning causes undefined behavior if node_ref is not an internal node or if
   the child at index is invalid. */
static struct node_ref node_ref_descend(struct node_ref node_ref,
                                        ushort index) {
  assert(node_ref.height > 0);
  assert(index <= node_ref.node->len);
  assert(inode_cast(node_ref)->children[index] != NULL);
  return (struct node_ref){inode_cast(node_ref)->children[index],
                           node_ref.height - 1};
}

struct kv {
  K k;
  V v;
};

struct split {
  struct leaf_node *node;
  struct kv kv;
};

static inline struct split split_none(void) {
  struct split split;
  split.node = NULL;
  return split;
}

/* Split old_node at index */
static struct kv node_split_leaf_data(struct leaf_node *old_node,
                                      struct leaf_node *new_node,
                                      ushort index) {
  ushort old_len = old_node->len;
  ushort new_len = old_len - index - 1;
  new_node->len = new_len;
  old_node->len = index;
  memcpy(new_node->keys, &old_node->keys[index + 1], new_len * sizeof(K));
  memcpy(new_node->vals, &old_node->vals[index + 1], new_len * sizeof(V));
  return (struct kv){old_node->keys[index], old_node->vals[index]};
}

/* Splits a node reference and returns a newly allocated node. */
static struct split node_ref_split(struct node_ref node, ushort index) {
  if (node_ref_is_leaf(node)) {
    struct leaf_node *new_leaf = leaf_node_new();
    struct kv kv = node_split_leaf_data(node.node, new_leaf, index);
    return (struct split){new_leaf, kv};
  } else {
    struct inode *new_inode = inode_new();
    struct kv kv = node_split_leaf_data(node.node, &new_inode->data, index);
    ushort new_len = new_inode->data.len;
    memcpy(new_inode->children, &inode_cast(node)->children[index + 1],
           (new_len + 1) * sizeof(struct leaf_node *));
    return (struct split){(struct leaf_node *)new_inode, kv};
  }
}

static struct kv node_remove_unchecked(struct leaf_node *node, ushort index) {
  struct kv kv = {node->keys[index], node->vals[index]};
  if (index < node->len) {
    /* We have to remove a kv pair from the middle of the arrays. So we have to
       shift back one or more elements to fill the gap.
       AB DEF
        <-|
       ABDEF
     */
    memmove(&node->keys[index], &node->keys[index + 1],
            (node->len - index - 1) * sizeof(K));
    memmove(&node->vals[index], &node->vals[index + 1],
            (node->len - index - 1) * sizeof(V));
  }
  node->len -= 1;
  return kv;
}

static void node_remove_child(struct inode *node, ushort index) {
  if (index <= node->data.len) {
    /* We have to remove a kv pair from the middle of the arrays. So we have to
       shift back one or more elements to fill the gap.
       AB DEF
        <-|
       ABDEF
     */
    memmove(&node->children[index], &node->children[index + 1],
            (node->data.len - index) * sizeof(struct leaf_node *));
  }
}

static void node_ref_borrow_from_left(struct node_ref parent, ushort index) {
  struct node_ref left = node_ref_descend(parent, index - 1);
  struct node_ref right = node_ref_descend(parent, index);

  // printf("LEFT:\n");
  // for (ushort i = 0; i < left.node->len; ++i) {
  //   printf("%i => %i\n", left.node->keys[i], left.node->vals[i]);
  // }
  // printf("RIGHT:\n");
  // for (ushort i = 0; i < right.node->len; ++i) {
  //   printf("%i => %i\n", right.node->keys[i], right.node->vals[i]);
  // }

  ushort left_len = left.node->len;
  ushort right_len = right.node->len;

  ushort shift = ((right_len + left_len) >> 1) - right_len;

  /* Make space for the borrowed key/values */
  memmove(&right.node->keys[shift], right.node->keys, right_len * sizeof(K));
  memmove(&right.node->vals[shift], right.node->vals, right_len * sizeof(V));
  if (parent.height > 1) {
    memmove(&inode_cast(right)->children[shift], &inode_cast(right)->children,
            (right_len + 1) * sizeof(struct leaf_node *));
  }

  right.node->keys[shift - 1] = parent.node->keys[index - 1];
  right.node->vals[shift - 1] = parent.node->vals[index - 1];

  parent.node->keys[index - 1] = left.node->keys[left_len - shift];
  parent.node->vals[index - 1] = left.node->vals[left_len - shift];

  memcpy(right.node->keys, &left.node->keys[left_len - shift + 1],
         (shift - 1) * sizeof(K));
  memcpy(right.node->vals, &left.node->vals[left_len - shift + 1],
         (shift - 1) * sizeof(V));
  if (parent.height > 1) {
    memcpy(inode_cast(right)->children,
           &inode_cast(left)->children[left_len - shift + 1],
           shift * sizeof(struct leaf_node *));
  }

  left.node->len -= shift;
  right.node->len += shift;

  // printf("LEFT:\n");
  // for (ushort i = 0; i < left.node->len; ++i) {
  //   printf("%i => %i\n", left.node->keys[i], left.node->vals[i]);
  // }
  // printf("RIGHT:\n");
  // for (ushort i = 0; i < right.node->len; ++i) {
  //   printf("%i => %i\n", right.node->keys[i], right.node->vals[i]);
  // }
}

static void node_ref_borrow_from_right(struct node_ref parent, ushort index) {
  struct node_ref left = node_ref_descend(parent, index);
  struct node_ref right = node_ref_descend(parent, index + 1);

  ushort left_len = left.node->len;
  ushort right_len = right.node->len;
  ushort shift = ((left_len + right_len) >> 1) - left_len;

  left.node->keys[left_len] = parent.node->keys[index];
  left.node->vals[left_len] = parent.node->vals[index];
  memcpy(&left.node->keys[left_len + 1], right.node->keys,
         (shift - 1) * sizeof(K));
  memcpy(&left.node->vals[left_len + 1], right.node->vals,
         (shift - 1) * sizeof(V));
  if (parent.height > 1) {
    memcpy(&inode_cast(left)->children[left_len + 1],
           &inode_cast(right)->children, shift * sizeof(struct leaf_node *));
  }
  parent.node->keys[index] = right.node->keys[shift - 1];
  parent.node->vals[index] = right.node->vals[shift - 1];

  memmove(right.node->keys, &right.node->keys[shift],
          (right_len - shift) * sizeof(K));
  memmove(right.node->vals, &right.node->vals[shift],
          (right_len - shift) * sizeof(V));

  if (parent.height > 1) {
    memmove(inode_cast(right)->children, &inode_cast(right)->children[shift],
            (right_len - shift + 1) * sizeof(struct leaf_node *));
  }

  left.node->len += shift;
  right.node->len -= shift;
}

/* Merges child and child_sibling into child, deallocates node and updates
   parent's children. */
static void node_ref_merge(struct node_ref parent, ushort index) {
  assert(!node_ref_is_leaf(parent));
  struct node_ref left = node_ref_descend(parent, index);
  struct node_ref right = node_ref_descend(parent, index + 1);
  ushort left_len = left.node->len;
  ushort right_len = right.node->len;
  /* Copy keys and values leaving an uninitialized space for the key/value pair
     in parent associated with child_sibling. */
  memcpy(&left.node->keys[left_len + 1], right.node->keys,
         right_len * sizeof(K));
  memcpy(&left.node->vals[left_len + 1], right.node->vals,
         right_len * sizeof(V));
  if (parent.height > 1) {
    /* The node is internal, copy children. */
    memcpy(&inode_cast(left)->children[left_len + 1],
           inode_cast(right)->children,
           (right_len + 1) * sizeof(struct leaf_node *));
  }
  node_remove_child(inode_cast(parent), index + 1);
  struct kv kv = node_remove_unchecked(parent.node, index);
  left.node->keys[left_len] = kv.k;
  left.node->vals[left_len] = kv.v;
  left.node->len = left_len + right_len + 1;
  /* We copied everything we needed to copy from child_sibling, deallocate it.
     We don't use node_ref_dealloc because it would also get rid of child nodes,
     whose ownership has been transferred to child. */
  DEALLOC(right.node);
}

/* Search for a key inside a node, this uses a linear search, a binary search
   algorithm could improve performance only if B was a lot higher. Since we
   search on short arrays (11 elements) linear search is actually faster. */
static ushort node_ref_search(struct node_ref node_ref, const K *key,
                              bool *found) {
  ushort i = 0;
  for (; i < node_ref.node->len; ++i) {
    int cmp = COMPARE(key, &node_ref.node->keys[i]);
    if (cmp == 0) {
      *found = true;
    }
    if (cmp <= 0) {
      return i;
    }
  }
  return i;
}

/* index must be <= node->len */
static void node_insert_unchecked(struct leaf_node *node, ushort index, K key,
                                  V value) {
  if (index < node->len) {
    /* We have to insert a kv pair in the middle of the arrays. So we have to
       shift forward one or more elements to make space.
       ABDEF
         |->
       AB DEF
         ^
         C
     */
    memmove(&node->keys[index + 1], &node->keys[index],
            (node->len - index) * sizeof(K));
    memmove(&node->vals[index + 1], &node->vals[index],
            (node->len - index) * sizeof(V));
  }
  node->keys[index] = key;
  node->vals[index] = value;
  node->len += 1;
}

static void underflow_left(struct node_ref node_ref, ushort index) {
  struct node_ref edge_left = node_ref_descend(node_ref, index);
  if (edge_left.node->len < B - 1) {
    struct node_ref edge_right = node_ref_descend(node_ref, index + 1);
    if (edge_right.node->len > B) {
      node_ref_borrow_from_right(node_ref, index);
    } else {
      node_ref_merge(node_ref, index);
    }
  }
}

static void underflow_right(struct node_ref node_ref, ushort index) {
  struct node_ref edge_right = node_ref_descend(node_ref, index);
  if (edge_right.node->len < B - 1) {
    struct node_ref edge_left = node_ref_descend(node_ref, index - 1);
    if (edge_left.node->len > B) {
      node_ref_borrow_from_left(node_ref, index);
    } else {
      node_ref_merge(node_ref, index - 1);
    }
  }
}

static void check_underflow(struct node_ref node_ref, ushort index) {
  if (index == 0) {
    underflow_left(node_ref, index);
  } else {
    underflow_right(node_ref, index);
  }
}

static struct kv node_remove_least(struct node_ref node_ref) {
  if (node_ref_is_leaf(node_ref)) {
    return node_remove_unchecked(node_ref.node, 0);
  }

  struct kv y = node_remove_least(node_ref_descend(node_ref, 0));
  check_underflow(node_ref, 0);
  return y;
}

static void node_insert_child(struct inode *node, ushort index,
                              struct leaf_node *child) {
  if (index <= node->data.len) {
    /* We have to insert a child in the middle of the array. So we have to
       shift forward one or more elements to make space.
       ABDEF
         |->
       AB DEF
         ^
         C
     */
    memmove(&node->children[index + 2], &node->children[index + 1],
            (node->data.len - index - 1) * sizeof(struct leaf_node *));
  }
  node->children[index + 1] = child;
}

static ushort node_find_splitpoint(ushort *index, bool *is_left) {
  if (*index < EDGE_IDX_LEFT_OF_CENTER) {
    *is_left = true;
    return KV_IDX_CENTER - 1;
  } else if (*index == EDGE_IDX_LEFT_OF_CENTER) {
    *is_left = true;
    return KV_IDX_CENTER;
  } else if (*index == EDGE_IDX_RIGHT_OF_CENTER) {
    *is_left = false;
    *index = 0;
    return KV_IDX_CENTER;
  } else {
    *is_left = false;
    *index = *index - KV_IDX_CENTER - 2;
    return KV_IDX_CENTER + 1;
  }
}

/* index must be <= node_ref.node->len */
static struct split node_insert(struct node_ref node_ref, ushort index, K key,
                                V value) {
  if (node_ref_is_full(node_ref)) {
    ushort insert_index = index;
    bool is_left;
    /* the node is full we have to split it */
    ushort middle_index = node_find_splitpoint(&insert_index, &is_left);
    struct split split = node_ref_split(node_ref, middle_index);
    node_insert_unchecked(is_left ? node_ref.node : split.node, insert_index,
                          key, value);
    return split;
  }

  /* We just checked that the node is not full. */
  node_insert_unchecked(node_ref.node, index, key, value);

  return split_none();
}

/* node_ref must be an internal node */
static struct split node_insert_with_child(struct node_ref node_ref,
                                           ushort index, K key, V value,
                                           struct leaf_node *child) {
  if (node_ref_is_full(node_ref)) {
    ushort insert_index = index;
    bool is_left;
    /* the node is full we have to split it */
    ushort middle_index = node_find_splitpoint(&insert_index, &is_left);
    struct split split = node_ref_split(node_ref, middle_index);
    struct inode *insert_node =
        (struct inode *)(is_left ? node_ref.node : split.node);
    node_insert_unchecked(&insert_node->data, insert_index, key, value);
    node_insert_child(insert_node, insert_index, child);
    return split;
  }

  /* We just checked that the node is not full. */
  node_insert_unchecked(node_ref.node, index, key, value);
  node_insert_child(inode_cast(node_ref), index, child);

  return split_none();
}

static struct split node_insert_recursive(struct node_ref node_ref, K key,
                                          V value, bool *found) {
  ushort index = node_ref_search(node_ref, &key, found);

  if (*found) {
    /* We found the key already in the tree, just update the value. */
    node_ref.node->vals[index] = value;
  } else if (node_ref_is_leaf(node_ref)) {
    /* This is a leaf, insert the key and value */
    return node_insert(node_ref, index, key, value);
  } else {
    /* Didn't find the key, descend */
    struct split child_split = node_insert_recursive(
        node_ref_descend(node_ref, index), key, value, found);
    if (child_split.node != NULL) {
      /* The child was split */
      return node_insert_with_child(node_ref, index, child_split.kv.k,
                                    child_split.kv.v, child_split.node);
    }
  }

  return split_none();
}

static bool node_remove_recursive(struct node_ref node_ref, const K *key) {
  bool found = false;
  ushort index = node_ref_search(node_ref, key, &found);
  if (found) {
    if (node_ref_is_leaf(node_ref)) {
      node_remove_unchecked(node_ref.node, index);
    } else {
      struct kv kv = node_remove_least(node_ref_descend(node_ref, index + 1));
      node_ref.node->keys[index] = kv.k;
      node_ref.node->vals[index] = kv.v;
      check_underflow(node_ref, index + 1);
    }
    return true;
  } else if (!node_ref_is_leaf(node_ref) &&
             node_remove_recursive(node_ref_descend(node_ref, index), key)) {
    check_underflow(node_ref, index);
    return true;
  }
  return false;
}

static void node_ref_dealloc_recursive(struct node_ref node_ref) {
  if (node_ref_is_leaf(node_ref)) {
    DEALLOC(node_ref.node);
  } else {
    for (ushort index = 0; index <= node_ref.node->len; ++index) {
      node_ref_dealloc_recursive(node_ref_descend(node_ref, index));
    }
    DEALLOC(node_ref.node);
  }
}

V *btree_map_get(BTreeMap *map, const K *key) {
  if (map->root == NULL) {
    return NULL;
  }
  struct node_ref node_ref = node_ref_from_root(map);
  while (true) {
    bool found = false;
    ushort index = node_ref_search(node_ref, key, &found);
    if (found) {
      return &node_ref.node->vals[index];
    } else if (node_ref_is_leaf(node_ref)) {
      return NULL;
    } else {
      node_ref = node_ref_descend(node_ref, index);
    }
  }
}

void btree_map_insert(BTreeMap *map, K key, V value) {
  /* The map is lazy, it will not allocate until we actually need to store keys
     and values. */
  if (map->root == NULL) {
    struct leaf_node *new_root = leaf_node_new();
    node_insert_unchecked(new_root, 0, key, value);
    map->size = 1;
    map->root = new_root;
    /* height is only guaranteed to be initialized when root is not NULL. */
    map->height = 0;
    return;
  }

  bool found = false;
  struct split split =
      node_insert_recursive(node_ref_from_root(map), key, value, &found);

  if (!found) {
    map->size += 1;
  }

  if (split.node != NULL) {
    /* Root was split. Create a new internal node and treat the old root and the
       split node as child nodes. */
    struct inode *new_root = inode_new();

    node_insert_unchecked(&new_root->data, 0, split.kv.k, split.kv.v);
    new_root->children[0] = node_ref_from_root(map).node;
    new_root->children[1] = split.node;

    map->root = new_root;
    /* Increase the height of the root */
    map->height += 1;
  }
}

void btree_map_remove(BTreeMap *map, const K *key) {
  if (map->root == NULL) {
    return;
  }

  if (node_remove_recursive(node_ref_from_root(map), key)) {
    /* We removed an element from the */
    map->size -= 1;

    if (((struct leaf_node *)map->root)->len == 0) {
      if (map->height == 0) {
        DEALLOC(map->root);
        map->root = NULL;
        return;
      }
      struct inode *old_root = (struct inode *)map->root;
      map->root = old_root->children[0];
      map->height -= 1;
      DEALLOC(old_root);
    }
  }
}

void btree_map_dealloc(BTreeMap *map) {
  if (map->root != NULL) {
    /* deallocate the whole tree */
    node_ref_dealloc_recursive(node_ref_from_root(map));
  }
}

void btree_map_clear(BTreeMap *map) {
  btree_map_dealloc(map);
  map->size = 0;
  map->root = NULL;
}

BTreeMapIter btree_map_iter(BTreeMap *map) {
  BTreeMapIter it;
  if (map->root == NULL) {
    it.node = NULL;
    return it;
  }
  it.max_height = map->height;
  if (it.max_height != 0) {
    it.parents = _alloc_checked(it.max_height * sizeof(struct node *) +
                                it.max_height * sizeof(ushort));
    it.indexes = (unsigned short *)(it.parents + it.max_height);
    it.parents[it.max_height - 1] = map->root;
  }
  btree_map_iter_reset(&it);
  return it;
}

void btree_map_iter_reset(BTreeMapIter *it) {
  if (it->max_height == 0) {
    it->index = 0;
    return;
  }
  struct node_ref node = {it->parents[it->max_height - 1], it->max_height};
  while (node.height != 0) {
    it->parents[node.height - 1] = node.node;
    it->indexes[node.height - 1] = 0;
    node = node_ref_descend(node, 0);
  }
  it->node = node.node;
  it->height = 0;
  it->index = 0;
}

bool btree_map_iter_next(BTreeMapIter *it, K **key, V **value) {
  if (it->node == NULL) {
    return false;
  }

  while (true) {
    if (it->index < ((struct leaf_node *)it->node)->len) {
      *key = &((struct leaf_node *)it->node)->keys[it->index];
      *value = &((struct leaf_node *)it->node)->vals[it->index];
      it->index += 1;
      if (it->height != 0) {
        it->height -= 1;
        it->parents[it->height] = it->node;
        it->indexes[it->height] = it->index;
        it->node = ((struct inode *)it->node)->children[it->index];
        it->index = 0;
        while (it->height != 0) {
          it->height -= 1;
          it->parents[it->height] = it->node;
          it->indexes[it->height] = 0;
          it->node = ((struct inode *)it->node)->children[0];
        }
      }
      return true;
    } else if (it->height >= it->max_height) {
      return false;
    } else {
      /* We have to ascend to the parent. */
      it->node = it->parents[it->height];
      it->index = it->indexes[it->height];
      it->height += 1;
    }
  }
}

void btree_map_iter_dealloc(BTreeMapIter *it) { DEALLOC(it->parents); }

#include <time.h>

int main(void) {
  BTreeMap map = btree_map_new();
  long start_time, end_time, elapsed;

  start_time = clock();
  for (K i = 0; i < 512; ++i) {
    // printf("SIZE: %zu ", map.size);
    btree_map_insert(&map, i, (V)i);
  }
  // Do something
  BTreeMapIter it = btree_map_iter(&map);
  K *key;
  V *value;
  while (btree_map_iter_next(&it, &key, &value)) {
  }
  end_time = clock();
  elapsed = (end_time - start_time) / 1000;
  printf("SIZE: %zu, in: %ld\n", map.size, elapsed);
  btree_map_iter_dealloc(&it);
  btree_map_dealloc(&map);
}
