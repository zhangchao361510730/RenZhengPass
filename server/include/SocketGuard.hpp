#pragma once

#include <unistd.h>

// RAII 包装器，用于自动关闭socket文件描述符
class SocketGuard {
public:
    explicit SocketGuard(int fd) : fd_(fd) {}
    ~SocketGuard() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }
    // 禁止拷贝构造和赋值
    SocketGuard(const SocketGuard&) = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;

private:
    int fd_;
};