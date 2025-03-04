const path = require('path');
const node_process_watcher = require('node-gyp-build')(path.join(__dirname,'..')) // 自动找到 .node
module.exports.node_process_watcher = node_process_watcher;

