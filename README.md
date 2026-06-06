Nginx Sorted Args Module
===============================

This Nginx module orders the args parameters of an HTTP request alphanumerically and makes the sorted key-value pairs accessible using an Nginx variable.

Requests like `/index.html?b=2&a=1&c=3`, `/index.html?b=2&c=3&a=1`, `/index.html?c=3&a=1&b=2`, `/index.html?c=3&b=2&a=1` will produce the same normalized args `a=1&b=2&c=3` which can be accessed within Nginx using the `$sorted_args` variable.

Sorting is bytewise and case-sensitive; the `-i` option only affects parameter filtering directives.

This is especially useful if you want to normalize the args to be used in a cache key, for example when used with the `proxy_cache_key` directive.

It is also possible to remove or keep selected query parameters with the `sorted_args_remove_args` and `sorted_args_keep_args` directives.

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
      sorted_args_keep_args -i v _ time b;

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

**sorted_args_remove_args**

**Syntax:** `sorted_args_remove_args * | [-i] args ...;`

**Default:** *-*

**Context:** *http, server, location, if in location*

List parameters to remove while using the `$sorted_args` variable. It cannot be configured in the same context as `sorted_args_keep_args`.

Use `sorted_args_remove_args *;` to clear all arguments. This also replaces any remove or keep list inherited from an upper context.

Optional **-i** parameter enables case-insensitive parameter matching.

Argument names support exact matches, prefix wildcards, and suffix wildcards:
- `token` matches only `token`
- `utm_*` matches names starting with `utm_`
- `*_sig` matches names ending with `_sig`
- `''` (empty string) matches parameters with an empty key (value with no name)

A single `*` has the special meaning above, so use a non-empty prefix or suffix when wildcard-filtering a remove list.

Examples:
```nginx
# Remove 'token' and 'session' parameters
sorted_args_remove_args token session;

# Remove all parameters with an empty key
sorted_args_remove_args '';

# Remove parameters with either wildcard shape
sorted_args_remove_args utm_* *_sig;

# Remove parameters case-insensitively
sorted_args_remove_args -i token session;

# Clear all args
sorted_args_remove_args *;
```

**sorted_args_keep_args**

**Syntax:** `sorted_args_keep_args * | [-i] args ...;`

**Default:** *-*

**Context:** *http, server, location, if in location*

List parameters to keep while using the `$sorted_args` variable. All other parameters are removed. It cannot be configured in the same context as `sorted_args_remove_args`.

Use `sorted_args_keep_args *;` to disable a remove or keep list inherited from an upper context.

Optional **-i** parameter enables case-insensitive parameter matching.

Prefix and suffix wildcards are supported. A single `*` has the special meaning above, so use a non-empty prefix or suffix when wildcard-filtering a keep list.

The argument list supports the same matching rules as `sorted_args_remove_args`:
- `token` matches only `token`
- `utm_*` matches names starting with `utm_`
- `*_sig` matches names ending with `_sig`
- `''` (empty string) matches parameters with an empty key

Examples:
```nginx
# Keep only 'id' and 'name' parameters (case-sensitive)
sorted_args_keep_args id name;

# Keep only 'id' and 'name' parameters (case-insensitive)
sorted_args_keep_args -i id name;

# Keep only parameters with an empty key
sorted_args_keep_args '';

# Keep only parameters beginning with 'public_'
sorted_args_keep_args public_*;

# Disable inherited filtering
sorted_args_keep_args *;
```

**sorted_args_clear_valueless_args**

**Syntax:** *sorted_args_clear_valueless_args on | off;*

**Default:** *off*

**Context:** *http, server, location, if in location*

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

**Context:** *http, server, location, if in location*

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

**Context:** *http, server, location, if in location*

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

**Context:** *http, server, location, if in location*

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

**Context:** *http, server, location, if in location*

If enabled, overrides the original `$args` with the sorted and filtered result. This allows downstream modules and proxy_pass to use the sorted arguments directly.

Examples:
```nginx
location /api {
    # Enable overwriting original args
    sorted_args_overwrite on;

    # Keep only specific parameters
    sorted_args_keep_args id name version;

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


<a id="installation"></a>Installation Instructions
--------------------------------------------------

[Download Nginx Stable](http://nginx.org/en/download.html) source and uncompress it (ex.: to ../nginx). You must then run ./configure with --add-module pointing to this project as usual. Something in the lines of:

    $ ./configure \
        --add-module=../nginx-sorted-args-module \
        --prefix=/home/user/dev-workspace/nginx
    $ make
    $ make install


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
