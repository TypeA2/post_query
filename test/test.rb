# frozen_string_literal: true

METATAGS = [
  "user", "approver", "commenter", "comm", "noter", "noteupdater", "artcomm",
  "commentaryupdater", "flagger", "appealer", "upvote", "downvote", "fav",
  "ordfav", "favgroup", "ordfavgroup", "pool", "ordpool", "note", "comment",
  "commentary", "id", "rating", "source", "status", "filetype", "disapproved",
  "parent", "child", "search", "embedded", "md5", "pixelhash", "width",
  "height", "mpixels", "ratio", "views", "score", "upvotes", "downvotes",
  "favcount", "filesize", "date", "age", "order", "limit", "tagcount",
  "pixiv_id", "pixiv", "unaliased", "exif", "duration", "random", "is", "has",
  "ai", "updater", "metadataupdater", "modelhash", "comment_count",
  "deleted_comment_count", "active_comment_count", "note_count",
  "deleted_note_count", "active_note_count", "flag_count", "child_count",
  "deleted_child_count", "active_child_count", "pool_count",
  "deleted_pool_count", "active_pool_count", "series_pool_count",
  "collection_pool_count", "appeal_count", "approval_count",
  "replacement_count", "comments", "deleted_comments", "active_comments",
  "notes", "deleted_notes", "active_notes", "flags", "children",
  "deleted_children", "active_children", "pools", "deleted_pools",
  "active_pools", "series_pools", "collection_pools", "appeals", "approvals",
  "replacements", "arttags", "copytags", "chartags", "gentags", "metatags"
]

require "post_query"

# puts PostQuery.parse("hi")
puts PostQuery.parse("zß水🍌", metatags: [ "hi_there", "zß水🍌" ])
puts PostQuery.parse("hi", metatags: METATAGS)
