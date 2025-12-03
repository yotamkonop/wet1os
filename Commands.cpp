#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <sys/wait.h>
#include <iomanip>
#include "Commands.h"
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <limits.h>
#include <ctype.h>
#include <algorithm>

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


//start of: helper functions --------------------------------------------------------------------





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


static bool read_first_line(const std::string& path, std::string& out) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        return false;
    }

    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) {
        return false;
    }

    buf[n] = '\0';
    std::string s(buf);

    // cut at newline
    size_t pos = s.find_first_of("\r\n");
    if (pos != std::string::npos) {
        s = s.substr(0, pos);
    }

    out = _trim(s);
    return !out.empty();
}


static bool list_dir_entries(const std::string& path,
                             std::vector<std::string>& names) {
    int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd == -1) {
        return false;
    }

    struct linux_dirent64 {
        ino64_t        d_ino;
        off64_t        d_off;
        unsigned short d_reclen;
        unsigned char  d_type;
        char           d_name[];
    };

    const int BUF_SIZE = 4096;
    char buf[BUF_SIZE];

    for (;;) {
        int nread = syscall(SYS_getdents64, fd, buf, BUF_SIZE);
        if (nread == -1) {
            close(fd);
            return false;
        }
        if (nread == 0) {
            break; // EOF
        }

        int bpos = 0;
        while (bpos < nread) {
            struct linux_dirent64* d =
                (struct linux_dirent64*)(buf + bpos);
            std::string name = d->d_name;

            if (name != "." && name != "..") {
                names.push_back(name);
            }
            bpos += d->d_reclen;
        }
    }

    close(fd);
    return true;
}

static int du_recursive(const std::string& path, unsigned long long& total_bytes) {
    struct stat st{};
    if (lstat(path.c_str(), &st) == -1) {
        perror("smash error: du: lstat failed");
        return -1;
    }

    // Do NOT follow symlinks – ignore them entirely
    if (S_ISLNK(st.st_mode)) {
        return 0;
    }

    // Count disk usage using st_blocks (512-byte units)
    total_bytes += static_cast<unsigned long long>(st.st_blocks) * 512ULL;

    if (!S_ISDIR(st.st_mode)) {
        return 0;
    }

    std::vector<std::string> children;
    if (!list_dir_entries(path, children)) {
        perror("smash error: du: read directory failed");
        return -1;
    }

    for (const auto& name : children) {
        std::string child = path;
        if (!child.empty() && child.back() != '/') {
            child += "/";
        }
        child += name;

        du_recursive(child, total_bytes); // ignore individual errors
    }

    return 0;
}

// end of: parsing functions --------------------------------------------------------------------






// start of smallShell class --------------------------------------------------------------------

SmallShell::SmallShell(): prompt("smash"), last_dir("") {

    alias_map = new AliasMap();
    job_list = new JobsList();
}

SmallShell::~SmallShell() {
    delete alias_map;
    delete job_list;
}

BuiltInCommand::BuiltInCommand(const char *cmd_line): Command(cmd_line) {}

Command::Command(const char *cmd_line): cmd_line(nullptr) {
    this->cmd_line = strdup(cmd_line);
}


/**
* Creates and returns a pointer to Command class which matches the given command line (cmd_line)
*/
Command *SmallShell::CreateCommand(const char *cmd_line) {
    std::string new_cmd_line = alias_map->replaceAlias(cmd_line);
    string cmd_s = _trim(new_cmd_line);
    string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));


    bool isRedirect = (cmd_s.find('>') != string::npos);
    if (isRedirect) {
        return new RedirectionCommand(cmd_s.c_str());
    }

    bool isPipe = (cmd_s.find('|') != string::npos);
    if (isPipe) {
        return new PipeCommand(cmd_s.c_str());
    }


    if (firstWord.compare("pwd") == 0) {
      return new GetCurrDirCommand(cmd_s.c_str());
    }
    else if (firstWord.compare("showpid") == 0) {
      return new ShowPidCommand(cmd_s.c_str());
    }
    else if (firstWord.compare("chprompt") == 0) {
        return new ChangePromptCommand(cmd_s.c_str());
    }
    else if (firstWord.compare("cd") == 0) {
        return new ChangeDirCommand(cmd_s.c_str());
    }
    else if (firstWord.compare("jobs") == 0) {
        return new JobsCommand(cmd_s.c_str(), job_list);
    }
    else if (firstWord.compare("fg") == 0) {
        return new ForegroundCommand(cmd_s.c_str(), job_list);
    }
    else if (firstWord.compare("quit") == 0) {
        return new QuitCommand(cmd_s.c_str(), job_list);
    }
    else if (firstWord.compare("kill") == 0) {
        return new KillCommand(cmd_s.c_str(), job_list);
    }
    else if (firstWord.compare("alias") == 0) {
        return new AliasCommand(cmd_s.c_str(), alias_map);
    }
    else if (firstWord.compare("unalias") == 0) {
        return new UnAliasCommand(cmd_s.c_str(), alias_map);
    }
    else if (firstWord.compare("unsetenv") == 0) {
        return new UnSetEnvCommand(cmd_s.c_str());
    }
    else if (firstWord.compare("sysinfo") == 0) {
        return new SysInfoCommand(cmd_s.c_str());
    }
    else if (firstWord.compare("du") == 0) {
        return new DiskUsageCommand(cmd_s.c_str());
    }
    else if (firstWord.compare("whoami") == 0) {
        return new WhoAmICommand(cmd_s.c_str());
    }
    else if (firstWord.compare("usbinfo") == 0) {
        return new USBInfoCommand(cmd_s.c_str());
    }
    else {
        return new ExternalCommand(cmd_s.c_str());
    }
}

JobsList *SmallShell::getJobsList() const{
    return job_list;
}


void SmallShell::executeCommand(const char *cmd_line) {

    Command* cmd = CreateCommand(cmd_line);

    cmd->setPID(getpid());
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


Command::~Command() {
    free((void *)cmd_line);
}

PipeCommand::PipeCommand(const char *cmd_line): Command(cmd_line){}


void PipeCommand::execute() {
    std::string line(cmd_line);
    size_t pos = line.find('|');
    std::string left  = _trim(line.substr(0, pos));
    std::string right = _trim(line.substr(pos + 1));

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("smash error: pipe failed");
        return;
    }

    pid_t left_pid = fork();
    if (left_pid == -1) {
        perror("smash error: fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }

    if (left_pid == 0) {

        setpgrp();
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            perror("smash error: dup2 failed");
            exit(1);
        }
        close(pipefd[0]);
        close(pipefd[1]);


        char *args[COMMAND_MAX_ARGS + 1];
        int argc = _parseCommandLine(left.c_str(), args);
        execvp(args[0], args);
        perror("smash error: execvp failed");
        exit(1);
    }

    pid_t right_pid = fork();
    if (right_pid == -1) {
        perror("smash error: fork failed");

        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }

    if (right_pid == 0) {

        setpgrp();
        if (dup2(pipefd[0], STDIN_FILENO) == -1) {
            perror("smash error: dup2 failed");
            exit(1);
        }
        close(pipefd[1]);
        close(pipefd[0]);

        char *args[COMMAND_MAX_ARGS + 1];
        int argc = _parseCommandLine(right.c_str(), args);
        execvp(args[0], args);
        perror("smash error: execvp failed");
        exit(1);
    }


    close(pipefd[0]);
    close(pipefd[1]);


    int status;
    waitpid(left_pid, &status, 0);
    waitpid(right_pid, &status, 0);
}



RedirectionCommand::RedirectionCommand(const char *cmd_line): Command(cmd_line) {}

void RedirectionCommand::execute() {
    std::string cmd_s = cmd_line;
    if (cmd_s.back() == '&') {
        cmd_s.pop_back();
    }
    cmd_s = _trim(cmd_s);
    char *args[COMMAND_MAX_ARGS + 1];
    int argc = _parseCommandLine(cmd_s.c_str(), args);
    int flag = 0;
    if (argc < 3) {
        for (int i = 0; i < argc; i++) {
            free(args[i]);
        }
        return;
    }
    else if (std::string(args[argc - 2]) == ">") {
        flag = O_TRUNC;
    }
    else if (std::string(args[argc - 2]) == ">>") {
        flag = O_APPEND;
    }
    else {
        for (int i = 0; i < argc; i++) {
            free(args[i]);
        }
        return;
    }
    int fd = open(args[argc - 1], O_WRONLY | O_CREAT | flag, 0666);
    if (fd == -1) {
        perror("smash error: open failed");
        for (int i = 0; i < argc; i++) {
            free(args[i]);
        }
        return;
    }
    int saved_stdout = dup(1);
    if (saved_stdout == -1) {
        perror("smash error: dup failed");
        close(fd);
        for (int i = 0; i < argc; i++) {
            free(args[i]);
        }
        return;
    }
    if (dup2(fd, STDOUT_FILENO) == -1) {
        perror("smash error: dup2 failed");
        close(fd);
        for (int i = 0; i < argc; i++) {
            free(args[i]);
        }
        return;
    }
    close(fd);
    for (int i = 0; i < argc; i++) {
        free(args[i]);
    }
    cmd_s = cmd_s.substr(0, cmd_s.find_first_of('>'));
    cmd_s = _trim(cmd_s);
    Command *cmd = SmallShell::getInstance().CreateCommand(cmd_s.c_str());
    cmd->execute();
    dup2(saved_stdout, 1);
}



ChangePromptCommand::ChangePromptCommand(const char *cmd_line): BuiltInCommand(cmd_line){}



void ChangePromptCommand::execute() {
    char *args[COMMAND_MAX_ARGS+1];
    int argc = _parseCommandLine(cmd_line, args);
    if (argc <= 1) SmallShell::getInstance().setPrompt("smash");
    else {
        std::string prompt = args[1];
        SmallShell::getInstance().setPrompt(prompt);
    }
    for (int i = 0; i < argc; i++) {
        free(args[i]);
    }
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

    char *args[COMMAND_MAX_ARGS + 1];
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
    jobs.push_back(new JobEntry(new_job_id, cmd->getPID(), cmd->getCMD(), is_stopped));
}
void JobsList::printJobsList() {
    removeFinishedJobs();
    //Jobs should already be sorted, but it should be further checked
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
        pid_t result = waitpid((*it)->pid, &status, WNOHANG);
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
        if (*endptr != '\0'||job_id <=0) {
            perror("smash error: fg: invalid arguments");
            for (int i = 0; i < argc; ++i) {
                free(args[i]);
            }
            return;
        }
        job = jobs->getJobById(job_id);
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
    waitpid(job->pid, &status, 0); // Maybe should add WUNTRACED flag
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
    if (*endptr != '\0'||signum <=0) {
        perror("smash error: kill: invalid arguments");
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }
    int job_id = strtol(args[2], &endptr, 10);
    if (*endptr != '\0'||job_id <=0) {
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


UnSetEnvCommand::UnSetEnvCommand(const char *cmd_line): BuiltInCommand(cmd_line) {}

void UnSetEnvCommand::execute() {
    char *args[COMMAND_MAX_ARGS + 1];
    int argc = _parseCommandLine(cmd_line, args);
    if (argc <= 1) {
        perror("smash error: unsetenv: not enough arguments");
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }
    pid_t pid = getpid();
    std::string path = "/proc/" + std::to_string(pid) + "/environ";

    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        perror("smash error: open failed");
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }

    // read file in chunks
    std::vector<char> buffer;
    char temp[4096];

    ssize_t bytes_read;
    while ((bytes_read = read(fd, temp, sizeof(temp))) > 0) {
        buffer.insert(buffer.end(), temp, temp + bytes_read);
    }

    if (bytes_read == -1) {
        perror("smash error: read failed");
    }

    close(fd);

    std::string allEnvVar = std::string(buffer.begin(), buffer.end());

    for (int i = 1; i < argc; ++i) {
        std::string currentEnvVar = std::string(args[i]);
        if (allEnvVar.find(currentEnvVar + "=") == std::string::npos) {
            perror(("smash error: unsetenv: " + currentEnvVar + " does not exist").c_str());
            for (int i = 0; i < argc; ++i) {
                free(args[i]);
            }
            return;
        }
        for (int i = 0; __environ[i] != 0; i++) {
            bool isSame = true;
            int t = 0;
            for (int j = 0; j < currentEnvVar.size() && __environ[i][j] != '='; j++) {
                if (__environ[i][j] != currentEnvVar[j]) {
                    isSame = false;
                    break;
                }
                t = j;
            }
            if (__environ[i][t]) {
                if (__environ[i][t + 1] != '=') isSame = false;
            }

            if (isSame) {

                do {
                    environ[i] = environ[i+1];
                    i++;
                } while (environ[i] != 0);
                break;
            }
        }
    }
    for (int i = 0; i < argc; ++i) {
        free(args[i]);
    }
}


SysInfoCommand::SysInfoCommand(const char *cmd_line): BuiltInCommand(cmd_line) {}

void SysInfoCommand::execute() {
    struct utsname uts{};
    if (uname(&uts) == -1) {
        perror("smash error: sysinfo: uname failed");
        return;
    }

    struct sysinfo si{};
    if (sysinfo(&si) == -1) {
        perror("smash error: sysinfo: sysinfo failed");
        return;
    }

    time_t now = time(nullptr);
    if (now == (time_t)-1) {
        perror("smash error: sysinfo: time failed");
        return;
    }

    time_t boot_time = now - si.uptime;

    struct tm boot_tm{};
    if (!localtime_r(&boot_time, &boot_tm)) {
        perror("smash error: sysinfo: localtime_r failed");
        return;
    }

    char boot_str[64];
    if (!strftime(boot_str, sizeof(boot_str), "%Y-%m-%d %H:%M:%S", &boot_tm)) {
        std::strcpy(boot_str, "unknown");
    }

    std::cout << "System: "      << uts.sysname  << std::endl;
    std::cout << "Hostname: "    << uts.nodename << std::endl;
    std::cout << "Kernel: "      << uts.release  << std::endl;
    std::cout << "Architecture: "<< uts.machine  << std::endl;
    std::cout << "Boot Time: "   << boot_str     << std::endl;

}

ExternalCommand::ExternalCommand(const char *cmd_line): Command(cmd_line) {}

void ExternalCommand::execute() {
    std::string cmd = std::string(cmd_line);
    std::string cmd_trimmed = _rtrim(cmd);

    if (cmd_trimmed.empty()) {
        return;
    }

    bool isBackground = (cmd_trimmed[cmd_trimmed.size() - 1] == '&');
    if (isBackground) {
        cmd_trimmed = cmd_trimmed.substr(0, cmd_trimmed.size() - 1);
        cmd_trimmed = _rtrim(cmd_trimmed);
    }

    bool isComplex = false;
    for (char c : cmd_trimmed) {
        if (c == '*' || c == '?') {
            isComplex = true;
            break;
        }
    }

    if (cmd_trimmed.empty()) {
        return;
    }
    int argc = 0;
    char *args[COMMAND_MAX_ARGS + 1];
    if (isComplex) {
        args[0] = strdup("bash");
        args[1] = strdup("-c");
        args[2] = strdup(cmd_trimmed.c_str());
        args[3] = NULL;
        argc = 3;
    }
    else {
        argc = _parseCommandLine(cmd_trimmed.c_str(), args);
    }

    if (argc == 0) {
        return;
    }

    pid_t pid = fork();

    if (pid == -1) {
        perror("smash error: fork failed");
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }


    if (pid == 0) {
        setpgrp();
        execvp(args[0], args);
        perror("smash error: execvp failed");
        exit(1);
    }
    if (!isBackground) {
        waitpid(pid, NULL, 0);
    }
    else {
        this->setPID(pid);
        SmallShell::getInstance().getJobsList()->addJob(this);
    }
    for (int i = 0; i < argc; ++i) {
        free(args[i]);
    }
}




// start of alias map ------------------------------------------------------------------------

AliasMap::AliasMap() = default;
AliasMap::~AliasMap() = default;

void AliasMap::addAlias(std::string alias, std::string command) {
    map.insert({alias, command});
}
void AliasMap::removeAlias(std::string alias) {
    map.erase(alias);
}
bool AliasMap::exists(std::string alias) {
    return map.find(alias) != map.end();
}
std::string AliasMap::getAlias(std::string alias) {
    if (map.find(alias) == map.end()) {
        return "";
    }
    return map.find(alias)->second;
}
void AliasMap::printAliases() {
    for (auto & it : map) {
        std::cout << it.first << "=\'" << it.second << "\'" << std::endl;
    }
}
std::string AliasMap::replaceAlias(const char *cmd_line) {
    string cmd_s = _trim(string(cmd_line));
    string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));
    if(exists(firstWord)) {
        std::string realFirstWord = getAlias(firstWord);
        size_t pos = cmd_s.find(' '); //may need to look out for special characters too
        if (pos == std::string::npos) {
            return realFirstWord;
        }
        return (realFirstWord+cmd_s.substr(pos));

    }
    return cmd_s;
}

// end of alias map ----------------------------------------------------





AliasCommand::AliasCommand(const char *cmd_line, AliasMap *map) : BuiltInCommand(cmd_line), map(map) {}
void AliasCommand::execute() {
    char *args[COMMAND_MAX_ARGS+1];
    int argc = _parseCommandLine(cmd_line, args);
    if (argc>2) {
        perror("smash error: alias: invalid alias format");
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }
    if (argc == 2) {
        std::string rest = args[1];
        size_t eq_pos = rest.find('=');
        if (eq_pos == std::string::npos) {
            perror("smash error: alias: invalid alias format");
            for (int i = 0; i < argc; ++i) {
                free(args[i]);
            }
            return;
        }
        std::string alias = rest.substr(0, eq_pos);
        std::string command = rest.substr(eq_pos+1);
        for (char c : alias) {
            if (!std::isalnum(c)&& c!='_') {
                perror("smash error: alias: invalid alias format");
                for (int i = 0; i < argc; ++i) {
                    free(args[i]);
                }
                return;
            }
        }
        if (map->exists(alias)) {
            std::string error = "smash error: alias: ";
            error += alias;
            error += " already exists or is a reserved command";
            perror (error.c_str());
            for (int i = 0; i < argc; ++i) {
                free(args[i]);
            }
            return;
        }
        std::vector<std::string> reserved = {
            "chprompt", "showpid", "pwd", "cd", "jobs", "fg", "quit",
            "kill", "alias", "unalias", "unsetenv", "sysinfo"};
        for (auto keyword : reserved) {
            if (alias == keyword) {
                std::string error = "smash error: alias: ";
                error += alias;
                error += " already exists or is a reserved command";
                perror (error.c_str());
                for (int i = 0; i < argc; ++i) {
                    free(args[i]);
                }
                return;
            }
        }
        map->addAlias(alias, command);
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
    }
    else {
        map->printAliases();
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
    }
}

UnAliasCommand::UnAliasCommand(const char *cmd_line, AliasMap *map) : BuiltInCommand(cmd_line), map(map) {}
void UnAliasCommand::execute() {
    char *args[COMMAND_MAX_ARGS+1];
    int argc = _parseCommandLine(cmd_line, args);
    if (argc<2) {
        perror("smash error: unalias: not enough arguments");
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }
    for (int j = 1; j < argc; ++j) {
        if (!map->exists(args[j])) {
            std::string error = "smash error: unalias: ";
            error += args[j];
            error += " does not exist";
            perror (error.c_str());
            for (int i = 0; i < argc; ++i) {
                free(args[i]);
            }
            return;
        }
        map->removeAlias(args[j]);

    }
    for (int i = 0; i < argc; ++i) {
        free(args[i]);
    }

}



DiskUsageCommand::DiskUsageCommand(const char *cmd_line)
    : Command(cmd_line) {}

void DiskUsageCommand::execute() {
    char *args[COMMAND_MAX_ARGS + 1];
    int argc = _parseCommandLine(cmd_line, args);

    std::string path;

    if (argc > 2) {

        perror("smash error: du: too many arguments");
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        return;
    }

    if (argc == 1) {
        // no path given -> use cwd
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) {
            perror("smash error: du: getcwd failed");
            for (int i = 0; i < argc; ++i) {
                free(args[i]);
            }
            return;
        }
        path = cwd;
    } else {
        path = args[1];
    }

    for (int i = 0; i < argc; ++i) {
        free(args[i]);
    }

    unsigned long long total_bytes = 0;
    if (du_recursive(path, total_bytes) == -1) {

        return;
    }

    unsigned long long total_kb = (total_bytes + 1023ULL) / 1024ULL;

    std::cout << "Total disk usage: " << total_kb << " KB" << std::endl;
}


WhoAmICommand::WhoAmICommand(const char *cmd_line)
    : Command(cmd_line) {}


void WhoAmICommand::execute() {
    // Parse and immediately free args – they are ignored by spec
    char *args[COMMAND_MAX_ARGS + 1];
    int argc = _parseCommandLine(cmd_line, args);
    for (int i = 0; i < argc; ++i) {
        free(args[i]);
    }

    uid_t uid = getuid();
    gid_t gid = getgid();

    struct passwd pw{};
    struct passwd* result = nullptr;
    char buf[4096];

    if (getpwuid_r(uid, &pw, buf, sizeof(buf), &result) != 0 || !result) {
        perror("smash error: whoami: getpwuid_r failed");
        return;
    }

    std::cout << uid << std::endl;
    std::cout << gid << std::endl;
    std::cout << pw.pw_name << " " << pw.pw_dir << std::endl;
}


USBInfoCommand::USBInfoCommand(const char *cmd_line)
    : Command(cmd_line) {}


void USBInfoCommand::execute() {
    // Ignore any arguments (but free them)
    char *args[COMMAND_MAX_ARGS + 1];
    int argc = _parseCommandLine(cmd_line, args);
    for (int i = 0; i < argc; ++i) {
        free(args[i]);
    }

    const std::string base = "/sys/bus/usb/devices";

    std::vector<std::string> entries;
    if (!list_dir_entries(base, entries)) {
        perror("smash error: usbinfo: cannot read /sys/bus/usb/devices");
        return;
    }

    std::vector<UsbDeviceInfo> devices;

    for (const auto& name : entries) {
        std::string devpath = base + "/" + name;

        // devnum tells us it's a "real" USB device
        std::string devnum_str;
        if (!read_first_line(devpath + "/devnum", devnum_str)) {
            continue; // not a real device directory
        }

        char* endptr = nullptr;
        long devnum = strtol(devnum_str.c_str(), &endptr, 10);
        if (endptr == devnum_str.c_str() || devnum <= 0) {
            continue; // malformed devnum
        }

        UsbDeviceInfo info{};
        info.devnum = (int)devnum;

        std::string vendor, product, manu, prodname;

        if (!read_first_line(devpath + "/idVendor", vendor))
            vendor = "N/A";
        if (!read_first_line(devpath + "/idProduct", product))
            product = "N/A";
        if (!read_first_line(devpath + "/manufacturer", manu))
            manu = "N/A";
        if (!read_first_line(devpath + "/product", prodname))
            prodname = "N/A";

        info.vendor = vendor;
        info.product = product;
        info.manufacturer = manu;
        info.product_name = prodname;

        // Max power: try bMaxPower, then power/max_power
        std::string mp;
        if (!read_first_line(devpath + "/bMaxPower", mp)) {
            read_first_line(devpath + "/power/max_power", mp);
        }

        if (mp.empty()) {
            info.max_power = "N/A";
        } else {
            // Extract digits only
            std::string digits;
            for (char c : mp) {
                if (isdigit((unsigned char)c)) {
                    digits.push_back(c);
                }
            }
            if (digits.empty()) {
                info.max_power = "N/A";
            } else {
                info.max_power = digits; // we'll add "mA" on print
            }
        }

        devices.push_back(info);
    }

    if (devices.empty()) {
        std::cerr << "smash error: usbinfo: no USB devices found" << std::endl;
        return;
    }

    // sort by devnum ascending
    std::sort(devices.begin(), devices.end(),
              [](const UsbDeviceInfo& a, const UsbDeviceInfo& b) {
                  return a.devnum < b.devnum;
              });

    for (const auto& d : devices) {
        std::cout << "Device " << d.devnum << ": ID "
                  << d.vendor << ":" << d.product << " "
                  << d.manufacturer << " " << d.product_name
                  << " MaxPower: ";

        if (d.max_power == "N/A") {
            std::cout << "N/A";
        } else {
            std::cout << d.max_power << "mA";
        }
        std::cout << std::endl;
    }
}
