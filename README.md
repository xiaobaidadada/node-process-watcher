# node-process-watcher
A lightweight real-time monitoring system for all process information.
轻量级的实时监控系统上所有的进程信息。
# Example
```js
const {node_process_watcher} = require("node-process-watcher");
// or import {node_process_watcher} from "node-process-watcher";
node_process_watcher.on("screen1",(list)=>{
    // Print the information of all processes on the system every second
    // 每秒打印一次系统上所有进程的信息
    console.log(list[0]);
    // { id: 4, user_name: 'SYSTEM', cpu: 0, mem: 0, name: 'System' }
    node_process_watcher.close("screen1"); // 关闭
})
node_process_watcher.on("screen2",(list)=>{
    // Filter specific processes
    // 过滤特定的进程
    node_process_watcher.pids("screen2",[
        list[0].id
    ])
    console.log(list[0]);
    if (list.length === 1) {
        // Stop listening for information only after all the on events are closed
        // 所有的on关闭后才会停止监听信息上的信息
        node_process_watcher.close("screen2");
    }
})
```

# 说明
对于进程的内存信息，在windows下采集的是工作集(wss)大小（一段时间内进程所需要的内存页集合大小），在Linux下采集的是(rss)物理常驻集大小，表示所占用的所有物理上的内存大小包括共享内存。
##  支持环境
1. linux ✅ Yes
2. windows ✅ Yes
3. mac 🟨 Not
   目前在mac 无法安装(没有测试过)。本项目使用了预构建，建议使用Node18，不需要编译而是从github下载编译好的文件，如果你电脑上的网络安装的时候无法访问github则会退化成编译。在windows上编译可能遇到的问题可以参考这个链接https://blog.csdn.net/jjocwc/article/details/134152602
