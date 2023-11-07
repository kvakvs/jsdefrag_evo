[![A Modern C++ Result Type](doc/feature-preview-banner.gif)](https://github.com/bitwizeshift/result/releases)

[![Ubuntu Build Status](https://github.com/bitwizeshift/result/workflows/Ubuntu/badge.svg?branch=master)](https://github.com/bitwizeshift/result/actions?query=workflow%3AUbuntu)
[![macOS Build Status](https://github.com/bitwizeshift/result/workflows/macOS/badge.svg?branch=master)](https://github.com/bitwizeshift/result/actions?query=workflow%3AmacOS)
[![Windows Build Status](https://github.com/bitwizeshift/result/workflows/Windows/badge.svg?branch=master)](https://github.com/bitwizeshift/result/actions?query=workflow%3AWindows)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/e163a49b3b2e4f1e953c32b7cbbb2f28)](https://www.codacy.com/gh/bitwizeshift/result/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=bitwizeshift/result&amp;utm_campaign=Badge_Grade)
[![Coverage Status](https://coveralls.io/repos/github/bitwizeshift/result/badge.svg?branch=master)](https://coveralls.io/github/bitwizeshift/result?branch=master)
[![Github Issues](https://img.shields.io/github/issues/bitwizeshift/result.svg)](http://github.com/bitwizeshift/result/issues)
<br>
[![Github Releases](https://img.shields.io/github/v/release/bitwizeshift/result.svg?include_prereleases)](https://github.com/bitwizeshift/result/releases)
[![GitHub Sponsors](https://img.shields.io/badge/GitHub-Sponsors-ff69b4)](https://github.com/sponsors/bitwizeshift)
<br>
[![Try online](https://img.shields.io/badge/try-online-blue.svg)](https://godbolt.org/z/qG11qK)

**Result** is a modern, simple, and light-weight error-handling alternative to
exceptions with a rich feature-set.

## Features

✔️ Offers a coherent, light-weight alternative to exceptions \
✔️ Compatible with <kbd>C++11</kbd> (with more features in <kbd>C++14</kbd> and <kbd>C++17</kbd>) \
✔️ Single-header, **header-only** solution -- easily drops into any project \
✔️ Zero overhead abstractions -- don't pay for what you don't use. \
✔️ No dependencies \
✔️ Support for value-type, reference-type, and `void`-type values in `result` \
✔️ Monadic composition functions like `map`, `flat_map`, and `map_error` for
easy functional use \
✔️ Optional support to disable all exceptions and rename the `cpp` `namespace` \
✔️ [Comprehensively unit tested](https://coveralls.io/github/bitwizeshift/result?branch=master) for both static
behavior and runtime validation \
✔️ [Incurs minimal cost when optimized](https://godbolt.org/z/TsonT1), especially for trivial types

Check out the [tutorial](doc/tutorial.md) to see what other features **Result**
offers.

If you're interested in how `cpp::result` deviates from `std::expected`
proposals, please see [this page](doc/deviations-from-proposal.md).

## Teaser

```cpp
enum class narrow_error{ none, loss_of_data };

template <typename To, typename From>
auto try_narrow(const From& from) noexcept -> cpp::result<To,narrow_error>
{
  const auto to = static_cast<To>(from);

  if (static_cast<From>(to) != from) {
    return cpp::fail(narrow_error::loss_of_data);
  }

  return to;
}

struct {
  template <typename T>
  auto operator()(const T& x) -> std::string {
    return std::to_string(x);
  }
} to_string;

auto main() -> int {
  assert(try_narrow<std::uint8_t>(42LL).map(to_string) == "42");
}
```

<kbd>[Try online](https://godbolt.org/z/448vf9)</kbd>

## Quick References

* [🔍 Why `result`?](#why-result) \
  A background on the problem **Result** solves
* [💾 Installation](doc/installing.md) \
  For a quick guide on how to install/use this in other projects
* [📚 Tutorial](doc/tutorial.md) \
  A quick pocket-guide to using **Result**
* [📄 API Reference](https://bitwizeshift.github.io/result/api/latest/) \
  For doxygen-generated API information
* [🚀 Contributing](.github/CONTRIBUTING.md) \
  How to contribute to the **Result** project
* [💼 Attribution](doc/legal.md) \
  Information about how to attribute this project
* [❓ FAQ](doc/faq.md) \
  A list of frequently asked questions

## Why `result`?

Error cases in C++ are often difficult to discern from the API. Any function
not marked `noexcept` can be assumed to throw an exception, but the exact _type_
of exception, and if it even derives from `std::exception`, is ambiguous.
Nothing in the language forces which exceptions may propagate from an API, which
can make dealing with such APIs complicated.

Often it is more desirable to achieve `noexcept` functions where possible, since
this allows for better optimizations in containers (e.g. optimal moves/swaps)
and less cognitive load on consumers.

Having a `result<T, E>` type on your API not only semantically encodes that
a function is _able lcn_to_ fail, it also indicates to the caller _how_ the function
may fail, and what discrete, testable conditions may cause it to fail -- which
is what this library intends to solve.

As a simple example, compare these two identical functions:

```cpp
// (1)
auto to_uint32(const std::string& x) -> std::uint32_t;

// (2)
enum class parse_error { overflow=1, underflow=2, bad_input=3 };
auto to_uint32(const std::string& x) noexcept -> result<std::uint32_t, parse_error>;
```

In `(1)`, it is ambiguous _what_ (if anything) this function may throw on
failure, or how this error case may be accounted for.

In `(2)`, on the other hand, it is explicit that `to_uint32` _cannot_ throw --
so there is no need for a `catch` handler. It's also clear that it may fail for
whatever reasons are in `parse_error`, which discretely enumerates any possible
case for failure.

## Compiler Compatibility

**Result** is compatible with any compiler capable of compiling valid
<kbd>C++11</kbd>. Specifically, this has been tested and is known to work
with:

* GCC 5, 6, 7, 8, 9, 10
* Clang 3.5, 3.6, 3.7, 3.8, 3.9, 4, 5, 6, 7, 8, 9, 10, 11
* Apple Clang (Xcode) 10.3, 11.2, 11.3, 12.3
* Visual Studio 2017<sup>[1]</sup>, 2019

Latest patch level releases are assumed in the versions listed above.

**Note:** Visual Studios 2015 is not currently supported due to an internal
compiler error experienced in the default constructor of `result`. Support for
this will be added at a later time.

<sup>[1] Visual Studios 2017 is officially supported, though toolchain 14.16
has some issues properly compiling `map_error` due to insufficient support for
SFINAE.</sup>

## License

<img align="right" src="http://opensource.org/trademarks/opensource/OSI-Approved-License-100x137.png">

**Result** is licensed under the
[MIT License](http://opensource.org/licenses/MIT):

> Copyright &copy; 2017-2021 Matthew Rodusek
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## References

* [P0323R9](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0323r9.html):
  `std::expected` proposal was used as an inspiration for the general template
  structure.
* [bit::stl](https://github.com/bitwizeshift/bit-stl/blob/20f41988d64e1c4820175e32b4b7478bcc3998b7/include/bit/stl/utilities/expected.hpp):
  the original version that seeded this repository, based off an earlier proposal version.