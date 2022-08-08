#ifndef NFA_H
#define NFA_H   1


typedef enum {
    FLAG_NONE       = 0x0,  /* nothing special */
    FLAG_EPSILON    = 0x1,  /* do not advance read head */
    FLAG_WILDCARD   = 0x2,  /* the '.' symbol, match anything but whitespace */
    FLAG_INVERT     = 0x4,  /* succeeded by '!' symbol */
} t_flag_t;


typedef struct state state_t;

typedef struct transition_list {
    state_t *next_state;
    struct transition_list *next;
    struct transition_list *prev;
    char symbol;
    t_flag_t flags;
} transition_t;

struct state {
    transition_t *transitions;
    struct state *next; /* for internal list of states */
    struct state *prev; /* for internal list of states */
};

typedef struct nfa {
    state_t *q0;
    state_t *qaccept;
    size_t expr_len;
} nfa_t;

nfa_t *build_nfa(char *expression);

void cleanup_states();


#endif  /* #ifndef NFA_H */
