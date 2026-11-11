# How to Debug Binaries Compiled Outside KR16

Instructions for a compiler author whose output is a **flat `.bin` image** that
must run on `kr16.exe`.

- Tested against: `build\windows-ninja\kr16.exe`, `kr16 1.0.0 (ISA 1, container 1, host ABI 1, debug 1)`
  (Debug build, 2026-10-29). The Release build in `build\windows-ninja-release\`
  is older; prefer the Debug one while iterating.
- Commands verified: 2026-11-11T08:28:54-08:00
- Platform: Windows. On Linux the only differences are LF instead of CRLF on
  stdout and the AppImage path.
- Everything below was executed against that binary; the expected output shown
  is real, not illustrative.

---

## 0. TL;DR

```text
kr16 vm prog.bin --raw --base 0x0100 --no-bios
```

- Your file is copied **verbatim** to `--base`; there is no header, so the entry
  point **is** the base address.
- A raw binary gets **no libc**: `printf`, `malloc`, `stdin` do not exist. Write
  output bytes to `0xF000`, read input from `0xF004`, and end with `HLT` with the
  status in `R0`.
- The CLI prints the program's console output only **after it exits**, and on
  Windows every `0x0A` in the output arrives as `0D 0A`.
- Capture stdout as **bytes**, normalize CRLF, and use the process exit code.

If any of that surprises you, the rest of this document is the detail.

---

## 1. What the loader expects

### 1.1 Flat bytes, loaded at `--base`

`--raw` means: read the file, `memcpy` it to `base`, set `PC = base`. There is
no magic number, no header, no size field, no entry field.

### 1.2 Entry and load-address rules

| Situation | Behaviour |
|---|---|
| `--raw --base 0x0100` (defaults) | The embedded BIOS is loaded and `PC` starts at `0xF100`, whose only instruction jumps to `0x0100`. Costs 2 cycles and 1 instruction. |
| `--raw --base 0x0100 --no-bios` | `PC = 0x0100` directly. Same behaviour, cleaner cycle counts. **Recommended.** |
| `--raw --base 0x0200` | `PC = 0x0200` (the BIOS is skipped automatically because the entry is not `0x0100`). |
| `--raw --base < 0x0100` | Rejected: `kr16 vm: program entry is below the first page`, exit 1. The first page belongs to vectors and the BIOS jump table. |
| `--raw` without `--base` | Base is `0x0100`. |

So: **the base address is the entry point.** Put your first instruction there.

### 1.3 Address space you may use

```text
0x0000–0x00FF   vectors + BIOS jump table (not for your code)
0x0100–0xEFFF   general RAM; your image is loaded at `base`
0xE000–0xF000   4 KiB stack region (SP starts at 0xF000, grows down;
                SP below 0xE000 raises bus fault 5)
0xF000–0xF0FF   memory-mapped I/O (write bytes to 0xF000 for stdout)
0xF100–0xFFFF   BIOS ROM, not writable
```

Raw mode fixes the stack limit at `0xE000`, so a guest may use **4 KiB of
stack**. A loop that pushes 3,000 words halts with
`kr16 vm: unhandled exception 5`; that is the guard working, not a loader bug.

### 1.4 Size limits (there is no check)

`load_bytes` writes byte by byte with a 16-bit address, so a file that runs past
`0xFFFF` **wraps silently to `0x0000`** and overwrites the vector table. Keep
`base + size <= 0xE000` (and `<= 0x10000` in any case). Nothing will warn you.

### 1.5 Alignment

Instructions are fetched a word at a time but the machine tolerates odd
addresses; an odd word load/store merely costs one extra cycle. Keeping the
image and your data at even addresses is still the sane default.

---

## 2. Running it

### 2.1 Flags that matter

| Flag | Meaning |
|---|---|
| `--raw` | Load a flat binary. **Required** for your output. |
| `--base <addr>` | Load address and entry point. Default `0x0100`. Accepts `0x…`. |
| `--no-bios` | Start at the entry point instead of through the BIOS. |
| `--max-cycles <n>` | Abort after `n` cycles. Use this in every test. |
| `--input <path>` | Feed bytes to the keyboard device (see §3.2). |
| `--trace` | One line per executed instruction, on **stderr**. |
| `--trace-syscalls` | One line per `SYS` call, on **stderr**. |
| `--stats` | `cycles=… instructions=…`, on **stderr**. |
| `--fs-root`, `--fs-quota`, `--fs-readonly`, `--no-host-fs` | Host filesystem sandbox for `SYS` file calls. |
| `-- args…` | Guest arguments (see §3.4). |
| `--headless`, `--line-input` | Accepted; both only set an unused flag today. `--line-input` does **not** read stdin. |
| `--realtime`, `--mhz` | Pacing is approximate; irrelevant to tests. |

### 2.2 First, prove your harness with a known-good binary

Write these 20 bytes to `smoke.bin`:

```text
48 90 20 F0 00 F0 40 60 69 90 40 60 8A 11 40 60 87 11 00 88
```

`kr16 dis smoke.bin --raw --base 0x0100` must show:

```text
0x0100  0x9048       MOV R0, #0x48
0x0102  0xF020 0xF000  MOV R1, #0xF000
0x0106  0x6040       STB [R1], R0
0x0108  0x9069       MOV R0, #0x69
0x010A  0x6040       STB [R1], R0
0x010C  0x118A       MOV R0, #0xA
0x010E  0x6040       STB [R1], R0
0x0110  0x1187       MOV R0, #0x7
0x0112  0x8800       HLT
```

and

```text
kr16 vm smoke.bin --raw --no-bios
```

must exit with status **7** and write the four bytes `48 69 0D 0A` (`Hi\r\n`)
to stdout. If that does not happen, the problem is your harness, not your
compiler.

---

## 3. The stdio situation

A raw binary is freestanding. There is no `FILE`, no `printf`, no `scanf`, no
`stdin`/`stdout` objects. Console I/O is memory-mapped:

| Address | Access | Meaning |
|---|---|---|
| `0xF000` | write | Console: appends the byte to program output. |
| `0xF001` | read | TX ready, always `1` in the VM. Poll it if you want a handshake. |
| `0xF002` | read/write | Console echo: bit 0 makes the VM echo keyboard bytes into output. Default **off**. |
| `0xF004` | read | Keyboard data: pops the next byte, **`0` when empty**. |
| `0xF005` | read/write | Keyboard status: bit 0 = data available, bit 1 = overflow (write bit 1 to clear). |
| `0xF006` | read/write | Keyboard IRQ enable (bit 0). |

### 3.1 Output

- Output is one byte at a time: `STB [R1], R0` with `R1 = 0xF000`. `0x0A` is the
  only newline convention; there is no line discipline, no terminal, no escape
  processing.
- The VM **buffers** everything and the CLI writes it to stdout **after the run
  ends**. You cannot stream output, and a program that never halts shows nothing
  until it is killed — except that `--max-cycles` timeouts still flush the
  partial output (§3.3).
- On Windows, guest `0x0A` becomes `0D 0A` in the host pipe because the CLI's
  stdout is a text-mode stream. A program writing `A\n` produces the bytes
  `41 0D 0A`, and `41 0A` on Linux. **Normalize before comparing.**
- Bytes outside ASCII pass through unchanged: `C3 A9 80 FF` arrives as
  `C3 A9 80 FF`. Only `0x0A` is translated.

### 3.2 Input

`--input file` feeds the file's bytes into the keyboard queue, **one byte per
instruction**, and only when the queue is empty. A program must poll:

```asm
  MOV R2, #0xF004          ; keyboard data
  MOV R3, #0xF005          ; keyboard status
  MOV R4, #0xF000          ; console
poll:
  LDB R0, [R3]
  AND R0, #1
  BEQ done                 ; status bit 0 clear -> no more input
  LDB R0, [R2]
  STB [R4], R0
  BRA poll
done:
  MOV R0, #0
  HLT
```

Two traps:

1. **`0xF004` returns `0` for "empty"**, so a NUL byte in the input is
   indistinguishable from end-of-input. If you need binary-safe input, poll
   `0xF005` bit 0 instead of testing the data byte. Verified: with input
   `41 00 42`, the status-polling reader emits `41 00 42`, while a
   "read until 0" reader emits only `41`.
2. **No input at all** is not an error: status reads 0 and the reader exits
   immediately with whatever exit code it chooses.

`--line-input` sounds like line buffering; it is not. It sets an unused flag.
Feed files with `--input`, or write your own line editor over `0xF004`.

### 3.3 Exit status, timeouts and faults

| Observed | Meaning |
|---|---|
| any `R0 & 0xFF` | Guest executed `HLT`. `MOV R0,#0x1234; HLT` exits **52**. |
| `124` | `--max-cycles` reached — unless the guest itself exited 124. stderr: `kr16 vm: maximum cycle count reached`. |
| `1` | An unhandled exception **or** a loader/CLI failure. stderr distinguishes them. |
| `2` | Command-line usage error (bad flag or missing value). |

`124` is also a legal guest exit code, so do not rely on the number alone to
detect a timeout: check stderr for the message, or keep guest exit codes below
124.

Fault and loader messages are one line on stderr, exactly:

```text
kr16 vm: unhandled exception <n>
kr16 vm: invalid KR16 magic
kr16 vm: cannot open <path>
kr16 vm: program entry is below the first page
```

What `<n>` means is in §8.2. `invalid KR16 magic` almost always means `--raw`
was omitted.

Partial output produced before a fault or timeout **is** printed. That is the
cheapest way to find where a program died: whatever it printed before the
failure is in the captured stdout.

### 3.4 Arguments

In raw mode, without extra arguments, `R1` (argc) and `R2` (argv) are **0** and
`SP = 0xF000`.

With `-- args…`, the VM builds a process stack and sets `R1 = argc`,
`R2 = argv`, where `argv[0]` is the **image file name**:

```text
kr16 vm prog.bin --raw -- a b c    ->  R1 = 4   (prog.bin, a, b, c)
kr16 vm prog.bin --raw             ->  R1 = 0
```

Arguments are strings in guest memory, terminated by 0, and `argv` is an array
of 16-bit pointers. Only do this if your compiler understands the layout; a
bare binary does not have to use it.

### 3.5 Files, time and randomness

Everything else the host can do goes through the `SYS` instruction:

```asm
  MOV R0, #block      ; R0 points at a 16-byte, 2-byte-aligned block
  SYS #0              ; 0 = SYS_VERSION
```

Block layout (word offsets): `0 status`, `2 result0`, `4 result1`, `6 result2`,
`8 arg0`, `10 arg1`, `12 arg2`, `14 arg3`. Encoding is `0x8F00 | n`.

Verified `SYS_VERSION` on this build:

```text
stdout: 1
stderr: SYS #0 block=0x11e status=0 result0=1 result1=63
```

`result0` is the ABI version (1); `result1` is the capability bitmask (63 = all
of filesystem, directories, time, random, console descriptors, quota). Status
codes: `0` OK, `1` ENOENT, `2` EACCES, `5` EFAULT, `8` ENOSYS, … — the full
table is in [`systemservices.md`](systemservices.md).

Services: `0 SYS_VERSION`, `1 OPEN`, `2 CLOSE`, `3 READ`, `4 WRITE`,
`5 SEEK`, `6 STAT`, `7 UNLINK`, `8 RENAME`, `9 MKDIR`, `10 READDIR`,
`11 TIME`, `12 RANDOM`, `13 FSINFO`.

Paths are sandbox-relative with `/` separators. The default sandbox root is
`./sandbox`; `--fs-root` moves it, `--fs-readonly` denies writes, `--fs-quota`
caps usage (default 5 MiB), and `--no-host-fs` makes every file call return
`ENOSYS`.

---

## 4. Driving tests from the CLI

The contract is simple: **stdout = program output, stderr = diagnostics,
exit code = status**. Capture both, and capture stdout as raw bytes.

### 4.1 Python (verified)

```python
import subprocess

# Adjust to your checkout, or put kr16.exe on PATH.
KR16 = r"C:\Users\kevin\source\repos\kr16\build\windows-ninja\kr16.exe"

def run(image, args=(), cycles=2_000_000):
    cmd = [KR16, "vm", image, "--raw", "--base", "0x0100", "--no-bios",
           "--max-cycles", str(cycles), "--", *args]
    r = subprocess.run(cmd, capture_output=True)
    return r.returncode, r.stdout.replace(b"\r\n", b"\n"), r.stderr

code, out, err = run("smoke.bin")
assert code == 7, (code, err)
assert out == b"Hi\n", out
```

### 4.2 cmd.exe (verified)

```bat
kr16 vm smoke.bin --raw --base 0x0100 --no-bios > out.bin 2> err.txt
echo %ERRORLEVEL%
```

`cmd` redirection preserves bytes. Do **not** use PowerShell `>`: it captures
text lines and re-encodes them, which changes the byte stream.

### 4.3 Node (verified; file redirection, not pipes)

```js
const fs = require("fs");
const { spawnSync } = require("child_process");

const out = fs.openSync("out.bin", "w");
const err = fs.openSync("err.bin", "w");
const r = spawnSync(KR16, ["vm", image, "--raw", "--base", "0x0100", "--no-bios"],
                    { stdio: ["ignore", out, err] });
fs.closeSync(out); fs.closeSync(err);
const stdout = fs.readFileSync("out.bin");   // Buffer, e.g. <48 69 0d 0a>
const status = r.status;                     // 7
```

Pipes work on a normal desktop; file redirection also works where a sandbox
forbids pipe handles, so it is the safer default for automation.

### 4.4 Reading the result

| Channel | Contents | Notes |
|---|---|---|
| stdout | guest console bytes | LF is `0D 0A` on Windows |
| stderr | `kr16 vm: …` diagnostics, `--trace`, `--stats`, syscall trace | empty on a clean run |
| exit code | guest `R0 & 0xFF`, or `124` timeout, `1` fault, `2` usage | see §3.3 |

For a first failure the exit code plus the last stderr line is enough to decide
between "wrong output", "wrong exit", "fault" and "hang".

---

## 5. Debugging a failing binary

Work through these in order; each step narrows the problem.

1. **Hang or no output?** Add `--max-cycles 100000`. Exit 124 means the program
   is looping or waiting for input.
2. **Fault?** The exception number names the cause (§3.3). `5` is almost always
   stack overflow below `0xE000`; `0` is an illegal encoding or a jump into
   data; `2` is division by zero.
3. **Wrong output?** Dump the bytes and compare with what you meant to emit.
   Remember the CRLF translation.
4. **Need to see where it went?** `--trace` prints `pc=XXXX cycles=N` per
   instruction (hex PC) on stderr. Pair it with
   `kr16 dis prog.bin --raw --base 0x0100` to map PCs back to instructions.
5. **Performance or a suspicious loop?** `--stats` prints
   `cycles=N instructions=N` on stderr. Cycles per instruction differ by
   encoding; a branch is 1 not-taken / 2 taken, `SYS` is 10.
6. **Host calls?** `--trace-syscalls` prints one line per `SYS`, for example
   `SYS #0 block=0x11e status=0 result0=1 result1=63`. A status other than 0 is
   usually a bad block address or an argument pointer.

### 5.1 Symptom table

| Symptom | Likely cause | Next command |
|---|---|---|
| `invalid KR16 magic` | `--raw` missing, or you passed a `.kex` | add `--raw` |
| `program entry is below the first page` | `--base` below `0x0100` | use `0x0100` |
| exit 124, no output | infinite loop, or polling input that never arrives | `--trace`, check loop increment |
| exit 124, partial output | ran out of cycles, not crashed | raise `--max-cycles`, check termination |
| `unhandled exception 5` | stack deeper than 4 KiB | reduce frames, move data to static memory |
| `unhandled exception 0` | bad opcode, or PC ran into data/strings | `--trace` to find the PC |
| output garbled / program overwrites itself | image wraps past `0xFFFF` | shrink image or move `--base` |
| output missing the expected newline | you wrote the text bytes but not `0x0A` | write `0x0A` |
| `\r\n` where you expect `\n` | Windows text-mode stdout | normalize in the harness |
| exit 0 but no output | program ended before writing, or wrote to the wrong address | `--trace`, check `0xF000` |

### 5.2 What you cannot get from a raw binary

- No source lines, no symbols, no function names: those live in the `.kex`
  container's debug sections.
- The windowed debugger (`kr16` with a file) accepts `.kex`, `.c`, `.s`, `.asm`
  and `.o` — **not** `.bin`.
- `kr16 debug prog.bin` reports `invalid KR16 magic`; it reads containers.

Interactive stepping is still possible over DAP (§6), by address.

---

## 6. Interactive debugging over DAP

`kr16 dap` runs the VM behind a Debug Adapter Protocol server on TCP. It
understands raw binaries.

### 6.1 Start it

```text
kr16 dap prog.bin --raw --base 0x0100 --port 23461 --ready-file ready.txt
```

| Flag | Meaning |
|---|---|
| `--port <n>` | TCP port (default `22152`). |
| `--ready-file <path>` | After binding, writes the port number to the file. Wait for it instead of sleeping. |
| `--raw`, `--base` | Same meaning as for `vm`; the launch request can override them. |
| `--bios`, `--no-bios` | As for `vm`. |
| `--stop-on-entry` / `--no-stop-on-entry` | Whether to report a stopped event at the entry point (default: stop). |

### 6.2 Launch request

The client's `launch` arguments are passed to the session, so a raw binary can
be configured entirely from the client:

```json
{
  "type": "request",
  "command": "launch",
  "arguments": {
    "program": "C:/work/prog.bin",
    "raw": true,
    "base": 256,
    "noBios": true,
    "stopOnEntry": true,
    "args": ["one", "two"]
  }
}
```

`base` accepts an integer or a hex string. `args` become guest arguments
(§3.4). Verified: this works with or without `--raw --base` on the command line.

### 6.3 Verified session

```text
response initialize success=true
event initialized
response launch success=true
response configurationDone success=true
event stopped reason=entry
event kr16.stopped
response continue success=true
event exited code=7
event terminated
```

### 6.4 What the adapter supports

| Capability | Supported |
|---|---|
| `configurationDone`, `terminate` | yes |
| `readMemory`, `disassemble` | yes |
| `evaluate` for hovers | yes |
| function breakpoints | yes, but resolved by symbol name, so not useful for raw images |
| single-thread execution requests | yes |
| conditional breakpoints, hit counts | no |
| step back, restart | no |
| `setVariable` | no |
| delayed stack traces, loaded sources | no |

Two caveats for a compiler author:

1. **Guest console output does not travel over DAP.** The adapter writes the
   guest's bytes to **its own stdout** and clears them; there are no `output`
   events. Automated tests must use the CLI (§4), not the adapter, to capture
   program output.
2. **No source-level breakpoints.** A raw binary has no `.debug_line`, so
   `setBreakpoints` has no file/line to bind to. Use `continue`, `next`,
   `stepIn`, and the `readMemory`/`disassemble` requests.

For a full interactive experience (source lines, variables, call stack), the
binary must be a container with debug info — see §7.

---

## 7. Alternative: emit KR16 assembly and link the runtime

If your compiler can emit KR16 assembly instead of raw bytes, you get the
project's libc (verified end to end):

```text
kr16 build prog.s --libc -o prog.kex
kr16 vm prog.kex
```

`build` assembles the file, links `resources/libc.s`, and also compiles and
links `resources/fileio.c` (the C stdio layer). The manual pipeline is:

```text
kr16 as resources/libc.s -o libc.o
kr16 cc resources/fileio.c -c -I include -o fileio.o
kr16 as prog.s -o prog.o
kr16 ld libc.o fileio.o prog.o -o prog.kex
```

What that buys you: `printf` (`%d %x %s %c %%`), `puts`, `putchar`, `getchar`,
`strlen`/`strcpy`/`strcat`/`strcmp`/`memset`/`memcpy`, `atoi`, `abs`,
`malloc`/`calloc`/`realloc`/`free`, `exit`, and the unbuffered stdio functions
(`fopen`, `fread`, `fwrite`, …) over the sandbox.

Costs and conditions:

- You must follow the KR16 calling convention: arguments in `R1`–`R3` then the
  stack, result in `R0`, `R4`/`SP` callee-saved.
- Your source must define `_start` (the compiler-generated `.crt0` only exists
  for C input), or provide `main` and compile through `kr16 cc`.
- Every symbol from another file needs an `.extern` declaration. An undeclared
  symbol is silently treated as 0, which turns `CALL puts` into a call to the
  next instruction.
- The runtime reserves a 16 KiB heap in `.bss`, so images carry that footprint
  even if you never call `malloc`.

The payoff is that the `.kex` container keeps symbols and debug records, so the
windowed debugger and source-level DAP breakpoints work.

---

## 8. Quick reference

### 8.1 Exit codes

| Code | Meaning |
|---|---|
| `R0 & 0xFF` | guest executed `HLT`; any value 0–255 |
| 124 | `--max-cycles` timeout — but a guest may also exit 124, so check stderr |
| 1 | unhandled exception, unreadable image, or another loader error (stderr says which) |
| 2 | command-line usage error |

### 8.2 Exceptions

| Vector | Cause |
|---|---|
| 0 | illegal instruction |
| 1 | `BRK` |
| 2 | divide by zero |
| 3 | timer interrupt |
| 4 | keyboard interrupt |
| 5 | bus fault (including stack below `0xE000`) |

### 8.3 Console and input registers

`0xF000` write output byte · `0xF001` TX ready (1) · `0xF002` echo bit 0 ·
`0xF004` input byte (0 = empty) · `0xF005` status bit 0 available, bit 1
overflow · `0xF006` keyboard IRQ enable.

### 8.4 Minimal valid program

```asm
.text
.global _start
_start:
  MOV   R1, #0xF000
  MOV   R0, #72          ; 'H'
  STB   [R1], R0
  MOV   R0, #0
  HLT                    ; exit status 0
```

---

## Appendix A — Minimal DAP client (verified)

Save as `dap-probe.js`, run `node dap-probe.js <kr16.exe> <prog.bin>`.

```js
'use strict';
const net = require('net');
const path = require('path');
const { spawn } = require('child_process');

const exe = path.resolve(process.argv[2]);
const bin = path.resolve(process.argv[3]);
const port = 23461;

const child = spawn(exe, ['dap', '--raw', '--base', '0x0100',
  '--port', String(port), bin], { stdio: 'ignore', windowsHide: true });
let done = false;
const finish = (code) => {
  if (done) return;
  done = true;
  try { child.kill(); } catch (_) {}
  process.exit(code);
};

function connect() {
  return new Promise((resolve, reject) => {
    let tries = 0;
    const attempt = () => {
      const socket = net.connect({ host: '127.0.0.1', port });
      socket.once('connect', () => resolve(socket));
      socket.once('error', (err) => {
        socket.destroy();
        if (++tries >= 50) reject(err); else setTimeout(attempt, 100);
      });
    };
    attempt();
  });
}

const frame = (value) => {
  const body = JSON.stringify(value);
  return 'Content-Length: ' + Buffer.byteLength(body) + '\r\n\r\n' + body;
};

(async () => {
  const socket = await connect();
  let buffer = Buffer.alloc(0);
  const log = [];
  socket.on('data', (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    for (;;) {
      const end = buffer.indexOf('\r\n\r\n');
      if (end < 0) return;
      const length = Number(/Content-Length:\s*(\d+)/i.exec(
        buffer.slice(0, end).toString('latin1'))[1]);
      if (buffer.length < end + 4 + length) return;
      log.push(JSON.parse(buffer.slice(end + 4, end + 4 + length).toString('utf8')));
      buffer = buffer.slice(end + 4 + length);
    }
  });

  let seq = 1;
  const send = (command, args) =>
    socket.write(frame({ seq: seq++, type: 'request', command, arguments: args || {} }));

  send('initialize', { adapterID: 'kr16', linesStartAt1: true });
  await new Promise((r) => setTimeout(r, 200));
  send('launch', { program: bin, raw: true, base: 0x100, stopOnEntry: true });
  await new Promise((r) => setTimeout(r, 200));
  send('configurationDone', {});
  await new Promise((r) => setTimeout(r, 200));
  send('continue', { threadId: 1 });
  await new Promise((r) => setTimeout(r, 1500));

  for (const m of log) {
    if (m.type === 'response') console.log('response', m.command, 'success=' + m.success);
    if (m.type === 'event') console.log('event', m.event,
      m.event === 'exited' ? 'code=' + m.body.exitCode
        : m.event === 'stopped' ? 'reason=' + m.body.reason : '');
  }
  finish(0);
})().catch((e) => { console.error('probe failed: ' + e.message); finish(1); });
```

Expected transcript: `initialize`, `initialized`, `launch`, `configurationDone`,
`stopped reason=entry`, `continue`, `exited code=7`, `terminated`.

## Appendix B — The smoke binary

| Offset | Bytes | Instruction |
|---|---|---|
| `0x0100` | `48 90` | `MOV R0, #0x48` |
| `0x0102` | `20 F0 00 F0` | `MOV R1, #0xF000` |
| `0x0106` | `40 60` | `STB [R1], R0` |
| `0x0108` | `69 90` | `MOV R0, #0x69` |
| `0x010A` | `40 60` | `STB [R1], R0` |
| `0x010C` | `8A 11` | `MOV R0, #0xA` |
| `0x010E` | `40 60` | `STB [R1], R0` |
| `0x0110` | `87 11` | `MOV R0, #0x7` |
| `0x0112` | `00 88` | `HLT` |

Expected: stdout `Hi\r\n`, exit status 7.
