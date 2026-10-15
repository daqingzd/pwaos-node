#!/usr/bin/env node
// index.js
'use strict';

const path = require('path');
const fs = require('fs');

const PLATFORM = process.platform === 'win32' ? 'win' : process.platform

// ---------- 默认配置 ----------
const DEFAULTS = {
  addon:path.join(
    __dirname,
    '..',
    'build',
    'Release',
    `pwaos_runtime-${PLATFORM}-${process.arch}.node`
  ),
  runtime: path.join(__dirname, '..', 'third_party', 'pwaos_sdk', 'runtime'),
  url:     'https://www.pwaos.net',
};

// ---------- 小工具 ----------
const isUrlLike = (s) => /^[a-z][a-z0-9+.-]*:\/\//i.test(s) || /^about:/i.test(s);
const sleep     = (ms) => new Promise((r) => setTimeout(r, ms));
const log       = (msg) => console.log(`[pwaos] ${msg}`);
const err       = (msg) => console.error(`[pwaos] ❌ ${msg}`);

function printUsage() {
  console.log(`
Usage:
  node index.js [options] [url]

Options:
  -a, --addon <path>     Path to pwaos_runtime.node addon
                         (env PWAOS_ADDON_PATH, default "${DEFAULTS.addon}")
  -r, --runtime <path>   Path to runtime executable
                         (env PWAOS_RUNTIME_EXEC, default "${DEFAULTS.runtime}")
  -u, --url <url>        URL to open (env PWAOS_URL)
  -h, --help             Show this help

Examples:
  node index.js https://www.baidu.com
  node index.js --url https://example.com
  node index.js -a ./pwaos_runtime.node -r ./runtime -u https://example.com
`);
}

// ---------- 参数解析 ----------
function parseArgs(argv) {
  const opts = { addon: null, runtime: null, url: null };
  const positional = [];

  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    switch (a) {
      case '-a': case '--addon':   opts.addon   = argv[++i]; break;
      case '-r': case '--runtime': opts.runtime = argv[++i]; break;
      case '-u': case '--url':     opts.url     = argv[++i]; break;
      case '-h': case '--help':    printUsage(); process.exit(0);
      default:                     positional.push(a);
    }
  }

  // 位置参数：URL 优先识别，其余按 addon / runtime 顺序填充
  for (const p of positional) {
    if (isUrlLike(p))       { if (!opts.url)     opts.url     = p; }
    else if (!opts.addon)   { opts.addon   = p; }
    else if (!opts.runtime) { opts.runtime = p; }
  }

  // 环境变量与默认值回退
  opts.addon   = opts.addon   || process.env.PWAOS_ADDON_PATH   || DEFAULTS.addon;
  opts.runtime = opts.runtime || process.env.PWAOS_RUNTIME_EXEC || DEFAULTS.runtime;
  opts.url     = opts.url     || process.env.PWAOS_URL          || DEFAULTS.url;

  return opts;
}

// ---------- 主流程 ----------
async function main() {
  const opts = parseArgs(process.argv.slice(2));

  const addonAbsolute   = path.resolve(__dirname, opts.addon);
  const runtimeAbsolute = path.resolve(__dirname, opts.runtime);

  if (!fs.existsSync(addonAbsolute)) {
    err(`Addon file not found: ${addonAbsolute}`);
    process.exit(1);
  }
  if (!fs.existsSync(runtimeAbsolute)) {
    err(`Runtime executable not found: ${runtimeAbsolute}`);
    process.exit(1);
  }

  log(`Addon   : ${addonAbsolute}`);
  log(`Runtime : ${runtimeAbsolute}`);
  log(`URL     : ${opts.url}`);

  // ---------- 加载原生模块 ----------
  let pwaos;
  try {
    pwaos = require(addonAbsolute);
  } catch (e) {
    err(`Failed to load addon: ${e.message}`);
    process.exit(1);
  }

  let currentInstance = null;
  let currentApp = null;
  let shuttingDown = false;

  function shutdown(code = 0) {
    if (shuttingDown) return;
    shuttingDown = true;
    try {
      if (currentApp && currentInstance) {
        currentApp.setCallback(null, null);
        currentInstance.destroyPwa(currentApp);
      }
      currentApp = null;
      if (currentInstance) {
        pwaos.destroyInstance(currentInstance);
      }
      currentInstance = null;
      pwaos.shutDown();
    } catch (e) {
      err(`Error during shutdown: ${e && e.stack ? e.stack : e}`);
    }
    log('Shutdown complete.');
    process.exit(code);
  }

  // 收到中断信号时优雅退出
  process.on('SIGINT',  () => { log('SIGINT received.');  shutdown(0); });
  process.on('SIGTERM', () => { log('SIGTERM received.'); shutdown(0); });

  pwaos.setRuntimeExecutablePath(runtimeAbsolute);

  try {
    pwaos.init().then(function (result){
      log(`pwaos_init succeeded. result code = ${result}`);

      const createInfo = {
        type:        pwaos.PWAOS_TYPE_INSTANCE_CREATE_INFO,
        runtimeType: pwaos.runtimeTypePwa,
        serverUrl:   'http://localhost',
        dataPath:    '', // 空串 = 使用默认路径
      };
      currentInstance = pwaos.createInstance(createInfo);

      let bQuit = false;
      while (!bQuit) {
        const ev = currentInstance.pollEvent();
        if (!ev) {
          sleep(1); // 让出事件循环，避免忙等
          continue;
        }

        log(`event: ${ev}`);

        if (ev.type === pwaos.PWAOS_TYPE_EVENT_DATA_INSTANCE_STATUS_CHANGED) {
          if (ev.status === pwaos.Instance.instanceStatusWorking) {
            log(`properties: ${JSON.stringify(currentInstance.getProperties())}`);
            const createPwaInfo = {
              type:    pwaos.PWAOS_TYPE_PWA_CREATE_INFO,
              appType: pwaos.App.appTypeBrowser,
              appUrl:  opts.url,           // ← 使用命令行传入的 URL
              width:   800,
              height:  600,
            };
            currentApp = currentInstance.createPwa(createPwaInfo);
          }
        } else if (ev.type === pwaos.PWAOS_TYPE_EVENT_DATA_APP_STATUS_CHANGED) {
          if (ev.status === pwaos.App.appStatusStopping) {
            bQuit = true;
          }
        }
      }

      shutdown(0);
    });
  } catch (e) {
    err(`pwaos_init failed: ${e && e.stack ? e.stack : e}`);
    shutdown(1);
  }
}

// ---------- 入口 ----------
main().catch((e) => {
  console.error('[pwaos] fatal:', e);
  process.exit(1);
});

console.log('load index.js completely!!!');
