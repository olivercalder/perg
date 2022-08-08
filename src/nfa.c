#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "nfa.h"


static state_t *state_list = NULL;


state_t *create_state() {
    state_t *new = malloc(sizeof(state_t));
    new->transitions = NULL;
    if (state_list != NULL)
        state_list->prev = new;
    new->next = state_list;
    state_list = new;
    new->prev = NULL;
    return new;
}


void add_transition(transition_t **transition_list,
                             char symbol,
                             t_flag_t flags,
                             state_t *next_state) {
    transition_t *new = malloc(sizeof(transition_t));
    new->next_state = next_state;
    new->next = *transition_list;
    new->prev = NULL;
    new->symbol = symbol;
    new->flags = flags;
    if (*transition_list != NULL)
        (*transition_list)->prev = new;
    *transition_list = new;
}


void cleanup_states() {
    state_t *tmp_s;
    transition_t *tmp_t;
    while ((tmp_s = state_list) != NULL) {
        while ((tmp_t = tmp_s->transitions) != NULL) {
            tmp_s->transitions = tmp_s->transitions->next;
            free(tmp_t);
        }
        state_list = state_list->next;
        free(tmp_s);
    }
}


nfa_t *build_nfa(char *expression) {
    state_t *cur_state, *prev_state = NULL;
    nfa_t *sub_nfa, *nfa = malloc(sizeof(nfa_t));
    nfa->q0 = create_state();
    nfa->qaccept = create_state();
    nfa->expr_len = 0;
    cur_state = nfa->q0;
    while (expression[nfa->expr_len] != '\0') {
        switch (expression[nfa->expr_len]) {
            case '(':
                nfa->expr_len++;    /* read from first char inside parens */
                assert(expression[nfa->expr_len] != '\0');
                sub_nfa = build_nfa(expression + nfa->expr_len);
                if (sub_nfa == NULL)
                    /* missing closing paren in a subexpression of this one */
                    return NULL;
                add_transition(&cur_state->transitions, '\0', FLAG_EPSILON, sub_nfa->q0);
                prev_state = cur_state;
                cur_state = sub_nfa->qaccept;
                nfa->expr_len += sub_nfa->expr_len;
                if (expression[nfa->expr_len] != ')') {
                    /* missing closing paren */
                    fprintf(stderr, "ERROR: unclosed parenthesis in expression: %s\n", expression);
                    return NULL;
                }
                break;
            case ')':
                if (cur_state != nfa->q0)
                    add_transition(&cur_state->transitions, '\0', FLAG_EPSILON, nfa->qaccept);
                /* This handles if total expression was (), which should match
                 * all, in which case cur_state is still nfa->q0 */
                /* This should probably actually match nothing:
                 * if matches with everything, then '()*' will cause endless
                 * loop of epsilon transitions, which is bad. */
                return nfa;
            case '|':
                add_transition(&cur_state->transitions, '\0', FLAG_EPSILON, nfa->qaccept);
                cur_state = nfa->q0;
                prev_state = NULL;
                break;
            case '.':
                prev_state = cur_state;
                cur_state = create_state();
                add_transition(&prev_state->transitions, '\0',
                        FLAG_WILDCARD, cur_state);
                break;
            case '*':
                add_transition(&cur_state->transitions, '\0', FLAG_EPSILON, prev_state);
                cur_state = prev_state;
                /* prev_state unchanged since it becomes meaningless */
                break;
            case '!':
                nfa->expr_len++;
                assert(expression[nfa->expr_len] != '\0');
                switch (expression[nfa->expr_len]) {
                    case '(':
                    case ')':
                    case '|':
                    case '*':
                    case '?':
                    case '+':
                        fprintf(stderr, "ERROR: Unexpected symbol '%c' following ! symbol\n",
                                expression[nfa->expr_len]);
                        assert(0);
                    case '!':
                        /* double negative: do nothing */
                        break;
                    case '.':
                        prev_state = cur_state;
                        cur_state = create_state();
                        add_transition(&prev_state->transitions, '\0',
                                FLAG_WILDCARD | FLAG_INVERT, cur_state);
                        break;
                    case '\\':
                        nfa->expr_len++;
                        assert(expression[nfa->expr_len] != '\0');
                        switch (expression[nfa->expr_len]) {
                            case 't':
                                prev_state = cur_state;
                                cur_state = create_state();
                                add_transition(&prev_state->transitions, '\t', FLAG_INVERT, cur_state);
                                break;
                            default:
                                prev_state = cur_state;
                                cur_state = create_state();
                                add_transition(&prev_state->transitions, expression[nfa->expr_len],
                                        FLAG_INVERT, cur_state);
                                break;
                        }
                        break;
                    default:
                        prev_state = cur_state;
                        cur_state = create_state();
                        add_transition(&prev_state->transitions, expression[nfa->expr_len],
                                FLAG_INVERT, cur_state);
                        break;
                }
                break;
            case '?':
                add_transition(&prev_state->transitions, '\0', FLAG_EPSILON, cur_state);
                break;
            case '\\':
                nfa->expr_len++;
                assert(expression[nfa->expr_len] != '\0');
                switch (expression[nfa->expr_len]) {
                    case 't':
                        prev_state = cur_state;
                        cur_state = create_state();
                        add_transition(&prev_state->transitions, '\t', FLAG_NONE, cur_state);
                        break;
                    default:
                        prev_state = cur_state;
                        cur_state = create_state();
                        add_transition(&prev_state->transitions, expression[nfa->expr_len],
                                FLAG_NONE, cur_state);
                        break;
                }
                break;
            default:
                prev_state = cur_state;
                cur_state = create_state();
                add_transition(&prev_state->transitions, expression[nfa->expr_len],
                        FLAG_NONE, cur_state);
        }
        nfa->expr_len++;
    }
    return nfa;
}
