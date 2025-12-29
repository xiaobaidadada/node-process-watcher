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
#include <signal.h>
#include <unordered_map>
#include <chrono>
#include <cstdint>

#include "process.h"
#include <SystemConfiguration/SystemConfiguration.h>
#include <CoreFoundation/CoreFoundation.h>
// types

typedef struct {
    uint64_t user; // nanoseconds
    uint64_t system; // nanoseconds
} SysTimes;

typedef struct {
    uint64_t utime; // nanoseconds
    uint64_t stime; // nanoseconds
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

// 全局变量
int process_list_size = 32;
ProcessInfo *process_list = nullptr;
ProcessInfo *tem_process_list = nullptr;
int process_count = 0;
std::mutex mtx;

extern std::map<std::string, Napi::ThreadSafeFunction> name_tsfn;
extern std::set<std::string> name_set;
extern std::map<std::string, std::set<int> > name_pids;
extern int print_second;

void *xmalloc(size_t size) { return malloc(size); }
void *xrealloc(void *ptr, size_t size) { return realloc(ptr, size); }

void increase_process_list_size() {
    process_list_size *= 2;
    process_list = (ProcessInfo *) xrealloc(process_list, process_list_size * sizeof(ProcessInfo));
    tem_process_list = (ProcessInfo *) xrealloc(tem_process_list, process_list_size * sizeof(ProcessInfo));
}

void get_username_from_uid(uid_t uid, char *username, size_t size) {
    struct passwd *pw = getpwuid(uid);
    if (pw) {
        strncpy(username, pw->pw_name, size - 1);
        username[size - 1] = '\0';
    } else {
        snprintf(username, size, "%d", uid);
    }
}

// 读取单个进程的基本信息与 rusage（纳秒）
bool read_process_stat(pid_t pid, ProcessInfo *procInfo) {
    proc_bsdinfo info;
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) <= 0) return false;

    procInfo->pid = pid;
    procInfo->ppid = info.pbi_ppid;
    strncpy(procInfo->comm, info.pbi_name, sizeof(procInfo->comm) - 1);
    procInfo->comm[sizeof(procInfo->comm) - 1] = '\0';
    procInfo->uid = info.pbi_uid;
    get_username_from_uid(procInfo->uid, procInfo->username, sizeof(procInfo->username));
    procInfo->state = info.pbi_status;

    // 使用 proc_pid_rusage 获取时间和内存（rusage_info_v2，时间以纳秒计）
    struct rusage_info_v2 rusage;
    if (proc_pid_rusage(pid, RUSAGE_INFO_V2, (rusage_info_t *) &rusage) == 0) {
        procInfo->stats.utime = rusage.ri_user_time; // nanoseconds
        procInfo->stats.stime = rusage.ri_system_time; // nanoseconds
        procInfo->rss = (long) rusage.ri_resident_size; // bytes
    } else {
        procInfo->stats.utime = 0;
        procInfo->stats.stime = 0;
        procInfo->rss = 0;
    }
    procInfo->cpu_usage = 0.0;
    return true;
}

// 获取所有 pid 列表
static std::vector<pid_t> list_all_pids() {
    // 先查询所需 buffer 大小
    int bufsize = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    std::vector<pid_t> pids;
    if (bufsize <= 0) return pids;

    // bufsize 表示字节数，估计 pid 数量
    int maxpids = bufsize / sizeof(pid_t) + 16;
    pids.resize(maxpids);
    int actual = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), (int) (pids.size() * sizeof(pid_t)));
    if (actual <= 0) {
        pids.clear();
        return pids;
    }
    int count = actual / sizeof(pid_t);
    std::vector<pid_t> ret;
    ret.reserve(count);
    for (int i = 0; i < count; i++) {
        if (pids[i] > 0) ret.push_back(pids[i]);
    }
    return ret;
}

// runner: 两次采样法计算 cpu 使用率（通用、跨核）
void runner() {
    const int ncpu = (int) sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu <= 0) {
        // 兜底，至少取 1
    }

    while (true) {
        // 如果没有监听者就结束
        if (name_set.empty()) break;

        // 一次完整采样：读取所有 pid 的时间戳
        std::vector<pid_t> pids = list_all_pids();
        if (pids.empty()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        // 前次采样 map: pid -> total_ns
        std::unordered_map<pid_t, uint64_t> prev_total;
        std::vector<ProcessInfo> sample1;
        sample1.reserve(pids.size());

        for (pid_t pid: pids) {
            ProcessInfo info;
            memset(&info, 0, sizeof(info));
            if (read_process_stat(pid, &info)) {
                uint64_t total = info.stats.utime + info.stats.stime;
                prev_total[pid] = total;
                sample1.push_back(info);
            }
        }

        // 记录时间点 t1
        auto t1 = std::chrono::steady_clock::now();

        // sleep 大约 1 秒（或采用 print_second，更灵活）
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 第二次采样
        std::unordered_map<pid_t, uint64_t> cur_total;
        std::vector<ProcessInfo> sample2;
        sample2.reserve(pids.size());

        for (pid_t pid: pids) {
            ProcessInfo info;
            memset(&info, 0, sizeof(info));
            if (read_process_stat(pid, &info)) {
                uint64_t total = info.stats.utime + info.stats.stime;
                cur_total[pid] = total;
                sample2.push_back(info);
            }
        }

        // 记录时间点 t2，并计算真实 elapsed 纳秒
        auto t2 = std::chrono::steady_clock::now();
        uint64_t elapsed_ns = (uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
        if (elapsed_ns == 0) elapsed_ns = 1; // 防止除0

        // 构建 tem_process_list
        int tem_count = 0;
        for (const ProcessInfo &info2: sample2) {
            pid_t pid = info2.pid;
            uint64_t prev = 0;
            auto it = prev_total.find(pid);
            if (it != prev_total.end()) prev = it->second;
            uint64_t cur = cur_total[pid];
            uint64_t delta_proc_ns = 0;
            if (cur >= prev) delta_proc_ns = cur - prev;
            double cpu_pct = 0.0;
            // cpu% = delta_proc_ns / (elapsed_ns * ncpu) * 100
            if (elapsed_ns > 0 && ncpu > 0) {
                cpu_pct = (double) delta_proc_ns / ((double) elapsed_ns * (double) ncpu) * 100.0;
                if (cpu_pct < 0) cpu_pct = 0;
            }
            // 将 info2 填入 tem_process_list
            if (tem_count >= process_list_size) increase_process_list_size();
            ProcessInfo &dest = tem_process_list[tem_count];
            dest = info2; // 结构体拷贝（包含 stats、rss、username 等）
            dest.cpu_usage = cpu_pct;
            tem_count++;
        }

        // 将临时结果写入共享数组（加锁）
        mtx.lock();
        process_count = tem_count;
        if (process_list == nullptr) {
            process_list = (ProcessInfo *) xmalloc(process_list_size * sizeof(ProcessInfo));
        }
        memcpy(process_list, tem_process_list, process_count * sizeof(ProcessInfo));
        mtx.unlock();

        // 下一轮继续（循环会在 name_set 为空时退出）
    }
}

// send_data_to_on 保持不变（只是示例） - 参见你原来的实现
void send_data_to_on() {
    for (const auto &name: name_set) {
        auto it = name_tsfn.find(name);
        if (it == name_tsfn.end()) continue;
        Napi::ThreadSafeFunction tsfn = it->second;

        // 复制必要数据
        mtx.lock();
        ProcessInfo *local_list = process_list;
        int local_count = process_count;
        std::set<int> pid_set = name_pids[name];
        mtx.unlock();

        tsfn.BlockingCall([local_list, local_count, pid_set](Napi::Env env, Napi::Function jsCallback) {
            Napi::HandleScope scope(env);
            Napi::Array list = Napi::Array::New(env);
            int idx = 0;
            for (int i = 0; i < local_count; i++) {
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

// start / stop / set_pids 保持逻辑不变，只需在 start 中启动 runner
void start() {
    process_list = (ProcessInfo *) xmalloc(process_list_size * sizeof(ProcessInfo));
    tem_process_list = (ProcessInfo *) xmalloc(process_list_size * sizeof(ProcessInfo));
    std::thread([]() { runner(); }).detach();

    while (true) {
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
    std::vector<pid_t> pids = list_all_pids();
    for (pid_t pid: pids) {
        ProcessInfo info;
        if (read_process_stat(pid, &info) && (ppid == (unsigned long) -1 || info.ppid == (pid_t) ppid)) {
            pid_set.push_back({(unsigned long) pid, (unsigned long) info.ppid});
        }
    }
}

void kill_process(unsigned long pid, bool kill_all_children) {
    if (kill_all_children) {
        std::vector<process_pid_info> children;
        get_all_process_ids(children, pid);
        for (auto &c: children) kill_process(c.pid, kill_all_children);
    }
    ::kill(static_cast<pid_t>(pid), SIGTERM);
}


std::vector<MacHttpProxy> getAllMacProxies() {
    std::vector<MacHttpProxy> result;
    SCPreferencesRef prefs = SCPreferencesCreate(NULL, CFSTR("MyAppProxy"), NULL);
    if (!prefs) return result;

    SCNetworkSetRef currentSet = SCNetworkSetCopyCurrent(prefs);
    if (!currentSet) {
        CFRelease(prefs);
        return result;
    }

    CFArrayRef services = SCNetworkSetCopyServices(currentSet);
    if (!services) {
        CFRelease(currentSet);
        CFRelease(prefs);
        return result;
    }

    CFIndex count = CFArrayGetCount(services);
    for (CFIndex i = 0; i < count; ++i) {
        SCNetworkServiceRef service = (SCNetworkServiceRef) CFArrayGetValueAtIndex(services, i);
        if (!SCNetworkServiceGetEnabled(service)) continue;

        SCNetworkInterfaceRef iface = SCNetworkServiceGetInterface(service);
        if (!iface) continue;

        CFStringRef typeRef = SCNetworkInterfaceGetInterfaceType(iface);
        if (!typeRef) continue;

        // 只保留 Wi-Fi 和 Ethernet
        if (CFStringCompare(typeRef, kSCNetworkInterfaceTypeIEEE80211, 0) != kCFCompareEqualTo &&
            CFStringCompare(typeRef, kSCNetworkInterfaceTypeEthernet, 0) != kCFCompareEqualTo) {
            continue;
        }

        CFStringRef nameRef = SCNetworkServiceGetName(service);
        if (!nameRef) continue;

        char nameBuf[256];
        CFStringGetCString(nameRef, nameBuf, sizeof(nameBuf), kCFStringEncodingUTF8);
        MacHttpProxy macProxy;
        macProxy.name = nameBuf;

        SCNetworkProtocolRef proto = SCNetworkServiceCopyProtocol(service, kSCNetworkProtocolTypeProxies);
        if (!proto) continue;

        CFDictionaryRef dict = SCNetworkProtocolGetConfiguration(proto);
        if (!dict) {
            CFRelease(proto);
            continue;
        }

        // === HTTP 代理 ===
        HttpProxy httpProxy{};
        httpProxy.type = 1;
        CFNumberRef enabledRef = (CFNumberRef) CFDictionaryGetValue(dict, kSCPropNetProxiesHTTPEnable);
        CFStringRef hostRef = (CFStringRef) CFDictionaryGetValue(dict, kSCPropNetProxiesHTTPProxy);
        CFNumberRef portRef = (CFNumberRef) CFDictionaryGetValue(dict, kSCPropNetProxiesHTTPPort);
        if (enabledRef) {
            int v;
            CFNumberGetValue(enabledRef, kCFNumberIntType, &v);
            httpProxy.enabled = v != 0;
        }
        if (hostRef) {
            char buf[256];
            CFStringGetCString(hostRef, buf, sizeof(buf), kCFStringEncodingUTF8);
            httpProxy.ip = buf;
        }
        if (portRef) {
            int p;
            CFNumberGetValue(portRef, kCFNumberIntType, &p);
            httpProxy.port = p;
        }
        macProxy.proxies.push_back(httpProxy);

        // === HTTPS 代理 ===
        HttpProxy httpsProxy{};
        httpsProxy.type = 2;
        enabledRef = (CFNumberRef) CFDictionaryGetValue(dict, kSCPropNetProxiesHTTPSEnable);
        hostRef = (CFStringRef) CFDictionaryGetValue(dict, kSCPropNetProxiesHTTPSProxy);
        portRef = (CFNumberRef) CFDictionaryGetValue(dict, kSCPropNetProxiesHTTPSPort);
        if (enabledRef) {
            int v;
            CFNumberGetValue(enabledRef, kCFNumberIntType, &v);
            httpsProxy.enabled = v != 0;
        }
        if (hostRef) {
            char buf[256];
            CFStringGetCString(hostRef, buf, sizeof(buf), kCFStringEncodingUTF8);
            httpsProxy.ip = buf;
        }
        if (portRef) {
            int p;
            CFNumberGetValue(portRef, kCFNumberIntType, &p);
            httpsProxy.port = p;
        }
        macProxy.proxies.push_back(httpsProxy);

        // === bypass (排除地址) ===
        CFArrayRef bypassArr = (CFArrayRef) CFDictionaryGetValue(dict, kSCPropNetProxiesExceptionsList);
        if (bypassArr) {
            CFMutableStringRef joined = CFStringCreateMutable(NULL, 0);
            CFIndex bypassCount = CFArrayGetCount(bypassArr);
            for (CFIndex j = 0; j < bypassCount; ++j) {
                CFStringRef item = (CFStringRef) CFArrayGetValueAtIndex(bypassArr, j);
                if (j > 0) CFStringAppend(joined, CFSTR(","));
                CFStringAppend(joined, item);
            }
            char bypassBuf[512];
            CFStringGetCString(joined, bypassBuf, sizeof(bypassBuf), kCFStringEncodingUTF8);
            macProxy.bypass = bypassBuf;
            CFRelease(joined);
        }

        result.push_back(macProxy);
        CFRelease(proto);
    }

    CFRelease(services);
    CFRelease(currentSet);
    CFRelease(prefs);
    return result;
}


bool setMacProxies(const std::vector<MacHttpProxy> &proxies) {
    AuthorizationRef auth = nullptr;
    OSStatus status = AuthorizationCreate(nullptr, kAuthorizationEmptyEnvironment,
                                          kAuthorizationFlagDefaults, &auth);
    if (status != errAuthorizationSuccess || !auth) return false;

    // 请求修改系统配置权限 出现弹窗
    AuthorizationItem right = {kAuthorizationRightExecute, 0, nullptr, 0};
    AuthorizationRights rights = {1, &right};

    status = AuthorizationCopyRights(auth, &rights, nullptr,
                                     kAuthorizationFlagInteractionAllowed |
                                     kAuthorizationFlagPreAuthorize |
                                     kAuthorizationFlagExtendRights,
                                     nullptr);
    if (status != errAuthorizationSuccess) {
        AuthorizationFree(auth, kAuthorizationFlagDefaults);
        return false;
    }

    // 使用授权创建 SCPreferences
    SCPreferencesRef prefs = SCPreferencesCreateWithAuthorization(NULL,
                                                                  CFSTR("MyAppProxy"),
                                                                  NULL,
                                                                  auth);
    if (!prefs) {
        AuthorizationFree(auth, kAuthorizationFlagDefaults);
        return false;
    }

    if (!SCPreferencesLock(prefs, true)) {
        CFRelease(prefs);
        AuthorizationFree(auth, kAuthorizationFlagDefaults);
        return false;
    }

    SCNetworkSetRef currentSet = SCNetworkSetCopyCurrent(prefs);
    if (!currentSet) {
        SCPreferencesUnlock(prefs);
        CFRelease(prefs);
        AuthorizationFree(auth, kAuthorizationFlagDefaults);
        return false;
    }

    CFArrayRef services = SCNetworkSetCopyServices(currentSet);
    if (!services) {
        CFRelease(currentSet);
        SCPreferencesUnlock(prefs);
        CFRelease(prefs);
        AuthorizationFree(auth, kAuthorizationFlagDefaults);
        return false;
    }

    CFIndex count = CFArrayGetCount(services);

    for (const auto &macProxy: proxies) {
        for (CFIndex i = 0; i < count; ++i) {
            SCNetworkServiceRef service = (SCNetworkServiceRef) CFArrayGetValueAtIndex(services, i);
            if (!SCNetworkServiceGetEnabled(service)) continue;

            CFStringRef nameRef = SCNetworkServiceGetName(service);
            if (!nameRef) continue;

            char nameBuf[256];
            if (!CFStringGetCString(nameRef, nameBuf, sizeof(nameBuf), kCFStringEncodingUTF8))
                continue;

            if (macProxy.name != nameBuf) continue;

            SCNetworkProtocolRef proto = SCNetworkServiceCopyProtocol(service, kSCNetworkProtocolTypeProxies);
            if (!proto) continue;

            CFDictionaryRef oldDict = SCNetworkProtocolGetConfiguration(proto);
            CFMutableDictionaryRef newDict = oldDict
                                                 ? CFDictionaryCreateMutableCopy(NULL, 0, oldDict)
                                                 : CFDictionaryCreateMutable(NULL, 0,
                                                                             &kCFTypeDictionaryKeyCallBacks,
                                                                             &kCFTypeDictionaryValueCallBacks);

            // 设置 HTTP/HTTPS 代理
            for (const auto &proxy: macProxy.proxies) {
                int enabledVal = proxy.enabled ? 1 : 0;
                CFNumberRef enabledRef = CFNumberCreate(NULL, kCFNumberIntType, &enabledVal);

                if (proxy.type == 1) {
                    CFDictionarySetValue(newDict, kSCPropNetProxiesHTTPEnable, enabledRef);
                    CFStringRef hostRef = CFStringCreateWithCString(NULL, proxy.ip.c_str(), kCFStringEncodingUTF8);
                    CFNumberRef portRef = CFNumberCreate(NULL, kCFNumberIntType, &proxy.port);
                    CFDictionarySetValue(newDict, kSCPropNetProxiesHTTPProxy, hostRef);
                    CFDictionarySetValue(newDict, kSCPropNetProxiesHTTPPort, portRef);
                    CFRelease(hostRef);
                    CFRelease(portRef);
                } else if (proxy.type == 2) {
                    CFDictionarySetValue(newDict, kSCPropNetProxiesHTTPSEnable, enabledRef);
                    CFStringRef hostRef = CFStringCreateWithCString(NULL, proxy.ip.c_str(), kCFStringEncodingUTF8);
                    CFNumberRef portRef = CFNumberCreate(NULL, kCFNumberIntType, &proxy.port);
                    CFDictionarySetValue(newDict, kSCPropNetProxiesHTTPSProxy, hostRef);
                    CFDictionarySetValue(newDict, kSCPropNetProxiesHTTPSPort, portRef);
                    CFRelease(hostRef);
                    CFRelease(portRef);
                }

                CFRelease(enabledRef);
            }

            // ✅ 设置代理绕过（bypass）
            if (!macProxy.bypass.empty()) {
                std::vector<CFStringRef> bypassItems;
                std::stringstream ss(macProxy.bypass);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    if (!item.empty()) {
                        CFStringRef cfItem = CFStringCreateWithCString(NULL, item.c_str(), kCFStringEncodingUTF8);
                        if (cfItem) bypassItems.push_back(cfItem);
                    }
                }

                CFArrayRef bypassArray = CFArrayCreate(NULL,
                                                       (const void **)bypassItems.data(),
                                                       bypassItems.size(),
                                                       &kCFTypeArrayCallBacks);
                CFDictionarySetValue(newDict, kSCPropNetProxiesExceptionsList, bypassArray);
                CFRelease(bypassArray);

                for (auto cf : bypassItems) CFRelease(cf);
            } else {
                // 没有绕过配置时移除
                CFDictionaryRemoveValue(newDict, kSCPropNetProxiesExceptionsList);
            }

            SCNetworkProtocolSetConfiguration(proto, newDict);
            CFRelease(newDict);
            CFRelease(proto);
        }
    }

    bool ok = SCPreferencesCommitChanges(prefs) && SCPreferencesApplyChanges(prefs);

    CFRelease(services);
    CFRelease(currentSet);
    SCPreferencesUnlock(prefs);
    CFRelease(prefs);
    AuthorizationFree(auth, kAuthorizationFlagDefaults);

    return ok;
}



HttpProxy getSystemProxy() {
    HttpProxy proxy;
    proxy.enabled = false;
    proxy.ip = "";
    proxy.port = 0;
    proxy.bypass = "";
    proxy.useForLocal = false;
    return proxy;
}

bool setSystemProxy(const HttpProxy &config) {
    // Mac/Linux暂不实现
    (void) config; // 防止未使用警告
    return false;
}

// Windows 特定函数的存根实现（Mac 下不实现，仅用于链接）
HttpProxy getSystemProxyForWindows() {
    HttpProxy proxy;
    proxy.enabled = false;
    proxy.ip = "";
    proxy.port = 0;
    proxy.bypass = "";
    proxy.useForLocal = false;
    return proxy;
}

bool setSystemProxyForWindows(const HttpProxy& config) {
    // Mac 下不实现 Windows 代理设置
    (void)config; // 防止未使用警告
    return false;
}

bool is_current_user_admin() {
    return geteuid() == 0;
}
