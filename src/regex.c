#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <regex/regex.h>

#define U8BUFSZ 5

// special characters
// use free codepoints
enum {
    LAST_VALID_CP = 0x10FFFF,
    SP_CP_ANY,   // .
    SP_CP_START, // ^
    SP_CP_END,   // $
};

enum {
    OP_NONE,
    OP_NOP,
    OP_CHAR,
    OP_CON,
    OP_ALT,
    OP_KLEENE,
    OP_QUESTION,
    OP_PLUS,
    OP_BRACKET,
    OP_SPLIT,
    OP_JMP,
    OP_MATCH,
    OP_MATCH_TOKEN,
};

typedef struct Node Node;
struct Node {
    int type;
    Char c;
    Node *l, *r;
    int neg;
};

#define BIT(n) (1 << (n))
#define LSBS(n) (BIT(n) - 1)
#define MSBS(n) (LSBS(n) << (8 - (n)))

static void u8enc(char *dst, CodePoint cp) {
    if (cp <= 0x7f) {
        dst[0] = cp;
        dst[1] = 0;
    }
    else if (cp <= 0x7ff) {
        dst[0] = MSBS(2) | ((cp >> 6) & LSBS(5));
        dst[1] = MSBS(1) | ((cp >> 0) & LSBS(6));
        dst[2] = 0;
    }
    else if (cp <= 0xffff) {
        dst[0] = MSBS(3) | ((cp >> 12) & LSBS(4));
        dst[1] = MSBS(1) | ((cp >> 6) & LSBS(6));
        dst[2] = MSBS(1) | ((cp >> 0) & LSBS(6));
        dst[3] = 0;
    }
    else { // cp <= 0x10ffff
        dst[0] = MSBS(4) | ((cp >> 18) & LSBS(3));
        dst[1] = MSBS(1) | ((cp >> 12) & LSBS(6));
        dst[2] = MSBS(1) | ((cp >> 6) & LSBS(6));
        dst[3] = MSBS(1) | ((cp >> 0) & LSBS(6));
        dst[4] = 0;
    }
}

static int u8dec(CodePoint *dst, char **src) {
    char first = **src;
    if ((first & MSBS(1)) == 0) {
        *dst = first;
        *src = *src + 1;
    }
    else if ((first & MSBS(3)) == MSBS(2)) {
        *dst = (((*src)[0] & LSBS(5)) << 6)
                | ((*src)[1] & LSBS(6));
        *src = *src + 2;
    }
    else if ((first & MSBS(4)) == MSBS(3)) {
        *dst = (((*src)[0] & LSBS(4)) << 12)
                | (((*src)[1] & LSBS(6)) << 6)
                | ((*src)[2] & LSBS(6));
        *src = *src + 3;
    }
    else if ((first & MSBS(5)) == MSBS(4)) {
        *dst = (((*src)[0] & LSBS(3)) << 18)
                | (((*src)[1] & LSBS(6)) << 12)
                | (((*src)[2] & LSBS(6)) << 6)
                | ((*src)[3] & LSBS(6));
        *src = *src + 4;
    }
    else {
        return 0;
    }
    return 1;
}

static void initregex(RegEx *re) {
    memset(re, 0, sizeof(RegEx));
}

static void fprintcp(FILE *f, CodePoint cp) {
    switch (cp) {
    case SP_CP_ANY: fprintf(f, "/any/"); return;
    case SP_CP_START: fprintf(f, "/^/"); return;
    case SP_CP_END: fprintf(f, "/$/"); return;
    }
    if (cp <= CHAR_MAX && !isgraph(cp)) {
        fprintf(f, "%#x", cp);
        return;
    }
    char u8buf[U8BUFSZ];
    u8enc(u8buf, cp);
    fprintf(f, "%s", u8buf);
}

static void fprintc(FILE *f, Char c) {
    fprintcp(f, c.cp);
    if (c.range) {
        fprintf(f, "-");
        fprintcp(f, c.range);
    }
}

static void _dumptree(Node *tree) {
    switch (tree->type) {
    case OP_ALT:
        printf("(");
        _dumptree(tree->l);
        printf("|");
        _dumptree(tree->r);
        printf(")");
        break;
    case OP_CON:
        _dumptree(tree->l);
        // printf("+");
        _dumptree(tree->r);
        break;
    case OP_KLEENE:
        printf("(");
        _dumptree(tree->l);
        printf(")*");
        break;
    case OP_QUESTION:
        printf("(");
        _dumptree(tree->l);
        printf(")?");
        break;
    case OP_PLUS:
        printf("(");
        _dumptree(tree->l);
        printf(")+");
        break;
    case OP_BRACKET:
        printf("[");
        _dumptree(tree->l);
        printf("]");
        break;
    case OP_CHAR:
        fprintc(stdout, tree->c);
        break;
    case OP_NOP: break;
    default:
        printf("*** can't print node [%i]\n", tree->type);
        break;
    }
}

static void dumptree(Node *tree) {
    printf("regex: [");
    _dumptree(tree);
    printf("]\n");
}

static CodePoint peekc(RegEx *re) {
    return re->cur;
}

static void advance(RegEx *re) {
    if (!u8dec(&re->cur, &re->pos)) {
        printf("*** couldn't decode [%#x]\n", *re->pos);
        re->cur = *re->pos++;
    }
}

static CodePoint isspec(CodePoint cp) {
    switch (cp) {
    case '.':
    case '[': case ']':
    case '\\':
    case '(': case ')':
    case '*': case '+': case '?':
    case '{': case '}':
    case '|':
    case '^': case '$':
        return 1;
    }
    return 0;
}

static Node *newnode(int type) {
    Node *n = malloc(sizeof(Node));
    memset(n, 0, sizeof(Node));
    n->type = type;
    return n;
}

static int isdupl(CodePoint c) {
    return c == '*' || c == '?' || c == '+';
}

static Node *dupl(CodePoint c, Node *child) {
    int op = c == '*' ? OP_KLEENE : (c == '+' ? OP_PLUS : OP_QUESTION);
    Node *n = newnode(op);
    n->l = child;
    return n;
}

static Node *alt(RegEx *re);

static Node *bracket(RegEx *re) {
    int neg = peekc(re) == '^';
    if (neg) advance(re);
    Node *n = newnode(OP_CHAR);
    n->c = (Char){peekc(re)};
    advance(re);
    while (peekc(re) && peekc(re) != ']') {
        CodePoint cp = peekc(re);
        advance(re);
        Node *lastc = n->type == OP_CHAR ? n : n->r;
        if (cp == '-' && peekc(re) && peekc(re) != ']' && !lastc->c.range) {
            lastc->c.range = peekc(re);
            advance(re);
        }
        else {
            Node *n2 = newnode(OP_CHAR);
            n2->c = (Char){cp};
            Node *alt = newnode(OP_CON);
            alt->l = n;
            alt->r = n2;
            n = alt;
        }
    }
    Node *b = newnode(OP_BRACKET);
    b->neg = neg;
    b->l = n;
    return b;
}

static Node *conchars(Char *cs, int num) {
    Node *n = newnode(OP_CHAR);
    n->c = cs[0];
    for (int i = 1; i < num; i++) {
        Node *tmp = newnode(OP_CON);
        tmp->l = n;
        tmp->r = newnode(OP_CHAR);
        tmp->r->c = cs[i];
        n = tmp;
    }
    return n;
}

static Node *escape(RegEx *re) {
    char buf[16];
    Node *n = newnode(OP_CHAR);
    CodePoint cp = peekc(re);
    advance(re);
    switch (cp) {
    case 0: 
        printf("*** trailing backslash\n");
        n->type = OP_NOP;
        break;
    case 'a': n->c = (Char){'\a'}; break;
    case 'b': n->c = (Char){'\b'}; break;
    case 'e': n->c = (Char){'\e'}; break;
    case 'f': n->c = (Char){'\f'}; break;
    case 'n': n->c = (Char){'\n'}; break;
    case 'r': n->c = (Char){'\r'}; break;
    case 't': n->c = (Char){'\t'}; break;
    case 'v': n->c = (Char){'\v'}; break;
    case 'w': // [A-Za-z0-9_]
    case 'W': // [^A-Za-z0-9_]
        n = newnode(OP_BRACKET);
        n->neg = (cp == 'W');
        n->l = conchars((Char[]){
                {'A', 'Z'},
                {'a', 'z'},
                {'0', '9'},
                {'_'}},
                4);
        break;
    case 'd': // [0-9]
    case 'D': // [^0-9]
        n = newnode(OP_BRACKET);
        n->neg = (cp == 'D');
        n->l = conchars((Char[]){{'0', '9'}}, 1);
        break;
    case 's': // [ \t\r\n\v\f]
    case 'S': // [^ \t\r\n\v\f]
        n = newnode(OP_BRACKET);
        n->neg = (cp == 'S');
        n->l = conchars((Char[]){
            {' '}, {'\t'}, {'\r'}, {'\n'}, {'\v'}, {'\f'}},
            6);
        break;
    case 'm':
        n = newnode(OP_MATCH_TOKEN);
        n->c.cp = 0; // store the token in Char codepoint
        char *ptr = buf;
        *ptr = 0;
        while (isdigit(peekc(re))) {
            *ptr++ = peekc(re);
            advance(re);
        }
        *ptr = 0;
        n->c.cp = atoi(buf);
        break;
    default:
        if (isspec(cp)) {
            n->c = (Char){cp};
            break;
        }
        printf("*** unrecognized escape sequence [");
        fprintcp(stdout, cp);
        printf("]\n");
        n->type = OP_NOP;
        break;
    }
    return n;
}

static Node *atom(RegEx *re) {
    Node *n;
    switch (peekc(re)) {
    case '(':
        advance(re);
        n = alt(re);
        if (peekc(re) == ')') advance(re);
        else printf("*** unterminated group\n");
        break;
    case '[':
        advance(re);
        n = bracket(re);
        if (peekc(re) == ']') advance(re);
        else printf("*** unterminated brackets\n");
        break;
    case 0:
    case '|':
    case ')':
        n = newnode(OP_NOP);
        break;
    case '\\':
        advance(re);
        n = escape(re);
        break;
    case '.':
        n = newnode(OP_CHAR);
        n->c = (Char){SP_CP_ANY};
        advance(re);
        break;
    case '^':
        n = newnode(OP_CHAR);
        n->c = (Char){SP_CP_START};
        advance(re);
        break;
    case '$':
        n = newnode(OP_CHAR);
        n->c = (Char){SP_CP_END};
        advance(re);
        break;
    default:
        n = newnode(OP_CHAR);
        n->c = (Char){peekc(re)};
        advance(re);
        break;
    }
    while (isdupl(peekc(re))) {
        n = dupl(peekc(re), n);
        advance(re);
    }
    return n;
}

static Node *con(RegEx *re) {
    Node *n = atom(re);
    while (peekc(re) && peekc(re) != '|' && peekc(re) != ')') {
        Node *tmp = newnode(OP_CON);
        tmp->l = n;
        tmp->r = atom(re);
        n = tmp;
    }
    return n;
}

static Node *alt(RegEx *re) {
    Node *n = con(re);
    while (peekc(re) == '|') {
        advance(re);
        Node *tmp = newnode(OP_ALT);
        tmp->l = n;
        tmp->r = con(re);
        n = tmp;
    }
    return n;
}

static void gen(RegEx *re, Node *n) {
    switch (n->type) {
    case OP_ALT: {
        Ins *split = &re->ins[re->numins++];
        int a = re->numins;
        gen(re, n->l);
        Ins *jmp = &re->ins[re->numins++];
        int b = re->numins;
        gen(re, n->r);
        *jmp = (Ins){OP_JMP, .a = re->numins};
        *split = (Ins){OP_SPLIT, .a = a, .b = b};
        break;
    }
    case OP_KLEENE: {
        int splitpos = re->numins;
        Ins *split = &re->ins[re->numins++];
        *split = (Ins){OP_SPLIT, .a = re->numins};
        gen(re, n->l);
        re->ins[re->numins++] = (Ins){OP_JMP, .a = splitpos};
        split->b = re->numins;
        break;
    }
    case OP_QUESTION: {
        Ins *split = &re->ins[re->numins++];
        *split = (Ins){OP_SPLIT, .a = re->numins};
        gen(re, n->l);
        split->b = re->numins;
        break;
    }
    case OP_PLUS: {
        int start = re->numins;
        gen(re, n->l);
        Ins *split = &re->ins[re->numins++];
        *split = (Ins){OP_SPLIT, .a = start, .b = re->numins};
        break;
    }
    case OP_CON:
        gen(re, n->l);
        gen(re, n->r);
        break;
    case OP_BRACKET:
        // .a = number of "char" commands to follow
        // .b = negation
        Ins *bracket = &re->ins[re->numins++];
        *bracket = (Ins){OP_BRACKET, .b = n->neg};
        int start = re->numins;
        gen(re, n->l);
        bracket->a = re->numins - start;
        break;
    case OP_CHAR:
        re->ins[re->numins++] = (Ins){OP_CHAR, n->c};
        break;
    case OP_MATCH_TOKEN:
        re->ins[re->numins++] = (Ins){OP_MATCH_TOKEN, n->c};
        break;
    case OP_NOP: break;
    default:
        printf("*** can't generate node [%i]\n", n->type);
        break;
    }
}

static void deltree(Node *tree) {
    switch (tree->type) {
    case OP_CHAR:
    case OP_NOP:
    case OP_MATCH_TOKEN:
        break;
    case OP_KLEENE:
    case OP_QUESTION:
    case OP_PLUS:
    case OP_BRACKET:
        deltree(tree->l);
        break;
    case OP_CON:
    case OP_ALT:
        deltree(tree->l);
        deltree(tree->r);
        break;
    default:
        printf("*** can't delete node [%i]\n", tree->type);
        break;
    }
    free(tree);
}

static void resetmatcher(RegEx *re) {
    re->clistsz = 0;
    re->nlistsz = 0;
    memset(re->added, 0, re->numins);
}

// returns true if OP_MATCH state was added
static int addstate(RegEx *re, int state, int atstart, int atend) {
    if (re->added[state]) return 0;
    re->added[state] = 1;
    Ins *i = &re->ins[state];
    if (i->op == OP_SPLIT) {
        int ma = addstate(re, i->a, atstart, atend);
        int mb = addstate(re, i->b, atstart, atend);
        return ma || mb;
    }
    else if (i->op == OP_JMP) {
        return addstate(re, i->a, atstart, atend);
    }
    else if (i->op == OP_CHAR && i->c.cp == SP_CP_START) {
        return atstart ? addstate(re, state + 1, atstart, atend) : 0;
    }
    else if (i->op == OP_CHAR && i->c.cp == SP_CP_END) {
        return atend ? addstate(re, state + 1, atstart, atend) : 0;
    }
    else if (i->op == OP_MATCH) {
        return 1;
    }
    else if (i->op == OP_MATCH_TOKEN) {
        re->lasttok = i->c.cp;
        return addstate(re, state + 1, atstart, atend);
    }
    re->nlist[re->nlistsz++] = state;
    return 0;
}

static void swap(RegEx *re) {
    int *tmp = re->clist;
    re->clist = re->nlist;
    re->nlist = tmp;
    re->clistsz = re->nlistsz;
    re->nlistsz = 0;
    memset(re->added, 0, re->numins);
}

void regexcompile(RegEx *re, char *src) {
    initregex(re);
    re->src = malloc(strlen(src) + 1);
    strcpy(re->src, src);
    re->pos = re->src;
    advance(re);
    Node *tree = alt(re);
    // dumptree(tree);
    re->ins = malloc(strlen(src) * 3 * sizeof(Ins));
    re->numins = 0;
    gen(re, tree);
    deltree(tree);
    re->ins[re->numins++] = (Ins){OP_MATCH};
    re->clist = malloc(re->numins * sizeof(int));
    re->nlist = malloc(re->numins * sizeof(int));
    re->added = malloc(re->numins);
}

void regexcompile2(RegEx *re, TokDef *defs) {
    int srclen = 0;
    int numtoks = 0;
    for (TokDef *td = defs; td->pattern; td++) {
        srclen += strlen(td->pattern);
        numtoks++;
    }
    char *src = malloc(srclen + numtoks * 16); // some very generous amount
    char *ptr = src;
    for (int i = 0; i < numtoks; i++) {
        if (i > 0) ptr += sprintf(ptr, "|");
        ptr += sprintf(ptr, "(%s)\\m%i", defs[i].pattern, defs[i].token);
    }
    regexcompile(re, src);
    free(src);
}

static int cmatch(Char c, CodePoint cp) {
    if (c.cp == SP_CP_ANY) return 1;
    if (c.range) return c.cp <= cp && cp <= c.range;
    return c.cp == cp;
}

int regexmatch(RegEx *re, Match *m, char *str) {
    re->lasttok = 0;
    char *start = str;
    int atstart = 1;
    int matched = 0;
    resetmatcher(re);
    if (addstate(re, 0, atstart, !*str)) {
        matched = 1;
        *m = (Match){str, 0, re->lasttok};
    }
    swap(re);
    CodePoint cp = 0;
    while (u8dec(&cp, &str) && cp) {
        if (!re->clistsz) break;
        for (int k = 0; k < re->clistsz; k++) {
            int state = re->clist[k];
            Ins *i = &re->ins[state];
            switch (i->op) {
            case OP_CHAR:
                if (!cmatch(i->c, cp)) break;
                if (addstate(re, state + 1, atstart, !*str)) {
                    int len = str - start;
                    if (!matched || len > m->len) {
                        matched = 1;
                        *m = (Match){start, str - start, re->lasttok};
                    }
                }
                break;
            case OP_BRACKET:
                // .a = number of "char" commands to follow
                // .b = negation
                int cm = 0;
                for (int n = 0; n < i->a; n++) {
                    Ins *c = &re->ins[state + 1 + n];
                    if (cm |= cmatch(c->c, cp)) break;
                }
                if ((cm && !i->b) || (!cm && i->b)) {
                    if (addstate(re, state + 1 + i->a, atstart, !*str)) {
                        int len = str - start;
                        if (!matched || len > m->len) {
                            matched = 1;
                            *m = (Match){start, str - start, re->lasttok};
                        }
                    }
                }
                break;
            default:
                printf("*** can't execute instruction [%i]\n", i->op);
                return 0;
            }
        }
        swap(re);
        atstart = 0;
    }
    return matched;
}

void regexdumpdot(RegEx *re, FILE *f) {
    fprintf(f, "digraph mygraph {\n");
    fprintf(f, "label=\"%s\"\n", re->src);
    fprintf(f, "fontcolor=blue\n");
    fprintf(f, "node [shape=circle width=0.25 label=\"\"];\n");
    fprintf(f, "edge [label=\" \"];\n");
    fprintf(f, "0 [label=\"S0\"];\n");
    fprintf(f, "rankdir=LR;\n");
    for (int k = 0; k < re->numins; k++) {
        Ins *i = &re->ins[k];
        switch (i->op) {
        case OP_CHAR:
            fprintf(f, "%i -> %i [label=\"", k, k + 1);
            fprintc(f, i->c);
            fprintf(f, "\"];\n");
            break;
        case OP_MATCH:
            fprintf(f, "%i [label=\"S%i\" shape=doublecircle];\n", k, k);
            break;
        case OP_MATCH_TOKEN:
            fprintf(f, "%i [label=\"Token|%i\" shape=record];\n", k, i->c.cp);
            fprintf(f, "%i -> %i;\n", k, k + 1);
            break;
        case OP_JMP:
            fprintf(f, "%i -> %i;\n", k, i->a);
            break;
        case OP_SPLIT:
            fprintf(f, "%i -> %i;\n", k, i->a);
            fprintf(f, "%i -> %i;\n", k, i->b);
            break;
        case OP_BRACKET:
            fprintf(f, "%i -> %i:0;\n", k, k + 1);
            fprintf(f, "%i:0 -> %i;\n", k + 1, k + 1 + i->a);
            fprintf(f, "%i [shape=record label=\"<0>%s", k + 1,
                    i->b ? "none of" : "one of");
            for (int n = 0; n < i->a; n++) {
                Ins *c = &re->ins[k + 1 + n];
                fprintf(f, "|");
                fprintc(f, c->c);
            }
            fprintf(f, "\"]\n");
            k = k + 1 + i->a - 1; // account for k++
            break;
        default:
            printf("*** can't graphviz instruction [%i]\n", i->op);
            break;
        }
    }
    fprintf(f, "}\n");
}

void regexdumpins(RegEx *re, FILE *f) {
    for (int k = 0; k < re->numins; k++) {
        Ins *i = &re->ins[k];
        fprintf(f, "%2i: ", k);
        switch (i->op) {
        case OP_SPLIT:
            fprintf(f, "split %i, %i\n", i->a, i->b);
            break;
        case OP_JMP:
            fprintf(f, "jmp %i\n", i->a);
            break;
        case OP_CHAR:
            fprintf(f, "char ");
            fprintc(f, i->c);
            fprintf(f, "\n");
            break;
        case OP_MATCH:
            fprintf(f, "match\n");
            break;
        case OP_MATCH_TOKEN:
            fprintf(f, "token %i\n", i->c.cp);
            break;
        case OP_BRACKET:
            fprintf(f, "bracket %s%i\n", i->b ? "!" : "", i->a);
            break;
        default:
            printf("*** can't print instruction [%i]\n", i->op);
            break;
        }
    }
}

void freeregex(RegEx *re) {
    if (re->ins) free(re->ins);
    if (re->clist) free(re->clist);
    if (re->nlist) free(re->nlist);
    if (re->added) free(re->added);
    if (re->src) free(re->src);
    initregex(re);
}
