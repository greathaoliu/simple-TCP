#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity), _timer(retx_timeout) {}

size_t TCPSender::bytes_in_flight() const {
    return _bytes_in_flight;
}

void TCPSender::fill_window() {
    TCPSegment seg;
    if(_next_seqno == 0) {
        seg.header().syn = true;
        send_non_empty_segment(seg);
        _syn_sent = true;
        return;
    }
    if(next_seqno_absolute() == bytes_in_flight()) {
        // SYN_SENT 发送了1个SYN 但未确认
        return;
    }

    size_t win = _window_size > 0? _window_size : 1;
    size_t remain_bytes;
    // 发送segment填满窗口
    while(remain_bytes = win - _next_seqno + _recv_abs_ackno, remain_bytes) {
        if(_stream.eof()) {
            if(_fin_sent) return;
            seg.header().fin = 1;
            send_non_empty_segment(seg);
            _fin_sent = true;
            return;
        } else {
            size_t payload_size = min(remain_bytes, TCPConfig::MAX_PAYLOAD_SIZE);
            seg.payload() = Buffer(std::move(_stream.read(payload_size)));
            
            if (seg.length_in_sequence_space() < win && _stream.eof()) { // 发送窗口有剩余，且流已经结束，发送fin
                seg.header().fin = 1;
                _fin_sent = 1;
            }

            if (seg.length_in_sequence_space() == 0) return; // 空seg不发送
            send_non_empty_segment(seg);
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
bool TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    if(ackno - next_seqno() > 0) return false; // 确认超过发送
    uint64_t abs_ackno = unwrap(ackno, _isn, _recv_abs_ackno);
    _window_size = window_size;
    if(abs_ackno <= _recv_abs_ackno) return true; // 确认了已经经过确认的

    _recv_abs_ackno = abs_ackno;
    // 恢复RTO初始值
    _timer._RTO = _timer._initial_RTO;
    _consecutive_retransmissions = 0;

    TCPSegment seg;
    while(!_segments_outstanding.empty()) {
        seg = _segments_outstanding.front();
        if(static_cast<size_t>(ackno - seg.header().seqno) >= seg.length_in_sequence_space()) {
            _bytes_in_flight -= seg.length_in_sequence_space();
            _segments_outstanding.pop();
        } else {
            break;
        }
    }

    fill_window();

    // ack后重新计时
    if(!_segments_outstanding.empty()) {
        _timer.start();
    }
    return true;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // 已经超时
    if(_timer.tick(ms_since_last_tick)) {
        if(!_segments_outstanding.empty()) {
            _segments_out.push(_segments_outstanding.front());
            if(_window_size) {
                _consecutive_retransmissions ++;
                _timer.double_RTO();
            }
            if(!_timer.open()) {
                _timer.start();
            }
            // SYN SENT状态
            if(_syn_sent && next_seqno_absolute() == bytes_in_flight()) {
                if(_timer._RTO < _timer._initial_RTO){
                    _timer._RTO = _timer._initial_RTO;
                }
            }
        }

        if(_segments_outstanding.empty()) {
            _timer.close();
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const {
    return _consecutive_retransmissions;
}

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = next_seqno();
    _segments_out.push(seg);
}

void TCPSender::send_non_empty_segment(TCPSegment& seg) {
    seg.header().seqno = next_seqno();
    _next_seqno += seg.length_in_sequence_space();
    _bytes_in_flight += seg.length_in_sequence_space();
    
    _segments_out.push(seg);
    _segments_outstanding.push(seg);

    // 开启超时计时
    if(!_timer.open()) {
        _timer.start();
    }
}
