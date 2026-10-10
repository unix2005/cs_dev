/**
 * @file q_ds_internal.h
 * @brief q_ds 内部结构与辅助（仅被 headers.h 引入，外部不可见）
 */
#ifndef Q_DS_INTERNAL_H
#define Q_DS_INTERNAL_H

/* 哈希表桶数（链式法解决冲突） */
#define Q_DS_MAP_BUCKETS 64

/* 双向链表节点与容器 */
struct q_ds_list_node
{
    void *data;
    struct q_ds_list_node *prev;
    struct q_ds_list_node *next;
};

struct q_ds_list
{
    struct q_ds_list_node *head;
    struct q_ds_list_node *tail;
    size_t size;
};

/* 动态数组 */
struct q_ds_vec
{
    void **items;
    size_t size;
    size_t cap;
};

/* 哈希表条目与容器（字符串键） */
struct q_ds_map_entry
{
    char *key;
    void *value;
    struct q_ds_map_entry *next;
};

struct q_ds_map
{
    struct q_ds_map_entry **buckets;
    size_t bucket_count;
    size_t size;
};

/* FNV-1a 字符串哈希，供 map 内部寻桶 */
static inline unsigned long q_ds_hash_str(const char *s)
{
    unsigned long h = 1469598103934665603UL;
    if (!s)
        return 0;
    for (; *s; s++)
    {
        h ^= (unsigned char)(*s);
        h *= 1099511628211UL;
    }
    return h;
}

#endif /* Q_DS_INTERNAL_H */
