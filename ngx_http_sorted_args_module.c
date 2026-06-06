
/*
 * Copyright (c) Hanada
 * Copyright (c) 2014 Wandenberg Peixoto
 */


#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


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
    ngx_array_t              *args_to_filter;
    ngx_uint_t                mode;
    ngx_uint_t                order;
    ngx_uint_t                dedupe;
    ngx_flag_t                case_insensitive;
    ngx_flag_t                clear_valueless_args;
    ngx_flag_t                clear_invalid_args;
    ngx_flag_t                overwrite;
} ngx_http_sorted_args_loc_conf_t;


typedef struct {
    ngx_str_t                 name;
    ngx_uint_t                wildcard;
} ngx_http_sorted_args_filter_t;


typedef struct {
    ngx_queue_t               args_queue;
} ngx_http_sorted_args_ctx_t;


typedef struct {
    ngx_queue_t               queue;
    ngx_str_t                 key;
    ngx_str_t                 complete;
    ngx_uint_t                index;
} ngx_http_sorted_args_parameter_t;


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
static ngx_int_t ngx_http_sorted_args_match_filter(
    ngx_http_sorted_args_loc_conf_t *salc, ngx_http_sorted_args_filter_t *filter,
    ngx_str_t *key);
static ngx_int_t ngx_http_sorted_args_should_output_parameter(
    ngx_http_sorted_args_loc_conf_t *salc, ngx_http_sorted_args_parameter_t *param);
static ngx_int_t ngx_http_sorted_args_same_key(
    ngx_http_sorted_args_parameter_t *one, ngx_http_sorted_args_parameter_t *two);
static ngx_int_t ngx_http_sorted_args_dedupe_parameter(
    ngx_http_sorted_args_loc_conf_t *salc, ngx_http_sorted_args_ctx_t *ctx,
    ngx_http_sorted_args_parameter_t *param);

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

    { ngx_string("sorted_args_remove_args"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_1MORE,
      ngx_http_sorted_args_filter,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, args_to_filter),
      NULL },

    { ngx_string("sorted_args_keep_args"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_1MORE,
      ngx_http_sorted_args_filter,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, args_to_filter),
      NULL },

    { ngx_string("sorted_args_clear_valueless_args"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, clear_valueless_args),
      NULL },

    { ngx_string("sorted_args_clear_invalid_args"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, clear_invalid_args),
      NULL },

    { ngx_string("sorted_args_order"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, order),
      &ngx_http_sorted_args_order },

    { ngx_string("sorted_args_dedupe"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, dedupe),
      &ngx_http_sorted_args_dedupe },

    { ngx_string("sorted_args_overwrite"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, overwrite),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_sorted_args_module_ctx = {
    ngx_http_sorted_args_add_variables,             /* preconfiguration */
    ngx_http_sorted_args_init,                      /* postconfiguration */

    NULL,                                           /* create main configuration */
    NULL,                                           /* init main configuration */

    NULL,                                           /* create server configuration */
    NULL,                                           /* merge server configuration */

    ngx_http_sorted_args_create_loc_conf,           /* create location configuration */
    ngx_http_sorted_args_merge_loc_conf             /* merge location configuration */
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

    conf->args_to_filter = NGX_CONF_UNSET_PTR;
    conf->mode = NGX_CONF_UNSET_UINT;
    conf->order = NGX_CONF_UNSET_UINT;
    conf->dedupe = NGX_CONF_UNSET_UINT;
    conf->case_insensitive = NGX_CONF_UNSET;
    conf->clear_valueless_args = NGX_CONF_UNSET;
    conf->clear_invalid_args = NGX_CONF_UNSET;
    conf->overwrite = NGX_CONF_UNSET;

    return conf;
}


static char *
ngx_http_sorted_args_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_sorted_args_loc_conf_t  *prev = parent;
    ngx_http_sorted_args_loc_conf_t  *conf = child;

    if (conf->mode == NGX_CONF_UNSET_UINT) {
        conf->args_to_filter = prev->args_to_filter;
        conf->mode = prev->mode;
        conf->case_insensitive = prev->case_insensitive;
    }

    ngx_conf_merge_uint_value(conf->order, prev->order,
                              NGX_HTTP_SORTED_ARGS_ORDER_ASC);
    ngx_conf_merge_uint_value(conf->dedupe, prev->dedupe,
                              NGX_HTTP_SORTED_ARGS_DEDUPE_OFF);
    ngx_conf_merge_value(conf->clear_valueless_args,
                         prev->clear_valueless_args, 0);
    ngx_conf_merge_value(conf->clear_invalid_args,
                         prev->clear_invalid_args, 0);
    ngx_conf_merge_value(conf->overwrite, prev->overwrite, 0);

    return NGX_CONF_OK;
}


static char *
ngx_http_sorted_args_filter(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_sorted_args_loc_conf_t  *salc = conf;

    ngx_http_sorted_args_filter_t     parsed, *filter;
    ngx_str_t                         *value;
    ngx_uint_t                         i, j, n, wildcards, mode;
    ngx_flag_t                         exists;

    mode = NGX_HTTP_SORTED_ARGS_MODE_REMOVE;

    if (cmd->name.len == sizeof("sorted_args_keep_args") - 1
        && ngx_strncmp(cmd->name.data, "sorted_args_keep_args",
                       sizeof("sorted_args_keep_args") - 1) == 0)
    {
        mode = NGX_HTTP_SORTED_ARGS_MODE_KEEP;
    }

    value = cf->args->elts;

    n = 1;

    if (salc->mode != NGX_CONF_UNSET_UINT) {
        if (salc->mode != mode) {
            return "conflicts with sorted_args_keep_args or "
                   "sorted_args_remove_args";
        }

        return "is duplicate";
    }

    salc->mode = mode;
    salc->case_insensitive = 0;

    if (value[n].len == 2
        && value[n].data[0] == '-'
        && value[n].data[1] == 'i')
    {
        salc->case_insensitive = 1;
        n++;
    }

    if (cf->args->nelts < n + 1) {
        return "requires at least one argument name";
    }

    if (cf->args->nelts == n + 1) {

        if (mode == NGX_HTTP_SORTED_ARGS_MODE_REMOVE && value[n].len == 1
            && value[n].data[0] == '*')
        {
            salc->mode = NGX_HTTP_SORTED_ARGS_MODE_CLEAR;
            salc->args_to_filter = NULL;
            return NGX_CONF_OK;
        }

        if (mode == NGX_HTTP_SORTED_ARGS_MODE_KEEP && value[n].len == 1
            && value[n].data[0] == '*')
        {
            salc->mode = NGX_HTTP_SORTED_ARGS_MODE_OFF;
            salc->args_to_filter = NULL;
            return NGX_CONF_OK;
        }
    }

    salc->args_to_filter = ngx_array_create(cf->pool,
                                        cf->args->nelts - n,
                                        sizeof(ngx_http_sorted_args_filter_t));
    if (salc->args_to_filter == NULL) {
        return NGX_CONF_ERROR;
    }

    for (i = n; i < cf->args->nelts; i++) {
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
        filter = salc->args_to_filter->elts;

        for (j = 0; j < salc->args_to_filter->nelts; j++) {

            if (parsed.wildcard != filter[j].wildcard) {
                continue;
            }

            if (ngx_http_sorted_args_str_eq(&parsed.name, &filter[j].name,
                                            salc->case_insensitive) == NGX_OK)
            {
                exists = 1;
                break;
            }
        }

        if (!exists) {
            filter = ngx_array_push(salc->args_to_filter);
            if (filter == NULL) {
                return NGX_CONF_ERROR;
            }

            *filter = parsed;
        }
    }

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
ngx_http_sorted_args_match_filter(ngx_http_sorted_args_loc_conf_t *salc,
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
                                           salc->case_insensitive);

    case NGX_HTTP_SORTED_ARGS_FILTER_SUFFIX:
        part.data = key->data + key->len - filter->name.len;
        part.len = filter->name.len;

        return ngx_http_sorted_args_str_eq(&part, &filter->name,
                                           salc->case_insensitive);

    default:
        return ngx_http_sorted_args_str_eq(key, &filter->name,
                                           salc->case_insensitive);
    }
}


static ngx_int_t
ngx_http_sorted_args_should_output_parameter(ngx_http_sorted_args_loc_conf_t *salc,
    ngx_http_sorted_args_parameter_t *param)
{
    ngx_http_sorted_args_filter_t  *filter;
    ngx_int_t                       matched;
    ngx_uint_t                      i;

    if (salc->clear_invalid_args && param->key.len == 0) {
        return NGX_DECLINED;
    }

    if (salc->clear_valueless_args
        && (param->key.len == param->complete.len
            || (param->key.len + 1 == param->complete.len
                && param->complete.data[param->key.len] == '=')))
    {
        return NGX_DECLINED;
    }

    matched = NGX_DECLINED;

    if (salc->mode == NGX_HTTP_SORTED_ARGS_MODE_OFF) {
        return NGX_OK;
    }

    if (salc->mode == NGX_CONF_UNSET_UINT
        || salc->args_to_filter == NGX_CONF_UNSET_PTR
        || salc->args_to_filter == NULL)
    {
        goto check_filter;
    }

    filter = salc->args_to_filter->elts;

    for (i = 0; i < salc->args_to_filter->nelts; i++) {
        if (ngx_http_sorted_args_match_filter(salc, &filter[i], &param->key)
            == NGX_OK)
        {
            matched = NGX_OK;
            break;
        }
    }

check_filter:

    if (salc->mode == NGX_HTTP_SORTED_ARGS_MODE_KEEP) {
        return matched;
    }

    return matched == NGX_OK ? NGX_DECLINED : NGX_OK;
}


static ngx_int_t
ngx_http_sorted_args_same_key(ngx_http_sorted_args_parameter_t *one,
    ngx_http_sorted_args_parameter_t *two)
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
ngx_http_sorted_args_dedupe_parameter(ngx_http_sorted_args_loc_conf_t *salc,
    ngx_http_sorted_args_ctx_t *ctx, ngx_http_sorted_args_parameter_t *param)
{
    ngx_http_sorted_args_parameter_t  *other;
    ngx_queue_t                      *q;

    if (salc->dedupe == NGX_HTTP_SORTED_ARGS_DEDUPE_OFF) {
        return NGX_OK;
    }

    for (q = ngx_queue_head(&ctx->args_queue);
         q != ngx_queue_sentinel(&ctx->args_queue);
         q = ngx_queue_next(q))
    {
        other = ngx_queue_data(q, ngx_http_sorted_args_parameter_t, queue);

        if (other == param
            || ngx_http_sorted_args_same_key(param, other) != NGX_OK
            || ngx_http_sorted_args_should_output_parameter(salc, other)
               != NGX_OK)
        {
            continue;
        }

        if (salc->dedupe == NGX_HTTP_SORTED_ARGS_DEDUPE_FIRST
            && other->index < param->index)
        {
            return NGX_DECLINED;
        }

        if (salc->dedupe == NGX_HTTP_SORTED_ARGS_DEDUPE_LAST
            && other->index > param->index)
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
    ngx_http_sorted_args_parameter_t   *first, *second;
    ngx_int_t                           rc;

    first  = ngx_queue_data(one, ngx_http_sorted_args_parameter_t, queue);
    second = ngx_queue_data(two, ngx_http_sorted_args_parameter_t, queue);

    rc = ngx_strncasecmp(first->key.data, second->key.data,
                            ngx_min(first->key.len, second->key.len));
    if (rc == 0) {
        rc = ngx_strncasecmp(first->complete.data, second->complete.data,
                                ngx_min(first->complete.len,
                                    second->complete.len));
        if (rc == 0) {
            rc = -1;
        }
    }

    return rc;
}


static ngx_int_t
ngx_http_sorted_args_process(ngx_http_request_t *r, ngx_str_t *result)
{
    ngx_http_sorted_args_loc_conf_t      *salc;
    ngx_http_sorted_args_ctx_t           *ctx;
    ngx_http_sorted_args_parameter_t     *param;
    u_char                               *ampersand, *equal, *last;
    ngx_queue_t                          *q;
    ngx_uint_t                            index;
    u_char                               *p, *args;
    size_t                                args_len;

    salc = ngx_http_get_module_loc_conf(r, ngx_http_sorted_args_module);

    if (salc->mode == NGX_HTTP_SORTED_ARGS_MODE_CLEAR) {
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
            param = ngx_pcalloc(r->pool,
                sizeof(ngx_http_sorted_args_parameter_t));
            if (param == NULL) {
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

            param->key.data = p;
            param->key.len = equal - p;

            param->complete.data = p;
            param->complete.len = ampersand - p;
            param->index = index++;

            ngx_queue_insert_tail(&ctx->args_queue, &param->queue);

            p = ampersand;
        }

        ngx_queue_sort(&ctx->args_queue, ngx_http_sorted_args_cmp_args);
    }

    args = ngx_pcalloc(r->pool, r->args.len + 2);
    if (args == NULL) {
        return NGX_ERROR;
    }

    p = args;
    for (q = (salc->order == NGX_HTTP_SORTED_ARGS_ORDER_DESC)
             ? ngx_queue_last(&ctx->args_queue)
             : ngx_queue_head(&ctx->args_queue);
         q != ngx_queue_sentinel(&ctx->args_queue);
         q = (salc->order == NGX_HTTP_SORTED_ARGS_ORDER_DESC)
             ? ngx_queue_prev(q)
             : ngx_queue_next(q))
    {
        param = ngx_queue_data(q, ngx_http_sorted_args_parameter_t, queue);

        if (ngx_http_sorted_args_should_output_parameter(salc, param)
            != NGX_OK)
        {
            continue;
        }

        if (ngx_http_sorted_args_dedupe_parameter(salc, ctx, param)
            != NGX_OK)
        {
            continue;
        }

        p = ngx_sprintf(p, "%V&", &param->complete);
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
    ngx_http_sorted_args_loc_conf_t      *salc;
    ngx_str_t                             result;
    ngx_int_t                             rc;

    salc = ngx_http_get_module_loc_conf(r, ngx_http_sorted_args_module);

    if (!salc->overwrite || r->args.len == 0) {
        return NGX_DECLINED;
    }

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
