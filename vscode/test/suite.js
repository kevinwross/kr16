'use strict';

const assert = require('assert');
const vscode = require('vscode');

describe('KR16 extension', () => {
  it('launches a debug session through the DAP server', async function () {
    this.timeout(25000);
    const extension = vscode.extensions.getExtension('kr16.kr16-debug');
    assert.ok(extension, 'KR16 extension is not loaded');
    await extension.activate();

    const program = process.env.KR16_TEST_PROGRAM;
    const kr16Path = process.env.KR16_TEST_EXE;
    assert.ok(program, 'KR16_TEST_PROGRAM is not set');
    assert.ok(kr16Path, 'KR16_TEST_EXE is not set');

    const started = new Promise((resolve) => {
      const subscription = vscode.debug.onDidStartDebugSession((session) => {
        subscription.dispose();
        resolve(session);
      });
    });

    const ok = await vscode.debug.startDebugging(undefined, {
      type: 'kr16',
      request: 'launch',
      name: 'KR16 test',
      program,
      kr16Path,
      stopOnEntry: true
    });
    assert.ok(ok, 'startDebugging returned false');

    const session = await Promise.race([
      started,
      new Promise((_, reject) =>
        setTimeout(() => reject(new Error('debug session timeout')), 20000)
      )
    ]);
    assert.ok(session, 'no debug session');
    await vscode.commands.executeCommand('workbench.action.terminal.focus');
    await new Promise((resolve) => setTimeout(resolve, 4000));
    await vscode.debug.stopDebugging(session);
  });
});

