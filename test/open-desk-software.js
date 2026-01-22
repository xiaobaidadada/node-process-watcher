const node_process_watcher = require("../build/Debug/node-process-watcher.node");
const path = require("path");


const p = "C:\\Users\\Administrator\\AppData\\Local\\Programs\\cursor\\Cursor.exe"
const a = node_process_watcher.launch_process_as_user_for_win_service(p,path.dirname(p))
console.log(a)

