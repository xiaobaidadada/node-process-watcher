const node_process_watcher = require("../build/Debug/node-process-watcher.node");

// const pojo = node_process_watcher.get_system_proxy_for_mac()
// const wifi = pojo.filter(v => v.name === "Wi-Fi")
// console.log(JSON.stringify(wifi))

const wifi_u = {
    "name": "Wi-Fi",
    "bypass": ".qq.com,.molardata.com,.zhihu.com,.csdn.com,.xiaohei,.bing.com,.feishu.cn,14.103.0.0/16,.abaka.ai,.figma.com,.ai-transsion.com,.aliyuncs.com,192.168.41.57,.csdn.net,.xiaohei123.fun",
    "proxies": [
        {"enabled": false, "ip": "127.0.0.1", "port": 3067, "type": 1},
        {
        "enabled": true,
        "ip": "192.168.34.247",
        "port": 30002,
        "type": 2
    }]
}

// node_process_watcher.set_system_proxy_for_mac([wifi_u])
//
// let pojo1 = node_process_watcher.get_system_proxy_for_mac()
// let wifi1 = pojo1.filter(v => v.name === "Wi-Fi")
// console.log(JSON.stringify(wifi1))

console.log(node_process_watcher.is_admin())
