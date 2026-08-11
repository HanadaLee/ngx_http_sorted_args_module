
/*
 * Copyright (c) Hanada
 * Copyright (c) 2014 Wandenberg Peixoto
 */


#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#if (NGX_CONDITION)
#include <ngx_http_condition_module.h>
#endif


#define NGX_HTTP_SORTED_ARGS_VARIABLE_ARGS        0
#define NGX_HTTP_SORTED_ARGS_VARIABLE_IS_ARGS     1
#define NGX_HTTP_SORTED_ARGS_VARIABLE_HAS_ARGS    2

#define NGX_HTTP_SORTED_ARGS_MODE_OFF             0
#define NGX_HTTP_SORTED_ARGS_MODE_KEEP            1
#define NGX_HTTP_SORTED_ARGS_MODE_REMOVE          2
#define NGX_HTTP_SORTED_ARGS_MODE_CLEAR           3

#define NGX_HTTP_SORTED_ARGS_FILTER_EXACT         0
#define NGX_HTTP_SORTED_ARGS_FILTER_PREFIX        1
#define NGX_HTTP_SORTED_ARGS_FILTER_SUFFIX        2

#define NGX_HTTP_SORTED_ARGS_ORDER_ASC            0
#define NGX_HTTP_SORTED_ARGS_ORDER_DESC           1

#define NGX_HTTP_SORTED_ARGS_DEDUPE_OFF           0
#define NGX_HTTP_SORTED_ARGS_DEDUPE_FIRST         1
#define NGX_HTTP_SORTED_ARGS_DEDUPE_LAST          2


typedef struct {
    ngx_str_t                 name;
    ngx_uint_t                wildcard;
} ngx_http_sorted_args_filter_t;


typedef struct {
    ngx_array_t              *args;
    ngx_uint_t                mode;
    ngx_flag_t                case_insensitive;
} ngx_http_sorted_args_filter_conf_t;


typedef struct {
#if (NGX_CONDITION)
    ngx_array_t              *filter;
    ngx_array_t              *order;
    ngx_array_t              *dedupe;
    ngx_array_t              *clear_valueless_args;
    ngx_array_t              *clear_invalid_args;
    ngx_array_t              *overwrite;
#else
    ngx_http_sorted_args_filter_conf_t  *filter;
    ngx_uint_t                order;
    ngx_uint_t                dedupe;
    ngx_flag_t                clear_valueless_args;
    ngx_flag_t                clear_invalid_args;
    ngx_flag_t                overwrite;
#endif
} ngx_http_sorted_args_loc_conf_t;


typedef struct {
    ngx_http_sorted_args_filter_conf_t  *filter;
    ngx_uint_t                           order;
    ngx_uint_t                           dedupe;
    ngx_flag_t                           clear_valueless_args;
    ngx_flag_t                           clear_invalid_args;
} ngx_http_sorted_args_runtime_conf_t;


typedef struct {
    ngx_queue_t               args_queue;
} ngx_http_sorted_args_ctx_t;


typedef struct {
    ngx_queue_t               queue;
    ngx_str_t                 key;
    ngx_str_t                 complete;
    ngx_uint_t                index;
} ngx_http_sorted_args_arg_t;


static ngx_int_t ngx_http_sorted_args_add_variables(ngx_conf_t *cf);
static ngx_int_t ngx_http_sorted_args_init(ngx_conf_t *cf);

static void *ngx_http_sorted_args_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_sorted_args_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
static char *ngx_http_sorted_args_filter(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);

static ngx_int_t ngx_http_sorted_args_cmp_args(const ngx_queue_t *one,
    const ngx_queue_t *two);
static ngx_int_t ngx_http_sorted_args_process(ngx_http_request_t *r,
    ngx_str_t *result);
static ngx_int_t ngx_http_sorted_args_str_eq(ngx_str_t *one, ngx_str_t *two,
    ngx_flag_t case_insensitive);
static ngx_int_t ngx_http_sorted_args_str_cmp(ngx_str_t *one, ngx_str_t *two);
static ngx_int_t ngx_http_sorted_args_match_filter(
    ngx_http_sorted_args_filter_conf_t *filter_conf,
    ngx_http_sorted_args_filter_t *filter, ngx_str_t *key);
static ngx_int_t ngx_http_sorted_args_should_output_arg(
    ngx_http_sorted_args_runtime_conf_t *rcf,
    ngx_http_sorted_args_arg_t *arg);
static ngx_int_t ngx_http_sorted_args_same_key(
    ngx_http_sorted_args_arg_t *one, ngx_http_sorted_args_arg_t *two);
static ngx_int_t ngx_http_sorted_args_dedupe_arg(
    ngx_http_sorted_args_runtime_conf_t *rcf,
    ngx_http_sorted_args_ctx_t *ctx,
    ngx_http_sorted_args_arg_t *arg);

static ngx_int_t ngx_http_sorted_args_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_sorted_args_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);


static ngx_conf_enum_t  ngx_http_sorted_args_order[] = {
    { ngx_string("asc"), NGX_HTTP_SORTED_ARGS_ORDER_ASC },
    { ngx_string("desc"), NGX_HTTP_SORTED_ARGS_ORDER_DESC },
    { ngx_null_string, 0 }
};


static ngx_conf_enum_t  ngx_http_sorted_args_dedupe[] = {
    { ngx_string("off"), NGX_HTTP_SORTED_ARGS_DEDUPE_OFF },
    { ngx_string("first"), NGX_HTTP_SORTED_ARGS_DEDUPE_FIRST },
    { ngx_string("last"), NGX_HTTP_SORTED_ARGS_DEDUPE_LAST },
    { ngx_null_string, 0 }
};


static ngx_command_t  ngx_http_sorted_args_commands[] = {

    { ngx_string("sorted_args_filter"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
#if (NGX_CONDITION)
                        |NGX_HTTP_MAIN_WHEN_CONF|NGX_HTTP_SRV_WHEN_CONF
                        |NGX_HTTP_LOC_WHEN_CONF
#endif
                        |NGX_CONF_1MORE,
      ngx_http_sorted_args_filter,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, filter),
      NULL },

    { ngx_string("sorted_args_clear_valueless_args"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
#if (NGX_CONDITION)
                        |NGX_HTTP_MAIN_WHEN_CONF|NGX_HTTP_SRV_WHEN_CONF
                        |NGX_HTTP_LOC_WHEN_CONF
#endif
                        |NGX_CONF_FLAG,
#if (NGX_CONDITION)
      ngx_conf_set_conditional_flag_slot,
#else
      ngx_conf_set_flag_slot,
#endif
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, clear_valueless_args),
      NULL },

    { ngx_string("sorted_args_clear_invalid_args"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
#if (NGX_CONDITION)
                        |NGX_HTTP_MAIN_WHEN_CONF|NGX_HTTP_SRV_WHEN_CONF
                        |NGX_HTTP_LOC_WHEN_CONF
#endif
                        |NGX_CONF_FLAG,
#if (NGX_CONDITION)
      ngx_conf_set_conditional_flag_slot,
#else
      ngx_conf_set_flag_slot,
#endif
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, clear_invalid_args),
      NULL },

    { ngx_string("sorted_args_order"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
#if (NGX_CONDITION)
                        |NGX_HTTP_MAIN_WHEN_CONF|NGX_HTTP_SRV_WHEN_CONF
                        |NGX_HTTP_LOC_WHEN_CONF
#endif
                        |NGX_CONF_TAKE1,
#if (NGX_CONDITION)
      ngx_conf_set_conditional_enum_slot,
#else
      ngx_conf_set_enum_slot,
#endif
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, order),
      &ngx_http_sorted_args_order },

    { ngx_string("sorted_args_dedupe"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
#if (NGX_CONDITION)
                        |NGX_HTTP_MAIN_WHEN_CONF|NGX_HTTP_SRV_WHEN_CONF
                        |NGX_HTTP_LOC_WHEN_CONF
#endif
                        |NGX_CONF_TAKE1,
#if (NGX_CONDITION)
      ngx_conf_set_conditional_enum_slot,
#else
      ngx_conf_set_enum_slot,
#endif
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, dedupe),
      &ngx_http_sorted_args_dedupe },

    { ngx_string("sorted_args_overwrite"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
#if (NGX_CONDITION)
                        |NGX_HTTP_MAIN_WHEN_CONF|NGX_HTTP_SRV_WHEN_CONF
                        |NGX_HTTP_LOC_WHEN_CONF
#endif
                        |NGX_CONF_FLAG,
#if (NGX_CONDITION)
      ngx_conf_set_conditional_flag_slot,
#else
      ngx_conf_set_flag_slot,
#endif
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, overwrite),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_sorted_args_module_ctx = {
    ngx_http_sorted_args_add_variables,             /* preconfiguration */
    ngx_http_sorted_args_init,                      /* postconfiguration */

    NULL,                                           /* create main conf */
    NULL,                                           /* init main conf */

    NULL,                                           /* create server conf */
    NULL,                                           /* merge server conf */

    ngx_http_sorted_args_create_loc_conf,           /* create location conf */
    ngx_http_sorted_args_merge_loc_conf             /* merge location conf */
};


ngx_module_t  ngx_http_sorted_args_module = {
    NGX_MODULE_V1,
    &ngx_http_sorted_args_module_ctx,               /* module context */
    ngx_http_sorted_args_commands,                  /* module directives */
    NGX_HTTP_MODULE,                                /* module type */
    NULL,                                           /* init master */
    NULL,                                           /* init module */
    NULL,                                           /* init process */
    NULL,                                           /* init thread */
    NULL,                                           /* exit thread */
    NULL,                                           /* exit process */
    NULL,                                           /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_variable_t  ngx_http_sorted_args_vars[] = {

    { ngx_string("sorted_args"), NULL,
      ngx_http_sorted_args_variable,
      NGX_HTTP_SORTED_ARGS_VARIABLE_ARGS,
      NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("sorted_is_args"), NULL,
      ngx_http_sorted_args_variable,
      NGX_HTTP_SORTED_ARGS_VARIABLE_IS_ARGS,
      NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("sorted_has_args"), NULL,
      ngx_http_sorted_args_variable,
      NGX_HTTP_SORTED_ARGS_VARIABLE_HAS_ARGS,
      NGX_HTTP_VAR_NOCACHEABLE, 0 },

      ngx_http_null_variable
};


static ngx_int_t
ngx_http_sorted_args_add_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t  *var, *v;

    for (v = ngx_http_sorted_args_vars; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}


static void *
ngx_http_sorted_args_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_sorted_args_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_sorted_args_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->filter = NGX_CONF_UNSET_PTR;
#if (NGX_CONDITION)
    conf->order = NGX_CONF_UNSET_PTR;
    conf->dedupe = NGX_CONF_UNSET_PTR;
    conf->clear_valueless_args = NGX_CONF_UNSET_PTR;
    conf->clear_invalid_args = NGX_CONF_UNSET_PTR;
    conf->overwrite = NGX_CONF_UNSET_PTR;
#else
    conf->order = NGX_CONF_UNSET_UINT;
    conf->dedupe = NGX_CONF_UNSET_UINT;
    conf->clear_valueless_args = NGX_CONF_UNSET;
    conf->clear_invalid_args = NGX_CONF_UNSET;
    conf->overwrite = NGX_CONF_UNSET;
#endif

    return conf;
}


static char *
ngx_http_sorted_args_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_sorted_args_loc_conf_t  *prev = parent;
    ngx_http_sorted_args_loc_conf_t  *conf = child;

#if (NGX_CONDITION)
    if (ngx_conf_merge_conditional_ptr_value(cf, &conf->filter,
                                             prev->filter, NULL)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (ngx_conf_merge_conditional_enum_value(cf, &conf->order, prev->order,
                                              NGX_HTTP_SORTED_ARGS_ORDER_ASC)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (ngx_conf_merge_conditional_enum_value(cf, &conf->dedupe, prev->dedupe,
                                              NGX_HTTP_SORTED_ARGS_DEDUPE_OFF)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (ngx_conf_merge_conditional_flag_value(cf,
                                              &conf->clear_valueless_args,
                                              prev->clear_valueless_args, 0)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (ngx_conf_merge_conditional_flag_value(cf, &conf->clear_invalid_args,
                                              prev->clear_invalid_args, 0)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (ngx_conf_merge_conditional_flag_value(cf, &conf->overwrite,
                                              prev->overwrite, 0)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }
#else
    ngx_conf_merge_ptr_value(conf->filter, prev->filter, NULL);
    ngx_conf_merge_uint_value(conf->order, prev->order,
                              NGX_HTTP_SORTED_ARGS_ORDER_ASC);
    ngx_conf_merge_uint_value(conf->dedupe, prev->dedupe,
                              NGX_HTTP_SORTED_ARGS_DEDUPE_OFF);
    ngx_conf_merge_value(conf->clear_valueless_args,
                         prev->clear_valueless_args, 0);
    ngx_conf_merge_value(conf->clear_invalid_args,
                         prev->clear_invalid_args, 0);
    ngx_conf_merge_value(conf->overwrite, prev->overwrite, 0);
#endif

    return NGX_CONF_OK;
}


static char *
ngx_http_sorted_args_filter(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_sorted_args_loc_conf_t  *slcf = conf;

    ngx_http_sorted_args_filter_conf_t  *filter_conf;
    ngx_http_sorted_args_filter_t        parsed, *filter;
    ngx_str_t                           *value;
    ngx_uint_t                           i, j, n, wildcards;
    ngx_flag_t                           exists;
#if (NGX_CONDITION)
    ngx_condition_expr_id_t              expr_id;
    ngx_conf_condition_ptr_ctx_t        *condition_ctx;
#endif

    value = cf->args->elts;

#if (NGX_CONDITION)
    expr_id = ngx_condition_get_associated_expr_id(cf);

    if (ngx_condition_find_expr_ctx(slcf->filter, expr_id,
                                    sizeof(ngx_conf_condition_ptr_ctx_t),
                                    offsetof(ngx_conf_condition_ptr_ctx_t,
                                             expr_id))
        != NULL)
    {
        return "is duplicate";
    }
#else
    if (slcf->filter != NGX_CONF_UNSET_PTR) {
        return "is duplicate";
    }
#endif

    filter_conf = ngx_pcalloc(cf->pool,
                              sizeof(ngx_http_sorted_args_filter_conf_t));
    if (filter_conf == NULL) {
        return NGX_CONF_ERROR;
    }

    if (ngx_strcmp(value[1].data, "off") == 0) {
        if (cf->args->nelts != 2) {
            return "\"off\" must be specified without argument names";
        }

        filter_conf->mode = NGX_HTTP_SORTED_ARGS_MODE_OFF;
        goto save;
    }

    if (ngx_strcmp(value[1].data, "keep") == 0) {
        filter_conf->mode = NGX_HTTP_SORTED_ARGS_MODE_KEEP;

    } else if (ngx_strcmp(value[1].data, "remove") == 0) {
        filter_conf->mode = NGX_HTTP_SORTED_ARGS_MODE_REMOVE;

    } else {
        return "first parameter must be \"keep\", \"remove\", or \"off\"";
    }

    n = 2;

    if (cf->args->nelts > n && value[n].len == 2
        && value[n].data[0] == '-'
        && value[n].data[1] == 'i')
    {
        filter_conf->case_insensitive = 1;
        n++;
    }

    if (cf->args->nelts < n + 1) {
        return "requires at least one argument name";
    }

    if (cf->args->nelts == n + 1) {

        if (value[n].len == 1 && value[n].data[0] == '*') {
            filter_conf->mode = filter_conf->mode
                                == NGX_HTTP_SORTED_ARGS_MODE_REMOVE
                                ? NGX_HTTP_SORTED_ARGS_MODE_CLEAR
                                : NGX_HTTP_SORTED_ARGS_MODE_OFF;
            goto save;
        }
    }

    filter_conf->args = ngx_array_create(
        cf->pool, cf->args->nelts - n,
        sizeof(ngx_http_sorted_args_filter_t));
    if (filter_conf->args == NULL) {
        return NGX_CONF_ERROR;
    }

    for (i = n; i < cf->args->nelts; i++) {
        if (value[i].len == 0) {
            return "empty argument name is not allowed";
        }

        wildcards = 0;

        for (j = 0; j < value[i].len; j++) {
            if (value[i].data[j] == '*') {
                wildcards++;
            }
        }

        parsed.name = value[i];
        parsed.wildcard = NGX_HTTP_SORTED_ARGS_FILTER_EXACT;

        if (wildcards == 1) {

            if (value[i].len == 1) {
                return "single wildcard is only supported as the only "
                       "argument name";

            } else if (value[i].data[0] == '*') {
                parsed.name.data = value[i].data + 1;
                parsed.name.len = value[i].len - 1;
                parsed.wildcard = NGX_HTTP_SORTED_ARGS_FILTER_SUFFIX;

            } else if (value[i].data[value[i].len - 1] == '*') {
                parsed.name.len = value[i].len - 1;
                parsed.wildcard = NGX_HTTP_SORTED_ARGS_FILTER_PREFIX;

            } else {
                return "wildcard is only supported at the beginning or end "
                       "of an argument name";
            }

        } else if (wildcards > 1) {
            return "wildcard is only supported at the beginning or end "
                   "of an argument name";
        }

        exists = 0;
        filter = filter_conf->args->elts;

        for (j = 0; j < filter_conf->args->nelts; j++) {

            if (parsed.wildcard != filter[j].wildcard) {
                continue;
            }

            if (ngx_http_sorted_args_str_eq(&parsed.name, &filter[j].name,
                                            filter_conf->case_insensitive)
                == NGX_OK)
            {
                exists = 1;
                break;
            }
        }

        if (!exists) {
            filter = ngx_array_push(filter_conf->args);
            if (filter == NULL) {
                return NGX_CONF_ERROR;
            }

            *filter = parsed;
        }
    }

save:

#if (NGX_CONDITION)
    if (slcf->filter == NULL || slcf->filter == NGX_CONF_UNSET_PTR) {
        slcf->filter = ngx_array_create(cf->pool, 2,
                                        sizeof(ngx_conf_condition_ptr_ctx_t));
        if (slcf->filter == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    condition_ctx = ngx_array_push(slcf->filter);
    if (condition_ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    condition_ctx->value = filter_conf;
    condition_ctx->expr_id = expr_id;
#else
    slcf->filter = filter_conf;
#endif

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_sorted_args_str_eq(ngx_str_t *one, ngx_str_t *two,
    ngx_flag_t case_insensitive)
{
    if (one->len != two->len) {
        return NGX_DECLINED;
    }

    if (one->len == 0) {
        return NGX_OK;
    }

    if (case_insensitive) {
        return ngx_strncasecmp(one->data, two->data, one->len) == 0
               ? NGX_OK : NGX_DECLINED;
    }

    return ngx_strncmp(one->data, two->data, one->len) == 0
           ? NGX_OK : NGX_DECLINED;
}


static ngx_int_t
ngx_http_sorted_args_str_cmp(ngx_str_t *one, ngx_str_t *two)
{
    ngx_int_t  rc;

    rc = ngx_strncmp(one->data, two->data, ngx_min(one->len, two->len));
    if (rc != 0) {
        return rc;
    }

    if (one->len < two->len) {
        return -1;
    }

    if (one->len > two->len) {
        return 1;
    }

    return 0;
}


static ngx_int_t
ngx_http_sorted_args_match_filter(
    ngx_http_sorted_args_filter_conf_t *filter_conf,
    ngx_http_sorted_args_filter_t *filter, ngx_str_t *key)
{
    ngx_str_t  part;

    if (key->len < filter->name.len) {
        return NGX_DECLINED;
    }

    switch (filter->wildcard) {

    case NGX_HTTP_SORTED_ARGS_FILTER_PREFIX:
        part.data = key->data;
        part.len = filter->name.len;

        return ngx_http_sorted_args_str_eq(&part, &filter->name,
                                           filter_conf->case_insensitive);

    case NGX_HTTP_SORTED_ARGS_FILTER_SUFFIX:
        part.data = key->data + key->len - filter->name.len;
        part.len = filter->name.len;

        return ngx_http_sorted_args_str_eq(&part, &filter->name,
                                           filter_conf->case_insensitive);

    default:
        return ngx_http_sorted_args_str_eq(key, &filter->name,
                                           filter_conf->case_insensitive);
    }
}


static ngx_int_t
ngx_http_sorted_args_should_output_arg(ngx_http_sorted_args_runtime_conf_t *rcf,
    ngx_http_sorted_args_arg_t *arg)
{
    ngx_http_sorted_args_filter_conf_t  *filter_conf;
    ngx_http_sorted_args_filter_t       *filter;
    ngx_int_t                            matched;
    ngx_uint_t                           i;

    if (rcf->clear_invalid_args && arg->key.len == 0) {
        return NGX_DECLINED;
    }

    if (rcf->clear_valueless_args
        && (arg->key.len == arg->complete.len
            || (arg->key.len + 1 == arg->complete.len
                && arg->complete.data[arg->key.len] == '=')))
    {
        return NGX_DECLINED;
    }

    matched = NGX_DECLINED;
    filter_conf = rcf->filter;

    if (filter_conf == NULL
        || filter_conf->mode == NGX_HTTP_SORTED_ARGS_MODE_OFF)
    {
        return NGX_OK;
    }

    filter = filter_conf->args->elts;

    for (i = 0; i < filter_conf->args->nelts; i++) {
        if (ngx_http_sorted_args_match_filter(filter_conf, &filter[i],
                                              &arg->key)
            == NGX_OK)
        {
            matched = NGX_OK;
            break;
        }
    }

    if (filter_conf->mode == NGX_HTTP_SORTED_ARGS_MODE_KEEP) {
        return matched;
    }

    return matched == NGX_OK ? NGX_DECLINED : NGX_OK;
}


static ngx_int_t
ngx_http_sorted_args_same_key(ngx_http_sorted_args_arg_t *one,
    ngx_http_sorted_args_arg_t *two)
{
    if (one->key.len != two->key.len) {
        return NGX_DECLINED;
    }

    if (one->key.len == 0) {
        return NGX_OK;
    }

    return ngx_strncmp(one->key.data, two->key.data, one->key.len) == 0
           ? NGX_OK : NGX_DECLINED;
}


static ngx_int_t
ngx_http_sorted_args_dedupe_arg(ngx_http_sorted_args_runtime_conf_t *rcf,
    ngx_http_sorted_args_ctx_t *ctx, ngx_http_sorted_args_arg_t *arg)
{
    ngx_http_sorted_args_arg_t  *other;
    ngx_queue_t                 *q;

    if (rcf->dedupe == NGX_HTTP_SORTED_ARGS_DEDUPE_OFF) {
        return NGX_OK;
    }

    for (q = ngx_queue_head(&ctx->args_queue);
         q != ngx_queue_sentinel(&ctx->args_queue);
         q = ngx_queue_next(q))
    {
        other = ngx_queue_data(q, ngx_http_sorted_args_arg_t, queue);

        if (other == arg
            || ngx_http_sorted_args_same_key(arg, other) != NGX_OK
            || ngx_http_sorted_args_should_output_arg(rcf, other)
               != NGX_OK)
        {
            continue;
        }

        if (rcf->dedupe == NGX_HTTP_SORTED_ARGS_DEDUPE_FIRST
            && other->index < arg->index)
        {
            return NGX_DECLINED;
        }

        if (rcf->dedupe == NGX_HTTP_SORTED_ARGS_DEDUPE_LAST
            && other->index > arg->index)
        {
            return NGX_DECLINED;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_sorted_args_cmp_args(const ngx_queue_t *one,
    const ngx_queue_t *two)
{
    ngx_http_sorted_args_arg_t   *first, *second;
    ngx_int_t                     rc;

    first  = ngx_queue_data(one, ngx_http_sorted_args_arg_t, queue);
    second = ngx_queue_data(two, ngx_http_sorted_args_arg_t, queue);

    rc = ngx_http_sorted_args_str_cmp(&first->key, &second->key);
    if (rc == 0) {
        rc = ngx_http_sorted_args_str_cmp(&first->complete, &second->complete);
    }

    return rc;
}


static ngx_int_t
ngx_http_sorted_args_process(ngx_http_request_t *r, ngx_str_t *result)
{
    ngx_http_sorted_args_loc_conf_t     *slcf;
    ngx_http_sorted_args_runtime_conf_t  rcf;
    ngx_http_sorted_args_ctx_t          *ctx;
    ngx_http_sorted_args_arg_t          *arg;
    u_char                              *ampersand, *equal, *last;
    ngx_queue_t                         *next, *q;
    ngx_uint_t                           index;
    u_char                              *p, *args;
    size_t                               args_len;

    slcf = ngx_http_get_module_loc_conf(r, ngx_http_sorted_args_module);

#if (NGX_CONDITION)
    rcf.filter = ngx_http_get_conditional_ptr_value(r, slcf->filter);
    rcf.order = ngx_http_get_conditional_enum_value(r, slcf->order);
    rcf.dedupe = ngx_http_get_conditional_enum_value(r, slcf->dedupe);
    rcf.clear_valueless_args =
        ngx_http_get_conditional_flag_value(r, slcf->clear_valueless_args);
    rcf.clear_invalid_args =
        ngx_http_get_conditional_flag_value(r, slcf->clear_invalid_args);
#else
    rcf.filter = slcf->filter;
    rcf.order = slcf->order;
    rcf.dedupe = slcf->dedupe;
    rcf.clear_valueless_args = slcf->clear_valueless_args;
    rcf.clear_invalid_args = slcf->clear_invalid_args;
#endif

    if (rcf.filter != NULL
        && rcf.filter->mode == NGX_HTTP_SORTED_ARGS_MODE_CLEAR)
    {
        result->len = 0;
        result->data = (u_char *) "";
        return NGX_OK;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_sorted_args_module);

    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_sorted_args_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ngx_http_set_ctx(r, ctx, ngx_http_sorted_args_module);

        ngx_queue_init(&ctx->args_queue);

        p = r->args.data;
        last = p + r->args.len;
        index = 0;

        for ( /* void */ ; p < last; p++) {
            arg = ngx_pcalloc(r->pool, sizeof(ngx_http_sorted_args_arg_t));
            if (arg == NULL) {
                return NGX_ERROR;
            }

            ampersand = ngx_strlchr(p, last, '&');
            if (ampersand == NULL) {
                ampersand = last;
            }

            equal = ngx_strlchr(p, last, '=');
            if (equal == NULL || equal > ampersand) {
                equal = ampersand;
            }

            arg->key.data = p;
            arg->key.len = equal - p;

            arg->complete.data = p;
            arg->complete.len = ampersand - p;
            arg->index = index++;

            ngx_queue_insert_tail(&ctx->args_queue, &arg->queue);

            p = ampersand;
        }

        ngx_queue_sort(&ctx->args_queue, ngx_http_sorted_args_cmp_args);
    }

    args = ngx_pcalloc(r->pool, r->args.len + 2);
    if (args == NULL) {
        return NGX_ERROR;
    }

    p = args;

    if (rcf.order == NGX_HTTP_SORTED_ARGS_ORDER_DESC) {
        q = ngx_queue_last(&ctx->args_queue);

    } else {
        q = ngx_queue_head(&ctx->args_queue);
    }

    for (/* void */; q != ngx_queue_sentinel(&ctx->args_queue); q = next) {

        if (rcf.order == NGX_HTTP_SORTED_ARGS_ORDER_DESC) {
            next = ngx_queue_prev(q);

        } else {
            next = ngx_queue_next(q);
        }

        arg = ngx_queue_data(q, ngx_http_sorted_args_arg_t, queue);

        if (ngx_http_sorted_args_should_output_arg(&rcf, arg)
            != NGX_OK)
        {
            continue;
        }

        if (ngx_http_sorted_args_dedupe_arg(&rcf, ctx, arg)
            != NGX_OK)
        {
            continue;
        }

        p = ngx_sprintf(p, "%V&", &arg->complete);
    }

    args_len = (p > args) ? p - args - 1 : 0;

    result->data = args;
    result->len = args_len;

    return NGX_OK;
}


static ngx_int_t
ngx_http_sorted_args_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_str_t    result;
    ngx_int_t    rc;

    if (r->args.len == 0) {

        if (data == NGX_HTTP_SORTED_ARGS_VARIABLE_HAS_ARGS) {
            v->len = 1;
            v->valid = 1;
            v->no_cacheable = 0;
            v->not_found = 0;
            v->data = (u_char *) "?";
            return NGX_OK;
        }

        *v = ngx_http_variable_null_value;
        return NGX_OK;
    }

    rc = ngx_http_sorted_args_process(r, &result);
    if (rc != NGX_OK) {
        return rc;
    }

    if (data == NGX_HTTP_SORTED_ARGS_VARIABLE_IS_ARGS) {

        if (result.len == 0) {
            *v = ngx_http_variable_null_value;
            return NGX_OK;
        }

        v->len = 1;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = (u_char *) "?";

        return NGX_OK;
    }

    if (data == NGX_HTTP_SORTED_ARGS_VARIABLE_HAS_ARGS) {

        v->len = 1;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;

        if (result.len == 0) {
            v->data = (u_char *) "?";

        } else {
            v->data = (u_char *) "&";
        }

        return NGX_OK;
    }

    v->data = result.data;
    v->len = result.len;

    return NGX_OK;
}


static ngx_int_t
ngx_http_sorted_args_handler(ngx_http_request_t *r)
{
    ngx_http_sorted_args_loc_conf_t      *slcf;
    ngx_str_t                             result;
    ngx_int_t                             rc;

    slcf = ngx_http_get_module_loc_conf(r, ngx_http_sorted_args_module);

#if (NGX_CONDITION)
    if (!ngx_http_get_conditional_flag_value(r, slcf->overwrite)
        || r->args.len == 0)
    {
        return NGX_DECLINED;
    }
#else
    if (!slcf->overwrite || r->args.len == 0) {
        return NGX_DECLINED;
    }
#endif

    rc = ngx_http_sorted_args_process(r, &result);
    if (rc != NGX_OK) {
        return rc;
    }

    r->args.data = result.data;
    r->args.len = result.len;

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_sorted_args_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_sorted_args_handler;

    return NGX_OK;
}
