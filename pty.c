/* http://paste.org.ru/?x9n75j */

// Сделать multicat, а потом wait или как? Хочется, чтобы закрытие fd терминала в ребёнке не приводило к HUP'у

#define _POSIX_C_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>

#include <err.h>
#include <pty.h>
#include <utmp.h>
#include <termios.h>

int master;

bool verbose = false;

FILE *logfp;

void winch_handler(int unused)
{
  siginterrupt (SIGWINCH, 0);
  struct winsize w;
  if (ioctl (0, TIOCGWINSZ, &w) != -1)
    {
      ioctl (master, TIOCSWINSZ, &w);
    }
  signal (SIGWINCH, &winch_handler);
  siginterrupt (SIGWINCH, 0);
}

void chld_handler(int unused)
{
  if (verbose)
    {
      fprintf(logfp, "[CLD] Child terminated\n");
    }

  for (;;){
    char c;
    int read_returned = read(master, &c, 1);

    if (read_returned == -1){
      /* Child closed tty */
      if (verbose){
        fprintf(logfp, "[EOF] Child  stdout -> parent stdout\n");
      }
      break;
    }else if (read_returned == 0)
      {
        errx (EXIT_FAILURE, "parent: cannot read from master: EOF");
      }

    if (c == '\r'){
      if (getenv("TRANSPARENT_PTY") == NULL){
        if (verbose){
          fprintf(logfp, "[^M ] Child  stdout -> /dev/null\n");
        }
        continue;
      }
    }

    if (verbose)
      {
        fprintf(logfp, "[ %c ] Child  stdout -> parent stdout\n", c);
      }

    switch (write(1, &c, 1)){
      case -1:
        err (EXIT_FAILURE, "parent: cannot write to stdout");
      case 0:
        errx (EXIT_FAILURE, "parent: write(1) returned 0");
    }
  }

  int status;

  if (wait(&status) == -1)
    {
      err (EXIT_FAILURE, "parent: wait");
    }

  if (WIFEXITED(status)){
    exit (WEXITSTATUS(status));
  }else if (WIFSIGNALED(status)){
    signal(WTERMSIG(status), SIG_DFL);
    if (kill(getpid(), WTERMSIG(status)) == -1)
      {
        err (EXIT_FAILURE, "parent: I cannot kill myself");
      }
    pause();
    errx (EXIT_FAILURE, "parent: I killed myself, but not dead");
  }else
    {
      errx (EXIT_FAILURE, "parent: child terminated in unusual way");
    }
}

void int_handler(int unused)
{
  write(master, "\3", 1);
  signal(SIGINT, &int_handler);
}

int main(int argc, char *argv[])
{
  --argc;
  ++argv;

  if (argc >= 1 && strcmp(argv[0], "-v") == 0){
    verbose = true;
    logfp = fopen("/tmp/log", "w");

    if (logfp == NULL)
      {
        err (EXIT_FAILURE, "/tmp/log");
      }

    setvbuf (logfp, NULL, _IOLBF, 0);

    --argc;
    ++argv;
  }

  if (argc < 1)
    {
      fprintf(stderr,
        "Usage: pty [-v] COMMAND (-v logs to /tmp/log, set TRANSPARENT_PTY to make pty fully transparent)\n" // и что это значит?
        "Example: { /* Wait for password prompt: */ sleep 1; echo password; /* Wait for shell: */ sleep 3; echo echo hello; /* EOF: */ printf '\\4'; } | pty ssh user@host\n"
      );
      exit (EXIT_FAILURE);
    }

  switch (forkpty (&master, NULL, NULL, NULL))
    {
      case -1:
        err (EXIT_FAILURE, "forkpty failed (is /dev/pts mounted?)");
      case 0:
        execvp(argv[0], argv);
        err (EXIT_FAILURE, "%s", argv[0]);
    }

  struct winsize w;

  if (ioctl (0, TIOCGWINSZ, &w) != -1)
    {
      ioctl (master, TIOCSWINSZ, &w);
    }

  if (signal(SIGCHLD, &chld_handler) == SIG_ERR)
    {
      err (EXIT_FAILURE, "parent: signal");
    }

  if (signal(SIGINT, &int_handler) == SIG_ERR)
    {
      err (EXIT_FAILURE, "parent: signal");
    }

  if (signal(SIGWINCH, &winch_handler) == SIG_ERR)
    {
      err (EXIT_FAILURE, "parent: signal");
    }

  siginterrupt (SIGCHLD, 0);
  siginterrupt (SIGINT, 0);
  siginterrupt (SIGWINCH, 0);

  bool eof_send = false;

  for (;;){
    fd_set set;

    FD_ZERO(&set);
    FD_SET(0, &set);
    FD_SET(master, &set);

    if (select(master + 1, &set, NULL, NULL, NULL) == -1){
      if (errno == EINTR){
        continue;
      }else{
        err (EXIT_FAILURE, "parent: select");
      }
    }

    if (FD_ISSET(0, &set)){
      char c;
      int read_returned = read(0, &c, 1);

      if (read_returned == -1){
        err (EXIT_FAILURE, "parent: cannot read from stdin");
      }else if (read_returned == 0){
        if (verbose && !eof_send){
          fprintf(logfp, "[EOF] Parent stdin  -> child  stdin\n");
          eof_send = true;
        }
      }else{
        if (verbose){
          fprintf(logfp, "[ %c ] Parent stdin  -> child  stdin\n", c);
        }

        switch (write(master, &c, 1)){
          case -1:
            err (EXIT_FAILURE, "parent: cannot write to master");
          case 0:
            errx (EXIT_FAILURE, "parent: write(master) returned 0");
        }
      }
    }

    if (FD_ISSET(master, &set)){
      char c;
      int read_returned = read(master, &c, 1);

      if (read_returned == -1){
        /* Child closed tty */
        if (verbose){
          fprintf(logfp, "[EOF] Child  stdout -> parent stdout\n");
        }
        continue;
      }else if (read_returned == 0){
        errx (EXIT_FAILURE, "parent: cannot read from master: EOF");
      }

      if (c == '\r'){
        if (getenv("TRANSPARENT_PTY") == NULL){
          if (verbose){
            fprintf(logfp, "[^M ] Child  stdout -> /dev/null\n");
          }
          continue;
        }
      }

      if (verbose){
        fprintf(logfp, "[ %c ] Child  stdout -> parent stdout\n", c);
      }

      switch (write(1, &c, 1)){
        case -1:
          err (EXIT_FAILURE, "parent: cannot write to stdout");
        case 0:
          errx (EXIT_FAILURE, "parent: write(1) returned 0");
      }
    }
  }

  /* NOTREACHED */
}
