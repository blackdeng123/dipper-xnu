#!/usr/bin/env python3
"""Exercise the compiled timer request from the patched XNU source."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("kernel_source", type=Path)
args = parser.parse_args()
source = (args.kernel_source / "osfmk/arm64/loader_return.c").read_text()
start = source.index("static void\nloader_auto_reboot(")
end = source.index("\n}\n", start) + 2
callback = source[start:end]
assembly = '__asm__ volatile ("dc cvac, %0; dsb sy" : : "r" ((uintptr_t)&loader_live.stop) : "memory");'
assert callback.count(assembly) == 1
callback = callback.replace(assembly, "flushes++;")
harness = r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#define __unused __attribute__((unused))
typedef void *thread_call_param_t;
static struct { volatile uint64_t stop; } loader_live;
static int messages, flushes;
static void loader_log_puts(const char *text) { assert(text); messages++; }
''' + callback + r'''
int main(void) {
    loader_live.stop = 0; loader_auto_reboot(0, 0);
    assert(loader_live.stop == 2 && messages == 1 && flushes == 1);
    for (uint64_t existing = 1; existing <= 4; existing++) {
        loader_live.stop = existing; loader_auto_reboot(0, 0);
        assert(loader_live.stop == existing && messages == 1 && flushes == 1);
    }
    puts("automatic recovery timer: requests reboot once and preserves existing requests");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "timer.c").write_text(harness)
    subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", str(path / "timer.c"), "-o", str(path / "timer")], check=True)
    subprocess.run([str(path / "timer")], check=True)
