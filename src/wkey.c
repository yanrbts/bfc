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

static size_t inc[128];
static size_t numofelements = 0;
static size_t inverted = 0;                 /* 0 for normal output 1 for aaa,baa,caa,etc */
static unsigned long long bytecount = 0;    /* user specified break output into size */
static unsigned long long linecount = 0;    /* user specified break output into count lines */
static options_type options;                /* store validated parameters passed to the program */
static struct thread_data my_thread;

/*
 * Need a largish global buffer for converting wide chars to multibyte
 * to avoid doing an alloc on every single output line
 * leading to heap frag.  Only alloc'd once at startup.
 * Size is MAXSTRING*MB_CUR_MAX+1
 */
static char *gconvbuffer = NULL;
static size_t gconvlen = 0;

static wchar_t *wk_dupwcs(const wchar_t *s);
static void wk_copy_without_dupes(wchar_t *dest, wchar_t *src);
static size_t wk_force_wide_string(wchar_t *wout, const char *s, size_t n);
static size_t wk_make_wide_string(wchar_t *wout, 
                                const char *s, 
                                size_t n, 
                                int *is_unicode);
static int wk_copy(wchar_t *dest, const char *src, int *is_unicode);
static int wk_parse_size(char *s, size_t *calc);
static int wk_parse_number(const char *s, size_t max, size_t *calc);
static int wk_dupskip(const char *s, options_type *op);
static wchar_t *wk_endstring(const char *s, int *is_unicode);
static wchar_t *wk_alloc_wide_string(const char *s, int *is_unicode);
static int wk_file(const char *s, char **fpath, char **tmpf, char **outputf);
static int wk_wordarray(const char *s, wchar_t ***warray, char **argv, int i, int *is_unicode);
static wchar_t **wk_readpermute(const char *filename, int *is_unicode);
static int wk_copy_charset(int argc, char **argv, int *i, wchar_t **c, int *is_unicode);
static int wk_check_member(const wchar_t *string1, const options_type *options);
static int wk_check_start_end(wchar_t *cset, wchar_t *start, wchar_t *end);
static int wk_default_literalstring(size_t max, wchar_t **wstr);
static size_t wk_find_index(const wchar_t *cset, size_t clen, wchar_t tofind);
static int wk_too_many_duplicates(const wchar_t *block, const options_type options);
static int wk_fill_minmax_strings(options_type *options);
static int wcstring_cmp(const void *a, const void *b);
static void wk_fill_pattern_info(options_type *options);
static wchar_t *wk_resumesession(const char *fpath, const wchar_t *charset);

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

int main(int argc, char **argv) {
    int i = 3;                      /* minimum number of parameters */
    size_t calc = 0;                /* recommend count */
    size_t min, max;
    wchar_t *endstr = NULL;         /* hold -e option */
    wchar_t *literalstring = NULL;  /* user passed something using -l */
    int is_unicode = 0;
    size_t flag = 0;                /* 0 for chunk 1 for permute */
    size_t flag4 = 0;               /* 0 don't create thread, 1 create print % done thread */
    size_t resume = 0;              /* 0 new session 1 for resume */
    char *outputf = NULL;           /* user specified filename to write output to */
    char *tmpf = NULL;
    char *fpath = NULL;             /* path to outputfilename if specified*/
    wchar_t **wordarray = NULL;     /* array to store words */
    wchar_t *startblock = NULL;     /* user specified starting point */
    wchar_t *pattern = NULL;        /* user specified pattern */
    char *compressalgo = NULL;      /* user specified compression program */
    char *tempfilename = NULL;
    wchar_t *charset;               /* character set */
    wchar_t *upp_charset = NULL;
    wchar_t *num_charset = NULL;
    wchar_t *sym_charset = NULL;

    wk_init_option(&options);

    gconvlen = MAXSTRING * MB_CUR_MAX + 1;
    gconvbuffer = (char*)malloc(gconvlen);
    if (gconvbuffer == NULL) {
        fprintf(stderr,"Error: Failed to allocate memory\n");
        goto err;
    }

    charset = wk_dupwcs(def_low_charset);
    if (charset == NULL) {
        fprintf(stderr,"bfc: can't allocate memory for default charset\n");
        goto err;
    }

    upp_charset = wk_dupwcs(def_upp_charset);
    if (upp_charset == NULL) {
        fprintf(stderr,"bfc: can't allocate memory for default upp_charset\n");
        goto err;
    }

    num_charset = wk_dupwcs(def_num_charset);
    if (num_charset == NULL) {
        fprintf(stderr,"bfc: can't allocate memory for default num_charset\n");
        goto err;
    }

    sym_charset = wk_dupwcs(def_sym_charset);
    if (sym_charset == NULL) {
        fprintf(stderr,"crunch: can't allocate memory for default sym_charset\n");
        goto err;
    }

    my_thread.finallinecount = 0;
    my_thread.linecounter = 0;
    my_thread.linetotal = 0;

    if (argc >= 4) {
        if (wk_copy_charset(argc, argv, &i, &charset, &is_unicode) == -1) goto err;
        if (wk_copy_charset(argc, argv, &i, &upp_charset, &is_unicode) == -1) goto err;
        if (wk_copy_charset(argc, argv, &i, &num_charset, &is_unicode) == -1) goto err;
        if (wk_copy_charset(argc, argv, &i, &sym_charset, &is_unicode) == -1) goto err;
    }

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
                if (wk_parse_number(argv[i+1], max, &calc) == -1) goto err;
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
        /* outputfilename specified */
        if (strncmp(argv[i], "-o", 2) == 0) {
            flag4 = 1;
            if (i+1 < argc) {
                if (wk_file(argv[i+1], &fpath, &tmpf, &outputf) == -1) goto err;
            } else {
                fprintf(stderr,"Please specify a output filename\n");
                goto err;
            }
        }
        /* user specified letters/words to permute */
        if (strncmp(argv[i], "-p", 2) == 0) {
            if (i+1 < argc) {
                flag = 1;
                numofelements = (size_t)(argc-i)-1;
                
                if (wk_wordarray(argv[i+1], &wordarray, argv, i, &is_unicode) == -1) goto err;
            } else {
                fprintf(stderr,"Please specify a word or words to permute\n");
                goto err;
            }
        }
        /* user specified file of words to permute */
        if (strncmp(argv[i], "-q", 2) == 0) {
            if (i+1 < argc) {
                wordarray = wk_readpermute(argv[i+1], &is_unicode);
                if (wordarray == NULL) goto err;
                qsort(wordarray, numofelements, sizeof(char *), wcstring_cmp);
                flag = 1; 
            } else {
                fprintf(stderr,"Please specify a filename for permute to read\n");
                goto err;
            }
        }
        /* user wants to resume a previous session */
        if (strncmp(argv[i], "-r", 2) == 0) {
            resume = 1;
            i--; /* decrease by 1 since -r has no parameter value */
        }
        /* startblock specified */
        if (strncmp(argv[i], "-s", 2) == 0) {
            if (i+1 < argc && argv[i+1]) {
                startblock = wk_alloc_wide_string(argv[i+1], &is_unicode);
                if (wcslen(startblock) != min) {
                    free(startblock);
                    fprintf(stderr,"Warning: minimum length should be %d\n", (int)wcslen(startblock));
                    goto err;
                }
            } else {
                fprintf(stderr,"Please specify the word you wish to start at\n");
                goto err;
            }
        }
        /* pattern specified */
        if (strncmp(argv[i], "-t", 2) == 0) {
            if (i+1 < argc) {
                pattern = wk_alloc_wide_string(argv[i+1], &is_unicode);

                if ((max > wcslen(pattern)) || (min < wcslen(pattern))) {
                    fprintf(stderr,"The maximum and minimum length should be the same size as the pattern you specified. \n");
                    fprintf(stderr,"min = %d  max = %d  strlen(%s)=%d\n",(int)min, (int)max, argv[i+1], (int)wcslen(pattern));
                    goto err;
                }
            } else {
                fprintf(stderr,"Please specify a pattern\n");
                goto err;
            }
        }
        /* suppress filesize info */
        if (strncmp(argv[i], "-u", 2) == 0) {
            fprintf(stderr,"Disabling printpercentage thread.  NOTE: MUST be last option\n\n");
            flag4=0;
            i--;
        }
        /* compression algorithm specified */
        if (strncmp(argv[i], "-z", 2) == 0) {
            if (i+1 < argc) {
                compressalgo = argv[i+1];
                if ((compressalgo != NULL)
                    && (strcmp(compressalgo, "gzip") != 0) 
                    && (strcmp(compressalgo, "bzip2") != 0) 
                    && (strcmp(compressalgo, "lzma") != 0) 
                    && (strcmp(compressalgo, "7z") != 0)) {
                    fprintf(stderr,"Only gzip, bzip2, lzma, and 7z are supported\n");
                    goto err;
                }
            } else {
                fprintf(stderr,"Only gzip, bzip2, lzma, and 7z are supported\n");
                goto err;
            }
        }
    } /* end parameter processing */

    /* parameter validation */
    if (literalstring != NULL && pattern == NULL) {
        fprintf(stderr,"you must specify -t when using -l\n");
        goto err;
    }

    if ((literalstring != NULL) && (pattern != NULL)) {
        if (wcslen(literalstring) != wcslen(pattern)) {
            fprintf(stderr,"Length of literal string should be the same length as pattern^\n");
            goto err;
        }
    }

    if (tempfilename != NULL) {
        if ((bytecount > 0) && (strcmp(tempfilename, "START") != 0)) {
            fprintf(stderr,"you must use -o START if you specify a count\n");
            goto err;
        }
    }

    if (endstr != NULL) {
        if (max != wcslen(endstr)) {
            fprintf(stderr,"End string length must equal maximum string size\n");
            goto err;
        }
    }

    if (wk_check_start_end(charset, startblock, endstr) == -1) goto err;

    if (literalstring == NULL) {
        if (wk_default_literalstring(max, &literalstring) == -1) goto err;
    }

    options.low_charset = charset;
    options.upp_charset = upp_charset;
    options.num_charset = num_charset;
    options.sym_charset = sym_charset;
    options.clen = charset ? wcslen(charset) : 0;
    options.ulen = upp_charset ? wcslen(upp_charset) : 0;
    options.nlen = num_charset ? wcslen(num_charset) : 0;
    options.slen = sym_charset ? wcslen(sym_charset) : 0;
    options.pattern = pattern;
    options.plen = pattern ? wcslen(pattern) : 0;
    options.literalstring = literalstring;
    options.endstring = endstr;
    options.max = max;

    if (pattern != NULL && startblock != NULL) {
        if (wk_check_member(startblock, &options) == 0) {
            fprintf(stderr,"startblock is not valid according to the pattern/literalstring\n");
            goto err;
        }
    }

    if (pattern != NULL && endstr != NULL) {
        if (wk_check_member(endstr, &options) == 0) {
            fprintf(stderr,"endstring is not valid according to the pattern/literalstring\n");
            goto err;
        }
    }

    if (endstr && wk_too_many_duplicates(endstr, options)) {
        fprintf(stderr,"Error: End string set by -e will never occur (too many duplicate chars)\n");
        goto err;
    }

    if (is_unicode) {
        char response[8];
        fprintf(stderr,
                "Notice: Detected unicode characters.  If you are piping crunch output\n"\
                "to another program such as john or aircrack please make sure that program\n"\
                "can handle unicode input.\n"\
                "\n");

        fprintf(stderr, "Do you want to continue? [Y/n] ");
        fgets(response, 8, stdin);
        if (toupper(response[0]) == 'N') {
            fprintf(stderr,"Aborted by user.\n");
            goto err;
        }
    }
    /* start processing */
    options.startstring = startblock;
    options.min = min;
    wk_fill_minmax_strings(&options);
    wk_fill_pattern_info(&options);

    if (resume == 1) {
        if (startblock != NULL) {
            fprintf(stderr,"you cannot specify a startblock and resume\n");
            goto err;
        }

        if (flag == 0) {
            startblock = wk_resumesession(fpath, charset);
            if (startblock == NULL) goto err; 
            min = wcslen(startblock);
        }

        if (flag == 1) {
            fprintf(stderr,"permute doesn't support resume\n");
            goto err;
        }
    } else {
        if (fpath != NULL)
            (void)remove(fpath);
    }

    return 0;
err:
    exit(EXIT_FAILURE);
    return -1;
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
    
    int multi = 0;
    size_t i;

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
    if (*calc > 4UL && multi >= 1073741824 && bytecount <= 4294967295ULL) {
        fprintf(stderr,"ERROR: Your system is unable to process numbers greater than 4.294.967.295. Please specify a filesize <= 4GiB.\n");
        return -1;
    }
    return 0;
}

static int wk_parse_number(const char *s, size_t max, size_t *calc) {
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
static int wk_dupskip(const char *s, options_type *op) {
    size_t dupvalue;
    char *endptr; /* temp pointer for duplicates option */

    if (s == NULL) return -1;

    dupvalue = (size_t)strtoul(s, &endptr, 10);
    if (endptr == s) {
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

static wchar_t *wk_endstring(const char *s, int *is_unicode) {
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
    (void)wk_make_wide_string(wstr, s ? s : "", len, is_unicode);
    return wstr;
}

static int wk_file(const char *s, char **fpath, char **tmpf, char **outputf) {
    char *hold;
    size_t tp;

    if (s == NULL) return -1;

    hold = strrchr(s, '/');
    *outputf = (char*)s;
    if (hold == NULL) {
        *fpath = calloc(6, sizeof(char));
        if (*fpath == NULL) {
            fprintf(stderr,"bfc: can't allocate memory for fpath1\n");
            return -1;
        }
        memcpy(*fpath, "START", 5);
        tmpf = outputf;
    } else {
        tp = strlen(s) - strlen(hold) + 1;
        tmpf = &s[tp];
        *fpath = calloc(tp+6, sizeof(char));
        if (*fpath == NULL) {
            fprintf(stderr,"bfc: can't allocate memory for fpath2\n");
            return -1;
        }
        memcpy(*fpath, s, tp);
        strncat(*fpath, "START", 5);
    }
    return 0;
}

static int wcstring_cmp(const void *a, const void *b) {
    const wchar_t **ia = (const wchar_t **)a;
    const wchar_t **ib = (const wchar_t **)b;
    return wcscmp(*ia, *ib);
}

static int wk_wordarray(const char *s, wchar_t ***warray, char **argv, int i, int *is_unicode) {
    wchar_t *tempwcs;

    if (numofelements == 1) {
        tempwcs = wk_alloc_wide_string(s, is_unicode);
        numofelements = wcslen(tempwcs);

        *warray = calloc(numofelements, sizeof(wchar_t*));
        if (*warray == NULL) {
            fprintf(stderr,"can't allocate memory for wordarray3\n");
            return -1;
        }

        for (size_t i = 0; i < numofelements; i++) {
            (*warray)[i] = calloc(2, sizeof(wchar_t));
            if ((*warray)[i] == NULL) {
                fprintf(stderr,"can't allocate memory for wordarray2\n");
                return -1;
            }
            (*warray)[i][0] = tempwcs[i];
            (*warray)[i][1] = '\0';
        }
        free(tempwcs);
    } else {
        *warray = calloc(numofelements, sizeof(wchar_t*));
        if (*warray == NULL) {
            fprintf(stderr,"can't allocate memory for wordarray3\n");
            return -1;
        }
        int n;
        for (n = 0; n < numofelements; n++, i++) {
            (*warray)[n] = wk_alloc_wide_string(argv[i+1], is_unicode);
        }
        /* sort wordarray so the results are sorted */
        qsort(*warray, n, sizeof(char *), wcstring_cmp);
    }
    return 0;
}

static wchar_t **wk_readpermute(const char *filename, int *is_unicode) {
    FILE *fp;
    wchar_t **warray = NULL;
    char buf[512];
    size_t i = 0;

    errno = 0;
    numofelements = 0;
    memset(buf, 0, sizeof(buf));
    if ((fp = fopen(filename, "r")) == NULL) {
        fprintf(stderr,"readpermute: File %s could not be opened\n", filename);
        fprintf(stderr,"The problem is = %s\n", strerror(errno));
        return NULL;
    } else {
        while (fgets(buf, sizeof(buf), fp) != NULL) {
            if (buf[0] != '\n')
                numofelements++;
        }
        (void)fseek(fp, 0, SEEK_SET);

        warray = calloc(numofelements, sizeof(wchar_t*));
        if (warray == NULL) {
            fprintf(stderr,"readpermute: can't allocate memory for wordarray1\n");
            return NULL;
        }
        while (fgets(buf, (int)sizeof(buf), fp) != NULL) {
            if (buf[0] != '\n' && buf[0] != '\0') {
                buf[strlen(buf)-1] = '\0';
                warray[i++] = wk_alloc_wide_string(buf, is_unicode);
            }
        }

        if (fclose(fp) != 0) {
            fprintf(stderr,"readpermute: fclose returned error number = %d\n", errno);
            fprintf(stderr,"The problem is = %s\n", strerror(errno));
        }
        return warray;
    }
}

static int wk_copy_charset(int argc, char **argv, int *i, wchar_t **c, int *is_unicode) {
    if (argc > *i && *argv[*i] != '-') {
        if (*argv[*i] != '+') {
            free(*c);
            if(wk_copy(*c, argv[*i], is_unicode) == -1)
                return -1;
        }
        (*i)++;
    }
    return 0;
}

/* return 0 if string1 does not comply with options.pattern and options.literalstring */
static int wk_check_member(const wchar_t *string1, const options_type *options) {
    const wchar_t *cset;
    size_t i;

    for (i = 0; i < wcslen(string1); i++) {
        cset = NULL;
        switch (options->pattern[i]) {
        case L'@':
            if (options->literalstring[i] != L'@')
                cset = options->low_charset;
            break;
        case L',':
            if (options->literalstring[i] != L',')
                cset = options->upp_charset;
            break;
        case L'%':
            if (options->literalstring[i] != L'%')
                cset = options->num_charset;
            break;
        case L'^':
            if (options->literalstring[i] != L'^')
                cset = options->sym_charset;
            break;
        default: /* constant part of pattern */
            break;
        }

        if (cset == NULL) {
            if (string1[i] != options->pattern[i])
                return 0;
            continue;
        }

        while (*cset) {
            if (string1[i] == *cset)
                break;
            else
                cset++;
        }
        if (*cset == L'\0')
            return 0;
    }
    return 1;
}

static int wk_check_start_end(wchar_t *cset, wchar_t *start, wchar_t *end) {
    size_t i;

    if (start != NULL && end != NULL) {
        for (i = 0; i < wcslen(start); i++) {
            wchar_t startcharsrch = start[i];
            wchar_t *startpos;
            startpos = wcschr(cset, startcharsrch);
            int startplace = startpos - cset;

            wchar_t endcharsrch = end[i];
            wchar_t *endpos;
            endpos = wcschr(cset, endcharsrch);
            int endplace = endpos - cset;

            if (startplace > endplace) {
                fprintf(stderr,"End string must be greater than start string\n");
                return -1;
            }

            if (startplace < endplace)
                return 0;
        }
    }
    return -1;
}

static int wk_default_literalstring(size_t max, wchar_t **wstr) {
    size_t size;

    *wstr = calloc(max+1, sizeof(wchar_t));
    if (*wstr == NULL) {
        fprintf(stderr,"can't allocate memory for literalstring\n");
        return -1;
    }
    
    for (size = 0; size < max; size++)
        (*wstr)[size] = L'-';
    (*wstr)[max] = L'\0';

    return 0;
}

/* NOTE: similar to strpbrk but length limited and only searches for a single char */
static size_t wk_find_index(const wchar_t *cset, size_t clen, wchar_t tofind) {
    size_t i;

    for (i = 0; i < clen; i++) {
        if (cset[i] == tofind)
            return i;
    }
    return NPOS;
}

static int wk_too_many_duplicates(const wchar_t *block, const options_type options) {
    wchar_t cchar = L'\0';
    size_t dupes_seen = 0;

    while (*block != L'\0') {
        if (*block == cchar) {
            /* check for overflow of duplicates */
            dupes_seen += 1;

            if (dupes_seen > options.duplicates[0]) {
                if (wk_find_index(options.low_charset, options.clen, cchar) != NPOS) return 1;
            }
            if (dupes_seen > options.duplicates[1]) {
                if (wk_find_index(options.upp_charset, options.ulen, cchar) != NPOS) return 1;
            }
            if (dupes_seen > options.duplicates[2]) {
                if (wk_find_index(options.num_charset, options.nlen, cchar) != NPOS) return 1;
            }
            if (dupes_seen > options.duplicates[3]) {
                if (wk_find_index(options.sym_charset, options.slen, cchar) != NPOS) return 1;
            }
        } else {
            cchar = *block;
            dupes_seen = 1;
        }
        block++;
    }
    return 0;
}

static inline wchar_t *wk_strcalloc(size_t size) {
    wchar_t *tmp = NULL;

    tmp = calloc(size+1, sizeof(wchar_t));
    if (tmp == NULL) {
        fprintf(stderr,"wk string calloc function can't allocate memory\n");
        return NULL;
    }
    tmp[size-1] = L'\0';

    return tmp;
}

static int wk_fill_minmax_strings(options_type *options) {
    size_t i;
    wchar_t *last_min;                  /* last string of size min */
    wchar_t *first_max;                 /* first string of size max */
    wchar_t *min_string, *max_string;   /* first string of size min, last string of size max */

    if ((last_min = wk_strcalloc(options->min+1)) == NULL) return -1;
    if ((first_max = wk_strcalloc(options->max+1)) == NULL) return -1;
    if ((min_string = wk_strcalloc(options->min+1)) == NULL) return -1;
    if ((max_string = wk_strcalloc(options->max+1)) == NULL) return -1;

    /* fill last_min and first_max */
    for (i = 0; i < options->max; i++) {
        if (options->pattern == NULL) {
            if (i < options->min) {
                last_min[i] = options->low_charset[options->clen-1];
                min_string[i] = options->low_charset[0];
            }
            first_max[i] = options->low_charset[0];
            max_string[i] = options->low_charset[options->clen-1];
        } else { /* min == max */
            min_string[i] = max_string[i] = last_min[i] = first_max[i] = options->pattern[i];
            switch (options->pattern[i]) {
            case L'@':
                if (options->literalstring[i] != L'@') {
                    max_string[i] = last_min[i] = options->low_charset[options->clen-1];
                    min_string[i] = first_max[i] = options->low_charset[0];
                }
                break;
            case L',':
                if (options->literalstring[i] != L',') {
                    max_string[i] = last_min[i] = options->upp_charset[options->clen-1];
                    min_string[i] = first_max[i] = options->upp_charset[0];
                }
                break;
            case L'%':
                if (options->literalstring[i] != L',') {
                    max_string[i] = last_min[i] = options->num_charset[options->clen-1];
                    min_string[i] = first_max[i] = options->num_charset[0];
                }
                break;
            case L'^':
                if (options->literalstring[i] != L',') {
                    max_string[i] = last_min[i] = options->sym_charset[options->clen-1];
                    min_string[i] = first_max[i] = options->sym_charset[0];
                }
                break;
            default:
                break;
            }
        }
    }

    options->last_min = last_min;
    options->first_max = first_max;

    if (options->startstring) {
        for (i = 0; i < options->min; i++)
            min_string[i] = options->startstring[i];
    }

    if (options->endstring) {
        for (i = 0; i < options->max; i++)
            max_string[i] = options->endstring[i];
    }

    options->min_string = min_string;
    options->max_string = max_string;

    return 0;
}

static void wk_fill_pattern_info(options_type *options) {
    struct pinfo *p;
    wchar_t *cset;
    size_t i, clen, index, si, ei;
    int is_fixed;
    size_t dupes;

    options->pattern_info = calloc(options->max, sizeof(struct pinfo));
    if (options->pattern_info == NULL) {
        fprintf(stderr,"fill_pattern_info: can't allocate memory for pattern info\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < options->max; i++) {
        cset = NULL;
        clen = 0;
        index = 0;
        is_fixed = 1;
        dupes = options->duplicates[0];

        if (options->pattern == NULL) {
            cset = options->low_charset;
            clen = options->clen;
            is_fixed = 0;
        } else {
            cset = NULL;
            switch (options->pattern[i]) {
            case L'@':
                if (options->literalstring[i] != L'@') {
                    cset = options->low_charset;
                    clen = options->clen;
                    is_fixed = 0;
                    dupes = options->duplicates[0];
                }
                break;
            case L',':
                if (options->literalstring[i] != L',') {
                    cset = options->upp_charset;
                    clen = options->ulen;
                    is_fixed = 0;
                    dupes = options->duplicates[1];
                }
                break;
            case L'%':
                if (options->literalstring[i] != L'%') {
                    cset = options->num_charset;
                    clen = options->nlen;
                    is_fixed = 0;
                    dupes = options->duplicates[2];
                }
                break;
            case L'^':
                if (options->literalstring[i] != L'^') {
                    cset = options->sym_charset;
                    clen = options->slen;
                    is_fixed = 0;
                    dupes = options->duplicates[3];
                }
                break;
            default: /* constant part of pattern */
                break;
            }
        }

        if (cset == NULL) {
            /* fixed character.  find its charset and index within. */
            cset = options->low_charset;
            clen = options->clen;
            dupes = options->duplicates[0];

            if (options->pattern == NULL) {
                fprintf(stderr,"fill_pattern_info: options->pattern is NULL!\n");
                exit(EXIT_FAILURE);
            }

            if ((index = wk_find_index(cset, clen, options->pattern[i])) == NPOS) {
                cset = options->upp_charset;
                clen = options->ulen;
                dupes = options->duplicates[1];
                if ((index = wk_find_index(cset, clen, options->pattern[i])) == NPOS) {
                    cset = options->num_charset;
                    clen = options->nlen;
                    dupes = options->duplicates[2];
                    if ((index = wk_find_index(cset, clen, options->pattern[i])) == NPOS) {
                        cset = options->num_charset;
                        clen = options->nlen;
                        dupes = options->duplicates[3];
                        if ((index = wk_find_index(cset, clen, options->pattern[i])) == NPOS) {
                            cset = NULL;
                            clen = 0;
                            dupes = (size_t)-1;
                            index = 0;
                        }
                    }
                }
            }

            si = ei = index;
        } else {
            if (i < wcslen(options->min_string))
                si = wk_find_index(cset, clen, options->min_string[i]);
            else
                si = 0;
            
            ei = wk_find_index(cset, clen, options->max_string[i]);

            if (si == NPOS || ei == NPOS) {
                fprintf(stderr,"fill_pattern_info: Internal error: "\
                        "Can't find char at pos #%lu in cset\n",
                        (unsigned long)i+1);
                exit(EXIT_FAILURE);
            }
        }
        
        p = &(options->pattern_info[i]);
        p->cset = cset;
        p->clen = clen;
        p->is_fixed = is_fixed;
        p->start_index = si;
        p->end_index = ei;
        p->duplicates = dupes;
    }
}

static wchar_t *wk_resumesession(const char *fpath, const wchar_t *charset) {
    FILE *fp;               /* ptr to START output file; will be renamed later */
    char buff[512];         /* buffer to hold line from wordlist */
    wchar_t *startblock;
    size_t j, k;

    errno = 0;
    memset(buff, 0, sizeof(buff));

    if ((fp = fopen(fpath, "r")) == NULL) {
        fprintf(stderr,"resume: File START could not be opened\n");
        exit(EXIT_FAILURE);
    } else {
        while (feof(fp) == 0) {
            (void)fgets(buff, (int)sizeof(buff), fp);
            ++my_thread.linecounter;
            my_thread.bytecounter += (unsigned long long)strlen(buff);
        }
        my_thread.linecounter--; /* -1 to get correct num */
        my_thread.bytecounter -= (unsigned long long)strlen(buff);

        if (fclose(fp) != 0) {
            fprintf(stderr,"resume: fclose returned error number = %d\n", errno);
            fprintf(stderr,"The problem is = %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (buff[0]) buff[strlen(buff)-1] = '\0';

        startblock = wk_alloc_wide_string(buff, NULL);
        fprintf(stderr, "Resuming from = %s\n", buff);

        for (j = 0; j < wcslen(startblock); j++) {
            for (k = 0; k < wcslen(charset); k++) {
                if (startblock[j] == charset[k])
                    inc[j] = k;
            }
        }
        return startblock;
    }
    return NULL;
}