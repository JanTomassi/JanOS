#!/usr/bin/env python3
"""Host tests for the deterministic QEMU harness itself."""

import importlib.util
import os
import tempfile
from pathlib import Path


MODULE_PATH = Path(__file__).with_name('qemu_test.py')
SPEC = importlib.util.spec_from_file_location('janos_qemu_test', MODULE_PATH)
HARNESS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HARNESS)


def fake_qemu(body):
    handle = tempfile.NamedTemporaryFile(mode='w', delete=False)
    try:
        handle.write('#!/bin/sh\n' + body)
        path = handle.name
    finally:
        handle.close()
    os.chmod(path, 0o700)
    return path


def run(fake, *extra):
    return HARNESS.main([
        '--qemu', fake,
        '--iso', 'unused.iso',
        '--timeout', '1',
        '--label', 'harness-test',
        '--expect', 'JANOS:TEST:FAKE:PASS',
        *extra,
    ])


def main():
    passing = fake_qemu(
        'printf "JANOS:TEST:FAKE:PASS\\nJANOS:TEST:EXIT:0\\n"\nexit 1\n'
    )
    missing = fake_qemu('exit 1\n')
    wrong_status = fake_qemu('exit 3\n')
    no_exit_marker = fake_qemu('printf "JANOS:TEST:FAKE:PASS\\n"\nexit 1\n')
    hanging = fake_qemu('sleep 5\n')
    try:
        assert run(passing) == 0
        assert run(missing) != 0
        assert run(wrong_status) != 0
        assert run(no_exit_marker) != 0
        assert run(hanging, '--timeout', '0.05') != 0
    finally:
        for path in (passing, missing, wrong_status, no_exit_marker, hanging):
            os.unlink(path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
