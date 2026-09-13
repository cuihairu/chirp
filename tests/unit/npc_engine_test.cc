// Rule engine tests: TSV parsing, matching order, case handling, and the
// fallback ladder (keyword rule -> npc `*` rule -> engine-wide fallback).
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "npc_engine.h"

namespace {

using chirp::npc::AsciiLower;
using chirp::npc::NpcRuleEntry;
using chirp::npc::RuleBasedNpcEngine;

TEST(NpcEngineTest, AsciiLowerLeavesNonAsciiAlone) {
  EXPECT_EQ(AsciiLower("HeLLo"), "hello");
  EXPECT_EQ(AsciiLower(""), "");
  // Bytes outside A-Z pass through unchanged (not locale-dependent).
  EXPECT_EQ(AsciiLower("\xc3\x84"), "\xc3\x84");
}

TEST(NpcEngineTest, KeywordSubstringMatchIsCaseInsensitive) {
  RuleBasedNpcEngine engine({{"npc1", "sword", "a blade"}});
  EXPECT_EQ(engine.Reply("npc1", "I need a SWORD"), "a blade");
  EXPECT_EQ(engine.Reply("npc1", "swords are nice"), "a blade");
  EXPECT_EQ(engine.Reply("npc1", "hello"), "");
}

TEST(NpcEngineTest, FirstKeywordMatchWinsInRuleOrder) {
  RuleBasedNpcEngine engine(
      {{"npc1", "sword", "first"}, {"npc1", "blade", "second"}});
  EXPECT_EQ(engine.Reply("npc1", "a sword or a blade"), "first");
  EXPECT_EQ(engine.Reply("npc1", "just a blade"), "second");
}

TEST(NpcEngineTest, DefaultRuleCatchesWhenNoKeywordMatches) {
  RuleBasedNpcEngine engine(
      {{"npc1", "sword", "keyword hit"}, {"npc1", "*", "default line"}});
  EXPECT_EQ(engine.Reply("npc1", "hello there"), "default line");
  // The keyword rule still beats the default even if it comes later.
  EXPECT_EQ(engine.Reply("npc1", "my sword"), "keyword hit");
}

TEST(NpcEngineTest, UnknownNpcFallsBackToEngineReply) {
  RuleBasedNpcEngine engine({{"npc1", "*", "npc1 default"}});
  engine.set_fallback_reply("who?");
  EXPECT_EQ(engine.Reply("npc2", "anything"), "who?");
  EXPECT_EQ(engine.Reply("npc1", "hi"), "npc1 default");
}

TEST(NpcEngineTest, LoadStreamSkipsCommentsBlanksAndBadLines) {
  RuleBasedNpcEngine engine;
  std::istringstream in(
      "# a comment line\n"
      "\n"
      "npc1\tsword\ta blade\n"
      "only two\tfields\n"
      "four\tfields\tthat is\ttoo many\n"
      "npc1\t*\tfallback\n");
  ASSERT_TRUE(engine.LoadStream(in));
  EXPECT_EQ(engine.rule_count(), 2u);
  EXPECT_EQ(engine.Reply("npc1", "SWORD please"), "a blade");
  EXPECT_EQ(engine.Reply("npc1", "hello"), "fallback");
}

TEST(NpcEngineTest, LoadStreamReplacesPreviousRules) {
  RuleBasedNpcEngine engine({{"npc1", "old", "old reply"}});
  std::istringstream in("npc1\tnew\tnew reply\n");
  ASSERT_TRUE(engine.LoadStream(in));
  EXPECT_EQ(engine.rule_count(), 1u);
  EXPECT_EQ(engine.Reply("npc1", "old"), "");
  EXPECT_EQ(engine.Reply("npc1", "new"), "new reply");
}

TEST(NpcEngineTest, LoadFileMissingFileLeavesRulesIntact) {
  RuleBasedNpcEngine engine({{"npc1", "keep", "kept"}});
  EXPECT_FALSE(engine.LoadFile("/nonexistent/path/npc_rules.tsv"));
  EXPECT_EQ(engine.Reply("npc1", "keep it"), "kept");
}

TEST(NpcEngineTest, LoadFileReadsRules) {
  const auto path = [] {
    static const char* kPaths[] = {"/tmp", "/data"};
    for (auto* base : kPaths) {
      const std::string p = std::string(base) + "/chirp_npc_engine_test.tsv";
      std::ofstream out(p);
      if (out.is_open()) {
        out << "npc1\theal\tmended\n";
        return p;
      }
    }
    return std::string();
  }();
  ASSERT_FALSE(path.empty());

  RuleBasedNpcEngine engine;
  ASSERT_TRUE(engine.LoadFile(path));
  EXPECT_EQ(engine.Reply("npc1", "HEAL me"), "mended");
  std::remove(path.c_str());
}

TEST(NpcEngineTest, DemoRulesAnswerBlacksmithAndHealer) {
  RuleBasedNpcEngine engine(chirp::npc::DemoRules());
  EXPECT_EQ(engine.Reply("blacksmith_01", "any quests?"),
            "Rats in my cellar! Drive them out and I will forge you a sword.");
  EXPECT_EQ(engine.Reply("blacksmith_01", "goodbye"),
            "Welcome to the forge, traveler. Looking for a sword, or work?");
  EXPECT_EQ(engine.Reply("healer_01", "please heal me"),
            "Rest a moment... there. That will be ten gold.");
  EXPECT_EQ(engine.Reply("stranger_npc", "hello"), "");
}

}  // namespace
