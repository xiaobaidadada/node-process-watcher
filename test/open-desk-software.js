const node_process_watcher = require("../build/Debug/node-process-watcher.node");

const a = node_process_watcher.launch_process_as_user_for_win_service("C:\\Users\\Administrator\\AppData\\Local\\Programs\\cursor\\Cursor.exe")
console.log(a)

