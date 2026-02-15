# frozen_string_literal: true

require_relative "lib/post_query/version"

Gem::Specification.new do |spec|
  spec.name = "post_query"
  spec.version = PostQuery::VERSION
  spec.authors = [ "TypeA2" ]
  
  spec.summary = "Danboor Post Query Parser"
  spec.homepage = "https://github.com/TypeA2/post_query"
  spec.license = "MIT"
  spec.required_ruby_version = ">=3.4.5"
  spec.extensions = ["ext/post_query/extconf.rb"]

  spec.files = [
    "lib/post_query.rb",
    "lib/post_query/version.rb",
    "lib/post_query/post_query.so",
  ]

  spec.add_development_dependency("rake", ["~> 13"])
  spec.add_development_dependency("rake-compiler", ["~> 1.3"])
end
