'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const https = require('https');
const { execFileSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const SDK_DIR = path.join(ROOT, 'third_party', 'pwaos_sdk');
const MARKER = path.join(SDK_DIR, '.installed');

const VERSION = '0.2.a';
const RELEASE_TAG = `v${VERSION}`;

// 所有日志走 stderr，避免污染 gyp 读取的 stdout
function log(...args) {
  process.stderr.write('[pwaos-sdk] ' + args.join(' ') + '\n');
}

function getPlatformInfo() {
  switch (process.platform) {
    case 'win32':
      return {
        name: 'win-x86',
        url: `https://github.com/daqingzd/pwaos/releases/download/${RELEASE_TAG}/pwaos_sdk-win-x86-v${VERSION}.0.7z`,
      };
    case 'linux':
      return {
        name: 'linux-x64',
        url: `https://github.com/daqingzd/pwaos/releases/download/${RELEASE_TAG}/pwaos_sdk-linux-x64-v${VERSION}.0.7z`,
      };
    default:
      throw new Error(`Unsupported platform: ${process.platform}`);
  }
}

function download(url, dest) {
  return new Promise((resolve, reject) => {
    log(`Downloading ${url}`);
    const file = fs.createWriteStream(dest);

    const request = (u, depth) => {
      if (depth > 5) {
        reject(new Error('Too many redirects'));
        return;
      }
      https
        .get(u, (res) => {
          if (
            res.statusCode >= 300 &&
            res.statusCode < 400 &&
            res.headers.location
          ) {
            res.resume();
            request(res.headers.location, depth + 1);
            return;
          }
          if (res.statusCode !== 200) {
            res.resume();
            reject(new Error(`HTTP ${res.statusCode} for ${u}`));
            return;
          }
          res.pipe(file);
          file.on('finish', () => file.close(() => resolve()));
        })
        .on('error', (err) => {
          file.close(() => {
            try { fs.unlinkSync(dest); } catch (_) {}
            reject(err);
          });
        });
    };

    request(url, 0);
  });
}

function extract7z(archive, destDir) {
  let sevenZip;
  try {
    sevenZip = require('7zip-bin').path7za;
  } catch (_) {
    throw new Error(
      'Missing "7zip-bin" dependency. Run: npm install --save-dev 7zip-bin'
    );
  }

  log(`Extracting to ${destDir}`);
  fs.mkdirSync(destDir, { recursive: true });
  execFileSync(sevenZip, ['x', archive, `-o${destDir}`, '-y'], {
    stdio: ['ignore', 'ignore', 'inherit'],
  });
}

// 在解压目录中寻找包含 include/ 的 SDK 根目录
function findSdkRoot(dir) {
  if (fs.existsSync(path.join(dir, 'include'))) return dir;

  for (const entry of fs.readdirSync(dir)) {
    const full = path.join(dir, entry);
    if (
      fs.statSync(full).isDirectory() &&
      fs.existsSync(path.join(full, 'include'))
    ) {
      return full;
    }
  }
  return null;
}

async function main() {
  if (fs.existsSync(MARKER)) {
    process.stdout.write(SDK_DIR.replace(/\\/g, '/'));
    return;
  }

  const info = getPlatformInfo();
  log(`Platform: ${info.name}`);

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'pwaos-sdk-'));
  const archivePath = path.join(tmpDir, 'pwaos_sdk.7z');
  const extractDir = path.join(tmpDir, 'extracted');

  try {
    await download(info.url, archivePath);
    extract7z(archivePath, extractDir);

    const root = findSdkRoot(extractDir);
    if (!root) {
      throw new Error(
        'Downloaded archive does not contain an "include" directory'
      );
    }

    // 清理旧的 SDK 目录，把新内容放进最终位置
    if (fs.existsSync(SDK_DIR)) {
      fs.rmSync(SDK_DIR, { recursive: true, force: true });
    }
    fs.mkdirSync(path.dirname(SDK_DIR), { recursive: true });
    fs.cpSync(root, SDK_DIR, { recursive: true });

    // 写标记文件，避免下次重复下载
    fs.writeFileSync(MARKER, new Date().toISOString());

    // 验证结果
    if (!fs.existsSync(path.join(SDK_DIR, 'include'))) {
      throw new Error('SDK install failed: include/ not found');
    }

    // 把 SDK 路径输出到 stdout，供 binding.gyp 使用
    process.stdout.write(SDK_DIR.replace(/\\/g, '/'));
  } finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
}

main().catch((err) => {
  process.stderr.write(`[pwaos-sdk] ERROR: ${err.message}\n`);
  process.exit(1);
});