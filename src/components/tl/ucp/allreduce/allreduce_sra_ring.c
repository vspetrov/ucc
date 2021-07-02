/**
 * Copyright (C) Mellanox Technologies Ltd. 2021.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#include "config.h"
#include "tl_ucp.h"
#include "allreduce.h"
#include "core/ucc_progress_queue.h"
#include "schedule/ucc_schedule_pipelined.h"
#include "tl_ucp_sendrecv.h"
#include "utils/ucc_math.h"
#include "utils/ucc_coll_utils.h"
#include "core/ucc_mc.h"
#include "../reduce_scatter/reduce_scatter.h"
#include "../allgather/allgather.h"
#include "coll_patterns/ring.h"

ucc_status_t ucc_tl_ucp_allreduce_sra_ring_start(ucc_coll_task_t *coll_task)
{
    ucc_schedule_t *schedule = ucc_derived_of(coll_task, ucc_schedule_t);
    /* static int __count = 0; */
    /* printf("start sra, id %d, task %p, rs tag %u, ag tag %u\n", __count++, coll_task, */
    /*        ucc_derived_of(schedule->tasks[0], ucc_tl_ucp_task_t)->tag, */
    /*        ucc_derived_of(schedule->tasks[1], ucc_tl_ucp_task_t)->tag); */
    return ucc_schedule_start(schedule);
}

static ucc_status_t
ucc_tl_ucp_allreduce_sra_ring_finalize(ucc_coll_task_t *frag)
{
    ucc_status_t status;
    status = ucc_schedule_finalize(frag);
    ucc_free(frag);
    return status;
}

static ucc_status_t
ucc_tl_ucp_allreduce_sra_ring_pipelined_finalize(ucc_coll_task_t *task)
{
    ucc_status_t status;
    status = ucc_schedule_pipelined_finalize(task);
    ucc_free(task);
    return status;
}

ucc_status_t ucc_tl_ucp_allreduce_sra_ring_setup_frag(ucc_schedule_pipelined_t *schedule_p,
                               ucc_schedule_t *frag, int frag_num)
{
    ucc_coll_args_t *args = &schedule_p->super.super.args;
    ucc_datatype_t     dt        = args->src.info.datatype;
    size_t             dt_size   = ucc_dt_size(dt);
    ucc_coll_args_t *targs    ;
    int n_frags = schedule_p->super.n_tasks;
    size_t frag_count = args->src.info.count / n_frags;
    size_t left = args->src.info.count % n_frags;
    size_t offset = frag_num * frag_count + left;
    ucc_tl_ucp_team_t *team = ucc_derived_of(schedule_p->super.super.team, ucc_tl_ucp_team_t);

    if (frag_num < left) {
        frag_count++;
        offset -= left - frag_num;
    }

    size_t block_offset;

    block_offset = ucc_ring_block_offset(frag_count, team->size, team->rank);

    targs = &frag->tasks[0]->args; //REDUCE_SCATTER
    targs->src.info.buffer = PTR_OFFSET(args->src.info.buffer, offset * dt_size);
    targs->dst.info.buffer = PTR_OFFSET(args->dst.info.buffer, (offset + block_offset) * dt_size);
    targs->src.info.count = frag_count;
    targs->dst.info.count = frag_count;

    targs = &frag->tasks[1]->args; //ALLGATHER
    targs->src.info.buffer = NULL;
    targs->dst.info.buffer = PTR_OFFSET(args->dst.info.buffer, offset * dt_size);
    targs->src.info.count = 0;
    targs->dst.info.count = frag_count;

    return UCC_OK;
}

ucc_status_t
ucc_tl_ucp_allreduce_sra_ring_init_frag(ucc_base_coll_args_t *coll_args,
                                           ucc_schedule_pipelined_t *sp, //NOLINT
                                           ucc_base_team_t      *team,
                                           ucc_schedule_t  **frag_p)
{
    ucc_tl_ucp_team_t   *tl_team  = ucc_derived_of(team, ucc_tl_ucp_team_t);
    ucc_schedule_t      *schedule = ucc_malloc(sizeof(*schedule), "sra_frag");
    ucc_base_coll_args_t args     = *coll_args;
    ucc_coll_task_t     *task, *rs_task;
    ucc_status_t         status;

    ucc_schedule_init(schedule, &coll_args->args, team, 0);

    /* for inplace AR user might set only src however for the
       inplace allgather below we will need everything in dst since
       it is inplace */

    /* args.args.src.info.count = */
        /* ucc_ring_block_count(coll_args->args.src.info.count, tl_team->size, tl_team->rank); */

    args.args.dst.info.datatype = args.args.src.info.datatype;
    args.args.dst.info.mem_type = args.args.src.info.mem_type;
    args.args.dst.info.count = args.args.src.info.count;


    /* 1st step of allreduce: ring reduce_scatter */
    status = ucc_tl_ucp_reduce_scatter_ring_init(&args, team, &task);
    if (UCC_OK != status) {
        tl_error(UCC_TL_TEAM_LIB(tl_team),
                 "failed to init reduce_scatter_ring task");
        goto out;
    }
    task->n_deps = 1;
    ucc_schedule_add_task(schedule, task);
    ucc_event_manager_subscribe(&schedule->super.em, UCC_EVENT_SCHEDULE_STARTED,
                                task, ucc_dependency_handler);

    rs_task                                    = task;
    /* 2nd step of allreduce: ring allgather. 2nd task subscribes
     to completion event of reduce_scatter task. */
    args.args.mask |= UCC_COLL_ARGS_FIELD_FLAGS;
    args.args.flags |= UCC_COLL_ARGS_FLAG_IN_PLACE;
    status = ucc_tl_ucp_allgather_ring_init(&args, team, &task);
    if (UCC_OK != status) {
        tl_error(UCC_TL_TEAM_LIB(tl_team),
                 "failed to init allgather_ring task");
        goto out;
    }
    task->n_deps = 1;
    ucc_schedule_add_task(schedule, task);
    ucc_event_manager_subscribe(&rs_task->em, UCC_EVENT_COMPLETED, task,
        ucc_dependency_handler);
    schedule->super.finalize = ucc_tl_ucp_allreduce_sra_ring_finalize;
    schedule->super.post = ucc_tl_ucp_allreduce_sra_ring_start;
    *frag_p = schedule;
    return UCC_OK;
out:
    return status;
}

static inline void get_sra_n_frags(ucc_base_coll_args_t *coll_args,
                                   ucc_tl_ucp_team_t      *team,
                                   int *n_frags, int *pipeline_depth)
{
//    ucc_memory_type_t mt = coll_args->args.src.info.mem_type;
    //TODO make selection mem_type - specific
    ucc_tl_ucp_lib_config_t *cfg = &UCC_TL_UCP_TEAM_LIB(team)->cfg;
    size_t msgsize = coll_args->args.src.info.count *
        ucc_dt_size(coll_args->args.src.info.datatype);
    *n_frags = 1;
    if (msgsize > cfg->allreduce_sra_ring_frag_thresh) {
        int min_num_frags = msgsize/cfg->allreduce_sra_ring_frag_size;
        *n_frags = ucc_max(min_num_frags,
                          cfg->allreduce_sra_ring_n_frags);
    }
    *pipeline_depth = ucc_min(*n_frags, cfg->allreduce_sra_ring_pipeline_depth);
    /* printf("pd %d, n_fragas %d\n", *pipeline_depth, *n_frags); */
}


ucc_status_t
ucc_tl_ucp_allreduce_sra_ring_init(ucc_base_coll_args_t *coll_args,
                                      ucc_base_team_t      *team,
                                      ucc_coll_task_t     **task_h)
{
    ucc_tl_ucp_team_t *tl_team = ucc_derived_of(team, ucc_tl_ucp_team_t);
    ucc_tl_ucp_lib_config_t *cfg = &UCC_TL_UCP_TEAM_LIB(tl_team)->cfg;
    int n_frags, pipeline_depth;
    ucc_schedule_pipelined_t *schedule_p = ucc_malloc(sizeof(*schedule_p), "sched_pipelined");
    if (!schedule_p) {
        tl_error(team->context->lib, "failed to allocate %zd bytes for sra schedule pipelined",
                 sizeof(*schedule_p));
        return UCC_ERR_NO_MEMORY;
    }
    get_sra_n_frags(coll_args, tl_team, &n_frags, &pipeline_depth);
    ucc_schedule_pipelined_init(coll_args, team, ucc_tl_ucp_allreduce_sra_ring_init_frag,
                                ucc_tl_ucp_allreduce_sra_ring_setup_frag,
                                pipeline_depth, n_frags, cfg->allreduce_sra_ring_seq,
                                schedule_p);
    schedule_p->super.super.finalize       =
        ucc_tl_ucp_allreduce_sra_ring_pipelined_finalize;
    schedule_p->super.super.triggered_post = ucc_coll_triggered_post_common;
    *task_h                  = &schedule_p->super.super;
    return UCC_OK;
}
