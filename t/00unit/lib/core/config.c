/*
 * Copyright (c) 2022 Fastly, Kazuho Oku
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <string.h>
#include "../../test.h"

static void test_register_path(void)
{
    vhttp_globalconf_t global;
    vhttp_config_init(&global);
    vhttp_hostconf_t *host = vhttp_config_register_host(&global, vhttp_iovec_init(vhttp_STRLIT("www.example.com")), 8080);

    vhttp_config_register_path(host, "/", 0);
    vhttp_config_register_path(host, "/a", 0);
    vhttp_config_register_path(host, "/c", 0);
    vhttp_config_register_path(host, "/a/1", 0);
    vhttp_config_register_path(host, "/b", 0);

#define CHECK(index, literal)                                                                                                      \
    do {                                                                                                                           \
        vhttp_pathconf_t *p = host->paths.entries[index];                                                                            \
        ok(vhttp_memis(p->path.base, p->path.len, vhttp_STRLIT(literal)));                                                             \
    } while (0)
    CHECK(0, "/a/1");
    CHECK(1, "/a");
    CHECK(2, "/b");
    CHECK(3, "/c");
    CHECK(4, "/");
#undef CHECK
}

void test_lib__core_config_c(void)
{
    subtest("register-path", test_register_path);
}
