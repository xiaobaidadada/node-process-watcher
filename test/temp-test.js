const node_process_watcher = require("../build/Release/node-process-watcher.node");

// const kk = node_process_watcher.get_all_processes()
// console.log(kk)

console.log(node_process_watcher.get_username_by_uid(0));
console.log(node_process_watcher.get_all_users());

console.log(node_process_watcher.get_file_owner('E:\\workspace\\github\\node-process-watcher\\test\\temp-test.js'))