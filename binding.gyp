{
  "variables": {
    "pwaos_sdk_path": "<!(node scripts/setup-sdk.js)"
  },
  "targets": [
    {
      "target_name": "pwaos_runtime",
      "sources": [
        "src/addon_main.cc",
        "src/app_obj.cc",
        "src/instance_obj.cc",
        "src/interface_obj.cc",
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "<(pwaos_sdk_path)/include"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "defines": [ "NAPI_VERSION=8" ],
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "conditions": [
        [
          "OS=='win'",
          {
            "product_name": "pwaos_runtime-win-<(target_arch)",
            "libraries": [
              "<(pwaos_sdk_path)/lib/win/pwaos_runtime_api.lib"
            ],
            "copies": [
              {
                "destination": "<(PRODUCT_DIR)",
                "files": [
                  "<(pwaos_sdk_path)/lib/win/pwaos_runtime_api.dll"
                ]
              }
            ],
            "msvs_settings": {
              "VCCLCompilerTool": {
                "ExceptionHandling": 1,
                "AdditionalOptions": ["/std:c++20", "/utf-8"]
              }
            }
          }
        ],
        [
          "OS=='linux'",
          {
            "product_name": "pwaos_runtime-linux-<(target_arch)",
            "libraries": [
              "-L<(pwaos_sdk_path)/lib",
              "-lpwaos_runtime_client",
              "-Wl,-rpath,<(pwaos_sdk_path)/lib/linux"
            ],
            "copies": [
              {
                "destination": "<(PRODUCT_DIR)",
                "files": [
                  "<(pwaos_sdk_path)/lib/linux/pwaos_runtime_api.so"
                ]
              }
            ],
            "cflags_cc": ["-std=c++20"]
          }
        ]
      ],
    }
  ]
}
