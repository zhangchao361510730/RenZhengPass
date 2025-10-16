#pragma once

#include "MessageProtocol.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory> // <<< 新增：为了 std::shared_ptr
#include <condition_variable> // <<< 新增：为了条件变量
#include <thread>             // <<< 新增：为了 std::thread

class TextCaptureServer {
public:
    TextCaptureServer(std::string savePath, int port);
    ~TextCaptureServer(); // <<< 新增：析构函数，确保线程被正确加入
    void run();

private:
    // --- 成员方法 ---
    void serverTask();
    void hotkeyListenerTask();
    void handleClient(int clientSocket);
    void stop();

    void triggerCaptureAction(); // <<< 修改：原 processCaptureAction 现在只作为触发器
    void captureWorkerTask();    // <<< 新增：专门处理捕获的工作线程函数
    void performPasteAction();

    bool sendMessage(int clientSocket, MessageProtocol::MessageType type, const std::string& payload);

    static std::string getSelectedText();
    static std::string escape_shell_arg(const std::string& arg);

    // --- 成员变量 ---
    const std::string SAVE_PATH;
    const int PORT;
    std::atomic<bool> keepRunning_;
    long fileCounter_;
    int serverFd_;

    std::shared_ptr<const std::string> pasteBufferPtr_;

    std::mutex clientsMutex_;
    std::vector<int> activeClients_;

    // <<< 新增：用于捕获工作线程的同步原语 >>>
    std::thread captureWorkerThread_;
    std::atomic<bool> capture_request_pending_{false};
    std::condition_variable capture_cv_;
    std::mutex capture_mutex_;
    // <<< ======================================== >>>
};