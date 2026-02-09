
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

#define NGX_HTTP_SORTED_ARGS_MODE_UNSET           0
#define NGX_HTTP_SORTED_ARGS_MODE_KEEP            1
#define NGX_HTTP_SORTED_ARGS_MODE_CLEAR           2


static ngx_int_t ngx_http_sorted_args_add_variables(ngx_conf_t *cf);
static ngx_int_t ngx_http_sorted_args_init(ngx_conf_t *cf);

static void *ngx_http_sorted_args_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_sorted_args_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
static char *ngx_http_sorted_args_filter(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);

static ngx_int_t ngx_http_sorted_args_cmp_parameters(const ngx_queue_t *one,
    const ngx_queue_t *two);
static ngx_int_t ngx_http_sorted_args_process(ngx_http_request_t *r,
    ngx_str_t *result);

static ngx_int_t ngx_http_sorted_args_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_sorted_args_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);


typedef struct {
    ngx_array_t              *parameters_to_filter;
    ngx_uint_t                mode;
    ngx_flag_t                case_insensitive;
    ngx_flag_t                clear_empty_args;
    ngx_flag_t                overwrite;
} ngx_http_sorted_args_loc_conf_t;


typedef struct {
    ngx_queue_t               args_queue;
} ngx_http_sorted_args_ctx_t;


typedef struct {
    ngx_queue_t               queue;
    ngx_str_t                 key;
    ngx_str_t                 complete;
} ngx_http_sorted_args_parameter_t;


static ngx_command_t  ngx_http_sorted_args_commands[] = {

    { ngx_string("sorted_args_filter"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_2MORE,
      ngx_http_sorted_args_filter,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, parameters_to_filter),
      NULL },

    { ngx_string("sorted_args_clear_empty_args"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_sorted_args_loc_conf_t, clear_empty_args),
      NULL },

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

    conf->parameters_to_filter = NGX_CONF_UNSET_PTR;
    conf->mode = NGX_HTTP_SORTED_ARGS_MODE_UNSET;
    conf->case_insensitive = NGX_CONF_UNSET;
    conf->clear_empty_args = NGX_CONF_UNSET;
    conf->overwrite = NGX_CONF_UNSET;

    return conf;
}


static char *
ngx_http_sorted_args_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_sorted_args_loc_conf_t  *prev = parent;
    ngx_http_sorted_args_loc_conf_t  *conf = child;

    if (conf->mode == NGX_HTTP_SORTED_ARGS_MODE_UNSET) {
        conf->parameters_to_filter = prev->parameters_to_filter;
        conf->mode = prev->mode;
        conf->case_insensitive = prev->case_insensitive;
    }

    ngx_conf_merge_value(conf->clear_empty_args, prev->clear_empty_args, 0);
    ngx_conf_merge_value(conf->overwrite, prev->overwrite, 0);

    return NGX_CONF_OK;
}


static char *
ngx_http_sorted_args_filter(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_sorted_args_loc_conf_t  *salc = conf;

    ngx_str_t        *value, *s;
    ngx_uint_t        i, j, n;
    ngx_flag_t        exists;

    if (salc->mode != NGX_HTTP_SORTED_ARGS_MODE_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    n = 1;

    if (ngx_strcmp(value[1].data, "keep") == 0) {
        salc->mode = NGX_HTTP_SORTED_ARGS_MODE_KEEP;
        n = 2;

    } else if (ngx_strcmp(value[1].data, "clear") == 0) {
        salc->mode = NGX_HTTP_SORTED_ARGS_MODE_CLEAR;
        n = 2;

    } else {
        return "first argument must be \"keep\" or \"clear\"";
    }

    if (cf->args->nelts < n + 1) {
        return "requires at least one parameter to filter";
    }

    salc->case_insensitive = 0;

    if (value[n].len == 2
        && value[n].data[0] == '-'
        && value[n].data[1] == 'i')
    {
        salc->case_insensitive = 1;
        n++;
    }

    if (cf->args->nelts < n + 1) {
        return "requires at least one parameter to filter";
    }

    salc->parameters_to_filter = ngx_array_create(cf->pool,
        cf->args->nelts, sizeof(ngx_str_t));
    if (salc->parameters_to_filter == NULL) {
        return NGX_CONF_ERROR;
    }

    for (i = n; i < cf->args->nelts; i++) {

        exists = 0;
        s = salc->parameters_to_filter->elts;

        for (j = 0; j < salc->parameters_to_filter->nelts; j++) {

            if (value[i].len == s[j].len) {

                if (salc->case_insensitive) {

                    if (ngx_strncasecmp(value[i].data, s[j].data, value[i].len)
                        == 0)
                    {
                        exists = 1;
                        break;
                    }

                } else {

                    if (ngx_strncmp(value[i].data, s[j].data, value[i].len)
                        == 0)
                    {
                        exists = 1;
                        break;
                    }
                }
            }
        }

        if (!exists) {
            s = ngx_array_push(salc->parameters_to_filter);
            if (s == NULL) {
                return NGX_CONF_ERROR;
            }

            *s = value[i];
        }
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_sorted_args_cmp_parameters(const ngx_queue_t *one,
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
    ngx_flag_t                            filter;
    ngx_str_t                            *value;
    ngx_uint_t                            i;
    u_char                               *p, *args;
    size_t                                args_len;

    salc = ngx_http_get_module_loc_conf(r, ngx_http_sorted_args_module);
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

            ngx_queue_insert_tail(&ctx->args_queue, &param->queue);

            p = ampersand;
        }

        ngx_queue_sort(&ctx->args_queue, ngx_http_sorted_args_cmp_parameters);
    }

    args = ngx_pcalloc(r->pool, r->args.len + 2);
    if (args == NULL) {
        return NGX_ERROR;
    }

    p = args;
    for (q = ngx_queue_head(&ctx->args_queue);
         q != ngx_queue_sentinel(&ctx->args_queue);
         q = ngx_queue_next(q))
    {
        param = ngx_queue_data(q, ngx_http_sorted_args_parameter_t, queue);

        if (salc->clear_empty_args
            && (param->key.len == param->complete.len
                || (param->key.len + 1 == param->complete.len
                    && param->complete.data[param->key.len] == '=')))
        {
            continue;
        }

        filter = 0;

        if (salc->mode == NGX_HTTP_SORTED_ARGS_MODE_UNSET
            || !salc->parameters_to_filter
            || param->key.len == 0)
        {
            goto check_filter;
        }

        value = salc->parameters_to_filter->elts;

        for (i = 0; i < salc->parameters_to_filter->nelts; i++) {

            if (param->key.len != value[i].len) {
                continue;
            }

            if (salc->case_insensitive) {

                if (ngx_strncasecmp(param->key.data, value[i].data,
                        param->key.len) == 0)
                {
                    filter = 1;
                    break;
                }

            } else {

                if (ngx_strncmp(param->key.data, value[i].data,
                        param->key.len) == 0)
                {
                    filter = 1;
                    break;
                }
            }
        }

check_filter:

        if (salc->mode == NGX_HTTP_SORTED_ARGS_MODE_KEEP) {

            if (filter) {
                p = ngx_sprintf(p, "%V&", &param->complete);
            }

        } else {

            if (!filter) {
                p = ngx_sprintf(p, "%V&", &param->complete);
            }
        }
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
