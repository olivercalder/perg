# Perg

Grep Regular Expressions: Perg

## About Perg

Perg is a regular expression matching tool built from scratch in C, leveraging nondeterministic finite automata and multithreading.

## Expression Syntax

The syntax recognized by perg is inspired by that which is common in computability theory (*not* the POSIX standard).
In particular, the following are metacharacters:

| Metachar | Description                                                                                                  |
| -------- | ------------                                                                                                 |
| `( )`    | Denotes a contained subexpression to which other metacharacters may be applied.                              |
| `\|`     | The union operator matches either the previous or next expression: i.e. `abc|123` matches `abc` or `123`.    |
| `.`      | Matches any single symbol.                                                                                   |
| `*`      | Matches the previous symbol or subexpression zero or more times: i.e. `ab*c` matches both `ac` and `abbbbc`. |
| `!`      | Matches any symbol *except* the next symbol ~~or subexpression~~: i.e. `a!bc` matches `atc` but not `abc`.   |

Special characters which are not metacharacters, including `\t` (tab) and `\\` (backslash), standard C char symbols are used.
To include any metacharacter as its literal symbol, precede it with a backslash, as in `'hello \(greeting\)'`.

In the future, additional metacharacters may be supported, including:

| Metachar | Description                                                                                    |
| -------- | ------------                                                                                   |
| `+`      | Matches the previous symbol or subexpression one or more times ($s$+ is equivalent to $ss*$).  |
| `**n`    | Matches exactly $n$ occurrences of the previous symbol or subexpression, $n \in \mathbb{Z}^+$. |

Additionally, applying `!` to subexpressions may be supported once such matching behavior has been defined.

## Usage

```sh
perg [OPTION...] EXPRESSION [FILE...]
```

If no file is specified and `-r` flag is not present, matches expression against stdin.

Options are heavily (and selectively) inspired by those of `grep` for the benefit of muscle memory.
Most are not yet implemented, but they will include:

| Option | Description                                                                                                                        |
| ------ | -----------                                                                                                                        |
| `-i`   | Ignore case distinctions.                                                                                                          |
| `-v`   | Invert matches to select non-matching lines.                                                                                       |
| `-w`   | Match whole words, where matching strings must be surrounded by whitespace.                                                        |
| `-x`   | Match whole lines, where the matching string must be the complete line.                                                            |
| `-o`   | Only print the matching parts of a matching line.                                                                                  |
| `-H`   | Print the filename before each match -- this is the default when multiple files or `-r` is given.                                  |
| `-h`   | Suppress printing the filename before each match -- this is the default when one or zero files are given, and `-r` is not present. |
| `-n`   | Prefix each matching line with the line number within the input file, after the filename if applicable.                            |
| `-a`   | Treat binary files as if they were text files.                                                                                     |
| `-r`   | Recursively read all files in each given directory and subdirectories -- if no files given, searches the working directory.        |

Other lower-priority options to be (possibly) implemented later: `-c`, `-L`, `-l`, `-q`, `-A`, `-B`, `-C`
