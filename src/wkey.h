/*
 * Copyright 2024-2024 yanruibinghxu
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef __WKEY_H__
#define __WKEY_H__

#include <assert.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>
#include <sys/types.h>

/* program options */
typedef struct opts_struct {
    wchar_t *low_charset;
    wchar_t *upp_charset;
    wchar_t *num_charset;
    wchar_t *sym_charset;
    size_t clen, ulen, nlen, slen;
    wchar_t *pattern;
    size_t plen;
    wchar_t *literalstring;
    wchar_t *startstring;
    wchar_t *endstring;
    size_t duplicates[4];       /* allowed number of duplicates for each charset */
    size_t min, max;
    wchar_t *last_min;          /* last string of length min */
    wchar_t *first_max;         /* first string of length max */
    wchar_t *min_string;
    wchar_t *max_string;        /* either startstring/endstring or calculated using the pattern */
    struct pinfo *pattern_info; /* information generated from pattern */
} options_type;

typedef struct wkey {
    pthread_t threads;
    FILE *fp;

} wkey;

void wk_init_option(options_type *op);
void wk_start(int argc, char **argv);

#endif