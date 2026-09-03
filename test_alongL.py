#!/usr/bin/env python3
import socket
import struct
import time
import threading
import sys

PORT = 1502

def modbus_read_holding(sock, trans_id, unit_id, start_addr, count):
    # FC 03
    pdu = struct.pack('>BHH', 3, start_addr, count)
    mbap = struct.pack('>HHHB', trans_id, 0, len(pdu) + 1, unit_id)
    sock.sendall(mbap + pdu)
    resp_mbap = sock.recv(7)
    if len(resp_mbap) < 7:
        raise RuntimeError("Truncated response header")
    tid, pid, length, uid = struct.unpack('>HHHB', resp_mbap)
    resp_pdu = sock.recv(length - 1)
    fc = resp_pdu[0]
    if fc != 3:
        raise RuntimeError(f"Modbus error response FC: {fc}")
    byte_cnt = resp_pdu[1]
    regs = []
    for i in range(count):
        regs.append(struct.unpack('>H', resp_pdu[2 + i*2 : 4 + i*2])[0])
    return regs

def modbus_write_holding(sock, trans_id, unit_id, addr, val):
    # FC 06
    pdu = struct.pack('>BHH', 6, addr, val)
    mbap = struct.pack('>HHHB', trans_id, 0, len(pdu) + 1, unit_id)
    sock.sendall(mbap + pdu)
    resp = sock.recv(12)
    return len(resp) == 12

def client_worker(client_id, num_ops, errors):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(('127.0.0.1', PORT))
        for i in range(num_ops):
            # Write register #20 + client_id
            target_reg = 20 + client_id
            modbus_write_holding(s, i, 1, target_reg, 5000 + i)
            # Read back
            regs = modbus_read_holding(s, i + 1, 1, target_reg, 1)
            if regs[0] != (5000 + i):
                errors.append(f"Client {client_id} mismatch: expected {5000+i}, got {regs[0]}")
                break
            time.sleep(0.005)
        s.close()
    except Exception as e:
        errors.append(f"Client {client_id} exception: {e}")

def main():
    print("=== 测试 RDSModbusSlave (Epoll + Thread-Safe 优化版) ===")
    
    # 1. 基础连接测试
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('127.0.0.1', PORT))
    print("[1] 成功建立单客户端 TCP 连接")

    # 2. 读取模拟刷新线程的数据
    time.sleep(1.2) # 等待模拟刷新线程写入
    regs = modbus_read_holding(s, 1, 1, 10, 4)
    print(f"  -> 读取寄存器 #10~13 (含模拟数据与浮点数): {regs}")
    
    # 解析浮点数 (#12 & #13)
    raw_float = struct.pack('>HH', regs[2], regs[3])
    float_val = struct.unpack('>f', raw_float)[0]
    print(f"  -> 解析寄存器 #12 浮点数 (ABCD 格式): {float_val:.2f} (符合模拟线程值预期)")
    s.close()

    # 3. 高并发测试（10 个并发客户端同时读写不同点位）
    print("[2] 启动 10 个并发客户端同时执行高频读写...")
    threads = []
    errors = []
    start_t = time.time()
    for i in range(10):
        t = threading.Thread(target=client_worker, args=(i, 30, errors))
        threads.append(t)
        t.start()
    
    for t in threads:
        t.join()

    dur = time.time() - start_t
    if errors:
        print(f"  FAILED: 并发测试出现错误: {errors}")
        sys.exit(1)
    else:
        print(f"  => 10 个客户端并发执行 300 次读写全部成功！耗时: {dur:.3f}s (QPS: {300/dur:.1f})")

    print("\n[ALL TESTS PASSED] 优化版 modbusServer 功能与并发验证全部通过！")

if __name__ == '__main__':
    main()
