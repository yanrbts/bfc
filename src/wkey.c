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
#include "wkey.h"


#define MAXSTRING   128             /* largest output string */
#define MAXCSET     256             /* longest character set */
#define NPOS        ((size_t)-1)    /* invalid index for size_t's */

static wchar_t *wk_dupwcs(const wchar_t *s);
static void wk_copy_without_dupes(wchar_t *dest, wchar_t *src);
static size_t wk_force_wide_string(wchar_t *wout, const char *s, size_t n);
static size_t wk_make_wide_string(wchar_t *wout, 
                                const char *s, 
                                size_t n, 
                                int *is_unicode);

/*
 * init validated parameters passed to the program
 */
void wk_init_option(options_type *op) {
    op->low_charset = NULL;
    op->upp_charset = NULL;
    op->num_charset = NULL;
    op->sym_charset = NULL;
    op->pattern = NULL;
    op->literalstring = NULL;
    op->startstring = NULL;
    op->endstring = NULL;
    op->last_min = NULL;
    op->first_max = NULL;
    op->min_string = NULL;
    op->max_string = NULL;
    op->pattern_info = NULL;
}

/* replacement for wcsdup, where not avail (it's POSIX) */
static wchar_t *wk_dupwcs(const wchar_t *s) {
    size_t n;
    wchar_t *p = NULL;

    if (s != NULL) {
        n = (1 + wcslen(s)) * sizeof(wchar_t);
        p = (wchar_t *)malloc(n);
        if (p != NULL)
            memcpy(p, s, n);
    }
    return p;
}

/* Copy without duplicates 
 * This function only copies non-duplicate wide characters. */
static void wk_copy_without_dupes(wchar_t *dest, wchar_t *src) {
    size_t i;
    size_t len = wcslen(src);

    dest[0] = L'\0';

    for (i = 0; i < len; i++) {
        /* wcschr - search a wide character in a wide-character string */
        if (wcschr(dest, src[i]) == NULL) {
            /* Character not found. */
            wcsncat(dest, &src[i], 1);
        }
    }
}

static size_t wk_force_wide_string(wchar_t *wout, const char *s, size_t n) {
    size_t i;
    const unsigned char *ucp = (const unsigned char*)s;
    size_t slen = strlen(s);

    /*
     * Blindly convert all characters to the numerically equivalent wchars.
     * This is intended to be used after a call to mbstowcs fails.
     * Like mbstowcs(), output may not be null terminated if returns n
     */
    for (i = 0; i < n && i < slen; ++i) {
        wout[i] = (wchar_t)ucp[i];
    }

    if (i < n) wout[i] = 0;

    return i;
}

static size_t wk_make_wide_string(wchar_t *wout, 
                                const char *s, 
                                size_t n, 
                                int *is_unicode) {
    size_t stres;
    const char* cp;
    int contains_upp128 = 0;

    /*
     * If 's' contains a UTF-8 string which is not plain 7bit,
     * is_unicode is set nonzero and wout contains the proper wide string.
     * Otherwise the code points are assumed to be the exact values in s.
     * Unlike mbstowcs, result is always null terminated as long as n is nonzero.
     * Leave r_is_unicode undisturbed unless setting to nonzero!
     * (must never be changed from 1 to 0 regardless of this call's data)
     * 
     * Iterate through the input string s, checking for characters greater than 128 
     * (i.e., not 7-bit ASCII characters). If such characters exist, 
     * it indicates that the string is not a pure 7-bit UTF-8 string. 
     * Set is_unicode to a non-zero value to indicate that the input string is Unicode encoded.
     */
    for (cp = s; *cp; ++cp) {
        if ((int)*cp < 0) {
            /* string is Unicode encoded */
            contains_upp128 = 1;
            break;
        }
    }

    /* convert a multibyte string to a wide-character string */
    stres = mbstowcs(wout, s, n);
    if (stres != NPOS) {
        if (contains_upp128 && is_unicode)
            *is_unicode = 1;
    } else {
        stres = wk_force_wide_string(wout, s, n);
    }

    if (n != 0) wout[n-1] = 0;

    return stres;
}