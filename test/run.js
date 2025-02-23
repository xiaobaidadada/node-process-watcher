const node_process_watcher = require("../build/Debug/node-process-watcher.node");


node_process_watcher.on("screen1",(list)=>{
    console.log(list[0]);
    // node_process_watcher.close("screen1"); // 关闭
})
node_process_watcher.kill_process(7936,true);
const list = node_process_watcher.get_all_pid(7936);
console.log(list)