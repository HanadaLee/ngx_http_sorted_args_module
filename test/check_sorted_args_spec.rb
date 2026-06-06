require File.expand_path('spec_helper', File.dirname(__FILE__))

describe "check sorted args module" do
  it "should expose the args args ordered in '$sorted_args' variable" do
    nginx_run_server do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?c=3&=6&a=1&=5&b=2").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "c=3&=6&a=1&=5&b=2", "sorted_args": "=5&=6&a=1&b=2&c=3"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should support arrays like parameters" do
    nginx_run_server do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?a=2&c[]=3&=6&a=1&=5&b=2&c[]=1&c[]=2").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "a=2&c[]=3&=6&a=1&=5&b=2&c[]=1&c[]=2", "sorted_args": "=5&=6&a=1&a=2&b=2&c[]=1&c[]=2&c[]=3"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should sort prefixed values by full parameter" do
    nginx_run_server do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?key=value1&key=value").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "key=value1&key=value", "sorted_args": "key=value&key=value1"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should sort prefixed names by full key before comparing values" do
    nginx_run_server do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?key1=value1&key=value").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "key1=value1&key=value", "sorted_args": "key=value&key1=value1"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should sort parameter names case-sensitively" do
    nginx_run_server do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?a=1&A=1").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "a=1&A=1", "sorted_args": "A=1&a=1"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should remove specified parameters" do
    nginx_run_server({remove_args: ["c", "_"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?c=3&=6&a=1&=5&b=2&_=12323&c=7").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "c=3&=6&a=1&=5&b=2&_=12323&c=7", "sorted_args": "=5&=6&a=1&b=2"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should not return error if there isn't a parameter" do
    nginx_run_server do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "", "sorted_args": ""}'
          EventMachine.stop
        end
      end
    end
  end

  it "should not return error if all parameters were removed" do
    nginx_run_server({remove_args: ["c", "_"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?c=3&_=12323&c=7").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "c=3&_=12323&c=7", "sorted_args": ""}'
          EventMachine.stop
        end
      end
    end
  end

  it "should be possible use the variable as cache_key" do
    nginx_run_server({remove_args: ["c", "_"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?c=3&=6&a=1&=5&b=2&_=12323&c=7").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "c=3&=6&a=1&=5&b=2&_=12323&c=7", "sorted_args": "=5&=6&a=1&b=2"}'

          req = EventMachine::HttpRequest.new("#{nginx_address}/?b=2&c=3&=6&=5&_=12323&c=7&a=1").get
          req.callback do
            expect(req).to be_http_status(200)
            expect(req.response).to be === '{"args": "c=3&=6&a=1&=5&b=2&_=12323&c=7", "sorted_args": "=5&=6&a=1&b=2"}'
            EventMachine.stop
          end
        end
      end
    end
  end

  it "should be possible overwrite the remove parameter list by each location" do
    nginx_run_server({remove_args: ["c", "_"], remove_args2: ["a", "b"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?c=3&=6&a=1&=5&b=2&_=12323&c=7").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "c=3&=6&a=1&=5&b=2&_=12323&c=7", "sorted_args": "=5&=6&a=1&b=2"}'

          req = EventMachine::HttpRequest.new("#{nginx_address}/overwrite?c=3&=6&a=1&=5&b=2&_=12323&c=7").get
          req.callback do
            expect(req).to be_http_status(200)
            expect(req.response).to be === '{"args": "c=3&=6&a=1&=5&b=2&_=12323&c=7", "sorted_args": "=5&=6&_=12323&c=3&c=7"}'
            EventMachine.stop
          end
        end
      end
    end
  end

  it "should remove empty key parameters with '' filter" do
    nginx_run_server({remove_args: ["''"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?c=3&=6&a=1&=5&b=2&_=12323&c=7").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "c=3&=6&a=1&=5&b=2&_=12323&c=7", "sorted_args": "_=12323&a=1&b=2&c=3&c=7"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should clear all parameters with remove wildcard all" do
    nginx_run_server({remove_args: ["*"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?c=3&=6&a=1&=5&b=2").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "c=3&=6&a=1&=5&b=2", "sorted_args": ""}'
          EventMachine.stop
        end
      end
    end
  end

  it "should disable inherited keep parameters with wildcard all" do
    nginx_run_server({keep_args: ["a"], keep_args2: ["*"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/overwrite?c=3&b=2&a=1").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "c=3&b=2&a=1", "sorted_args": "a=1&b=2&c=3"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should treat off as a regular remove parameter" do
    nginx_run_server({remove_args: ["off"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?c=3&off=1&a=1").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "c=3&off=1&a=1", "sorted_args": "a=1&c=3"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should treat all as a regular keep parameter" do
    nginx_run_server({keep_args: ["all"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?c=3&all=1&a=1").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "c=3&all=1&a=1", "sorted_args": "all=1"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should keep only specified parameters" do
    nginx_run_server({keep_args: ["id", "name"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?token=abc&name=bob&id=123&extra=1").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "token=abc&name=bob&id=123&extra=1", "sorted_args": "id=123&name=bob"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should support wildcard parameters" do
    nginx_run_server({remove_args: ["utm_*", "*_sig"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?utm_source=google&a=1&request_sig=abc&name=2&utm_medium=cpc").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "utm_source=google&a=1&request_sig=abc&name=2&utm_medium=cpc", "sorted_args": "a=1&name=2"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should sort parameters in descending order" do
    nginx_run_server({order: ["desc"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?a=1&c=3&b=2").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "a=1&c=3&b=2", "sorted_args": "c=3&b=2&a=1"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should dedupe parameters keeping the first occurrence" do
    nginx_run_server({dedupe: ["first"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?b=2&a=1&a=3&c=4&a=0").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "b=2&a=1&a=3&c=4&a=0", "sorted_args": "a=1&b=2&c=4"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should dedupe parameters keeping the last occurrence" do
    nginx_run_server({dedupe: ["last"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?b=2&a=1&a=3&c=4&a=0").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "b=2&a=1&a=3&c=4&a=0", "sorted_args": "a=0&b=2&c=4"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should clear valueless parameters" do
    nginx_run_server({clear_valueless_args: ["on"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?a=1&b=&c&d=2").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "a=1&b=&c&d=2", "sorted_args": "a=1&d=2"}'
          EventMachine.stop
        end
      end
    end
  end

  it "should clear invalid parameters" do
    nginx_run_server({clear_invalid_args: ["on"]}) do
      EventMachine.run do
        req = EventMachine::HttpRequest.new("#{nginx_address}/?a=1&&=&&=ac&b=2&c=&d").get
        req.callback do
          expect(req).to be_http_status(200)
          expect(req.response).to be === '{"args": "a=1&&=&&=ac&b=2&c=&d", "sorted_args": "a=1&b=2&c=&d"}'
          EventMachine.stop
        end
      end
    end
  end
end
