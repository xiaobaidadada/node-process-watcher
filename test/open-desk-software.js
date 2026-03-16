const {node_process_watcher} = require("../lib/index.js");
const path = require("path");


const p = "node.exe"
const args = "log.js"
const cwd = "E:\\workspace\\github\\node-process-watcher\\test"
const a = node_process_watcher.launch_process_as_user_for_win_service(p,args,cwd,1)
// console.log(a)

