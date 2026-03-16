
const {node_process_watcher} = require("../lib/index.js");
const path = require("path");

// 在nssm下运行

const p = "node.exe"
const args = "log.js"
const cwd = "E:\\workspace\\github\\node-process-watcher\\test"
node_process_watcher.launch_process_as_user_for_win_service_console(p,args,cwd,(data)=>{
    console.log("11",data)
},()=>{
    console.log('ok')
})
// console.log(a)

