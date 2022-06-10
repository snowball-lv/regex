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

## Usage

### Library

    RegEx re;
    regexcompile(&re, "[a-zA-Z0-9_]\w*");
    Match m;
    if (regexmatch(&re, &m, "Hello, World!")
        printf("%.*s\n", m.len, m.start);
    freeregex(&re);

### Command line
See `help` for supported flags.

    ./bin/regex [flags] pattern input_string

## Build

    make

## Debug

Both the library and command line support printing the pattern's bytecode and a graph representation in [DOT](https://en.wikipedia.org/wiki/DOT_(graph_description_language)).

    ./bin/regex -gregex.dot "[^👀]🍆+|😤" ""
    dot regex.dot -T jpg -o regex.jpg

![](https://i.imgur.com/J2eTgV1.jpeg)
