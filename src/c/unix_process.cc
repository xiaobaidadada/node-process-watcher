#ifdef _WIN32
// Windows 平台保持原有实现
#else
#include "process.h"
#include <pwd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <mutex>
#include <signal.h>
#include <thread>
#include <map>
#include <set>
#include <vector>
#include <napi.h>

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <libproc.h>
#endif

#define BUFFER_SIZE 1024

typedef struct {
    unsigned long user;
    unsigned long system;
} sys_times_t;

typedef struct {
    unsigned long utime;
    unsigned long stime;
} process_stat_t;

typedef struct {
    char comm[100];
    char state;
    pid_t pid;
    pid_t ppid;
    process_stat_t stats;
    uid_t uid;
    char username[60];
    double cpu_usage;
    long rss;
} process_info_t;

void *xmalloc(size_t size) { return malloc(size); }
void *xrealloc(void *ptr, size_t size) { return realloc(ptr, size); }

int process_list_size = 32;
process_info_t *process_list;
process_info_t *tem_process_list;
int process_count = 0;

std::mutex mtx;

void increase_process_list_size() {
    process_list_size *= 2;
    process_list = (process_info_t *)xrealloc(process_list, process_list_size * sizeof *process_list);
    tem_process_list = (process_info_t *)xrealloc(tem_process_list, process_list_size * sizeof *tem_process_list);
}

#ifdef __linux__
// Linux 平台逻辑
int get_process_info(pid_t pid, uid_t *uid, long *rss, char *comm) {
    char path[BUFFER_SIZE];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *file = fopen(path, "r");
    if (!file) return -1;

    char line[BUFFER_SIZE];
    int i = 0;
    while (fgets(line, sizeof(line), file)) {
        if (i == 3) break;
        if (strncmp(line, "Uid:", 4) == 0) {
            sscanf(line, "Uid:\t%u", uid);
            i++;
            continue;
        }
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line, "VmRSS: %ld kB", rss);
            *rss *= 1024;
            i++;
            continue;
        }
        if (strncmp(line, "Name:", 5) == 0) {
            sscanf(line, "Name: %s", comm);
            i++;
            continue;
        }
    }
    fclose(file);
    return 0;
}

int read_process_stat(pid_t pid, process_info_t *proc_info, int not_name) {
    char path[BUFFER_SIZE];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *file = fopen(path, "r");
    if (!file) return -1;

    proc_info->pid = pid;
    fscanf(file, "%*d %*s %c %d %*d %*d %*d %*d %*d %*d %*d %*d %ld %ld",
           &proc_info->state, &proc_info->ppid, &proc_info->stats.utime, &proc_info->stats.stime);
    fclose(file);

    if (not_name) {
        uid_t uid;
        proc_info->uid = uid;
        char username[60];
        if (get_process_info(pid, &uid, &proc_info->rss, proc_info->comm) == 0) {
            if (getpwuid(uid)) {
                strncpy(proc_info->username, getpwuid(uid)->pw_name, sizeof(proc_info->username) - 1);
            } else {
                snprintf(proc_info->username, sizeof(proc_info->username), "%d", uid);
            }
        }
    }
    return 0;
}

int get_cpu_times(sys_times_t *times) {
    FILE *file = fopen("/proc/stat", "r");
    if (!file) return -1;
    fscanf(file, "cpu %lu %*lu %lu", &times->user, &times->system);
    fclose(file);
    return 0;
}
#endif

#ifdef __APPLE__
// macOS 平台逻辑
#include <sys/resource.h>
#include <mach/mach.h>

int read_process_stat(pid_t pid, process_info_t *proc_info, int not_name) {
    struct proc_bsdinfo info{};
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) <= 0) {
        return -1;
    }
    proc_info->pid = pid;
    proc_info->ppid = info.pbi_ppid;
    proc_info->uid = info.pbi_uid;
    strncpy(proc_info->comm, info.pbi_name, sizeof(proc_info->comm) - 1);
    proc_info->state = '?';

    struct task_basic_info t_info;
    mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count) == KERN_SUCCESS) {
        proc_info->rss = t_info.resident_size;
    } else {
        proc_info->rss = 0;
    }

    proc_info->cpu_usage = 0.0;

    struct passwd *pw = getpwuid(proc_info->uid);
    if (pw) {
        strncpy(proc_info->username, pw->pw_name, sizeof(proc_info->username) - 1);
    } else {
        snprintf(proc_info->username, sizeof(proc_info->username), "%d", proc_info->uid);
    }
    return 0;
}
#endif

extern std::map<std::string, Napi::ThreadSafeFunction> name_tsfn;
extern std::set<std::string> name_set;
extern std::map<std::string, std::set<int>> name_pids;
extern int print_second;

void runner() {
    for (;;) {
        if (name_set.empty()) break;

        int tem_process_count = 0;

#ifdef __linux__
        DIR *dir = opendir("/proc");
        if (!dir) return;

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_DIR && isdigit(entry->d_name[0])) {
                pid_t pid = atoi(entry->d_name);
                process_info_t proc_info{};
                proc_info.rss = 0;
                if (read_process_stat(pid, &proc_info, 1) == 0) {
                    tem_process_list[tem_process_count++] = proc_info;
                    if (tem_process_count >= process_list_size) increase_process_list_size();
                }
            }
        }
        closedir(dir);
#endif

#ifdef __APPLE__
        pid_t pid = getpid(); // 示例：获取当前进程，可扩展为 sysctl 获取所有 PID
        process_info_t proc_info{};
        if (read_process_stat(pid, &proc_info, 1) == 0) {
            tem_process_list[tem_process_count++] = proc_info;
            if (tem_process_count >= process_list_size) increase_process_list_size();
        }
#endif

        sleep(1);

        mtx.lock();
        process_count = tem_process_count;
        memcpy(process_list, tem_process_list, process_count * sizeof *process_list);
        mtx.unlock();
    }
}

void send_data_to_on() {
    for (const auto &name : name_set) {
        Napi::ThreadSafeFunction tsfn = name_tsfn[name];
        std::set<int> pid_set = name_pids[name];

        tsfn.BlockingCall([pid_set, process_list_ptr = process_list](Napi::Env env, Napi::Function js_callback) {
            Napi::HandleScope scope(env);
            Napi::Array list = Napi::Array::New(env);
            int push_count = 0;

            for (int i = 0; i < process_count; i++) {
                process_info_t proc_info = process_list_ptr[i];
                if (!pid_set.empty() && pid_set.find(proc_info.pid) == pid_set.end()) continue;

                Napi::Object obj = Napi::Object::New(env);
                obj.Set("id", proc_info.pid);
                obj.Set("user_name", proc_info.username);
                obj.Set("cpu", proc_info.cpu_usage);
                obj.Set("mem", proc_info.rss);
                obj.Set("name", proc_info.comm);
                list.Set(push_count++, obj);
            }

            js_callback.Call({list});
        });
    }
}

// 声明剩余接口
void kill_process(unsigned long pid, bool kill_all_children);
void get_all_process_ids(std::vector<process_pid_info> &pid_set, unsigned long ppid);
void start();
void stop(std::string name);
void set_pids(std::string name, std::set<int> pid_set);

#endif
