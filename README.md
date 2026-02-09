Nginx Sorted Args Module
===============================

This Nginx module orders the args parameters of an HTTP request alphanumerically and makes the sorted key-value pairs accessible using an Nginx variable.

Requests like `/index.html?b=2&a=1&c=3`, `/index.html?b=2&c=3&a=1`, `/index.html?c=3&a=1&b=2`, `/index.html?c=3&b=2&a=1` will produce the same normalized args `a=1&b=2&c=3` which can be accessed within Nginx using the `$sorted_args` variable.

This is especially useful if you want to normalize the args to be used in a cache key, for example when used with the `proxy_cache_key` directive.

It is also possible to remove one or more undesired query parameters by defining their name with the `sorted_args_filter` directive, like `sorted_args_filter <parameter_name> [<parameter_name> <parameter_name> ...];`.

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
* **$sorted_is_args** - "?" if args after filtering and sorting is not null, or an empty string otherwise
* **$sorted_has_args** - "&" if args after filtering and sorting is not null, or "?" otherwise

Directives
----------

**sorted_args_filter**

**Syntax:** *sorted_args_filter keep [-i] args ...;* | *sorted_args_filter clear [-i] args ...;*

**Default:** *-*

**Context:** *http, server, location, if in location*

list parameters to be filtered while using the `$sorted_args` variable.

The directive supports two modes:
- **keep**: Only keep the specified parameters, remove all others
- **clear**: Remove the specified parameters, keep all others

Optional **-i** parameter enables case-insensitive parameter matching.

Examples:
```nginx
# Keep only 'id' and 'name' parameters (case-sensitive)
sorted_args_filter keep id name;

# Keep only 'id' and 'name' parameters (case-insensitive)
sorted_args_filter keep -i id name;

# Remove 'token' and 'session' parameters, keep all others
sorted_args_filter clear token session;

# Remove 'token' and 'session' parameters (case-insensitive)
sorted_args_filter clear -i token session;
```

**sorted_args_clear_empty_args**

**Syntax:** *sorted_args_clear_empty_args on | off;*

**Default:** *off*

**Context:** *http, server, location, if in location*

If enabled, removes parameters that have no value (e.g., `key` or `key=`).

Examples:
```nginx
# Enable clearing empty args
sorted_args_clear_empty_args on;

# Input:  ?a=1&b=&c&d=2
# Output: a=1&d=2
# (b= and c are removed)
```


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
