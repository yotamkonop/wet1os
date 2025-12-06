#include <iostream>
#include <signal.h>
#include "signals.h"
#include "Commands.h"

using namespace std;
pid_t foreground_pid = 0;
void ctrlCHandler(int sig_num) {
    cout << "smash: got ctrl-C" << endl;
    if (getForegroundPid() > 0 ) {
        if (kill(getForegroundPid(), SIGKILL) == 0) {
            cout << "smash: process " << getForegroundPid() << " was killed"<< endl;
        }
        foreground_pid = 0;
    }
}
void setForegroundPid(pid_t pid) {
    foreground_pid = pid;
}
pid_t getForegroundPid() {
    return foreground_pid;
}


