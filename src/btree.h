/*
 * Copyright 2013 Jung-Sang Ahn <jungsang.ahn@gmail.com>.
 * All Rights Reserved.
 */

#ifndef _JSAHN_BTREE_H
#define _JSAHN_BTREE_H

#include <stdint.h>
#include "common.h"

#define _get_kvsize(kvsize, ksize, vsize) \
    (ksize) = ((kvsize) & 0xf0) >> 4;    \
    (vsize) = ((kvsize) & 0x0f)
#define __ksize(kvsize) (((kvsize) & 0xf0) >> 4)
#define __vsize(kvsize) (((kvsize) & 0x0f))


#define BTREE_BLK_NOT_FOUND BLK_NOT_FOUND

typedef enum {
    BTREE_RESULT_SUCCESS,
    BTREE_RESULT_UPDATE,
    BTREE_RESULT_FAIL
} btree_result;

//#define _BTREE_32BIT_IDX
#ifdef _BTREE_32BIT_IDX
    typedef uint32_t idx_t;
    #define BTREE_IDX_NOT_FOUND 0xffffffff
#else
    typedef uint16_t idx_t;
    #define BTREE_IDX_NOT_FOUND 0xffff
#endif

typedef uint8_t bnode_flag_t;

struct bnode{
    uint8_t kvsize;
    bnode_flag_t flag;
    uint16_t level;
    idx_t nentry;
    // array of key value pair ([k1][v1][k2][v2]...)
    void *data;
};
#define BNODE_MASK_ROOT 0x1
#define BNODE_MASK_METADATA 0x2
#define BNODE_MASK_SEQTREE 0x4

typedef uint8_t metasize_t;
struct btree_meta{
    metasize_t size;
    void *data;
};

typedef void* voidref;
typedef struct bnode* bnoderef;

struct btree_blk_ops {
    voidref (*blk_alloc)(void *handle, bid_t *bid);
    voidref (*blk_read)(void *handle, bid_t bid);
    voidref (*blk_move)(void *handle, bid_t bid, bid_t *new_bid);
    int (*blk_is_writable)(void *handle, bid_t bid);
    void (*blk_set_dirty)(void *handle, bid_t bid);
    void (*blk_operation_end)(void *handle); // optional
#ifdef _BNODE_COMP
    void (*blk_set_uncomp_size)(void *handle, bid_t bid, size_t uncomp_size);
    size_t (*blk_comp_size)(void *handle, bid_t bid);
#endif
};

struct btree {
    uint8_t ksize;
    uint8_t vsize;
    uint32_t blksize;
    bid_t root_bid;
    bnode_flag_t root_flag;
    uint16_t height;
    void *blk_handle;
    struct btree_blk_ops *blk_ops;
    struct btree_kv_ops *kv_ops;
};

struct btree_kv_ops {
    void (*get_kv)(struct bnode *node, idx_t idx, void *key, void *value);
    void (*set_kv)(struct bnode *node, idx_t idx, void *key, void *value);
    void (*ins_kv)(struct bnode *node, idx_t idx, void *key, void *value);
    void (*copy_kv)(struct bnode *node_dst, struct bnode *node_src, idx_t dst_idx, idx_t src_idx, idx_t len);

    // return node size after inserting list of key/value pairs
    size_t (*get_data_size)(struct bnode *node, void *key_arr, void *value_arr, size_t len);
    // return (actual) key value size
    size_t (*get_kv_size)(struct btree *tree, void *key, void *value);

    void (*init_kv_var)(struct btree *tree, void *key, void *value);
    void (*free_kv_var)(struct btree *tree, void *key, void *value);
    
    void (*set_key)(struct btree *tree, void *dst, void *src);
    void (*set_value)(struct btree *tree, void *dst, void *src);

    void (*get_nth_idx)(struct bnode *node, idx_t num, idx_t den, idx_t *idx);
    //void (*get_nth_splitter)(struct bnode *node, idx_t num, idx_t den, void *key);
    void (*get_nth_splitter)(struct bnode *prev_node, struct bnode *node, void *key);

    int (*cmp)(void *key1, void *key2);
    bid_t (*value2bid)(void *value);
    voidref (*bid2value)(bid_t *bid);
};

struct btree_iterator {
    struct btree btree;
    void *curkey;
    bid_t *bid;
    idx_t *idx;
    struct bnode **node;
};

typedef void btree_print_func(struct btree *btree, void *key, void *value);
void btree_print_node(struct btree *btree, btree_print_func func);

metasize_t btree_read_meta(struct btree *btree, void *buf);
void btree_update_meta(struct btree *btree, struct btree_meta *meta);
btree_result btree_init_from_bid(
        struct btree *btree, void *blk_handle,
        struct btree_blk_ops *blk_ops,     struct btree_kv_ops *kv_ops,
        uint32_t nodesize, bid_t root_bid);
btree_result btree_init(
        struct btree *btree, void *blk_handle,
        struct btree_blk_ops *blk_ops,     struct btree_kv_ops *kv_ops,
        uint32_t nodesize, uint8_t ksize, uint8_t vsize,
        bnode_flag_t flag, struct btree_meta *meta);

btree_result btree_iterator_init(struct btree *btree, struct btree_iterator *it, void *initial_key);
btree_result btree_iterator_free(struct btree_iterator *it);
btree_result btree_next(struct btree_iterator *it, void *key_buf, void *value_buf);

btree_result btree_find(struct btree *btree, void *key, void *value_buf);
btree_result btree_insert(struct btree *btree, void *key, void *value);
btree_result btree_remove(struct btree *btree, void *key);
btree_result btree_operation_end(struct btree *btree);

#endif