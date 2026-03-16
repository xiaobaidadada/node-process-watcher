const {node_process_watcher} = require("../lib/index.js");
const path = require("path");


const p = "C:\\Users\\Administrator\\AppData\\Roaming\\Upupoo\\upupoo-main.exe"
const args = ""
const cwd = "E:\\workspace\\github\\node-process-watcher\\test"
const a = node_process_watcher.launch_process_as_user_for_win_service(p,args,cwd)
console.log(a)

