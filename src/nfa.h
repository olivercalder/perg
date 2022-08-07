#ifndef NFA_H
#define NFA_H  1


#define EPSILON (~0)


typedef struct state state_t;

typedef struct transition_list {
    state_t *next_state;
    struct transition_list *next;
    char symbol;
} transition_list_ele_t;

typedef struct state {
    transition_list_ele_t *transitions;
    struct state *next; /* for linked list of states */
    size_t position;    /* possibly unnecessary */
} state_t;

typedef struct nfa {
    state_t *q0;
    size_t state_count;
} nfa_t;


nfa_t *build_nfa(char *expression);


#endif  /* #ifndef NFA_H */
