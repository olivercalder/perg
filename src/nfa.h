#ifndef NFA_H
#define NFA_H   1


typedef enum {
    MATCH_NONE,
    MATCH_PROGRESS,
    MATCH_FOUND,
} match_status_t;

typedef enum {
    FLAG_NONE,      /* nothing special */
    FLAG_EPSILON,   /* do not advance read head */
    FLAG_WILDCARD,  /* the '.' symbol, match any single symbol */
    FLAG_INVERT,    /* succeeded by '!' symbol */
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
    int id;
};

typedef struct nfa {
    state_t *q0;
    state_t *qaccept;
    size_t expr_len;
} nfa_t;

typedef struct nfa_arg {
    char *buf;
    size_t bufsize;
    state_t *state;
    state_t *qaccept;
    size_t pos;
    size_t end;
    int case_insensitive;
} nfa_arg_t;

typedef struct plet {
    pthread_t thread;
    struct plet *next;
    nfa_arg_t arg;
} pthread_list_ele_t;

typedef struct match {
    size_t start;
    size_t end;
    struct match *next;
} match_list_ele_t;

typedef struct match_list {
    match_list_ele_t *head;
    match_list_ele_t *tail;
} match_list_t;

nfa_t *build_nfa(char *expression, int case_insensitive);

void print_nfa(nfa_t *nfa, FILE *outfile);

void free_nfa();

match_status_t search_buffer(char *buf, size_t bufsize, nfa_t *nfa, match_list_t *match_list, int case_insensitive, int match_full_words, int match_full_lines);


#endif  /* #ifndef NFA_H */
