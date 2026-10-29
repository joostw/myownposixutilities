/*
 * ipc.c - make and use XSI IPC objects, for the tests of ipcs and ipcrm
 * (POSIX has no utility that makes them).
 *
 *   ipc q KEY MODE              msgget(): a message queue; writes its ID
 *   ipc m KEY MODE SIZE         shmget(): a shared memory segment
 *   ipc s KEY MODE NSEMS        semget(): a semaphore set
 *   ipc send ID TEXT            msgsnd() of TEXT (type 1)
 *   ipc recv ID                 msgrcv() of a message; writes it
 *   ipc attach ID               shmat(), then shmdt()
 *   ipc hold ID SECONDS         shmat(), sleep, then exit (attached)
 *   ipc semop ID                semop(): add 1 to semaphore 0
 *   ipc exists q|m|s ID         exit status 0 if the ID is there
 *   ipc rm q|m|s ID             remove it (IPC_RMID)
 *
 * KEY is a number (strtol() base 0, so 0x... too), 0 for IPC_PRIVATE;
 * MODE is octal.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>

struct msg {
    long type;
    char text[4096];
};

static void die(const char *what)
{
    fprintf(stderr, "ipc: %s: %s\n", what, strerror(errno));
    exit(1);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: ipc q|m|s|send|recv|attach|hold|semop|exists|rm ...\n");
        return 2;
    }
    const char *op = argv[1];
    if (strlen(op) == 1 && argc >= 4) {
        key_t key = (key_t)strtol(argv[2], NULL, 0);
        int mode = (int)strtol(argv[3], NULL, 8) | IPC_CREAT | IPC_EXCL;
        int id;
        if (op[0] == 'q')
            id = msgget(key, mode);
        else if (op[0] == 'm')
            id = shmget(key, argc > 4 ? (size_t)strtoul(argv[4], NULL, 10) : 4096, mode);
        else
            id = semget(key, argc > 4 ? atoi(argv[4]) : 1, mode);
        if (id < 0)
            die("get");
        printf("%d\n", id);
        return 0;
    }
    int id = atoi(argv[2]);
    if (strcmp(op, "send") == 0 && argc > 3) {
        struct msg m;
        m.type = 1;
        size_t n = strlen(argv[3]);
        if (n > sizeof m.text)
            n = sizeof m.text;
        memcpy(m.text, argv[3], n);
        if (msgsnd(id, &m, n, 0) != 0)
            die("msgsnd");
    } else if (strcmp(op, "recv") == 0) {
        struct msg m;
        ssize_t n = msgrcv(id, &m, sizeof m.text, 0, 0);
        if (n < 0)
            die("msgrcv");
        printf("%.*s\n", (int)n, m.text);
    } else if (strcmp(op, "attach") == 0 || strcmp(op, "hold") == 0) {
        void *p = shmat(id, NULL, 0);
        if (p == (void *)-1)
            die("shmat");
        if (op[0] == 'h')
            sleep(argc > 3 ? (unsigned)atoi(argv[3]) : 5);
        else if (shmdt(p) != 0)
            die("shmdt");
    } else if (strcmp(op, "semop") == 0) {
        struct sembuf b = { 0, 1, 0 };
        if (semop(id, &b, 1) != 0)
            die("semop");
    } else if (strcmp(op, "exists") == 0 && argc > 3) {
        int r;
        id = atoi(argv[3]);
        if (argv[2][0] == 'q') {
            struct msqid_ds d;
            r = msgctl(id, IPC_STAT, &d);
        } else if (argv[2][0] == 'm') {
            struct shmid_ds d;
            r = shmctl(id, IPC_STAT, &d);
        } else {
            struct semid_ds d;
            union { int val; struct semid_ds *buf; unsigned short *array; } u;
            u.buf = &d;
            r = semctl(id, 0, IPC_STAT, u);
        }
        return r == 0 || errno == EACCES ? 0 : 1;
    } else if (strcmp(op, "rm") == 0 && argc > 3) {
        id = atoi(argv[3]);
        int r = argv[2][0] == 'q' ? msgctl(id, IPC_RMID, NULL)
              : argv[2][0] == 'm' ? shmctl(id, IPC_RMID, NULL) : semctl(id, 0, IPC_RMID);
        if (r != 0)
            die("rm");
    } else {
        fprintf(stderr, "ipc: %s: unknown\n", op);
        return 2;
    }
    return 0;
}
