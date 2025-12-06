#ifndef SMASH__SIGNALS_H_
#define SMASH__SIGNALS_H_

void ctrlCHandler(int sig_num);
void setForegroundPid(pid_t pid);
pid_t getForegroundPid();
#endif //SMASH__SIGNALS_H_
