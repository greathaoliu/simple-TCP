#include "tcp_connection.hh"

#include <iostream>
#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _ms_since_last_seg_recvd; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    _ms_since_last_seg_recvd = 0;
    bool send_empty = false;
    
    if (seg.header().ack && _sender.syn_sent()) {
        // unacceptable ACKs should produced a segment that existed
        if(!_sender.ack_received(seg.header().ackno, seg.header().win)) {
            send_empty = true;
        } else {
            _sender.fill_window();
        }
    }

    bool recv_recv = _receiver.segment_received(seg);
    if (!recv_recv) {
        send_empty = true;
    }

    if (seg.header().syn && !_sender.syn_sent()) {
        connect();
        return;
    }

    // 忽略超出窗口范围带RST的seg
    if(seg.header().rst){
        if(recv_recv || seg.header().ack && _sender.next_seqno() == seg.header().ackno){
            _rst = 1;
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            test_end();
        }
        return;
    }

    
    if(seg.header().fin){
        if(!_sender.fin_sent())      // 继续单向发送，最后一个seg发送FIN+ACK
            _sender.fill_window();
        if (_sender.segments_out().empty())     // 发送ACK
            send_empty=true;
    } else if(seg.length_in_sequence_space()){
        send_empty = true;
    }

    if(send_empty){
        // if the ackno is missing, don't send back an ACK.
        if (_receiver.ackno().has_value() && _sender.segments_out().empty())
            _sender.send_empty_segment();
    }
    fill_queue();
    test_end();
}

bool TCPConnection::active() const {
    return _shutdown_status == 0 && !_rst;
}

size_t TCPConnection::write(const string &data) {
    if(data.empty()) return 0;
    size_t ret = _sender.stream_in().write(data);
    _sender.fill_window();
    fill_queue();
    test_end();
    return ret;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _sender.tick(ms_since_last_tick);
    _ms_since_last_seg_recvd += ms_since_last_tick;
    fill_queue();
    test_end();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window(); // 填满发送窗口
    fill_queue();
    test_end();
}

void TCPConnection::connect() {
    _sender.fill_window(); // 发送SYN
    _rst = false;
    fill_queue();
    test_end();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            _rst = true; // 当active()时析构
            _sender.send_empty_segment(); // 向sender队列中添加一个empty_seg
            fill_queue(); // 添加到TCPConnection的队列中
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

TCPSegment TCPConnection::popTCPSegment() {
    TCPSegment seg = _sender.segments_out().front();
    _sender.segments_out().pop();

    // 对之前收到的seg进行确认
    if(_receiver.ackno().has_value() && !_rst) { 
        seg.header().ackno = _receiver.ackno().value();
        seg.header().ack = true;
    }

    // 非正常结束
    if(_rst || _sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        _rst = true;
        seg.header().rst = true;
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        return seg;
    }

    // 窗口大小为uint16_t
    seg.header().win = min(_receiver.window_size(), static_cast<size_t>(numeric_limits<uint16_t>::max()));

    return seg;
}

// 检查状态
void TCPConnection::test_end() {
    if(_receiver.stream_out().input_ended() && !_sender.stream_in().eof()) {
        _linger_after_streams_finish = false;
    }

    if(_receiver.stream_out().eof() && unassembled_bytes() == 0 // Prereq #1
    && _sender.stream_in().eof() && _sender.fin_sent() // Prereq #2
    && bytes_in_flight() == 0) { // Prereq #3
        if(!_linger_after_streams_finish) {
            _shutdown_status = 1; // clean
        } else if(_ms_since_last_seg_recvd >= 10 * _cfg.rt_timeout) {
            _shutdown_status = 2; // unclean
        }
    }
}

void TCPConnection::fill_queue() {
    while(!_sender.segments_out().empty()) {
        TCPSegment seg = std::move(popTCPSegment());
        _segments_out.push(seg);
    }
}
