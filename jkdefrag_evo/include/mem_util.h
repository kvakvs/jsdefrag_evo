#pragma once

// A non-owning memory reader
class MemSlice {
private:
    template<typename T = uint64_t>
    MemSlice(uint8_t *init, Bytes<T> limit) : ptr_(init), limit_(limit.value()) {
        data_max_ = ptr_ + limit.value();
    }

public:
    using Self = MemSlice;

    template<typename T = uint64_t>
    static Self from_ptr(uint8_t *init, Bytes<T> limit) { return Self(init, limit); }

    static Self empty() { return Self(nullptr, Bytes64(0)); }

    // Access the const storage
    [[nodiscard]] const uint8_t *get() const { return ptr_; }

    // Access the storage
    [[nodiscard]] uint8_t *get() { return ptr_; }

    template<typename OUT_T, typename T = uint64_t>
    OUT_T read(Bytes<T> byte_offset) const {
        const auto read_ptr = ptr_ + byte_offset.value();
        _ASSERT(read_ptr < data_max_);
        return *((OUT_T *) read_ptr);
    }

    template<typename OUT_T>
    OUT_T read(size_t byte_offset) const {
        const auto read_ptr = ptr_ + byte_offset;
        _ASSERT(read_ptr < data_max_);
        return *((OUT_T *) read_ptr);
    }

    template<typename OUT_T, typename T = uint64_t>
    OUT_T *ptr_to(Bytes<T> byte_offset) const {
        const auto read_ptr = ptr_ + byte_offset.value();
        _ASSERT(read_ptr < data_max_);
        return (OUT_T *) read_ptr;
    }

    template<typename OUT_T>
    OUT_T *ptr_to(size_t byte_offset) const {
        const auto read_ptr = ptr_ + byte_offset;
        _ASSERT(read_ptr < data_max_);
        return (OUT_T *) read_ptr;
    }

    [[nodiscard]] Self sub_view(Bytes64 offset, Bytes64 limit) const {
        return from_ptr(ptr_ + offset.value(), limit);
    }

    [[nodiscard]] Bytes64 length() const { return limit_; }

    [[nodiscard]] bool is_empty() const { return limit_.is_zero() || ptr_ == nullptr; }

    [[nodiscard]] operator bool() const { return ptr_ != nullptr; }

private:
    uint8_t *ptr_;
    const uint8_t *data_max_;
    Bytes64 limit_;
};

// Takes ownership of smart pointer, typically a byte buffer, and allows reading other types from it.
// A read slice of memory, accessible as other types
class UniquePtrSlice {
private:
    template<typename T = uint64_t>
    UniquePtrSlice(std::unique_ptr<uint8_t> &&smart, Bytes<T> limit) : smart_(std::move(smart)), limit_(limit) {
        data_max_ = smart_.get() + limit.value();
    }

    // Construct from Bytes<> size, allocate here
    template<typename T = uint64_t>
    explicit UniquePtrSlice(Bytes<T> limit) : smart_(std::make_unique<uint8_t>(limit.value())), limit_(limit) {
        data_max_ = smart_.get() + limit.value();
    }

public:
    using Self = UniquePtrSlice;

    // Construct from moved std::unique_ptr and limit
    template<typename T = uint64_t>
    static Self make(std::unique_ptr<uint8_t> &&smart, Bytes<T> limit) { return Self(std::move(smart), limit); }

    // Construct from Bytes<> size, allocate here
    template<typename T = uint64_t>
    static Self make_new(Bytes<T> limit) { return Self(limit); }

    // Access the const storage
    const uint8_t *get() const {
        return smart_.get();
    }

    // Access the storage
    uint8_t *get() {
        return smart_.get();
    }

    // Access the slice. NOTE: The returned result must not outlive this object
    template<typename T = uint64_t>
    MemSlice as_slice(Bytes<T> offset = Bytes<T>(0)) {
        return MemSlice::from_ptr(smart_.get() + offset.value(), limit_);
    }

    // Read as a different type
    template<typename OUT_T, typename T = uint64_t>
    OUT_T read(Bytes<T> byte_offset) const {
        const auto read_ptr = smart_.get() + byte_offset.value();
        _ASSERT(read_ptr < data_max_);
        return *((OUT_T *) read_ptr);
    }

    template<typename OUT_T>
    OUT_T read(size_t byte_offset) const {
        const auto read_ptr = smart_.get() + byte_offset;
        _ASSERT(read_ptr < data_max_);
        return *((OUT_T *) read_ptr);
    }

    template<typename OUT_T, typename T = uint64_t>
    OUT_T *ptr_to(Bytes<T> byte_offset) const {
        const auto read_ptr = smart_.get() + byte_offset.value();
        _ASSERT(read_ptr < data_max_);
        return (OUT_T *) read_ptr;
    }

    template<typename OUT_T>
    OUT_T *ptr_to(size_t byte_offset) const {
        const auto read_ptr = smart_.get() + byte_offset;
        _ASSERT(read_ptr < data_max_);
        return (OUT_T *) read_ptr;
    }

    [[nodiscard]] Bytes64 length() const { return limit_; }

    [[nodiscard]] bool is_empty() const { return limit_.is_zero() || smart_ == nullptr; }

    [[nodiscard]] operator bool() const { return smart_ != nullptr; }

private:
    std::unique_ptr<uint8_t> smart_;
    const uint8_t *data_max_;
    Bytes64 limit_;
};
