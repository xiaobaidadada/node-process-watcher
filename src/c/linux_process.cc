
#ifdef _WIN32
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

#define PROC_DIR "/proc"
#define STAT_FILE "stat"
#define BUFFER_SIZE 1024
#define BUFFER_SIZE 1024
#define HZ sysconf(_SC_CLK_TCK) // 获取系统时钟滴答频率
// 单位是jiffies 滴答，一般是毫秒
typedef struct {
    unsigned long user; // 用户模式下的CPU时间（不包含nice值为负的进程的时间）。
    unsigned long system; // system: 内核模式下的CPU时间。
} SysTimes;

typedef struct {
    unsigned long utime;     // 用户模式下的CPU时间
    unsigned long stime;     // 内核模式下的CPU时间
} ProcessStat;

typedef struct {
    char comm[100]; // 进程名称
    char state;             // 进程状态
    pid_t pid;              // 进程ID
    pid_t ppid; // 父进程
    ProcessStat stats;      // 进程的CPU时间统计
    uid_t uid;    // 用户uid
    char username[60]; // 用户名字

    double cpu_usage; // cpu 使用率
    long rss;               // 物理内存使用量 (初始获取单位: 页) (最后的单位是字节)
} ProcessInfo;

// 用于分配指定大小的内存块
void *xmalloc(size_t size)
{
    void *m = malloc(size);

    return m;
}
// 用于调整已分配内存块的大小
void *xrealloc(void *ptr, size_t size)
{
    void *m = realloc(ptr, size);

    return m;
}
// 开始
int process_list_size = 32; // 默认32个元素
ProcessInfo *process_list; // 用于保存最后结果
ProcessInfo *tem_process_list; // 用于临时存储进程信息，最后保存到process_list
int process_count = 0; // 实际进程数量，初始化的时候就是0，不是0的时候肯定和 process_list中的数据可以对应上
void increase_process_list_size(void)
{
    // 每次扩容，虽然都是成倍，且以后不再缩小，这样减少了多次扩容，这点内存也不多。不需要的内存以后不利用就行了，也就是使用数组访问的时候
    process_list_size += process_list_size;
    process_list = (ProcessInfo *) xrealloc(process_list,process_list_size * sizeof * process_list); // 临时按32个进程处理
    tem_process_list = (ProcessInfo *) xrealloc(tem_process_list,process_list_size * sizeof *tem_process_list);
}

// 获取进程的UID
int get_process_info(pid_t pid, uid_t *uid, long *rss, char *comm) {
    char path[BUFFER_SIZE];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    FILE *file = fopen(path, "r");
    if (!file) {
        perror("fopen");
        return -1;
    }

    char line[BUFFER_SIZE];
    int i = 0;
    while (fgets(line, sizeof(line), file)) {
        if (i ==3) {
            break;
        }
        if (strncmp(line, "Uid:", 4) == 0) {
            sscanf(line, "Uid:\t%u", uid);
            i++;
            continue;
        }
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line, "VmRSS: %ld kB", rss);
            *rss *= 1024;  // Convert RSS from KB to bytes
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

// 将UID转换为用户名
int get_username_from_uid(uid_t uid, char *username, size_t size) {
    struct passwd *pw = getpwuid(uid);
    if (pw) {
        strncpy(username, pw->pw_name, size - 1);
        username[size - 1] = '\0'; // 确保字符串以 '\0' 结尾
        return 0;
    } else {
        // 找不到名字 用uid代替名字
        snprintf(username, sizeof(username), "%d", uid);
        return 0;
    }
}

int read_process_stat(pid_t pid, ProcessInfo *procInfo,int not_name ) {
    char path[BUFFER_SIZE];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *file = fopen(path, "r");
    if (!file) {
        return -1;
    }
    procInfo->pid = pid;
    // 读取 comm, state, PPID, utime, stime,  rss
    fscanf(file, "%*d %*s %c %d %*d %*d %*d %*d %*d %*d %*d %*d %ld %ld",&procInfo->state, &procInfo->ppid, &procInfo->stats.utime, &procInfo->stats.stime);

    fclose(file);
    if (not_name) {
        // 获取名字
        uid_t uid;
        procInfo->uid = uid;
        char username[60];
        if (get_process_info(pid, &uid, &procInfo->rss, procInfo->comm) == 0) {
            // 根据UID获取用户名
            if (get_username_from_uid(uid, username, sizeof(username)) == 0) {
                strcpy(procInfo->username, username);
            }
        }
    }
    return 0;
}

int get_cpu_times(SysTimes *times) {
    FILE *file = fopen("/proc/stat", "r");
    if (!file) {
        perror("fopen /proc/stat");
        return -1;
    }

    fscanf(file, "cpu %lu %*lu %lu", &times->user, &times->system);

    fclose(file);
    return 0;
}



extern std::map<std::string ,Napi::ThreadSafeFunction> name_tsfn;
extern std::set<std::string> name_set;
extern std::map<std::string,std::set<int>> name_pids;
std::mutex mtx;  // 创建一个互斥锁

void runner() {
    for(;;) {
        if (name_set.size() == 0) {
            break;
        }
        DIR *dir = opendir("/proc");
        if (!dir) {
            return ;
        }
        int tem_process_count = 0;
        struct dirent *entry;
        // 第一次获取
        SysTimes times;
        get_cpu_times(&times);
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_DIR && isdigit(entry->d_name[0])) {
                // 是目录且以数字命名才是进程 /proc 下除了进程文件，还有别的文件
                pid_t pid = atoi(entry->d_name);
                ProcessInfo procInfo;
                procInfo.rss = 0;
                if (read_process_stat(pid, &procInfo,1) == 0) {
                    tem_process_list[tem_process_count] = procInfo;
                    tem_process_count++;
                    if (tem_process_count >= process_list_size) {
                        increase_process_list_size();
                    }
                }
            }
        }
        closedir(dir);
        sleep(1);
        SysTimes times_new;
        get_cpu_times(&times_new);
        long sys_total_cpu_time = (times_new.system+times_new.user) - (times.system+times.user);
        // 第二次获取
        for (int index = 0; index < tem_process_count; index ++) {
            ProcessInfo *procInfo = &tem_process_list[index];
            ProcessInfo tem_procInfo;
            if (read_process_stat(procInfo->pid, &tem_procInfo,0) == 0) { // 不变 ？
                long proc_total_cpu_time = (tem_procInfo.stats.utime + tem_procInfo.stats.stime) - (procInfo->stats.utime + procInfo->stats.stime);
                double cpu_uage = (double)((100.0 * (double)proc_total_cpu_time) / (double)sys_total_cpu_time);
                procInfo->cpu_usage = cpu_uage;
            }
        }
        mtx.lock();  // 尝试获取锁
        process_count = tem_process_count;
        memcpy(process_list, tem_process_list, process_count * sizeof *process_list);
        mtx.unlock();  // 释放锁
    }
}
void display_process_info() {
    for (int index = 0; index < process_count; index ++) {
        ProcessInfo procInfo = process_list[index];
        std::cout<<"pid:"<<procInfo.pid<<"| cpu%:"<<procInfo.cpu_usage<<" |内存:"<<procInfo.rss<<"\n";
//        display_process_info(&procInfo);
    }

//    printf("PID: %d | 命令: %s | cpu使用率: %f  | 内存:%ld \n", procInfo->pid, procInfo->comm, procInfo->cpu_usage,procInfo->rss);

}

// 向js监听函数发送数据
void send_data_to_on() {
    // 遍历 map
    for (const  auto& name : name_set) {
        Napi::ThreadSafeFunction  tsfn = name_tsfn[name];
        std::set<int> pid_set = name_pids[name];
        tsfn.BlockingCall([process_list,process_count,pid_set](Napi::Env env, Napi::Function jsCallback) {
            // 但是这里是可以使用 env的
            Napi::HandleScope scope(Napi::Env);
            Napi::Array  list = Napi::Array::New(env);
            int push_count = 0;
            for (int index = 0; index < process_count; index ++) {
                ProcessInfo process = process_list[index];
                if (pid_set.size()!=0 && pid_set.find(process.pid) == pid_set.end()) {
                    continue;
                }
                Napi::Object object =  Napi::Object::New(env);
                object.Set("id",process.pid);
                object.Set("user_name",process.username);
                // 使用率
                object.Set("cpu",process.cpu_usage);
                // 字节
                object.Set("mem",process.rss);
                object.Set("name",process.comm);
                list.Set(push_count,object);
                push_count++;
            }

            // 内部线程调用jsFunc(就是外部传进来的handler)子函数
            jsCallback.Call({ list});
        });
    }


}
void start() {

    process_list = (ProcessInfo *) xmalloc(process_list_size * sizeof *process_list); // 临时按32个进程处理
    tem_process_list = (ProcessInfo *) xmalloc(process_list_size * sizeof *process_list);
    // 创建子线程
    std::thread thread([]() {
        // env无法使用
       runner();
    });
    // 不等待子线程结束
    thread.detach();
    for(;;) {
        if (process_count==0) {
            sleep(1);
            continue;
        }
        // 遍历打印一下
//        display_process_info();
        if (name_set.size() == 0) {
            break;
        }
        mtx.lock();  // 尝试获取锁
        send_data_to_on();
        mtx.unlock();  // 释放锁
        sleep(1);
    };
}

void stop(std::string name) {
    mtx.lock();  // 尝试获取锁
    name_set.erase(name);
    name_pids.erase(name);
    name_tsfn[name].Release();
    name_tsfn.erase(name);
    mtx.unlock();  // 释放锁
}

void set_pids(std::string name,std::set<int> pid_set) {
    mtx.lock();  // 尝试获取锁
    name_pids[name] = pid_set;
    mtx.unlock();  // 释放锁
}
#endif