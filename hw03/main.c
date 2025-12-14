#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <limits.h>

#define NPROC 10

#define READY   0
#define RUNNING 1
#define SLEEP   2
#define DONE    3

typedef struct {
    pid_t pid;
    int state;
    int tq;
    int io;
    int wait;
} PCB;

static PCB pcb[NPROC];

static int rq[NPROC];
static int rq_head = 0, rq_tail = 0, rq_cnt = 0;

static void rq_push(int idx) {
    if (rq_cnt == NPROC) return;
    rq[rq_tail] = idx;
    rq_tail = (rq_tail + 1) % NPROC;
    rq_cnt++;
}

static int rq_pop(void) {
    if (rq_cnt == 0) return -1;
    int v = rq[rq_head];
    rq_head = (rq_head + 1) % NPROC;
    rq_cnt--;
    return v;
}

static int rq_empty(void) { return rq_cnt == 0; }

static int running = -1;
static int finished = 0;
static int TQ_INIT = 2;

static int total_ticks = 0;
static int ctx_switches = 0;
static int idle_ticks = 0;

static volatile sig_atomic_t tick_flag = 0;
static volatile sig_atomic_t io_flag = 0;
static volatile sig_atomic_t chld_flag = 0;
static volatile sig_atomic_t preempt_flag = 0;

static int rnd(int a, int b) { return a + (rand() % (b - a + 1)); }

static void on_alarm(int sig) {
    (void)sig;
    tick_flag = 1;
    alarm(1);
}

static void on_io(int sig) {
    (void)sig;
    io_flag = 1;
}

static void on_chld(int sig) {
    (void)sig;
    chld_flag = 1;
}

static volatile sig_atomic_t child_burst = 0;
static volatile sig_atomic_t exit_after_io = 0;
static pid_t parent_pid = 0;

static void on_run(int sig) {
    (void)sig;

    if (exit_after_io) {
        _exit(0);
    }

    if (child_burst > 0) child_burst--;

    if (child_burst == 0) {
        int do_io = rand() % 2;
        if (do_io) {
            exit_after_io = 1;
            kill(parent_pid, SIGUSR1);
            return;
        } else {
            _exit(0);
        }
    }
}

static void child_main(void) {
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    parent_pid = getppid();
    child_burst = rnd(1, 10);
    signal(SIGUSR2, on_run);
    while (1) pause();
}

static void reap_children(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < NPROC; i++) {
            if (pcb[i].pid == pid && pcb[i].state != DONE) {
                pcb[i].state = DONE;
                finished++;
                if (running == i) running = -1;
                break;
            }
        }
    }
}

static int all_tq_zero_except_done(void) {
    for (int i = 0; i < NPROC; i++) {
        if (pcb[i].state != DONE && pcb[i].tq > 0) return 0;
    }
    return 1;
}

static void reset_all_tq(void) {
    for (int i = 0; i < NPROC; i++) {
        if (pcb[i].state != DONE) pcb[i].tq = TQ_INIT;
    }
}

static void dispatch_next(void) {
    if (running != -1) return;

    if (all_tq_zero_except_done()) reset_all_tq();

    int tries = NPROC;
    while (!rq_empty() && tries--) {
        int idx = rq_pop();
        if (idx < 0 || idx >= NPROC) break;
        if (pcb[idx].state != READY) continue;
        if (pcb[idx].tq == 0) { rq_push(idx); continue; }
        pcb[idx].state = RUNNING;
        running = idx;
        ctx_switches++;
        return;
    }
}

static void handle_io_request(void) {
    if (running == -1) return;
    pcb[running].state = SLEEP;
    pcb[running].io = rnd(1, 5);
    running = -1;
}

static void schedule_tick(void) {
    total_ticks++;

    for (int i = 0; i < NPROC; i++) {
        if (pcb[i].state == SLEEP) {
            pcb[i].io--;
            if (pcb[i].io <= 0) {
                pcb[i].io = 0;
                pcb[i].state = READY;
                rq_push(i);
            }
        }
    }

    for (int i = 0; i < NPROC; i++) {
        if (pcb[i].state == READY) pcb[i].wait++;
    }

    if (running != -1) {
        kill(pcb[running].pid, SIGUSR2);
        pcb[running].tq--;
        if (pcb[running].tq < 0) pcb[running].tq = 0;
        if (pcb[running].tq == 0) preempt_flag = 1;
    }
}

static void apply_preempt_if_needed(void) {
    if (!preempt_flag) return;
    preempt_flag = 0;

    if (running == -1) return;
    if (pcb[running].state != RUNNING) return;
    if (pcb[running].tq != 0) return;

    pcb[running].state = READY;
    rq_push(running);
    running = -1;
}

int main(int argc, char *argv[]) {
    if (argc >= 2) TQ_INIT = atoi(argv[1]);
    if (TQ_INIT <= 0) TQ_INIT = 2;

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    signal(SIGALRM, on_alarm);
    signal(SIGUSR1, on_io);
    signal(SIGCHLD, on_chld);

    for (int i = 0; i < NPROC; i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(1); }
        if (pid == 0) child_main();

        pcb[i].pid = pid;
        pcb[i].state = READY;
        pcb[i].tq = TQ_INIT;
        pcb[i].io = 0;
        pcb[i].wait = 0;
        rq_push(i);
    }

    dispatch_next();
    alarm(1);

    while (finished < NPROC) {
        pause();

        if (tick_flag) {
            tick_flag = 0;
            schedule_tick();
        }

        if (io_flag) {
            io_flag = 0;
            handle_io_request();
        }

        apply_preempt_if_needed();
        dispatch_next();

        if (running == -1 && rq_empty()) idle_ticks++;

        if (chld_flag) {
            chld_flag = 0;
            reap_children();
        }
    }

    long sum = 0;
    int maxw = 0;
    int minw = INT_MAX;

    for (int i = 0; i < NPROC; i++) {
        sum += pcb[i].wait;
        if (pcb[i].wait > maxw) maxw = pcb[i].wait;
        if (pcb[i].wait < minw) minw = pcb[i].wait;
    }

    printf("TQ=%d\n", TQ_INIT);
    printf("total_ticks=%d, ctx_switches=%d, idle_ticks=%d\n", total_ticks, ctx_switches, idle_ticks);
    printf("avg_wait=%.2f, min_wait=%d, max_wait=%d\n", (double)sum / (double)NPROC, minw, maxw);

    printf("wait_each=[");
    for (int i = 0; i < NPROC; i++) {
        printf("%d", pcb[i].wait);
        if (i != NPROC - 1) printf(", ");
    }
    printf("]\n");

    return 0;
}

