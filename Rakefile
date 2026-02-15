# frozen_string_literal: true

require "rake/extensiontask"
require "rake/testtask"

# XXX: Danbooru's container uses g++-13
ENV["MAKE"] = "make CXX=g++-13"

Rake::ExtensionTask.new "post_query" do |ext|
  ext.lib_dir = "lib/post_query"
end

CLEAN.include ["lib/post_query/post_query.so"]

task test: :compile
Rake::TestTask.new(:test) do |t|
  t.test_files = ["test/test.rb"]
end
