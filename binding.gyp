{
  "targets": [
    {
     "target_name": "node-process-watcher",
     "sources": ["src/c/main.cc"],
     'includes': [
            './common.gypi'
          ],
     "cflags_cc": [ "-std=c++17" ],
     'conditions': [
         ['OS=="win"', {
           "sources": ["src/c/win_process.cc"],
         }],
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
