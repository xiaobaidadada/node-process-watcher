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

void start();

void stop(std::string name);

void set_pids(std::string name,std::set<int> pid_set);

