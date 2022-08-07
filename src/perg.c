#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "queue.h"
#include "nfa.h"


#define ERR_BINARY  (~0)
#define ERR_EOF     0


#define COLOR_RESET ("\e[39;49m")


typedef enum {
    DEFAULT = 0,
    BLACK   = 30,
    RED     = 31,
    GREEN   = 32,
    YELLOW  = 33,
    BLUE    = 34,
    MAGENTA = 35,
    CYAN    = 36,
    WHITE   = 37,
} color_t;

typedef enum {
    STANDARD    = 0,
    BOLD        = 1,
} bold_t;

typedef struct filepath_node {
    char *path;
    struct filepath_node *next;
} filepath_node_t;

typedef struct plet {
    pthread_t thread;
    size_t start;
    size_t end;
    struct plet *next;
} pthread_list_ele_t;

typedef struct match {
    size_t start;
    size_t end;
    struct match *next;
} match_list_ele_t;

typedef enum {
    MATCH_NONE,
    MATCH_PROGRESS,
    MATCH_FOUND,
} match_status_t;


/* Returns number of bytes read, including null terminator.
 * Return value of 0 means EOF, so caller should close file. */
size_t fill_buffer(FILE *infile, char **buf, size_t *bufsize, int *binary) {
    char c *tmp_buf;
    size_t bytes_read = 0;
    c = fgetc(infile);
    if (c == EOF || bufsize == 0)
        return ERR_EOF;
    if (c == '\n' && *binary == 0) {
        (*buf)[bytes_read] = '\0';
        return 1;
    }
    (*buf)[bytes_read] = c;
    bytes_read++;
    /* Need to fgetc(infile) after checking bytes_read, and dont' want to
     * update bytes_read until c has been processed */
    while (1) {
        while (bytes_read < *bufsize) {
            c = fgetc(infile);
            if (c == EOF || (c == '\n' && *binary == 0)) {
                (*buf)[bytes_read] = '\0';
                bytes_read++;
                return bytes_read;
            } else if (c >= 128) {
                /* Binary file, ignore unless -a flag given... for now, always
                 * print error and stop reading file */
                *binary = 1;
            }
            /* *binary |= (c >> 7) & 1; */
            (*buf)[bytes_read] = c;
            bytes_read++;
        }
        if (bytes_read < *bufsize || *binary != 0) {
            break;
        }
        /* Buffer filled but not done with line (in text mode), so double buffer size */
        tmp_buf = malloc(sizeof(char) * (*bufsize) << 1);
        memcpy(tmp_buf, *buf, *bufsize);
        free(*buf);
        *buf = tmp_buf;
        *bufsize <<= 1;
    }
    return bytes_read;
}


match_status_t search_buffer(char *buf, size_t bufsize, nfa_t *nfa, queue_t *match_list){
    /* Do _not_ overwrite match_list->head or ->tail or assume they are null,
     * since in text mode, if partial matches exist, they are preserved by
     * maintaining a position in the match list. */
}


size_t preserve_buffer_overlap(char **buf, size_t *bufsize, size_t bytes_read, size_t start) {
    char *orig_buf = *buf;
    size_t bytes_remaining, i, bytes_to_preserve = bytes_read - start;
    /* If majority of buffer is part of a partial match, double queue size */
    if (bytes_to_preserve > ((*bufsize) >> 1)) {
        (*bufsize) <<= 1;
        *buf = malloc(sizeof(char) * (*bufsize));
    }
    bytes_remaining = (*bufsize) - bytes_to_preserve;
    for (i = 0; i < bytes_to_preserve; i++)
        buf[i] = orig_buf[start + i];
    if (orig_buf != *buf)
        free(orig_buf);
    return bytes_to_preserve;
}


void print_str_colored(char *str, color_t color, bold_t bold) {
    if (isatty(fileno(stdout))) {
        switch (bold) {
            case STANDARD:
                printf("\e[%dm%s%s", color, str, COLOR_RESET);
                break;
            case BOLD:
                printf("\e[1;%dm%s%s", color, str, COLOR_RESET);
                break;
        }
    } else {
        printf(str);
    }
}


void print_from_buffer(char *buf, size_t start, size_t end, color_t color, bold_t bold) {
    char char_to_save = buf[end];
    buf[end] = '\0';
    if (color != DEFAULT)
        print_str_colored(buf + start, color, bold);
    else
        printf(buf + start);
    buf[end] = char_to_save;
}


match_status_t search_file(char *filename, FILE *infile, nfa_t *nfa, size_t expr_len) {
    char *buf;
    size_t bytes_read, bufsize = 4096, bytes_preserved, i;
    int binary = 0;
    queue_t match_list;
    match_list_ele_t *cur_match;
    match_status_t status, confirmed_match = MATCH_NONE;
    /* Need some way of knowing whether a match was in progress at the
     * end of the buffer, in which case the buffer size should be
     * doubled and filled and any child thread in progress should be
     * re-spawned with the same start index, end state, and positioned
     * at the end of the original buffer (or at the first index of the
     * extension).
     * Current solution: match_status_t status
     */
    buf = malloc(sizeof(char) * bufsize);
    if ((bytes_read = fill_buffer(infile, &buf, &bufsize, &binary)) == ERR_EOF)
        return;
    match_list.head = NULL;
    match_list.tail = NULL;
    while (binary == 0) {
        status = search_buffer(buf, bufsize, nfa, &match_list);
        cur_match = (match_list_ele_t *)(match_list.head);
        switch (status) {
            case MATCH_FOUND:
                confirmed_match = MATCH_FOUND;
                i = 0;
                while (cur_match != NULL) {
                    if (cur_match->end == bufsize) {
                        /* reached the end of complete matches, so stop printing
                         * and copy from start into beginning of buffer (by
                         * falling through to MATCH_PROGRESS case, and make
                         * match_list continue from here */
                        break;
                    }
                    /* print line between previous and current match */
                    print_from_buffer(buf, i, cur_match->start, DEFAULT, STANDARD);
                    /* print current match */
                    print_from_buffer(buf, cur_match->start, cur_match->end, RED, BOLD);
                    i = cur_match->end;
                    (match_list.head) = (void *)(cur_match->next);
                    free(cur_match);
                    cur_match = (match_list_ele_t *)(match_list.head);
                }
                if (match_list.head == NULL) {    /* cur_match == NULL */
                    match_list.tail = NULL;
                    print_from_buffer(buf, i, bytes_read, DEFAULT, STANDARD);
                    printf("\n");
                    bytes_read = fill_buffer(infile, &buf, &bufsize, &binary);
                    break;
                }
                /* else there were some partial matches, so handle them by
                 * falling through to case MATCH_PROGRESS */
            case MATCH_PROGRESS:    /* only possible if upper limit on bufsize for text files */
                /* No full matches, so first match in queue must be in
                 * progress, and must also be the earliest match index */
                bytes_preserved = preserve_buffer_overlap(&buf, &bufsize, bytes_read, cur_match->start);
                /* If text, fill_buffer() might changes the bufsize, so the
                 * following parameters might be unchanged by fill_buffer(),
                 * which is very bad. TODO: fix it
                 * ... or disallow fixed buffer size for text input */
                bytes_read = bytes_to_preserve +
                    fill_buffer(infile, &(buf + bytes_preserved), &bytes_remaining, &binary);
                /* I don't think this   ^^^^^^^^^^^^^^^^^^^^^^^^  will work... TODO: fix it */
                break;
            case MATCH_NONE:
                bytes_read = fill_buffer(infile, &buf, &bufsize, &binary);
                /* assert((match_list.head | match_list.tail) == NULL); */
                break;
        }
    }
    while (binary != 0) {   /* always true once true; a convenient "while (1)" */
        status = search_buffer(buf, bytes_read, nfa, &match_list);
        cur_match = (match_list_ele_t *)(match_list.head);  /* may be NULL if status == MATCH_NONE */
        switch (status) {
            case MATCH_FOUND:
                confirmed_match = MATCH_FOUND;
                fprintf(stderr, "Binary file %s matches\n", filename);
                while (cur_match != NULL) {
                    (match_list.head) = (void *)(cur_match.next);
                    free(cur_match);
                    cur_match = (match_list_ele_t *)(match_list.head);
                }
                return MATCH_FOUND;
            case MATCH_PROGRESS:
                /* No full matches, so first match in queue must be in
                 * progress, and must also be the earliest match index */
                bytes_preserved = preserve_buffer_overlap(&buf, &bufsize, bytes_read, cur_match->start);
                /* If binary, fill_buffer() never changes the bufsize, so the
                    * following parameters should be unchanged by fill_buffer() */
                bytes_read = bytes_to_preserve +
                    fill_buffer(infile, &(buf + bytes_preserved), &bytes_remaining, &binary);
                /* I don't think this   ^^^^^^^^^^^^^^^^^^^^^^^^  will work... TODO: fix it */
                break;
            case MATCH_NONE:
                bytes_read = fill_buffer(infile, &buf, &bufsize, &binary);
                break;
        }
    }
    free(buf);
    return confirmed_match;
}


int main(int argc, char *argv[]) {
    char c, *buf, *expression;
    struct filepath_node *filepaths_head, *filepath_ptr;
    FILE *infile, *outfile;
    nfa_t *nfa;
    /* some code */
    if (argv < 2) {
        /* ERROR: missing expression */
        exit(1);
    }
    expression = argv[1];
    /* some code */
    nfa = build_nfa(expression);
    /* some code */
    if (argc > 2 || R_FLAG) {
        for (filepath_ptr = filepaths_head; filepath_ptr != NULL; filepath_ptr = filepath_ptr->next) {
            filepath = filepath_ptr->path;
            FILE *infile = fopen(filepath, "r");
        }
    } else {    /* read from stdin */
        search_file(stdin, nfa, outfile);
    }
}
