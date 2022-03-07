/* Copyright 2016, Ableton AG, Berlin. All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  If you would like to incorporate Link into a proprietary software application,
 *  please contact <link-devs@ableton.com>.
 */

#pragma once

#include <ableton/link/GhostXForm.hpp>
#include <ableton/link/PayloadEntries.hpp>
#include <ableton/link/SessionId.hpp>
#include <ableton/link/v1/Messages.hpp>
#include <ableton/util/Injected.hpp>
#include <ableton/util/SafeAsyncHandler.hpp>
#include <chrono>
#include <memory>

#include <stdlib.h>
#include <string>
#include <iostream>
#include <ableton/link/Show.hpp>

namespace ableton
{
namespace link
{

template <typename Clock, typename IoContext>
class PingResponder
{
  using IoType = util::Injected<IoContext&>;
  using Socket = typename IoType::type::template Socket<v1::kMaxMessageSize>;

public:
  PingResponder(asio::ip::address_v4 address,
    SessionId sessionId,
    GhostXForm ghostXForm,
    Clock clock,
    IoType io)
    : show_g("Before mIo")
    , mIo(io)
    ,show_h("Before mpImpl")
    , mpImpl(std::make_shared<Impl>(std::move(address),
        std::move(sessionId),
        std::move(ghostXForm),
        std::move(clock),
        std::move(io)))
    , show_i("After mpImpl")
  {
    std::cout << "PingResponder:";
    mpImpl->listen();
    std::cout << " listened"<< std::endl;
  }

  PingResponder(const PingResponder&) = delete;
  PingResponder(PingResponder&&) = delete;

  void updateNodeState(const SessionId& sessionId, const GhostXForm& xform)
  {
    mpImpl->mSessionId = std::move(sessionId);
    mpImpl->mGhostXForm = std::move(xform);
  }

  asio::ip::udp::endpoint endpoint() const
  {
    return mpImpl->mSocket.endpoint();
  }

  asio::ip::address address() const
  {
    return endpoint().address();
  }

  Socket socket() const
  {
    return mpImpl->mSocket;
  }

private:
  struct Impl : std::enable_shared_from_this<Impl>
  {
    Impl(asio::ip::address_v4 address,
      SessionId sessionId,
      GhostXForm ghostXForm,
      Clock clock,
      IoType io)
      : show_a("Before mSessionId")
      , mSessionId(std::move(sessionId))
      , show_b("Before mGhostXForm")
      , mGhostXForm(std::move(ghostXForm))
      , show_c("Before mClock")
      , mClock(std::move(clock))
      , show_d("Before mLog")
      , mLog(channel(io->log(), "gateway@" + address.to_string()))
      , show_e("Before mSocket")
      , mSocket(io->template openUnicastSocket<v1::kMaxMessageSize>(address))
      , show_f("After mSocket")
    {
      std::cout << "PingResponder Impl" << std::endl;
    }

    void listen()
    {
      mSocket.receive(util::makeAsyncSafe(this->shared_from_this()));
    }

    // Operator to handle incoming messages on the interface
    template <typename It>
    void operator()(const asio::ip::udp::endpoint& from, const It begin, const It end)
    {
      using namespace discovery;

      // Decode Ping Message
      const auto result = link::v1::parseMessageHeader(begin, end);
      const auto& header = result.first;
      const auto payloadBegin = result.second;

      // Check Payload size
      const auto payloadSize = static_cast<std::size_t>(std::distance(payloadBegin, end));
      const auto maxPayloadSize =
        sizeInByteStream(makePayload(HostTime{}, PrevGHostTime{}));
      if (header.messageType == v1::kPing && payloadSize <= maxPayloadSize)
      {
        debug(mLog) << " Received ping message from " << from;

        try
        {
          reply(std::move(payloadBegin), std::move(end), from);
        }
        catch (const std::runtime_error& err)
        {
          info(mLog) << " Failed to send pong to " << from << ". Reason: " << err.what();
        }
      }
      else
      {
        info(mLog) << " Received invalid Message from " << from << ".";
      }
      listen();
    }

    template <typename It>
    void reply(It begin, It end, const asio::ip::udp::endpoint& to)
    {
      using namespace discovery;

      // Encode Pong Message
      const auto id = SessionMembership{mSessionId};
      const auto currentGt = GHostTime{mGhostXForm.hostToGhost(mClock.micros())};
      const auto pongPayload = makePayload(id, currentGt);

      v1::MessageBuffer pongBuffer;
      const auto pongMsgBegin = std::begin(pongBuffer);
      auto pongMsgEnd = v1::pongMessage(pongPayload, pongMsgBegin);
      // Append ping payload to pong message.
      pongMsgEnd = std::copy(begin, end, pongMsgEnd);

      const auto numBytes =
        static_cast<std::size_t>(std::distance(pongMsgBegin, pongMsgEnd));
      mSocket.send(pongBuffer.data(), numBytes, to);
    }

    Show show_a;
    SessionId mSessionId;
    Show show_b;
    GhostXForm mGhostXForm;
    Show show_c;
    Clock mClock;
    Show show_d;
    typename IoType::type::Log mLog;
    Show show_e;
    Socket mSocket;
    Show show_f;
  };

  Show show_g;
  IoType mIo;
  Show show_h;
  std::shared_ptr<Impl> mpImpl;
  Show show_i;
};

} // namespace link
} // namespace ableton
