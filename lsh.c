
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

static char *parse_line(char *buf);
static void prompt(int interactive);
static int do_internal(int argc, char **argv);
static void redirect(char **args);
static void expand(int max_args, char **args);
static int internal_cd(int argc, char **argv);
static int internal_exit(int argc, char **argv);

static int last_exit = 0;
static int do_exit = 0;
static int global_argc;
static char **global_argv;


static struct {
  char *name;
  int (*func)(int, char **);
} internal_cmd[] = {
  { "cd", internal_cd },
  { "exit", internal_exit }
};

int lsh_main(int argc, char **argv)
{
  FILE *f = stdin;
  char buf[256], *s;
  int interactive = 0;

  *buf = 0;

  global_argc = argc - 1;
  global_argv = argv + 1;

  if(isatty(0) && argc == 1) {
    interactive = 1;
    signal(SIGINT, SIG_IGN);
  }

  if(argc > 2 && !strcmp(argv[1], "-c")) {
    f = NULL;
    strncpy(buf, argv[2], sizeof buf);
    buf[sizeof buf - 1] = 0;
    global_argv += 2;
    global_argc -= 2;
  }
  else {
    if(argc > 1) f = fopen(argv[1], "r");
    global_argv++;
    global_argc--;
  }

  if(!f && *buf) {
    s = buf;
    while((s = parse_line(s)));
  }

  if(f) {
    while(prompt(interactive), fgets(buf, sizeof buf, f)) {
      s = buf;
      while((s = parse_line(s)));
    }
  }

  return last_exit;
}

char *parse_line(char *buf)
{
  char c;
  char arg[256];
  char *args[8];
  int arg_cnt, arg_len;
  int nok, is_string;
  char *next = NULL;
  pid_t child;
  int status;

  // printf(">>%s<\n", buf);

  is_string = 0; nok = 1;
  for(*arg = 0, *args = NULL, arg_cnt = 0, arg_len = -1, c = *buf; nok; c = *++buf) {
    if(!c) nok = 0;
    if((!c || c == ';' || isspace(c)) && !is_string) {
      if(arg_len == -1 && c != ';') continue;
      if(arg_len != -1) {
        arg[arg_len] = 0;
        if(arg_cnt < (int) (sizeof args / sizeof *args) - 1) {
          args[arg_cnt++] = strdup(arg);
          args[arg_cnt] = NULL;
        }
        arg_len = -1;
      }
      if(c == ';') {
        next = buf + 1;
        break;
      }
    }
    else {
      if(!is_string) {
        if(c == '"' || c == '\'') {
          is_string = c;
          if(arg_len == -1) arg_len = 0;
          continue;
        }
        if(c == '#') break;
        if(c == '\\' && buf[1]) c = *++buf;
      }
      else {
        if(c == is_string) {
          is_string = 0;
          continue;
        }
      }
      if(arg_len == -1) arg_len = 0;
      if(arg_len < (int) sizeof arg - 1) {
        arg[arg_len++] = c;
      }
    }
  }

#if 0
  {
    int i;

    printf("arg_cnt = %d\n", arg_cnt);

    for(i = 0; args[i]; i++) {
      printf("%2d: >%s<\n", i, args[i]);
    }
  }
#endif

  expand(sizeof args / sizeof *args, args);

  if(*args && **args) {
    if(!do_internal(arg_len, args)) {
      child = fork();
      if(child) {
        if(child != -1) wait(&status);
        if(WIFEXITED(status)) last_exit = WEXITSTATUS(status);
      }
      else {
        redirect(args);
        execvp(*args, args);
        fprintf(stderr, "%s: command not found\n", *args);
        exit(127);
      }
    }
  }

  if(do_exit) return NULL;

  return next;
}

void prompt(interactive)
{
  char buf[1024], *s;

  if(!interactive) return;

  s = getcwd(buf, sizeof buf);
  if(!s) s = "";

  printf("%s>", s);
  fflush(stdout);
}

int do_internal(int argc, char **argv)
{
  int i;

  for(i = 0; (unsigned) i < sizeof internal_cmd / sizeof *internal_cmd; i++) {
    if(!strcmp(*argv, internal_cmd[i].name)) {
      last_exit = internal_cmd[i].func(argc, argv);
      return 1;
    }
  }

  return 0;
}

void redirect(char **args)
{
  char *s;
  int fd, new_fd, io;
  int noclose = 0;

  for(; (s = *args); args++) {
    io = 1;
    fd = -1;
    if(*s == '<') {
      fd = 0;
      io = 0;
      s++;
    }
    else if(*s == '>' && s[1] == '>') {
      fd = 1;
      io = 2;
      s += 2;
    }
    else if(*s == '>') {
      fd = 1;
      s++;
    }
    else if(isdigit(*s) && s[1] == '>') {
      fd = *s - '0';
      s += 2;
    }
    if(fd >= 0) {
      if(io == 1 && *s == '&' && isdigit(s[1]) && !s[2]) {
        new_fd = s[1] - '0';
        noclose = 1;
      }
      else {
        if(io == 2) {
          new_fd = open(s, O_CREAT | O_WRONLY | O_APPEND, 0644);
        }
        else {
          new_fd = io ? open(s, O_CREAT | O_WRONLY | O_TRUNC, 0644) : open(s, O_RDONLY);
        }
      }
      if(new_fd) {
        dup2(new_fd, fd);
        if(!noclose) close(new_fd);
      }
      free(*args);
      *args = NULL;
    }
  }
}

void expand(int max_args, char **args)
{
  int i, j;
  char *s, *t, buf[8];

  for(i = 0; i < max_args ; i++) {
    if(!(s = args[i])) break;
    t = NULL;
    if(*s == '$' && *++s) {
      if(!strcmp(s, "?")) {
        sprintf(buf, "%d", last_exit & 255);
        t = buf;
      }
      else if(isdigit(*s)) {
        j = atoi(s) - 1;
        if(j >= 0 && j < global_argc) {
          t = global_argv[j];
        }
        else {
          *(t = buf) = 0;
        }
      }
      else {
        t = getenv(s);
        if(!t) *(t = buf) = 0;
      }
    }

    if(t) {
      free(args[i]);
      args[i] = strdup(t);
      continue;
    }

  }
}

int internal_cd(int argc, char **argv)
{
  if(!chdir(argv[1] ? argv[1] : "/")) return 0;

  return errno;
}

int internal_exit(int argc, char **argv)
{
  do_exit = 1;
  if(argv[1]) last_exit = atoi(argv[1]);

  return last_exit;
}

