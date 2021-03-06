// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#pragma once

#include "Protocol.h"
#include "msg/async/frames_v2.h"
#include "msg/async/crypto_onwire.h"

namespace ceph::net {

class ProtocolV2 final : public Protocol {
 public:
  ProtocolV2(Dispatcher& dispatcher,
             SocketConnection& conn,
             SocketMessenger& messenger);
  ~ProtocolV2() override;

 private:
  void start_connect(const entity_addr_t& peer_addr,
                     const entity_type_t& peer_type) override;

  void start_accept(SocketFRef&& socket,
                    const entity_addr_t& peer_addr) override;

  void trigger_close() override;

  seastar::future<> write_message(MessageRef msg) override;

  seastar::future<> do_keepalive() override;

  seastar::future<> do_keepalive_ack() override;

 private:
  SocketMessenger &messenger;

  enum class state_t {
    NONE = 0,
    ACCEPTING,
    CONNECTING,
    READY,
    STANDBY,
    WAIT,           // ? CLIENT_WAIT
    SERVER_WAIT,    // ?
    REPLACING,      // ?
    CLOSING
  };
  state_t state = state_t::NONE;

  static const char *get_state_name(state_t state) {
    const char *const statenames[] = {"NONE",
                                      "ACCEPTING",
                                      "CONNECTING",
                                      "READY",
                                      "STANDBY",
                                      "WAIT",           // ? CLIENT_WAIT
                                      "SERVER_WAIT",    // ?
                                      "REPLACING",      // ?
                                      "CLOSING"};
    return statenames[static_cast<int>(state)];
  }

  void trigger_state(state_t state, write_state_t write_state, bool reentrant);

  entity_name_t peer_name;
  uint64_t connection_features = 0;
  uint64_t peer_required_features = 0;

  uint64_t client_cookie = 0;
  uint64_t server_cookie = 0;
  uint64_t global_seq = 0;
  uint64_t peer_global_seq = 0;
  uint64_t connect_seq = 0;

  utime_t last_keepalive_ack_to_send;

 // TODO: Frame related implementations, probably to a separate class.
 private:
  bool record_io = false;
  ceph::bufferlist rxbuf;
  ceph::bufferlist txbuf;

  void enable_recording();
  seastar::future<Socket::tmp_buf> read_exactly(size_t bytes);
  seastar::future<bufferlist> read(size_t bytes);
  seastar::future<> write(bufferlist&& buf);
  seastar::future<> write_flush(bufferlist&& buf);

  ceph::crypto::onwire::rxtx_t session_stream_handlers;
  boost::container::static_vector<ceph::msgr::v2::segment_t,
				  ceph::msgr::v2::MAX_NUM_SEGMENTS> rx_segments_desc;
  boost::container::static_vector<ceph::bufferlist,
				  ceph::msgr::v2::MAX_NUM_SEGMENTS> rx_segments_data;

  size_t get_current_msg_size() const;
  seastar::future<ceph::msgr::v2::Tag> read_main_preamble();
  seastar::future<> read_frame_payload();
  template <class F>
  seastar::future<> write_frame(F &frame, bool flush=true);

 private:
  seastar::future<> fault();
  void dispatch_reset();
  void reset_session(bool full);
  seastar::future<entity_type_t, entity_addr_t> banner_exchange();

  // CONNECTING (client)
  seastar::future<> handle_auth_reply();
  inline seastar::future<> client_auth() {
    std::vector<uint32_t> empty;
    return client_auth(empty);
  }
  seastar::future<> client_auth(std::vector<uint32_t> &allowed_methods);

  seastar::future<bool> process_wait();
  seastar::future<bool> client_connect();
  seastar::future<bool> client_reconnect();
  void execute_connecting();

  // ACCEPTING (server)
  seastar::future<> _auth_bad_method(int r);
  seastar::future<> _handle_auth_request(bufferlist& auth_payload, bool more);
  seastar::future<> server_auth();

  seastar::future<bool> send_wait();

  seastar::future<bool> handle_existing_connection(SocketConnectionRef existing);
  seastar::future<bool> server_connect();

  seastar::future<bool> read_reconnect();
  seastar::future<bool> send_retry(uint64_t connect_seq);
  seastar::future<bool> send_retry_global(uint64_t global_seq);
  seastar::future<bool> send_reset(bool full);
  seastar::future<bool> server_reconnect();

  void execute_accepting();

  // CONNECTING/ACCEPTING
  seastar::future<> finish_auth();

  // ACCEPTING/REPLACING (server)
  seastar::future<> send_server_ident();

  // REPLACING (server)
  seastar::future<> send_reconnect_ok();

  // READY
  seastar::future<> read_message(utime_t throttle_stamp);
  void handle_message_ack(seq_num_t seq);
  void execute_ready();

  // STANDBY
  void execute_standby();

  // WAIT
  void execute_wait();

  // SERVER_WAIT
  void execute_server_wait();
};

} // namespace ceph::net
