#!/usr/bin/env python3
"""
control.py — CAMS3 Harness 自动化测试
用法:
  python3 control.py <ip> info         # 系统状态
  python3 control.py <ip> snap         # 拍照
  python3 control.py <ip> reset        # 远程重启
  python3 control.py <ip> camdiag      # 摄像头诊断（含重试）
  python3 control.py <ip> auto         # 全自动测试
"""
import sys, json, time, subprocess, os

IP = sys.argv[1] if len(sys.argv) > 1 else "192.168.4.1"
CMD = sys.argv[2] if len(sys.argv) > 2 else "info"

def curl(url, timeout=5):
    """curl 封装"""
    try:
        r = subprocess.run(["curl", "-s", "--connect-timeout", str(timeout), url],
                          capture_output=True, text=True, timeout=timeout+2)
        return r.stdout, r.returncode
    except:
        return "", -1

def cmd_info():
    data, rc = curl(f"http://{IP}/info")
    if rc != 0 or not data:
        print(f"[FAIL] CAMS3 无响应 ({IP})")
        return False
    try:
        j = json.loads(data)
        print(f"📡 IP:      {j.get('ip','?')}")
        print(f"📷 Camera:  {j.get('camera','?')}")
        print(f"🧠 PSRAM:   {j.get('psram_kb','?')} KB")
        print(f"💾 Free:    {j.get('free_heap','?')} bytes")
        print(f"📊 Min:     {j.get('min_free_heap','?')} bytes")
        print(f"📶 WiFi:    {j.get('wifi_rssi','?')} dBm")
        print(f"⏱ Uptime:  {j.get('uptime_ms','?')} ms")
        print(f"🔬 Chip:    rev {j.get('chip_rev','?')}")
        return j.get('camera') == "OK"
    except:
        print(f"[FAIL] JSON 解析失败: {data[:100]}")
        return False

def cmd_snap():
    """拍照保存"""
    path = f"/tmp/cams3_snap.jpg"
    t0 = time.time()
    r = subprocess.run(["curl", "-s", "--connect-timeout", "8",
                        f"http://{IP}/snap", "-o", path],
                      capture_output=True, timeout=12)
    elapsed = time.time() - t0
    if r.returncode != 0 or not os.path.exists(path) or os.path.getsize(path) < 100:
        print(f"[FAIL] 拍照失败 ({elapsed:.1f}s)")
        return False
    size = os.path.getsize(path)
    print(f"[OK] 拍照成功: {size} bytes ({elapsed:.1f}s)")
    print(f"     MEDIA:{path}")
    return True

def cmd_camdiag():
    """摄像头诊断+重试"""
    data, rc = curl(f"http://{IP}/camdiag", timeout=15)
    if rc != 0:
        print(f"[FAIL] 无响应")
        return False
    print(f"诊断结果: {data[:200]}")
    return True

def cmd_reset():
    """远程重启"""
    data, _ = curl(f"http://{IP}/reset")
    print(f"重启: {data}")
    time.sleep(10)
    print("等待启动...")
    return cmd_info()

def cmd_auto():
    """全自动测试"""
    tests = [
        ("📡 连接测试", lambda: curl("http://{IP}/")[1] == 0),
        ("📡 信息读取", lambda: cmd_info()),
        ("📶 WiFi 诊断", lambda: ("wifi_rssi" in curl(f"http://{IP}/info")[0])),
        ("📷 摄像头诊断", cmd_camdiag),
        ("📷 拍照测试", cmd_snap),
    ]
    passed = 0
    for name, fn in tests:
        print(f"\n{'='*40}\n[{name}]")
        ok = fn()
        if ok:
            print(f"  ✅ {name} 通过")
            passed += 1
        else:
            print(f"  ❌ {name} 失败")
    print(f"\n{'='*40}\n测试结果: {passed}/{len(tests)} 通过")
    return passed == len(tests)

actions = {
    "info":   cmd_info,
    "snap":   cmd_snap,
    "reset":  cmd_reset,
    "camdiag": cmd_camdiag,
    "auto":   cmd_auto,
}

if CMD in actions:
    actions[CMD]()
else:
    print(f"未知命令: {CMD}")
    print("支持: info, snap, reset, camdiag, auto")
