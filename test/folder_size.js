const node_process_watcher = require("../build/Debug/node-process-watcher.node");


node_process_watcher.on_folder_size("E:\\files\\filecat",(num,size)=>{
    console.log(num,size);
    node_process_watcher.stop_folder_size("E:\\files\\filecat"); // 关闭
})

// node_process_watcher.on_folder_size("E:\\files\\filecat",(num,size)=>{
//     console.log(num,size);
//     // node_process_watcher.close("screen1"); // 关闭
// })
