#include "web_config.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// Web 服务器实例
static WebServer s_server(80);
static Preferences s_prefs;

// STA Wi-Fi 及系统网络缓存
static String s_sta_ssid = "";
static String s_sta_password = "";
static String s_sta_name = "";
static String s_mqtt_broker = "";
static int s_mqtt_port = 1883;
static String s_mqtt_user = "";
static String s_mqtt_pass = "";

// 传感器数据和水泵通道缓存结构
struct SensorCache {
    uint16_t raw_val;
    uint16_t filtered;
    uint16_t baseline;
    uint16_t threshold;
    bool detected;
};

struct RelayCache {
    bool active;
    int stage;
    bool pump_on;
    int on_count;
    bool detected;
};

static SensorCache s_sensor_cache[4] = {0};
static RelayCache s_relay_cache[3] = {0};

// ---------------- HTML 页面内容 ----------------
static const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>水泵网关监控与配置中心</title>
    <style>
        :root {
            --bg-color: #0b0f19;
            --card-bg: rgba(22, 30, 49, 0.7);
            --border-color: rgba(255, 255, 255, 0.08);
            --text-main: #f1f5f9;
            --text-muted: #64748b;
            --accent-blue: #38bdf8;
            --accent-cyan: #06b6d4;
            --accent-green: #10b981;
            --accent-orange: #f97316;
            --water-alert: #22d3ee;
            --dry-normal: #64748b;
            --nav-bg: rgba(17, 24, 39, 0.95);
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
        }

        body {
            background-color: var(--bg-color);
            color: var(--text-main);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
        }

        /* 顶部导航栏 */
        header {
            width: 100%;
            background: var(--nav-bg);
            border-bottom: 1px solid var(--border-color);
            padding: 1rem 1.5rem;
            display: flex;
            justify-content: space-between;
            align-items: center;
            position: sticky;
            top: 0;
            z-index: 100;
            backdrop-filter: blur(8px);
        }

        .logo {
            font-size: 1.15rem;
            font-weight: 700;
            letter-spacing: 0.5px;
            background: linear-gradient(135deg, var(--accent-blue), var(--accent-cyan));
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }

        .menu-btn {
            background: transparent;
            border: none;
            color: var(--text-main);
            font-size: 1.5rem;
            cursor: pointer;
            outline: none;
            width: 40px;
            height: 40px;
            display: flex;
            align-items: center;
            justify-content: center;
            border-radius: 8px;
            transition: background 0.2s;
        }

        .menu-btn:hover {
            background: rgba(255, 255, 255, 0.05);
        }

        /* 侧边菜单 */
        .drawer {
            position: fixed;
            top: 0;
            right: 0;
            width: 250px;
            height: 100%;
            background: #111827;
            border-left: 1px solid var(--border-color);
            box-shadow: -10px 0 30px rgba(0,0,0,0.5);
            transform: translateX(100%);
            transition: transform 0.3s cubic-bezier(0.4, 0, 0.2, 1);
            z-index: 200;
            padding: 2rem 1.5rem;
            display: flex;
            flex-direction: column;
            gap: 1.5rem;
        }

        .drawer.open {
            transform: translateX(0);
        }

        .drawer-close {
            align-self: flex-end;
            background: transparent;
            border: none;
            color: var(--text-muted);
            font-size: 1.5rem;
            cursor: pointer;
        }

        .drawer-close:hover {
            color: var(--text-main);
        }

        .nav-link {
            color: var(--text-muted);
            text-decoration: none;
            font-size: 1.1rem;
            font-weight: 500;
            transition: color 0.2s;
            cursor: pointer;
            padding: 0.5rem 0;
            border-bottom: 1px solid rgba(255,255,255,0.02);
        }

        .nav-link:hover, .nav-link.active {
            color: var(--accent-blue);
        }

        .overlay {
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0,0,0,0.5);
            z-index: 150;
            display: none;
        }

        .overlay.show {
            display: block;
        }

        /* 主容器 */
        .container {
            width: 100%;
            max-width: 900px;
            padding: 1.5rem 1rem;
            display: flex;
            flex-direction: column;
            gap: 1.5rem;
        }

        /* TAB 切换 */
        .tab-content {
            display: none;
        }

        .tab-content.active {
            display: block;
        }

        .card {
            background: var(--card-bg);
            backdrop-filter: blur(16px);
            border: 1px solid var(--border-color);
            border-radius: 16px;
            padding: 1.5rem;
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
            margin-bottom: 1.5rem;
        }

        .card-title {
            font-size: 1.15rem;
            font-weight: 600;
            margin-bottom: 1.25rem;
            border-bottom: 1px solid var(--border-color);
            padding-bottom: 0.5rem;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }

        .grid-3 {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
            gap: 1rem;
        }

        .grid-4 {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
            gap: 1rem;
        }

        /* 卡片内子项 */
        .status-item {
            background: rgba(10, 15, 28, 0.6);
            border: 1px solid var(--border-color);
            border-radius: 12px;
            padding: 1rem;
            display: flex;
            flex-direction: column;
            gap: 0.5rem;
            transition: all 0.3s;
            position: relative;
        }

        .status-item.active-run {
            border-color: var(--accent-green);
            box-shadow: inset 0 0 12px rgba(16, 185, 129, 0.1);
        }

        .status-item.water-detected {
            border-color: var(--accent-cyan);
            box-shadow: inset 0 0 12px rgba(6, 182, 212, 0.15);
        }

        .item-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 0.25rem;
        }

        .item-title {
            font-weight: 600;
            color: var(--accent-blue);
        }

        .badge-list {
            display: flex;
            gap: 0.3rem;
        }

        .badge {
            font-size: 0.72rem;
            padding: 0.15rem 0.45rem;
            border-radius: 9999px;
            font-weight: 700;
        }

        .badge.gray {
            background: rgba(100, 116, 139, 0.15);
            color: var(--text-muted);
        }

        .badge.blue {
            background: rgba(56, 189, 248, 0.15);
            color: var(--accent-blue);
        }

        .badge.green {
            background: rgba(16, 185, 129, 0.15);
            color: var(--accent-green);
        }

        .badge.orange {
            background: rgba(249, 115, 22, 0.15);
            color: var(--accent-orange);
        }

        .item-meta {
            font-size: 0.78rem;
            color: var(--text-muted);
            line-height: 1.5;
        }

        .item-meta span {
            color: var(--text-main);
            font-weight: 500;
        }

        /* 信号强度栏 */
        .info-bar {
            display: flex;
            justify-content: space-between;
            align-items: center;
            flex-wrap: wrap;
            gap: 1rem;
            font-size: 0.9rem;
        }

        .signal-indicator {
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }

        .signal-bar {
            display: inline-flex;
            gap: 2px;
            align-items: flex-end;
            height: 14px;
        }

        .signal-bar span {
            display: inline-block;
            width: 3px;
            background-color: rgba(255,255,255,0.2);
            border-radius: 1px;
        }

        .signal-bar span.active {
            background-color: var(--accent-green);
        }

        /* 表单 */
        .form-group {
            display: flex;
            flex-direction: column;
            gap: 0.5rem;
            margin-bottom: 1.25rem;
        }

        label {
            font-size: 0.88rem;
            font-weight: 500;
            color: var(--text-muted);
        }

        input {
            background: rgba(10, 15, 28, 0.8);
            border: 1px solid var(--border-color);
            color: var(--text-main);
            padding: 0.75rem;
            border-radius: 8px;
            font-size: 0.95rem;
            outline: none;
            width: 100%;
        }

        input:focus {
            border-color: var(--accent-blue);
        }

        .btn {
            background: linear-gradient(135deg, var(--accent-blue), var(--accent-cyan));
            color: #fff;
            border: none;
            padding: 0.8rem 1.5rem;
            border-radius: 8px;
            font-size: 0.95rem;
            font-weight: 600;
            cursor: pointer;
            transition: opacity 0.2s;
            box-shadow: 0 4px 12px rgba(56, 189, 248, 0.25);
            display: inline-flex;
            justify-content: center;
            align-items: center;
        }

        .btn:hover {
            opacity: 0.9;
        }

        #toast {
            position: fixed;
            bottom: 2rem;
            left: 50%;
            transform: translateX(-50%) translateY(200px);
            opacity: 0;
            visibility: hidden;
            background: rgba(16, 185, 129, 0.95);
            color: white;
            padding: 0.75rem 1.75rem;
            border-radius: 50px;
            font-weight: 600;
            box-shadow: 0 8px 24px rgba(16, 185, 129, 0.3);
            transition: transform 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275), opacity 0.2s, visibility 0.2s;
            pointer-events: none;
            z-index: 1000;
            font-size: 0.9rem;
        }

        #toast.show {
            transform: translateX(-50%) translateY(0);
            opacity: 1;
            visibility: visible;
        }

        .wifi-list {
            margin-top: 0.5rem;
            max-height: 150px;
            overflow-y: auto;
            border: 1px solid var(--border-color);
            border-radius: 8px;
            background: rgba(0, 0, 0, 0.2);
            display: none;
        }
        .wifi-list.show {
            display: block;
        }
        .wifi-item {
            padding: 0.6rem 1rem;
            border-bottom: 1px solid var(--border-color);
            cursor: pointer;
            display: flex;
            justify-content: space-between;
            font-size: 0.88rem;
            transition: background 0.2s;
        }
        .wifi-item:last-child {
            border-bottom: none;
        }
        .wifi-item:hover {
            background: rgba(255, 255, 255, 0.05);
        }
        .wifi-signal {
            color: var(--accent-blue);
        }
    </style>
</head>
<body>

    <header>
        <div class="logo">水泵控制网关监视中心</div>
        <button class="menu-btn" onclick="toggleDrawer(true)">☰</button>
    </header>

    <!-- 侧边菜单 -->
    <div class="overlay" id="overlay" onclick="toggleDrawer(false)"></div>
    <div class="drawer" id="drawer">
        <button class="drawer-close" onclick="toggleDrawer(false)">✕</button>
        <div style="height: 1rem;"></div>
        <div class="nav-link active" data-tab="tab-monitor" onclick="switchTab(this)">实时运行监控</div>
        <div class="nav-link" data-tab="tab-wifi" onclick="switchTab(this)">网络与系统配置</div>
        <div class="nav-link" data-tab="tab-about" onclick="switchTab(this)">关于设备</div>
    </div>

    <div class="container">
        <!-- 实时监控 TAB -->
        <div id="tab-monitor" class="tab-content active">
            <!-- 信号强度与连接信息 -->
            <div class="card" style="padding: 1rem 1.5rem; margin-bottom: 1.25rem;">
                <div class="info-bar">
                    <div>
                        站点名称: <span id="info-station" style="font-weight: 600; color: var(--accent-cyan); margin-right: 1.5rem;">dongzhan</span>
                        STA SSID: <span id="info-ssid" style="font-weight: 600; color: var(--accent-blue);">未连接</span>
                        <span id="info-ip" style="font-size: 0.85rem; color: var(--text-muted); margin-left: 0.5rem;"></span>
                    </div>
                    <div class="signal-indicator">
                        <span>信号强度:</span>
                        <span id="signal-dbm" style="font-weight: 500;">-- dBm</span>
                        <div class="signal-bar" id="signal-bars">
                            <span id="sbar1"></span>
                            <span id="sbar2"></span>
                            <span id="sbar3"></span>
                            <span id="sbar4"></span>
                        </div>
                    </div>
                </div>
            </div>

            <!-- 继电器/水泵通道列表 -->
            <div class="card">
                <div class="card-title">
                    <span>3 水泵通道控制状态</span>
                    <span style="font-size: 0.8rem; font-weight: normal; color: var(--text-muted);">状态机驱动 (10步法)</span>
                </div>
                <div class="grid-3" id="relay-grid">
                    <!-- JS 动态渲染 -->
                </div>
            </div>

            <!-- 传感器物理通道列表 -->
            <div class="card">
                <div class="card-title">
                    <span>4 通道输入传感器值</span>
                    <span style="font-size: 0.8rem; font-weight: normal; color: var(--text-muted);">自动更新(1Hz)</span>
                </div>
                <div class="grid-4" id="sensor-grid">
                    <!-- JS 动态渲染 -->
                </div>
            </div>
        </div>

        <!-- 系统与网络配置 TAB -->
        <div id="tab-wifi" class="tab-content">
            <div class="card">
                <div class="card-title">无线局域网连接 (Wi-Fi STA)</div>
                <form id="wifi-form" onsubmit="saveWifi(event)">
                    <div class="form-group">
                        <label for="ssid">Wi-Fi 网络名称 (SSID)</label>
                        <div style="display: flex; gap: 0.5rem;">
                            <input type="text" id="ssid" name="ssid" placeholder="请输入 WiFi SSID" style="flex: 1;" required>
                            <button type="button" class="btn" style="width: auto; padding: 0.5rem 1rem; font-size: 0.85rem;" onclick="scanWifi(this)">扫描</button>
                        </div>
                        <div id="wifi-list" class="wifi-list"></div>
                    </div>
                    <div class="form-group">
                        <label for="password">Wi-Fi 密码 (Password)</label>
                        <input type="password" id="password" name="password" placeholder="请输入密码">
                    </div>

                    <div style="margin: 1.5rem 0 1rem 0; border-top: 1px solid var(--border-color); padding-top: 1rem;">
                        <h4 style="font-size: 1rem; font-weight: 600; color: var(--accent-blue); margin-bottom: 0.75rem;">网关与 MQTT 配置</h4>
                    </div>

                    <div class="form-group">
                        <label for="name">设备站点标识 (STATION_NAME)</label>
                        <input type="text" id="name" name="name" placeholder="例如: dongzhan" required>
                    </div>
                    <div class="form-group">
                        <label for="broker">MQTT Broker 地址 (域名或 IP)</label>
                        <input type="text" id="broker" name="broker" placeholder="例如: voicevon.vicp.io" required>
                    </div>
                    <div class="form-group">
                        <label for="port">MQTT 端口 (Port)</label>
                        <input type="number" id="port" name="port" min="1" max="65535" placeholder="默认: 1883" required>
                    </div>
                    <div class="form-group">
                        <label for="user">MQTT 用户名 (Username)</label>
                        <input type="text" id="user" name="user" placeholder="可选，无则留空">
                    </div>
                    <div class="form-group">
                        <label for="pass_mqtt">MQTT 密码 (Password)</label>
                        <input type="password" id="pass_mqtt" name="pass_mqtt" placeholder="可选，无则留空">
                    </div>

                    <button type="submit" class="btn" style="width: 100%;">保存并应用配置</button>
                </form>
            </div>
        </div>

        <!-- 关于设备 TAB -->
        <div id="tab-about" class="tab-content">
            <div class="card">
                <div class="card-title">设备关于</div>
                <div style="line-height: 2; font-size: 0.95rem;">
                    <p>设备名称：<span style="color: var(--accent-blue);">water_brain</span></p>
                    <p>工作模式：Smart Gateway (MQTT / BLE)</p>
                    <p>固件版本：V2.1</p>
                    <p>软硬件设计：山东卷积分公司</p>
                    <p style="margin-top: 1.5rem; color: var(--text-muted); font-size: 0.85rem; border-top: 1px solid var(--border-color); padding-top: 0.75rem; text-align: center;">
                        版权所有 © 山东卷积分公司
                    </p>
                </div>
            </div>
        </div>
    </div>

    <div id="toast">保存成功，已应用并于后台重连！</div>

    <script>
        // 切换抽屉菜单
        function toggleDrawer(open) {
            document.getElementById('drawer').classList.toggle('open', open);
            document.getElementById('overlay').classList.toggle('show', open);
        }

        // 切 Tab
        function switchTab(el) {
            document.querySelectorAll('.nav-link').forEach(link => link.classList.remove('active'));
            el.classList.add('active');
            
            const targetId = el.getAttribute('data-tab');
            document.querySelectorAll('.tab-content').forEach(tab => tab.classList.remove('active'));
            document.getElementById(targetId).classList.add('active');
            
            toggleDrawer(false);

            if (targetId === 'tab-wifi') {
                fetchWifi();
            }
        }

        async function fetchWifi() {
            try {
                const res = await fetch('/api/sysconfig');
                const data = await res.json();
                document.getElementById('ssid').value = data.ssid || '';
                document.getElementById('password').value = data.pass || '';
                document.getElementById('name').value = data.name || 'dongzhan';
                document.getElementById('broker').value = data.broker || 'voicevon.vicp.io';
                document.getElementById('port').value = data.port || 1883;
                document.getElementById('user').value = data.user || '';
                document.getElementById('pass_mqtt').value = data.pass_mqtt || '';
            } catch (err) {
                console.error("Fetch system credentials error:", err);
            }
        }

        // 保存配置
        async function saveWifi(e) {
            e.preventDefault();
            const form = document.getElementById('wifi-form');
            const params = new URLSearchParams(new FormData(form));
            try {
                const res = await fetch('/api/sysconfig', { method: 'POST', body: params });
                if (res.ok) {
                    showToast();
                } else {
                    alert("保存配置失败！");
                }
            } catch (err) {
                alert("请求错误：" + err);
            }
        }

        function showToast() {
            const t = document.getElementById('toast');
            t.classList.add('show');
            setTimeout(() => t.classList.remove('show'), 3000);
        }

        // 扫描附近 WiFi
        async function scanWifi(btn) {
            const orig = btn.textContent;
            btn.textContent = "扫描中...";
            btn.disabled = true;
            const list = document.getElementById('wifi-list');
            list.innerHTML = '<div style="padding: 0.75rem 1rem; font-size: 0.85rem; color: var(--text-muted); text-align: center;">扫描附近 WiFi 热点中...</div>';
            list.classList.add('show');
            
            try {
                const res = await fetch('/api/scan');
                const data = await res.json();
                list.innerHTML = '';
                if (data.networks && data.networks.length > 0) {
                    data.networks.forEach(net => {
                        const div = document.createElement('div');
                        div.className = 'wifi-item';
                        div.innerHTML = `
                            <span>${net.ssid}</span>
                            <span class="wifi-signal">${net.rssi} dBm</span>
                        `;
                        div.onclick = () => {
                            document.getElementById('ssid').value = net.ssid;
                            list.classList.remove('show');
                        };
                        list.appendChild(div);
                    });
                } else {
                    list.innerHTML = '<div style="padding: 0.75rem 1rem; font-size: 0.85rem; color: var(--text-muted); text-align: center;">未找到可用的 WiFi 网络</div>';
                }
            } catch (err) {
                list.innerHTML = '<div style="padding: 0.75rem 1rem; font-size: 0.85rem; color: #ef4444; text-align: center;">扫描失败</div>';
            } finally {
                btn.textContent = orig;
                btn.disabled = false;
            }
        }

        // 渲染信号格
        function updateSignalStrength(rssi) {
            const dbmSpan = document.getElementById('signal-dbm');
            const bars = [
                document.getElementById('sbar1'),
                document.getElementById('sbar2'),
                document.getElementById('sbar3'),
                document.getElementById('sbar4')
            ];
            
            bars.forEach(b => b.classList.remove('active'));

            if (rssi === 0 || rssi < -100) {
                dbmSpan.textContent = "无信号";
                return;
            }

            dbmSpan.textContent = rssi + " dBm";
            
            bars[0].classList.add('active'); // 至少有 1 格
            if (rssi > -85) bars[1].classList.add('active');
            if (rssi > -70) bars[2].classList.add('active');
            if (rssi > -55) bars[3].classList.add('active');
        }

        // 核心轮询更新函数
        async function updateDashboard() {
            if (!document.getElementById('tab-monitor').classList.contains('active')) {
                return;
            }
            try {
                const res = await fetch('/api/data');
                const data = await res.json();

                // 1. 更新顶部连接信息与信号强度
                document.getElementById('info-ssid').textContent = data.ssid || "未连接";
                document.getElementById('info-station').textContent = data.station || "dongzhan";
                if (data.ip) {
                    document.getElementById('info-ip').textContent = `(IP: ${data.ip})`;
                } else {
                    document.getElementById('info-ip').textContent = "";
                }
                updateSignalStrength(data.rssi);

                // 2. 更新 3 个 Relay/水泵控制通道
                const relayGrid = document.getElementById('relay-grid');
                relayGrid.innerHTML = '';
                data.relays.forEach((r, idx) => {
                    const chDiv = document.createElement('div');
                    chDiv.className = `status-item ${r.active ? 'active-run' : ''}`;
                    chDiv.innerHTML = `
                        <div class="item-header">
                            <span class="item-title">水泵通道 #${r.id}</span>
                            <div class="badge-list">
                                <span class="badge ${r.active ? 'green' : 'gray'}">${r.active ? '采样中' : '空闲'}</span>
                                <span class="badge ${r.pump_on ? 'orange' : 'gray'}">${r.pump_on ? '水泵开' : '水泵关'}</span>
                            </div>
                        </div>
                        <div class="item-meta">
                            <div>当前阶段: <span>Stage ${r.stage}</span></div>
                            <div>累计开启: <span>${r.on_count} 次</span></div>
                            <div>感应状态: <span>${r.detected ? '<span style="color: var(--accent-cyan);">有水</span>' : '无水'}</span></div>
                        </div>
                    `;
                    relayGrid.appendChild(chDiv);
                });

                // 3. 更新 4 个传感器输入值
                const sensorGrid = document.getElementById('sensor-grid');
                sensorGrid.innerHTML = '';
                data.sensors.forEach((s, idx) => {
                    const isWater = s.detected;
                    const chDiv = document.createElement('div');
                    chDiv.className = `status-item ${isWater ? 'water-detected' : ''}`;
                    chDiv.innerHTML = `
                        <div class="item-header">
                            <span class="item-title">传感器 #${idx + 1}</span>
                            <span class="badge ${isWater ? 'blue' : 'gray'}">${isWater ? '有水' : '干燥'}</span>
                        </div>
                        <div class="item-meta">
                            <div>测得电容值: <span>${(s.raw / 100.0).toFixed(2)} pF</span></div>
                            <div style="margin-top: 4px; color: var(--text-muted);">状态字直接解码: <span>${isWater ? '触发有水' : '正常干燥'}</span></div>
                        </div>
                    `;
                    sensorGrid.appendChild(chDiv);
                });

            } catch (err) {
                console.error("Fetch dashboard data error:", err);
            }
        }

        // 定时轮询 (1 秒刷新)
        setInterval(updateDashboard, 1000);
        updateDashboard();
    </script>
</body>
</html>
)rawhtml";

// Web API - 实时网关及设备数据接口
static void handle_get_data() {
    String json = "{";
    // WiFi 信号与连接状态
    json += "\"station\":\"" + s_sta_name + "\",";
    json += "\"ssid\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "") + "\",";
    json += "\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "") + "\",";
    json += "\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) + ",";

    // 传感器输入状态缓存 (4路)
    json += "\"sensors\":[";
    for (int i = 0; i < 4; i++) {
        json += "{";
        json += "\"raw\":" + String(s_sensor_cache[i].raw_val) + ",";
        json += "\"detected\":" + String(s_sensor_cache[i].detected ? "true" : "false");
        json += "}";
        if (i < 3) json += ",";
    }
    json += "],";

    // 继电器/水泵通道状态缓存 (3路)
    json += "\"relays\":[";
    for (int i = 0; i < 3; i++) {
        json += "{";
        json += "\"id\":" + String(i + 1) + ",";
        json += "\"active\":" + String(s_relay_cache[i].active ? "true" : "false") + ",";
        json += "\"stage\":" + String(s_relay_cache[i].stage) + ",";
        json += "\"pump_on\":" + String(s_relay_cache[i].pump_on ? "true" : "false") + ",";
        json += "\"on_count\":" + String(s_relay_cache[i].on_count) + ",";
        json += "\"detected\":" + String(s_relay_cache[i].detected ? "true" : "false");
        json += "}";
        if (i < 2) json += ",";
    }
    json += "]";
    json += "}";
    
    s_server.send(200, "application/json", json);
}

// Web API - 获取系统与网络配置
static void handle_get_sysconfig() {
    String json = "{";
    json += "\"ssid\":\"" + s_sta_ssid + "\",";
    json += "\"pass\":\"" + s_sta_password + "\",";
    json += "\"name\":\"" + s_sta_name + "\",";
    json += "\"broker\":\"" + s_mqtt_broker + "\",";
    json += "\"port\":" + String(s_mqtt_port) + ",";
    json += "\"user\":\"" + s_mqtt_user + "\",";
    json += "\"pass_mqtt\":\"" + s_mqtt_pass + "\"";
    json += "}";
    s_server.send(200, "application/json", json);
}

// Web API - 更改系统与网络配置并保存
static void handle_post_sysconfig() {
    bool changed = false;
    if (s_server.hasArg("ssid")) {
        String val = s_server.arg("ssid");
        if (val.length() > 0 && val != s_sta_ssid) {
            s_sta_ssid = val;
            s_prefs.putString("sta_ssid", val);
            changed = true;
        }
    }
    if (s_server.hasArg("password")) {
        String val = s_server.arg("password");
        if (val != s_sta_password) {
            s_sta_password = val;
            s_prefs.putString("sta_pass", val);
            changed = true;
        }
    }
    if (s_server.hasArg("name")) {
        String val = s_server.arg("name");
        if (val.length() > 0 && val != s_sta_name) {
            s_sta_name = val;
            s_prefs.putString("sta_name", val);
            changed = true;
        }
    }
    if (s_server.hasArg("broker")) {
        String val = s_server.arg("broker");
        if (val.length() > 0 && val != s_mqtt_broker) {
            s_mqtt_broker = val;
            s_prefs.putString("mqtt_broker", val);
            changed = true;
        }
    }
    if (s_server.hasArg("port")) {
        int val = s_server.arg("port").toInt();
        if (val > 0 && val != s_mqtt_port) {
            s_mqtt_port = val;
            s_prefs.putInt("mqtt_port", val);
            changed = true;
        }
    }
    if (s_server.hasArg("user")) {
        String val = s_server.arg("user");
        if (val != s_mqtt_user) {
            s_mqtt_user = val;
            s_prefs.putString("mqtt_user", val);
            changed = true;
        }
    }
    if (s_server.hasArg("pass_mqtt")) {
        String val = s_server.arg("pass_mqtt");
        if (val != s_mqtt_pass) {
            s_mqtt_pass = val;
            s_prefs.putString("mqtt_pass", val);
            changed = true;
        }
    }

    if (changed) {
        Serial.println("[WebConfig] System & Network credentials updated to NVS.");
    }
    s_server.send(200, "text/plain", "OK");
}

// Web API - 扫描附近可用 WiFi 网络
static void handle_wifi_scan() {
    int n = WiFi.scanNetworks(false, false);
    String json = "{\"networks\":[";
    if (n > 0) {
        int indices[n];
        for (int i = 0; i < n; i++) indices[i] = i;
        for (int i = 0; i < n - 1; i++) {
            for (int j = i + 1; j < n; j++) {
                if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
                    int temp = indices[i];
                    indices[i] = indices[j];
                    indices[j] = temp;
                }
            }
        }

        int count = n < 15 ? n : 15;
        for (int i = 0; i < count; i++) {
            int idx = indices[i];
            json += "{";
            json += "\"ssid\":\"" + WiFi.SSID(idx) + "\",";
            json += "\"rssi\":" + String(WiFi.RSSI(idx));
            json += "}";
            if (i < count - 1) json += ",";
        }
    }
    json += "]}";
    WiFi.scanDelete();
    s_server.send(200, "application/json", json);
}

// 打印运行状态辅助函数
static void print_wifi_status(const char* label) {
    wifi_mode_t mode = WiFi.getMode();
    const char* mode_str = "UNKNOWN";
    if (mode == WIFI_OFF) mode_str = "OFF";
    else if (mode == WIFI_STA) mode_str = "STA";
    else if (mode == WIFI_AP) mode_str = "AP";
    else if (mode == WIFI_AP_STA) mode_str = "AP_STA";
    
    Serial.printf("[%s] Current WiFi Mode: %s, AP IP: %s, Station IP: %s, Station Status: %d\n",
                  label, mode_str, 
                  WiFi.softAPIP().toString().c_str(), 
                  WiFi.localIP().toString().c_str(),
                  WiFi.status());
}

void web_config_init() {
    // 1. 初始化 Preferences (NVS 存储)
    s_prefs.begin("sensor_map", false);

    // 从 NVS 读取参数，没有则使用 config.h 定义的缺省宏值
    s_sta_ssid = s_prefs.getString("sta_ssid", FACTORY_WIFI_SSID);
    s_sta_password = s_prefs.getString("sta_pass", FACTORY_WIFI_PASSWORD);
    s_sta_name = s_prefs.getString("sta_name", FACTORY_STATION_NAME);
    s_mqtt_broker = s_prefs.getString("mqtt_broker", FACTORY_MQTT_BROKER);
    s_mqtt_port = s_prefs.getInt("mqtt_port", FACTORY_MQTT_PORT);
    s_mqtt_user = s_prefs.getString("mqtt_user", FACTORY_MQTT_USERNAME);
    s_mqtt_pass = s_prefs.getString("mqtt_pass", FACTORY_MQTT_PASSWORD);

    Serial.printf("[WebConfig] Loaded WiFi STA SSID: %s\n", s_sta_ssid.c_str());
    Serial.printf("[WebConfig] Loaded Station Name: %s, MQTT Broker: %s:%d\n", s_sta_name.c_str(), s_mqtt_broker.c_str(), s_mqtt_port);

    // 2. 启动 WiFi AP_STA 混合模式，允许用户通过 AP (AP_Relay) 连接配置
    print_wifi_status("WebConfig BEFORE softAP");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(FACTORY_WIFI_AP_SSID, FACTORY_WIFI_AP_PASSWORD);
    print_wifi_status("WebConfig AFTER softAP");

    // 3. 挂载路由
    s_server.on("/", HTTP_GET, []() {
        s_server.send_P(200, "text/html", INDEX_HTML);
    });
    s_server.on("/api/data", HTTP_GET, handle_get_data);
    s_server.on("/api/sysconfig", HTTP_GET, handle_get_sysconfig);
    s_server.on("/api/sysconfig", HTTP_POST, handle_post_sysconfig);
    
    // 兼容旧接口
    s_server.on("/api/wifi", HTTP_GET, handle_get_sysconfig);
    s_server.on("/api/wifi", HTTP_POST, handle_post_sysconfig);
    s_server.on("/api/scan", HTTP_GET, handle_wifi_scan);

    s_server.begin();
    Serial.println("[WebConfig] Built-in Web Server started on port 80");
}

void web_config_loop() {
    if (WiFi.getMode() == WIFI_STA) {
        Serial.println("[WebConfig] WiFi mode was reverted to STA. Restoring AP_STA and restarting softAP...");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(FACTORY_WIFI_AP_SSID, FACTORY_WIFI_AP_PASSWORD);
        print_wifi_status("WebConfig RESTORED AP_STA");
    }
    s_server.handleClient();
}

String get_sta_ssid() {
    return s_sta_ssid;
}

String get_sta_password() {
    return s_sta_password;
}

String get_station_name() {
    return s_sta_name;
}

String get_mqtt_broker() {
    return s_mqtt_broker;
}

int get_mqtt_port() {
    return s_mqtt_port;
}

String get_mqtt_user() {
    return s_mqtt_user;
}

String get_mqtt_pass() {
    return s_mqtt_pass;
}

void web_config_update_sensor(int idx, uint16_t raw_val, uint16_t filtered, uint16_t baseline, uint16_t threshold, bool detected) {
    if (idx < 0 || idx >= 4) return;
    s_sensor_cache[idx].raw_val = raw_val;
    s_sensor_cache[idx].filtered = filtered;
    s_sensor_cache[idx].baseline = baseline;
    s_sensor_cache[idx].threshold = threshold;
    s_sensor_cache[idx].detected = detected;
}

void web_config_update_relay(int idx, bool active, int stage, bool pump_on, int on_count, bool detected) {
    if (idx < 0 || idx >= 3) return;
    s_relay_cache[idx].active = active;
    s_relay_cache[idx].stage = stage;
    s_relay_cache[idx].pump_on = pump_on;
    s_relay_cache[idx].on_count = on_count;
    s_relay_cache[idx].detected = detected;
}
