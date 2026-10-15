using System;
using Chirp.Gateway;
using Google.Protobuf;

namespace Chirp.Sdk
{
    /// <summary>
    /// A typed request/response pair. On this protocol a response is
    /// correlated by Packet.sequence, not by msgId — the server answers with
    /// the RESP msgId and echoes the request's sequence. The spec keeps both
    /// ids and the decoder in one place so call sites never touch raw msgIds.
    /// </summary>
    public sealed class MessageSpec<TResp> where TResp : IMessage<TResp>
    {
        public MessageSpec(MsgID reqMsgId, MsgID respMsgId, MessageParser<TResp> parser,
            Func<TResp, TResp>? inflate = null)
        {
            ReqMsgId = reqMsgId;
            RespMsgId = respMsgId;
            _parser = parser;
            _inflate = inflate;
        }

        public MsgID ReqMsgId { get; }
        public MsgID RespMsgId { get; }

        private readonly MessageParser<TResp> _parser;

        /// <summary>Optional post-decode step, present only for the
        /// compressed-history shape (GET_HISTORY) which inflates
        /// MessagesGzip before handing the response up. Specs without the
        /// hook stay fully synchronous.</summary>
        private readonly Func<TResp, TResp>? _inflate;

        public TResp Decode(byte[] body)
        {
            var parsed = _parser.ParseFrom(body);
            return _inflate is null ? parsed : _inflate(parsed);
        }
    }
}
