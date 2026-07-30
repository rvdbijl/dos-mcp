# DOS reentrancy rules

## Core rule

Packet-driver callbacks and hardware interrupt handlers do not call arbitrary DOS services. They copy the minimum validated metadata/data into preallocated storage, set a flag, and return.

## Contexts

### Interrupt or packet callback

Allowed:

- acknowledge/copy a bounded packet;
- update lock-free or interrupt-protected queue indices;
- record monotonic tick information;
- signal deferred work.

Forbidden:

- filesystem calls;
- allocation;
- command execution;
- unbounded loops;
- screen compression;
- network retransmission state machines;
- callbacks into code with unknown DOS behavior.

### Deferred observe mode

When the required state checks show a safe context, process status, text capture, bounded memory reads, and BIOS keyboard queue operations. A real implementation must inspect the DOS `InDOS` and critical-error state appropriate to the supported DOS versions.

### Foreground agent shell

Filesystem work and child-process execution occur only while a foreground companion owns the DOS context. The shell reports busy/child state so the bridge does not confuse request acceptance with completion.

The implemented `RAGENT.EXE` is this foreground companion. Its packet-driver
upcall is an assembly stub because Open Watcom's interrupt-parameter stack
layout is not the Turbo C layout used by the historical sample. The stub:

1. returns a fixed 1,518-byte buffer in `ES:DI` for the allocation upcall;
2. stores only `CX` and a ready flag for the completion upcall;
3. returns with `IRET`;
4. performs no DOS, protocol, or network work.

The main loop validates and handles the datagram after the callback returns.
Only one receive buffer is accepted at a time; additional frames are dropped
and recovered by bridge retries.

## Queue design requirements

- Fixed capacity selected from a documented memory budget.
- Fixed maximum request size in the callback path.
- Explicit full behavior: drop/negative acknowledgement without overwrite.
- Request IDs retained so duplicate mutations are not repeated.
- Producer/consumer indices updated atomically for 8088 code generation.
- Cancellation represented as state, not an unsafe asynchronous interruption.

## Keyboard notes

BIOS queue injection targets applications using `INT 16h` or DOS console input. It does not promise compatibility with software reading the keyboard controller directly. Check available BIOS buffer space before insertion and report partial acceptance rather than overrunning the queue.

## Execution notes

DOS does not provide modern process control. Command cancellation and exit status are capabilities, not assumptions. The foreground prototype accepts console commands only through the explicit keyboard tool; it does not advertise a direct execution operation. `system()` runs only from the foreground loop, never a callback. During a child command the network endpoint is unavailable and the bridge retries after the shell returns. Later direct execution work must establish:

- how `COMMAND.COM` is invoked or wrapped;
- how output is captured;
- how child completion is detected;
- which exit codes are reliable by DOS/version/program;
- what cancellation can safely mean.

## Verification still required

Test the chosen deferred-work strategy on each supported DOS family and packet driver. Do not infer safety solely from emulator behavior. Measure resident memory, maximum callback time, keyboard compatibility, and screen-copy latency on a genuine 4.77 MHz 8088.
