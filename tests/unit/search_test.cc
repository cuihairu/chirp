#include <gtest/gtest.h>

#include <chrono>

#include <string>
#include <vector>

#include "message_search_service.h"

using chirp::search::IndexedDocument;
using chirp::search::MessageSearchService;
using chirp::search::SearchConfig;
using chirp::search::SearchQuery;
using chirp::search::SearchResponse;

namespace {

int64_t DaysAgoMs(int days) {
  return 1750000000000LL - static_cast<int64_t>(days) * 24 * 3600 * 1000;
}

IndexedDocument MakeDoc(const std::string& id, const std::string& content,
                        const std::string& sender = "u1",
                        const std::string& channel = "c1",
                        int64_t ts = 1750000000000LL) {
  IndexedDocument doc;
  doc.doc_id = id;
  doc.content = content;
  doc.sender_id = sender;
  doc.channel_id = channel;
  doc.timestamp = ts;
  return doc;
}

std::vector<std::string> ResultIds(const SearchResponse& resp) {
  std::vector<std::string> ids;
  for (const auto& r : resp.results) {
    ids.push_back(r.message_id);
  }
  return ids;
}

bool Contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

class MessageSearchServiceTest : public ::testing::Test {
protected:
  MessageSearchService svc_;
};

TEST_F(MessageSearchServiceTest, IndexAndSearchBasics) {
  ASSERT_TRUE(svc_.IndexDocument(MakeDoc("m1", "hello world")));
  ASSERT_TRUE(svc_.IndexDocument(MakeDoc("m2", "goodbye world")));
  ASSERT_TRUE(svc_.IndexDocument(MakeDoc("m3", "random text")));

  SearchQuery q;
  q.query = "hello";
  auto resp = svc_.Search(q);

  ASSERT_EQ(resp.results.size(), 1u);
  EXPECT_EQ(resp.results[0].message_id, "m1");
  EXPECT_EQ(resp.results[0].content, "hello world");
  EXPECT_GT(resp.results[0].score, 0.0);
  EXPECT_EQ(resp.total_count, 1);
  EXPECT_FALSE(resp.has_more);
  EXPECT_GE(resp.search_time_ms, 0.0);
}

TEST_F(MessageSearchServiceTest, SearchIsCaseInsensitive) {
  ASSERT_TRUE(svc_.IndexDocument(MakeDoc("m1", "Hello WORLD")));

  SearchQuery q;
  q.query = "hello world";
  auto resp = svc_.Search(q);
  ASSERT_EQ(resp.results.size(), 1u);
}

TEST_F(MessageSearchServiceTest, SearchMatchesMultipleDocsSortedByScore) {
  // m2 contains the exact phrase and both words -> highest score
  svc_.IndexDocument(MakeDoc("m1", "hello there", "u1", "c1", DaysAgoMs(30)));
  svc_.IndexDocument(MakeDoc("m2", "hello world again", "u1", "c1", DaysAgoMs(1)));
  svc_.IndexDocument(MakeDoc("m3", "world peace", "u1", "c1", DaysAgoMs(30)));

  SearchQuery q;
  q.query = "hello world";
  auto resp = svc_.Search(q);

  ASSERT_EQ(resp.results.size(), 3u);
  EXPECT_EQ(resp.results[0].message_id, "m2");
  EXPECT_GE(resp.results[0].score, resp.results[1].score);
}

TEST_F(MessageSearchServiceTest, SearchResultFillsNamesAndSnippet) {
  svc_.AddChannel("c1", "general");
  svc_.SetUsername("u1", "alice");
  svc_.IndexDocument(MakeDoc("m1", "the quick brown fox jumps over something"));

  SearchQuery q;
  q.query = "quick brown";
  auto resp = svc_.Search(q);

  ASSERT_EQ(resp.results.size(), 1u);
  const auto& r = resp.results[0];
  EXPECT_EQ(r.channel_name, "general");
  EXPECT_EQ(r.sender_username, "alice");
  EXPECT_EQ(r.sender_id, "u1");
  EXPECT_EQ(r.channel_id, "c1");
  EXPECT_FALSE(r.snippet.empty());
  EXPECT_FALSE(r.matches.empty());
}

TEST_F(MessageSearchServiceTest, ShortTokensBelowMinLengthAreSkipped) {
  // Default min_word_length = 2 -> single letters are not indexed
  svc_.IndexDocument(MakeDoc("m1", "a b c x"));

  SearchQuery q;
  q.query = "a";
  auto resp = svc_.Search(q);
  EXPECT_TRUE(resp.results.empty());
}

TEST_F(MessageSearchServiceTest, DefaultStopWordsAreFiltered) {
  svc_.IndexDocument(MakeDoc("m1", "the and"));

  SearchQuery q;
  q.query = "the";
  auto resp = svc_.Search(q);
  EXPECT_TRUE(resp.results.empty());
}

TEST_F(MessageSearchServiceTest, CustomStopWordsAreKept) {
  SearchConfig config;
  config.stop_words = {"foo"};  // non-empty: defaults not applied
  MessageSearchService svc(config);

  svc.IndexDocument(MakeDoc("m1", "the foo bar"));

  SearchQuery q1;
  q1.query = "the";  // no longer a stop word with custom list
  EXPECT_EQ(svc.Search(q1).results.size(), 1u);

  SearchQuery q2;
  q2.query = "foo";  // custom stop word filtered
  EXPECT_TRUE(svc.Search(q2).results.empty());
}

TEST_F(MessageSearchServiceTest, SpecialCharactersArePartOfTokens) {
  svc_.IndexDocument(MakeDoc("m1", "ping @alice and #general"));

  SearchQuery q;
  q.query = "@alice";
  auto resp = svc_.Search(q);
  EXPECT_EQ(resp.results.size(), 1u);

  SearchQuery q2;
  q2.query = "#general";
  EXPECT_EQ(svc_.Search(q2).results.size(), 1u);
}

TEST_F(MessageSearchServiceTest, UnderscoreTokensIndexQueryAndDelete) {
  // Underscores flow through the index tokenizer, the query tokenizer, the
  // normalizer, and the removal pass on delete.
  svc_.IndexDocument(MakeDoc("m1", "user_id login @team_chat #ops_room here"));

  SearchQuery by_underscore;
  by_underscore.query = "user_id";
  EXPECT_EQ(svc_.Search(by_underscore).results.size(), 1u);

  SearchQuery by_hash;
  by_hash.query = "#ops_room";
  EXPECT_EQ(svc_.Search(by_hash).results.size(), 1u);

  SearchQuery by_at;
  by_at.query = "@team_chat";
  EXPECT_EQ(svc_.Search(by_at).results.size(), 1u);

  ASSERT_TRUE(svc_.DeleteDocument("m1"));
  EXPECT_TRUE(svc_.Search(by_underscore).results.empty());
}

TEST_F(MessageSearchServiceTest, NonContiguousQueryTruncatesSnippetWithoutMatches) {
  // The two tokens are indexed independently, but the full query string is
  // not a substring of the content, so the match list stays empty and the
  // snippet falls back to the truncation path.
  const std::string content =
      "aaaa " + std::string(280, 'x') + " cccc";
  svc_.IndexDocument(MakeDoc("m1", content));

  SearchQuery q;
  q.query = "aaaa cccc";
  auto resp = svc_.Search(q);
  ASSERT_EQ(resp.results.size(), 1u);
  EXPECT_TRUE(resp.results[0].matches.empty());
  ASSERT_GE(resp.results[0].snippet.size(), 3u);
  EXPECT_EQ(resp.results[0].snippet.substr(resp.results[0].snippet.size() - 3),
            "...");
  EXPECT_LT(resp.results[0].snippet.size(), content.size());  // truncated
}

TEST_F(MessageSearchServiceTest, GetUsernameHandlesEmptyAndLongValues) {
  svc_.SetUsername("empty_user", "");
  EXPECT_EQ(svc_.GetUsername("empty_user"), "");

  const std::string long_name(300, 'z');
  svc_.SetUsername("long_user", long_name);
  EXPECT_EQ(svc_.GetUsername("long_user"), long_name);

  EXPECT_EQ(svc_.GetUsername("missing_user"), "");
}

TEST_F(MessageSearchServiceTest, SuggestionsSkipNonTokenCharacters) {
  svc_.SetUsername("u1", "helpdesk");

  // Hyphen is not a token character: NormalizeWord drops it via the final
  // reject arm, so "hel-p" still prefixes the username "helpdesk".
  auto sugg = svc_.GetSuggestions("hel-p", 10);
  ASSERT_EQ(sugg.size(), 1u);
  EXPECT_EQ(sugg[0].text, "helpdesk");

  // Separators-only prefix normalizes to the empty string, which matches
  // every registered source; '.', ' ' and '-' each exercise the reject arm.
  auto all = svc_.GetSuggestions(". -", 10);
  EXPECT_EQ(all.size(), 1u);
}

TEST_F(MessageSearchServiceTest, FilterByChannelId) {
  svc_.IndexDocument(MakeDoc("m1", "hello", "u1", "c1"));
  svc_.IndexDocument(MakeDoc("m2", "hello", "u1", "c2"));

  SearchQuery q;
  q.query = "hello";
  q.channel_id = "c2";
  auto resp = svc_.Search(q);
  ASSERT_EQ(resp.results.size(), 1u);
  EXPECT_EQ(resp.results[0].message_id, "m2");
}

TEST_F(MessageSearchServiceTest, FilterByMultipleChannels) {
  svc_.IndexDocument(MakeDoc("m1", "hello", "u1", "c1"));
  svc_.IndexDocument(MakeDoc("m2", "hello", "u1", "c2"));
  svc_.IndexDocument(MakeDoc("m3", "hello", "u1", "c3"));

  SearchQuery q;
  q.query = "hello";
  q.channel_ids = {"c1", "c3"};
  auto resp = svc_.Search(q);

  auto ids = ResultIds(resp);
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_TRUE(Contains(ids, "m1"));
  EXPECT_TRUE(Contains(ids, "m3"));
}

TEST_F(MessageSearchServiceTest, FilterBySenderId) {
  svc_.IndexDocument(MakeDoc("m1", "hello", "alice"));
  svc_.IndexDocument(MakeDoc("m2", "hello", "bob"));

  SearchQuery q;
  q.query = "hello";
  q.sender_id = "bob";
  auto resp = svc_.Search(q);
  ASSERT_EQ(resp.results.size(), 1u);
  EXPECT_EQ(resp.results[0].message_id, "m2");
}

TEST_F(MessageSearchServiceTest, FilterByMentionedUser) {
  auto doc = MakeDoc("m1", "hello there");
  doc.mentioned_user_ids = {"carol"};
  svc_.IndexDocument(doc);
  svc_.IndexDocument(MakeDoc("m2", "hello again"));

  SearchQuery q;
  q.query = "hello";
  q.mentioned_user_id = "carol";
  auto resp = svc_.Search(q);
  ASSERT_EQ(resp.results.size(), 1u);
  EXPECT_EQ(resp.results[0].message_id, "m1");
}

TEST_F(MessageSearchServiceTest, FilterByTimeRange) {
  svc_.IndexDocument(MakeDoc("m1", "hello", "u1", "c1", 1000));
  svc_.IndexDocument(MakeDoc("m2", "hello", "u1", "c1", 2000));
  svc_.IndexDocument(MakeDoc("m3", "hello", "u1", "c1", 3000));

  SearchQuery before;
  before.query = "hello";
  before.before_timestamp = 2500;  // strictly before
  auto ids = ResultIds(svc_.Search(before));
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_TRUE(Contains(ids, "m1"));
  EXPECT_TRUE(Contains(ids, "m2"));

  SearchQuery after;
  after.query = "hello";
  after.after_timestamp = 1500;  // strictly after
  ids = ResultIds(svc_.Search(after));
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_TRUE(Contains(ids, "m2"));
  EXPECT_TRUE(Contains(ids, "m3"));

  SearchQuery range;
  range.query = "hello";
  range.before_timestamp = 2500;
  range.after_timestamp = 1500;
  ids = ResultIds(svc_.Search(range));
  ASSERT_EQ(ids.size(), 1u);
  EXPECT_EQ(ids[0], "m2");
}

TEST_F(MessageSearchServiceTest, FilterByMsgType) {
  auto file_doc = MakeDoc("m1", "hello");
  file_doc.msg_type = chirp::chat::IMAGE;
  svc_.IndexDocument(file_doc);
  svc_.IndexDocument(MakeDoc("m2", "hello"));  // TEXT

  // Default msg_type = TEXT: no type filtering, both match
  SearchQuery all;
  all.query = "hello";
  EXPECT_EQ(svc_.Search(all).results.size(), 2u);

  // Explicit FILE filter keeps only file messages
  SearchQuery files;
  files.query = "hello";
  files.msg_type = chirp::chat::IMAGE;
  auto resp = svc_.Search(files);
  ASSERT_EQ(resp.results.size(), 1u);
  EXPECT_EQ(resp.results[0].message_id, "m1");
}

TEST_F(MessageSearchServiceTest, FilterByAttachments) {
  auto with_file = MakeDoc("m1", "hello");
  with_file.has_attachments = true;
  svc_.IndexDocument(with_file);
  svc_.IndexDocument(MakeDoc("m2", "hello"));

  SearchQuery q;
  q.query = "hello";
  q.has_attachments = true;
  auto resp = svc_.Search(q);
  ASSERT_EQ(resp.results.size(), 1u);
  EXPECT_EQ(resp.results[0].message_id, "m1");
}

TEST_F(MessageSearchServiceTest, Pagination) {
  for (int i = 1; i <= 5; ++i) {
    svc_.IndexDocument(MakeDoc("m" + std::to_string(i), "hello world", "u1", "c1", 1000 + i));
  }

  SearchQuery page1;
  page1.query = "hello";
  page1.limit = 2;
  page1.offset = 0;
  auto r1 = svc_.Search(page1);
  EXPECT_EQ(r1.results.size(), 2u);
  EXPECT_EQ(r1.total_count, 5);
  EXPECT_TRUE(r1.has_more);

  SearchQuery page3;
  page3.query = "hello";
  page3.limit = 2;
  page3.offset = 4;
  auto r3 = svc_.Search(page3);
  EXPECT_EQ(r3.results.size(), 1u);
  EXPECT_FALSE(r3.has_more);

  SearchQuery past;
  past.query = "hello";
  past.limit = 2;
  past.offset = 10;  // beyond the end
  auto rp = svc_.Search(past);
  EXPECT_TRUE(rp.results.empty());
  EXPECT_EQ(rp.total_count, 5);
  EXPECT_FALSE(rp.has_more);
}

TEST_F(MessageSearchServiceTest, UpdateDocumentReindexesContent) {
  svc_.IndexDocument(MakeDoc("m1", "old content"));

  SearchQuery old_q;
  old_q.query = "old";
  EXPECT_EQ(svc_.Search(old_q).results.size(), 1u);

  ASSERT_TRUE(svc_.UpdateDocument("m1", MakeDoc("m1", "new content")));

  EXPECT_TRUE(svc_.Search(old_q).results.empty());

  SearchQuery new_q;
  new_q.query = "new";
  auto resp = svc_.Search(new_q);
  ASSERT_EQ(resp.results.size(), 1u);
  EXPECT_EQ(resp.results[0].content, "new content");
  EXPECT_EQ(svc_.GetDocumentCount(), 1u);  // no duplicate
}

TEST_F(MessageSearchServiceTest, DeleteDocument) {
  svc_.IndexDocument(MakeDoc("m1", "hello"));

  ASSERT_TRUE(svc_.DeleteDocument("m1"));
  EXPECT_EQ(svc_.GetDocumentCount(), 0u);
  EXPECT_FALSE(svc_.DeleteDocument("m1"));  // already gone
  EXPECT_FALSE(svc_.DeleteDocument("missing"));

  SearchQuery q;
  q.query = "hello";
  EXPECT_TRUE(svc_.Search(q).results.empty());
}

TEST_F(MessageSearchServiceTest, EmptyAndNoMatchQueries) {
  svc_.IndexDocument(MakeDoc("m1", "hello"));

  SearchQuery q;
  q.query = "";
  auto resp = svc_.Search(q);
  EXPECT_TRUE(resp.results.empty());
  EXPECT_EQ(resp.total_count, 0);

  SearchQuery q2;
  q2.query = "zebra";
  EXPECT_TRUE(svc_.Search(q2).results.empty());
}

TEST_F(MessageSearchServiceTest, ChannelManagement) {
  EXPECT_TRUE(svc_.AddChannel("c1", "general"));
  EXPECT_TRUE(svc_.AddChannel("c2", "random"));
  EXPECT_EQ(svc_.GetDocumentCount(), 0u);

  svc_.SetChannelName("c2", "random-updated");

  EXPECT_TRUE(svc_.RemoveChannel("c2"));
  EXPECT_FALSE(svc_.RemoveChannel("c2"));  // already removed
  EXPECT_FALSE(svc_.RemoveChannel("missing"));
}

TEST_F(MessageSearchServiceTest, UsernameManagement) {
  svc_.SetUsername("u1", "alice");
  EXPECT_EQ(svc_.GetUsername("u1"), "alice");
  EXPECT_EQ(svc_.GetUsername("missing"), "");
}

TEST_F(MessageSearchServiceTest, SuggestionsFromQueriesUsersAndChannels) {
  // Register suggestions sources
  svc_.SetUsername("u1", "alice");
  svc_.AddChannel("c1", "general");

  SearchQuery q;
  q.query = "alice";
  svc_.Search(q);
  svc_.Search(q);  // frequency 2

  auto sugg = svc_.GetSuggestions("al", 10);
  ASSERT_EQ(sugg.size(), 2u);

  // Frequency-sorted: query suggestion ("alice", freq 2) first
  EXPECT_EQ(sugg[0].text, "alice");
  EXPECT_EQ(sugg[0].type, "query");
  EXPECT_EQ(sugg[0].frequency, 2);

  EXPECT_EQ(sugg[1].text, "alice");
  EXPECT_EQ(sugg[1].type, "user");
}

TEST_F(MessageSearchServiceTest, SuggestionsChannelType) {
  svc_.AddChannel("c1", "general");
  svc_.SetUsername("u1", "bob");

  auto sugg = svc_.GetSuggestions("gen", 10);
  ASSERT_EQ(sugg.size(), 1u);
  EXPECT_EQ(sugg[0].text, "general");
  EXPECT_EQ(sugg[0].type, "channel");

  EXPECT_TRUE(svc_.GetSuggestions("zzz", 10).empty());
}

TEST_F(MessageSearchServiceTest, SuggestionsRespectLimit) {
  for (int i = 0; i < 5; ++i) {
    svc_.SetUsername("u" + std::to_string(i), "user" + std::to_string(i));
  }
  auto sugg = svc_.GetSuggestions("user", 3);
  EXPECT_EQ(sugg.size(), 3u);
}

TEST_F(MessageSearchServiceTest, DocumentAndIndexStats) {
  EXPECT_EQ(svc_.GetDocumentCount(), 0u);
  EXPECT_EQ(svc_.GetIndexSizeBytes(), 0u);

  svc_.IndexDocument(MakeDoc("m1", "hello world", "u1", "c1", 1000));
  EXPECT_EQ(svc_.GetDocumentCount(), 1u);
  EXPECT_GT(svc_.GetIndexSizeBytes(), 0u);

  svc_.Reset();
  EXPECT_EQ(svc_.GetDocumentCount(), 0u);
  EXPECT_EQ(svc_.GetIndexSizeBytes(), 0u);
}

TEST_F(MessageSearchServiceTest, ResetClearsSearchHistoryForSuggestions) {
  SearchQuery q;
  q.query = "hello";
  svc_.Search(q);

  svc_.Reset();
  EXPECT_TRUE(svc_.GetSuggestions("hel", 10).empty());
}

TEST_F(MessageSearchServiceTest, RankBoostsFromRepliesAndReactions) {
  auto popular = MakeDoc("m1", "hello world", "u1", "c1", DaysAgoMs(30));
  popular.reply_count = 10;
  popular.reaction_count = 5;
  svc_.IndexDocument(popular);

  auto plain = MakeDoc("m2", "hello world", "u1", "c1", DaysAgoMs(30));
  svc_.IndexDocument(plain);

  SearchQuery q;
  q.query = "hello world";
  auto resp = svc_.Search(q);

  ASSERT_EQ(resp.results.size(), 2u);
  EXPECT_EQ(resp.results[0].message_id, "m1");  // engagement boosts rank
  EXPECT_GT(resp.results[0].score, resp.results[1].score);
}

TEST_F(MessageSearchServiceTest, ConcurrentIndexAndSearchAreSafe) {
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < 10; ++i) {
        svc_.IndexDocument(MakeDoc("m" + std::to_string(t) + "_" + std::to_string(i),
                                   "hello world thread", "u1", "c1", 1000));
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }

  SearchQuery q;
  q.query = "hello";
  EXPECT_EQ(svc_.Search(q).total_count, 40);
  EXPECT_EQ(svc_.GetDocumentCount(), 40u);
}

TEST_F(MessageSearchServiceTest, ReindexingSameIdCleansPreviousTokens) {
  // The first content contains a stop word ("the") that is filtered on
  // indexing but reappears during removal; the stale-token path must be hit
  // without corrupting the index.
  IndexedDocument d1 = MakeDoc("d1", "the hello world");
  EXPECT_TRUE(svc_.IndexDocument(d1));
  IndexedDocument d2 = MakeDoc("d1", "the goodbye world");
  EXPECT_TRUE(svc_.IndexDocument(d2));

  SearchQuery q;
  q.query = "hello";
  EXPECT_EQ(svc_.Search(q).total_count, 0);
  q.query = "goodbye";
  EXPECT_EQ(svc_.Search(q).total_count, 1);
}

TEST_F(MessageSearchServiceTest, RecencyBoostPrefersRecentDocuments) {
  int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) * 1000LL;
  EXPECT_TRUE(svc_.IndexDocument(MakeDoc("fresh", "alpha report", "u1", "c1", now)));
  EXPECT_TRUE(svc_.IndexDocument(MakeDoc("week", "alpha report", "u1", "c1", now - 3LL * 24 * 3600 * 1000)));
  EXPECT_TRUE(svc_.IndexDocument(MakeDoc("old", "alpha report", "u1", "c1", now - 90LL * 24 * 3600 * 1000)));

  SearchQuery q;
  q.query = "alpha report";
  auto ids = ResultIds(svc_.Search(q));
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "fresh");
  EXPECT_EQ(ids[1], "week");
  EXPECT_EQ(ids[2], "old");
}

TEST_F(MessageSearchServiceTest, SnippetAddsEllipsesAndTruncates) {
  std::string filler_a(120, 'x');
  std::string filler_b(120, 'y');
  std::string content = filler_a + " target " + filler_b;
  EXPECT_TRUE(svc_.IndexDocument(MakeDoc("long", content)));

  SearchQuery q;
  q.query = "target";
  auto resp = svc_.Search(q);
  ASSERT_EQ(resp.results.size(), 1u);
  const std::string& snip = resp.results[0].snippet;
  // Match sits in the middle: both ends are trimmed with ellipses and the
  // final snippet respects the configured maximum length.
  EXPECT_TRUE(snip.size() > 0u);
  EXPECT_LE(snip.size(), 200u + 3u);

  // A small snippet budget forces the truncation branch.
  SearchConfig config;
  config.max_snippet_length = 20;
  MessageSearchService small_svc{config};
  EXPECT_TRUE(small_svc.IndexDocument(MakeDoc("long", content)));
  auto short_resp = small_svc.Search(q);
  ASSERT_EQ(short_resp.results.size(), 1u);
  const std::string& short_snip = short_resp.results[0].snippet;
  EXPECT_LE(short_snip.size(), 20u + 3u);
  EXPECT_NE(short_snip.find("..."), std::string::npos);
}

TEST_F(MessageSearchServiceTest, SuggestionsMixQueriesUsersAndChannels) {
  SearchQuery q;
  q.query = "hello";
  (void)svc_.Search(q); // seeds query frequency

  svc_.SetUsername("u1", "helen");
  ASSERT_TRUE(svc_.AddChannel("c1", "helpdesk"));

  auto sugg = svc_.GetSuggestions("he");
  ASSERT_FALSE(sugg.empty());
  bool has_query = false, has_user = false, has_channel = false;
  for (const auto& s : sugg) {
    if (s.type == "query" && s.text == "hello") has_query = true;
    if (s.type == "user" && s.text == "helen") has_user = true;
    if (s.type == "channel" && s.text == "helpdesk") has_channel = true;
  }
  EXPECT_TRUE(has_query);
  EXPECT_TRUE(has_user);
  EXPECT_TRUE(has_channel);

  // No match yields an empty list.
  EXPECT_TRUE(svc_.GetSuggestions("zz").empty());
}

}  // namespace
