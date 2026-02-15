# frozen_string_literal: true

require "mkmf"

$CXXFLAGS += "-Wall -std=c++23"

create_makefile "post_query/post_query"
