/*
 * Copyright (C) 2021 Greg Becker.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <getopt.h>
#include <time.h>
#include <pthread.h>
#include <sysexits.h>
#include <sys/param.h>
#include <sys/resource.h>

#if defined(__FreeBSD__) || defined(__APPLE__)
#include <sys/time.h>
#endif

#ifndef timespecsub
#define timespecsub(_tsp, _usp, _vsp)                          \
    do {                                                       \
        (_vsp)->tv_sec = (_tsp)->tv_sec - (_usp)->tv_sec;      \
        (_vsp)->tv_nsec = (_tsp)->tv_nsec - (_usp)->tv_nsec;   \
        if ((_vsp)->tv_nsec < 0) {                             \
            (_vsp)->tv_sec--;                                  \
            (_vsp)->tv_nsec += 1000000000L;                    \
        }                                                      \
    } while (0)
#endif

#include "lfstack.h"


/* If pthread barriers don't exist (e.g., Darwin) then implement
 * a very crude implementation...
 */
#ifndef PTHREAD_BARRIER_SERIAL_THREAD
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

typedef struct {
    atomic_uint count;
    atomic_uint max;
    u_int       init;
} pthread_barrier_t;

int
pthread_barrier_init(pthread_barrier_t *bar, void *attr, u_int count)
{
    bar->count = 0;
    bar->max = count;
    bar->init = count;

    return 0;
}

int
pthread_barrier_wait(pthread_barrier_t *bar)
{
    atomic_fetch_add(&bar->count, 1);

    while (bar->count < bar->max)
        usleep(100);

    if (atomic_fetch_sub(&bar->max, 1) == 0) {
        bar->max = bar->init;
        bar->count = 0;
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }

    return 0;
}
#endif


struct mynode {
    u_long count;
};

struct tdargs {
    alignas(128)
    struct lfstack *lfstack;
    pthread_t       ptid;
    int             ltid;
    u_long          empty;
};

struct {
    alignas(128)
    pthread_barrier_t bar_start, bar_done;
    struct timespec   ts_start, ts_done, ts_delta;
    u_long            itermax, count;
} g;


u_int maxnodes, maxbkts, maxjobs;
const char *progname;


void *
work(void *arg)
{
    struct tdargs *args = arg;
    struct mynode *node;
    int i;

    node = aligned_alloc(128, roundup(sizeof(*node), 128));
    if (!node)
        abort();

    memset(node, 0, sizeof(*node));
    lfstack_push(args->lfstack, node);

    if (pthread_barrier_wait(&g.bar_start))
        clock_gettime(CLOCK_MONOTONIC, &g.ts_start);

    for (i = 0; i < g.itermax; ++i) {
        do {
            node = lfstack_pop(args->lfstack);
            if (!node)
                args->empty++;
        } while (!node);

        node->count++;

        lfstack_push(args->lfstack, node);
    }

    if (pthread_barrier_wait(&g.bar_done))
        clock_gettime(CLOCK_MONOTONIC, &g.ts_done);

    pthread_exit(NULL);
}

void
mydtor(void *arg)
{
    struct mynode *node = arg;

    g.count += node->count;
    free(node);
}

void
usage(void)
{
    printf("usage: %s [options]\n", progname);
    printf("-b bkts   max buckets in lfstack cache (default: %u)\n", maxbkts);
    printf("-h        print this help list\n");
    printf("-j jobs   max threads to spawn (default: %u)\n", maxjobs);
    printf("-n nodes  max nodes per bucket (default: %u)\n", maxnodes);
}

int
main(int argc, char **argv)
{
    struct lfstack *lfstack;
    struct tdargs *tdargsv;
    size_t tdargsvsz;
    u_int tdargsc;
    long elapsed;
    u_long empty;
    int rc, c, i;

    progname = strchr(argv[0], '/');
    progname = progname ? progname + 1 : argv[0];

    maxnodes = 12;
    maxjobs = 12;
    maxbkts = 8;
    empty = 0;

    while (-1 != (c = getopt(argc, argv, ":b:hj:n:"))) {
        switch (c) {
        case 'b':
            maxbkts = strtoul(optarg, NULL, 0);
            break;

        case 'h':
            usage();
            exit(0);

        case 'j':
            maxjobs = strtoul(optarg, NULL, 0);
            break;

        case 'n':
            maxnodes = strtoul(optarg, NULL, 0);
            break;

        case ':':
            fprintf(stderr, "%s: option -%c requires a parameter, use -h for help\n",
                    progname, optopt);
            exit(EX_USAGE);

        case '?':
            fprintf(stderr, "%s: invalid option -%c, use -h for help\n",
                    progname, optopt);
            exit(EX_USAGE);

        default:
            fprintf(stderr, "%s: unhandled option -%c ignored\n", progname, c);
            break;
        }
    }

    tdargsc = maxjobs;
    tdargsvsz = sizeof(*tdargsv) * tdargsc;

    g.itermax = (128 * 1024 * 1024) / tdargsc;
    if (g.itermax > 4 * 1024 * 1024)
        g.itermax = 4 * 1024 * 1024;

    lfstack = lfstack_create(maxnodes);
    if (!lfstack)
        exit(1);

    pthread_barrier_init(&g.bar_start, NULL, tdargsc);
    pthread_barrier_init(&g.bar_done, NULL, tdargsc);

    tdargsv = aligned_alloc(__alignof(*tdargsv), tdargsvsz);
    if (!tdargsv)
        abort();

    memset(tdargsv, 0, tdargsvsz);

    for (i = 0; i < tdargsc; ++i) {
        tdargsv[i].ltid = i;
        tdargsv[i].lfstack = lfstack;
        rc = pthread_create(&tdargsv[i].ptid, NULL, work, &tdargsv[i]);
        if (rc)
            exit(EX_OSERR);
    }

    for (i = 0; i < tdargsc; ++i) {
        pthread_join(tdargsv[i].ptid, NULL);

        empty += tdargsv[i].empty;
    }

    timespecsub(&g.ts_done, &g.ts_start, &g.ts_delta);

    lfstack_destroy(lfstack, mydtor);

    elapsed = g.ts_delta.tv_sec * 1000000000 + g.ts_delta.tv_nsec;

    printf("elapsed %.3lf, us/iter %.3lf, count %lu, empty %lu\n",
           elapsed / 1000000000.0, (elapsed / g.itermax) / 1000.0,
           g.count, empty);

    if (g.count != g.itermax * tdargsc) {
        fprintf(stderr, "error: got %lu, expected %lu\n", g.count, g.itermax * tdargsc);
        exit(1);
    }

    return 0;
}
