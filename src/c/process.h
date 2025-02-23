//
// Created by Administrator on 2024/5/12.
//

#include <node_api.h>
#include <napi.h>
#include <thread>
#include <stdio.h>
#include <set>
#include <map>
#include <iostream>
#include <string>

typedef struct process_pid_info
{
    unsigned long pid; // 进程ID
    unsigned long ppid; // 父进程
} process_pid_info;

void start();

void stop(std::string name);

void set_pids(std::string name, std::set<int> pid_set);

// 获取系统所有的进程id信息
void get_all_process_ids(std::vector<process_pid_info> & pid_set,unsigned long ppid);

void kill_process(unsigned long pid, bool kill_all_children = false);
