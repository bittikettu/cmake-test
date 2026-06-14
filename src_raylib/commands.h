// commands.h -- the shell's only export. main.c's input loop hands a finished
// command line to run_command; everything else (the cmd_* verbs) is private to
// commands.c.
#ifndef COMMANDS_H
#define COMMANDS_H

void run_command(char *cmdline);

#endif // COMMANDS_H
