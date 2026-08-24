#!/usr/bin/perl

# Tests for ngx_http_sorted_args_module.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use Test::Nginx qw/ :DEFAULT http_content /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite ngx_condition_module
	ngx_http_sorted_args_module/)->plan(14);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    sorted_args_filter remove secret;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        condition descending str_eq $http_x_order desc;

        location /default {
            sorted_args_filter off;
            return 200 "$sorted_args|$sorted_is_args|$sorted_has_args";
        }

        location /inherited {
            return 200 "$sorted_args";
        }

        location /keep {
            sorted_args_filter keep -i id utm_* *_sig;
            return 200 "$sorted_args";
        }

        location /remove {
            sorted_args_filter remove token tmp_* *_sig;
            return 200 "$sorted_args";
        }

        location /clean {
            sorted_args_filter off;
            sorted_args_clear_valueless_args on;
            sorted_args_clear_invalid_args on;
            return 200 "$sorted_args";
        }

        location /desc {
            sorted_args_filter off;
            sorted_args_order desc;
            return 200 "$sorted_args";
        }

        location /first {
            sorted_args_filter off;
            sorted_args_dedupe first;
            return 200 "$sorted_args";
        }

        location /last {
            sorted_args_filter off;
            sorted_args_dedupe last;
            return 200 "$sorted_args";
        }

        location /overwrite {
            sorted_args_filter remove secret;
            sorted_args_overwrite on;
            return 200 "$args";
        }

        location /conditional {
            sorted_args_filter off;
            when descending {
                sorted_args_order desc;
            }
            sorted_args_order asc;
            return 200 "$sorted_args";
        }
    }
}

EOF

$t->run();

###############################################################################

is(content('/default?b=2&a=1&c=3'), 'a=1&b=2&c=3|?|&',
	'default ascending order');
is(content('/default'), '||?', 'empty argument helpers');
is(content('/default?b=2&a=1&a=0'), 'a=0&a=1&b=2|?|&',
	'duplicate arguments preserved');
is(content('/inherited?secret=x&b=2&a=1'), 'a=1&b=2',
	'inherited filter');
is(content('/keep?ID=2&utm_source=x&x_sig=s&drop=1'),
	'ID=2&utm_source=x&x_sig=s', 'keep exact and wildcard arguments');
is(content('/remove?z=3&token=x&tmp_a=1&x_sig=s&a=2'),
	'a=2&z=3', 'remove exact and wildcard arguments');
is(content('/clean?a=1&&=&&=bad&b=&c&d=2'), 'a=1&d=2',
	'clear invalid and valueless arguments');
is(content('/desc?a=1&c=3&b=2'), 'c=3&b=2&a=1', 'descending order');
is(content('/first?b=2&a=1&a=3&c=4'), 'a=1&b=2&c=4',
	'keep first duplicate');
is(content('/last?b=2&a=1&a=3&c=4'), 'a=3&b=2&c=4',
	'keep last duplicate');
is(content('/overwrite?secret=x&b=2&a=1'), 'a=1&b=2',
	'overwrite request arguments');
is(content('/conditional?b=2&a=1'), 'a=1&b=2',
	'conditional order miss');
is(header_content('/conditional?b=2&a=1', 'asc'), 'a=1&b=2',
	'conditional fallback');
is(header_content('/conditional?b=2&a=1', 'desc'), 'b=2&a=1',
	'conditional order hit');

###############################################################################

sub content {
	my ($uri) = @_;
	return http_content(http_get($uri));
}

sub header_content {
	my ($uri, $order) = @_;
	return http_content(http(<<EOF));
GET $uri HTTP/1.0
Host: localhost
X-Order: $order

EOF
}

###############################################################################
