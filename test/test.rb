# frozen_string_literal: true

METATAGS = [
  "user", "approver", "commenter", "comm", "noter", "noteupdater", "artcomm",
  "commentaryupdater", "flagger", "appealer", "upvote", "downvote", "fav",
  "ordfav", "favgroup", "ordfavgroup", "reacted", "pool", "ordpool", "note",
  "comment", "commentary", "id", "rating", "source", "status", "filetype",
  "disapproved", "parent", "child", "search", "embedded", "md5", "pixelhash",
  "width", "height", "mpixels", "ratio", "score", "upvotes", "downvotes",
  "favcount", "filesize", "date", "age", "order", "limit", "tagcount",
  "pixiv_id", "pixiv", "unaliased", "exif", "duration", "random", "is", "has",
  "ai", "comment_count", "deleted_comment_count", "active_comment_count",
  "note_count", "deleted_note_count", "active_note_count", "flag_count",
  "child_count", "deleted_child_count", "active_child_count", "pool_count",
  "deleted_pool_count", "active_pool_count", "series_pool_count",
  "collection_pool_count", "appeal_count", "approval_count",
  "replacement_count", "comments", "deleted_comments", "active_comments",
  "notes", "deleted_notes", "active_notes", "flags", "children",
  "deleted_children", "active_children", "pools", "deleted_pools",
  "active_pools", "series_pools", "collection_pools", "appeals", "approvals",
  "replacements", "arttags", "copytags", "chartags", "gentags", "metatags"
]

require "./lib/post_query"

if true
  def dump(title, node)
    puts "#{title}: #{node.inspect}"
    puts "   > infix -> [#{node.to_infix}]"
    puts "   > s-exp -> [#{node.to_sexp}]"
    #puts "   > tree -> [#{parsed.tree}]"
  end

  def test(exp)
    parsed = PostQuery.parse(exp, metatags: METATAGS)
    dump("Raw", parsed)
    dump("CNF", parsed.to_cnf)
  end

  test "a and"
else
  require "minitest/autorun"

  class PostQueryTest < Minitest::Test
    def parse(input, metatags: METATAGS)
      PostQuery.parse(input, metatags: metatags).to_cnf.to_sexp
    end
  
    def assert_parse_equals(expected, input)
      assert_equal(expected, parse(input))
    end
  
    def test_empty_queries
      assert_parse_equals("all", "")
      assert_parse_equals("all", " ")
    end
  
    def test_basic_tags
      assert_parse_equals("a", "a")
      assert_parse_equals("a", "A")
  
      assert_parse_equals(";)", ";)")
      assert_parse_equals("9", "(9)")
    end

    def test_parentheses
      assert_parse_equals("foo_(bar)", "foo_(bar)")
      assert_parse_equals("foo_(bar)", "(foo_(bar))")
      assert_parse_equals("foo_(bar)", "((foo_(bar)))")

      assert_parse_equals("foo_(bar_(baz))", "foo_(bar_(baz))")
      assert_parse_equals("foo_(bar_(baz))", "(foo_(bar_(baz)))")
      assert_parse_equals("foo_(bar_baz))", "(foo_(bar_baz)))")

      assert_parse_equals("(and abc_(def) ghi)", "abc_(def) ghi")
      assert_parse_equals("(and abc_(def) ghi)", "(abc_(def) ghi)")
      assert_parse_equals("(and abc_(def) ghi)", "((abc_(def)) ghi)")

      assert_parse_equals("(and abc def_(ghi))", "abc def_(ghi)")
      assert_parse_equals("(and abc def_(ghi))", "(abc def_(ghi))")
      assert_parse_equals("(and abc def_(ghi))", "(abc (def_(ghi)))")

      assert_parse_equals("(and abc_(def) ghi_(jkl))", "abc_(def) ghi_(jkl)")
      assert_parse_equals("(and abc_(def) ghi_(jkl))", "(abc_(def) ghi_(jkl))")

      assert_parse_equals(":)", ":)")
      assert_parse_equals(":)", "(:))")
      assert_parse_equals("none", "(:)")

      assert_parse_equals("(and :) >:))", "(:) >:))")
      assert_parse_equals("none", "(:) >:)")

      assert_parse_equals("(wildcard *))", "*)")
      assert_parse_equals("(wildcard *)", "(*)")

      assert_parse_equals("(wildcard foo*)", "(foo*)")
      assert_parse_equals("(wildcard foo*))", "foo*)")

      assert_parse_equals("(and bar (wildcard foo*)))", "foo*) bar")
      assert_parse_equals("(and bar (wildcard foo*))", "(foo*) bar")
      assert_parse_equals("(and bar) (wildcard foo*))", "(foo*) bar)")

      assert_parse_equals("(wildcard *_(foo))", "*_(foo)")
      assert_parse_equals("(wildcard *_(foo))", "(*_(foo))")

      assert_parse_equals("(and bar (wildcard *_(foo)))", "(*_(foo) bar)")
      assert_parse_equals("(and bar (wildcard *_(foo)))", "((*_(foo)) bar)")
      assert_parse_equals("(and bar (wildcard *_(foo)))", "(bar *_(foo))")
      assert_parse_equals("(and bar (wildcard *_(foo)))", "(bar (*_(foo)))")

      assert_parse_equals("note:a", "(note:a)")
      assert_parse_equals("note:(a", "(note:(a)")
      assert_parse_equals("note:(a)", "(note:(a))")

      assert_parse_equals("(and note:a note:b)", "(note:a note:b)")
      assert_parse_equals("(and note:a note:b))", "(note:a) note:b)")
      assert_parse_equals('(and note:"a)" note:b)', '(note:"a)" note:b)')
    end

    def test_basic_queries
      assert_parse_equals("(and a b)", "a b")
      assert_parse_equals("(or a b)", "a or b")
      assert_parse_equals("(or a b)", "~a ~b")

      assert_parse_equals("(not a)", "-a")
      assert_parse_equals("(and (not b) a)", "a -b")

      assert_parse_equals("fav:a", "fav:a")
      assert_parse_equals("(not fav:a)", "-fav:a")

      assert_parse_equals("(and fav:a fav:b)", "fav:a fav:b")
    end

    def test_metatags
      assert_parse_equals("fav:a", "fav:a")
      assert_parse_equals("user:a", "user:a")
      assert_parse_equals("pool:a", "pool:a")
      assert_parse_equals("order:a", "order:a")
      assert_parse_equals("source:a", "source:a")

      assert_parse_equals("fav:a", "FAV:a")
      assert_parse_equals("fav:A", "fav:A")

      assert_parse_equals("fav:a", "~fav:a")
      assert_parse_equals("(not fav:a)", "-fav:a")

      assert_parse_equals("(and fav:a fav:b)", "fav:a fav:b")
      assert_parse_equals("(or fav:a fav:b)", "~fav:a ~fav:b")
      assert_parse_equals("(or fav:a fav:b)", "fav:a or fav:b")

      assert_parse_equals("fav:a", "(fav:a)")
      assert_parse_equals("fav:(a)", "fav:(a)")
      assert_parse_equals("fav:(a", "(fav:(a)")

      assert_parse_equals('source:"foo bar"', 'source:"foo bar"')
      assert_parse_equals('source:foobar"(', 'source:foobar"(')
      assert_parse_equals('source:', 'source:')
      assert_parse_equals('source:""', 'source:""')
      assert_parse_equals('source:"\""', 'source:"\""')
      assert_parse_equals(%q{source:"don't say \"lazy\" okay"}, %q{source:"don't say \"lazy\" okay"})
      assert_parse_equals(%q{(and source:"foo)bar" a)}, %q{(a (source:"foo)bar"))})

      assert_parse_equals('source:"foo bar"', "source:'foo bar'")
      assert_parse_equals("source:foobar'(", "source:foobar'(")
      assert_parse_equals('source:""', "source:''")
      assert_parse_equals('source:"\'"', "source:'\\''")
      assert_parse_equals(%q{source:"don't say \"lazy\" okay"}, %q{source:'don\'t say "lazy" okay'})
      assert_parse_equals(%q{(and source:"foo)bar" a)}, %q{(a (source:'foo)bar'))})

      assert_parse_equals('(and source:"foo bar" baz)', %q{source:foo\ bar baz})
      assert_parse_equals(%q{(and source:"don't say \"lazy\"" blah)}, %q{source:don't\ say\ "lazy" blah})
    end

    def test_synonyms
      assert_parse_equals("comment_count:0", "comments:0")
      assert_parse_equals("deleted_comment_count:0", "deleted_comments:0")
      assert_parse_equals("active_comment_count:0", "active_comments:0")
      assert_parse_equals("note_count:0", "notes:0")
      assert_parse_equals("deleted_note_count:0", "deleted_notes:0")
      assert_parse_equals("active_note_count:0", "active_notes:0")
      assert_parse_equals("flag_count:0", "flags:0")
      assert_parse_equals("child_count:0", "children:0")
      assert_parse_equals("deleted_child_count:0", "deleted_children:0")
      assert_parse_equals("active_child_count:0", "active_children:0")
      assert_parse_equals("pool_count:0", "pools:0")
      assert_parse_equals("deleted_pool_count:0", "deleted_pools:0")
      assert_parse_equals("active_pool_count:0", "active_pools:0")
      assert_parse_equals("series_pool_count:0", "series_pools:0")
      assert_parse_equals("collection_pool_count:0", "collection_pools:0")
      assert_parse_equals("appeal_count:0", "appeals:0")
      assert_parse_equals("approval_count:0", "approvals:0")
      assert_parse_equals("replacement_count:0", "replacements:0")

      assert_parse_equals("order:comment_count", "order:comments")
      assert_parse_equals("order:note_count", "order:notes")
      assert_parse_equals("order:flag_count", "order:flags")
      assert_parse_equals("order:child_count", "order:children")
      assert_parse_equals("order:pool_count", "order:pools")
      assert_parse_equals("order:appeal_count", "order:appeals")
      assert_parse_equals("order:approval_count", "order:approvals")
      assert_parse_equals("order:replacement_count", "order:replacements")
      
      assert_parse_equals("order:comment_count_asc", "order:comments_asc")
      assert_parse_equals("order:note_count_asc", "order:notes_asc")
      assert_parse_equals("order:flag_count_asc", "order:flags_asc")
      assert_parse_equals("order:child_count_asc", "order:children_asc")
      assert_parse_equals("order:pool_count_asc", "order:pools_asc")
      assert_parse_equals("order:appeal_count_asc", "order:appeals_asc")
      assert_parse_equals("order:approval_count_asc", "order:approvals_asc")
      assert_parse_equals("order:replacement_count_asc", "order:replacements_asc")

      assert_parse_equals("order:comment_count_desc", "order:comments_desc")
      assert_parse_equals("order:note_count_desc", "order:notes_desc")
      assert_parse_equals("order:flag_count_desc", "order:flags_desc")
      assert_parse_equals("order:child_count_desc", "order:children_desc")
      assert_parse_equals("order:pool_count_desc", "order:pools_desc")
      assert_parse_equals("order:appeal_count_desc", "order:appeals_desc")
      assert_parse_equals("order:approval_count_desc", "order:approvals_desc")
      assert_parse_equals("order:replacement_count_desc", "order:replacements_desc")
    end

    def test_wildcards
      assert_parse_equals("(wildcard *)", "*")
      assert_parse_equals("(wildcard *a)", "*a")
      assert_parse_equals("(wildcard a*)", "a*")
      assert_parse_equals("(wildcard *a*)", "*a*")
      assert_parse_equals("(wildcard a*b)", "a*b")

      assert_parse_equals("(and b (wildcard *))", "* b")
      assert_parse_equals("(and b (wildcard *a))", "*a b")
      assert_parse_equals("(and b (wildcard a*))", "a* b")
      assert_parse_equals("(and b (wildcard *a*))", "*a* b")

      assert_parse_equals("(and a (wildcard *))", "a *")
      assert_parse_equals("(and a (wildcard *b))", "a *b")
      assert_parse_equals("(and a (wildcard b*))", "a b*")
      assert_parse_equals("(and a (wildcard *b*))", "a *b*")

      assert_parse_equals("(and (not (wildcard *)) a)", "a -*")
      assert_parse_equals("(and (not (wildcard b*)) a)", "a -b*")
      assert_parse_equals("(and (not (wildcard *b)) a)", "a -*b")
      assert_parse_equals("(and (not (wildcard *b*)) a)", "a -*b*")

      assert_parse_equals("(or a (wildcard *))", "~a ~*")
      assert_parse_equals("(or a (wildcard *))", "~* ~a")
      assert_parse_equals("(or a (wildcard *a))", "~a ~*a")
      assert_parse_equals("(or a (wildcard *a))", "~*a ~a")

      assert_parse_equals("(or a (wildcard a*))", "a or a*")
      assert_parse_equals("(and a (wildcard a*))", "a and a*")

      assert_parse_equals("(and (wildcard a*) (wildcard b*))", "a* b*")
      assert_parse_equals("(or (wildcard a*) (wildcard b*))", "a* or b*")

      assert_parse_equals("(and a c (wildcard b*))", "a b* c")
      assert_parse_equals("(and (not (wildcard *)) a c)", "a -* c")
    end

    def test_single_tag
      assert_parse_equals("a", "a")
      assert_parse_equals("a", "a ")
      assert_parse_equals("a", " a")
      assert_parse_equals("a", " a ")
      assert_parse_equals("a", "(a)")
      assert_parse_equals("a", "( a)")
      assert_parse_equals("a", "(a )")
      assert_parse_equals("a", " ( a ) ")
      assert_parse_equals("a", "((a))")
      assert_parse_equals("a", "( ( a ) )")
      assert_parse_equals("a", " ( ( a ) ) ")
    end

    def test_spaces
      assert_parse_equals("(and a b c d e)", "a\tb\nc\u0085d\u3000e")
      assert_parse_equals("(and (wildcard a*) (wildcard b*) (wildcard c*) (wildcard d*) (wildcard e*))", "a*\tb*\nc*\u0085d*\u3000e*")
      assert_parse_equals("(and a b c)", "a\tand\nb\u0085and\u3000c")
      assert_parse_equals("(or a b c)", "a\tor\nb\u0085or\u3000c")
      assert_parse_equals("(and source:a b)", "source:a\u3000b")

      ["skirt\u0009tail", "skirt\u000atail", "skirt\u000btail", "skirt\u000ctail", "skirt\u000dtail", "skirt\u0020tail",
       "skirt\u0085tail", "skirt\u00a0tail", "skirt\u1680tail", "skirt\u2000tail", "skirt\u2001tail", "skirt\u2002tail",
       "skirt\u2003tail", "skirt\u2004tail", "skirt\u2005tail", "skirt\u2006tail", "skirt\u2007tail", "skirt\u2008tail",
       "skirt\u2009tail", "skirt\u200atail", "skirt\u2028tail", "skirt\u2029tail", "skirt\u202ftail", "skirt\u205ftail",
       "skirt\u3000tail",].each do |search|
        assert_parse_equals("(and skirt tail)", search)
      end
    end

    def test_nested_and
      assert_parse_equals("(and a b)", "a b")
      assert_parse_equals("(and a b)", "(a b)")
      assert_parse_equals("(and a b)", "a (b)")
      assert_parse_equals("(and a b)", "(a) b")
      assert_parse_equals("(and a b)", "(a) (b)")
      assert_parse_equals("(and a b)", "((a) (b))")

      assert_parse_equals("(and a b c)", "a b c")
      assert_parse_equals("(and a b c)", "(a b) c")
      assert_parse_equals("(and a b c)", "((a) b) c")
      assert_parse_equals("(and a b c)", "(((a) b) c)")
      assert_parse_equals("(and a b c)", "((a b) c)")
      assert_parse_equals("(and a b c)", "((a) (b) (c))")

      assert_parse_equals("(and a b c)", "a (b c)")
      assert_parse_equals("(and a b c)", "a (b (c))")
      assert_parse_equals("(and a b c)", "(a (b (c)))")
      assert_parse_equals("(and a b c)", "(a (b c))")
      assert_parse_equals("(and a b c)", "(a b c)")

      assert_parse_equals("(and a b)", "a and b")
      assert_parse_equals("(and a b)", "a AND b")
      assert_parse_equals("(and a b)", "(a and b)")
      assert_parse_equals("(and a b c)", "a and b and c")
      assert_parse_equals("(and a b c)", "(a and b) and c")
      assert_parse_equals("(and a b c)", "a and (b and c)")
      assert_parse_equals("(and a b c)", "(a and b and c)")
    end

    def test_nested_or
      assert_parse_equals("(or a b)", "a or b")
      assert_parse_equals("(or a b)", "a OR b")
      assert_parse_equals("(or a b)", "(a or b)")
      assert_parse_equals("(or a b)", "(a) or (b)")

      assert_parse_equals("(or a b c)", "a or b or c")
      assert_parse_equals("(or a b c)", "(a or b) or c")
      assert_parse_equals("(or a b c)", "a or (b or c)")
      assert_parse_equals("(or a b c)", "(a or b or c)")

      assert_parse_equals("(or a b c d)", "a or (b or (c or d))")
      assert_parse_equals("(or a b c d)", "((a or b) or c) or d")
      assert_parse_equals("(or a b c d)", "(a or b) or (c or d)")
    end

    def test_opt
      assert_parse_equals("(or a b)", "~a ~b")
      assert_parse_equals("(or a b c)", "~a ~b ~c")
      assert_parse_equals("(or a b c d)", "~a ~b ~c ~d")

      assert_parse_equals("a", "~a")
      assert_parse_equals("a", "(~a)")
      assert_parse_equals("a", "~(a)")
      assert_parse_equals("a", "~(~a)")
      assert_parse_equals("a", "~(~(~a))")

      assert_parse_equals("(not a)", "~(-a)")
      assert_parse_equals("(not a)", "-(~a)")
      assert_parse_equals("a", "-(~(-(~a)))")
      assert_parse_equals("a", "~(-(~(-a)))")

      assert_parse_equals("(and a b)", "a ~b")
      assert_parse_equals("(and a b)", "~a b")
      assert_parse_equals("(and a b)", "((a) ~b)")
      assert_parse_equals("(and a b)", "~(a b)")

      assert_parse_equals("(and a b)", "~a and ~b")
      assert_parse_equals("(or a b)", "~a or ~b")
      assert_parse_equals("(or (not a) (not b))", "~(-a) or ~(-b)")

      assert_parse_equals("(or a b)", "~(a) ~(b)")
      assert_parse_equals("(and a b)", "(~a) (~b)")

      assert_parse_equals("(and (or b c) a)", "(~a) ~b ~c")
      assert_parse_equals("(and (or b c) a)", "~a (~b ~c)")

      assert_parse_equals("(or a b c d)", "~a ~b or ~c ~d")
      assert_parse_equals("(and (or a b) (or c d))", "~a ~b and ~c ~d")
      assert_parse_equals("(and (or a b) (or c d))", "(~a ~b) (~c ~d)")
      assert_parse_equals("(and (or a c) (or a d) (or b c) (or b d))", "~(a b) ~(c d)")
      assert_parse_equals("(and (or a c) (or a d) (or b c) (or b d))", "(a b) or (c d)")

      assert_parse_equals("(and a b c d)",      " a  b  c  d")
      assert_parse_equals("(and a b c d)",      " a  b  c ~d")
      assert_parse_equals("(and a b c d)",      " a  b ~c  d")
      assert_parse_equals("(and (or c d) a b)", " a  b ~c ~d")
      assert_parse_equals("(and a b c d)",      " a ~b  c  d")
      assert_parse_equals("(and (or b d) a c)", " a ~b  c ~d")
      assert_parse_equals("(and (or b c) a d)", " a ~b ~c  d")
      assert_parse_equals("(and (or b c d) a)", " a ~b ~c ~d")
      assert_parse_equals("(and a b c d)",      "~a  b  c  d")
      assert_parse_equals("(and (or a d) b c)", "~a  b  c ~d")
      assert_parse_equals("(and (or a c) b d)", "~a  b ~c  d")
      assert_parse_equals("(and (or a c d) b)", "~a  b ~c ~d")
      assert_parse_equals("(and (or a b) c d)", "~a ~b  c  d")
      assert_parse_equals("(and (or a b d) c)", "~a ~b  c ~d")
      assert_parse_equals("(and (or a b c) d)", "~a ~b ~c  d")
      assert_parse_equals("(or a b c d)",       "~a ~b ~c ~d")
    end

    def test_not
      assert_parse_equals("(not a)", "-a")

      assert_parse_equals("(and (not b) a)", "(a -b)")
      assert_parse_equals("(and (not b) a)", "a (-b)")
      assert_parse_equals("(and (not b) a)", "((a) -b)")
    end

    def test_double_neg
      assert_parse_equals("(not a)", "-a")
      assert_parse_equals("(not a)", "-(-(-a))")

      assert_parse_equals("a", "-(-a)")
      assert_parse_equals("a", "-(-(-(-a)))")

      assert_parse_equals("(and a b c)", "a -(-(b)) c")
      assert_parse_equals("(and a b c d)", "a -(-(b -(-c))) d")
    end

    def test_demorgan
      assert_parse_equals("(or (not a) (not b))", "-(a b)")
      assert_parse_equals("(and (not a) (not b))", "-(a or b)")

      assert_parse_equals("(or (not a) (not b) (not c))", "-(a b c)")
      assert_parse_equals("(and (not a) (not b) (not c))", "-(a or b or c)")

      assert_parse_equals("(or a b c)", "-(-a -b -c)")
      assert_parse_equals("(and a b c)", "-(-a or -b or -c)")

      assert_parse_equals("(and (or (not a) (not c) (not d)) (or (not a) b))", "-(a -(b -(c d)))")
    end

    def test_error
      assert_parse_equals("none", "(")
      assert_parse_equals("none", ")")
      assert_parse_equals("none", "-")
      assert_parse_equals("none", "~")

      assert_parse_equals("none", "(a")
      assert_parse_equals("none", ")a")
      assert_parse_equals("none", "-~a")
      assert_parse_equals("none", "~-a")
      assert_parse_equals("none", "~~a")
      assert_parse_equals("none", "--a")

      assert_parse_equals("none", "and")
      assert_parse_equals("none", "-and")
      assert_parse_equals("none", "~and")
      assert_parse_equals("none", "or")
      assert_parse_equals("none", "-or")
      assert_parse_equals("none", "~or")
      assert_parse_equals("none", "a and")
      assert_parse_equals("none", "a or")
      assert_parse_equals("none", "and a")
      assert_parse_equals("none", "or a")

      assert_parse_equals("none", "a -")
      assert_parse_equals("none", "a ~")

      assert_parse_equals("none", "(a b")
      assert_parse_equals("none", "(a (b)")

      assert_parse_equals("none", 'source:"foo')
      assert_parse_equals("none", 'source:"foo bar')
    end
  end
end
