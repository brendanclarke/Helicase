#!/bin/sh
# S084 VX3 remediation §18.4: one specialised probe per insert type.
#
# What:       writes build/vx3_p_<type>.c, which includes the type's source and
#             wraps its process path (wet_only = 0, size 32) in a noinline
#             probe_<type>() function; compiles it with the firmware -Ofast
#             flags; looplen.py then reports the probe's loops. A loop that
#             is split by the compiler shows as several entries; the steady
#             path is the one the type runs per sample (see §18.4 table).
# Inputs:     $1 = ARM compiler command line (ARMCC ARMFLAGS).
# Accessors:  make -C tools/dsp_test armcheck-vx3.
set -e
CC="$1"
ROOT=../..
IE=$ROOT/Core/DSP/InsertEffects
probe() {
  f=$1; call=$2; b=$(basename "$f")
  printf '#include "%s.c"\n__attribute__((noinline)) void probe_%s(void *s, int16_t *buf, const insert_block_ctx_t *c) { %s; }\n' \
    "$IE/$f" "$b" "$call" > build/vx3_p_$b.c
  $CC -fno-unroll-loops -I$ROOT/Core/DSP/Effects -c build/vx3_p_$b.c -o build/vx3_p_$b.o
}
probe InsertDriveSrr 'driveSrr_process(s,buf,32,c)'
probe InsertFold 'fold_process(s,buf,32,c)'
probe InsertCrush 'crush_process(s,buf,32,c)'
probe InsertRing 'ring_process(s,buf,32,c)'
probe InsertPhaser 'phaser_process(s,buf,32,c)'
probe InsertFlanger 'flanger_run((flanger_state_t*)s,buf,32,c,0u)'
probe InsertChorus 'chorus_run((chorus_state_t*)s,buf,32,c,0u)'
probe InsertEcho 'echo_run((echo_state_t*)s,buf,32,c,0u)'
probe InsertComb 'comb_run(buf,32,c,0u)'
probe InsertRoom 'room_run((room_state_t*)s,buf,32,c,0u)'
probe Transient/TransientResonator 'resonator_run((resonator_state_t*)s,buf,32,c,0u)'
probe Transient/TransientPluck 'pluck_run((pluck_state_t*)s,buf,32,c,0u)'
probe Transient/TransientTube 'tube_run((tube_state_t*)s,buf,32,c,0u)'
probe Transient/TransientFlam 'flam_run(buf,32,c,0u)'
probe Transient/TransientStutter 'stutter_run((stutter_state_t*)s,buf,32,c,0u)'
probe Transient/TransientReverse 'reverse_run((reverse_state_t*)s,buf,32,c,0u)'
probe Transient/TransientSpring 'spring_run((spring_state_t*)s,buf,32,c,0u)'
