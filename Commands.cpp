#include <unistd.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <sys/wait.h>
#include <iomanip>
#include "Commands.h"

using namespace std;

const std::string WHITESPACE = " \n\r\t\f\v";

#if 0
#define FUNC_ENTRY()  \
  cout << __PRETTY_FUNCTION__ << " --> " << endl;

#define FUNC_EXIT()  \
  cout << __PRETTY_FUNCTION__ << " <-- " << endl;
#else
#define FUNC_ENTRY()
#define FUNC_EXIT()
#endif


//start of: parsing functions --------------------------------------------------------------------

string _ltrim(const std::string &s) {
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == std::string::npos) ? "" : s.substr(start);
}

string _rtrim(const std::string &s) {
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

string _trim(const std::string &s) {
    return _rtrim(_ltrim(s));
}

int _parseCommandLine(const char *cmd_line, char **args) {
    FUNC_ENTRY()
    int i = 0;
    std::istringstream iss(_trim(string(cmd_line)).c_str());
    for (std::string s; iss >> s;) {
        args[i] = (char *) malloc(s.length() + 1);
        memset(args[i], 0, s.length() + 1);
        strcpy(args[i], s.c_str());
        args[++i] = NULL;
    }
    return i;
    FUNC_EXIT()
}

bool _isBackgroundComamnd(const char *cmd_line) {
    const string str(cmd_line);
    return str[str.find_last_not_of(WHITESPACE)] == '&';
}

void _removeBackgroundSign(char *cmd_line) {
    const string str(cmd_line);
    // find last character other than spaces
    unsigned int idx = str.find_last_not_of(WHITESPACE);
    // if all characters are spaces then return
    if (idx == string::npos) {
        return;
    }
    // if the command line does not end with & then return
    if (cmd_line[idx] != '&') {
        return;
    }
    // replace the & (background sign) with space and then remove all tailing spaces.
    cmd_line[idx] = ' ';
    // truncate the command line string up to the last non-space character
    cmd_line[str.find_last_not_of(WHITESPACE, idx) + 1] = 0;
}

// end of: parsing functions --------------------------------------------------------------------






// start of smallShell class --------------------------------------------------------------------

SmallShell::SmallShell(): prompt("smash"), last_dir("") {
}

SmallShell::~SmallShell() {}

BuiltInCommand::BuiltInCommand(const char *cmd_line): Command(cmd_line) {}

Command::Command(const char *cmd_line): cmd_line(cmd_line) {}


/**
* Creates and returns a pointer to Command class which matches the given command line (cmd_line)
*/
Command *SmallShell::CreateCommand(const char *cmd_line) {

    string cmd_s = _trim(string(cmd_line));
    string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));


    if (firstWord.compare("pwd") == 0) {
      return new GetCurrDirCommand(cmd_line);
    }
    else if (firstWord.compare("showpid") == 0) {
      return new ShowPidCommand(cmd_line);
    }
    else if (firstWord.compare("chprompt") == 0) {
        char **args = new char *[COMMAND_MAX_ARGS+1];
        _parseCommandLine(cmd_line, args);
        if (args[1] == NULL) return new ChangePromptCommand(cmd_line, "");
        std::string prompt = args[1];
        delete[] args;
        return new ChangePromptCommand(cmd_line, string(prompt));
    }
    else if (firstWord.compare("cd") == 0) {
        return new ChangeDirCommand(cmd_line);
    }
    else if (firstWord.compare("jobs") == 0) {
        return new JobsCommand(cmd_line, job_list);
    }
    else if (firstWord.compare("fg") == 0) {
        return new ForegroundCommand(cmd_line, job_list);
    }
    else if (firstWord.compare("quit") == 0) {
        return new QuitCommand(cmd_line, job_list);
    }
    else if (firstWord.compare("kill") == 0) {
        return new KillCommand(cmd_line, job_list);
    }

    return nullptr;
}



void SmallShell::executeCommand(const char *cmd_line) {

    Command* cmd = CreateCommand(cmd_line);
    cmd->execute();
}


std::string SmallShell::getPrompt() const {
    return prompt;
}


void SmallShell::setPrompt(const std::string &prompt) {
    this->prompt = prompt;
}


std::string SmallShell::getLastDir() const {
    return last_dir;
}


void SmallShell::setLastDir(const std::string &last_dir) {
    this->last_dir = last_dir;
}

// end of smallShell class --------------------------------------------------------------------






// start of commands --------------------------------------------------------------------


ChangePromptCommand::ChangePromptCommand(const char *cmd_line, const std::string &prompt): BuiltInCommand(cmd_line),
prompt(prompt) {}



void ChangePromptCommand::execute() {
    if (prompt.empty()) SmallShell::getInstance().setPrompt("smash");
    else SmallShell::getInstance().setPrompt(prompt);
}



GetCurrDirCommand::GetCurrDirCommand(const char *cmd_line): BuiltInCommand(cmd_line) {}

void GetCurrDirCommand::execute() {
    char* buffer = getcwd(NULL, 0);
    if (!buffer) {
        perror("smash error: getcwd failed");
        return;
    }
    std::cout << buffer << "\n";
    free(buffer);
}



ShowPidCommand::ShowPidCommand(const char *cmd_line): BuiltInCommand(cmd_line){}

void ShowPidCommand::execute() {
    std::cout << "smash pid is " << getpid() << '\n';
}



ChangeDirCommand::ChangeDirCommand(const char *cmd_line): BuiltInCommand(cmd_line) {}

void ChangeDirCommand::execute() {
    SmallShell &smash = SmallShell::getInstance();

    char *args[100];
    int argc = _parseCommandLine(cmd_line, args);

    // cd with no arguments -> no impact
    if (argc == 1) {
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }

    // more than one argument -> error
    if (argc > 2) {
        std::cerr << "smash error: cd: too many arguments" << std::endl;
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }

    // save current directory before changing
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        perror("smash error: getcwd failed");
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }

    const char *target = nullptr;

    if (strcmp(args[1], "-") == 0) {
        const std::string &last = smash.getLastDir();
        if (last.empty()) {
            std::cerr << "smash error: cd: OLDPWD not set" << std::endl;
            for (int i = 0; i < argc; ++i) {
                free(args[i]);
            }
            return;
        }
        target = last.c_str();
    } else {
        target = args[1];
    }
    if (chdir(target) == -1) {
        perror("smash error: chdir failed");
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }

    smash.setLastDir(std::string(cwd));

    for (int i = 0; i < argc; ++i) {
        free(args[i]);
    }
}
JobsList::JobsList() : max_job_id(0) {}
JobsList::~JobsList() {
    for (auto job:jobs) {
        delete job;
    }
    jobs.clear();
}
void JobsList::addJob(Command *cmd, bool is_stopped) {
    removeFinishedJobs();
    int new_job_id = ++max_job_id;
    jobs.push_back(new JobEntry(new_job_id, cmd->getPID(), is_stopped, cmd->getCMD()));
}
void JobsList::printJobsList() {
    removeFinishedJobs();
    //Jobs should already be sorted but it should be further checked
    for (auto job: jobs) {
        std::cout<< "[" << job->job_id << "] " << job->cmd_line << std::endl;
    }
}
void JobsList::killAllJobs() {
    removeFinishedJobs();
    for (auto job: jobs) {
        //find why this can't resolve kill
        if (kill(job->pid, SIGKILL) ==0) {
            std::cout << job->pid << ": " << job->cmd_line << std::endl;
        }
        else {
            perror("smash error: kill failed");
        }
    }
}
void JobsList::removeFinishedJobs() {
    auto it = jobs.begin();
    while (it != jobs.end()) {
        int status;
        pid_t result = waitpid((*it)->pid, &status, WHOHANG);
        if (result > 0) {
            delete *it;
            it = jobs.erase(it);
        }
        else {
            it++;
        }
    }
}
JobsList::JobEntry *JobsList::getJobById(int jobId) {
    for (auto job:jobs) {
        if (job->job_id == jobId) {
            return job;
        }
    }
    return nullptr;
}
void JobsList::removeJobById(int jobId) {
    for (auto it = jobs.begin(); it != jobs.end(); it++) {
        if ((*it)->job_id == jobId) {
            delete *it;
            jobs.erase(it);
            return;
        }
    }
}
JobsList::JobEntry *JobsList::getLastJob(int *lastJobId) {
    removeFinishedJobs();
    if(jobs.empty()) {
        return nullptr;
    }
    if (lastJobId) {
        *lastJobId = jobs.back()->job_id;
    }
    return jobs.back();
}
JobsList::JobEntry *JobsList::getLastStoppedJob(int *jobId) {
    removeFinishedJobs();
    JobEntry* last_stopped = nullptr;
    for (auto job : jobs) {
        if (job->is_stopped) {
            last_stopped = job;
        }
    }
    if (jobId) {
        if (!last_stopped) {
            *jobId = 0;
            return last_stopped;
        }
        *jobId = last_stopped->job_id;
    }
    return last_stopped;
}
JobsCommand::JobsCommand(const char *cmd_line, JobsList *jobs) : BuiltInCommand(cmd_line), jobs(jobs) {}
void JobsCommand::execute() {
    jobs->printJobsList();
}

ForegroundCommand::ForegroundCommand(const char *cmd_line, JobsList *jobs) : BuiltInCommand(cmd_line), jobs(jobs) {}

void ForegroundCommand::execute() {
    jobs-> removeFinishedJobs();
    char *args[COMMAND_MAX_ARGS+1];
    int argc = _parseCommandLine(cmd_line, args);
    int job_id = 0;
    JobsList::JobEntry *job = nullptr;
    if (argc == 1) {
        if (jobs->isEmpty()) {
            perror("smash error: fg: jobs list is empty");
            for (int i = 0; i < argc; ++i) {
                free(args[i]);
            }
            return;
        }
        job = jobs->getLastJob(&job_id);
    }
    else if (argc == 2) {
        char* endptr;
        job_id = strtol(args[1], &endptr, 10);
        if (endptr != "\0"||job_id <=0) {
            perror("smash error: fg: invalid arguments");
            for (int i = 0; i < argc; ++i) {
                free(args[i]);
            }
            return;
        }
        JobsList::JobEntry* job = jobs->getJobById(job_id);
        if (!job) {
            std::string error = "smash error: fg: jobs-id ";
            error += std::to_string(job_id);
            error += " does not exist";
            perror (error.c_str());
            for (int i = 0; i < argc; ++i) {
                free(args[i]);
            }
            return;
        }
    }
    else {
        perror("smash error: fg: invalid arguments");
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }
    std::cout << job->cmd_line << " " << job->pid << std::endl;
    int status;
    waitpid(job->pid, &pid); // Maybe should add WUNTRACED flag
    for (int i = 0; i < argc; ++i) {
        free(args[i]);
    }
}

QuitCommand::QuitCommand(const char *cmd_line, JobsList *jobs) : BuiltInCommand(cmd_line), jobs(jobs) {}
void QuitCommand::execute() {
    char *args[COMMAND_MAX_ARGS+1];
    int argc = _parseCommandLine(cmd_line, args);
    bool kill = false;
    if (argc >1 && strcmp(args[1], "kill") == 0) {
        kill = true;
    }
    if (kill) {
        int job_count = jobs->getJobCount();
        std::cout << "smash: sending SIGKILL signal to " << job_count <<" jobs"<< std::endl;
        jobs->killAllJobs();
    }
    for (int i = 0; i < argc; ++i) {
        free(args[i]);
    }
    exit(0);
}

KillCommand::KillCommand(const char *cmd_line, JobsList *jobs) : BuiltInCommand(cmd_line), jobs(jobs) {}
void KillCommand::execute() {
    char *args[COMMAND_MAX_ARGS+1];
    int argc = _parseCommandLine(cmd_line, args);
    if (argc != 3) {
        perror("smash error: kill: invalid arguments");
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }
    if (args[1][0] != '-') {
        perror("smash error: kill: invalid arguments");
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }
    char* endptr;
    int signum = strtol(args[1]+1, &endptr, 10);
    if (endptr != "\0"||signum <=0) {
        perror("smash error: kill: invalid arguments");
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }
    int job_id = strtol(args[2], &endptr, 10);
    if (endptr != "\0"||job_id <=0) {
        perror("smash error: kill: invalid arguments");
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }
    JobsList::JobEntry* job = jobs->getJobById(job_id);
    if (!job) {
        std::string error = "smash error: kill: jobs-id ";
        error += std::to_string(job_id);
        error += " does not exist";
        perror (error.c_str());
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }
    //here maybe a check of was it successful is necessary
    kill(job->pid, signum);
    std::cout << "signal number " << signum << " was sent to pid " << job->pid << std::endl;
    for (int i = 0; i < argc; ++i) {
        free(args[i]);
    }
}





