#ifndef CHIRP_NPC_DIALOG_NPC_ENGINE_H_
#define CHIRP_NPC_DIALOG_NPC_ENGINE_H_

#include <istream>
#include <string>
#include <vector>

namespace chirp::npc {

// One keyword rule: when the player's text contains `keyword` (ASCII
// case-insensitive substring), `npc_id` answers `reply`. A keyword of `*`
// marks the NPC's fallback reply for when no keyword rule matches.
struct NpcRuleEntry {
  std::string npc_id;
  std::string keyword;
  std::string reply;
};

// Dialog brain: turns a player utterance into the NPC's reply. Implemented
// with a keyword rule table; the interface is the seam for a smarter engine
// (e.g. an LLM service) later.
class NpcEngine {
 public:
  virtual ~NpcEngine() = default;

  // The reply a player gets for `text` addressed to `npc_id`.
  virtual std::string Reply(const std::string& npc_id,
                            const std::string& text) = 0;
};

// ASCII lowercasing shared by the rule matcher.
std::string AsciiLower(std::string text);

// Keyword-table engine. Rules load from TSV text: one `npc_id\tkeyword\treply`
// per line; `#`-prefixed lines, blank lines, and lines that do not split into
// exactly three tab-separated fields are skipped. Loading replaces the
// previous rule set. Matching is ASCII case-insensitive substring,
// first-match-wins in file order; a miss falls back to the NPC's `*` rule,
// and an NPC with no rules at all to the engine-wide fallback reply.
class RuleBasedNpcEngine : public NpcEngine {
 public:
  RuleBasedNpcEngine() = default;
  explicit RuleBasedNpcEngine(std::vector<NpcRuleEntry> rules);

  // Replaces the rule set with the text's rules.
  bool LoadStream(std::istream& in);
  // Same, reading from `path`; returns false (leaving the current rules in
  // place) when the file cannot be opened.
  bool LoadFile(const std::string& path);

  // Reply used for NPCs that have no rules at all.
  void set_fallback_reply(const std::string& reply) { fallback_reply_ = reply; }
  const std::string& fallback_reply() const { return fallback_reply_; }

  size_t rule_count() const { return rules_.size(); }

  std::string Reply(const std::string& npc_id,
                    const std::string& text) override;

 private:
  std::vector<NpcRuleEntry> rules_;
  std::string fallback_reply_;
};

// Built-in starter rules so a bare `chirp_npc_dialog` (no --rules_file)
// still demonstrates the loop: blacksmith_01 and healer_01.
std::vector<NpcRuleEntry> DemoRules();

}  // namespace chirp::npc

#endif  // CHIRP_NPC_DIALOG_NPC_ENGINE_H_
