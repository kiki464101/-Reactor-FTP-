#include "ipc_shm.h"

#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

client_info_t *shm_init(void)
{
    key_t key = ftok("/tmp", 0x77);   /* arbitrary key */
    if (key < 0) { perror("ftok"); return NULL; }

    int shmid = shmget(key, sizeof(client_info_t) * MAX_CLIENTS,
                        IPC_CREAT | IPC_EXCL | 0666);
    if (shmid < 0) {
        /* segment may already exist – try to attach */
        shmid = shmget(key, sizeof(client_info_t) * MAX_CLIENTS, 0666);
        if (shmid < 0) { perror("shmget"); return NULL; }
    }

    client_info_t *p = (client_info_t *)shmat(shmid, NULL, 0);
    if (p == (void *)-1) { perror("shmat"); return NULL; }

    /* zero-initialise on first creation */
    memset(p, 0, sizeof(client_info_t) * MAX_CLIENTS);
    return p;
}

void shm_add_client(client_info_t *shm, int pid,
                    const char *ip, int port, const char *status)
{
    if (!shm) return;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!shm[i].active) {
            shm[i].pid    = pid;
            shm[i].port   = port;
            strncpy(shm[i].ip,     ip,     sizeof(shm[i].ip) - 1);
            strncpy(shm[i].status, status, sizeof(shm[i].status) - 1);
            shm[i].active = true;
            return;
        }
    }
    fprintf(stderr, "shm_add_client: table full\n");
}

void shm_update_status(client_info_t *shm, int pid, const char *status)
{
    if (!shm) return;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (shm[i].active && shm[i].pid == pid) {
            strncpy(shm[i].status, status, sizeof(shm[i].status) - 1);
            return;
        }
    }
}

void shm_remove_client(client_info_t *shm, int pid)
{
    if (!shm) return;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (shm[i].active && shm[i].pid == pid) {
            shm[i].active = false;
            return;
        }
    }
}
