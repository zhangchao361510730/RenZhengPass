#include "TextCaptureServer.hpp"
#include <iostream>

int main() {
    try {
        // 配置并创建服务实例
        TextCaptureServer app("/home/jojo/4Worker/", 9998);
        // 运行服务
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "程序发生致命错误: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}