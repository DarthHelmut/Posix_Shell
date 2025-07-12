#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/wait.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_INPUT 256
#define MAX_ARGS 32
#define MAX_CMDS 8
#define MAX_JOBS 16

int execute_control_block(const char* input);

typedef struct {
    pid_t pid;
    char cmd[MAX_INPUT];
    int active;
} Job;

Job jobs[MAX_JOBS];
int job_count = 0;
int last_status = 0;

char* safe_strdup(const char* src) {
    if (!src) return NULL;
    char* copy = strdup(src);
    if (!copy) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    return copy;
}

void safe_free(char** ptr) {
    if (*ptr) {
        free(*ptr);
        *ptr = NULL;
    }
}

void add_job(pid_t pid, const char* cmd) {
    if (job_count < MAX_JOBS) {
        jobs[job_count].pid = pid;
        strncpy(jobs[job_count].cmd, cmd, MAX_INPUT - 1);
        jobs[job_count].cmd[MAX_INPUT - 1] = '\0';
        jobs[job_count].active = 1;
        job_count++;
    }
}

void list_jobs() {
    for (int i = 0; i < job_count; i++)
        if (jobs[i].active)
            printf("[%d] %d Running    %s\n", i + 1, jobs[i].pid, jobs[i].cmd);
}

void bring_fg(int index) {
    if (index < 1 || index > job_count || !jobs[index - 1].active) {
        printf("fg: no such job\n");
        return;
    }
    pid_t pid = jobs[index - 1].pid;
    tcsetpgrp(STDIN_FILENO, pid);
    kill(pid, SIGCONT);
    waitpid(pid, NULL, WUNTRACED);
    tcsetpgrp(STDIN_FILENO, getpid());
    jobs[index - 1].active = 0;
}

void sigchld_handler(int signo) {
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
        for (int i = 0; i < job_count; i++)
            if (jobs[i].pid == pid)
                jobs[i].active = 0;
}

void sigint_handler(int signo) {
    printf("\nCaught Ctrl+C — shell remains active\n");
}

void sigtstp_handler(int signo) {
    printf("\nCaught Ctrl+Z — shell will not suspend\n");
}

char* expand_variables(const char* input) {
    char result[MAX_INPUT] = {0};
    int i = 0, j = 0;
    while (input[i] && j < MAX_INPUT - 1) {
        if (input[i] == '$') {
            i++;
            char var[64] = {0};
            int k = 0;
            while ((isalnum(input[i]) || input[i] == '_') && k < 63)
                var[k++] = input[i++];
            var[k] = '\0';
            if (strcmp(var, "?") == 0) {
                j += snprintf(result + j, MAX_INPUT - j, "%d", last_status);
            } else {
                const char* val = getenv(var);
                if (val && j + strlen(val) < MAX_INPUT - 1) {
                    strncat(result, val, MAX_INPUT - j - 1);
                    j += strlen(val);
                }
            }
        } else {
            result[j++] = input[i++];
        }
    }
    result[j] = '\0';
    return safe_strdup(result);
}

int split_logic(char* input, char* segments[MAX_CMDS]) {
    int count = 0;
    char* token = strtok(input, "&&");
    while (token && count < MAX_CMDS) {
        while (*token == ' ') token++;
        segments[count++] = token;
        token = strtok(NULL, "&&");
    }
    return count;
}

int split_pipeline(char* input, char* cmds[MAX_CMDS]) {
    int count = 0;
    char* token = strtok(input, "|");
    while (token && count < MAX_CMDS) {
        while (*token == ' ') token++;
        cmds[count++] = token;
        token = strtok(NULL, "|");
    }
    return count;
}

void parse_command(char* input, char** args, char** in_file, char** out_file, int* background) {
    int i = 0, j = 0;
    char arg[MAX_INPUT] = {0};
    int in_single = 0, in_double = 0;
    while (*input) {
        if (*input == '\'' && !in_double) in_single = !in_single;
        else if (*input == '"' && !in_single) in_double = !in_double;
        else if ((*input == ' ' || *input == '\t') && !in_single && !in_double) {
            if (j > 0) {
                arg[j] = '\0';
                if (strcmp(arg, "<") == 0 || strcmp(arg, ">") == 0) {
                    int is_out = (arg[0] == '>');
                    input++; while (*input == ' ') input++;
                    j = 0; while (*input && *input != ' ' && j < MAX_INPUT - 1)
                        arg[j++] = *input++;
                    arg[j] = '\0';
                    if (is_out) *out_file = safe_strdup(arg);
                    else *in_file = safe_strdup(arg);
                    memset(arg, 0, sizeof(arg)); j = 0; continue;
                }
                args[i++] = safe_strdup(arg);
                j = 0; memset(arg, 0, sizeof(arg));
            }
        } else {
            if (*input == '&' && !in_single && !in_double)
                *background = 1;
            else if (j < MAX_INPUT - 1)
                arg[j++] = *input;
        }
        input++;
    }
    if (j > 0) {
        arg[j] = '\0';
        args[i++] = safe_strdup(arg);
    }
    args[i] = NULL;
}

int execute_segment(char* segment) {
    char* cmds[MAX_CMDS];
    int cmd_count = split_pipeline(segment, cmds);
    int prev_fd = -1;
    for (int i = 0; i < cmd_count; i++) {
        char* args[MAX_ARGS] = {0};
        char* in_file = NULL;
        char* out_file = NULL;
        int background = 0;
        parse_command(cmds[i], args, &in_file, &out_file, &background);
        if (!args[0]) continue;
        if (strcmp(args[0], "exit") == 0) exit(0);
        if (strcmp(args[0], "cd") == 0) {
            const char* target = args[1] ? args[1] : getenv("HOME");
            if (chdir(target) != 0) perror("cd");
            continue;
        }

        int pipe_fd[2]; if (i < cmd_count - 1) pipe(pipe_fd);
        pid_t pid = fork();
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            if (prev_fd != -1) { dup2(prev_fd, STDIN_FILENO); close(prev_fd); }
            if (i < cmd_count - 1) { close(pipe_fd[0]); dup2(pipe_fd[1], STDOUT_FILENO); close(pipe_fd[1]); }
            if (in_file) { int fd = open(in_file, O_RDONLY); if (fd < 0) { perror("input"); exit(1); } dup2(fd, STDIN_FILENO); close(fd); }
            if (out_file) { int fd = open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644); if (fd < 0) { perror("output"); exit(1); } dup2(fd, STDOUT_FILENO); close(fd); }
            if (background) setpgid(0, 0);
            execvp(args[0], args); perror("exec"); exit(1);
        } else {
            if (background) add_job(pid, cmds[i]);
            else {
                if (prev_fd != -1) close(prev_fd);
                if (i < cmd_count - 1) { close(pipe_fd[1]); prev_fd = pipe_fd[0]; }
                int status; waitpid(pid, &status, 0); last_status = WEXITSTATUS(status);
            }
            for (int k = 0; args[k]; k++) safe_free(&args[k]);
            safe_free(&in_file);
            safe_free(&out_file);
        }
    }
    return last_status;
}

int main() {
    signal(SIGCHLD, sigchld_handler);    // Reap background jobs
    signal(SIGINT, sigint_handler);      // Ignore Ctrl+C in shell
    signal(SIGTSTP, sigtstp_handler);    // Ignore Ctrl+Z in shell

    char cwd[128];
    char* input;

    while (1) {
        getcwd(cwd, sizeof(cwd));
        char prompt[160];



char* user = getenv("USER");
char hostname[64];
gethostname(hostname, sizeof(hostname));
snprintf(prompt, sizeof(prompt), " \033[1;34m[%s]\033[0m \033[1;35m(%s)\033[0m $ ", user, hostname, cwd);




        input = readline(prompt);
        if (!input) break;
        if (strlen(input) == 0) {
            free(input);
            continue;
        }

        add_history(input);

        // Expand variables safely into new buffer
        char* expanded = expand_variables(input);
        free(input);
        input = expanded;


	// Control block check
	if (execute_control_block(input) == 0) {
 	free(input);
    	continue;
	}


        char* segments[MAX_CMDS];
        int seg_count = split_logic(input, segments);
        for (int i = 0; i < seg_count; i++) {
            if (execute_segment(segments[i]) != 0) break;
        }

        free(input);
    }

    return 0;
}

int execute_control_block(const char* input) {
    if (strncmp(input, "if ", 3) == 0) {
        // Very simple tokenizer (for now)
        char condition[MAX_INPUT] = {0};
        char then_block[MAX_INPUT] = {0};
        char else_block[MAX_INPUT] = {0};

        const char* then_ptr = strstr(input, "then");
        const char* else_ptr = strstr(input, "else");
        const char* fi_ptr   = strstr(input, "fi");

        if (!then_ptr || !fi_ptr) {
            fprintf(stderr, "Syntax error: missing 'then' or 'fi'\n");
            return 1;
        }

        strncpy(condition, input + 3, then_ptr - (input + 3));
        condition[then_ptr - (input + 3)] = '\0';

        if (else_ptr) {
            strncpy(then_block, then_ptr + 4, else_ptr - (then_ptr + 4));
            then_block[else_ptr - (then_ptr + 4)] = '\0';
            strncpy(else_block, else_ptr + 4, fi_ptr - (else_ptr + 4));
            else_block[fi_ptr - (else_ptr + 4)] = '\0';
        } else {
            strncpy(then_block, then_ptr + 4, fi_ptr - (then_ptr + 4));
            then_block[fi_ptr - (then_ptr + 4)] = '\0';
        }

        // Trim and execute condition
        char* cond_trim = expand_variables(condition);
        int status = execute_segment(cond_trim);
        free(cond_trim);

        if (status == 0) {
            char* then_trim = expand_variables(then_block);
            execute_segment(then_trim);
            free(then_trim);
        } else if (else_ptr) {
            char* else_trim = expand_variables(else_block);
            execute_segment(else_trim);
            free(else_trim);
        }
        return 0;
    }
    return -1; // Not a control block
}

