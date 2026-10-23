/* Crash report gate (idea taken from nxnative d47f899's crash.c, MIT, github.com/seedds/nxnative; the code is this project's own).
   Only the first crashing thread prints its state: when several threads crash together (job threads often hit the same bad data at once), their reports used to interleave into a mess.
   Later threads park in pause() until the first thread finishes printing and ends the process with _exit; if the first thread itself comes back in (another signal while printing), it exits at once.
   If printing hangs (reading something a suspended thread holds, for example), a SIGALRM after `seconds` seconds finishes the job: it writes one line and exits with 128 + the original signal, so the process never hangs.
   Call it only once a crash is certain to be reported: recoverable faults (reads guarded by runtime_fault_guard, dirty-page faults in the graphics layer, emulated instructions) must be handled and return before it,
   otherwise, while one thread is printing, another thread's recoverable fault would be parked as a crash. Uses only async-signal-safe calls.
   Crash report gate (idea from nxnative d47f899's crash.c, MIT, github.com/seedds/nxnative; the code is this project's own).
   Only the first crashing thread prints its state: when several threads crash together (job threads often run into the same bad
   data), their dumps used to interleave. Later threads stop in pause() until the first one finishes and ends the process with
   _exit; the first thread entering again (another signal while it prints) exits at once. A dump that stalls (reading something a
   suspended thread holds, say) is ended by a SIGALRM after `seconds`: one line, then exit with 128 + the original signal, so the
   process never hangs. Call it only once a crash is certain: recoverable faults (reads guarded by runtime_fault_guard, the graphics
   layer's dirty-page faults, emulated instructions) must be handled and returned from before it, or a recoverable fault on another
   thread would be stopped as a crash while one thread prints. Uses only async-signal-safe calls. */
#pragma once
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

static uintptr_t crash_gate_owner;   /* pthread_self() of the thread printing the crash, 0 = none */
static int crash_gate_signal;
static void crash_gate_alarm(int sig) {
    (void)sig;
    static const char note[] = "[totk] crash report stalled: exiting\n";
    ssize_t w = write(2, note, sizeof note - 1); (void)w;
    _exit(128 + __atomic_load_n(&crash_gate_signal, __ATOMIC_SEQ_CST));
}
/* Returns only in the first crashing thread; every other caller waits for the process to end or exits it. */
static void crash_gate(int sig, unsigned seconds) {
    uintptr_t me = (uintptr_t)pthread_self(), none = 0;
    if (__atomic_compare_exchange_n(&crash_gate_owner, &none, me, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        __atomic_store_n(&crash_gate_signal, sig, __ATOMIC_SEQ_CST);
        if (seconds) {
            struct sigaction a = {0}; a.sa_handler = crash_gate_alarm; sigemptyset(&a.sa_mask);
            sigaction(SIGALRM, &a, NULL);
            sigset_t s; sigemptyset(&s); sigaddset(&s, SIGALRM); pthread_sigmask(SIG_UNBLOCK, &s, NULL);
            alarm(seconds);
        }
        return;
    }
    if (none == me) _exit(128 + sig);   /* the printing thread itself, again */
    for (;;) pause();
}
