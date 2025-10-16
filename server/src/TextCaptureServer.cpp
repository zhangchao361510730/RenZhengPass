#include "TextCaptureServer.hpp"
#include "SocketGuard.hpp"

// C++ Standard Library
#include <iostream>
#include <fstream>
#include <thread>
#include <stdexcept>
#include <algorithm> // For std::remove

// System and Network Programming
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // For ntohl, htonl

// X11 Library
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

// Other
#include <climits> // For LONG_MAX

TextCaptureServer::TextCaptureServer(std::string savePath, int port)
    : SAVE_PATH(std::move(savePath)),
      PORT(port),
      keepRunning_(true),
      fileCounter_(0),
      serverFd_(-1),
      pasteBufferPtr_(std::make_shared<const std::string>(""))
{
    if (system(("mkdir -p " + SAVE_PATH).c_str()) != 0) {
        throw std::runtime_error("无法创建目录: " + SAVE_PATH);
    }
}

TextCaptureServer::~TextCaptureServer() {
    // 确保即使run()没有被完全执行，线程也能被安全处理
    if (captureWorkerThread_.joinable()) {
        stop();
        captureWorkerThread_.join();
    }
}

void TextCaptureServer::run() {
    std::cout << "程序启动中..." << std::endl;
    
    // 启动所有工作线程
    std::thread serverThread(&TextCaptureServer::serverTask, this);
    std::thread hotkeyThread(&TextCaptureServer::hotkeyListenerTask, this);
    captureWorkerThread_ = std::thread(&TextCaptureServer::captureWorkerTask, this); // 启动新的捕获工作线程

    std::cout << "程序正在运行... 按 Enter 键退出。" << std::endl;
    std::cin.get();

    stop();

    // 按顺序加入所有线程
    serverThread.join();
    hotkeyThread.join();
    captureWorkerThread_.join();

    std::cout << "程序已退出。" << std::endl;
}

void TextCaptureServer::stop() {
    keepRunning_ = false;
    
    // 通知捕获工作线程退出
    capture_cv_.notify_one();

    if (serverFd_ != -1) {
        shutdown(serverFd_, SHUT_RDWR);
        close(serverFd_);
        serverFd_ = -1;
    }
}

void TextCaptureServer::hotkeyListenerTask() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        std::cerr << "[ERROR] 无法打开 X Display. 热键监听功能失效。" << std::endl;
        return;
    }

    Window root = DefaultRootWindow(display);
    
    KeyCode hKey = XKeysymToKeycode(display, XK_H);
    KeyCode jKey = XKeysymToKeycode(display, XK_J);
    unsigned int modifiers = ControlMask | ShiftMask;

    XGrabKey(display, hKey, modifiers, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, hKey, modifiers | Mod2Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, hKey, modifiers | LockMask, root, True, GrabModeAsync, GrabModeAsync);

    XGrabKey(display, jKey, modifiers, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, jKey, modifiers | Mod2Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, jKey, modifiers | LockMask, root, True, GrabModeAsync, GrabModeAsync);

    std::cout << "[INFO] 热键监听线程已启动。捕获(Ctrl+Shift+H), 粘贴(Ctrl+Shift+J)" << std::endl;

    XEvent ev;
    while (keepRunning_) {
        if (XPending(display)) {
            XNextEvent(display, &ev);
            if (ev.type == KeyPress && (ev.xkey.state & modifiers) == modifiers) {
                if (ev.xkey.keycode == hKey) {
                    // 这是现在的"生产者"，只做最少的工作：设置标志并通知
                    triggerCaptureAction();
                } else if (ev.xkey.keycode == jKey) {
                    performPasteAction();
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    XCloseDisplay(display);
    std::cout << "[INFO] 热键监听线程已停止。" << std::endl;
}

void TextCaptureServer::triggerCaptureAction() {
    // 这是一个非阻塞操作
    capture_request_pending_ = true;
    capture_cv_.notify_one(); // 唤醒工作线程
}

void TextCaptureServer::captureWorkerTask() {
    std::cout << "[INFO] 捕获工作线程已启动。" << std::endl;
    while (keepRunning_) {
        // 使用 unique_lock 和条件变量进行等待
        std::unique_lock<std::mutex> lock(capture_mutex_);
        capture_cv_.wait(lock, [this]{ 
            // 只有当有挂起请求或程序要求退出时才唤醒
            return capture_request_pending_.load() || !keepRunning_.load(); 
        });

        // 如果唤醒是为了退出，则直接返回
        if (!keepRunning_) {
            break;
        }

        // 消耗掉请求标志。这步是"去抖"的关键。
        capture_request_pending_ = false;
        
        // 解锁，以便在执行耗时操作时，热键线程可以继续设置新请求
        lock.unlock();

        // --- 执行实际的捕获和广播工作 ---
        std::cout << "[WORKER] 检测到捕获请求，开始处理..." << std::endl;
        std::string selectedText = getSelectedText();
        if (selectedText.empty()) {
            std::cout << "[WORKER][WARN] 没有选中的文本。" << std::endl;
            continue; // 继续下一次循环等待
        }

        // 保存文件 (可选)
        std::string filename = SAVE_PATH + "capture_" + std::to_string(++fileCounter_) + ".txt";
        std::ofstream outFile(filename);
        if (outFile.is_open()) {
            outFile << selectedText;
        }

        // 广播给客户端
        std::vector<int> deadClients;
        {
            std::lock_guard<std::mutex> clientLock(clientsMutex_);
            for (int clientSocket : activeClients_) {
                if (!sendMessage(clientSocket, MessageProtocol::MessageType::CAPTURED_TEXT, selectedText)) {
                    deadClients.push_back(clientSocket);
                }
            }
        }
        
        if (!deadClients.empty()) {
            std::lock_guard<std::mutex> clientLock(clientsMutex_);
            for (int deadSocket : deadClients) {
                activeClients_.erase(std::remove(activeClients_.begin(), activeClients_.end(), deadSocket), activeClients_.end());
            }
        }
        std::cout << "[WORKER] 捕获和广播任务完成。" << std::endl;
    }
    std::cout << "[INFO] 捕获工作线程已停止。" << std::endl;
}

void TextCaptureServer::performPasteAction() {
    std::cout << "[EVENT] 检测到粘贴热键 Ctrl+Shift+J!" << std::endl;
    
    // 步骤1: 原子性地加载最新的缓冲区指针
    auto bufferToPaste = std::atomic_load(&pasteBufferPtr_);

    if (!bufferToPaste || bufferToPaste->empty()) {
        std::cout << "[WARN] 粘贴缓冲区为空，无内容可粘贴。" << std::endl;
        return;
    }
    
    std::string textToPaste = *bufferToPaste;

    // 步骤2: 使用 xclip 将文本放入剪贴板
    std::string command = "echo -n " + escape_shell_arg(textToPaste) + " | xclip -selection clipboard";
    if(system(command.c_str()) != 0) {
        std::cerr << "[ERROR] 执行 xclip 命令失败。请确保 xclip 已安装。" << std::endl;
        return;
    }
    std::cout << "[INFO] 文本已成功放入剪贴板。" << std::endl;

    // 步骤3: 模拟 Ctrl+V 粘贴
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        std::cerr << "[ERROR] 无法打开 Display 以模拟粘贴。" << std::endl;
        return;
    }
    KeyCode ctrlKey = XKeysymToKeycode(display, XK_Control_L);
    KeyCode vKey = XKeysymToKeycode(display, XK_V);
    
    XTestFakeKeyEvent(display, ctrlKey, True, 0);
    XTestFakeKeyEvent(display, vKey, True, 0);
    XTestFakeKeyEvent(display, vKey, False, 0);
    XTestFakeKeyEvent(display, ctrlKey, False, 0);
    
    XFlush(display);
    XCloseDisplay(display);
    
    std::cout << "[SUCCESS] 已模拟粘贴操作 (Ctrl+V)。" << std::endl;
}

void TextCaptureServer::serverTask() {
    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ == -1) { std::cerr << "[ERROR] Socket 创建失败" << std::endl; return; }

    SocketGuard serverGuard(serverFd_);
    int opt = 1;
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(serverFd_, (struct sockaddr *)&address, sizeof(address)) < 0) { std::cerr << "[ERROR] 端口 " << PORT << " 绑定失败" << std::endl; return; }
    if (listen(serverFd_, 5) < 0) { std::cerr << "[ERROR] 监听失败" << std::endl; return; }

    std::cout << "[SERVER] 服务器已在端口 " << PORT << " 启动，等待客户端连接..." << std::endl;

    while (keepRunning_) {
        int clientSocket = accept(serverFd_, nullptr, nullptr);
        if (clientSocket < 0) {
            if (!keepRunning_) break;
            std::cerr << "[ERROR] 接受连接失败" << std::endl;
            continue;
        }
        std::thread(&TextCaptureServer::handleClient, this, clientSocket).detach();
    }
    std::cout << "[INFO] 服务器线程已停止。" << std::endl;
}

void TextCaptureServer::handleClient(int clientSocket) {
    SocketGuard clientGuard(clientSocket);
    std::cout << "[SERVER] 客户端已连接 (Socket " << clientSocket << ")" << std::endl;
    
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        activeClients_.push_back(clientSocket);
    }

    while (keepRunning_) {
        MessageProtocol::Header header;
        ssize_t bytesRead = recv(clientSocket, &header, sizeof(header), MSG_WAITALL);
        if (bytesRead <= 0) break;

        if (bytesRead != sizeof(header)) {
             std::cerr << "[ERROR] 接收消息头不完整。" << std::endl;
             continue;
        }
        
        header.length = ntohl(header.length);

        if (header.type == MessageProtocol::MessageType::TEXT_FOR_PASTE) {
            if (header.length > 10 * 1024 * 1024) {
                std::cerr << "[ERROR] 消息体过大: " << header.length << " bytes" << std::endl;
                continue;
            }
            
            std::vector<char> body(header.length);
            bytesRead = recv(clientSocket, body.data(), header.length, MSG_WAITALL);
            
            if (bytesRead != static_cast<ssize_t>(header.length)) {
                std::cerr << "[ERROR] 接收消息体不完整。" << std::endl;
                continue;
            }
            
            auto newBuffer = std::make_shared<const std::string>(body.begin(), body.end());
            std::atomic_store(&pasteBufferPtr_, newBuffer);

            std::cout << "[SERVER] 成功接收 " << header.length << " 字节的文本用于粘贴 (已原子更新)。" << std::endl;

        } else {
            std::cerr << "[WARN] 收到未知消息类型: " << static_cast<int>(header.type) << std::endl;
            std::vector<char> dummy(header.length);
            recv(clientSocket, dummy.data(), header.length, MSG_WAITALL);
        }
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        activeClients_.erase(std::remove(activeClients_.begin(), activeClients_.end(), clientSocket), activeClients_.end());
    }
    std::cout << "[SERVER] 客户端 (Socket " << clientSocket << ") 断开连接，已从活动列表移除。" << std::endl;
}

bool TextCaptureServer::sendMessage(int clientSocket, MessageProtocol::MessageType type, const std::string& payload) {
    MessageProtocol::Header header;
    header.type = type;
    header.length = htonl(static_cast<uint32_t>(payload.size()));

    if (send(clientSocket, &header, sizeof(header), 0) < 0) return false;
    if (!payload.empty()) {
        if (send(clientSocket, payload.c_str(), payload.size(), 0) < 0) return false;
    }
    return true;
}

std::string TextCaptureServer::escape_shell_arg(const std::string& arg) {
    std::string escaped = "'";
    for (char c : arg) {
        if (c == '\'') {
            escaped += "'\\''";
        } else {
            escaped += c;
        }
    }
    escaped += "'";
    return escaped;
}

std::string TextCaptureServer::getSelectedText() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        std::cerr << "[ERROR] 无法打开 X Display。" << std::endl;
        return "";
    }
    Window root = DefaultRootWindow(display);
    Window owner = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    Atom selection = XA_PRIMARY;
    Atom target = XInternAtom(display, "UTF8_STRING", false);
    Atom property = XInternAtom(display, "CUSTOM_SELECTION_PROP", false);
    XConvertSelection(display, selection, target, property, owner, CurrentTime);
    XFlush(display);
    XEvent event;
    std::string result;
    auto startTime = std::chrono::steady_clock::now();
    while (true) {
        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count() > 2) {
            std::cerr << "[WARN] 获取选择区超时。" << std::endl;
            break;
        }
        if (XCheckTypedEvent(display, SelectionNotify, &event)) {
            if (event.xselection.property != None) {
                Atom actual_type;
                int actual_format;
                unsigned long nitems, bytes_after;
                unsigned char* data = nullptr;
                if (XGetWindowProperty(display, owner, property, 0, LONG_MAX, false, AnyPropertyType, &actual_type, &actual_format, &nitems, &bytes_after, &data) == Success && nitems > 0 && data) {
                    result.assign(reinterpret_cast<char*>(data), nitems);
                    XFree(data);
                }
            }
            break;
        }
    }
    XDestroyWindow(display, owner);
    XCloseDisplay(display);
    if (!result.empty()) {
        std::cout << "[SUCCESS] 成功捕获 " << result.length() << " 字节的文本。" << std::endl;
    }
    return result;
}

