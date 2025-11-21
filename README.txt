The given skeleton includes the following files:

- Commands.h/Commands.cpp: The supported commands of smash. Each command is represented by a class that inherits from either BuiltInCommand or ExternalCommand.
  Each command that you add should implement execute, which is a virtual method that executes the command.

- signals.h/signals.cpp: Declares and implements the required signal handler — the SIGINT handler to handle Ctrl+C.

- smash.cpp: Contains the smash main, which runs an infinite loop that receives the next typed command and sends it to SmallShell::executeCommand to handle it.

- Makefile: Builds and tests your smash. You can use "make submit" to prepare a zip file for submission; this is recommended, as it ensures you follow our submission structure.

- *.txt: Basic test files that are used by the given Makefile to run basic tests on your smash implementation.


Our solution and the skeleton code also use a few known design patterns to make the code modular and readable.
We mainly use two design patterns: Singleton and Factory Method.

There are many resources on the internet explaining these design patterns; they are sometimes known as the GoF (Gang of Four) design patterns.
We recommend you do a quick review of these two design patterns for a better understanding of the skeleton.


How to start:

First, you have to understand the skeleton design. The given skeleton works as follows:
- In smash.cpp, the main function runs an infinite loop that reads the next typed command.
- After reading the next command, it calls SmallShell::executeCommand.
- SmallShell::executeCommand should create the relevant command class using the factory method CreateCommand.
- After instantiating the relevant Command class, you have to:
    - Fork if needed.
    - Call setpgrp from the child process.
    - Run the created-command execute method (from the child process or parent process?)
    - Should the parent wait for the child? If yes, then how? Using wait or waitpid?


To implement new commands, you need to:
- Implement the new command class in Commands.cpp.
- Add any private data fields in the created class and initialize them in the constructor.
- Implement the new command execute method.
- Add an if statement to handle it in SmallShell::CreateCommand.


We recommend that you start your implementation with:
- The simple built-in commands (e.g., chprompt/pwd/showpid/...). After making sure that they work fine with no bugs, move forward.
- Implement the rest of the built-in commands.
- Implement the external commands.
- Implement the execution of external commands in the background.
- Implement the jobs list and all relevant commands (fg/jobs/...).
- Implement the I/O redirection.
- Finally, implement the rest of commands, followed by the bonus part and pipes.


Good luck :)
