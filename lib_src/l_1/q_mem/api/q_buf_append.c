/**
 * @file q_buf_append.c
 * @brief q_buf_append —— 追加数据（不足自动增长）
 */
#include "headers.h"
#include "q_mem.h"

int q_buf_append(q_buf_t *b, const void *data, size_t len)
{
    if (!b)
        return -1;
    if (len == 0)
        return 0;
    if (!data)
        return -1;

    if (b->len + len < b->len) /* 溢出保护 */
        return -1;

    if (b->len + len > b->cap)
    {
        size_t new_cap = b->cap ? b->cap : 64;
        while (new_cap < b->len + len)
        {
            size_t doubled = new_cap + (new_cap >> 1); /* 1.5x 增长 */
            if (doubled <= new_cap)                    /* 防溢出 */
                new_cap = b->len + len;
            else
                new_cap = doubled;
        }

        unsigned char *nd = (unsigned char *)realloc(b->data, new_cap);
        if (!nd)
            return -1;

        /* 新扩容区间立即清零（防堆残留旧敏感数据） */
        q_mem_zero(nd + b->len, new_cap - b->len);
        b->data = nd;
        b->cap = new_cap;
    }

    memcpy(b->data + b->len, data, len);
    b->len += len;
    return 0;
}
