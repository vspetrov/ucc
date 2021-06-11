/**
 * Copyright (C) Mellanox Technologies Ltd. 2020.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#include "cl_hier.h"
#include "utils/ucc_malloc.h"
ucc_status_t ucc_cl_hier_get_lib_attr(const ucc_base_lib_t *lib,
                                       ucc_base_lib_attr_t  *base_attr);
ucc_status_t ucc_cl_hier_get_context_attr(const ucc_base_context_t *context,
                                           ucc_base_ctx_attr_t      *base_attr);

static ucc_config_field_t ucc_cl_hier_lib_config_table[] = {
    {"", "", NULL, ucc_offsetof(ucc_cl_hier_lib_config_t, super),
     UCC_CONFIG_TYPE_TABLE(ucc_cl_lib_config_table)},

    {"ALLREDUCE_HYBRID_FRAG_THRESH", "inf",
     "Threshold to enable fragmentation and pipelining of Hybrid allreduce alg",
     ucc_offsetof(ucc_cl_hier_lib_config_t, allreduce_hybrid_frag_thresh),
     UCC_CONFIG_TYPE_MEMUNITS},

    {"ALLREDUCE_HYBRID_FRAG_SIZE", "inf",
     "Maximum allowed fragment size of hybrid allreduce algorithm",
     ucc_offsetof(ucc_cl_hier_lib_config_t, allreduce_hybrid_frag_size),
     UCC_CONFIG_TYPE_MEMUNITS},

    {"ALLREDUCE_HYBRID_N_FRAGS", "2",
     "Number of fragments each allreduce is split into when hybrid alg is used\n"
     "The actual number of fragments can be larger if fragment size exceeds\n"
     "ALLREDUCE_HYBRID_FRAG_SIZE",
     ucc_offsetof(ucc_cl_hier_lib_config_t, allreduce_hybrid_n_frags),
     UCC_CONFIG_TYPE_UINT},

    {"ALLREDUCE_HYBRID_PIPELINE_DEPTH", "2",
     "Number of fragments simultaneously progressed by the hybrid alg",
     ucc_offsetof(ucc_cl_hier_lib_config_t, allreduce_hybrid_pipeline_depth),
     UCC_CONFIG_TYPE_UINT},

    {NULL}
};

static ucs_config_field_t ucc_cl_hier_context_config_table[] = {
    {"", "", NULL, ucc_offsetof(ucc_cl_hier_context_config_t, super),
     UCC_CONFIG_TYPE_TABLE(ucc_cl_context_config_table)},

    {NULL}
};

UCC_CLASS_DEFINE_NEW_FUNC(ucc_cl_hier_lib_t, ucc_base_lib_t,
                          const ucc_base_lib_params_t *,
                          const ucc_base_config_t *);

UCC_CLASS_DEFINE_DELETE_FUNC(ucc_cl_hier_lib_t, ucc_base_lib_t);

UCC_CLASS_DEFINE_NEW_FUNC(ucc_cl_hier_context_t, ucc_base_context_t,
                          const ucc_base_context_params_t *,
                          const ucc_base_config_t *);

UCC_CLASS_DEFINE_DELETE_FUNC(ucc_cl_hier_context_t, ucc_base_context_t);

UCC_CLASS_DEFINE_NEW_FUNC(ucc_cl_hier_team_t, ucc_base_team_t,
                          ucc_base_context_t *, const ucc_base_team_params_t *);

ucc_status_t ucc_cl_hier_team_create_test(ucc_base_team_t *cl_team);

ucc_status_t ucc_cl_hier_team_destroy(ucc_base_team_t *cl_team);

ucc_status_t ucc_cl_hier_team_get_scores(ucc_base_team_t   *cl_team,
                                          ucc_coll_score_t **score);
UCC_CL_IFACE_DECLARE(hier, HIER);
