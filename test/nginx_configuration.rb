require 'nginx_test_helper'
module NginxConfiguration
  def self.default_configuration
    {
      disable_start_stop_server: false,
      master_process: 'off',
      daemon: 'off',

      remove_args: nil,
      remove_args2: nil,
      keep_args: nil,
      keep_args2: nil,
      clear_valueless_args: nil,
      clear_invalid_args: nil,
      order: nil,
      dedupe: nil,
    }
  end


  def self.template_configuration
  %(
pid               <%= pid_file %>;
error_log         <%= error_log %> debug;

# Development Mode
master_process    <%= master_process %>;
daemon            <%= daemon %>;

worker_processes  <%= nginx_workers %>;

events {
  worker_connections  1024;
  use                 <%= (RUBY_PLATFORM =~ /darwin/) ? 'kqueue' : 'epoll' %>;
}

http {
  access_log      <%= access_log %>;

  proxy_cache_path <%= File.expand_path(nginx_tests_tmp_dir) %>/cache levels=1:2 keys_zone=zone:10m inactive=10d max_size=100m;

  server {
    listen        <%= nginx_port %>;
    server_name   <%= nginx_host %>;

    <%= write_directive("sorted_args_remove_args", remove_args) %>
    <%= write_directive("sorted_args_keep_args", keep_args) %>
    <%= write_directive("sorted_args_clear_valueless_args", clear_valueless_args) %>
    <%= write_directive("sorted_args_clear_invalid_args", clear_invalid_args) %>
    <%= write_directive("sorted_args_order", order) %>
    <%= write_directive("sorted_args_dedupe", dedupe) %>

    location / {
      proxy_set_header Host "static_files_server";
      proxy_pass http://<%= nginx_host %>:<%= nginx_port %>;

      proxy_cache zone;
      proxy_cache_key "$uri$is_args$sorted_args";
      proxy_cache_valid 200 1m;
    }
  }

  server {
    listen        <%= nginx_port %>;
    server_name   static_files_server;

    <%= write_directive("sorted_args_remove_args", remove_args) %>
    <%= write_directive("sorted_args_keep_args", keep_args) %>
    <%= write_directive("sorted_args_clear_valueless_args", clear_valueless_args) %>
    <%= write_directive("sorted_args_clear_invalid_args", clear_invalid_args) %>
    <%= write_directive("sorted_args_order", order) %>
    <%= write_directive("sorted_args_dedupe", dedupe) %>

    location /overwrite {
      <%= write_directive("sorted_args_remove_args", remove_args2) %>
      <%= write_directive("sorted_args_keep_args", keep_args2) %>

      return 200 '{"args": "$args", "sorted_args": "$sorted_args"}';
    }

    location / {
      return 200 '{"args": "$args", "sorted_args": "$sorted_args"}';
    }
  }
}
  )
  end
end
