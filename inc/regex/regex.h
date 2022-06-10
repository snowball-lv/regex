#pragma once

typedef uint32_t CodePoint;

typedef struct {
    CodePoint cp;    // from
    CodePoint range; // to
} Char;

typedef struct {
    int op;
    Char c;
    int a, b;
} Ins;

typedef struct {
    char *src;
    char *pos;
    CodePoint cur;
    Ins *ins;
    int numins;
    int *clist;
    int clistsz;
    int *nlist;
    int nlistsz;
    char *added;
} RegEx;

typedef struct {
    char *start;
    int len;
} Match;

void regexcompile(RegEx *re, char *src);
int regexmatch(RegEx *re, Match *m, char *str);
void regexdumpdot(RegEx *re, FILE *f);
void regexdumpins(RegEx *re, FILE *f);
void freeregex(RegEx *re);
