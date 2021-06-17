/**
 * Copyright (C) Mellanox Technologies Ltd. 2020-2021.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#include "cl_hier.h"
#include "utils/ucc_malloc.h"
#include "core/ucc_team.h"

ucc_status_t ucc_cl_hier_oob_allgather(void *src_buf, void *recv_buf, size_t size,
                                       void *allgather_info,  void **request)
{
    ucc_sbgp_t *sbgp = (ucc_sbgp_t*)allgather_info;
    ucc_team_t *team = sbgp->team;
    ucc_tl_iface_t  *tl_iface = UCC_TL_TEAM_IFACE(team->service_team);
    ucc_tl_team_subset_t subset = {
        .map.type   = UCC_EP_MAP_ARRAY,
        .map.array.map = sbgp->rank_map,
        .map.array.elem_size = sizeof(ucc_rank_t),
        .map.ep_num = sbgp->group_size,
        .myrank     = sbgp->group_rank,
    };
    ucc_status_t status;
    ucc_coll_task_t *task;
    status = tl_iface->scoll.allgather(
        &team->service_team->super, src_buf, recv_buf, size,
        subset, &task);
    if (status < 0) {
        ucc_error("failed to start service allgather in cl hier pair creation");
        return status;
    }
    *request = (void*)task;
    return status;
}

ucc_status_t ucc_cl_hier_oob_req_test(void *request)
{
    ucc_coll_task_t *task = (ucc_coll_task_t*)request;
    ucc_context_progress(task->team->context->ucc_context);
    return task->super.status;
}

ucc_status_t ucc_cl_hier_oob_req_free(void *request)
{
    ucc_coll_task_t *task = (ucc_coll_task_t*)request;
    return task->finalize(task);
}

UCC_CLASS_INIT_FUNC(ucc_cl_hier_team_t, ucc_base_context_t *cl_context,
                    const ucc_base_team_params_t *params)
{
    ucc_cl_hier_context_t *ctx =
        ucc_derived_of(cl_context, ucc_cl_hier_context_t);
    ucc_cl_hier_lib_t *lib = ucc_derived_of(cl_context->lib, ucc_cl_hier_lib_t);
    int                     i, j;
    ucc_status_t            status;
    ucc_hier_sbgp_t *hs;
    ucc_config_names_array_t *tls;

    UCC_CLASS_CALL_SUPER_INIT(ucc_cl_team_t, &ctx->super, params->team);
    if (!params->team->topo) {
        cl_info(cl_context->lib, "can't create hier team without topology data");
        return UCC_ERR_INVALID_PARAM;
    }
    self->sbgps[UCC_HIER_SBGP_NODE].state = UCC_HIER_SBGP_ENABLED;
    self->sbgps[UCC_HIER_SBGP_NODE].sbgp_type = UCC_SBGP_NODE;
    self->sbgps[UCC_HIER_SBGP_NET].state = UCC_HIER_SBGP_ENABLED;
    self->sbgps[UCC_HIER_SBGP_NET].sbgp_type = UCC_SBGP_NET;
    self->sbgps[UCC_HIER_SBGP_NODE2].state = UCC_HIER_SBGP_ENABLED;
    self->sbgps[UCC_HIER_SBGP_NODE2].sbgp_type = UCC_SBGP_NODE;
    int n_sbgp_teams = 0;
    for (i = 0; i < UCC_HIER_SBGP_LAST; i++) {
        hs = &self->sbgps[i];
        hs->score = NULL;
        if (hs->state == UCC_HIER_SBGP_ENABLED) {
            hs->sbgp = ucc_team_topo_get_sbgp(params->team->topo,
                                              hs->sbgp_type);
            if (hs->sbgp->status != UCC_SBGP_ENABLED) {
                cl_debug(cl_context->lib, "sbgp %s is not enabled",
                          ucc_sbgp_type_str[hs->sbgp_type]);
                hs->state = UCC_HIER_SBGP_DISABLED;
            }
            hs->n_tls = 0;
            tls = &lib->cfg.sbgp_tls[i];
            for (j = 0; j < tls->count; j++) {
                status = ucc_tl_context_get(ctx->super.super.ucc_context,
                                            tls->names[j], &hs->tl_ctxs[hs->n_tls]);
                if (UCC_OK != status) {
                    cl_warn(cl_context->lib, "tl context %s is not available for sbgp %s",
                             tls->names[j], ucc_sbgp_type_str[hs->sbgp_type]);
                } else {
                    hs->n_tls++;
                    n_sbgp_teams++;
                }

            }
        }
    }
    status = ucc_team_multiple_req_alloc(&self->team_create_req,
                                         n_sbgp_teams);
    if (UCC_OK != status) {
        cl_error(cl_context->lib, "failed to allocate team req multiple");
        goto err;
    }

    int t;
    j = 0;
    struct ucc_team_team_desc *d;
    self->team_create_req->n_teams = n_sbgp_teams;
    for (i = 0; i < UCC_HIER_SBGP_LAST; i++) {
        hs = &self->sbgps[i];
        for (t = 0; t < hs->n_tls; t++) {
            if (hs->state == UCC_HIER_SBGP_ENABLED) {
                d = &self->team_create_req->descs[j];
                d->param.team = params->team;
                d->param.rank = hs->sbgp->group_rank;
                d->param.params.team_size = hs->sbgp->group_size;
                d->param.params.mask = UCC_TEAM_PARAM_FIELD_EP_RANGE |
                    UCC_TEAM_PARAM_FIELD_EP | UCC_TEAM_PARAM_FIELD_TEAM_SIZE |
                    UCC_TEAM_PARAM_FIELD_OOB;
                d->param.params.ep = (uint64_t)hs->sbgp->group_rank;
                d->param.params.ep_range = UCC_COLLECTIVE_EP_RANGE_CONTIG;
                d->ctx            = hs->tl_ctxs[t];
                d->param.scope    = UCC_CL_HIER;
                d->param.id = params->id;
                d->param.scope_id = i;
                d->param.map.type = UCC_EP_MAP_ARRAY;
                d->param.map.array.map = hs->sbgp->rank_map;
                d->param.map.array.elem_size = sizeof(ucc_rank_t);
                d->param.map.ep_num = hs->sbgp->group_size;
                d->param.params.oob.allgather = ucc_cl_hier_oob_allgather;
                d->param.params.oob.req_test = ucc_cl_hier_oob_req_test;
                d->param.params.oob.req_free = ucc_cl_hier_oob_req_free;
                d->param.params.oob.participants = hs->sbgp->group_size;
                d->param.params.oob.coll_info = (void*)hs->sbgp;
                d->args[0] = i;
                d->args[1] = t;
                j++;
            }
        }
    }

    status = ucc_tl_team_create_multiple(self->team_create_req);
    if (status < 0) {
        cl_error(cl_context->lib, "failed to post tl team create (%d)",
                 status);
        goto err;
    }
    cl_info(cl_context->lib, "posted cl team: %p", self);
    return UCC_OK;
err:
    return status;
}

UCC_CLASS_CLEANUP_FUNC(ucc_cl_hier_team_t)
{
    cl_info(self->super.super.context->lib, "finalizing cl team: %p", self);
}

UCC_CLASS_DEFINE_DELETE_FUNC(ucc_cl_hier_team_t, ucc_base_team_t);
UCC_CLASS_DEFINE(ucc_cl_hier_team_t, ucc_cl_team_t);

ucc_status_t ucc_cl_hier_team_destroy(ucc_base_team_t *cl_team)
{
    ucc_cl_hier_team_t    *team    = ucc_derived_of(cl_team, ucc_cl_hier_team_t);
    ucc_cl_hier_context_t *ctx     = UCC_CL_HIER_TEAM_CTX(team);
    ucc_status_t            status  = UCC_OK;
    int                     i, j;
    ucc_hier_sbgp_t *hs;


    if (NULL == team->team_create_req) {
        status = ucc_team_multiple_req_alloc(&team->team_create_req,
                                             team->n_tl_teams);
        if (UCC_OK != status) {
            cl_error(ctx->super.super.lib, "failed to allocate team req multiple");
            return status;
        }
        team->team_create_req->n_teams       = 0;
        for (i = 0; i < UCC_HIER_SBGP_LAST; i++) {
            hs = &team->sbgps[i];
            if (hs->state == UCC_HIER_SBGP_ENABLED) {
                ucc_coll_score_free_map(hs->score_map);
                for (j = 0; j < hs->n_tls; j++) {
                    ucc_tl_context_put(hs->tl_ctxs[j]);
                    team->team_create_req->descs[team->team_create_req->n_teams++].team = 
                        hs->tl_teams[j];
                }
            }
        }
    }
    status = ucc_tl_team_destroy_multiple(team->team_create_req);
    if (UCC_INPROGRESS == status) {
        return status;
    }
    for (i = 0; i < team->team_create_req->n_teams; i++) {
        if (team->team_create_req->descs[i].status != UCC_OK) {
            cl_error(ctx->super.super.lib, "tl team destroy failed (%d)",
                     status);
            status = team->team_create_req->descs[i].status;
        }
    }
    ucc_team_multiple_req_free(team->team_create_req);
    UCC_CLASS_DELETE_FUNC_NAME(ucc_cl_hier_team_t)(cl_team);
    return status;
}

ucc_status_t ucc_cl_hier_team_create_test(ucc_base_team_t *cl_team)
{
    ucc_cl_hier_team_t    *team = ucc_derived_of(cl_team, ucc_cl_hier_team_t);
    ucc_cl_hier_context_t *ctx  = UCC_CL_HIER_TEAM_CTX(team);
    ucc_status_t            status;
    int                     i;
    ucc_coll_score_t *score, *score_merge;
    struct ucc_team_team_desc *d;
    ucc_hier_sbgp_t *hs;
    status = ucc_tl_team_create_multiple(team->team_create_req);

    if (status != UCC_OK) {
        return status;
    }

    team->n_tl_teams = 0;

    for (i = 0; i < team->team_create_req->n_teams; i++) {
        d = &team->team_create_req->descs[i];
        ucc_hier_sbgp_type_t st = (ucc_hier_sbgp_type_t)d->args[0];
        int                  tl = (int)d->args[1];
        hs = &team->sbgps[st];
        if (d->status == UCC_OK) {
            hs->tl_teams[tl] = d->team;
            team->n_tl_teams++;
            status = UCC_TL_TEAM_IFACE(d->team)->team.get_scores(&d->team->super,
                                                                 &score);
            if (UCC_OK != status) {
                cl_warn(ctx->super.super.lib, "failed to get tl %s scores",
                        UCC_TL_TEAM_IFACE(d->team)->super.name);
                continue;
                //goto cleanup ?
            }
            if (hs->score == NULL) {
                hs->score = score;
            } else {
                status = ucc_coll_score_merge(hs->score, score, &score_merge, 1);
                if (UCC_OK != status) {
                    cl_warn(ctx->super.super.lib, "failed to merge scores");
                } else {
                    hs->score = score_merge;
                }
            }
            cl_info(ctx->super.super.lib, "initialized tl %s team for sbgp %s",
                    UCC_TL_CTX_IFACE(d->ctx)->super.name, ucc_sbgp_type_str[hs->sbgp_type]);
        } else {
            cl_error(ctx->super.super.lib, "failed to create tl %s team",
                     UCC_TL_CTX_IFACE(d->ctx)-> super.name);
            hs->state = UCC_HIER_SBGP_DISABLED;
            hs->tl_teams[tl] = NULL;
            hs->tl_ctxs[tl] = NULL;
            ucc_tl_context_put(d->ctx);
        }
    }

    for (i = 0; i < UCC_HIER_SBGP_LAST; i++) {
        hs = &team->sbgps[i];
        if (hs->score == NULL) {
            cl_error(ctx->super.super.lib,
                     "no tl teams were created for sbgp %s",
                     ucc_sbgp_type_str[hs->sbgp_type]);
            hs->state = UCC_HIER_SBGP_DISABLED;
        } else {
            status = ucc_coll_score_build_map(hs->score, &hs->score_map);
            if (UCC_OK != status) {
                cl_error(ctx->super.super.lib, "failed to build score map");
                hs->state = UCC_HIER_SBGP_DISABLED;
            }
        }
    }
    ucc_team_multiple_req_free(team->team_create_req);
    team->team_create_req = NULL;


    return status;
}

ucc_status_t ucc_cl_hier_team_get_scores(ucc_base_team_t *cl_team,
                                          ucc_coll_score_t **score_p)
{
    ucc_cl_hier_team_t *team = ucc_derived_of(cl_team, ucc_cl_hier_team_t);
    ucc_base_lib_t      *lib  = UCC_CL_TEAM_LIB(team);
    ucc_coll_score_t  *score;
    ucc_status_t       status;
    ucc_memory_type_t mt[2] = {UCC_MEMORY_TYPE_HOST, UCC_MEMORY_TYPE_CUDA};
    int i;
    status = ucc_coll_score_alloc(&score);
    if (UCC_OK != status) {
        cl_error(lib, "faild to alloc score_t");
        return status;
    }
    for (i = 0; i < 2; i++) {
        status = ucc_coll_score_add_range(score, UCC_COLL_TYPE_ALLREDUCE,
                                          mt[i], 4096, UCC_MSG_MAX,
                                          UCC_CL_HIER_DEFAULT_SCORE, ucc_cl_hier_allreduce_init,
                                          cl_team);
        if (UCC_OK != status) {
            cl_error(lib, "faild to add range to score_t");
            return status;
        }
    }
    if (strlen(lib->score_str) > 0) {
        status = ucc_coll_score_update_from_str(
            lib->score_str, score, cl_team->team->size,
            NULL, cl_team, UCC_CL_HIER_DEFAULT_SCORE, NULL);

        /* If INVALID_PARAM - User provided incorrect input - try to proceed */
        if ((status < 0) && (status != UCC_ERR_INVALID_PARAM) &&
            (status != UCC_ERR_NOT_SUPPORTED)) {
            goto err;
        }
    }
    *score_p = score;
    return UCC_OK;
err:
    ucc_coll_score_free(score);
    *score_p = NULL;
    return status;
}
