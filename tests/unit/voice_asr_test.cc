// Unit tests for the voice ASR moderation chain (VOICE-ASR-FILTER):
// mock ASR transcript -> shared chat word filter -> disposition ->
// room member mute. Room state is driven in-memory like voice_tests, so
// the full chain is pinned without sockets or an ASR runtime.

#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "asr/asr_filter.h"
#include "asr/mock_asr_provider.h"
#include "proto/gateway.pb.h"
#include "proto/voice.pb.h"

namespace {

using chirp::voice::asr::AsrDisposition;
using chirp::voice::asr::AsrFilter;
using chirp::voice::asr::AsrFilterOptions;
using chirp::voice::asr::AsrFilterResult;
using chirp::voice::asr::MockAsrProvider;

// Pure in-memory Session mock: records everything sent through it
// (same shape as voice_service_test.cc's MockSession).
class MockSession : public chirp::network::Session {
 public:
  void Send(std::string bytes) override { sent.push_back(std::move(bytes)); }
  void SendAndClose(std::string bytes) override {
    sent.push_back(std::move(bytes));
    close_after_send = true;
  }
  void Close() override { closed = true; }
  bool IsClosed() const override { return closed; }
  std::string RemoteAddress() const override { return "127.0.0.1"; }

  std::vector<std::string> sent;
  bool closed = false;
  bool close_after_send = false;
};

// A lexicon file that removes itself when the test ends (same recipe as
// word_filter_test.cc's TempLexicon, minus the hot-reload plumbing the
// chain tests don't drive).
class TempLexicon {
 public:
  TempLexicon() {
    path_ = (std::filesystem::temp_directory_path() /
             ("chirp_voice_asr_" + std::to_string(::getpid()) + "_" +
              std::to_string(reinterpret_cast<long>(this)) + ".txt"))
                .string();
  }
  ~TempLexicon() { std::remove(path_.c_str()); }

  TempLexicon(const TempLexicon&) = delete;
  TempLexicon& operator=(const TempLexicon&) = delete;

  const std::string& path() const { return path_; }

  void Write(const std::string& content) const {
    FILE* f = std::fopen(path_.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fputs(content.c_str(), f);
    std::fclose(f);
  }

 private:
  std::string path_;
};

class VoiceAsrTest : public ::testing::Test {
 protected:
  void SetUp() override {
    lexicon_.Write("badword\n");
    AsrFilterOptions options;
    options.lexicon_path = lexicon_.path();
    options.reload_check_interval_ms = 0;
    filter_ = std::make_unique<AsrFilter>(options,
                                          std::make_unique<MockAsrProvider>("you are a badword"));
    state_ = std::make_shared<chirp::voice::direct::VoiceState>();
    room_ = std::make_shared<chirp::voice::direct::VoiceRoom>();
    room_->room_id = "room-1";
    state_->rooms["room-1"] = room_;
  }

  // Adds user_id to the test room and binds a live mock session, mirroring
  // the state JoinRoom leaves behind.
  std::shared_ptr<MockSession> AddSpeaker(const std::string& user_id, bool muted = false) {
    chirp::voice::ParticipantInfo p;
    p.set_user_id(user_id);
    p.set_state(muted ? chirp::voice::MUTED : chirp::voice::CONNECTED);
    p.set_muted(muted);
    room_->participants[user_id] = p;
    auto session = std::make_shared<MockSession>();
    state_->user_to_session[user_id] = session;
    state_->user_to_room[user_id] = "room-1";
    return session;
  }

  TempLexicon lexicon_;
  std::unique_ptr<AsrFilter> filter_;
  std::shared_ptr<chirp::voice::direct::VoiceState> state_;
  std::shared_ptr<chirp::voice::direct::VoiceRoom> room_;
};

TEST(MockAsrProviderTest, EmptyPcmYieldsNoSpeechAndDataYieldsThePreset) {
  MockAsrProvider p("hello");
  EXPECT_TRUE(p.Transcribe("").empty());
  EXPECT_EQ(p.Transcribe("any-pcm"), "hello");
}

TEST_F(VoiceAsrTest, EmptyTranscriptFromProviderIsNoSpeech) {
  AsrFilter empty(filter_->options(), std::make_unique<MockAsrProvider>(""));
  const AsrFilterResult r = empty.Inspect("fake-pcm");
  EXPECT_EQ(r.disposition, AsrDisposition::kNone);
  EXPECT_TRUE(r.publishable.empty());
  EXPECT_TRUE(empty.audit_log().empty());
}

TEST(AsrDispositionTest, FromStringParsesTheThreeActionsAndFallsBackToFlag) {
  EXPECT_EQ(chirp::voice::asr::AsrDispositionFromString("mute"), AsrDisposition::kMute);
  EXPECT_EQ(chirp::voice::asr::AsrDispositionFromString("INTERCEPT"), AsrDisposition::kIntercept);
  EXPECT_EQ(chirp::voice::asr::AsrDispositionFromString("flag"), AsrDisposition::kFlag);
  EXPECT_EQ(chirp::voice::asr::AsrDispositionFromString(""), AsrDisposition::kFlag);
  EXPECT_EQ(chirp::voice::asr::AsrDispositionFromString("nonsense"), AsrDisposition::kFlag);
}

TEST(AsrDispositionTest, ToStringRoundsTripTheCanonicalSpellings) {
  EXPECT_STREQ(chirp::voice::asr::AsrDispositionToString(AsrDisposition::kNone), "none");
  EXPECT_STREQ(chirp::voice::asr::AsrDispositionToString(AsrDisposition::kFlag), "flag");
  EXPECT_STREQ(chirp::voice::asr::AsrDispositionToString(AsrDisposition::kIntercept),
               "intercept");
  EXPECT_STREQ(chirp::voice::asr::AsrDispositionToString(AsrDisposition::kMute), "mute");
  EXPECT_EQ(chirp::voice::asr::AsrDispositionFromString(
                chirp::voice::asr::AsrDispositionToString(AsrDisposition::kMute)),
            AsrDisposition::kMute);
}

TEST_F(VoiceAsrTest, EmptyPcmYieldsNoSpeechAndNoDisposition) {
  const AsrFilterResult r = filter_->Inspect("");
  EXPECT_EQ(r.disposition, AsrDisposition::kNone);
  EXPECT_EQ(r.hits, 0);
  EXPECT_TRUE(r.publishable.empty());
  EXPECT_TRUE(filter_->audit_log().empty());
}

TEST_F(VoiceAsrTest, CleanTranscriptPassesVerbatim) {
  filter_ = std::make_unique<AsrFilter>(
      filter_->options(), std::make_unique<MockAsrProvider>("weather is nice today"));
  const AsrFilterResult r = filter_->Inspect("fake-pcm");
  EXPECT_EQ(r.disposition, AsrDisposition::kNone);
  EXPECT_EQ(r.hits, 0);
  EXPECT_EQ(r.publishable, "weather is nice today");
  EXPECT_TRUE(filter_->audit_log().empty());
}

TEST_F(VoiceAsrTest, DisabledChainIsAPassThrough) {
  AsrFilterOptions options;  // no lexicon path
  AsrFilter disabled(options, std::make_unique<MockAsrProvider>("anything at all"));
  const AsrFilterResult r = disabled.Inspect("fake-pcm");
  EXPECT_EQ(r.disposition, AsrDisposition::kNone);
  EXPECT_EQ(r.publishable, "anything at all");
  EXPECT_TRUE(disabled.audit_log().empty());
}

TEST_F(VoiceAsrTest, FlagMasksTheTranscriptUsingTheSharedMasker) {
  const AsrFilterResult r = filter_->Inspect("fake-pcm");
  ASSERT_EQ(r.disposition, AsrDisposition::kFlag);
  EXPECT_EQ(r.hits, 1);
  EXPECT_EQ(r.publishable, "you are a **");
}

TEST_F(VoiceAsrTest, InterceptSuppressesTheTranscriptButStillAudits) {
  AsrFilterOptions options;
  options.lexicon_path = lexicon_.path();
  options.disposition = "intercept";
  options.reload_check_interval_ms = 0;
  AsrFilter intercept(options, std::make_unique<MockAsrProvider>("you are a badword"));

  const AsrFilterResult r = intercept.InspectAudio(state_, "alice", "room-1", "fake-pcm");
  EXPECT_EQ(r.disposition, AsrDisposition::kIntercept);
  EXPECT_TRUE(r.publishable.empty());  // the text never leaves the chain
  ASSERT_EQ(intercept.audit_log().size(), 1u);
  EXPECT_EQ(intercept.audit_log().front().disposition, AsrDisposition::kIntercept);
  EXPECT_EQ(intercept.audit_log().front().hits, 1);
}

TEST_F(VoiceAsrTest, AuditTrailStaysBoundedAtCapacity) {
  AsrFilterOptions options;
  options.lexicon_path = lexicon_.path();
  options.audit_capacity = 3;
  options.reload_check_interval_ms = 0;
  AsrFilter small(options, std::make_unique<MockAsrProvider>("badword"));

  // InspectAudio with no room: the audit still records, the mute is a no-op.
  for (int i = 0; i < 5; ++i) {
    small.InspectAudio(state_, "speaker", "room-1", "fake-pcm");
  }
  ASSERT_EQ(small.audit_log().size(), 3u);
  // The two oldest were dropped: the surviving head is the third hit.
  EXPECT_EQ(small.audit_log().front().hits, 1);
  EXPECT_EQ(small.audit_log().back().hits, 1);
}

TEST_F(VoiceAsrTest, MuteDispositionMutesTheSpeakerAndNotifiesEveryone) {
  const auto speaker = AddSpeaker("alice");
  const auto listener = AddSpeaker("bob");

  AsrFilterOptions options;
  options.lexicon_path = lexicon_.path();
  options.disposition = "mute";
  options.reload_check_interval_ms = 0;
  AsrFilter mute(options, std::make_unique<MockAsrProvider>("you are a badword"));

  const AsrFilterResult r =
      mute.InspectAudio(state_, "alice", "room-1", "fake-pcm");
  ASSERT_EQ(r.disposition, AsrDisposition::kMute);
  EXPECT_TRUE(r.publishable.empty());

  // The member state carries the mute (same derived state as SET_MUTE).
  const auto& p = room_->participants.at("alice");
  EXPECT_TRUE(p.muted());
  EXPECT_EQ(p.state(), chirp::voice::MUTED);

  // Everyone including the offender gets exactly one state-changed notify.
  // Sends are framed (u32-BE length + Packet), same as voice_tests decodes.
  ASSERT_EQ(speaker->sent.size(), 1u);
  const std::string& framed = speaker->sent.back();
  ASSERT_GE(framed.size(), 4u);
  const uint32_t len = (static_cast<uint32_t>(static_cast<uint8_t>(framed[0])) << 24) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(framed[1])) << 16) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(framed[2])) << 8) |
                       static_cast<uint32_t>(static_cast<uint8_t>(framed[3]));
  ASSERT_EQ(framed.size(), 4u + static_cast<size_t>(len));
  chirp::gateway::Packet pkt;
  ASSERT_TRUE(pkt.ParseFromString(framed.substr(4)));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::PARTICIPANT_STATE_CHANGED_NOTIFY);
  chirp::voice::ParticipantStateChangedNotify notify;
  ASSERT_TRUE(notify.ParseFromString(pkt.body()));
  EXPECT_EQ(notify.user_id(), "alice");
  EXPECT_EQ(notify.state(), chirp::voice::MUTED);
  EXPECT_EQ(listener->sent.size(), 1u);
  ASSERT_EQ(mute.audit_log().size(), 1u);
  EXPECT_EQ(mute.audit_log().front().speaker_id, "alice");
  EXPECT_EQ(mute.audit_log().front().room_id, "room-1");
}

TEST_F(VoiceAsrTest, MuteKeepsDeafenedAsDeafened) {
  AddSpeaker("alice");
  room_->participants.at("alice").set_deafened(true);
  room_->participants.at("alice").set_state(chirp::voice::DEAFENED);

  EXPECT_TRUE(chirp::voice::asr::MuteForViolation(state_, "room-1", "alice"));
  const auto& p = room_->participants.at("alice");
  EXPECT_TRUE(p.muted());
  EXPECT_EQ(p.state(), chirp::voice::DEAFENED);  // deafened wins, same as SET_MUTE
}

TEST_F(VoiceAsrTest, MuteForUnknownRoomOrSpeakerIsAQuietNoOp) {
  AddSpeaker("alice");
  EXPECT_FALSE(chirp::voice::asr::MuteForViolation(state_, "no-such-room", "alice"));
  EXPECT_FALSE(chirp::voice::asr::MuteForViolation(state_, "room-1", "no-such-speaker"));
  EXPECT_EQ(room_->participants.at("alice").muted(), false);
}

}  // namespace
