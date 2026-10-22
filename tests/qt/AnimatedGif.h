#pragma once
#include <QByteArray>
// Two 1x1 frames, red then blue, with a looping NETSCAPE animation extension.
inline QByteArray animatedGif() {
    return QByteArray::fromHex(
        "47494638396101000100800000ff00000000ff"
        "21ff0b4e45545343415045322e300301000000"
        "21f904080a0000002c0000000001000100000202440100"
        "21f904080a0000002c00000000010001000002024c01003b");
}
