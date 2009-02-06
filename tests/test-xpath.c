/*
 * test-xpath.c: check that XPath expressions yield the expected result
 *
 * Copyright (C) 2007 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 *
 * Author: David Lutterkort <dlutter@redhat.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include <augeas.h>
#include <internal.h>
#include <memory.h>

static const char *abs_top_srcdir;
static char *root;

#define KW_TEST "test"

struct entry {
    struct entry *next;
    char *path;
    char *value;
};

struct test {
    struct test *next;
    char *name;
    char *match;
    struct entry *entries;
};

#define die(msg)                                                    \
    do {                                                            \
        fprintf(stderr, "%d: Fatal error: %s\n", __LINE__, msg);    \
        exit(EXIT_FAILURE);                                         \
    } while(0)

static char *skipws(char *s) {
    while (isspace(*s)) s++;
    return s;
}

static char *findws(char *s) {
    while (*s && ! isspace(*s)) s++;
    return s;
}

static char *token(char *s, char **tok) {
    char *t = skipws(s);
    s = findws(t);
    *tok = strndup(t, s - t);
    return s;
}

static char *token_to_eol(char *s, char **tok) {
    char *t = skipws(s);
    while (*s && *s != '\n') s++;
    *tok = strndup(t, s - t);
    return s;
}

static struct test *read_tests(void) {
    char *fname;
    FILE *fp;
    char line[BUFSIZ];
    struct test *result = NULL, *t = NULL;
    int lc = 0;

    if (asprintf(&fname, "%s/tests/xpath.tests", abs_top_srcdir) < 0)
        die("asprintf fname");

    if ((fp = fopen(fname, "r")) == NULL)
        die("fopen xpath.tests");

    while (fgets(line, BUFSIZ, fp) != NULL) {
        lc += 1;
        char *s = skipws(line);
        if (*s == '#' || *s == '\0')
            continue;
        if (STREQLEN(s, KW_TEST, strlen(KW_TEST))) {
            if (ALLOC(t) < 0)
                die("out of memory");
            list_append(result, t);
            s = token(s + strlen(KW_TEST), &(t->name));
            s = token_to_eol(s, &(t->match));
        } else {
            struct entry *e = NULL;
            if (ALLOC(e) < 0)
                die("out of memory");
            list_append(t->entries, e);
            s = token(s, &(e->path));
            s = skipws(s);
            if (*s) {
                if (*s != '=') {
                    fprintf(stderr,
                     "line %d: either list only a path or path = value\n", lc);
                    die("xpath.tests has incorrect format");
                }
                s = token_to_eol(s + 1, &(e->value));
            }
        }
        s = skipws(s);
        if (*s != '\0') {
            fprintf(stderr, "line %d: junk at end of line\n", lc);
            die("xpath.tests has incorrect format");
        }
    }
    return result;
}

static void print_pv(const char *path, const char *value) {
    if (value)
        printf("    %s = %s\n", path, value);
    else
        printf("    %s\n", path);
}

static int run_one_test(struct augeas *aug, struct test *t) {
    int nexp = 0, nact;
    char **matches;
    int result = 0;

    printf("%-30s ... ", t->name);
    list_for_each(e, t->entries)
        nexp++;
    nact = aug_match(aug, t->match, &matches);
    if (nact != nexp) {
        result = -1;
    } else {
        struct entry *e;
        int i;
        const char *val;

        for (e = t->entries, i = 0; e != NULL; e = e->next, i++) {
            if (!STREQ(e->path, matches[i]))
                result = -1;
            if (! streqv(e->value, "...")) {
                aug_get(aug, e->path, &val);
                if (!streqv(e->value, val))
                    result = -1;
            }
        }
    }
    if (result == 0) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");

        printf("  Match: %s\n", t->match);
        printf("  Expected: %d entries\n", nexp);
        list_for_each(e, t->entries) {
            print_pv(e->path, e->value);
        }
        if (nact < 0) {
            printf("  Actual: aug_match failed\n");
            } else {
            printf("  Actual: %d entries\n", nact);
        }
        for (int i=0; i < nact; i++) {
            const char *val;
            aug_get(aug, matches[i], &val);
            print_pv(matches[i], val);
        }
    }
    return result;
}

static int run_tests(struct test *tests) {
    char *lensdir;
    struct augeas *aug = NULL;
    int result = EXIT_SUCCESS;

    if (asprintf(&lensdir, "%s/lenses", abs_top_srcdir) < 0)
        die("asprintf lensdir failed");

    aug = aug_init(root, lensdir, AUG_NO_STDINC|AUG_SAVE_NEWFILE);
    if (aug == NULL)
        die("aug_init");
    list_for_each(t, tests) {
        if (run_one_test(aug, t) < 0)
            result = EXIT_FAILURE;
    }
    aug_close(aug);

    return result;
}

int main(void) {
    struct test *tests;

    abs_top_srcdir = getenv("abs_top_srcdir");
    if (abs_top_srcdir == NULL)
        die("env var abs_top_srcdir must be set");

    if (asprintf(&root, "%s/tests/root", abs_top_srcdir) < 0) {
        die("failed to set root");
    }

    tests = read_tests();
    return run_tests(tests);
    /*
    list_for_each(t, tests) {
        printf("Test %s\n", t->name);
        printf("match %s\n", t->match);
        list_for_each(e, t->entries) {
            if (e->value)
                printf("    %s = %s\n", e->path, e->value);
            else
                printf("    %s\n", e->path);
        }
    }
    */
}

/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */