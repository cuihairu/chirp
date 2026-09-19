using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Chirp.Sdk;
using Xunit;

public class FrameCodecTests
{
    private static byte[] Payload(string s) => Encoding.UTF8.GetBytes(s);

    [Fact]
    public void EncodeDecode_RoundtripsOnCleanStream()
    {
        var decoder = new FrameDecoder();
        var frames = decoder.Feed(FrameCodec.Encode(Payload("hello")));
        Assert.Single(frames);
        Assert.Equal(Payload("hello"), frames.Single());
    }

    [Fact]
    public void CoalescedFramesSplit_SplitFramesReassemble()
    {
        var decoder = new FrameDecoder();
        var a = FrameCodec.Encode(Payload("aaaa"));
        var b = FrameCodec.Encode(Payload("bb"));

        // Two frames in one chunk.
        var both = a.Concat(b).ToArray();
        var split = decoder.Feed(both);
        Assert.Equal(new[] { "aaaa", "bb" }, split.Select(Encoding.UTF8.GetString));

        // One frame across three chunks.
        decoder.Reset();
        var single = FrameCodec.Encode(Payload("hello world"));
        var delivered = new List<byte[]>();
        delivered.AddRange(decoder.Feed(single.Take(5).ToArray()));
        Assert.Empty(delivered); // no premature delivery
        delivered.AddRange(decoder.Feed(single.Skip(5).Take(10).ToArray()));
        delivered.AddRange(decoder.Feed(single.Skip(15).ToArray()));
        Assert.Equal(new[] { "hello world" }, delivered.Select(Encoding.UTF8.GetString));
    }

    [Fact]
    public void OversizedFrame_ThrowsAndResetRestoresUsability()
    {
        var decoder = new FrameDecoder();
        // 0x01000001 = MaxFrameBytes + 1.
        var huge = new byte[4 + FrameCodec.MaxFrameBytes + 1];
        huge[0] = 0x01;
        huge[1] = 0x00;
        huge[2] = 0x00;
        huge[3] = 0x01;
        Assert.Throws<FrameError>(() => decoder.Feed(huge));
        decoder.Reset();
        var ok = decoder.Feed(FrameCodec.Encode(Payload("ok")));
        Assert.Equal(new[] { "ok" }, ok.Select(Encoding.UTF8.GetString));
    }

    [Fact]
    public void LengthPrefix_DoesNotCountItself()
    {
        var encoded = FrameCodec.Encode(new byte[] { 1, 2, 3 });
        Assert.Equal(7, encoded.Length);
        Assert.Equal(3, (encoded[0] << 24) | (encoded[1] << 16) | (encoded[2] << 8) | encoded[3]);
    }
}
