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
  /* The node pointer is never NULL, unless this is an empty root node.
     This may be an internal node or a leaf node. */
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

/* Check whether this node_ref references a leaf node */
static inline bool node_ref_is_leaf(struct node_ref node_ref) {
  return node_ref.height == 0;
}

static inline bool node_ref_is_full(struct node_ref node_ref) {
  return node_ref.node->len == CAPACITY;
}

/* Get a reference to a child node.
   Warning causes undefined behavior if node_ref is not an internal node or if
   index is not a valid child node. */
static struct node_ref node_ref_descend(struct node_ref node_ref,
                                        ushort index) {
  assert(node_ref.height != 0);
  assert(index <= node_ref.node->len);
  assert(((struct inode *)node_ref.node)->children[index] != NULL);
  return (struct node_ref){((struct inode *)node_ref.node)->children[index],
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
static struct kv node_ref_split_leaf_data(struct leaf_node *old_node,
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
    struct kv kv = node_ref_split_leaf_data(node.node, new_leaf, index);
    return (struct split){new_leaf, kv};
  } else {
    struct inode *old_inode = (struct inode *)node.node;
    struct inode *new_inode = inode_new();
    struct kv kv = node_ref_split_leaf_data(node.node, &new_inode->data, index);
    ushort old_len = node.node->len;
    ushort new_len = new_inode->data.len;
    memcpy(new_inode->children, &old_inode->children[index + 1],
           (new_len + 1) * sizeof(struct leaf_node *));
    return (struct split){(struct leaf_node *)new_inode, kv};
  }
}

/* Search for a key inside a node, this uses a linear search, a binary search
   algorithm could improve performance only if B was a lot higher. Since we
   search on short arrays (11 elements) linear search is actually faster. */
static ushort node_ref_search(struct node_ref node_ref, const K *key,
                              bool *found) {
  ushort i = 0;
  for (; i < node_ref.node->len; ++i) {
    char cmp = COMPARE(key, &node_ref.node->keys[i]);
    if (cmp == 0) {
      *found = true;
    }
    if (cmp <= 0) {
      return i;
    }
  }
  return i;
}

void memmove1(void *dest, void *src, size_t n) { memmove(dest, src, n); }

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
            (node->data.len - index) * sizeof(struct leaf_node *));
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
  node_insert_child((struct inode *)node_ref.node, index, child);

  return split_none();
}

static struct split node_insert_recursive(struct node_ref node_ref, K key,
                                          V value) {
  bool found = false;
  ushort index = node_ref_search(node_ref, &key, &found);

  if (found) {
    /* We found the key already in the tree, just update the value. */
    node_ref.node->vals[index] = value;
  } else if (node_ref_is_leaf(node_ref)) {
    /* This is a leaf, insert the key and value */
    return node_insert(node_ref, index, key, value);
  } else {
    /* Didn't find the key, descend */
    struct split child_split =
        node_insert_recursive(node_ref_descend(node_ref, index), key, value);
    if (child_split.node != NULL) {
      /* The child was split */
      return node_insert_with_child(node_ref, index, child_split.kv.k,
                                    child_split.kv.k, child_split.node);
    }
  }

  return split_none();
}

static void node_ref_dealloc(struct node_ref node_ref) {
  if (node_ref_is_leaf(node_ref)) {
    DEALLOC(node_ref.node);
  } else {
    for (ushort index = 0; index <= node_ref.node->len; ++index) {
      node_ref_dealloc(node_ref_descend(node_ref, index));
    }
    DEALLOC(node_ref.node);
  }
}

V *btree_map_get(BTreeMap *map, K *const key) {
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
    map->root = new_root;
    map->height = 0;
    return;
  }

  struct split split =
      node_insert_recursive(node_ref_from_root(map), key, value);

  if (split.node != NULL) {
    /* Root was split. Create a new internal node and treat the old root and the
       split as child nodes. */
    struct inode *new_root = inode_new();

    node_insert_unchecked(&new_root->data, 0, split.kv.k, split.kv.v);
    new_root->children[0] = node_ref_from_root(map).node;
    new_root->children[1] = split.node;

    map->root = new_root;
    /* Increase the height of the root */
    map->height += 1;
  }
}

/* deallocate the whole tree */
void btree_map_dealloc(BTreeMap *map) {
  if (map->root != NULL) {
    node_ref_dealloc(node_ref_from_root(map));
  }
}

void btree_map_clear(BTreeMap *map) {
  btree_map_dealloc(map);
  map->size = 0;
  map->root = NULL;
}

static void print_leaf(struct leaf_node *leaf) {
  for (ushort i = 0; i < leaf->len; ++i) {
    printf("%i => %i\n", leaf->keys[i], leaf->vals[i]);
  }
}

static void print_tree(struct node_ref node_ref) {
  printf("FULL TREE\n");
  print_leaf(node_ref.node);
  if (node_ref.height)
    for (ushort i = 0; i <= node_ref.node->len; ++i) {
      printf("%u: {", i);
      if (node_ref.height <= 1) {
        print_leaf(node_ref_descend(node_ref, i).node);
      } else {
        print_tree(node_ref_descend(node_ref, i));
      }
      printf("}\n");
    }
}

int main(void) {
  BTreeMap map = btree_map_new();
  for (int i = 0; i < 4096; ++i) {
    btree_map_insert(&map, i, i);
  }
  printf("\n");
  for (int i = 0; i < 4096; ++i) {
    if (i != *btree_map_get(&map, &i)) {
      printf("ERROR ");
    }
    printf("%i ", *btree_map_get(&map, &i));
  }
  printf("\n");
  btree_map_dealloc(&map);
  printf("%p\n", map.root);
}
