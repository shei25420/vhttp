module vhttp

  class ConfigurationContext
    def self.instance()
      @@instance
    end
    def self.reset()
      @@instance = self.new()
    end
    def initialize()
      @values = {}
      @post_handler_generation_hooks = [
        proc {|handler|
          if !handler.respond_to?(:call)
            raise "app is not callable"
          end
        }
      ]
    end
    def get_value(key)
      @values[key]
    end
    def set_value(key, value)
      @values[key] = value
    end
    def delete_value(key)
      @values[key].delete
    end
    def add_post_handler_generation_hook(hook)
      @post_handler_generation_hooks << hook
    end
    def call_post_handler_generation_hooks(handler)
      @post_handler_generation_hooks.each {|hook| hook.call(handler) }
    end
  end

end
