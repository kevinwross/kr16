'use strict';

const path = require('path');
const { runTests } = require('@vscode/test-electron');

async function main() {
  await runTests({
    vscodeExecutablePath:
      process.env.KR16_VSCODE_EXE ||
      'C:\\Users\\kevin\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe',
    extensionDevelopmentPath: path.resolve(__dirname, '..'),
    extensionTestsPath: path.resolve(__dirname, 'index.js'),
    launchArgs: ['--disable-workspace-trust', '--skip-welcome']
  });
}

main().catch((error) => {
  console.error(error && error.stack ? error.stack : String(error));
  process.exit(1);
});
