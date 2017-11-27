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

#define _GNU_SOURCE

#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "map.h"
#include "util.h"

START_TEST(test_map_simple)
{
        UfHashmap *map = NULL;
        void *v = NULL;

        map = uf_hashmap_new(uf_hashmap_string_hash, uf_hashmap_string_equal);
        fail_if(!map, "Failed to construct string hashmap!");

        fail_if(!uf_hashmap_put(map, "charlie", UF_INT_TO_PTR(12)), "Failed to insert");
        fail_if(!uf_hashmap_put(map, "bob", UF_INT_TO_PTR(38)), "Failed to insert");

        v = uf_hashmap_get(map, "charlie");
        fail_if(!v, "Failed to get charlie");
        fail_if(UF_PTR_TO_INT(v) != 12, "Retrieved value is incorrect");
        v = NULL;

        v = uf_hashmap_get(map, "bob");
        fail_if(!v, "Failed to get bob");
        fail_if(UF_PTR_TO_INT(v) != 38, "Retrieved value is incorrect");

        uf_hashmap_free(map);
}
END_TEST

START_TEST(test_map_null_zero)
{
        UfHashmap *map = NULL;
        char *ret = NULL;

        /* Construct hashmap of int to string value to check null key */
        map = uf_hashmap_new_full(uf_hashmap_simple_hash, uf_hashmap_simple_equal, NULL, free);
        fail_if(!map, "Failed to construct hashmap");

        for (size_t i = 0; i < 1000; i++) {
                char *p = NULL;
                if (asprintf(&p, "VALUE: %ld", i) < 0) {
                        abort();
                }
                fail_if(!uf_hashmap_put(map, UF_INT_TO_PTR(i), p), "Failed to insert keypair");
        }

        ret = uf_hashmap_get(map, UF_INT_TO_PTR(0));
        fail_if(!ret, "Failed to retrieve key 0 (glibc NULL)");
        fail_if(strcmp(ret, "VALUE: 0") != 0, "Returned string is incorrect");

        uf_hashmap_free(map);
}
END_TEST

/**
 * Standard helper for running a test suite
 */
static int uf_test_run(Suite *suite)
{
        SRunner *runner = NULL;
        int n_failed = 0;

        runner = srunner_create(suite);
        srunner_run_all(runner, CK_VERBOSE);
        n_failed = srunner_ntests_failed(runner);
        srunner_free(runner);

        return n_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static Suite *test_create(void)
{
        Suite *s = NULL;
        TCase *tc = NULL;

        s = suite_create(__FILE__);
        tc = tcase_create(__FILE__);
        suite_add_tcase(s, tc);

        tcase_add_test(tc, test_map_simple);
        tcase_add_test(tc, test_map_null_zero);

        /* TODO: Add actual tests. */
        return s;
}

int main(__attribute__((unused)) int argc, __attribute__((unused)) char **argv)
{
        return uf_test_run(test_create());
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
