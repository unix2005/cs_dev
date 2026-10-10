/**
 * @file q_ds_ut.c
 * @brief q_ds 单元测试小程序（构建后由 make ut 运行）
 */
#include "headers.h"
#include "q_ds.h"

#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, msg)                              \
    do                                                \
    {                                                 \
        if (!(cond))                                  \
        {                                             \
            printf("FAIL: %s\n", msg);                \
            g_fail++;                                 \
        }                                             \
        else                                          \
        {                                             \
            printf("PASS: %s\n", msg);               \
        }                                             \
    } while (0)

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

int main(void)
{
    /* ============ 双向链表 ============ */
    q_ds_list_t *l = q_ds_list_create();
    CHECK(l != NULL, "list create");
    int a = 1, b = 2, c = 3;
    q_ds_list_push_back(l, &a);
    q_ds_list_push_back(l, &b);
    q_ds_list_push_front(l, &c);
    CHECK(q_ds_list_size(l) == 3, "list size == 3");
    CHECK(q_ds_list_find(l, &b, cmp_int) == &b, "list find b");
    int x = 2;
    CHECK(q_ds_list_remove(l, &x, cmp_int) == 1, "list remove b");
    CHECK(q_ds_list_size(l) == 2, "list size == 2 after remove");
    CHECK(q_ds_list_pop_front(l) == &c, "list pop_front == c");
    q_ds_list_clear(l, NULL);
    CHECK(q_ds_list_size(l) == 0, "list clear -> size 0");
    q_ds_list_destroy(l);

    /* ============ 哈希表 ============ */
    q_ds_map_t *m = q_ds_map_create();
    CHECK(m != NULL, "map create");
    int v1 = 10, v2 = 20;
    q_ds_map_set(m, "alpha", &v1);
    q_ds_map_set(m, "beta", &v2);
    CHECK(q_ds_map_size(m) == 2, "map size == 2");
    CHECK(q_ds_map_has(m, "alpha") == 1, "map has alpha");
    CHECK(q_ds_map_get(m, "beta") == &v2, "map get beta");
    q_ds_map_set(m, "alpha", &v2); /* 覆盖 */
    CHECK(q_ds_map_get(m, "alpha") == &v2, "map overwrite alpha");
    CHECK(q_ds_map_del(m, "alpha") == 1, "map del alpha");
    CHECK(q_ds_map_has(m, "alpha") == 0, "map has alpha after del == 0");
    q_ds_map_destroy(m);

    /* ============ 动态数组 ============ */
    q_ds_vec_t *vec = q_ds_vec_create(2);
    CHECK(vec != NULL, "vec create");
    int e1 = 100, e2 = 200, e3 = 300;
    q_ds_vec_push(vec, &e1);
    q_ds_vec_push(vec, &e2);
    q_ds_vec_push(vec, &e3); /* 触发扩容 */
    CHECK(q_ds_vec_size(vec) == 3, "vec size == 3");
    CHECK(q_ds_vec_capacity(vec) >= 3, "vec capacity grew");
    CHECK(q_ds_vec_get(vec, 1) == &e2, "vec get idx1 == e2");
    q_ds_vec_set(vec, 0, &e3);
    CHECK(q_ds_vec_get(vec, 0) == &e3, "vec set idx0 == e3");
    CHECK(q_ds_vec_pop(vec) == &e3, "vec pop == e3");
    CHECK(q_ds_vec_size(vec) == 2, "vec size == 2 after pop");
    q_ds_vec_clear(vec, NULL);
    q_ds_vec_destroy(vec);

    if (g_fail == 0)
        printf("\nALL PASS\n");
    else
        printf("\n%d FAILED\n", g_fail);

    return g_fail == 0 ? 0 : 1;
}
