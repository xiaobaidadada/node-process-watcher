{
  "targets": [
    {
     "target_name": "node-process-watcher",
     "sources": ["src/c/main.cc","src/c/win_process.cc","src/c/linux_process.cc","src/c/process.h"],
     'includes': [
            './common.gypi'
          ],
     "cflags_cc": [ "-std=c++17" ],
    },
  ]
}
