/*
 JkDefrag  --  Defragment and optimize all harddisks.

 This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 Public License as published by the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 For the full text of the license see the "License gpl.txt" file.

 Jeroen C. Kessels, Internet Engineer
 http://www.kessels.com/
 */

#pragma once

#include "constants.h"

// Generalized value with an unit and some arithmetic
template<typename STORAGE, typename UNIT>
class Numeral {
public:
    constexpr Numeral() = default;

    ~Numeral() = default;

    explicit constexpr Numeral(STORAGE value) : value_(value) {}

    using Self = Numeral<STORAGE, UNIT>;
    using Storage = STORAGE;
    using Unit = UNIT;

    //-----------------
    // Comparisons
    //-----------------

    friend bool operator==(const Self a, const Self b) {
        return a.value_ == b.value_;
    }

    friend bool operator<=(const Self a, const Self b) {
        return a.value_ <= b.value_;
    }

    friend bool operator<(const Self a, const Self b) {
        return a.value_ < b.value_;
    }

    friend bool operator>=(const Self a, const Self b) {
        return a.value_ >= b.value_;
    }

    friend bool operator>(const Self a, const Self b) {
        return a.value_ > b.value_;
    }

    //-----------------
    // Math
    //-----------------

    Self operator+(const Self other) const { return Self{this->value_ + other.value_}; }

    // Adding a scalar does not change the unit
    Self operator+(const Storage scalar) const { return Self{this->value_ + scalar}; }

    Self operator-(const Self other) const { return Self{this->value_ - other.value_}; }

    // Subtracting a scalar does not change the unit
    Self operator-(const Storage scalar) const { return Self{this->value_ - scalar}; }

    Self operator*(const Self other) const { return Self{this->value_ * other.value_}; }

    // Multiplication by a scalar, the unit is retained: unit/1 = unit
    Self operator*(const Storage scalar) const { return Self{this->value_ * scalar}; }

    // Division returns raw number, the unit is lost: unit/unit = 1
    Storage operator/(const Self other) const { return Storage{this->value_ / other.value_}; }

    // Division by a raw number, the unit is retained: unit/1 = unit
    Self operator/(const Storage scalar) const { return Self{this->value_ / scalar}; }

    Self operator%(const Self other) const {
        return Self{this->value_ % other.value_};
    }

    //-----------------
    // Inline math
    //-----------------
    Self operator++(int) {
        ++value_;
        return *this;
    }

    Self operator--(int) {
        --value_;
        return *this;
    }

    Self operator+=(const Self other) {
        this->value_ += other.value_;
        return *this;
    }

    Self operator-=(const Self other) {
        this->value_ -= other.value_;
        return *this;
    }

    template<typename NEW_NUMERAL>
    NEW_NUMERAL recast() const { return NEW_NUMERAL{value_}; }

    // Take value casted to OUT_T
    template<typename OUT_T>
    [[nodiscard]] OUT_T as() const { return static_cast<OUT_T>(value_); }

    [[nodiscard]] constexpr STORAGE value() const { return value_; }

    [[nodiscard]] bool is_odd() const { return (value_ & 1) == 1; }

    // Value equals default value for the storage (zero)
    [[nodiscard]] bool is_zero() const { return value_ == STORAGE{}; }

    [[nodiscard]] explicit operator bool() const { return value_ != STORAGE{}; }

private:
    STORAGE value_;
};

template<typename T>
T clamp_above(const T value, const T max_value) {
    return value > max_value ? max_value : value;
}

template<typename T>
T clamp_below(const T value, const T min_value) {
    return value < min_value ? min_value : value;
}

template<typename T>
T clamp(const T value, const T min_value, const T max_value) {
    return clamp_above(clamp_below(value, min_value), max_value);
}

template<typename TYPE, typename UNIT, typename CHAR_TYPE>
struct std::formatter<Numeral<TYPE, UNIT>, CHAR_TYPE> : std::formatter<TYPE, CHAR_TYPE> {
//    constexpr auto parse(std::format_parse_context &ctx) {
//        return ctx.begin();
//    }

    template<class FormatContext>
    auto format(const Numeral<TYPE, UNIT> &obj, FormatContext &format_context) {
        return std::format_to(format_context.out(), NUM_FMT, obj.value());
        // return std::formatter<TYPE, CHAR_TYPE>::format(NUM_FMT, obj.value(), format_context);
    }
};
