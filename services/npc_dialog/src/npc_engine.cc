#include "npc_engine.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace chirp::npc {

namespace {

constexpr char kDefaultKeyword[] = "*";

std::vector<std::string> SplitTab(const std::string& line) {
  std::vector<std::string> parts;
  std::string::size_type start = 0;
  for (;;) {
    const auto tab = line.find('\t', start);
    if (tab == std::string::npos) {
      parts.push_back(line.substr(start));
      return parts;
    }
    parts.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
}

}  // namespace

std::string AsciiLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
  });
  return text;
}

RuleBasedNpcEngine::RuleBasedNpcEngine(std::vector<NpcRuleEntry> rules)
    : rules_(std::move(rules)) {}

bool RuleBasedNpcEngine::LoadStream(std::istream& in) {
  std::vector<NpcRuleEntry> loaded;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    auto parts = SplitTab(line);
    if (parts.size() != 3) {
      continue;  // malformed line: skip rather than fail the whole file
    }
    loaded.push_back({std::move(parts[0]), std::move(parts[1]), std::move(parts[2])});
  }
  rules_ = std::move(loaded);
  return true;
}

bool RuleBasedNpcEngine::LoadFile(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return false;
  }
  return LoadStream(in);
}

std::string RuleBasedNpcEngine::Reply(const std::string& npc_id,
                                      const std::string& text) {
  const std::string lowered = AsciiLower(text);
  const std::string* fallback = nullptr;
  for (const auto& rule : rules_) {
    if (rule.npc_id != npc_id) {
      continue;
    }
    if (rule.keyword == kDefaultKeyword) {
      fallback = &rule.reply;
      continue;  // keyword rules win over the default regardless of order
    }
    if (lowered.find(AsciiLower(rule.keyword)) != std::string::npos) {
      return rule.reply;  // first keyword match in file order
    }
  }
  if (fallback) {
    return *fallback;
  }
  return fallback_reply_;
}

std::vector<NpcRuleEntry> DemoRules() {
  return {
      {"blacksmith_01", "sword",
       "A fine blade? Bring me two iron ore and it is yours."},
      {"blacksmith_01", "quest",
       "Rats in my cellar! Drive them out and I will forge you a sword."},
      {"blacksmith_01", "*",
       "Welcome to the forge, traveler. Looking for a sword, or work?"},
      {"healer_01", "heal",
       "Rest a moment... there. That will be ten gold."},
      {"healer_01", "*",
       "May the light mend you. Speak, child."},
  };
}

}  // namespace chirp::npc
