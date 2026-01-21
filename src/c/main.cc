
#include <filesystem>
#include <stack>
#include "process.h"
namespace fs = std::filesystem;

std::map<std::string ,Napi::ThreadSafeFunction> name_tsfn;
std::set<std::string> name_set;
std::map<std::string,std::set<int>> name_pids;
int print_second = 1; // 默认打印间隔 1 秒

std::set<std::string> thread_id_set;

void on(const Napi::CallbackInfo &info) {

    // 全局对象
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env); // 作用域限制 性能优化 避免对象提前消失
    Napi::String name = info[0].As<Napi::String>();
    // 接收函数 被子线程调用
    Napi::Function handler = info[1].As<Napi::Function>();

    auto it = name_set.find(name.Utf8Value());
    if (it != name_set.end()) {
        return; // 已经注册过了
    }
    std::set<int> pid_set;
    if (info.Length() ==3 && info[2].IsArray()) {
        Napi::Array pidsp = info[2].As<Napi::Array>();
        for (uint32_t i = 0; i < pidsp.Length(); i++) {
            Napi::Value val = pidsp[i];

            // 检查数组元素是否为数字类型
            if (val.IsNumber()) {
//                pids.push_back(val.As<Napi::Number>().Int32Value());
                pid_set.insert(val.As<Napi::Number>().Int32Value());  // 插入到 set 集合中
            } else {
                Napi::TypeError::New(env, "Array elements must be numbers").ThrowAsJavaScriptException();
            }
        }
    }
    Napi::ThreadSafeFunction tsfn = Napi::ThreadSafeFunction::New(
            env,
            handler,  // 传递参数，子线程需要
            "ChildThread",                  // 线程名字
            0,                              // 从子线程发送到主线程的数据项队列的最大值，0是没有限制
            1                              // 最大线程数量，当有这么多个线程调用Release函数后，就会释放了
    );
    name_set.insert(name.Utf8Value());
    name_tsfn[name.Utf8Value()] = tsfn;
    name_pids[name.Utf8Value()] = pid_set;
    if (name_set.size() == 1) {
        // 创建子线程
        std::thread thread([]() {
            // env无法使用
            // 子线程执行
            start();
        });
        // 不等待子线程结束
        thread.detach();
    }
}

void off(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::String name = info[0].As<Napi::String>();
//    // 通过 Napi::External<Napi::ThreadSafeFunction> 解包 tsfn
//    Napi::External<Napi::ThreadSafeFunction> externalTsfn = info[0].As<Napi::External<Napi::ThreadSafeFunction>>();
//    // 恢复 Napi::ThreadSafeFunction 对象
//    Napi::ThreadSafeFunction tsfn = *externalTsfn.Data();
    auto it = name_set.find(name.Utf8Value());
    if (it == name_set.end()) {
        return; // 没有注册过
    }
    stop(name.Utf8Value());
}
void reset_pids(const Napi::CallbackInfo &info) {
    std::set<int> pid_set;
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);
    Napi::String name = info[0].As<Napi::String>();
    auto it = name_set.find(name.Utf8Value());
    if (it == name_set.end()) {
        return; // 没有注册过
    }
    if (info.Length() ==2 && info[1].IsArray()) {
        Napi::Array pidsp = info[1].As<Napi::Array>();
        for (uint32_t i = 0; i < pidsp.Length(); i++) {
            Napi::Value val = pidsp[i];

            // 检查数组元素是否为数字类型
            if (val.IsNumber()) {
                pid_set.insert(val.As<Napi::Number>().Int32Value());  // 插入到 set 集合中
            } else {
                Napi::TypeError::New(env, "Array elements must be numbers").ThrowAsJavaScriptException();
            }
        }
    }
    set_pids(name.Utf8Value(),pid_set);
}
void set_print_second(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::Number second = info[0].As<Napi::Number>();
    print_second = second.Int32Value();
}

Napi::Value get_all_pid(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::Number ppid = info[0].As<Napi::Number>();
    std::vector<process_pid_info>  pid_set;
    get_all_process_ids(pid_set,info.Length() == 0 ?-1: ppid.Int64Value());
    Napi::Array jsArray = Napi::Array::New(env, pid_set.size());
    for (size_t i = 0; i < pid_set.size(); ++i) {
        const process_pid_info& info = pid_set[i];
        // 创建一个 JavaScript 对象来存储每个 process_pid_info 的字段
        Napi::Object obj = Napi::Object::New(env);
        obj.Set("pid", Napi::Number::New(env, info.pid));
        obj.Set("ppid", Napi::Number::New(env, info.ppid));
        // 将该对象添加到 JavaScript 数组中
        jsArray[i] = obj;
    }
    return jsArray;
}
void kill (const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if(info.Length() < 1)
    {
        Napi::TypeError::New(env, "param num is error .").ThrowAsJavaScriptException();
        return;
    }
    kill_process(info[0].As<Napi::Number>().Int64Value(), info.Length() ==1 ? false: info[1].As<Napi::Boolean>().Value());
}

Napi::Boolean is_admin(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    bool ok = is_current_user_admin();
    return Napi::Boolean::New(env, ok);
}

uintmax_t get_directory_size(std::string dir,Napi::ThreadSafeFunction tsfn) {
    const fs::path& root(dir);
    if (!fs::exists(root) || !fs::is_directory(root)) {
        // std::cerr << "路径无效: " << root << std::endl;
        return 0;
    }

    std::stack<fs::path> dirs;
    dirs.push(root);
    uintmax_t total_size = 0;
    uintmax_t file_num = 0;

    while (!dirs.empty()) {
        if (thread_id_set.find(dir) == thread_id_set.end()) {
            return 0;
        }
        fs::path current_dir = dirs.top();
        dirs.pop();
        for (const auto& entry : fs::directory_iterator(current_dir)) {
            try {
                if (fs::is_directory(entry.status())) {
                    dirs.push(entry.path());  // 目录压栈，后续继续处理
                } else if (fs::is_regular_file(entry.status())) {
                    total_size += fs::file_size(entry.path());
                    file_num++;
                }
            } catch (const std::exception& e) {
                // std::cerr << "跳过错误项: " << entry.path() << " 错误: " << e.what() << std::endl;
            }
        }
        tsfn.BlockingCall([file_num,total_size](Napi::Env env, Napi::Function jsCallback)
                            {
                                // 但是这里是可以使用 env的
                                Napi::HandleScope scope(Napi::Env);
                                // Napi::Object object = Napi::Object::New(env);
                                // object.Set("file_num", Napi::Number::New(env, file_num));
                                // object.Set("total_size", Napi::Number::New(env, total_size));
                                jsCallback.Call({Napi::Number::New(env, file_num),Napi::Number::New(env, total_size)});
                            });
    }
    return total_size;
}

void on_folder_size(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);
    Napi::String folder_name = info[0].As<Napi::String>();
    Napi::Function on_handler = info[1].As<Napi::Function>();
    Napi::ThreadSafeFunction tsfn = Napi::ThreadSafeFunction::New(
            env,
            on_handler,
            "ChildThread",
            0,
            1
    );
    // fs::path dir(folder_name.Utf8Value());
    std::string dir  =  folder_name.Utf8Value();
    std::thread thread([dir,tsfn]() {
        get_directory_size(dir,tsfn);
            // 释放
            tsfn.Release();
        });
    thread.detach();
    thread_id_set.insert(folder_name.Utf8Value());
}
void stop_folder_size(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);
    Napi::String folder_name = info[0].As<Napi::String>();
    thread_id_set.erase(folder_name.Utf8Value());
}

Napi::Boolean set_system_proxy_for_windows(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Expected an object parameter").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }

    Napi::Object obj = info[0].As<Napi::Object>();
    HttpProxy config;
    config.enabled = obj.Has("enabled") && obj.Get("enabled").As<Napi::Boolean>().Value();
    config.ip = obj.Has("ip") ? obj.Get("ip").As<Napi::String>().Utf8Value() : "";
    config.port = obj.Has("port") ? obj.Get("port").As<Napi::Number>().Int32Value() : 0;
    config.bypass = obj.Has("bypass") ? obj.Get("bypass").As<Napi::String>().Utf8Value() : "";
    config.useForLocal = obj.Has("useForLocal") && obj.Get("useForLocal").As<Napi::Boolean>().Value();

    bool success = setSystemProxyForWindows(config);
    return Napi::Boolean::New(env, success);
}


Napi::Object get_system_proxy_for_windows(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    HttpProxy proxy = getSystemProxyForWindows();

    Napi::Object obj = Napi::Object::New(env);
    obj.Set("enabled", Napi::Boolean::New(env, proxy.enabled));
    obj.Set("ip", Napi::String::New(env, proxy.ip));
    obj.Set("port", Napi::Number::New(env, proxy.port));
    obj.Set("bypass", Napi::String::New(env, proxy.bypass));
    obj.Set("useForLocal", Napi::Boolean::New(env, proxy.useForLocal));

    return obj;
}

// 转换 HttpProxy -> Napi::Object
Napi::Object HttpProxyToObject(Napi::Env env, const HttpProxy& proxy) {
    Napi::Object obj = Napi::Object::New(env);
    obj.Set("enabled", Napi::Boolean::New(env, proxy.enabled));
    obj.Set("ip", Napi::String::New(env, proxy.ip));
    obj.Set("port", Napi::Number::New(env, proxy.port));
    obj.Set("type", Napi::Number::New(env, proxy.type));
    // obj.Set("interfaceName", Napi::String::New(env, proxy.interfaceName));
    return obj;
}

// 转换 MacHttpProxy -> Napi::Object
Napi::Object MacHttpProxyToObject(Napi::Env env, const MacHttpProxy& proxy) {
    Napi::Object obj = Napi::Object::New(env);
    obj.Set("name", Napi::String::New(env, proxy.name));
    obj.Set("bypass", Napi::String::New(env, proxy.bypass));

    Napi::Array proxyArray = Napi::Array::New(env, proxy.proxies.size());
    for (size_t i = 0; i < proxy.proxies.size(); ++i) {
        proxyArray[i] = HttpProxyToObject(env, proxy.proxies[i]);
    }
    obj.Set("proxies", proxyArray);
    return obj;
}

// 获取 macOS 当前代理（返回单个对象，包含多个 HttpProxy）
Napi::Array get_system_proxy_for_mac(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    std::vector<MacHttpProxy> allProxies = getAllMacProxies();
    Napi::Array result = Napi::Array::New(env, allProxies.size());

    for (size_t i = 0; i < allProxies.size(); ++i) {
        result[i] = MacHttpProxyToObject(env, allProxies[i]);
    }

    return result;
}


// 设置 macOS 网络代理（接收 MacHttpProxy 对象）
Napi::Boolean set_system_proxy_for_mac(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // 参数必须是数组
    if (info.Length() < 1 || !info[0].IsArray()) {
        Napi::TypeError::New(env, "Expected an array of MacHttpProxy objects").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }

    Napi::Array inputArr = info[0].As<Napi::Array>();
    std::vector<MacHttpProxy> macProxies;

    for (uint32_t i = 0; i < inputArr.Length(); ++i) {
        Napi::Value val = inputArr[i];
        if (!val.IsObject()) continue;

        Napi::Object macObj = val.As<Napi::Object>();
        MacHttpProxy macProxy;

        // name 是必须的（如 Wi-Fi / Ethernet）
        macProxy.name = macObj.Has("name") ? macObj.Get("name").As<Napi::String>().Utf8Value() : "";
        macProxy.bypass = macObj.Has("bypass") ? macObj.Get("bypass").As<Napi::String>().Utf8Value() : "";

        // proxies 数组
        if (macObj.Has("proxies") && macObj.Get("proxies").IsArray()) {
            Napi::Array proxyArr = macObj.Get("proxies").As<Napi::Array>();
            for (uint32_t j = 0; j < proxyArr.Length(); ++j) {
                Napi::Value pVal = proxyArr[j];
                if (!pVal.IsObject()) continue;

                Napi::Object pObj = pVal.As<Napi::Object>();
                HttpProxy proxy;

                proxy.enabled = pObj.Has("enabled") && pObj.Get("enabled").As<Napi::Boolean>().Value();
                proxy.ip = pObj.Has("ip") ? pObj.Get("ip").As<Napi::String>().Utf8Value() : "";
                proxy.port = pObj.Has("port") ? pObj.Get("port").As<Napi::Number>().Int32Value() : 0;
                proxy.type = pObj.Has("type") ? pObj.Get("type").As<Napi::Number>().Int32Value() : 1;
                proxy.bypass = "";
                proxy.useForLocal = false;

                macProxy.proxies.push_back(proxy);
            }
        }

        macProxies.push_back(macProxy);
    }

    // 传给底层执行系统代理修改
    bool success = setMacProxies(macProxies);
    return Napi::Boolean::New(env, success);
}

// widnwos 下才注册的哈桑农户
#ifdef _WIN32
// windwos 下才注册的函数
Napi::Value LaunchAsUser(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "exe path required").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::u16string path = info[0].As<Napi::String>().Utf16Value();
    std::wstring exePath(path.begin(), path.end());

    bool result = LaunchProcessAsUser(exePath);
    return Napi::Boolean::New(env, result);
}
#endif

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    // 设置函数
    exports.Set(Napi::String::New(env, "on"),
                Napi::Function::New(env, on));
    exports.Set(Napi::String::New(env, "close"),
                Napi::Function::New(env, off));
    exports.Set(Napi::String::New(env, "pids"),
                Napi::Function::New(env, reset_pids));
    exports.Set(Napi::String::New(env, "set_print_second"),
                Napi::Function::New(env, set_print_second));
    exports.Set(Napi::String::New(env, "get_all_pid"),
              Napi::Function::New(env, get_all_pid));
    exports.Set(Napi::String::New(env, "kill_process"),
              Napi::Function::New(env, kill));
    exports.Set(Napi::String::New(env, "on_folder_size"),
              Napi::Function::New(env, on_folder_size));
    exports.Set(Napi::String::New(env, "stop_folder_size"),
              Napi::Function::New(env, stop_folder_size));
    exports.Set(Napi::String::New(env, "is_admin"),
              Napi::Function::New(env, is_admin));
    exports.Set(Napi::String::New(env, "get_system_proxy_for_windows"),
                Napi::Function::New(env, get_system_proxy_for_windows));
    exports.Set(Napi::String::New(env, "set_system_proxy_for_windows"),
                    Napi::Function::New(env, set_system_proxy_for_windows));
    exports.Set(Napi::String::New(env, "get_system_proxy_for_mac"),
            Napi::Function::New(env, get_system_proxy_for_mac));
    exports.Set(Napi::String::New(env, "set_system_proxy_for_mac"),
            Napi::Function::New(env, set_system_proxy_for_mac));

    #ifdef _WIN32
    // windwos 下才注册的函数
    exports.Set(
            "launch_process_as_user_for_win_service",
            Napi::Function::New(env, LaunchAsUser)
        );
    #endif

    return exports;
}


NODE_API_MODULE(get_process, Init)




