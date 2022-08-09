#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "queue.h"
#include "nfa.h"


#define ERR_EOF     0


#define COLOR_RESET ("\e[39;49m")


typedef enum {
    ARG_FLAG_NONE   = 0x00000,
    ARG_FLAG_I      = 0x00001,  /* case insensitive */
    ARG_FLAG_V      = 0x00002,  /* invert match */
    ARG_FLAG_W      = 0x00004,  /* match whole words */
    ARG_FLAG_X      = 0x00008,  /* match whole lines */
    ARG_FLAG_O      = 0x00010,  /* only print matching part of lines */
    ARG_FLAG_HH     = 0x00020,  /* print filename before each match */
    ARG_FLAG_H      = 0x00040,  /* suppress printing filename before each match */
    ARG_FLAG_N      = 0x00080,  /* prefix each matching line with line number */
    ARG_FLAG_A      = 0x00100,  /* treat binary files as text */
    ARG_FLAG_R      = 0x00200,  /* recursively read all files in each given dir */

    /* Ignored for now */
    ARG_FLAG_C      = 0x00400,
    ARG_FLAG_LL     = 0x00800,
    ARG_FLAG_L      = 0x01000,
    ARG_FLAG_Q      = 0x02000,
    ARG_FLAG_AA     = 0x04000,
    ARG_FLAG_BB     = 0x08000,
    ARG_FLAG_CC     = 0x10000,
} arg_flag_t;

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


/* Returns number of bytes read, including null terminator.
 * Return value of 0 means EOF, so caller should close file. */
size_t fill_buffer(FILE *infile, char **buf, size_t *bufsize, int *binary) {
    char c;
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
        *bufsize <<= 1;
        *buf = realloc(*buf, sizeof(char) * (*bufsize));
    }
    return bytes_read;
}


size_t preserve_buffer_overlap(char **buf, size_t *bufsize, size_t bytes_read, size_t start) {
    size_t i, bytes_to_preserve = bytes_read - start;
    /* If majority of buffer is part of a partial match, double queue size */
    if (bytes_to_preserve > ((*bufsize) >> 1)) {
        (*bufsize) <<= 1;
        *buf = realloc(*buf, sizeof(char) * (*bufsize));
    }
    for (i = 0; i < bytes_to_preserve; i++)
        (*buf)[i] = (*buf)[start + i];
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
        printf("%s", str);
    }
}


void print_from_buffer(char *buf, size_t start, size_t end, color_t color, bold_t bold) {
    char char_to_save = buf[end];
    buf[end] = '\0';
    if (color != DEFAULT)
        print_str_colored(buf + start, color, bold);
    else
        printf("%s", buf + start);
    buf[end] = char_to_save;
}


match_status_t search_file(char *filename, FILE *infile, nfa_t *nfa) {
    char *buf, *fake_buf;
    size_t bytes_read, bufsize = 4096, bytes_preserved, bytes_remaining, earliest_partial_start, i;
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
        return confirmed_match;
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
                assert(0);  /* don't bother */
                /* No full matches, so first match in queue must be in
                 * progress, and must also be the earliest match index */
                bytes_preserved = preserve_buffer_overlap(&buf, &bufsize, bytes_read, cur_match->start);
                /* If text, fill_buffer() might changes the bufsize, so the
                 * following parameters might be unchanged by fill_buffer(),
                 * which is very bad. NOT TRUE, since the whole reason we're
                 * here is that the buffer size is fixed.
                 * Nonetheless, disallow fixed buffer size for text input. */
                fake_buf = buf + bytes_preserved;
                bytes_remaining = bufsize - bytes_preserved;
                bytes_read = bytes_preserved +
                    fill_buffer(infile, &fake_buf, &bytes_remaining, &binary);
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
            case MATCH_NONE:
                bytes_read = fill_buffer(infile, &buf, &bufsize, &binary);
                break;
            case MATCH_PROGRESS:
                /* Possibly full matches in list, so check full list to see if a full match is found */
                earliest_partial_start = cur_match->start;
                while (cur_match != NULL) {
                    if (cur_match->end != 0)
                        goto GOTO_MATCH_FOUND;
                    cur_match = cur_match->next;
                }
                /* No full matches */
                bytes_preserved = preserve_buffer_overlap(&buf, &bufsize, bytes_read, earliest_partial_start);
                /* If binary, fill_buffer() never changes the bufsize, so the
                 * following parameters should be unchanged by fill_buffer() */
                fake_buf = buf + bytes_preserved;
                bytes_remaining = bufsize - bytes_preserved;
                bytes_read = bytes_preserved +
                    fill_buffer(infile, &fake_buf, &bytes_remaining, &binary);
                break;
            case MATCH_FOUND:
GOTO_MATCH_FOUND:
                confirmed_match = MATCH_FOUND;
                fprintf(stderr, "Binary file %s matches\n", filename);
                while (cur_match != NULL) {
                    (match_list.head) = (void *)(cur_match->next);
                    free(cur_match);
                    cur_match = (match_list_ele_t *)(match_list.head);
                }
                return MATCH_FOUND;
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
    arg_flag_t flags = ARG_FLAG_NONE;
    /* some code */
    if (argc < 2) {
        /* ERROR: missing expression */
        exit(1);
    }
    expression = argv[1];
    /* some code */
    nfa = build_nfa(expression);
    /* some code */
    if (argc > 2 || ARG_FLAG_R) {
        for (filepath_ptr = filepaths_head; filepath_ptr != NULL; filepath_ptr = filepath_ptr->next) {
            FILE *infile = fopen(filepath_ptr->path, "r");
            search_file(filepath_ptr->path, infile, nfa);
        }
    } else {    /* read from stdin */
        search_file("stdin", stdin, nfa);
    }
}
