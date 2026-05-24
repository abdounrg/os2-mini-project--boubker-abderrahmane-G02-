
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

/* ---------- thread argument ---------- */
typedef struct {
    const char *type;   
    int         id;     
    unsigned    seed;   
} player_arg_t;


static void play(player_arg_t *p)
{
    printf("[%-5s #%d] is waiting to play\n", p->type, p->id);
    fflush(stdout);

  
    unsigned int duration = 1 + (rand_r(&p->seed) % 3);
    sleep(duration);

    printf("[%-5s #%d] is done playing (played %u s)\n",
           p->type, p->id, duration);
    fflush(stdout);
}

void *football_player(void *arg)    { play((player_arg_t *)arg); return NULL; }
void *supermario_player(void *arg)  { play((player_arg_t *)arg); return NULL; }
void *nfs_player(void *arg)         { play((player_arg_t *)arg); return NULL; }


typedef struct {
    const char *type;
    int         count;
    void *(*fn)(void *);
} group_t;

#define N_THREADS 16   

int main(void)
{
    srand((unsigned)time(NULL));

    group_t groups[] = {
        { "P-F",   8, football_player    },
        { "P-SM",  4, supermario_player  },
        { "P-NFS", 4, nfs_player         },
    };
    const int n_groups = sizeof(groups) / sizeof(groups[0]);

    pthread_t     tids[N_THREADS];
    player_arg_t  args[N_THREADS];

    
    int k = 0;
    for (int g = 0; g < n_groups; ++g) {
        for (int i = 0; i < groups[g].count; ++i, ++k) {
            args[k].type = groups[g].type;
            args[k].id   = i;                       
            args[k].seed = (unsigned)time(NULL) ^ (unsigned)(k * 2654435761u);

            if (pthread_create(&tids[k], NULL, groups[g].fn, &args[k]) != 0) {
                perror("pthread_create");
                exit(EXIT_FAILURE);
            }
        }
    }

  
    for (int i = 0; i < N_THREADS; ++i)
        pthread_join(tids[i], NULL);

    printf("\nAll 16 players have finished. Console is shutting down.\n");
    return 0;
}
