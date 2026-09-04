#!/usr/bin/env python3
"""Run a deterministic JanOS QEMU test and validate its guest exit status."""

import argparse
import os
import re
import signal
import subprocess
import sys


def parse_args(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', required=True)
    parser.add_argument('--iso', required=True)
    parser.add_argument('--disk', action='append', default=[])
    parser.add_argument('--smp', type=int, default=1)
    parser.add_argument('--timeout', type=float, default=30.0)
    parser.add_argument('--expect', action='append', default=[])
    parser.add_argument('--forbid', action='append', default=[])
    parser.add_argument('--expect-exit', type=int, default=0)
    parser.add_argument('--label', default='qemu-test')
    return parser.parse_args(argv)


def qemu_command(args):
    command = [
        args.qemu,
        '-machine', 'pc',
        '-cpu', 'qemu32',
        '-smp', str(args.smp),
        '-m', '1G',
        '-display', 'none',
        '-monitor', 'none',
        '-serial', 'stdio',
        '-no-reboot',
        '-snapshot',
        '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
    ]
    if args.disk:
        command.extend([
            '-drive', f'id=os_file,file={args.iso},format=raw,if=none',
            '-device', 'ahci,id=ahci',
            '-device', 'ide-hd,drive=os_file,bus=ahci.0',
        ])
        for index, disk in enumerate(args.disk, start=1):
            command.extend([
                '-drive', f'id=test_disk{index},file={disk},format=raw,if=none',
                '-device', f'ide-hd,drive=test_disk{index},bus=ahci.{index}',
            ])
    else:
        command.extend(['-drive', f'file={args.iso},format=raw,if=ide'])
    return command


def terminate_process(process):
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()


def run_qemu(command, timeout):
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors='replace',
        start_new_session=True,
    )
    timed_out = False
    output = ''
    try:
        output, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired as error:
        timed_out = True
        partial = error.output or ''
        if isinstance(partial, bytes):
            partial = partial.decode(errors='replace')
        terminate_process(process)
        remaining, _ = process.communicate()
        if isinstance(remaining, bytes):
            remaining = remaining.decode(errors='replace')
        output = partial + remaining
    return process.returncode, output.replace('\r\n', '\n').replace('\r', '\n'), timed_out


def main(argv=None):
    args = parse_args(argv)
    if args.smp < 1:
        print(f'{args.label}: --smp must be positive', file=sys.stderr)
        return 2
    command = qemu_command(args)
    returncode, output, timed_out = run_qemu(command, args.timeout)
    forbidden = [
        'KERNEL PANIC',
        'KERNEL BUG',
        'Unhandled exception',
        'User fault:',
        'Kernel page fault:',
    ] + args.forbid
    missing = [marker for marker in args.expect if marker not in output]
    present_forbidden = [marker for marker in forbidden if marker in output]
    decoded_exit = None
    if returncode is not None and returncode >= 0 and returncode & 1:
        decoded_exit = returncode >> 1
    exit_markers = re.findall(r'(?m)^JANOS:TEST:EXIT:(\d+)$', output)

    failure = []
    if timed_out:
        failure.append(f'timed out after {args.timeout:g}s')
    if returncode is None or returncode < 0:
        failure.append(f'QEMU terminated abnormally ({returncode})')
    if decoded_exit != args.expect_exit:
        failure.append(
            f'expected guest exit {args.expect_exit}, got {decoded_exit} '
            f'(QEMU status {returncode})'
        )
    if len(exit_markers) != 1:
        failure.append('expected exactly one JANOS:TEST:EXIT marker')
    elif int(exit_markers[0]) != decoded_exit:
        failure.append(
            f'guest exit marker {exit_markers[0]} does not match '
            f'QEMU status {decoded_exit}'
        )
    if missing:
        failure.append('missing markers: ' + ', '.join(missing))
    if present_forbidden:
        failure.append('forbidden markers: ' + ', '.join(present_forbidden))

    if failure:
        print(f'{args.label}: FAIL: ' + '; '.join(failure), file=sys.stderr)
        print(f'{args.label}: QEMU command: {subprocess.list2cmdline(command)}',
              file=sys.stderr)
        print(f'{args.label}: serial output:', file=sys.stderr)
        print(output, file=sys.stderr, end='' if output.endswith('\n') else '\n')
        return 1

    print(f'{args.label}: PASS (guest exit {decoded_exit})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
