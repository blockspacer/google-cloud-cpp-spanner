// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "google/cloud/spanner/bytes.h"
#include "google/cloud/status.h"
#include <array>
#include <climits>

namespace google {
namespace cloud {
namespace spanner {
inline namespace SPANNER_CLIENT_NS {

namespace {

constexpr char kPadding = '=';

// The extra braces are working around an old clang bug that was fixed in 6.0
// https://bugs.llvm.org/show_bug.cgi?id=21629
constexpr std::array<char, 64> kIndexToChar = {{
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/',
}};

// The extra braces are working around an old clang bug that was fixed in 6.0
// https://bugs.llvm.org/show_bug.cgi?id=21629
constexpr std::array<unsigned char, UCHAR_MAX + 1> kCharToIndexExcessOne = {{
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  63, 0,  0,  0,  64, 53, 54, 55, 56, 57, 58,
    59, 60, 61, 62, 0,  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  6,  7,
    8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
    26, 0,  0,  0,  0,  0,  0,  27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
    38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
}};

// UCHAR_MAX is required to be at least 255, meaning std::string::value_type
// can always hold an octet. If UCHAR_MAX > 255, however, we have no way to
// base64 encode large values. So, we demand exactly 255.
static_assert(UCHAR_MAX == 255, "required by base64 encoder");

}  // namespace

void Bytes::Encoder::Flush() {
  unsigned int const v = buf_[0] << 16 | buf_[1] << 8 | buf_[2];
  rep_.push_back(kIndexToChar[v >> 18]);
  rep_.push_back(kIndexToChar[v >> 12 & 0x3f]);
  rep_.push_back(kIndexToChar[v >> 6 & 0x3f]);
  rep_.push_back(kIndexToChar[v & 0x3f]);
  len_ = 0;
}

void Bytes::Encoder::FlushAndPad() {
  switch (len_) {
    case 2: {
      unsigned int const v = buf_[0] << 16 | buf_[1] << 8;
      rep_.push_back(kIndexToChar[v >> 18]);
      rep_.push_back(kIndexToChar[v >> 12 & 0x3f]);
      rep_.push_back(kIndexToChar[v >> 6 & 0x3f]);
      rep_.push_back(kPadding);
      break;
    }
    case 1: {
      unsigned int const v = buf_[0] << 16;
      rep_.push_back(kIndexToChar[v >> 18]);
      rep_.push_back(kIndexToChar[v >> 12 & 0x3f]);
      rep_.append(2, kPadding);
      break;
    }
  }
}

void Bytes::Decoder::Iterator::Fill() {
  if (pos_ != end_) {
    unsigned char p0 = *pos_++;
    unsigned char p1 = *pos_++;
    unsigned char p2 = *pos_++;
    unsigned char p3 = *pos_++;
    auto i0 = kCharToIndexExcessOne[p0] - 1;
    auto i1 = kCharToIndexExcessOne[p1] - 1;
    if (p3 == kPadding) {
      if (p2 == kPadding) {
        buf_[++len_] = i0 << 2 | i1 >> 4;
      } else {
        auto i2 = kCharToIndexExcessOne[p2] - 1;
        buf_[++len_] = i1 << 4 | i2 >> 2;
        buf_[++len_] = i0 << 2 | i1 >> 4;
      }
    } else {
      auto i2 = kCharToIndexExcessOne[p2] - 1;
      auto i3 = kCharToIndexExcessOne[p3] - 1;
      buf_[++len_] = i2 << 6 | i3;
      buf_[++len_] = i1 << 4 | i2 >> 2;
      buf_[++len_] = i0 << 2 | i1 >> 4;
    }
  }
}

namespace internal {

// Construction from a base64-encoded US-ASCII `std::string`.
StatusOr<Bytes> BytesFromBase64(std::string input) {
  auto* p = reinterpret_cast<unsigned char const*>(input.data());
  auto* ep = p + input.size();
  while (ep - p >= 4) {
    auto i0 = kCharToIndexExcessOne[p[0]];
    auto i1 = kCharToIndexExcessOne[p[1]];
    if (--i0 >= 64 || --i1 >= 64) break;
    if (p[3] == kPadding) {
      if (p[2] == kPadding) {
        if ((i1 & 0xf) != 0) break;
      } else {
        auto i2 = kCharToIndexExcessOne[p[2]];
        if (--i2 >= 64 || (i2 & 0x3) != 0) break;
      }
      p += 4;
      break;
    }
    auto i2 = kCharToIndexExcessOne[p[2]];
    auto i3 = kCharToIndexExcessOne[p[3]];
    if (--i2 >= 64 || --i3 >= 64) break;
    p += 4;
  }
  if (p != ep) {
    auto const offset = reinterpret_cast<char const*>(p) - input.data();
    auto const bad_chunk = input.substr(offset, 4);
    auto message = "Invalid base64 chunk \"" + bad_chunk + "\"" +
                   " at offset " + std::to_string(offset);
    return Status(StatusCode::kInvalidArgument, std::move(message));
  }
  Bytes bytes;
  bytes.base64_rep_ = std::move(input);
  return bytes;
}

// Conversion to a base64-encoded US-ASCII `std::string`.
std::string BytesToBase64(Bytes b) { return std::move(b.base64_rep_); }

}  // namespace internal
}  // namespace SPANNER_CLIENT_NS
}  // namespace spanner
}  // namespace cloud
}  // namespace google
