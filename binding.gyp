{
  "targets": [
    {
     "target_name": "node-process-watcher",
     "sources": ["src/c/main.cc","src/c/win_process.cc"],
     'includes': [
            './common.gypi'
          ],
     "cflags_cc": [ "-std=c++17" ],
     'conditions': [
         ['OS=="mac"', {
           "sources": ["src/c/mac_process.cc"],
         }],
         ['OS=="linux"', {
           "sources": ["src/c/linux_process.cc"],
         }]
       ],
    },
  ]
}
