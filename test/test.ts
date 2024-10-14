import {node_process_watcher} from "../src";

node_process_watcher.on("screen1",(list)=>{
    console.log(list[0]);
    node_process_watcher.close("screen1"); // 关闭
})
