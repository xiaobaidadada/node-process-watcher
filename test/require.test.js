
const {node_process_watcher} = require("../src")

describe('测试js',  function() {

    it('监听函数1', function() {
        node_process_watcher.on("screen1",(list)=>{
            console.log(list[0]);
            node_process_watcher.close("screen1"); // 关闭
        })
    });
    it('监听函数2', function() {
        node_process_watcher.on("screen2",(list)=>{
            console.log(list[1]);
            node_process_watcher.close("screen2");
        })
    });
    it('切换pids', function() {
        node_process_watcher.on("screen3",(list)=>{
            node_process_watcher.pids("screen3",[
                list[0].id
            ])
            if (list.length === 1) {
                node_process_watcher.close("screen3");
            }
        })
    });
});
