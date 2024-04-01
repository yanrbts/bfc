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

static const wchar_t def_low_charset[] = L"abcdefghijklmnopqrstuvwxyz";
static const wchar_t def_upp_charset[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const wchar_t def_num_charset[] = L"0123456789";
static const wchar_t def_sym_charset[] = L"!@#$%^&*()-_+=~`[]{}|\\:;\"'<>,.?/ ";

static size_t inverted = 0;                 /* 0 for normal output 1 for aaa,baa,caa,etc */
static unsigned long long bytecount = 0;    /* user specified break output into size */
static unsigned long long linecount = 0;    /* user specified break output into count lines */
static options_type options;                /* store validated parameters passed to the program */

static wchar_t *wk_dupwcs(const wchar_t *s);
static void wk_copy_without_dupes(wchar_t *dest, wchar_t *src);
static size_t wk_force_wide_string(wchar_t *wout, const char *s, size_t n);
static size_t wk_make_wide_string(wchar_t *wout, 
                                const char *s, 
                                size_t n, 
                                int *is_unicode);
static int wk_copy(wchar_t *dest, const char *src, int *is_unicode);
static int wk_parse_size(char *s, size_t *calc);
static int wk_parse_number(char *s, size_t max, size_t *calc);
static int wk_dupskip(char *s, options_type *op);
static wchar_t *wk_endstring(char *s, int *is_unicode);
static wchar_t *wk_alloc_wide_string(const char *s, int *is_unicode);

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

    for (int i = 0; i < 4; i++)
        op->duplicates[i] = NPOS;
}

void wk_start(int argc, char **argv) {
    int i = 0;
    size_t calc = 0; /* recommend count */
    size_t min, max;
    wchar_t *endstr = NULL;
    wchar_t *literalstring = NULL; /* user passed something using -l */
    int is_unicode = 0;

    wk_init_option(&options);

    min = (size_t)atoi(argv[1]);
    max = (size_t)atoi(argv[2]);

    if (max < min) {
        fprintf(stderr,"Starting length is greater than the ending length\n");
        goto err;
    }

    if (max > MAXSTRING) {
        fprintf(stderr,"Crunch can only make words with a length of less than %d characters\n", MAXSTRING+1);
        goto err;
    }

    for (; i < argc; i += 2) {
        if (strncmp(argv[i], "-b", 2) == 0) {
            /* user wants to split files by size */
            if (i+1 < argc) {
                if (wk_parse_size(argv[i+1], &calc) == -1) goto err;
            } else {
                fprintf(stderr,"Please specify a value\n");
                goto err;
            }
        }

        if (strncmp(argv[i], "-c", 2) == 0) {
            if (i+1 < argc) {
                if (wk_parse_number(argv[i+1], &max, &calc) == -1) goto err;
            } else {
                fprintf(stderr,"Please specify the number of lines you want\n");
            }
        }

        if (strncmp(argv[i], "-d", 2) == 0) {
            if (i+1 < argc) {
                if (wk_dupskip(argv[i+1], &options) == -1) goto err;
            } else {
                fprintf(stderr,"Please specify the type of duplicates to skip\n");
                goto err;
            }
        }

        if (strncmp(argv[i], "-e", 2) == 0) {
            if (i+1 < argc) {
                endstr = wk_endstring(argv[i+1], &is_unicode);
            } else {
                fprintf(stderr,"Please specify the string you want crunch to stop at\n");
                goto err;
            }
        }
        /* user wants to invert output calculation */
        if (strncmp(argv[i], "-i", 2) == 0) {
            inverted = 1;
            i--; /* decrease by 1 since -i has no parameter value */
        }
        /* user wants to list literal characters */
        if (strncmp(argv[i], "-l", 2) == 0) {
            if (i+1 < argc) {
                literalstring = wk_alloc_wide_string(argv[i+1], &is_unicode);
            } else {
                fprintf(stderr,"Please specify a list of characters you want to treat as literal @?%%^\n");
                goto err;
            }
        }
    }
err:
    exit(EXIT_FAILURE);
}

/* replacement for wcsdup, where not avail (it's POSIX) 
 * The return value needs to be released by calling free */
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

static int wk_copy(wchar_t *dest, const char *src, int *is_unicode) {
    size_t slen;
    wchar_t *tpwc = NULL;

    slen = strlen(src) + 1;

    tpwc = (wchar_t*)malloc(slen * sizeof(wchar_t));

    if (tpwc == NULL) {
        fprintf(stderr,"[BFC] can't allocate memory for user charset\n");
        goto err;
    }
    (void)wk_make_wide_string(tpwc, src, slen, is_unicode);
    wk_copy_without_dupes(dest, tpwc);
    free(tpwc);

    return 0;
err:
    return -1;
}

/*
 * Parse the following command:
 * Specifies the size of the output file, only works if -o START is used, i.e.: 60MB
 * -b 4mb (4gb ..)
 */
static int wk_parse_size(char *s, size_t *calc) {
    size_t slen;
    
    int i, multi = 0;

    if (s == NULL) {
        fprintf(stderr,"bvalue has a serious problem\n");
        return -1;
    }

    slen = strlen(s);
    for (i = 0; i < slen; i++)
        s[i] = tolower(s[i]);
    
    if (strstr(s, "kb") != 0) multi = 1000;
    else if (strstr(s, "mb") != 0) multi = 1000000;
    else if (strstr(s, "gb") != 0) multi = 1000000000;
    else if (strstr(s, "kib") != 0) multi = 1024;
    else if (strstr(s, "mib") != 0) multi = 1048576;
    else if (strstr(s, "gib") != 0) multi = 1073741824;

    *calc = strtoul(s, NULL, 10);
    bytecount = (*calc) * multi;
    if (calc > 4 && multi >= 1073741824 && bytecount <= 4294967295ULL) {
        fprintf(stderr,"ERROR: Your system is unable to process numbers greater than 4.294.967.295. Please specify a filesize <= 4GiB.\n");
        return -1;
    }
    return 0;
}

static int wk_parse_number(char *s, size_t max, size_t *calc) {
    if (s == NULL) return -1;

    linecount = strtoul(s, NULL, 10);
    
    if ((linecount * max) > 2147483648UL) {
        *calc = (2147483648UL / (unsigned long)max);
        fprintf(stderr,"WARNING: resulting file will probably be larger than 2GB \n");
        fprintf(stderr,"Some applications (john the ripper) can't use wordlists greater than 2GB\n");
        fprintf(stderr,"A value of %lu ", *calc);
        fprintf(stderr,"or less should result in a file less than 2GB\n");
        fprintf(stderr,"The above value is calcualated based on 2147483648UL/max\n");

        return -1;
    }
    return 0;
}

/* specify duplicates to skip */
static int wk_dupskip(char *s, options_type *op) {
    size_t dupvalue;
    char *endptr; /* temp pointer for duplicates option */

    if (s == NULL) return -1;

    dupvalue = (size_t)strtoul(s, &endptr, 10);
    if (*endptr == s) {
        fprintf(stderr,"-d must be followed by [n][@,%%^]\n");
        return -1;
    }

    if ((*endptr) == '\0')
        op->duplicates[0] = dupvalue;
    while (*endptr != '\0') {
        switch (*endptr) {
        case '@': options.duplicates[0] = dupvalue; break;
        case ',': options.duplicates[1] = dupvalue; break;
        case '%': options.duplicates[2] = dupvalue; break;
        case '^': options.duplicates[3] = dupvalue; break;
        default:
            fprintf(stderr,"the type of duplicates must be one of [@,%%^]\n");
            return -1;
        }
        endptr++;
    }
    return 0;
}

static wchar_t *wk_endstring(char *s, int *is_unicode) {
    size_t slen;
    wchar_t *endstr;

    slen = strlen(s) + 1;

    endstr = (wchar_t *)malloc(slen * sizeof(wchar_t));
    if (endstr == NULL) {
        fprintf(stderr,"Error: Can;t allocate mem for endstring\n");
        return NULL;
    }
    (void)wk_make_wide_string(endstr, s, slen, is_unicode);
    return endstr;
}

static wchar_t *wk_alloc_wide_string(const char *s, int *is_unicode) {
    wchar_t *wstr = NULL;
    size_t len = s ? strlen(s)+1 : 1;

    wstr = (wchar_t *)malloc(len * sizeof(wchar_t));
    if (wstr == NULL) {
        fprintf(stderr,"wk_alloc_wide_string: Can't allocate mem!\n");
        exit(EXIT_FAILURE);
    }
    (void)make_wide_string(wstr, s ? s : "", len, is_unicode);
    return wstr;
}