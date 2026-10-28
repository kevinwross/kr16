'use strict';

const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

function shellQuote(value) {
  if (process.platform === 'win32') {
    return '"' + String(value).replace(/"/g, '""') + '"';
  }
  return "'" + String(value).replace(/'/g, "'\\''") + "'";
}

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

class Kr16DescriptorFactory {
  constructor(context) {
    this.context = context;
  }

  async createDebugAdapterDescriptor(session) {
    const configuration = session.configuration || {};
    const program = configuration.program;
    if (!program) {
      throw new Error('KR16: the launch configuration needs a "program" path');
    }
    const port = Number(configuration.port || 22152);
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
      throw new Error('KR16: invalid DAP port');
    }

    if (configuration.launchAdapter !== false) {
      const executable = configuration.kr16Path || 'kr16';
      const terminal = vscode.window.createTerminal({
        name: 'KR16 DAP',
        cwd: vscode.workspace.workspaceFolders
          ? vscode.workspace.workspaceFolders[0].uri.fsPath
          : undefined
      });
      terminal.show(true);
      const args = Array.isArray(configuration.args) ? configuration.args : [];
      const readyFile = path.join(
        path.dirname(program),
        '.kr16-dap-' + process.pid + '-' + port + '-' + Date.now() + '.ready'
      );
      try {
        fs.unlinkSync(readyFile);
      } catch (_) {
        // Nothing to clean up.
      }
      const command = [
        shellQuote(executable),
        'dap',
        '--port',
        String(port),
        '--ready-file',
        shellQuote(readyFile),
        shellQuote(program),
        ...args.map(shellQuote)
      ].join(' ');
      const shell = (vscode.env.shell || '').toLowerCase();
      const needsCallOperator =
        process.platform === 'win32' &&
        (shell.includes('powershell') || shell.includes('pwsh'));
      terminal.sendText((needsCallOperator ? '& ' : '') + command, true);
      this.context.subscriptions.push(terminal);
      // The server writes a readiness marker after binding. Do not probe the
      // TCP port: the DAP server accepts exactly one client, so a probe would
      // consume the connection VS Code needs.
      let ready = false;
      for (let attempt = 0; attempt < 100; attempt += 1) {
        if (fs.existsSync(readyFile)) {
          ready = true;
          break;
        }
        await delay(100);
      }
      if (ready) {
        try {
          fs.unlinkSync(readyFile);
        } catch (_) {
          // The server may already have removed it.
        }
      } else {
        throw new Error(
          'KR16: DAP server did not become ready on 127.0.0.1:' +
            port +
            '; check the KR16 DAP terminal'
        );
      }
    }

    return new vscode.DebugAdapterServer(port, '127.0.0.1');
  }
}

function activate(context) {
  const factory = new Kr16DescriptorFactory(context);
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory('kr16', factory)
  );
  context.subscriptions.push(
    vscode.commands.registerCommand('kr16.debug', async () => {
      const workspace = vscode.workspace.workspaceFolders;
      if (!workspace || workspace.length === 0) {
        vscode.window.showErrorMessage('KR16: open a workspace first');
        return;
      }
      const folder = workspace[0];
      const suggested = vscode.Uri.joinPath(folder.uri, 'build', 'program.kex')
        .fsPath;
      const program = await vscode.window.showInputBox({
        prompt: 'Path to the KR16 .kex image',
        value: suggested
      });
      if (!program) {
        return;
      }
      const configuration = {
        type: 'kr16',
        request: 'launch',
        name: 'Debug KR16 program',
        program,
        stopOnEntry: true
      };
      await vscode.debug.startDebugging(folder, configuration);
    })
  );
}

function deactivate() {}

module.exports = { activate, deactivate };

