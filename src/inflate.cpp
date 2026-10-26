#include "inflate.h"

#include <cstdint>

namespace inflate {
namespace {

// bit reader: deflate packs its codes starting at the lowest bit of each byte
struct Bits {
    const unsigned char* p;
    size_t size, pos = 0;
    uint32_t buf = 0;
    int count = 0;
    bool bad = false;

    int Get(int need) {   // need <= 16
        uint32_t v = buf;
        while (count < need) {
            if (pos >= size) { bad = true; return 0; }
            v |= (uint32_t)p[pos++] << count;
            count += 8;
        }
        buf = v >> need;
        count -= need;
        return (int)(v & ((1u << need) - 1));
    }
};

// canonical Huffman code: number of codes per length + the symbols ordered by code
struct Huffman {
    short count[16];
    short symbol[288];
};

// < 0: over-subscribed, 0: complete, > 0: incomplete
int Build(Huffman& h, const short* lengths, int n) {
    for (short& c : h.count) c = 0;
    for (int s = 0; s < n; ++s) ++h.count[lengths[s]];
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len < 16; ++len) {
        left = (left << 1) - h.count[len];
        if (left < 0) return left;
    }
    short offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; ++len) offs[len + 1] = (short)(offs[len] + h.count[len]);
    for (int s = 0; s < n; ++s)
        if (lengths[s]) h.symbol[offs[lengths[s]]++] = (short)s;
    return left;
}

int Decode(Bits& b, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= b.Get(1);
        if (b.bad) return -1;
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

const short kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                            31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,    65,    97,    129,
                             193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const short kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool Codes(Bits& b, std::string& out, const Huffman& lens, const Huffman& dists) {
    for (;;) {
        int sym = Decode(b, lens);
        if (sym < 0) return false;
        if (sym < 256) {
            out += (char)sym;
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            const size_t len = (size_t)(kLenBase[sym] + b.Get(kLenExtra[sym]));
            const int d = Decode(b, dists);
            if (d < 0 || d >= 30) return false;
            const size_t dist = (size_t)(kDistBase[d] + b.Get(kDistExtra[d]));
            if (b.bad || dist > out.size()) return false;
            const size_t from = out.size() - dist;   // may overlap the bytes being written
            for (size_t i = 0; i < len; ++i) out += out[from + i];
        }
    }
}

bool Stored(Bits& b, std::string& out) {
    b.buf = 0;   // the rest of the current byte is skipped
    b.count = 0;
    if (b.pos + 4 > b.size) return false;
    const unsigned len = b.p[b.pos] | (unsigned)b.p[b.pos + 1] << 8;
    const unsigned inv = b.p[b.pos + 2] | (unsigned)b.p[b.pos + 3] << 8;
    b.pos += 4;
    if ((len ^ 0xFFFFu) != inv || b.pos + len > b.size) return false;
    out.append(reinterpret_cast<const char*>(b.p + b.pos), len);
    b.pos += len;
    return true;
}

bool Fixed(Bits& b, std::string& out) {
    static Huffman lens, dists;
    static bool built = false;
    if (!built) {   // (first use: from one thread, under the caller's lock)
        short l[288];
        for (int s = 0; s < 288; ++s) l[s] = s < 144 ? 8 : s < 256 ? 9 : s < 280 ? 7 : 8;
        Build(lens, l, 288);
        for (short& x : l) x = 5;
        Build(dists, l, 30);
        built = true;
    }
    return Codes(b, out, lens, dists);
}

bool Dynamic(Bits& b, std::string& out) {
    static const short kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int nlen = b.Get(5) + 257, ndist = b.Get(5) + 1, ncode = b.Get(4) + 4;
    if (b.bad || nlen > 286 || ndist > 30) return false;
    short lengths[316] = {0};
    for (int i = 0; i < ncode; ++i) lengths[kOrder[i]] = (short)b.Get(3);
    Huffman lencode, lens, dists;
    if (b.bad || Build(lencode, lengths, 19) != 0) return false;
    for (int index = 0; index < nlen + ndist;) {
        const int sym = Decode(b, lencode);
        if (sym < 0) return false;
        if (sym < 16) {
            lengths[index++] = (short)sym;
            continue;
        }
        short len = 0;
        int repeat;
        if (sym == 16) {
            if (index == 0) return false;
            len = lengths[index - 1];
            repeat = 3 + b.Get(2);
        } else {
            repeat = sym == 17 ? 3 + b.Get(3) : 11 + b.Get(7);
        }
        if (b.bad || index + repeat > nlen + ndist) return false;
        while (repeat--) lengths[index++] = len;
    }
    if (lengths[256] == 0) return false;   // no end-of-block code
    int err = Build(lens, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lens.count[0] != 1)) return false;   // only a single code may be incomplete
    err = Build(dists, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - dists.count[0] != 1)) return false;
    return Codes(b, out, lens, dists);
}

uint32_t Crc32(const std::string& s) {
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : s) {
        crc ^= c;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

uint32_t Le32(const unsigned char* p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

}  // namespace

bool Gunzip(const unsigned char* data, size_t size, std::string& out) {
    out.clear();
    if (!data || size < 18 || data[0] != 0x1F || data[1] != 0x8B || data[2] != 8) return false;
    const unsigned flags = data[3];
    size_t pos = 10;
    if (flags & 4) {   // FEXTRA
        if (pos + 2 > size) return false;
        pos += 2 + (data[pos] | (size_t)data[pos + 1] << 8);
    }
    for (unsigned f : {8u, 16u})   // FNAME, FCOMMENT: zero-terminated
        if (flags & f) {
            while (pos < size && data[pos]) ++pos;
            ++pos;
        }
    if (flags & 2) pos += 2;   // FHCRC
    if (pos >= size) return false;

    Bits b{data, size - 8};   // the last 8 bytes: CRC-32 and length
    b.pos = pos;
    bool last = false;
    while (!last) {
        last = b.Get(1) == 1;
        const int type = b.Get(2);
        bool ok = !b.bad;
        if (ok) ok = type == 0 ? Stored(b, out) : type == 1 ? Fixed(b, out) : type == 2 ? Dynamic(b, out) : false;
        if (!ok || b.bad) {
            out.clear();
            return false;
        }
    }
    const unsigned char* tail = data + size - 8;
    if (Le32(tail) != Crc32(out) || Le32(tail + 4) != (uint32_t)out.size()) {
        out.clear();
        return false;
    }
    return true;
}

}  // namespace inflate
