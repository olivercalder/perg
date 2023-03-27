#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "include/nfa.h"


#define DEFAULT_BUFSIZE 512

#define ERR_EOF     0

#define COLOR_RESET ("\e[0;39;49m")


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
size_t fill_buffer(FILE *infile, char **buf, size_t *bufsize, int *binary, int binary_as_text) {
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
            } else if (c >= 128 && !binary_as_text) {
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
    if (end <= start)
        return;
    buf[end] = '\0';
    if (color != DEFAULT)
        print_str_colored(buf + start, color, bold);
    else
        printf("%s", buf + start);
    buf[end] = char_to_save;
}


match_status_t search_file(char *filename, FILE *infile, nfa_t *nfa, arg_flag_t flags) {
    char *buf, *fake_buf;
    size_t bytes_read, bufsize = DEFAULT_BUFSIZE, bytes_preserved, bytes_remaining, earliest_partial_start, i;
    int binary = 0;
    match_list_t match_list;
    match_list_ele_t *tmp;
    match_status_t status, confirmed_match = MATCH_NONE;
    /* Need some way of knowing whether a match was in progress at the
     * end of the buffer, in which case the buffer size should be
     * doubled and filled and any child thread in progress should be
     * re-spawned with the same start index, end state, and positioned
     * at the end of the original buffer (or at the first index of the
     * extension).
     * Current solution: match_status_t status
     */
    match_list.head = NULL;
    match_list.tail = NULL;
    buf = malloc(sizeof(char) * bufsize);
    if ((bytes_read = fill_buffer(infile, &buf, &bufsize, &binary, flags & ARG_FLAG_A)) == ERR_EOF)
        goto RETURN_STATUS;
    while (binary == 0) {
        status = search_buffer(buf, bufsize, nfa, &match_list,
                               flags & ARG_FLAG_I,
                               flags & ARG_FLAG_W,
                               flags & ARG_FLAG_X,
                               flags & ARG_FLAG_V);
        switch (status) {
        case MATCH_FOUND:
            confirmed_match = MATCH_FOUND;
            i = 0;
            if (flags & ARG_FLAG_V) {
                while (match_list.head != NULL) {
                    tmp = match_list.head;
                    match_list.head = tmp->next;
                    free(tmp);
                }
            }
            while (match_list.head != NULL) {
                if (match_list.head->end == bufsize) {
                    /* reached the end of complete matches, so stop printing
                     * and copy from start into beginning of buffer (by
                     * falling through to MATCH_PROGRESS case, and make
                     * match_list continue from here */
                    break;
                }
                if (match_list.head->start >= i) {
                    /* print line between previous and current match */
                    print_from_buffer(buf, i, match_list.head->start, DEFAULT, STANDARD);
                    /* print current match */
                    print_from_buffer(buf, match_list.head->start, match_list.head->end, RED, BOLD);
                    i = match_list.head->end;
                }
                tmp = match_list.head;
                match_list.head = tmp->next;
                free(tmp);
            }
            if (match_list.head == NULL) {
                match_list.tail = NULL;
                print_from_buffer(buf, i, bytes_read, DEFAULT, STANDARD);
                printf("\n");
                if ((bytes_read = fill_buffer(infile, &buf, &bufsize, &binary, flags & ARG_FLAG_A)) == ERR_EOF)
                    goto RETURN_STATUS;
                break;
            }
            /* else there were some partial matches, so handle them by
             * falling through to case MATCH_PROGRESS */
        case MATCH_PROGRESS:    /* only possible if upper limit on bufsize for text files */
            assert(0 && "don't allow partial matches on text files");   /* don't bother */
            /* No full matches, so first match in queue must be in
             * progress, and must also be the earliest match index */
            bytes_preserved = preserve_buffer_overlap(&buf, &bufsize, bytes_read, match_list.head->start);
            /* If text, fill_buffer() might changes the bufsize, so the
             * following parameters might be unchanged by fill_buffer(),
             * which is very bad. NOT TRUE, since the whole reason we're
             * here is that the buffer size is fixed.
             * Nonetheless, disallow fixed buffer size for text input. */
            while (match_list.head != NULL) {
                tmp = match_list.head;
                match_list.head = tmp->next;
                free(tmp);
            }
            match_list.tail = NULL;
            fake_buf = buf + bytes_preserved;
            bytes_remaining = bufsize - bytes_preserved;
            bytes_read = bytes_preserved +
                fill_buffer(infile, &fake_buf, &bytes_remaining, &binary, flags & ARG_FLAG_A);
            if (bytes_read == bytes_preserved)  /* fill_buffer() returned ERR_EOF */
                goto RETURN_STATUS;
            break;
        case MATCH_NONE:
            while (match_list.head != NULL) {
                /* if invert_match, will have matches in the list, else does nothing */
                tmp = match_list.head;
                match_list.head = tmp->next;
                free(tmp);
            }
            match_list.tail = NULL;
            if ((bytes_read = fill_buffer(infile, &buf, &bufsize, &binary, flags & ARG_FLAG_A)) == ERR_EOF)
                goto RETURN_STATUS;
            /* assert((match_list.head | match_list.tail) == NULL); */
            break;
        }
    }
    while (binary != 0) {   /* always true once true; a convenient "while (1)" */
        status = search_buffer(buf, bufsize, nfa, &match_list,
                               flags & ARG_FLAG_I,
                               flags & ARG_FLAG_W,
                               flags & ARG_FLAG_X,
                               flags & ARG_FLAG_V);
        switch (status) {
        case MATCH_NONE:
            while (match_list.head != NULL) {
                /* if invert_match, will have matches in the list, else does nothing */
                tmp = match_list.head;
                match_list.head = tmp->next;
                free(tmp);
            }
            match_list.tail = NULL;
            if ((bytes_read = fill_buffer(infile, &buf, &bufsize, &binary, flags & ARG_FLAG_A)) == ERR_EOF)
                goto RETURN_STATUS;
            break;
        case MATCH_PROGRESS:
            /* Possibly full matches in list, so check full list to see if a
             * full match is found */
            earliest_partial_start = match_list.head->start;
            while (match_list.head != NULL) {
                if (match_list.head->end != 0)  /* only MATCH_FOUND has end != 0 */
                    goto BINARY_MATCH_FOUND;
                tmp = match_list.head;
                match_list.head = tmp->next;
                free(tmp);
            }
            match_list.tail = NULL;
            /* No full matches */
            bytes_preserved = preserve_buffer_overlap(&buf, &bufsize, bytes_read, earliest_partial_start);
            /* If binary, fill_buffer() never changes the bufsize, so the
             * following parameters should be unchanged by fill_buffer() */
            fake_buf = buf + bytes_preserved;
            bytes_remaining = bufsize - bytes_preserved;
            bytes_read = bytes_preserved +
                fill_buffer(infile, &fake_buf, &bytes_remaining, &binary, flags & ARG_FLAG_A);
            if (bytes_read == bytes_preserved) {    /* fill_buffer() returned ERR_EOF */
                confirmed_match = MATCH_NONE;
                goto RETURN_STATUS;
            }
            break;
        case MATCH_FOUND:
BINARY_MATCH_FOUND:
            confirmed_match = MATCH_FOUND;
            while (match_list.head != NULL) {
                /* don't care about match positions, just whether they occured */
                tmp = match_list.head;
                match_list.head = tmp->next;
                free(tmp);
            }
            match_list.tail = NULL;
            fprintf(stderr, "Binary file %s matches\n", filename);
            goto RETURN_STATUS;
        }
    }
RETURN_STATUS:
    while (match_list.head != NULL) {
        tmp = match_list.head;
        match_list.head = tmp->next;
        free(tmp);
    }
    match_list.tail = NULL;
    free(buf);
    return confirmed_match;
}


match_status_t search_filepaths(struct filepath_node *filepaths, nfa_t *nfa, arg_flag_t flags) {
    struct filepath_node *curr_fp;
    FILE *infile;
    match_status_t status = MATCH_NONE;
    for (curr_fp = filepaths; curr_fp != NULL; curr_fp = curr_fp->next) {
        infile = fopen(curr_fp->path, "r");
        if (search_file(curr_fp->path, infile, nfa, flags) == MATCH_FOUND)
            status = MATCH_FOUND;
        fclose(infile);
    }
    return status;
}


struct filepath_node *build_recursive_filepaths_list(char *filepath) {
    struct filepath_node *head = NULL;//, *tail = NULL;
    /* If filename is a directory, recursively add files in filename */
    /* If filename is not a directory, add it to list */
    assert(0 && "Recursive searching not yet implemented");
    return head;
}


void cleanup_filepaths(struct filepath_node *filepaths) {
    struct filepath_node *tmp;
    while (filepaths != NULL) {
        tmp = filepaths;
        filepaths = filepaths->next;
        free(tmp);
    }
}


void print_usage(FILE *outfile, char *name) {
    fprintf(outfile, "USAGE: %s [OPTION]... EXPRESSION [FILE]...\n", name);
}


int main(int argc, char *argv[]) {
    char *expression;
    struct filepath_node *filepaths;
    FILE *infile;
    int opt, i;
    match_status_t status = MATCH_NONE;
    nfa_t *nfa;
    arg_flag_t flags = ARG_FLAG_NONE;
    /* some code */
    while ((opt = getopt(argc, argv, "aABcChHilLnoqrvwx")) != -1) {
        switch (opt) {
        case 'a':
            flags |= ARG_FLAG_A;
            break;
        case 'h':
            flags |= ARG_FLAG_H;
            flags &= ~ARG_FLAG_HH;
            break;
        case 'H':
            flags |= ARG_FLAG_HH;
            flags &= ~ARG_FLAG_H;
            break;
        case 'i':
            flags |= ARG_FLAG_I;
            break;
        case 'n':
            flags |= ARG_FLAG_N;
            break;
        case 'o':
            flags |= ARG_FLAG_O;
            break;
        case 'r':
            flags |= ARG_FLAG_R;
            break;
        case 'v':
            flags |= ARG_FLAG_V;
            break;
        case 'w':
            flags |= ARG_FLAG_W;
            break;
        case 'x':
            flags |= ARG_FLAG_X;
            break;
        /* The following flags are not yet implemented */
        case 'A':
        case 'B':
        case 'c':
        case 'C':
        case 'l':
        case 'L':
        case 'q':
            fprintf(stderr, "ERROR: option -%c not yet implemented.\n", opt);
            print_usage(stderr, argv[0]);
            exit(1);
        default:
            fprintf(stderr, "ERROR: argument -%c unrecognized.\n", opt);
            print_usage(stderr, argv[0]);
            exit(1);
        }
    }
    if (argc - optind == 0) {
        print_usage(stderr, argv[0]);
        exit(1);
    }
    expression = argv[optind];
    /* some code */
    nfa = build_nfa(expression, flags & ARG_FLAG_I);
    if (nfa == NULL)
        exit(1);
    /* some code */
    if (flags & ARG_FLAG_R) {
        if (argc - optind == 1) {
            filepaths = build_recursive_filepaths_list(".");
            if (search_filepaths(filepaths, nfa, flags) == MATCH_FOUND)
                status = MATCH_FOUND;
            cleanup_filepaths(filepaths);
        }   /* implied else */
        for (i = optind + 1; i < argc; i++) {
            filepaths = build_recursive_filepaths_list(argv[i]);
            if (search_filepaths(filepaths, nfa, flags) == MATCH_FOUND)
                status = MATCH_FOUND;
            cleanup_filepaths(filepaths);
        }
    } else {
        if (argc - optind == 1) /* read from stdin */
            status = search_file("stdin", stdin, nfa, flags);
        for (i = optind + 1; i < argc; i++) {
            infile = fopen(argv[i], "r");
            if (search_file(argv[i], infile, nfa, flags) == MATCH_FOUND)
                status = MATCH_FOUND;
            fclose(infile);
        }
    }
    free_nfa(nfa);
    return (status != MATCH_FOUND);
}
