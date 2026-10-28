'use strict';

const net = require('net');
const path = require('path');
const { spawn } = require('child_process');

const executable = process.argv[2] ? path.resolve(process.argv[2]) : '';
const program = process.argv[3] ? path.resolve(process.argv[3]) : '';
if (!executable || !program) {
  console.error('usage: node dap-smoke.js <kr16> <program.kex>');
  process.exit(2);
}

const port = 23153;
const child = spawn(executable, ['dap', '--port', String(port), program], {
  stdio: 'ignore',
  windowsHide: true
});

let exited = false;
child.on('error', (error) => {
  console.error('failed to start DAP server: ' + error.message);
  stop(1);
});
child.on('exit', (code) => {
  if (!exited) {
    console.error('DAP server exited early with code ' + code);
    stop(1);
  }
});
function stop(code) {
  if (!exited) {
    exited = true;
    try { child.kill(); } catch (_) { /* ignore */ }
  }
  process.exit(code);
}

function connectSocket() {
  return new Promise((resolve, reject) => {
    let attempts = 0;
    const attempt = () => {
      attempts += 1;
      const socket = net.connect({ host: '127.0.0.1', port });
      socket.once('connect', () => resolve(socket));
      socket.once('error', (error) => {
        socket.destroy();
        if (attempts >= 60) {
          reject(error);
        } else {
          setTimeout(attempt, 100);
        }
      });
    };
    attempt();
  });
}

function frame(value) {
  const body = JSON.stringify(value);
  return 'Content-Length: ' + Buffer.byteLength(body) + '\r\n\r\n' + body;
}

(async () => {
  try {
    const socket = await connectSocket();
    socket.on('error', () => stop(1));

    let buffer = '';
    const messages = [];
    let waiters = [];
    socket.on('data', (chunk) => {
      buffer += chunk.toString('utf8');
      for (;;) {
        const headerEnd = buffer.indexOf('\r\n\r\n');
        if (headerEnd < 0) break;
        const header = buffer.slice(0, headerEnd);
        const match = /Content-Length:\s*(\d+)/i.exec(header);
        if (!match) {
          console.error('missing Content-Length');
          stop(1);
        }
        const length = Number(match[1]);
        const bodyStart = headerEnd + 4;
        if (buffer.length - bodyStart < length) break;
        const body = buffer.slice(bodyStart, bodyStart + length);
        buffer = buffer.slice(bodyStart + length);
        const message = JSON.parse(body);
        messages.push(message);
        const pending = waiters;
        waiters = [];
        for (const waiter of pending) waiter();
      }
    });

    async function waitFor(predicate, label) {
      const deadline = Date.now() + 5000;
      for (;;) {
        const found = messages.find(predicate);
        if (found) return found;
        if (Date.now() > deadline) {
          console.error('timed out waiting for ' + label);
          stop(1);
        }
        await new Promise((resolve) => {
          waiters.push(resolve);
          setTimeout(resolve, 50);
        });
      }
    }

    socket.write(frame({
      seq: 1,
      type: 'request',
      command: 'initialize',
      arguments: { clientID: 'kr16-smoke', adapterID: 'kr16' }
    }));
    await waitFor((m) => m.type === 'event' && m.event === 'initialized',
                  'initialized event');

    socket.write(frame({
      seq: 2,
      type: 'request',
      command: 'launch',
      arguments: { program, stopOnEntry: true }
    }));
    await waitFor((m) => m.type === 'response' && m.command === 'launch',
                  'launch response');

    socket.write(frame({
      seq: 3,
      type: 'request',
      command: 'configurationDone',
      arguments: {}
    }));
    await waitFor((m) => m.type === 'response' &&
                         m.command === 'configurationDone',
                  'configurationDone response');
    await waitFor((m) => m.type === 'event' && m.event === 'stopped',
                  'stopped event');

    socket.write(frame({
      seq: 4,
      type: 'request',
      command: 'setFunctionBreakpoints',
      arguments: { breakpoints: [{ name: 'main' }] }
    }));
    const functionBreakpoints = await waitFor(
      (m) => m.type === 'response' && m.command === 'setFunctionBreakpoints',
      'setFunctionBreakpoints response');
    if (!functionBreakpoints.success || !functionBreakpoints.body ||
        !Array.isArray(functionBreakpoints.body.breakpoints) ||
        !functionBreakpoints.body.breakpoints[0].verified) {
      console.error('function breakpoint was not resolved');
      stop(1);
    }

    socket.write(frame({
      seq: 5,
      type: 'request',
      command: 'continue',
      arguments: { threadId: 1 }
    }));
    await waitFor((m) => m.type === 'response' && m.command === 'continue',
                  'continue response');
    await waitFor((m) => m.type === 'event' && m.event === 'stopped' &&
                         m.body && m.body.reason === 'breakpoint',
                  'function breakpoint stop');

    socket.write(frame({
      seq: 6,
      type: 'request',
      command: 'evaluate',
      arguments: { expression: '$pc', context: 'hover' }
    }));
    const evaluated = await waitFor((m) => m.type === 'response' &&
                                           m.command === 'evaluate',
                                     'evaluate response');
    if (!evaluated.success || !evaluated.body ||
        typeof evaluated.body.result !== 'string') {
      console.error('evaluate response is malformed');
      stop(1);
    }

    socket.write(frame({
      seq: 7,
      type: 'request',
      command: 'readMemory',
      arguments: { memoryReference: '0x0100', count: 4, offset: 0 }
    }));
    const memory = await waitFor((m) => m.type === 'response' &&
                                       m.command === 'readMemory',
                                 'readMemory response');
    if (!memory.success || !memory.body || typeof memory.body.data !== 'string') {
      console.error('readMemory response is malformed');
      stop(1);
    }

    socket.write(frame({
      seq: 8,
      type: 'request',
      command: 'disassemble',
      arguments: { memoryReference: '0x0100', instructionCount: 1 }
    }));
    const disassembly = await waitFor((m) => m.type === 'response' &&
                                           m.command === 'disassemble',
                                     'disassemble response');
    if (!disassembly.success || !disassembly.body ||
        !Array.isArray(disassembly.body.instructions) ||
        disassembly.body.instructions.length !== 1) {
      console.error('disassemble response is malformed');
      stop(1);
    }

    socket.write(frame({
      seq: 9,
      type: 'request',
      command: 'threads',
      arguments: {}
    }));
    const threads = await waitFor((m) => m.type === 'response' &&
                                        m.command === 'threads',
                                  'threads response');
    if (!threads.success || !threads.body || !Array.isArray(threads.body.threads)) {
      console.error('threads response is malformed');
      stop(1);
    }

    socket.write(frame({
      seq: 10,
      type: 'request',
      command: 'disconnect',
      arguments: {}
    }));
    await waitFor((m) => m.type === 'response' && m.command === 'disconnect',
                  'disconnect response');
    socket.end();
    console.log('DAP smoke passed');
    stop(0);
  } catch (error) {
    console.error(error && error.stack ? error.stack : String(error));
    stop(1);
  }
})();
