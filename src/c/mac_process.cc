#include <napi.h>
#include <thread>
#include <set>
#include <map>
#include <vector>
#include <string>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <pwd.h>
#include <libproc.h>
#include <sys/types.h>
#include <mach/mach.h>
#include <iostream>
#include <signal.h>  // 必须加上

typedef struct process_pid_info {
    unsigned long pid;
    unsigned long ppid;
} process_pid_info;

typedef struct {
    unsigned long user;
    unsigned long system;
} SysTimes;

typedef struct {
    unsigned long utime;
    unsigned long stime;
} ProcessStat;

typedef struct {
    char comm[100];
    char state;
    pid_t pid;
    pid_t ppid;
    ProcessStat stats;
    uid_t uid;
    char username[60];
    double cpu_usage;
    long rss;
} ProcessInfo;

int process_list_size = 32;
ProcessInfo *process_list;
ProcessInfo *tem_process_list;
int process_count = 0;
std::mutex mtx;

extern std::map<std::string, Napi::ThreadSafeFunction> name_tsfn;
extern std::set<std::string> name_set;
extern std::map<std::string, std::set<int>> name_pids;
extern int print_second;

void *xmalloc(size_t size) { return malloc(size); }
void *xrealloc(void *ptr, size_t size) { return realloc(ptr, size); }

void increase_process_list_size() {
    process_list_size *= 2;
    process_list = (ProcessInfo*) xrealloc(process_list, process_list_size * sizeof(ProcessInfo));
    tem_process_list = (ProcessInfo*) xrealloc(tem_process_list, process_list_size * sizeof(ProcessInfo));
}

void get_username_from_uid(uid_t uid, char* username, size_t size) {
    struct passwd* pw = getpwuid(uid);
    if (pw) {
        strncpy(username, pw->pw_name, size - 1);
        username[size-1] = '\0';
    } else {
        snprintf(username, size, "%d", uid);
    }
}

bool read_process_stat(pid_t pid, ProcessInfo* procInfo) {
    proc_bsdinfo info;
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) <= 0) return false;

    procInfo->pid = pid;
    procInfo->ppid = info.pbi_ppid;
    strncpy(procInfo->comm, info.pbi_name, sizeof(procInfo->comm)-1);
    procInfo->comm[sizeof(procInfo->comm)-1] = '\0';
    procInfo->uid = info.pbi_uid;
    get_username_from_uid(procInfo->uid, procInfo->username, sizeof(procInfo->username));
    procInfo->state = info.pbi_status;

    struct rusage_info_v2 rusage;
    if (proc_pid_rusage(pid, RUSAGE_INFO_V2, (rusage_info_t*)&rusage) == 0) {
        procInfo->stats.utime = rusage.ri_user_time;
        procInfo->stats.stime = rusage.ri_system_time;
        procInfo->rss = rusage.ri_resident_size;
    } else {
        procInfo->stats.utime = 0;
        procInfo->stats.stime = 0;
        procInfo->rss = 0;
    }
    procInfo->cpu_usage = 0.0;
    return true;
}

void runner() {
    while (true) {
        int num_pids = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
        if (num_pids <= 0) break;

        std::vector<pid_t> pids(num_pids / sizeof(pid_t));
        num_pids = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), (int)pids.size() * sizeof(pid_t));
        int tem_count = 0;

        for (int i=0; i<num_pids/sizeof(pid_t); i++) {
            pid_t pid = pids[i];
            if (pid <= 0) continue;
            ProcessInfo info;
            if (read_process_stat(pid, &info)) {
                tem_process_list[tem_count++] = info;
                if (tem_count >= process_list_size) increase_process_list_size();
            }
        }

        mtx.lock();
        process_count = tem_count;
        memcpy(process_list, tem_process_list, process_count * sizeof(ProcessInfo));
        mtx.unlock();

        sleep(1);
    }
}

void send_data_to_on() {
    for (auto &name : name_set) {
        auto tsfn_it = name_tsfn.find(name);
        if (tsfn_it == name_tsfn.end()) continue;

        Napi::ThreadSafeFunction tsfn = tsfn_it->second;

        // 先拷贝全局变量，避免 lambda 捕获问题
        ProcessInfo* local_list;
        int local_count;
        mtx.lock();
        local_list = process_list;
        local_count = process_count;
        std::set<int> pid_set = name_pids[name];
        mtx.unlock();

        tsfn.BlockingCall([local_list, local_count, pid_set](Napi::Env env, Napi::Function jsCallback) {
            Napi::HandleScope scope(env);
            Napi::Array list = Napi::Array::New(env);
            int idx = 0;
            for (int i=0; i<local_count; i++) {
                ProcessInfo &proc = local_list[i];
                if (!pid_set.empty() && pid_set.find(proc.pid) == pid_set.end()) continue;

                Napi::Object obj = Napi::Object::New(env);
                obj.Set("id", proc.pid);
                obj.Set("user_name", proc.username);
                obj.Set("cpu", proc.cpu_usage);
                obj.Set("mem", proc.rss);
                obj.Set("name", proc.comm);
                list.Set(idx++, obj);
            }
            jsCallback.Call({list});
        });
    }
}

// 外部可调用接口
void start() {
    process_list = (ProcessInfo*) xmalloc(process_list_size * sizeof(ProcessInfo));
    tem_process_list = (ProcessInfo*) xmalloc(process_list_size * sizeof(ProcessInfo));
    std::thread([](){ runner(); }).detach();

    while(true) {
        sleep(print_second);
        if (!name_set.empty()) send_data_to_on();
    }
}

void stop(std::string name) {
    mtx.lock();
    name_set.erase(name);
    name_pids.erase(name);
    auto it = name_tsfn.find(name);
    if (it != name_tsfn.end()) it->second.Release();
    name_tsfn.erase(name);
    mtx.unlock();
}

void set_pids(std::string name, std::set<int> pid_set) {
    mtx.lock();
    name_pids[name] = pid_set;
    mtx.unlock();
}

void get_all_process_ids(std::vector<process_pid_info> &pid_set, unsigned long ppid) {
    int num_pids = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (num_pids <= 0) return;

    std::vector<pid_t> pids(num_pids / sizeof(pid_t));
    num_pids = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), (int)pids.size() * sizeof(pid_t));

    for (int i=0; i<num_pids/sizeof(pid_t); i++) {
        pid_t pid = pids[i];
        if (pid <= 0) continue;

        ProcessInfo info;
        if (read_process_stat(pid, &info) && (ppid == -1 || info.ppid == static_cast<pid_t>(ppid))) {
            pid_set.push_back({(unsigned long)pid, (unsigned long)info.ppid});
        }
    }
}

void kill_process(unsigned long pid, bool kill_all_children) {
    if (kill_all_children) {
        std::vector<process_pid_info> children;
        get_all_process_ids(children, pid);
        for (auto &c : children) kill_process(c.pid, kill_all_children);
    }
    ::kill(static_cast<pid_t>(pid), SIGTERM); // 强制转换
}
