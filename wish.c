// wish — Wisconsin Shell (OSTEP processes-shell project)
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_PATHS 256
#define MAX_CMDS  256

static char *paths[MAX_PATHS];
static int npaths = 0;

static void print_error(void) {
    static const char msg[] = "An error has occurred\n";
    if (write(STDERR_FILENO, msg, sizeof(msg) - 1) < 0) { /* nothing to do */ }
}

static void set_path(char **dirs, int n) {
    for (int i = 0; i < npaths; i++) free(paths[i]);
    npaths = 0;
    for (int i = 0; i < n && npaths < MAX_PATHS; i++) paths[npaths++] = strdup(dirs[i]);
}

/* Розбиває рядок на токени за пробілами/табами; '>' та '&' стають окремими токенами. */
static int tokenize(char *line, char ***out) {
    size_t cap = 16, n = 0;
    char **toks = malloc(cap * sizeof(char *));
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        char *start;
        size_t len;
        if (*p == '>' || *p == '&') { start = p; len = 1; p++; }
        else {
            start = p;
            while (*p && !strchr(" \t\n\r>&", *p)) p++;
            len = (size_t)(p - start);
        }
        if (n + 1 >= cap) { cap *= 2; toks = realloc(toks, cap * sizeof(char *)); }
        toks[n++] = strndup(start, len);
    }
    toks[n] = NULL;
    *out = toks;
    return (int)n;
}

static char *find_exec(const char *name) {
    for (int i = 0; i < npaths; i++) {
        char *full;
        if (asprintf(&full, "%s/%s", paths[i], name) < 0) return NULL;
        if (access(full, X_OK) == 0) return full;
        free(full);
    }
    return NULL;
}

/* Запускає одну команду (токени [s, e)). Повертає pid дочірнього процесу, або -1. */
static pid_t run_command(char **toks, int s, int e) {
    if (s >= e) return -1;                       /* порожня команда між '&' — не помилка */

    int redir = -1;
    for (int i = s; i < e; i++)
        if (strcmp(toks[i], ">") == 0) {
            if (redir != -1) { print_error(); return -1; }  /* декілька '>' */
            redir = i;
        }
    char *outfile = NULL;
    int argend = e;
    if (redir != -1) {
        if (redir == s || e - redir != 2) { print_error(); return -1; }
        outfile = toks[redir + 1];
        argend = redir;
    }
    int argc = argend - s;
    char **argv = &toks[s];

    /* Вбудовані команди */
    if (strcmp(argv[0], "exit") == 0) {
        if (argc != 1) { print_error(); return -1; }
        exit(0);
    }
    if (strcmp(argv[0], "cd") == 0) {
        if (argc != 2 || chdir(argv[1]) != 0) print_error();
        return -1;
    }
    if (strcmp(argv[0], "path") == 0) {
        set_path(argv + 1, argc - 1);
        return -1;
    }

    char *prog = find_exec(argv[0]);
    if (!prog) { print_error(); return -1; }

    char **cargv = malloc((argc + 1) * sizeof(char *));
    for (int i = 0; i < argc; i++) cargv[i] = argv[i];
    cargv[argc] = NULL;

    pid_t pid = fork();
    if (pid < 0) { print_error(); free(prog); free(cargv); return -1; }
    if (pid == 0) {
        if (outfile) {
            int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { print_error(); _exit(1); }
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        execv(prog, cargv);
        print_error();
        _exit(1);
    }
    free(prog);
    free(cargv);
    return pid;
}

static void process_line(char *line) {
    char **toks;
    int n = tokenize(line, &toks);
    pid_t pids[MAX_CMDS];
    int np = 0, start = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || strcmp(toks[i], "&") == 0) {
            pid_t pid = run_command(toks, start, i);
            if (pid > 0 && np < MAX_CMDS) pids[np++] = pid;
            start = i + 1;
        }
    }
    for (int i = 0; i < np; i++) waitpid(pids[i], NULL, 0);   /* чекаємо всі паралельні */
    for (int i = 0; i < n; i++) free(toks[i]);
    free(toks);
}

int main(int argc, char *argv[]) {
    FILE *in = stdin;
    int interactive = 1;
    if (argc > 2) { print_error(); exit(1); }
    if (argc == 2) {
        in = fopen(argv[1], "r");
        if (!in) { print_error(); exit(1); }
        interactive = 0;
    }
    char *init[] = { "/bin" };
    set_path(init, 1);

    char *line = NULL;
    size_t cap = 0;
    while (1) {
        if (interactive) { printf("wish> "); fflush(stdout); }
        if (getline(&line, &cap, in) == -1) break;
        process_line(line);
    }
    free(line);
    exit(0);
}
