
#include "process.h"

std::map<std::string ,Napi::ThreadSafeFunction> name_tsfn;
std::set<std::string> name_set;
std::map<std::string,std::set<int>> name_pids;
int print_second = 1; // 默认打印间隔 1 秒

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
    return exports;
}

// 模块的入口点，node addon 就是写模块的
// 名字 初始化函数
NODE_API_MODULE(get_process, Init)




