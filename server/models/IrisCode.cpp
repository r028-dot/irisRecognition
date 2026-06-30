#include "IrisCode.h"
#include <stdexcept>

// סריאליזציה: bits[256] || mask[256] → 512 בתים
vector<uint8_t> IrisCode::toBytes() const
{
    vector<uint8_t> v(512);
    memcpy(v.data(),       bits, 256);
    memcpy(v.data() + 256, mask, 256);
    return v;
}

// דה-סריאליזציה מ-512 בתים
IrisCode IrisCode::fromBytes(const uint8_t* data, size_t size)
{
    if (size < 512)
        throw runtime_error("IrisCode::fromBytes: need 512 bytes");
    IrisCode code;
    memcpy(code.bits, data,       256);
    memcpy(code.mask, data + 256, 256);
    return code;
}
