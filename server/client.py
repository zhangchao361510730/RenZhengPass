import socket
import struct
import sys
import threading

# --- 服务器配置 ---
HOST = '127.0.0.1'  # 如果服务器在另一台机器上，请更改IP
PORT = 9998

# --- 消息协议 ---
# C++服务器定义的用于粘贴的消息类型
MSG_TYPE_TEXT_FOR_PASTE = 1
# 我们可以假定服务器发来的捕获文本使用类型2（即使服务器目前不发送类型）
MSG_TYPE_CAPTURED_TEXT = 2


def send_message(sock, msg_type, payload_str):
    """
    构建并发送符合协议的消息。
    这个函数保持不变，因为它完美地封装了发送逻辑。
    """
    try:
        # 将字符串编码为 UTF-8
        payload_bytes = payload_str.encode('utf-8')
        
        # 构建消息头: 1字节类型, 4字节长度 (网络字节序 >!)
        header = struct.pack('>BI', msg_type, len(payload_bytes))
        
        # 完整的消息 = 头 + 体
        message = header + payload_bytes
        
        sock.sendall(message)
        print(f"\n<-- [已发送] 类型: {msg_type}, 长度: {len(payload_bytes)}")
        print(f"<-- 内容: '{payload_str}'")
        print("> ", end="", flush=True) # 重新显示输入提示符

    except Exception as e:
        print(f"\n[错误] 发送失败: {e}")
        print("> ", end="", flush=True)

def listen_for_messages(sock):
    """
    在独立的线程中持续监听服务器发来的消息。
    这是本次修改的核心功能。
    """
    header_size = struct.calcsize('>BI') # 5 字节

    while True:
        try:
            # 1. 接收固定大小的消息头
            header_data = sock.recv(header_size)
            if not header_data:
                print("\n[信息] 与服务器的连接已断开。")
                break
            
            # 2. 解析消息头
            # 注意：我们假设服务器也会遵循相同的协议来发送消息
            msg_type, payload_len = struct.unpack('>BI', header_data)
            
            # 3. 根据长度接收消息体
            payload_bytes = sock.recv(payload_len)
            if not payload_bytes:
                print("\n[信息] 与服务器的连接已断开 (在接收消息体时)。")
                break

            received_text = payload_bytes.decode('utf-8')

            print(f"\n--> [已接收] 类型: {msg_type}, 长度: {payload_len}")
            print(f"--> 内容: '{received_text}'")

            # 4. 实现您的核心需求：处理并回发消息
            modified_text = f"333KLKLKL333{received_text}333KLKLKL333"
            print(f"[*] 正在处理并回发修改后的文本...")
            
            # 将修改后的文本发回给服务器，让服务器可以把它放入粘贴缓冲区
            send_message(sock, MSG_TYPE_TEXT_FOR_PASTE, modified_text)

        except ConnectionResetError:
            print("\n[信息] 与服务器的连接被重置。")
            break
        except Exception as e:
            print(f"\n[错误] 在监听时发生未知错误: {e}")
            break

if __name__ == "__main__":
    # 使用 with 语句确保 socket 在结束后总是被关闭
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            s.connect((HOST, PORT))
            print(f"成功连接到服务器 {HOST}:{PORT}")

            # 创建并启动监听线程
            # daemon=True 意味着当主线程退出时，这个线程也会被强制退出
            listener_thread = threading.Thread(target=listen_for_messages, args=(s,), daemon=True)
            listener_thread.start()
            
            print("客户端已启动。您可以：")
            print("1. 在服务器端按 Ctrl+Shift+H 捕获文本，客户端会自动接收并处理。")
            print("2. 在下方直接输入文本按 Enter 发送，以供服务器粘贴。")
            print("3. 输入 'exit' 退出程序。")

            # 主线程负责处理用户输入并发送
            while True:
                user_input = input("> ")
                if user_input.lower() == 'exit':
                    break
                if user_input:
                    send_message(s, MSG_TYPE_TEXT_FOR_PASTE, user_input)

        except ConnectionRefusedError:
            print(f"连接被拒绝。请确保C++服务器正在 {HOST}:{PORT} 上运行。")
        except KeyboardInterrupt:
            print("\n检测到 Ctrl+C，正在关闭客户端...")
        except Exception as e:
            print(f"\n发生未知错误: {e}")
        finally:
            print("客户端已关闭。")

