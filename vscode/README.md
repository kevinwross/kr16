# KR16 VS Code Debugger

Minimal VS Code debug adapter integration for KR16.

## Launch configuration

```json
{
  "type": "kr16",
  "request": "launch",
  "name": "Debug KR16 program",
  "program": "${workspaceFolder}/build/program.kex",
  "stopOnEntry": true,
  "kr16Path": "kr16"
}
```

The extension launches `kr16 dap --port 22152 <program>` in an integrated
terminal, waits for the port to accept, and connects the VS Code debug client.
Program output stays in that terminal.

Set `"launchAdapter": false` to attach to an already-running `kr16 dap`
instance; the extension then only connects to the configured port.

The command palette also provides **KR16: Debug Program**, which asks for an
image path and starts a launch session.

## Install for development

Copy this directory into a VS Code extension development host, or package it:

```text
npx @vscode/vsce package
code --install-extension kr16-debug-0.1.0.vsix
```

The debugger supports initialize/launch/configurationDone, source and address
breakpoints, continue, pause, step in/over/out, stack frames, register scopes,
and disconnect. Memory/disassembly/evaluate views and function-entry
breakpoints are not implemented yet.
