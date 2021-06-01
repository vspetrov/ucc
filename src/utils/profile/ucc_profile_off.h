/**
 * Copyright (C) Mellanox Technologies Ltd. 2021.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#ifndef UCC_PROFILE_OFF_H_
#define UCC_PROFILE_OFF_H_

#define UCC_PROFILE_FUNC(_ret_type, _name, _arglist, ...)  _ret_type _name(__VA_ARGS__)
#define UCC_PROFILE_REQUEST_NEW(...)                        UCS_EMPTY_STATEMENT
#define UCC_PROFILE_REQUEST_EVENT(...)                      UCS_EMPTY_STATEMENT
#define UCC_PROFILE_REQUEST_FREE(...)                       UCS_EMPTY_STATEMENT

#endif
