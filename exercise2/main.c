

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>


enum { G_NONE = -1, G_F = 0, G_SM = 1, G_NFS = 2, G_COUNT = 3 };

static const char *GAME_NAME[G_COUNT] = { "Football", "SuperMario", "NFS" };
static const char *PTYPE[G_COUNT]     = { "P-F",      "P-SM",       "P-NFS" };
static const int   CAPACITY[G_COUNT]  = {  4,          2,            1     };


static int current_game = G_NONE;
static int active_count = 0;
static int waiting[G_COUNT] = { 0, 0, 0 };
static int last_served      = G_NONE;


static sem_t mutex;
static sem_t gate[G_COUNT];


static void release_one_waiter(int g)
{
    waiting[g]--;
    active_count++;
    sem_post(&gate[g]);
}


 
static int pick_next_game(void)
{
    for (int step = 1; step <= G_COUNT; ++step) {
        int g = (last_served + step) % G_COUNT;
        if (waiting[g] > 0) return g;
    }
    return G_NONE;
}


static void start_session(int g)
{
    current_game = g;
    last_served  = g;
    int slots = CAPACITY[g];
    while (slots > 0 && waiting[g] > 0) {
        release_one_waiter(g);
        slots--;
    }
}



static void enter_game(int g, int id)
{
    sem_wait(&mutex);

   
    int other_waiting = 0;
    for (int i = 0; i < G_COUNT; ++i)
        if (i != g) other_waiting += waiting[i];

    if ( (current_game == G_NONE)
      || (current_game == g && active_count < CAPACITY[g] && other_waiting == 0) )
    {
        if (current_game == G_NONE) { current_game = g; last_served = g; }
        active_count++;
        printf("[%-5s #%d] ENTERS  %-10s  (players now on console: %d/%d)\n",
               PTYPE[g], id, GAME_NAME[g], active_count, CAPACITY[g]);
        fflush(stdout);
        sem_post(&mutex);
        return;
    }

    
    waiting[g]++;
    printf("[%-5s #%d] waits   %-10s  (queue F=%d SM=%d NFS=%d, on=%s/%d)\n",
           PTYPE[g], id, GAME_NAME[g],
           waiting[G_F], waiting[G_SM], waiting[G_NFS],
           current_game == G_NONE ? "none" : GAME_NAME[current_game],
           active_count);
    fflush(stdout);
    sem_post(&mutex);

    sem_wait(&gate[g]);   

    sem_wait(&mutex);
    printf("[%-5s #%d] ENTERS  %-10s  (players now on console: %d/%d)\n",
           PTYPE[g], id, GAME_NAME[g], active_count, CAPACITY[g]);
    fflush(stdout);
    sem_post(&mutex);
}

static void quit_game(int g, int id)
{
    sem_wait(&mutex);

    active_count--;
    printf("[%-5s #%d] LEAVES  %-10s  (players still on console: %d)\n",
           PTYPE[g], id, GAME_NAME[g], active_count);
    fflush(stdout);

    if (active_count == 0) {
        /* Console is empty -> pick the next game fairly. */
        int next = pick_next_game();
        if (next == G_NONE) {
            current_game = G_NONE;
            printf("           >>> console is now FREE\n");
        } else {
            printf("           >>> switching console to %s\n", GAME_NAME[next]);
            start_session(next);
        }
    } else {
      
        int other_waiting = 0;
        for (int i = 0; i < G_COUNT; ++i)
            if (i != g) other_waiting += waiting[i];

        if (other_waiting == 0 && waiting[g] > 0)
            release_one_waiter(g);
    }

    sem_post(&mutex);
}


void enter_F_game   (int id) { enter_game(G_F,   id); }
void quit_F_game    (int id) { quit_game (G_F,   id); }
void enter_SM_game  (int id) { enter_game(G_SM,  id); }
void quit_SM_game   (int id) { quit_game (G_SM,  id); }
void enter_NFS_game (int id) { enter_game(G_NFS, id); }
void quit_NFS_game  (int id) { quit_game (G_NFS, id); }


typedef struct {
    int      game;   
    int      id;
    unsigned seed;
} parg_t;

static void player_body(parg_t *p,
                        void (*enter)(int),
                        void (*quit )(int))
{
    enter(p->id);

    unsigned int duration = 1 + (rand_r(&p->seed) % 3);
    sleep(duration);
    printf("[%-5s #%d] playing %-10s (%u s)\n",
           PTYPE[p->game], p->id, GAME_NAME[p->game], duration);
    fflush(stdout);

    quit(p->id);
}

void *football_player   (void *a) { player_body(a, enter_F_game,   quit_F_game);   return NULL; }
void *supermario_player (void *a) { player_body(a, enter_SM_game,  quit_SM_game);  return NULL; }
void *nfs_player        (void *a) { player_body(a, enter_NFS_game, quit_NFS_game); return NULL; }


typedef struct {
    int   game;
    int   count;
    void *(*fn)(void *);
} group_t;

#define N_THREADS 16

int main(void)
{
    srand((unsigned)time(NULL));

    /* init semaphores */
    if (sem_init(&mutex, 0, 1) != 0) { perror("sem_init mutex"); exit(1); }
    for (int g = 0; g < G_COUNT; ++g)
        if (sem_init(&gate[g], 0, 0) != 0) { perror("sem_init gate"); exit(1); }

    group_t groups[] = {
        { G_F,   8, football_player    },
        { G_SM,  4, supermario_player  },
        { G_NFS, 4, nfs_player         },
    };
    const int n_groups = sizeof(groups) / sizeof(groups[0]);

    pthread_t tids[N_THREADS];
    parg_t    args[N_THREADS];

    /* --- create all 16 threads first --- */
    int k = 0;
    for (int gi = 0; gi < n_groups; ++gi) {
        for (int i = 0; i < groups[gi].count; ++i, ++k) {
            args[k].game = groups[gi].game;
            args[k].id   = i;
            args[k].seed = (unsigned)time(NULL) ^ (unsigned)(k * 2654435761u);
            if (pthread_create(&tids[k], NULL, groups[gi].fn, &args[k]) != 0) {
                perror("pthread_create");
                exit(EXIT_FAILURE);
            }
        }
    }

    /* --- only now do we join them --- */
    for (int i = 0; i < N_THREADS; ++i)
        pthread_join(tids[i], NULL);

    /* cleanup */
    sem_destroy(&mutex);
    for (int g = 0; g < G_COUNT; ++g) sem_destroy(&gate[g]);

    printf("\nAll 16 players have finished. Console is shutting down.\n");
    return 0;
}
