#pragma once

// Takes ownership of smart pointer, typically a byte buffer, and allows reading other types from it.
// A read slice of memory, accessible as other types
template<typename MemT = uint8_t>
class MemReader {
public:
    MemReader(std::unique_ptr<MemT[]> &&smart, size_t limit) : smart_(std::move(smart)) {
        data_max_ = smart_.get() + limit;
    }

    const MemT *get() const {
        return smart_.get();
    }

    MemT *get() {
        return smart_.get();
    }

    template<typename ResultT>
    ResultT read(size_t byte_offset) const {
        const auto read_ptr = smart_.get() + byte_offset;
        _ASSERT(read_ptr < data_max_);
        return *((ResultT *) read_ptr);
    }

private:
    std::unique_ptr<MemT[]> smart_;
    const MemT *data_max_;
};
