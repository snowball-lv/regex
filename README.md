A UTF-8 aware regular expression implementation using VM approach.  
For details on the approach see [articles by Russ Cox](https://swtch.com/~rsc/regexp).

## Implementation

Compiling a pattern transforms it into an AST which in turn is used to generate bytecode. The matcher effectively does powerset construction at runtime. It does a single pass over the input string and treats the bytecode as an NFA, tracking every reachable state at once, where the states of the automaton are the indices of the instructions.

## What's missing

- `{}` operator
- All matches are anchored at the first input character
- Escape sequence for specifying code points
- Tests!

## Supported features

- **Operators:** `| () [] [^] - ? * + ^ $`
- **Character classes:** `\w \W \d \D \s \S`
- Custom escape sequence `\m#` which tags a match with a non-negative integer ID
- Matching multiple patterns and returning ID of matched one

## Usage

### Library

#### Matching a single pattern:

```c
RegEx re;
Match m;
regexcompile(&re, "[a-zA-Z0-9_]\w*");
if (regexmatch(&re, &m, "Hello, World!")
    printf("%.*s\n", m.len, m.start);
freeregex(&re);
```

#### Matching multiple patterns:

```c
enum { T_NONE, T_IF, T_WHILE, T_INT, T_ID, T_SPACE };
TokDef tokdefs[] = {
    {"^if", T_IF},
    {"^while", T_WHILE},
    {"^[0-9]+", T_INT},
    {"^[a-zA-Z_]\\w*", T_ID},
    {"^\\s+", T_SPACE},
    {0},
};
RegEx re;
Match m;
regexcompile2(&re, tokdefs);
while (regexmatch(&re, &m, input_string)) {
    input_string += m.len;
    if (m.token != T_SPACE)
        printf("[%i] [%.*s]\n", m.token, m.len, m.start);
}
freeregex(&re);
```

Avoid using `0` as an ID as it's the default value. Under the hood it just appends `\m#` to each pattern and concatenates them. In case of multiple matching tokens the one appearing first in the list will be returned.

### Command line

See `help` for supported flags.

```bash
./bin/regex [flags] pattern input_string
```

## Build

```bash
make
```

## Debug

Both the library and command line support printing the pattern's bytecode and a graph representation in [DOT](https://en.wikipedia.org/wiki/DOT_(graph_description_language)).

    ./bin/regex -gregex.dot "[^👀]🍆+|😤" ""
    dot regex.dot -T png -o regex.png

![](https://i.imgur.com/xiiW0Sa.png)
