/**
 * Copyright (C) Mellanox Technologies Ltd. 2021.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#include "config.h"
#include "tl_ucp.h"
#include "alltoall.h"

ucc_status_t ucc_tl_ucp_alltoall_pairwise_start(ucc_coll_task_t *task);
ucc_status_t ucc_tl_ucp_alltoall_pairwise_progress(ucc_coll_task_t *task);

ucc_status_t ucc_tl_ucp_alltoall_init(ucc_tl_ucp_task_t *task)
{
    if ((task->super.args.mask & UCC_COLL_ARGS_FIELD_FLAGS) &&
        (task->super.args.flags & UCC_COLL_ARGS_FLAG_IN_PLACE)) {
        tl_debug(UCC_TASK_LIB(task),
                 "inplace alltoall is not supported");
        return UCC_ERR_NOT_SUPPORTED;
    }
    if ((task->super.args.src.info.datatype == UCC_DT_USERDEFINED) ||
        (task->super.args.dst.info.datatype == UCC_DT_USERDEFINED)) {
        tl_debug(UCC_TASK_LIB(task),
                 "user defined datatype is not supported");
        return UCC_ERR_NOT_SUPPORTED;
    }
    task->super.post     = ucc_tl_ucp_alltoall_pairwise_start;
    task->super.progress = ucc_tl_ucp_alltoall_pairwise_progress;
    return UCC_OK;
}
