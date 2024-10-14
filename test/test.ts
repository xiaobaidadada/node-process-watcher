import {node_process_monitor} from "../src";

node_process_monitor.on("screen1",(list)=>{
    console.log(list[0]);
    node_process_monitor.close("screen1"); // 关闭
})
