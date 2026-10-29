/*
 * getconf.c - getconf: get configuration values (POSIX.1-2024, XCU getconf)
 *
 * Synopsis:
 *   getconf [-v specification] system_var
 *   getconf [-v specification] path_var pathname
 *
 * A system_var is a variable of sysconf() (the names of its table, without
 * the braces, but for the ones the standard leaves out), NPROCESSORS_CONF
 * and NPROCESSORS_ONLN, a name of confstr() without _CS_, or a maximum or
 * minimum value of <limits.h>; a path_var is a variable of pathconf().
 * The earlier names POSIX2_C_BIND ... POSIX2_VERSION and POSIX2_BC_BASE_MAX
 * ... POSIX2_RE_DUP_MAX are those with a leading underscore.  The values
 * are written with "%ld\n" (strings with "%s\n"), or "undefined\n".
 *
 * Choices where the standard leaves room:
 *  - A variable whose function returns -1 without setting errno or with
 *    EINVAL (and one the system has no name for) is "undefined", options
 *    included; a
 *    string variable whose value is empty is written as an empty line.
 *  - A variable of a V8 compilation environment (_POSIX_V8_LP64_OFF64,
 *    POSIX_V8_LP64_OFF64_CFLAGS, V8_ENV, ...) that the system has no name
 *    for has the value of the V7 one: the environments are the same.
 *  - -v takes the names of the compilation environments of c17,
 *    POSIX_V8_ILP32_OFF32, POSIX_V8_ILP32_OFFBIG, POSIX_V8_LP64_OFF64 and
 *    POSIX_V8_LPBIG_OFFBIG (and those of V7).  The values are those of the
 *    environment getconf was compiled in; for another one it cannot
 *    determine them, and that is an error (status 1).
 *  - A system_var with a pathname, a path_var without one, and an unknown
 *    name are errors (status 1, nothing written); usage errors have
 *    status 2.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define NONE (-1000)            /* a name the system does not define */

struct var {
    const char *name;
    int code;
};

struct limit {
    const char *name;
    long value;
};

/* the system variables: sysconf() */
static const struct var sysvars[] = {
#ifdef _SC_AIO_LISTIO_MAX
    { "AIO_LISTIO_MAX", _SC_AIO_LISTIO_MAX },
#else
    { "AIO_LISTIO_MAX", NONE },
#endif
#ifdef _SC_AIO_MAX
    { "AIO_MAX", _SC_AIO_MAX },
#else
    { "AIO_MAX", NONE },
#endif
#ifdef _SC_AIO_PRIO_DELTA_MAX
    { "AIO_PRIO_DELTA_MAX", _SC_AIO_PRIO_DELTA_MAX },
#else
    { "AIO_PRIO_DELTA_MAX", NONE },
#endif
#ifdef _SC_ARG_MAX
    { "ARG_MAX", _SC_ARG_MAX },
#else
    { "ARG_MAX", NONE },
#endif
#ifdef _SC_ATEXIT_MAX
    { "ATEXIT_MAX", _SC_ATEXIT_MAX },
#else
    { "ATEXIT_MAX", NONE },
#endif
#ifdef _SC_BC_BASE_MAX
    { "BC_BASE_MAX", _SC_BC_BASE_MAX },
#else
    { "BC_BASE_MAX", NONE },
#endif
#ifdef _SC_BC_DIM_MAX
    { "BC_DIM_MAX", _SC_BC_DIM_MAX },
#else
    { "BC_DIM_MAX", NONE },
#endif
#ifdef _SC_BC_SCALE_MAX
    { "BC_SCALE_MAX", _SC_BC_SCALE_MAX },
#else
    { "BC_SCALE_MAX", NONE },
#endif
#ifdef _SC_BC_STRING_MAX
    { "BC_STRING_MAX", _SC_BC_STRING_MAX },
#else
    { "BC_STRING_MAX", NONE },
#endif
#ifdef _SC_CHILD_MAX
    { "CHILD_MAX", _SC_CHILD_MAX },
#else
    { "CHILD_MAX", NONE },
#endif
#ifdef _SC_COLL_WEIGHTS_MAX
    { "COLL_WEIGHTS_MAX", _SC_COLL_WEIGHTS_MAX },
#else
    { "COLL_WEIGHTS_MAX", NONE },
#endif
#ifdef _SC_DELAYTIMER_MAX
    { "DELAYTIMER_MAX", _SC_DELAYTIMER_MAX },
#else
    { "DELAYTIMER_MAX", NONE },
#endif
#ifdef _SC_EXPR_NEST_MAX
    { "EXPR_NEST_MAX", _SC_EXPR_NEST_MAX },
#else
    { "EXPR_NEST_MAX", NONE },
#endif
#ifdef _SC_HOST_NAME_MAX
    { "HOST_NAME_MAX", _SC_HOST_NAME_MAX },
#else
    { "HOST_NAME_MAX", NONE },
#endif
#ifdef _SC_IOV_MAX
    { "IOV_MAX", _SC_IOV_MAX },
#else
    { "IOV_MAX", NONE },
#endif
#ifdef _SC_LINE_MAX
    { "LINE_MAX", _SC_LINE_MAX },
#else
    { "LINE_MAX", NONE },
#endif
#ifdef _SC_LOGIN_NAME_MAX
    { "LOGIN_NAME_MAX", _SC_LOGIN_NAME_MAX },
#else
    { "LOGIN_NAME_MAX", NONE },
#endif
#ifdef _SC_MQ_OPEN_MAX
    { "MQ_OPEN_MAX", _SC_MQ_OPEN_MAX },
#else
    { "MQ_OPEN_MAX", NONE },
#endif
#ifdef _SC_MQ_PRIO_MAX
    { "MQ_PRIO_MAX", _SC_MQ_PRIO_MAX },
#else
    { "MQ_PRIO_MAX", NONE },
#endif
#ifdef _SC_NGROUPS_MAX
    { "NGROUPS_MAX", _SC_NGROUPS_MAX },
#else
    { "NGROUPS_MAX", NONE },
#endif
#ifdef _SC_NPROCESSORS_CONF
    { "NPROCESSORS_CONF", _SC_NPROCESSORS_CONF },
#else
    { "NPROCESSORS_CONF", NONE },
#endif
#ifdef _SC_NPROCESSORS_ONLN
    { "NPROCESSORS_ONLN", _SC_NPROCESSORS_ONLN },
#else
    { "NPROCESSORS_ONLN", NONE },
#endif
#ifdef _SC_OPEN_MAX
    { "OPEN_MAX", _SC_OPEN_MAX },
#else
    { "OPEN_MAX", NONE },
#endif
#ifdef _SC_PAGESIZE
    { "PAGESIZE", _SC_PAGESIZE },
#else
    { "PAGESIZE", NONE },
#endif
#ifdef _SC_PAGE_SIZE
    { "PAGE_SIZE", _SC_PAGE_SIZE },
#else
    { "PAGE_SIZE", NONE },
#endif
#ifdef _SC_THREAD_DESTRUCTOR_ITERATIONS
    { "PTHREAD_DESTRUCTOR_ITERATIONS", _SC_THREAD_DESTRUCTOR_ITERATIONS },
#else
    { "PTHREAD_DESTRUCTOR_ITERATIONS", NONE },
#endif
#ifdef _SC_THREAD_KEYS_MAX
    { "PTHREAD_KEYS_MAX", _SC_THREAD_KEYS_MAX },
#else
    { "PTHREAD_KEYS_MAX", NONE },
#endif
#ifdef _SC_THREAD_STACK_MIN
    { "PTHREAD_STACK_MIN", _SC_THREAD_STACK_MIN },
#else
    { "PTHREAD_STACK_MIN", NONE },
#endif
#ifdef _SC_THREAD_THREADS_MAX
    { "PTHREAD_THREADS_MAX", _SC_THREAD_THREADS_MAX },
#else
    { "PTHREAD_THREADS_MAX", NONE },
#endif
#ifdef _SC_RE_DUP_MAX
    { "RE_DUP_MAX", _SC_RE_DUP_MAX },
#else
    { "RE_DUP_MAX", NONE },
#endif
#ifdef _SC_RTSIG_MAX
    { "RTSIG_MAX", _SC_RTSIG_MAX },
#else
    { "RTSIG_MAX", NONE },
#endif
#ifdef _SC_SEM_NSEMS_MAX
    { "SEM_NSEMS_MAX", _SC_SEM_NSEMS_MAX },
#else
    { "SEM_NSEMS_MAX", NONE },
#endif
#ifdef _SC_SEM_VALUE_MAX
    { "SEM_VALUE_MAX", _SC_SEM_VALUE_MAX },
#else
    { "SEM_VALUE_MAX", NONE },
#endif
#ifdef _SC_SIGQUEUE_MAX
    { "SIGQUEUE_MAX", _SC_SIGQUEUE_MAX },
#else
    { "SIGQUEUE_MAX", NONE },
#endif
#ifdef _SC_STREAM_MAX
    { "STREAM_MAX", _SC_STREAM_MAX },
#else
    { "STREAM_MAX", NONE },
#endif
#ifdef _SC_SYMLOOP_MAX
    { "SYMLOOP_MAX", _SC_SYMLOOP_MAX },
#else
    { "SYMLOOP_MAX", NONE },
#endif
#ifdef _SC_TIMER_MAX
    { "TIMER_MAX", _SC_TIMER_MAX },
#else
    { "TIMER_MAX", NONE },
#endif
#ifdef _SC_TTY_NAME_MAX
    { "TTY_NAME_MAX", _SC_TTY_NAME_MAX },
#else
    { "TTY_NAME_MAX", NONE },
#endif
#ifdef _SC_TZNAME_MAX
    { "TZNAME_MAX", _SC_TZNAME_MAX },
#else
    { "TZNAME_MAX", NONE },
#endif
#ifdef _SC_2_CHAR_TERM
    { "_POSIX2_CHAR_TERM", _SC_2_CHAR_TERM },
#else
    { "_POSIX2_CHAR_TERM", NONE },
#endif
#ifdef _SC_2_C_BIND
    { "_POSIX2_C_BIND", _SC_2_C_BIND },
#else
    { "_POSIX2_C_BIND", NONE },
#endif
#ifdef _SC_2_C_DEV
    { "_POSIX2_C_DEV", _SC_2_C_DEV },
#else
    { "_POSIX2_C_DEV", NONE },
#endif
#ifdef _SC_2_FORT_RUN
    { "_POSIX2_FORT_RUN", _SC_2_FORT_RUN },
#else
    { "_POSIX2_FORT_RUN", NONE },
#endif
#ifdef _SC_2_LOCALEDEF
    { "_POSIX2_LOCALEDEF", _SC_2_LOCALEDEF },
#else
    { "_POSIX2_LOCALEDEF", NONE },
#endif
#ifdef _SC_2_SW_DEV
    { "_POSIX2_SW_DEV", _SC_2_SW_DEV },
#else
    { "_POSIX2_SW_DEV", NONE },
#endif
#ifdef _SC_2_UPE
    { "_POSIX2_UPE", _SC_2_UPE },
#else
    { "_POSIX2_UPE", NONE },
#endif
#ifdef _SC_2_VERSION
    { "_POSIX2_VERSION", _SC_2_VERSION },
#else
    { "_POSIX2_VERSION", NONE },
#endif
#ifdef _SC_ADVISORY_INFO
    { "_POSIX_ADVISORY_INFO", _SC_ADVISORY_INFO },
#else
    { "_POSIX_ADVISORY_INFO", NONE },
#endif
#ifdef _SC_ASYNCHRONOUS_IO
    { "_POSIX_ASYNCHRONOUS_IO", _SC_ASYNCHRONOUS_IO },
#else
    { "_POSIX_ASYNCHRONOUS_IO", NONE },
#endif
#ifdef _SC_BARRIERS
    { "_POSIX_BARRIERS", _SC_BARRIERS },
#else
    { "_POSIX_BARRIERS", NONE },
#endif
#ifdef _SC_CLOCK_SELECTION
    { "_POSIX_CLOCK_SELECTION", _SC_CLOCK_SELECTION },
#else
    { "_POSIX_CLOCK_SELECTION", NONE },
#endif
#ifdef _SC_CPUTIME
    { "_POSIX_CPUTIME", _SC_CPUTIME },
#else
    { "_POSIX_CPUTIME", NONE },
#endif
#ifdef _SC_DEVICE_CONTROL
    { "_POSIX_DEVICE_CONTROL", _SC_DEVICE_CONTROL },
#else
    { "_POSIX_DEVICE_CONTROL", NONE },
#endif
#ifdef _SC_FSYNC
    { "_POSIX_FSYNC", _SC_FSYNC },
#else
    { "_POSIX_FSYNC", NONE },
#endif
#ifdef _SC_IPV6
    { "_POSIX_IPV6", _SC_IPV6 },
#else
    { "_POSIX_IPV6", NONE },
#endif
#ifdef _SC_JOB_CONTROL
    { "_POSIX_JOB_CONTROL", _SC_JOB_CONTROL },
#else
    { "_POSIX_JOB_CONTROL", NONE },
#endif
#ifdef _SC_MAPPED_FILES
    { "_POSIX_MAPPED_FILES", _SC_MAPPED_FILES },
#else
    { "_POSIX_MAPPED_FILES", NONE },
#endif
#ifdef _SC_MEMLOCK
    { "_POSIX_MEMLOCK", _SC_MEMLOCK },
#else
    { "_POSIX_MEMLOCK", NONE },
#endif
#ifdef _SC_MEMLOCK_RANGE
    { "_POSIX_MEMLOCK_RANGE", _SC_MEMLOCK_RANGE },
#else
    { "_POSIX_MEMLOCK_RANGE", NONE },
#endif
#ifdef _SC_MEMORY_PROTECTION
    { "_POSIX_MEMORY_PROTECTION", _SC_MEMORY_PROTECTION },
#else
    { "_POSIX_MEMORY_PROTECTION", NONE },
#endif
#ifdef _SC_MESSAGE_PASSING
    { "_POSIX_MESSAGE_PASSING", _SC_MESSAGE_PASSING },
#else
    { "_POSIX_MESSAGE_PASSING", NONE },
#endif
#ifdef _SC_MONOTONIC_CLOCK
    { "_POSIX_MONOTONIC_CLOCK", _SC_MONOTONIC_CLOCK },
#else
    { "_POSIX_MONOTONIC_CLOCK", NONE },
#endif
#ifdef _SC_PRIORITIZED_IO
    { "_POSIX_PRIORITIZED_IO", _SC_PRIORITIZED_IO },
#else
    { "_POSIX_PRIORITIZED_IO", NONE },
#endif
#ifdef _SC_PRIORITY_SCHEDULING
    { "_POSIX_PRIORITY_SCHEDULING", _SC_PRIORITY_SCHEDULING },
#else
    { "_POSIX_PRIORITY_SCHEDULING", NONE },
#endif
#ifdef _SC_RAW_SOCKETS
    { "_POSIX_RAW_SOCKETS", _SC_RAW_SOCKETS },
#else
    { "_POSIX_RAW_SOCKETS", NONE },
#endif
#ifdef _SC_READER_WRITER_LOCKS
    { "_POSIX_READER_WRITER_LOCKS", _SC_READER_WRITER_LOCKS },
#else
    { "_POSIX_READER_WRITER_LOCKS", NONE },
#endif
#ifdef _SC_REALTIME_SIGNALS
    { "_POSIX_REALTIME_SIGNALS", _SC_REALTIME_SIGNALS },
#else
    { "_POSIX_REALTIME_SIGNALS", NONE },
#endif
#ifdef _SC_REGEXP
    { "_POSIX_REGEXP", _SC_REGEXP },
#else
    { "_POSIX_REGEXP", NONE },
#endif
#ifdef _SC_SAVED_IDS
    { "_POSIX_SAVED_IDS", _SC_SAVED_IDS },
#else
    { "_POSIX_SAVED_IDS", NONE },
#endif
#ifdef _SC_SEMAPHORES
    { "_POSIX_SEMAPHORES", _SC_SEMAPHORES },
#else
    { "_POSIX_SEMAPHORES", NONE },
#endif
#ifdef _SC_SHARED_MEMORY_OBJECTS
    { "_POSIX_SHARED_MEMORY_OBJECTS", _SC_SHARED_MEMORY_OBJECTS },
#else
    { "_POSIX_SHARED_MEMORY_OBJECTS", NONE },
#endif
#ifdef _SC_SHELL
    { "_POSIX_SHELL", _SC_SHELL },
#else
    { "_POSIX_SHELL", NONE },
#endif
#ifdef _SC_SPAWN
    { "_POSIX_SPAWN", _SC_SPAWN },
#else
    { "_POSIX_SPAWN", NONE },
#endif
#ifdef _SC_SPIN_LOCKS
    { "_POSIX_SPIN_LOCKS", _SC_SPIN_LOCKS },
#else
    { "_POSIX_SPIN_LOCKS", NONE },
#endif
#ifdef _SC_SPORADIC_SERVER
    { "_POSIX_SPORADIC_SERVER", _SC_SPORADIC_SERVER },
#else
    { "_POSIX_SPORADIC_SERVER", NONE },
#endif
#ifdef _SC_SS_REPL_MAX
    { "_POSIX_SS_REPL_MAX", _SC_SS_REPL_MAX },
#else
    { "_POSIX_SS_REPL_MAX", NONE },
#endif
#ifdef _SC_SYNCHRONIZED_IO
    { "_POSIX_SYNCHRONIZED_IO", _SC_SYNCHRONIZED_IO },
#else
    { "_POSIX_SYNCHRONIZED_IO", NONE },
#endif
#ifdef _SC_THREADS
    { "_POSIX_THREADS", _SC_THREADS },
#else
    { "_POSIX_THREADS", NONE },
#endif
#ifdef _SC_THREAD_ATTR_STACKADDR
    { "_POSIX_THREAD_ATTR_STACKADDR", _SC_THREAD_ATTR_STACKADDR },
#else
    { "_POSIX_THREAD_ATTR_STACKADDR", NONE },
#endif
#ifdef _SC_THREAD_ATTR_STACKSIZE
    { "_POSIX_THREAD_ATTR_STACKSIZE", _SC_THREAD_ATTR_STACKSIZE },
#else
    { "_POSIX_THREAD_ATTR_STACKSIZE", NONE },
#endif
#ifdef _SC_THREAD_CPUTIME
    { "_POSIX_THREAD_CPUTIME", _SC_THREAD_CPUTIME },
#else
    { "_POSIX_THREAD_CPUTIME", NONE },
#endif
#ifdef _SC_THREAD_PRIORITY_SCHEDULING
    { "_POSIX_THREAD_PRIORITY_SCHEDULING", _SC_THREAD_PRIORITY_SCHEDULING },
#else
    { "_POSIX_THREAD_PRIORITY_SCHEDULING", NONE },
#endif
#ifdef _SC_THREAD_PRIO_INHERIT
    { "_POSIX_THREAD_PRIO_INHERIT", _SC_THREAD_PRIO_INHERIT },
#else
    { "_POSIX_THREAD_PRIO_INHERIT", NONE },
#endif
#ifdef _SC_THREAD_PRIO_PROTECT
    { "_POSIX_THREAD_PRIO_PROTECT", _SC_THREAD_PRIO_PROTECT },
#else
    { "_POSIX_THREAD_PRIO_PROTECT", NONE },
#endif
#ifdef _SC_THREAD_PROCESS_SHARED
    { "_POSIX_THREAD_PROCESS_SHARED", _SC_THREAD_PROCESS_SHARED },
#else
    { "_POSIX_THREAD_PROCESS_SHARED", NONE },
#endif
#ifdef _SC_THREAD_ROBUST_PRIO_INHERIT
    { "_POSIX_THREAD_ROBUST_PRIO_INHERIT", _SC_THREAD_ROBUST_PRIO_INHERIT },
#else
    { "_POSIX_THREAD_ROBUST_PRIO_INHERIT", NONE },
#endif
#ifdef _SC_THREAD_ROBUST_PRIO_PROTECT
    { "_POSIX_THREAD_ROBUST_PRIO_PROTECT", _SC_THREAD_ROBUST_PRIO_PROTECT },
#else
    { "_POSIX_THREAD_ROBUST_PRIO_PROTECT", NONE },
#endif
#ifdef _SC_THREAD_SAFE_FUNCTIONS
    { "_POSIX_THREAD_SAFE_FUNCTIONS", _SC_THREAD_SAFE_FUNCTIONS },
#else
    { "_POSIX_THREAD_SAFE_FUNCTIONS", NONE },
#endif
#ifdef _SC_THREAD_SPORADIC_SERVER
    { "_POSIX_THREAD_SPORADIC_SERVER", _SC_THREAD_SPORADIC_SERVER },
#else
    { "_POSIX_THREAD_SPORADIC_SERVER", NONE },
#endif
#ifdef _SC_TIMEOUTS
    { "_POSIX_TIMEOUTS", _SC_TIMEOUTS },
#else
    { "_POSIX_TIMEOUTS", NONE },
#endif
#ifdef _SC_TIMERS
    { "_POSIX_TIMERS", _SC_TIMERS },
#else
    { "_POSIX_TIMERS", NONE },
#endif
#ifdef _SC_TYPED_MEMORY_OBJECTS
    { "_POSIX_TYPED_MEMORY_OBJECTS", _SC_TYPED_MEMORY_OBJECTS },
#else
    { "_POSIX_TYPED_MEMORY_OBJECTS", NONE },
#endif
#ifdef _SC_V7_ILP32_OFF32
    { "_POSIX_V7_ILP32_OFF32", _SC_V7_ILP32_OFF32 },
#else
    { "_POSIX_V7_ILP32_OFF32", NONE },
#endif
#ifdef _SC_V7_ILP32_OFFBIG
    { "_POSIX_V7_ILP32_OFFBIG", _SC_V7_ILP32_OFFBIG },
#else
    { "_POSIX_V7_ILP32_OFFBIG", NONE },
#endif
#ifdef _SC_V7_LP64_OFF64
    { "_POSIX_V7_LP64_OFF64", _SC_V7_LP64_OFF64 },
#else
    { "_POSIX_V7_LP64_OFF64", NONE },
#endif
#ifdef _SC_V7_LPBIG_OFFBIG
    { "_POSIX_V7_LPBIG_OFFBIG", _SC_V7_LPBIG_OFFBIG },
#else
    { "_POSIX_V7_LPBIG_OFFBIG", NONE },
#endif
#ifdef _SC_V8_ILP32_OFF32
    { "_POSIX_V8_ILP32_OFF32", _SC_V8_ILP32_OFF32 },
#else
    { "_POSIX_V8_ILP32_OFF32", NONE },
#endif
#ifdef _SC_V8_ILP32_OFFBIG
    { "_POSIX_V8_ILP32_OFFBIG", _SC_V8_ILP32_OFFBIG },
#else
    { "_POSIX_V8_ILP32_OFFBIG", NONE },
#endif
#ifdef _SC_V8_LP64_OFF64
    { "_POSIX_V8_LP64_OFF64", _SC_V8_LP64_OFF64 },
#else
    { "_POSIX_V8_LP64_OFF64", NONE },
#endif
#ifdef _SC_V8_LPBIG_OFFBIG
    { "_POSIX_V8_LPBIG_OFFBIG", _SC_V8_LPBIG_OFFBIG },
#else
    { "_POSIX_V8_LPBIG_OFFBIG", NONE },
#endif
#ifdef _SC_VERSION
    { "_POSIX_VERSION", _SC_VERSION },
#else
    { "_POSIX_VERSION", NONE },
#endif
#ifdef _SC_XOPEN_CRYPT
    { "_XOPEN_CRYPT", _SC_XOPEN_CRYPT },
#else
    { "_XOPEN_CRYPT", NONE },
#endif
#ifdef _SC_XOPEN_ENH_I18N
    { "_XOPEN_ENH_I18N", _SC_XOPEN_ENH_I18N },
#else
    { "_XOPEN_ENH_I18N", NONE },
#endif
#ifdef _SC_XOPEN_REALTIME
    { "_XOPEN_REALTIME", _SC_XOPEN_REALTIME },
#else
    { "_XOPEN_REALTIME", NONE },
#endif
#ifdef _SC_XOPEN_REALTIME_THREADS
    { "_XOPEN_REALTIME_THREADS", _SC_XOPEN_REALTIME_THREADS },
#else
    { "_XOPEN_REALTIME_THREADS", NONE },
#endif
#ifdef _SC_XOPEN_SHM
    { "_XOPEN_SHM", _SC_XOPEN_SHM },
#else
    { "_XOPEN_SHM", NONE },
#endif
#ifdef _SC_XOPEN_UNIX
    { "_XOPEN_UNIX", _SC_XOPEN_UNIX },
#else
    { "_XOPEN_UNIX", NONE },
#endif
#ifdef _SC_XOPEN_UUCP
    { "_XOPEN_UUCP", _SC_XOPEN_UUCP },
#else
    { "_XOPEN_UUCP", NONE },
#endif
#ifdef _SC_XOPEN_VERSION
    { "_XOPEN_VERSION", _SC_XOPEN_VERSION },
#else
    { "_XOPEN_VERSION", NONE },
#endif
};

/* the path variables: pathconf() */
static const struct var pathvars[] = {
#ifdef _PC_FILESIZEBITS
    { "FILESIZEBITS", _PC_FILESIZEBITS },
#else
    { "FILESIZEBITS", NONE },
#endif
#ifdef _PC_LINK_MAX
    { "LINK_MAX", _PC_LINK_MAX },
#else
    { "LINK_MAX", NONE },
#endif
#ifdef _PC_MAX_CANON
    { "MAX_CANON", _PC_MAX_CANON },
#else
    { "MAX_CANON", NONE },
#endif
#ifdef _PC_MAX_INPUT
    { "MAX_INPUT", _PC_MAX_INPUT },
#else
    { "MAX_INPUT", NONE },
#endif
#ifdef _PC_NAME_MAX
    { "NAME_MAX", _PC_NAME_MAX },
#else
    { "NAME_MAX", NONE },
#endif
#ifdef _PC_PATH_MAX
    { "PATH_MAX", _PC_PATH_MAX },
#else
    { "PATH_MAX", NONE },
#endif
#ifdef _PC_PIPE_BUF
    { "PIPE_BUF", _PC_PIPE_BUF },
#else
    { "PIPE_BUF", NONE },
#endif
#ifdef _PC_2_SYMLINKS
    { "POSIX2_SYMLINKS", _PC_2_SYMLINKS },
#else
    { "POSIX2_SYMLINKS", NONE },
#endif
#ifdef _PC_ALLOC_SIZE_MIN
    { "POSIX_ALLOC_SIZE_MIN", _PC_ALLOC_SIZE_MIN },
#else
    { "POSIX_ALLOC_SIZE_MIN", NONE },
#endif
#ifdef _PC_REC_INCR_XFER_SIZE
    { "POSIX_REC_INCR_XFER_SIZE", _PC_REC_INCR_XFER_SIZE },
#else
    { "POSIX_REC_INCR_XFER_SIZE", NONE },
#endif
#ifdef _PC_REC_MAX_XFER_SIZE
    { "POSIX_REC_MAX_XFER_SIZE", _PC_REC_MAX_XFER_SIZE },
#else
    { "POSIX_REC_MAX_XFER_SIZE", NONE },
#endif
#ifdef _PC_REC_MIN_XFER_SIZE
    { "POSIX_REC_MIN_XFER_SIZE", _PC_REC_MIN_XFER_SIZE },
#else
    { "POSIX_REC_MIN_XFER_SIZE", NONE },
#endif
#ifdef _PC_REC_XFER_ALIGN
    { "POSIX_REC_XFER_ALIGN", _PC_REC_XFER_ALIGN },
#else
    { "POSIX_REC_XFER_ALIGN", NONE },
#endif
#ifdef _PC_SYMLINK_MAX
    { "SYMLINK_MAX", _PC_SYMLINK_MAX },
#else
    { "SYMLINK_MAX", NONE },
#endif
#ifdef _PC_TEXTDOMAIN_MAX
    { "TEXTDOMAIN_MAX", _PC_TEXTDOMAIN_MAX },
#else
    { "TEXTDOMAIN_MAX", NONE },
#endif
#ifdef _PC_ASYNC_IO
    { "_POSIX_ASYNC_IO", _PC_ASYNC_IO },
#else
    { "_POSIX_ASYNC_IO", NONE },
#endif
#ifdef _PC_CHOWN_RESTRICTED
    { "_POSIX_CHOWN_RESTRICTED", _PC_CHOWN_RESTRICTED },
#else
    { "_POSIX_CHOWN_RESTRICTED", NONE },
#endif
#ifdef _PC_FALLOC
    { "_POSIX_FALLOC", _PC_FALLOC },
#else
    { "_POSIX_FALLOC", NONE },
#endif
#ifdef _PC_NO_TRUNC
    { "_POSIX_NO_TRUNC", _PC_NO_TRUNC },
#else
    { "_POSIX_NO_TRUNC", NONE },
#endif
#ifdef _PC_PRIO_IO
    { "_POSIX_PRIO_IO", _PC_PRIO_IO },
#else
    { "_POSIX_PRIO_IO", NONE },
#endif
#ifdef _PC_SYNC_IO
    { "_POSIX_SYNC_IO", _PC_SYNC_IO },
#else
    { "_POSIX_SYNC_IO", NONE },
#endif
#ifdef _PC_TIMESTAMP_RESOLUTION
    { "_POSIX_TIMESTAMP_RESOLUTION", _PC_TIMESTAMP_RESOLUTION },
#else
    { "_POSIX_TIMESTAMP_RESOLUTION", NONE },
#endif
#ifdef _PC_VDISABLE
    { "_POSIX_VDISABLE", _PC_VDISABLE },
#else
    { "_POSIX_VDISABLE", NONE },
#endif
};

/* the string variables: confstr() */
static const struct var strvars[] = {
#ifdef _CS_PATH
    { "PATH", _CS_PATH },
#else
    { "PATH", NONE },
#endif
#ifdef _CS_POSIX_V7_ILP32_OFF32_CFLAGS
    { "POSIX_V7_ILP32_OFF32_CFLAGS", _CS_POSIX_V7_ILP32_OFF32_CFLAGS },
#else
    { "POSIX_V7_ILP32_OFF32_CFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V7_ILP32_OFF32_LDFLAGS
    { "POSIX_V7_ILP32_OFF32_LDFLAGS", _CS_POSIX_V7_ILP32_OFF32_LDFLAGS },
#else
    { "POSIX_V7_ILP32_OFF32_LDFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V7_ILP32_OFF32_LIBS
    { "POSIX_V7_ILP32_OFF32_LIBS", _CS_POSIX_V7_ILP32_OFF32_LIBS },
#else
    { "POSIX_V7_ILP32_OFF32_LIBS", NONE },
#endif
#ifdef _CS_POSIX_V7_ILP32_OFFBIG_CFLAGS
    { "POSIX_V7_ILP32_OFFBIG_CFLAGS", _CS_POSIX_V7_ILP32_OFFBIG_CFLAGS },
#else
    { "POSIX_V7_ILP32_OFFBIG_CFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V7_ILP32_OFFBIG_LDFLAGS
    { "POSIX_V7_ILP32_OFFBIG_LDFLAGS", _CS_POSIX_V7_ILP32_OFFBIG_LDFLAGS },
#else
    { "POSIX_V7_ILP32_OFFBIG_LDFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V7_ILP32_OFFBIG_LIBS
    { "POSIX_V7_ILP32_OFFBIG_LIBS", _CS_POSIX_V7_ILP32_OFFBIG_LIBS },
#else
    { "POSIX_V7_ILP32_OFFBIG_LIBS", NONE },
#endif
#ifdef _CS_POSIX_V7_LP64_OFF64_CFLAGS
    { "POSIX_V7_LP64_OFF64_CFLAGS", _CS_POSIX_V7_LP64_OFF64_CFLAGS },
#else
    { "POSIX_V7_LP64_OFF64_CFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V7_LP64_OFF64_LDFLAGS
    { "POSIX_V7_LP64_OFF64_LDFLAGS", _CS_POSIX_V7_LP64_OFF64_LDFLAGS },
#else
    { "POSIX_V7_LP64_OFF64_LDFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V7_LP64_OFF64_LIBS
    { "POSIX_V7_LP64_OFF64_LIBS", _CS_POSIX_V7_LP64_OFF64_LIBS },
#else
    { "POSIX_V7_LP64_OFF64_LIBS", NONE },
#endif
#ifdef _CS_POSIX_V7_LPBIG_OFFBIG_CFLAGS
    { "POSIX_V7_LPBIG_OFFBIG_CFLAGS", _CS_POSIX_V7_LPBIG_OFFBIG_CFLAGS },
#else
    { "POSIX_V7_LPBIG_OFFBIG_CFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V7_LPBIG_OFFBIG_LDFLAGS
    { "POSIX_V7_LPBIG_OFFBIG_LDFLAGS", _CS_POSIX_V7_LPBIG_OFFBIG_LDFLAGS },
#else
    { "POSIX_V7_LPBIG_OFFBIG_LDFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V7_LPBIG_OFFBIG_LIBS
    { "POSIX_V7_LPBIG_OFFBIG_LIBS", _CS_POSIX_V7_LPBIG_OFFBIG_LIBS },
#else
    { "POSIX_V7_LPBIG_OFFBIG_LIBS", NONE },
#endif
#ifdef _CS_POSIX_V7_THREADS_CFLAGS
    { "POSIX_V7_THREADS_CFLAGS", _CS_POSIX_V7_THREADS_CFLAGS },
#else
    { "POSIX_V7_THREADS_CFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V7_THREADS_LDFLAGS
    { "POSIX_V7_THREADS_LDFLAGS", _CS_POSIX_V7_THREADS_LDFLAGS },
#else
    { "POSIX_V7_THREADS_LDFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V7_WIDTH_RESTRICTED_ENVS
    { "POSIX_V7_WIDTH_RESTRICTED_ENVS", _CS_POSIX_V7_WIDTH_RESTRICTED_ENVS },
#else
    { "POSIX_V7_WIDTH_RESTRICTED_ENVS", NONE },
#endif
#ifdef _CS_POSIX_V8_ILP32_OFF32_CFLAGS
    { "POSIX_V8_ILP32_OFF32_CFLAGS", _CS_POSIX_V8_ILP32_OFF32_CFLAGS },
#else
    { "POSIX_V8_ILP32_OFF32_CFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V8_ILP32_OFF32_LDFLAGS
    { "POSIX_V8_ILP32_OFF32_LDFLAGS", _CS_POSIX_V8_ILP32_OFF32_LDFLAGS },
#else
    { "POSIX_V8_ILP32_OFF32_LDFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V8_ILP32_OFF32_LIBS
    { "POSIX_V8_ILP32_OFF32_LIBS", _CS_POSIX_V8_ILP32_OFF32_LIBS },
#else
    { "POSIX_V8_ILP32_OFF32_LIBS", NONE },
#endif
#ifdef _CS_POSIX_V8_ILP32_OFFBIG_CFLAGS
    { "POSIX_V8_ILP32_OFFBIG_CFLAGS", _CS_POSIX_V8_ILP32_OFFBIG_CFLAGS },
#else
    { "POSIX_V8_ILP32_OFFBIG_CFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V8_ILP32_OFFBIG_LDFLAGS
    { "POSIX_V8_ILP32_OFFBIG_LDFLAGS", _CS_POSIX_V8_ILP32_OFFBIG_LDFLAGS },
#else
    { "POSIX_V8_ILP32_OFFBIG_LDFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V8_ILP32_OFFBIG_LIBS
    { "POSIX_V8_ILP32_OFFBIG_LIBS", _CS_POSIX_V8_ILP32_OFFBIG_LIBS },
#else
    { "POSIX_V8_ILP32_OFFBIG_LIBS", NONE },
#endif
#ifdef _CS_POSIX_V8_LP64_OFF64_CFLAGS
    { "POSIX_V8_LP64_OFF64_CFLAGS", _CS_POSIX_V8_LP64_OFF64_CFLAGS },
#else
    { "POSIX_V8_LP64_OFF64_CFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V8_LP64_OFF64_LDFLAGS
    { "POSIX_V8_LP64_OFF64_LDFLAGS", _CS_POSIX_V8_LP64_OFF64_LDFLAGS },
#else
    { "POSIX_V8_LP64_OFF64_LDFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V8_LP64_OFF64_LIBS
    { "POSIX_V8_LP64_OFF64_LIBS", _CS_POSIX_V8_LP64_OFF64_LIBS },
#else
    { "POSIX_V8_LP64_OFF64_LIBS", NONE },
#endif
#ifdef _CS_POSIX_V8_LPBIG_OFFBIG_CFLAGS
    { "POSIX_V8_LPBIG_OFFBIG_CFLAGS", _CS_POSIX_V8_LPBIG_OFFBIG_CFLAGS },
#else
    { "POSIX_V8_LPBIG_OFFBIG_CFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V8_LPBIG_OFFBIG_LDFLAGS
    { "POSIX_V8_LPBIG_OFFBIG_LDFLAGS", _CS_POSIX_V8_LPBIG_OFFBIG_LDFLAGS },
#else
    { "POSIX_V8_LPBIG_OFFBIG_LDFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V8_LPBIG_OFFBIG_LIBS
    { "POSIX_V8_LPBIG_OFFBIG_LIBS", _CS_POSIX_V8_LPBIG_OFFBIG_LIBS },
#else
    { "POSIX_V8_LPBIG_OFFBIG_LIBS", NONE },
#endif
#ifdef _CS_POSIX_V8_THREADS_CFLAGS
    { "POSIX_V8_THREADS_CFLAGS", _CS_POSIX_V8_THREADS_CFLAGS },
#else
    { "POSIX_V8_THREADS_CFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V8_THREADS_LDFLAGS
    { "POSIX_V8_THREADS_LDFLAGS", _CS_POSIX_V8_THREADS_LDFLAGS },
#else
    { "POSIX_V8_THREADS_LDFLAGS", NONE },
#endif
#ifdef _CS_POSIX_V8_WIDTH_RESTRICTED_ENVS
    { "POSIX_V8_WIDTH_RESTRICTED_ENVS", _CS_POSIX_V8_WIDTH_RESTRICTED_ENVS },
#else
    { "POSIX_V8_WIDTH_RESTRICTED_ENVS", NONE },
#endif
#ifdef _CS_V7_ENV
    { "V7_ENV", _CS_V7_ENV },
#else
    { "V7_ENV", NONE },
#endif
#ifdef _CS_V8_ENV
    { "V8_ENV", _CS_V8_ENV },
#else
    { "V8_ENV", NONE },
#endif
};

/* the maximum and minimum values of <limits.h>, which the standard fixes */
static const struct limit limits[] = {
    { "_POSIX2_BC_BASE_MAX", 99 },
    { "_POSIX2_BC_DIM_MAX", 2048 },
    { "_POSIX2_BC_SCALE_MAX", 99 },
    { "_POSIX2_BC_STRING_MAX", 1000 },
    { "_POSIX2_CHARCLASS_NAME_MAX", 14 },
    { "_POSIX2_COLL_WEIGHTS_MAX", 2 },
    { "_POSIX2_EXPR_NEST_MAX", 32 },
    { "_POSIX2_LINE_MAX", 2048 },
    { "_POSIX2_RE_DUP_MAX", 255 },
    { "_POSIX_AIO_LISTIO_MAX", 2 },
    { "_POSIX_AIO_MAX", 1 },
    { "_POSIX_ARG_MAX", 4096 },
    { "_POSIX_CHILD_MAX", 25 },
    { "_POSIX_CLOCKRES_MIN", 20000000 },
    { "_POSIX_DELAYTIMER_MAX", 32 },
    { "_POSIX_HOST_NAME_MAX", 255 },
    { "_POSIX_LINK_MAX", 8 },
    { "_POSIX_LOGIN_NAME_MAX", 9 },
    { "_POSIX_MAX_CANON", 255 },
    { "_POSIX_MAX_INPUT", 255 },
    { "_POSIX_MQ_OPEN_MAX", 8 },
    { "_POSIX_MQ_PRIO_MAX", 32 },
    { "_POSIX_NAME_MAX", 14 },
    { "_POSIX_NGROUPS_MAX", 8 },
    { "_POSIX_OPEN_MAX", 20 },
    { "_POSIX_PATH_MAX", 256 },
    { "_POSIX_PIPE_BUF", 512 },
    { "_POSIX_RE_DUP_MAX", 255 },
    { "_POSIX_RTSIG_MAX", 8 },
    { "_POSIX_SEM_NSEMS_MAX", 256 },
    { "_POSIX_SEM_VALUE_MAX", 32767 },
    { "_POSIX_SIGQUEUE_MAX", 32 },
    { "_POSIX_SSIZE_MAX", 32767 },
    { "_POSIX_SS_REPL_MAX", 4 },
    { "_POSIX_STREAM_MAX", 8 },
    { "_POSIX_SYMLINK_MAX", 255 },
    { "_POSIX_SYMLOOP_MAX", 8 },
    { "_POSIX_THREAD_DESTRUCTOR_ITERATIONS", 4 },
    { "_POSIX_THREAD_KEYS_MAX", 128 },
    { "_POSIX_THREAD_THREADS_MAX", 64 },
    { "_POSIX_TIMER_MAX", 32 },
    { "_POSIX_TTY_NAME_MAX", 9 },
    { "_POSIX_TZNAME_MAX", 6 },
    { "_XOPEN_IOV_MAX", 16 },
    { "_XOPEN_NAME_MAX", 255 },
    { "_XOPEN_PATH_MAX", 1024 },
};

#define N(a) (sizeof (a) / sizeof (a)[0])

static const struct var *find(const struct var *t, size_t n, const char *name)
{
    for (size_t i = 0; i < n; i++)
        if (strcmp(t[i].name, name) == 0)
            return &t[i];
    return NULL;
}

/* the earlier names, which are those with an underscore before them */
static const char *const compat[] = {
    "POSIX2_C_BIND", "POSIX2_C_DEV", "POSIX2_CHAR_TERM", "POSIX2_FORT_RUN",
    "POSIX2_LOCALEDEF", "POSIX2_SW_DEV", "POSIX2_UPE", "POSIX2_VERSION",
    "POSIX2_BC_BASE_MAX", "POSIX2_BC_DIM_MAX", "POSIX2_BC_SCALE_MAX",
    "POSIX2_BC_STRING_MAX", "POSIX2_COLL_WEIGHTS_MAX", "POSIX2_EXPR_NEST_MAX",
    "POSIX2_LINE_MAX", "POSIX2_RE_DUP_MAX",
};

/* is the compilation environment spec the one getconf was compiled in? */
static int check_spec(const char *spec)
{
    const char *e;
    if (strncmp(spec, "POSIX_V8_", 9) == 0 || strncmp(spec, "POSIX_V7_", 9) == 0)
        e = spec + 9;
    else
        return -1;
    int lbits = (int)(sizeof(long) * CHAR_BIT), pbits = (int)(sizeof(void *) * CHAR_BIT);
    int obits = (int)(sizeof(off_t) * CHAR_BIT), ibits = (int)(sizeof(int) * CHAR_BIT);
    if (strcmp(e, "ILP32_OFF32") == 0)
        return ibits == 32 && lbits == 32 && pbits == 32 && obits == 32;
    if (strcmp(e, "ILP32_OFFBIG") == 0)
        return ibits == 32 && lbits == 32 && pbits == 32 && obits >= 64;
    if (strcmp(e, "LP64_OFF64") == 0)
        return ibits == 32 && lbits == 64 && pbits == 64 && obits == 64;
    if (strcmp(e, "LPBIG_OFFBIG") == 0)
        return ibits >= 32 && lbits >= 64 && pbits >= 64 && obits >= 64;
    return -1;
}

static void usage(void)
{
    fprintf(stderr, "usage: getconf [-v specification] system_var\n"
                    "       getconf [-v specification] path_var pathname\n");
    exit(2);
}

static int out_status(void)
{
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "getconf: write error: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    const char *spec = NULL;
    int c;
    while ((c = getopt(argc, argv, "v:")) != -1) {
        if (c != 'v')
            usage();
        spec = optarg;
    }
    int nops = argc - optind;
    if (nops < 1 || nops > 2)
        usage();
    if (spec) {
        int r = check_spec(spec);
        if (r < 0) {
            fprintf(stderr, "getconf: -v %s: not a specification\n", spec);
            return 1;
        }
        if (r == 0) {
            fprintf(stderr, "getconf: -v %s: the values for this environment are not available\n", spec);
            return 1;
        }
    }
    const char *name = argv[optind];
    char v7[128];
    for (size_t i = 0; i < N(compat); i++)
        if (strcmp(name, compat[i]) == 0) {
            static char buf[64];
            snprintf(buf, sizeof buf, "_%s", name);
            name = buf;
            break;
        }

    if (nops == 2) {
        const struct var *v = find(pathvars, N(pathvars), name);
        if (!v) {
            fprintf(stderr, "getconf: %s: not a path variable\n", argv[optind]);
            return 1;
        }
        const char *path = argv[optind + 1];
        if (v->code == NONE) {
            /* still the pathname must be there */
            if (access(path, F_OK) != 0) {
                fprintf(stderr, "getconf: %s: %s\n", path, strerror(errno));
                return 1;
            }
            puts("undefined");
            return out_status();
        }
        errno = 0;
        long r = pathconf(path, v->code);
        if (r == -1 && errno == EINVAL && access(path, F_OK) == 0)
            errno = 0;          /* not associated with this file */
        if (r == -1 && errno) {
            fprintf(stderr, "getconf: %s: %s\n", path, strerror(errno));
            return 1;
        }
        if (r == -1)
            puts("undefined");
        else
            printf("%ld\n", r);
        return out_status();
    }

    /* a V8 variable the system has no name for: that of V7, the same
       environment under the name of the earlier version */
    const struct var *v;
    if ((v = find(sysvars, N(sysvars), name)) || (v = find(strvars, N(strvars), name)))
        if (v->code == NONE && strstr(name, "V8_") && strlen(name) < sizeof v7) {
            strcpy(v7, name);
            strstr(v7, "V8_")[1] = '7';
            name = v7;
        }
    if ((v = find(sysvars, N(sysvars), name))) {
        long r = -1;
        errno = 0;
        if (v->code != NONE)
            r = sysconf(v->code);
        if (r == -1 && errno == EINVAL)
            errno = 0;          /* a name the system knows but does not support */
        if (r == -1 && errno) {
            fprintf(stderr, "getconf: %s: %s\n", argv[optind], strerror(errno));
            return 1;
        }
        if (r == -1)
            puts("undefined");
        else
            printf("%ld\n", r);
        return out_status();
    }
    if ((v = find(strvars, N(strvars), name))) {
        size_t n = 0;
        errno = 0;
        if (v->code != NONE)
            n = confstr(v->code, NULL, 0);
        if (n == 0 && errno && errno != EINVAL) {
            fprintf(stderr, "getconf: %s: %s\n", argv[optind], strerror(errno));
            return 1;
        }
        if (n == 0) {
            puts("undefined");
            return out_status();
        }
        char *s = malloc(n);
        if (!s) {
            fprintf(stderr, "getconf: out of memory\n");
            return 1;
        }
        confstr(v->code, s, n);
        puts(s);
        free(s);
        return out_status();
    }
    for (size_t i = 0; i < N(limits); i++)
        if (strcmp(limits[i].name, name) == 0) {
            printf("%ld\n", limits[i].value);
            return out_status();
        }
    if (find(pathvars, N(pathvars), name))
        fprintf(stderr, "getconf: %s: a path variable needs a pathname\n", argv[optind]);
    else
        fprintf(stderr, "getconf: %s: not a configuration variable\n", argv[optind]);
    return 1;
}
