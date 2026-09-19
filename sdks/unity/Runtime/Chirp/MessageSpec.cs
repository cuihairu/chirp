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
        public MessageSpec(MsgID reqMsgId, MsgID respMsgId, MessageParser<TResp> parser)
        {
            ReqMsgId = reqMsgId;
            RespMsgId = respMsgId;
            _parser = parser;
        }

        public MsgID ReqMsgId { get; }
        public MsgID RespMsgId { get; }

        private readonly MessageParser<TResp> _parser;

        public TResp Decode(byte[] body) => _parser.ParseFrom(body);
    }
}
