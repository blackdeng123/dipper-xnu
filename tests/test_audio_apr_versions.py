#!/usr/bin/env python3
"""Exercise the actual experimental APR callback with representative packets."""
import argparse
from pathlib import Path
import subprocess
import tempfile

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("kernel_source", type=Path)
    args = parser.parse_args()
    source = (args.kernel_source / "bsd/dev/mi8_rproc.c").read_text()
    start = source.index("struct audio_apr_header {")
    end = source.index("\nstatic int\naudio_glink_apr_send", start)
    fixture = r'''
    #include <assert.h>
    #include <stdbool.h>
    #include <stdint.h>
    #include <stddef.h>
    #include <string.h>
    #include "mi8-apr-client.h"
    static void say(const char *fmt, ...) { (void)fmt; }
    '''
    fixture += source[start:end]
    fixture += r'''
    int main(void) {
        struct audio_apr_probe answer = { 0 };
        struct { struct audio_apr_header h; uint32_t word; } packet = {
            .h = { .field = 0x250, .size = sizeof(packet), .token = 1, .opcode = 0x1290d },
            .word = 1
        };
        audio_apr_receive(&answer, &packet, sizeof(packet) - 1);
        assert(!answer.answered);
        audio_apr_receive(&answer, &packet, sizeof(packet));
        assert(answer.answered && answer.state == 1);
        packet.h.token = 1; packet.h.opcode = 0x12906;
        audio_apr_receive(&answer, &packet, sizeof(packet));
        assert(!answer.versions);
        packet.h.token = 2;
        audio_apr_receive(&answer, &packet, sizeof(packet));
        assert(answer.versions);
        return 0;
    }
    '''
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "apr.c"
        executable = Path(directory) / "apr"
        path.write_text(fixture)
        subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(args.kernel_source / "bsd/dev"), str(path), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        print("APR callback: truncated packet, state response and versions token validated")


if __name__ == "__main__":
    main()
