const node_process_watcher = require("../build/Debug/node-process-watcher.node");


node_process_watcher.on("screen1",(list)=>{
    console.log(list[0]);
    // node_process_watcher.close("screen1"); // 关闭
})

const list = node_process_watcher.get_all_pid(15448);
console.log(list)