/**
 * @file q_ds.h
 * @brief l_1 基础层 数据结构库（libqds）对外头文件
 * @platform Windows/Linux/macOS
 * @layer  lib_src/l_1/q_ds  ->  产出 libqds
 * @note   本文件自包含：仅依赖公共基础头 include_stdio.h（提供 size_t/stdint/bool 等），
 *         故外部模块 `#include <q_ds.h>` 可独立编译，无需依赖内部聚合头 headers.h。
 *         api/ 下源文件仍只需 `#include "headers.h"` + `#include "q_ds.h"`。
 *
 * 提供的容器（均不透明句柄，数据以 void* 承载，生命周期由调用方或 free_fn 管理）：
 *   - q_ds_list：双向链表（头尾 O(1) 增删、线性查找）
 *   - q_ds_map ：字符串键哈希表（链地址法，覆盖/删除/查询均摊 O(1)）
 *   - q_ds_vec ：动态数组（自动扩容，下标访问 O(1)）
 */
#ifndef L_1_Q_DS_H
#define L_1_Q_DS_H

#include "include_stdio.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ===================== 双向链表 ===================== */
    typedef struct q_ds_list q_ds_list_t;

    q_ds_list_t *q_ds_list_create(void);
    void q_ds_list_destroy(q_ds_list_t *l);
    int q_ds_list_push_back(q_ds_list_t *l, void *data);
    int q_ds_list_push_front(q_ds_list_t *l, void *data);
    void *q_ds_list_pop_front(q_ds_list_t *l);
    void *q_ds_list_find(q_ds_list_t *l, const void *key, int (*cmp)(const void *a, const void *b));
    int q_ds_list_remove(q_ds_list_t *l, const void *key, int (*cmp)(const void *a, const void *b));
    size_t q_ds_list_size(q_ds_list_t *l);
    void q_ds_list_clear(q_ds_list_t *l, void (*free_fn)(void *));

    /* ===================== 哈希表（字符串键） ===================== */
    typedef struct q_ds_map q_ds_map_t;

    q_ds_map_t *q_ds_map_create(void);
    void q_ds_map_destroy(q_ds_map_t *m);
    int q_ds_map_set(q_ds_map_t *m, const char *key, void *value);
    void *q_ds_map_get(q_ds_map_t *m, const char *key);
    int q_ds_map_del(q_ds_map_t *m, const char *key);
    int q_ds_map_has(q_ds_map_t *m, const char *key);
    size_t q_ds_map_size(q_ds_map_t *m);
    void q_ds_map_clear(q_ds_map_t *m, void (*free_fn)(void *));

    /* ===================== 动态数组 ===================== */
    typedef struct q_ds_vec q_ds_vec_t;

    q_ds_vec_t *q_ds_vec_create(size_t cap);
    void q_ds_vec_destroy(q_ds_vec_t *v);
    int q_ds_vec_push(q_ds_vec_t *v, void *data);
    void *q_ds_vec_pop(q_ds_vec_t *v);
    void *q_ds_vec_get(q_ds_vec_t *v, size_t idx);
    int q_ds_vec_set(q_ds_vec_t *v, size_t idx, void *data);
    size_t q_ds_vec_size(q_ds_vec_t *v);
    size_t q_ds_vec_capacity(q_ds_vec_t *v);
    void q_ds_vec_clear(q_ds_vec_t *v, void (*free_fn)(void *));

#ifdef __cplusplus
}
#endif

#endif /* L_1_Q_DS_H */
