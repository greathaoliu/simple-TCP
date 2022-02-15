#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

// 谨慎使用返回参数
int StreamReassembler::merge_block(block_node& a, block_node& b) {
    bool flag = false; // 区分向前合并 向后合并
    if(a.begin > b.begin) {
        swap(a, b);
        flag = true;
    }

    if(a.begin + a.length < b.begin) { // can't merge
        if(flag) swap(a, b);
        return -1;
    }

    if(a.begin + a.length >= b.begin + b.length) {
        if(flag) return a.length;
        return b.length;
    }
    
    int len = a.length;
    a.data = a.data + b.data.substr(a.begin + a.length - b.begin);
    a.length = a.data.length();
    if(flag) return len;
    return b.length;
}

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const uint64_t index, const bool eof) {
    if(index >= _head_index + _capacity) return; // unable to push
    if(data.length() == 0 || index + data.length() <= _head_index) {
        if(eof) {
            _eof = eof;
        }
        if(_eof && empty()) {
            _output.end_input();
        }
        return;
    }

    block_node node;
    if(index < _head_index) {
        node.begin = _head_index;
        node.data = data.substr(_head_index - index);
        node.length = node.data.length();
    } else {
        node.begin = index;
        node.data = data;
        node.length = data.length();
    }

    int erased_bytes = 0;
    if(_blocks.empty()) {
        _blocks.insert(node);
        _unassembled_byte = node.length;
    } else {
        auto iter = lower_bound(_blocks.begin(), _blocks.end(), node);
        int merge_bytes;
        auto iter_next = iter;

        if(iter != _blocks.end()) {
            block_node next = *iter;
            // merge next
            while(iter != _blocks.end() && (merge_bytes = merge_block(node, next)) != -1) {
                iter_next = iter;
                iter_next ++;
                erased_bytes += iter->length;
                _blocks.erase(iter);
                if(iter_next != _blocks.end()) {
                    next = *iter_next;
                    iter = iter_next;
                } else {
                    break;
                }
            }
        }

        iter = lower_bound(_blocks.begin(), _blocks.end(), node);
        // merge prev
        if(iter != _blocks.begin()) {
            iter --;
            auto next = *iter;
            while((merge_bytes = merge_block(node, next)) != -1) {
                iter_next = iter;
                iter_next --;
                if(iter != _blocks.begin()) {
                    erased_bytes += iter->length;
                    _blocks.erase(iter);
                    next = *iter_next;
                    iter = iter_next;
                } else {
                    erased_bytes += iter->length;
                    _blocks.erase(iter);
                    break;
                }
            }
        }

        _blocks.insert(node);
        _unassembled_byte += node.length - erased_bytes;
    }

    while(!_blocks.empty() && _blocks.begin()->begin == _head_index) {
        auto iter = _blocks.begin();
        size_t write_bytes = _output.write(iter->data);
        _head_index += write_bytes;
        _unassembled_byte -= write_bytes;
        if(write_bytes < iter->length) {
            node = *iter;
            _blocks.erase(iter);
            node.length -= write_bytes;
            node.data = node.data.substr(write_bytes);
            node.begin = _head_index;
            break;
        }
        _blocks.erase(iter);
    }

    if(eof) {
        _eof = eof;
    }
    if(_eof && empty()) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const {
    return _unassembled_byte;
}

bool StreamReassembler::empty() const {
    return _unassembled_byte == 0;
}
