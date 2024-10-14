const test = require('./build/Release/node-process-monitor.node')
// test.close();
// test.pids("ok1",[]);
test.on((data)=>{
    console.log("ok1")
    // test.pids([4]);
    // console.log(data)
},"ok1",[3,4])
test.on((data)=>{
    console.log("ok2")
    // test.pids([4]);
    // console.log(data)
},"ok2",[1,2])
test.close("ok1");
test.pids("ok2",[3,4])
// test.close("ok2");
// test.on((data)=>{
// },[])
console.log(123)