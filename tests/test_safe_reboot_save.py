#!/usr/bin/env python3
"""Compile the actual patched save handshake with simulated saver responses."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("kernel_source", type=Path)
args = parser.parse_args()
source = (args.kernel_source / "bsd/dev/redwood_ufs.c").read_text()
start = source.index("int\nredwood_var_save_on_return(void)")
opening = source.index("{", start)
depth = 1
end = opening + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
function = source[start:end]
harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#define VAR_SAVE_NONE 0
#define VAR_SAVE_IDLE 1
#define VAR_SAVE_WANTED 2
#define VAR_SAVE_DONE 3
#define VAR_SAVE_FAILED 4
#define VAR_SAVE_MS 60
static int ufs_ready = 1, ufs_write_part = 0, ufs_var_save = VAR_SAVE_IDLE, answer;
static void PE_parse_boot_argn(const char *name, void *value, unsigned long size) { (void)name; (void)value; (void)size; }
static void IOSleep(int ms) { (void)ms; if (answer) ufs_var_save = answer; }
'''
harness += function
harness += r'''
int main(void) {
    answer = VAR_SAVE_DONE; ufs_var_save = VAR_SAVE_IDLE;
    assert(redwood_var_save_on_return() == 1);
    answer = VAR_SAVE_FAILED; ufs_var_save = VAR_SAVE_IDLE;
    assert(redwood_var_save_on_return() == 0);
    answer = 0; ufs_var_save = VAR_SAVE_IDLE;
    assert(redwood_var_save_on_return() == 0);
    ufs_var_save = VAR_SAVE_NONE;
    assert(redwood_var_save_on_return() == 0);
    puts("actual kernel save handshake: success, failure, timeout, missing saver passed");
}
'''
with tempfile.TemporaryDirectory() as directory:
    source_file = Path(directory) / "handshake.c"
    binary = Path(directory) / "handshake"
    source_file.write_text(harness)
    subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-format", str(source_file), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
