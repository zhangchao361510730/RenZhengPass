#pragma once

#include <cstdint>

namespace MessageProtocol {
    // 消息类型
    enum class MessageType : uint8_t {
        TEXT_FOR_PASTE = 1,
        CAPTURED_TEXT = 2,
    };

    // 消息头结构 (5字节)
    #pragma pack(push, 1) // 确保编译器不会添加内存对齐填充
    struct Header {
        MessageType type;
        uint32_t length; // 预期为网络字节序
    };
    #pragma pack(pop)
}