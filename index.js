// index.js
const path = require('path');

// 与 binding.gyp 中 product_name 的命名保持一致：win / linux
const platform = process.platform === 'win32' ? 'win' : process.platform;
const arch = process.arch; // 'x64' | 'arm64' | 'ia32' | 'arm'

if (platform === 'win') {
  // Windows：把 lib/win 加入 DLL 搜索路径
  const libDir = path.join(__dirname, 'lib', 'win');
  process.env.PATH = libDir + ';' + process.env.PATH;
} else if (platform === 'linux') {
  // Linux：把 lib/linux 加入库搜索路径
  const libDir = path.join(__dirname, 'lib', 'linux');
  process.env.LD_LIBRARY_PATH = libDir + ':' + (process.env.LD_LIBRARY_PATH || '');
} else {
  throw new Error(
    `@pwaos/runtime-node does not support platform "${process.platform}" yet. ` +
    `Supported platforms: win32, linux.`
  );
}

const binaryPath = path.join(
  __dirname,
  'build',
  'Release',
  `pwaos_runtime-${platform}-${arch}.node`
);

let addon;
try {
  addon = require(binaryPath);
} catch (err) {
  err.message =
    `Failed to load native addon at:\n  ${binaryPath}\n` +
    `Current runtime is ${process.platform}/${arch}. ` +
    `Make sure the package was built for this platform/arch (try "npm rebuild @pwaos/runtime-node").\n\n` +
    err.message;
  throw err;
}

module.exports = addon;