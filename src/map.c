/*
 * This file is part of libuf.
 *
 * Copyright © 2017 Ikey Doherty
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <assert.h>
#include <stdlib.h>

#include "map.h"
#include "util.h"

typedef struct UfHashmapNode UfHashmapNode;

/**
 * Initial size of 128 items. Slight overcommit but prevents too much future
 * growth as our growth ratio and algorithm is ^2 based.
 */
#define UF_HASH_INITIAL_SIZE 128

/**
 * 60% = full hashmap from our perspective.
 */
#define UF_HASH_FILL_RATE 0.6

/**
 * Instead of forcing a new field to account for NULL/0 glibc issues, we simply
 * always apply this mask to the hash field upon storage.
 *
 * This effectively acts as a cheap tombstone mechanism by flagging the key
 * within a Thing.
 */
#define UF_KEY_SET 0x000001

/**
 * Opaque UfHashmap implementation, simply an organised header for the
 * function pointers, state and buckets.
 */
struct UfHashmap {
        struct {
                UfHashmapNode *blob;      /**<Contiguous blob of buckets, over-commits */
                unsigned int max;         /**<How many buckets are currently allocated? */
                unsigned int current;     /**<How many items do we currently have? */
                unsigned int mask;        /**< pow2 n_buckets - 1 */
                unsigned int next_resize; /**<At what point do we perform resize? */
        } buckets;
        struct {
                uf_hashmap_hash_func hash;     /**<Key hash generator */
                uf_hashmap_equal_func compare; /**<Key value comparison */
        } key;
        struct {
                uf_hashmap_free_func key;   /**<Key free function */
                uf_hashmap_free_func value; /**<Value free function */
        } free;
};

/**
 * A UfHashmapNode is simply a single bucket within a UfHashmap and can have
 * a key/value. This is a chained mechanism.
 */
struct UfHashmapNode {
        void *key;
        void *value;
        struct UfHashmapNode *next;
        uint32_t hash;
};

UfHashmap *uf_hashmap_new(uf_hashmap_hash_func hash, uf_hashmap_equal_func compare)
{
        return uf_hashmap_new_full(hash, compare, NULL, NULL);
}

UfHashmap *uf_hashmap_new_full(uf_hashmap_hash_func hash, uf_hashmap_equal_func compare,
                               uf_hashmap_free_func key_free, uf_hashmap_free_func value_free)
{
        UfHashmap *ret = NULL;

        UfHashmap clone = {
                .key.hash = hash,
                .key.compare = compare,
                .free.key = key_free,
                .free.value = value_free,
                .buckets.blob = NULL,
                .buckets.current = 0,
                .buckets.max = UF_HASH_INITIAL_SIZE,
                .buckets.mask = UF_HASH_INITIAL_SIZE - 1,
                .buckets.next_resize = (int)(((double)UF_HASH_INITIAL_SIZE) * UF_HASH_FILL_RATE),
        };

        /* Some things we actually do need, sorry programmer. */
        assert(clone.key.hash);
        assert(clone.key.compare);

        ret = calloc(1, sizeof(struct UfHashmap));
        if (!ret) {
                return NULL;
        }
        *ret = clone;

        ret->buckets.blob = calloc((size_t)clone.buckets.max, sizeof(struct UfHashmapNode));
        if (!ret->buckets.blob) {
                uf_hashmap_free(ret);
                return NULL;
        }

        return ret;
}

static inline void bucket_free(UfHashmap *self, UfHashmapNode *node)
{
        if (!node) {
                return;
        }
        bucket_free(self, node->next);
        if (self->free.key) {
                self->free.key(node->key);
        }
        if (self->free.value) {
                self->free.value(node->value);
        }
        free(node);
}

void uf_hashmap_free(UfHashmap *self)
{
        if (uf_unlikely(!self)) {
                return;
        }
        for (size_t i = 0; i < self->buckets.max; i++) {
                UfHashmapNode *node = &self->buckets.blob[i];
                bucket_free(self, node->next);
        }
        free(self->buckets.blob);
        free(self);
        return;
}

bool uf_hashmap_simple_equal(const void *a, const void *b)
{
        return a == b;
}

uint32_t uf_hashmap_simple_hash(const void *v)
{
        return UF_PTR_TO_INT(v);
}

/**
 * Find the base bucket to work from
 */
static inline UfHashmapNode *uf_hashmap_initial_bucket(UfHashmap *self, uint32_t hash)
{
        return &self->buckets.blob[hash & self->buckets.mask];
}

bool uf_hashmap_put(UfHashmap *self, void *key, void *value)
{
        UfHashmapNode *bucket = NULL;
        uint32_t hash;
        UfHashmapNode *candidate = NULL;

        if (uf_unlikely(!self)) {
                return false;
        }

        /* Ensure we have at least key *and* value together */
        if (uf_unlikely(!key && !value)) {
                return true;
        }

        hash = self->key.hash(key);

        /* Cheat for now. Soon, we need to handle collisions */
        bucket = uf_hashmap_initial_bucket(self, hash);

        for (UfHashmapNode *node = bucket; node; node = node->next) {
                /* Root node isn't occupied */
                if (node->hash == 0) {
                        candidate = node;
                        /* We have more buckets used now */
                        ++self->buckets.current;
                        break;
                }

                /* Attempt to find dupe */
                if (uf_unlikely(self->key.compare(node->key, key))) {
                        candidate = node;
                        /* Replacing a bucket, no diff in count */
                        break;
                }
        }

        /* Displace an existing node */
        if (uf_unlikely(candidate != NULL)) {
                if (self->free.key) {
                        self->free.key(candidate->key);
                        self->free.value(candidate->value);
                }
                goto insert_bucket;
        }

        /* Construct a new input node */
        candidate = calloc(1, sizeof(UfHashmapNode));
        if (uf_unlikely(!candidate)) {
                return false;
        }

        candidate->next = bucket->next;
        bucket->next = candidate;
        ++self->buckets.current;

insert_bucket:
        /* Prepend and balance leaf */
        candidate->hash = hash ^ UF_KEY_SET; /* We have something stored. */
        candidate->key = key;
        candidate->value = value;

        return true;
}

void *uf_hashmap_get(UfHashmap *self, void *key)
{
        UfHashmapNode *bucket = NULL;
        uint32_t hash;

        if (uf_unlikely(!self)) {
                return NULL;
        }

        hash = self->key.hash(key);
        bucket = uf_hashmap_initial_bucket(self, hash);

        for (UfHashmapNode *node = bucket; node; node = node->next) {
                if (self->key.compare(node->key, key)) {
                        return node->value;
                }
        }

        return NULL;
}
/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 expandtab:
 * :indentSize=8:tabSize=8:noTabs=true:
 */
