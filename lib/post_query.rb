# frozen_string_literal: true

require "post_query/post_query"

class PostQuery
  class Error < StandardError; end

  def self.parse(string, metatags: [])
    parse_raw(string, metatags)
  end
end
