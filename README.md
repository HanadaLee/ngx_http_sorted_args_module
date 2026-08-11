Nginx Sorted Args Module
===============================

This Nginx module orders the args parameters of an HTTP request alphanumerically and makes the sorted key-value pairs accessible using an Nginx variable.

Requests like `/index.html?b=2&a=1&c=3`, `/index.html?b=2&c=3&a=1`, `/index.html?c=3&a=1&b=2`, `/index.html?c=3&b=2&a=1` will produce the same normalized args `a=1&b=2&c=3` which can be accessed within Nginx using the `$sorted_args` variable.

Sorting is bytewise and case-sensitive; the `-i` option only affects parameter filtering directives.

This is especially useful if you want to normalize the args to be used in a cache key, for example when used with the `proxy_cache_key` directive.

It is also possible to remove or keep selected query parameters with the `sorted_args_filter` directive.

_This module is not distributed with the Nginx source. See [the installation instructions](#installation)._


Configuration
-------------

An example:

```
pid         logs/nginx.pid;
error_log   logs/nginx-main_error.log debug;

worker_processes    2;

events {
  worker_connections  1024;
  #use                 kqueue; # MacOS
  use                 epoll; # Linux
}

http {
  default_type    text/plain;

  types {
    text/html   html;
  }

  log_format main  '[$time_local] $host "$request" $request_time '
                 '$status $body_bytes_sent "$http_referer" "$http_user_agent" '
                 'cache_status: "$upstream_cache_status" args: "$args '
                 'sorted_args: "$sorted_args" ';

  access_log       logs/nginx-http_access.log;

  proxy_cache_path /tmp/cache levels=1:2 keys_zone=zone:10m inactive=10d max_size=100m;

  server {
    listen          8080;
    server_name     localhost;

    access_log       logs/nginx-http_access.log main;

    location /filtered {
      sorted_args_filter keep -i v _ time b;

      proxy_set_header Host "static_files_server";
      proxy_pass http://localhost:8081;

      proxy_cache zone;
      proxy_cache_key "$sorted_args";
      proxy_cache_valid 200 1m;
    }

    location / {
      proxy_pass http://localhost:8081;

      proxy_cache zone;
      proxy_cache_key "$sorted_args";
      proxy_cache_valid 200 10m;
    }
  }

  server {
    listen          8081;

    location / {
      return 200 "$args\n";
    }
  }
}
```

Variables
---------

* **$sorted_args** - args after filtering and sorting
* **$sorted_is_args** - "?" if args after filtering and sorting is not empty, or an empty string otherwise
* **$sorted_has_args** - "&" if args after filtering and sorting is not empty, or "?" otherwise

Directives
----------

**sorted_args_filter**

**Syntax:** `sorted_args_filter off | keep [-i] args ... | remove [-i] args ...;`

**Default:** *-*

**Context:** *http, server, location, if in location, when*

Configures which parameters are retained while evaluating `$sorted_args` or overwriting the request arguments.

- `keep` retains matching parameters and removes all others
- `remove` removes matching parameters and retains all others
- `off` disables a filter inherited from an upper context

Optional **-i** parameter enables case-insensitive parameter matching.

Argument names cannot be empty. They support exact matches, prefix wildcards, and suffix wildcards:

- `token` matches only `token`
- `utm_*` matches names starting with `utm_`
- `*_sig` matches names ending with `_sig`

A single `*` must be the only argument name. `keep *` keeps every parameter,
while `remove *` removes every parameter.

Examples:
```nginx
# Keep only 'id' and 'name' parameters (case-sensitive)
sorted_args_filter keep id name;

# Keep only 'id' and 'name' parameters (case-insensitive)
sorted_args_filter keep -i id name;

# Remove parameters with either wildcard shape
sorted_args_filter remove utm_* *_sig;

# Disable inherited filtering
sorted_args_filter off;

# Keep or remove every parameter
sorted_args_filter keep *;
sorted_args_filter remove *;
```

**sorted_args_clear_valueless_args**

**Syntax:** *sorted_args_clear_valueless_args on | off;*

**Default:** *off*

**Context:** *http, server, location, if in location, when*

If enabled, removes parameters that have no value (e.g., `key` or `key=`).

Examples:
```nginx
# Enable clearing valueless args
sorted_args_clear_valueless_args on;

# Input:  a=1&b=&c&d=2
# Output: a=1&d=2
# (b= and c are removed)
```

**sorted_args_clear_invalid_args**

**Syntax:** *sorted_args_clear_invalid_args on | off;*

**Default:** *off*

**Context:** *http, server, location, if in location, when*

If enabled, removes invalid parameters whose key is empty. This includes empty segments from consecutive ampersands (`&&`), a bare equals sign (`=`), and values with no key (`=value`).

Examples:
```nginx
sorted_args_clear_invalid_args on;

# Input:  a=1&&=&&=ac&b=2&c=&d
# Output: a=1&b=2&c=&d
```

**sorted_args_order**

**Syntax:** *sorted_args_order asc | desc;*

**Default:** *asc*

**Context:** *http, server, location, if in location, when*

Controls the output sort order for `$sorted_args`.

Examples:
```nginx
sorted_args_order desc;

# Input:  a=1&c=3&b=2
# Output: c=3&b=2&a=1
```

**sorted_args_dedupe**

**Syntax:** *sorted_args_dedupe first | last | off;*

**Default:** *off*

**Context:** *http, server, location, if in location, when*

Controls whether duplicate argument names are removed. `first` keeps the first occurrence in the original query string, `last` keeps the last occurrence, and `off` preserves all occurrences.

Examples:
```nginx
sorted_args_dedupe first;

# Input:  b=2&a=1&a=3&c=4
# Output: a=1&b=2&c=4
```

**sorted_args_overwrite**

**Syntax:** *sorted_args_overwrite on | off;*

**Default:** *off*

**Context:** *http, server, location, if in location, when*

If enabled, overrides the original `$args` with the sorted and filtered result. This allows downstream modules and proxy_pass to use the sorted arguments directly.

Examples:
```nginx
location /api {
    # Enable overwriting original args
    sorted_args_overwrite on;

    # Keep only specific parameters
    sorted_args_filter keep id name version;

    # Clear valueless args
    sorted_args_clear_valueless_args on;

    # Now $args contains the sorted and filtered result
    proxy_pass http://backend;

    # Input:  version=1&id=123&name=&token=abc&extra=xyz
    # $args:  id=123&version=1
    # (sorted, filtered to keep only id/name/version, empty name= removed)
}
```

**Important:** If `sorted_args_overwrite` is enabled, the original query string is modified early in the request processing, affecting all subsequent phases.


Conditional configuration
-------------------------

When [ngx_condition_module](https://git.hanada.info/hanada/ngx_condition_module)
is enabled, every directive provided by this module can be placed inside an
`http`, `server`, or `location` `when` block:

```nginx
condition mobile_client str_contains -i $http_user_agent mobile;

when mobile_client {
    sorted_args_filter remove tracking_id;
    sorted_args_order desc;
    sorted_args_dedupe first;
    sorted_args_clear_valueless_args on;
    sorted_args_clear_invalid_args on;
    sorted_args_overwrite on;
}
```

Rules are evaluated in configuration order. An unconditional value placed
before a conditional value has higher priority. Use `sorted_args_filter off`
inside a `when` block when that condition should disable an inherited filter.


<a id="installation"></a>Installation Instructions
--------------------------------------------------

[Download Nginx Stable](http://nginx.org/en/download.html) source and uncompress it (ex.: to ../nginx). You must then run ./configure with --add-module pointing to this project as usual. Something in the lines of:

    $ ./configure \
        --add-module=../nginx-sorted-args-module \
        --prefix=/home/user/dev-workspace/nginx
    $ make
    $ make install

To enable conditional configuration, build `ngx_condition_module` before this
module in the same Nginx configuration:

    $ ./configure \
        --add-module=../ngx_condition_module \
        --add-module=../ngx_http_sorted_args_module


Running Tests
-------------

This project uses [nginx_test_helper](https://github.com/wandenberg/nginx_test_helper) on the test suite. So, after you've installed the module, you can just download the necessary gems:

    $ cd test
    $ bundle install

And run rspec pointing to where your Nginx binary is (default: /usr/local/nginx/sbin/nginx):

    $ NGINX_EXEC=../path/to/my/nginx rspec .


Changelog
---------

This is still a work in progress. Be the change. And take a look on the Changelog file.
