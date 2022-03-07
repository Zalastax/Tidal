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

#include <ableton/discovery/InterfaceScanner.hpp>
#include <ableton/platforms/asio/AsioWrapper.hpp>
#include <map>
#include <stdlib.h>
#include <string>
#include <iostream>
#include <exception>
#include <typeinfo>
#include <stdexcept>

namespace ableton
{
namespace discovery
{

// GatewayFactory must have an operator()(NodeState, IoRef, asio::ip::address)
// that constructs a new PeerGateway on a given interface address.
template <typename NodeState, typename GatewayFactory, typename IoContext>
class PeerGateways
{
public:
  using IoType = typename util::Injected<IoContext>::type;
  using Gateway = typename std::result_of<GatewayFactory(
    NodeState, util::Injected<IoType&>, asio::ip::address)>::type;
  using GatewayMap = std::map<asio::ip::address, Gateway>;

  PeerGateways(const std::chrono::seconds rescanPeriod,
    NodeState state,
    GatewayFactory factory,
    util::Injected<IoContext> io)
    : mIo(std::move(io))
  {
    mpScannerCallback =
      std::make_shared<Callback>(std::move(state), std::move(factory), *mIo);
    mpScanner = std::make_shared<Scanner>(
      rescanPeriod, util::injectShared(mpScannerCallback), util::injectRef(*mIo));
  }

  ~PeerGateways()
  {
    mpScanner.reset();
    mpScannerCallback.reset();
  }

  PeerGateways(const PeerGateways&) = delete;
  PeerGateways& operator=(const PeerGateways&) = delete;

  PeerGateways(PeerGateways&&) = delete;
  PeerGateways& operator=(PeerGateways&&) = delete;

  void enable(const bool bEnable)
  {
    mpScannerCallback->mGateways.clear();
    mpScanner->enable(bEnable);
  }

  template <typename Handler>
  void withGateways(Handler handler)
  {
    handler(mpScannerCallback->mGateways.begin(), mpScannerCallback->mGateways.end());
  }

  void updateNodeState(const NodeState& state)
  {
    mpScannerCallback->mState = state;
    for (const auto& entry : mpScannerCallback->mGateways)
    {
      entry.second->updateNodeState(state);
    }
  }

  // If a gateway has become non-responsive or is throwing exceptions,
  // this method can be invoked to either fix it or discard it.
  void repairGateway(const asio::ip::address& gatewayAddr)
  {
    if (mpScannerCallback->mGateways.erase(gatewayAddr))
    {
      // If we erased a gateway, rescan again immediately so that
      // we will re-initialize it if it's still present
      mpScanner->scan();
    }
  }

private:
  struct Callback
  {
    Callback(NodeState state, GatewayFactory factory, IoType& io)
      : mState(std::move(state))
      , mFactory(std::move(factory))
      , mIo(io)
    {
    }

    template <typename AddrRange>
    void operator()(const AddrRange& range)
    {
      using namespace std;
      // Get the set of current addresses.
      vector<asio::ip::address> curAddrs;
      curAddrs.reserve(mGateways.size());
      transform(std::begin(mGateways), std::end(mGateways), back_inserter(curAddrs),
        [](const typename GatewayMap::value_type& vt) { return vt.first; });

      std::cout << "Pierre 1";

      // Now use set_difference to determine the set of addresses that
      // are new and the set of cur addresses that are no longer there
      vector<asio::ip::address> newAddrs;
      set_difference(std::begin(range), std::end(range), std::begin(curAddrs),
        std::end(curAddrs), back_inserter(newAddrs));
      std::cout << "Pierre 2";
      vector<asio::ip::address> staleAddrs;
      set_difference(std::begin(curAddrs), std::end(curAddrs), std::begin(range),
        std::end(range), back_inserter(staleAddrs));

      std::cout << "Pierre 3";
      // Remove the stale addresses
      for (const auto& addr : staleAddrs)
      {
        mGateways.erase(addr);
      }

      std::cout << "Pierre 4";
      // Add the new addresses
      for (const auto& addr : newAddrs)
      {
        std::cout << "Pierre 5";
        try
        {
          // Only handle v4 for now
          if (addr.is_v4())
          {
            std::cout << "Pierre 6";
            info(mIo.log()) << "initializing peer gateway on interface " << addr;
            auto addv4 = addr.to_v4();
            std::cout << "Pierre 8";
            auto hhhhh = mFactory(mState, util::injectRef(mIo), addv4);
            std::cout << "Pierre 9";
            mGateways.emplace(addr, hhhhh);
            std::cout << "Pierre 13" << std::endl;
          }
        }
        catch (const runtime_error& e)
        {
          std::cout << "Pierre 12";
          warning(mIo.log()) << "failed to init gateway on interface " << addr
                             << " reason: " << e.what();
        }
        catch(...)
        {
          std::exception_ptr p = std::current_exception();
          std::cout <<(p ? p.__cxa_exception_type()->name() : "null") << std::endl;
        }
      }
    }

    NodeState mState;
    GatewayFactory mFactory;
    IoType& mIo;
    GatewayMap mGateways;
  };

  using Scanner = InterfaceScanner<std::shared_ptr<Callback>, IoType&>;
  std::shared_ptr<Callback> mpScannerCallback;
  std::shared_ptr<Scanner> mpScanner;
  util::Injected<IoContext> mIo;
};

// Factory function
template <typename NodeState, typename GatewayFactory, typename IoContext>
std::unique_ptr<PeerGateways<NodeState, GatewayFactory, IoContext>> makePeerGateways(
  const std::chrono::seconds rescanPeriod,
  NodeState state,
  GatewayFactory factory,
  util::Injected<IoContext> io)
{
  using namespace std;
  using Gateways = PeerGateways<NodeState, GatewayFactory, IoContext>;
  return unique_ptr<Gateways>{
    new Gateways{rescanPeriod, std::move(state), std::move(factory), std::move(io)}};
}

} // namespace discovery
} // namespace ableton
