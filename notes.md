# Notes on design and thought process

When creating "transition table", keep  track of how many available
transitions exist for each given symbol (or epsilon).  Thus, if there
is only one valid transition, thread can decide not to fork a child, and
instead move itself to that next state.

When looking for available transitions given a symbol, look through all,
and only create and start the thread once the next valid one is found, so
the parent thread can choose not to create a new thread for the current
transition it is keeping track of, and instead execute it itself, if the
file is text. If binary, want the ability to kill all child threads if a
match is found by any thread, as grep does. Regardless, probably want to
fork a child for all available transitions, so parent can join each one and
free the memory allocated to them. Alternatively, simply pass a mutex and
a `(size_t *)` so that each thread can update the end index if it is greater
than the current greatest, and detach each thread as it is spawned.


Keep track of start and end vertices of each match, so that when multiple
matches are found within a line, they can be sorted by end position (greatest
to least), and then by start position (least to greatest), so that, assuming
initial position is preserved for tie breaks, the first match for any given
start position will be the longest, and all others can be ignored until the
next position.

It should be impossible to have overlaps where the second matching string in
the overlap is longer than the first, since there is only one match pattern.

FALSE (counterexample below):

```
Line:        "aaabbbb"
Expression:  "aab*"
Best (longest) match should be "aabbbb"
HOWEVER, grep finds "aa", unclear why...
```

Probably: highlight all (greedy) longest non-overlapping matches. So, if
match starts on index i and ends on index k, but a longer match begins on
index $j$ with $i < j < k$, then highlight $i$ through $k$, then start
highlighting again at the next match start index after (or including) $k$.


Handling main thread and child threads:
For each (used) index of buffer, if there is a transition from q0 for the
symbol at that index, then spawn a thread at q0 with current index to be
read next. Thus, direct child thread will spawn children for all valid
transitions from q0 to another state, and all matches starting from a given
index will fall under that single child thread of the main thread. Then,
child thread and all descendants of it need only keep track of the end index
of the match, if it exists. Let the end index be the index of the symbol
following the final symbol in the match. Abstractly: start <= match < end

# TODO:

- Set upper limit on buffer size, even for text files
- Figure out how to eliminate epsilon transitions
  - Ideally, never return from `build_nfa` with epsilon transitions still in place
- Figure out how to handle `!( ... )`
  - For now, don't allow expressions containing that form

