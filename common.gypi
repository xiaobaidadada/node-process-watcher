{
# 为什么要这个配置  https://github.com/nodejs/node-gyp/issues/26
  'cflags!': ['-fno-exceptions'],
  'cflags_cc!': ['-fno-exceptions'],
      "include_dirs": [
             "<!@(node -p \"require('node-addon-api').include\")",
      ],
      "dependencies": [
         "<!(node -p \"require('node-addon-api').gyp\")"
      ],
  'conditions': [
    ['OS=="mac"', {
      'cflags+': ['-fvisibility=hidden',"-std=c++17"],
      'xcode_settings': {
        'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',
        'CLANG_CXX_LIBRARY': 'libc++',
        'MACOSX_DEPLOYMENT_TARGET': '10.7',
        'GCC_SYMBOLS_PRIVATE_EXTERN': 'YES', # -fvisibility=hidden
        "OTHER_CPLUSPLUSFLAGS": [ "-std=c++17" ],
      }
    }],
    ['OS=="win"', { 
      'msvs_settings': {
        'VCCLCompilerTool': {
          'ExceptionHandling': 1,
          'AdditionalOptions': ['/source-charset:utf-8',"/std:c++17"]
        },
      },
      'defines':[
        '_HAS_EXCEPTIONS=1',
        'NOMINMAX'
      ]
    }]
  ],
  # node16 打包会出现的问题
  'variables' : {
      'openssl_fips': '',
  }
}
