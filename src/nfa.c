#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

#include "include/nfa.h"


static state_t *state_list = NULL;

static int state_count = 0;


state_t *create_state() {
    state_t *new = malloc(sizeof(state_t));
    new->transitions = NULL;
    if (state_list != NULL)
        state_list->prev = new;
    new->next = state_list;
    state_list = new;
    new->prev = NULL;
    new->id = state_count;
    state_count++;
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


nfa_t *build_nfa(char *expression, int case_insensitive) {
    state_t *cur_state, *prev_state = NULL;
    transition_t *cur_transition;
    nfa_t *sub_nfa, *nfa = malloc(sizeof(nfa_t));
    nfa->q0 = create_state();
    nfa->qaccept = create_state();
    nfa->expr_len = 0;
    cur_state = nfa->q0;
    while (1) {
        switch (expression[nfa->expr_len]) {
        case '(':
            nfa->expr_len++;    /* read from first char inside parens */
            assert(expression[nfa->expr_len] != '\0');
            if (expression[nfa->expr_len] == ')') {
                /* subexpression is (), which is meaningless. But other
                    * metachars *, ?, and + should have no effect when
                    * following (). So verify this. */
                nfa->expr_len++;    /* move to next symbol */
                switch (expression[nfa->expr_len]) {
                case '*':
                case '?':
                case '+':
                    break;
                default:
                    nfa->expr_len--;    /* move back so "next symbol" is handled */
                    break;
                }
            }
            sub_nfa = build_nfa(expression + nfa->expr_len, case_insensitive);
            if (sub_nfa == NULL)
                /* missing closing paren in a subexpression of this one */
                return NULL;
            /* Make direct transition from cur_state to all transition states
                * of sub_nfa->q0, both to save time at runtime and to ensure
                * that there are no epsilon transitions from nfa->q0.
                * However, leave sub_nfa->q0 in place, since there may be
                * internal states which transition to sub_nfa->q0. */
            cur_transition = sub_nfa->q0->transitions;
            while (cur_transition != NULL) {
                add_transition(&cur_state->transitions,
                        cur_transition->symbol,
                        cur_transition->flags,
                        cur_transition->next_state);
                cur_transition = cur_transition->next;
            }
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
            /* Cannot be the case that prev symbol was '(', which would lead
                * to "()", since there is a check in the handler for '('.
                * Otherwise, the expression "()" would match everything, and
                * "...()*...", "...()?...", and "...()+..." would cause problems. */
            add_transition(&cur_state->transitions, '\0', FLAG_EPSILON, nfa->qaccept);
            return nfa;
        case '\0':
            add_transition(&cur_state->transitions, '\0', FLAG_EPSILON, nfa->qaccept);
            return nfa;
        case '|':
            if (cur_state == nfa->q0)
                /* match nothing|..., so the | does nothing, so skip it */
                break;
            add_transition(&cur_state->transitions, '\0', FLAG_EPSILON, nfa->qaccept);
            cur_state = nfa->q0;
            prev_state = NULL;
            break;
        case '.':
            prev_state = cur_state;
            cur_state = create_state();
            add_transition(&prev_state->transitions, '\0', FLAG_WILDCARD, cur_state);
            break;
        case '*':
            if (prev_state == cur_state)
                /* two * in a row, which is equivalent to one, so ignore the second */
                break;
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
                /* !. would seem to imply no symbol, so ignore it */
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
                    if (case_insensitive &&
                            (expression[nfa->expr_len] >= 0x41) &&
                            (expression[nfa->expr_len] <= 0x5A))
                        add_transition(&prev_state->transitions,
                                        expression[nfa->expr_len] | 0x20,
                                        FLAG_INVERT, cur_state);
                    else
                        add_transition(&prev_state->transitions,
                                        expression[nfa->expr_len],
                                        FLAG_INVERT, cur_state);
                    break;
                }
                break;
            default:
                prev_state = cur_state;
                cur_state = create_state();
                if (case_insensitive &&
                        (expression[nfa->expr_len] >= 0x41) &&
                        (expression[nfa->expr_len] <= 0x5A))
                    add_transition(&prev_state->transitions,
                                    expression[nfa->expr_len] | 0x20,
                                    FLAG_INVERT, cur_state);
                else
                    add_transition(&prev_state->transitions,
                                    expression[nfa->expr_len],
                                    FLAG_INVERT, cur_state);
                break;
            }
            break;
        case '?':
            if (prev_state == cur_state)
                /* previous symbol was *, so this ? is meaningless, so ignore it */
                break;
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
                if (case_insensitive &&
                        (expression[nfa->expr_len] >= 0x41) &&
                        (expression[nfa->expr_len] <= 0x5A))
                    add_transition(&prev_state->transitions,
                                    expression[nfa->expr_len] | 0x20,
                                    FLAG_NONE, cur_state);
                else
                    add_transition(&prev_state->transitions,
                                    expression[nfa->expr_len],
                                    FLAG_NONE, cur_state);
                break;
            }
            break;
        default:
            prev_state = cur_state;
            cur_state = create_state();
            if (case_insensitive &&
                    (expression[nfa->expr_len] >= 0x41) &&
                    (expression[nfa->expr_len] <= 0x5A))
                add_transition(&prev_state->transitions,
                                expression[nfa->expr_len] | 0x20,
                                FLAG_NONE, cur_state);
            else
                add_transition(&prev_state->transitions,
                                expression[nfa->expr_len],
                                FLAG_NONE, cur_state);
        }
        nfa->expr_len++;
    }
    return nfa;
}


void print_nfa(nfa_t *nfa, FILE *outfile) {
    transition_t *cur_t;
    state_t *cur_s = state_list;
    fprintf(outfile, "Printing nfa:\n");
    fprintf(outfile, "start: q%d\naccept: q%d\n", nfa->q0->id, nfa->qaccept->id);
    for (cur_s = state_list; cur_s != NULL; cur_s = cur_s->next) {
        for (cur_t = cur_s->transitions; cur_t != NULL; cur_t = cur_t->next) {
            fprintf(outfile, "q%d->q%d: %c (%d)\n", cur_s->id, cur_t->next_state->id, cur_t->symbol, cur_t->flags);
        }
    }
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


void free_nfa(nfa_t *nfa) {
    cleanup_states();
    free(nfa);
}


void *run_nfa(void *arg) {
    match_status_t match_status = MATCH_NONE;
    void *retval;
    size_t pos = ((nfa_arg_t *)arg)->pos;
    transition_t *cur_t = ((nfa_arg_t *)arg)->state->transitions;
    pthread_list_ele_t *tmp, *threads = NULL;
    size_t future_child_pos;
    state_t *future_child_state = NULL;
    char c = ((nfa_arg_t *)arg)->buf[pos];
    if (((nfa_arg_t *)arg)->state == ((nfa_arg_t *)arg)->qaccept) {
        ((nfa_arg_t *)arg)->end = pos;
        return (void *)MATCH_FOUND;
    }
    if (pos == ((nfa_arg_t *)arg)->bufsize - 1)
        return (void *)MATCH_PROGRESS;
    if (((nfa_arg_t *)arg)->case_insensitive && c >= 0x41 && c <= 0x5A)
        c |= 0x20;  /* transitions should already be case insensitive */
    while (cur_t != NULL) {   /* catch viable transitions, and fork on previous one */
        if (cur_t->flags == FLAG_EPSILON || (c != '\0' &&
                    (cur_t->flags == FLAG_WILDCARD ||
                     (cur_t->flags == FLAG_INVERT ?
                      c != cur_t->symbol :
                      c == cur_t->symbol)))) {
            if (future_child_state != NULL) {
                tmp = malloc(sizeof(pthread_list_ele_t));
                tmp->next = threads;
                threads = tmp;
                tmp->arg.buf = ((nfa_arg_t *)arg)->buf;
                tmp->arg.bufsize = ((nfa_arg_t *)arg)->bufsize;
                tmp->arg.state = future_child_state;
                tmp->arg.qaccept = ((nfa_arg_t *)arg)->qaccept;
                tmp->arg.pos = future_child_pos;
                tmp->arg.end = 0;
                tmp->arg.case_insensitive = ((nfa_arg_t *)arg)->case_insensitive;
                pthread_create(&tmp->thread, NULL, &run_nfa, &tmp->arg);
                //fprintf(stderr, "Forked thread on q%d->q%d: %c at position %d\n", ((nfa_arg_t *)arg)->state->id, future_child_state->id, ((nfa_arg_t *)arg)->buf[((nfa_arg_t *)arg)->pos], ((nfa_arg_t *)arg)->pos);
            }
            future_child_pos = pos + (cur_t->flags != FLAG_EPSILON);
            future_child_state = cur_t->next_state;
        }
        cur_t = cur_t->next;
    }
    if (future_child_state == NULL)
        return (void *)MATCH_NONE;
    /* Now, all but most recent viable transition have been forked */
    if (threads == NULL) {
        /* There was only one viable transition, so do it yourself */
        ((nfa_arg_t *)arg)->state = future_child_state;
        ((nfa_arg_t *)arg)->pos = future_child_pos;
        match_status = (match_status_t)run_nfa(arg);
        ((nfa_arg_t *)arg)->pos = pos;
        return (void *)match_status;
    }
    /* There were multiple viable transitions, so fork the last one */
    tmp = malloc(sizeof(pthread_list_ele_t));
    tmp->next = threads;
    threads = tmp;
    tmp->arg.buf = ((nfa_arg_t *)arg)->buf;
    tmp->arg.bufsize = ((nfa_arg_t *)arg)->bufsize;
    tmp->arg.state = future_child_state;
    tmp->arg.qaccept = ((nfa_arg_t *)arg)->qaccept;
    tmp->arg.pos = future_child_pos;
    tmp->arg.end = 0;
    tmp->arg.case_insensitive = ((nfa_arg_t *)arg)->case_insensitive;
    pthread_create(&tmp->thread, NULL, &run_nfa, &tmp->arg);
    //fprintf(stderr, "Forked thread on q%d->q%d: %c at position %d\n", ((nfa_arg_t *)arg)->state->id, future_child_state->id, ((nfa_arg_t *)arg)->buf[((nfa_arg_t *)arg)->pos], ((nfa_arg_t *)arg)->pos);
    /* There were threads forked, so join them and take the best */
    while (threads != NULL) {
        pthread_join(threads->thread, &retval);
        /* Here, prioritize MATCH_PROGRESS over MATCH_FOUND, and use the end
         * position to indicate that a full match was found. */
        switch ((match_status_t)retval) {
            case MATCH_NONE:
                break;
            case MATCH_FOUND:
                if (match_status == MATCH_NONE)
                    match_status = MATCH_FOUND;
                break;
            case MATCH_PROGRESS:
                match_status = MATCH_PROGRESS;
                break;
        }
        if (threads->arg.end > ((nfa_arg_t *)arg)->end)
            ((nfa_arg_t *)arg)->end = threads->arg.end;
        tmp = threads;
        threads = threads->next;
        free(tmp);
    }
    return (void *)match_status;
}


match_status_t search_buffer(char *buf, size_t bufsize, nfa_t *nfa,
                             match_list_t *match_list,  int case_insensitive,
                             int match_full_words,      int match_full_lines,
                             int invert_match) {
    /* Do _not_ overwrite match_list->head or ->tail or assume they are NULL,
     * since in text mode, if partial matches exist, they are preserved by
     * maintaining a position in the match list. */
    size_t pos;
    pthread_list_ele_t *head = NULL, *tail = NULL, *tmp;
    match_list_ele_t *new_match;
    void *retval;
    match_status_t match_status = MATCH_NONE;
    transition_t *cur_transition;
    char c;
    for (pos = 0; pos < bufsize && buf[pos] != '\0'; pos++) {
TOP_OF_FOR_LOOP:
        c = buf[pos];
        if (case_insensitive && c >= 0x41 && c <= 0x5A)
            c |= 0x20;
        cur_transition = nfa->q0->transitions;
        while (cur_transition != NULL) {
            if (cur_transition->flags == FLAG_WILDCARD ||
                    (cur_transition->flags == FLAG_INVERT ?
                     cur_transition->symbol != c :
                     cur_transition->symbol == c))
                break;
            cur_transition = cur_transition->next;
        }
        if (match_full_lines && (cur_transition == NULL || match_list->head != NULL)) {
            /* No viable transition from first position, or the buffer isn't
             * actually the start of the line, so don't count any matches */
            /* TODO figure out what to do about previous partial matches and preserved buffers */
            return (invert_match ? MATCH_FOUND : MATCH_NONE);
        }
        if (cur_transition == NULL) {
            /* no viable transitions, move on */
            if (match_full_words)
                goto SKIP_TO_NEXT_WORD;
            continue;
        }
        /* "fork" a child to search from this index */
        tmp = malloc(sizeof(pthread_list_ele_t));
        if (tail == NULL)
            head = tmp;
        else
            tail->next = tmp;
        tail = tmp;
        tmp->next = NULL;
        tmp->arg.buf = buf;
        tmp->arg.bufsize = bufsize;
        tmp->arg.state = nfa->q0;
        tmp->arg.qaccept = nfa->qaccept;
        tmp->arg.pos = pos;
        tmp->arg.end = 0;
        tmp->arg.case_insensitive = case_insensitive;
        if (match_full_lines) {
            match_status = (match_status_t)run_nfa(&tmp->arg);
            if (buf[tmp->arg.end] != '\0') {
                free(tmp);
                return (invert_match ? MATCH_FOUND : MATCH_NONE);
            }
            new_match = malloc(sizeof(match_list_ele_t));
            new_match->start = tmp->arg.pos;    /* must be 0 */
            new_match->end = tmp->arg.end;
            new_match->next = NULL;
            match_list->head = new_match;
            match_list->tail = new_match;
            free(tmp);
            goto RETURN_OR_INVERT_STATUS;
        }
        pthread_create(&tmp->thread, NULL, &run_nfa, &tmp->arg);
        //fprintf(stderr, "Forked thread on q%d->q%d: %c at position %d\n", ((nfa_arg_t *)arg)->state->id, future_child_state->id, ((nfa_arg_t *)arg)->buf[((nfa_arg_t *)arg)->pos], ((nfa_arg_t *)arg)->pos);
        if (match_full_words) { /* advance until buf[pos] is whitespace */
SKIP_TO_NEXT_WORD:
            while (pos < bufsize) {
                switch (buf[pos]) {
                case '\0':
                    goto END_OF_BUF;
                case '\t':
                case ' ':
                    goto TOP_OF_FOR_LOOP;
                default:
                    pos++;
                }
            }
        }
    }
END_OF_BUF:
    while (head != NULL) {
        pthread_join(head->thread, &retval);
        if (match_full_words) {
            switch (buf[head->arg.end]) {
            case '\0':
            case '\t':
            case ' ':
                break;
            default:    /* not whitespace or end of buf, so not a match */
                retval = (void *)MATCH_NONE;
                head->arg.end = 0;
            }
        }
        /* Here, prioritize MATCH_PROGRESS over MATCH_FOUND, and use the end
         * position to indicate that a full match was found. */
        switch ((match_status_t)retval) {
        case MATCH_NONE:
            break;
        case MATCH_FOUND:
            if (match_status == MATCH_NONE)
                match_status = MATCH_FOUND;
            break;
        case MATCH_PROGRESS:
            match_status = MATCH_PROGRESS;
            break;
        }
        if (head->arg.end > head->arg.pos) {
            new_match = malloc(sizeof(match_list_ele_t));
            new_match->start = head->arg.pos;
            new_match->end = head->arg.end;
            new_match->next = NULL;
            if (match_list->head == NULL)
                match_list->head = new_match;
            else
                match_list->tail->next = new_match;
            match_list->tail = new_match;
        }
        tmp = head;
        head = head->next;
        free(tmp);
    }
RETURN_OR_INVERT_STATUS:
    if (invert_match) {
        switch (match_status) {
        case MATCH_NONE:
            return MATCH_FOUND;
        case MATCH_PROGRESS:
            return MATCH_PROGRESS;
        case MATCH_FOUND:
            return MATCH_NONE;
        }
    }
    return match_status;
}

