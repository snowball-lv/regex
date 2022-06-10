#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <regex/regex.h>

static void help() {
    printf("Usage:\n%4sregex [flags] pattern string\n", "");
    printf("%4s%-12sdon't print matching string\n",
            "", "-s");
    printf("%4s%-12soutput .dot to file or stdout\n",
            "", "-g[file]");
    printf("%4s%-12soutput vm instructions to file or stdout\n",
            "", "-i[file]");
}

int main(int argc, char **argv) {
    int silent = 0;
    int printdot = 0;
    FILE *fdot = 0;
    int printins = 0;
    FILE *fins = 0;
    int i = 1;
    for (; i < argc; i++) {
        if (strchr(argv[i], '-') != argv[i]) break;
        if (strcmp(argv[i], "-s") == 0) {
            silent = 1;
        } 
        else if (strncmp(argv[i], "-g", 2) == 0) {
            printdot = 1;
            if (strlen(argv[i]) > 2)
                fdot = fopen(argv[i] + 2, "w");
        }
        else if (strncmp(argv[i], "-i", 2) == 0) {
            printins = 1;
            if (strlen(argv[i]) > 2)
                fins = fopen(argv[i] + 2, "w");
        }
        else {
            printf("unknown flag [%s]\n", argv[i]);
            help();
            exit(1);
        }
    }
    if (argc - i < 2) {
        help();
        exit(1);
    }
    RegEx re;
    regexcompile(&re, argv[i + 0]);
    Match m;
    int r = regexmatch(&re, &m, argv[i + 1]);
    if (r && !silent)
        printf("%.*s\n", m.len, m.start);
    if (printdot) regexdumpdot(&re, fdot ? fdot : stdout);
    if (printins) regexdumpins(&re, fins ? fins : stdout);
    if (fdot) fclose(fdot);
    if (fins) fclose(fins);
    freeregex(&re);
    return !r;
}
