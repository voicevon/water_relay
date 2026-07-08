#pragma once
#include <Arduino.h>

// 水泵网关监控暗色 SPA 前端 HTML（存储于 Flash PROGMEM，不占用 RAM）
static const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>水泵控制节点</title>
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

        /* WiFi/MQTT 状态徽章 */
        .status-badge {
            font-size: 0.72rem;
            padding: 0.15rem 0.45rem;
            border-radius: 9999px;
            font-weight: 700;
        }

        .status-badge.dry {
            background: rgba(100, 116, 139, 0.15);
            color: var(--dry-normal);
        }

        .status-badge.water {
            background: rgba(6, 182, 212, 0.15);
            color: var(--water-alert);
        }

        /* 信号强度条 */
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
        <div class="logo">水泵控制节点</div>
        <button class="menu-btn" onclick="toggleDrawer(true)">☰</button>
    </header>

    <!-- 侧边菜单 -->
    <div class="overlay" id="overlay" onclick="toggleDrawer(false)"></div>
    <div class="drawer" id="drawer">
        <div style="height: 1rem;"></div>
        <div class="nav-link active" data-tab="tab-monitor" onclick="switchTab(this)">实时运行监控</div>
        <div class="nav-link" data-tab="tab-wifi" onclick="switchTab(this)">网络与系统配置</div>
        <div class="nav-link" data-tab="tab-about" onclick="switchTab(this)">关于</div>
    </div>

    <div class="container">
        <!-- 实时监控 TAB -->
        <div id="tab-monitor" class="tab-content active">
            <!-- 信号强度与连接信息 -->
            <div class="card" style="padding: 1rem 1.5rem; margin-bottom: 1.25rem;">
                <div class="info-bar">
                    <div>
                        站点名称: <span id="info-station" style="font-weight: 600; color: var(--accent-cyan);">home</span><br>
                        STA SSID: <span id="info-ssid" style="font-weight: 600; color: var(--accent-blue);">未连接</span><br>
                        IP 地址: <span id="info-ip" style="font-weight: 600; color: var(--text-main);"></span>
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
                    <span>3 通道输入传感器值</span>
                    <span style="font-size: 0.8rem; font-weight: normal; color: var(--text-muted);">自动更新(1Hz)</span>
                </div>
                <div class="grid-4" id="sensor-grid">
                    <!-- JS 动态渲染 -->
                </div>
            </div>
        </div>

        <!-- 系统与网络配置 TAB -->
        <div id="tab-wifi" class="tab-content">
            <!-- WiFi & MQTT 物理状态卡片 -->
            <div class="card" style="padding: 1rem 1.5rem; margin-bottom: 1.25rem; display: flex; flex-direction: column; gap: 0.75rem; align-items: flex-start; font-size: 0.95rem;">
                <div style="display: flex; align-items: center; gap: 0.5rem;">
                    <span style="color: var(--text-muted);">WiFi:</span>
                    <span id="wifi-status" class="status-badge dry">检测中...</span>
                </div>
                <div style="display: flex; align-items: center; gap: 0.5rem;">
                    <span style="color: var(--text-muted);">MQTT:</span>
                    <span id="mqtt-status" class="status-badge dry">检测中...</span>
                </div>
            </div>
            
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
                        <input type="text" id="name" name="name" placeholder="例如: home" required>
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

        <!-- 关于 TAB -->
        <div id="tab-about" class="tab-content">
            <div class="card">
                <div class="card-title">关于</div>
                <div style="line-height: 2; font-size: 0.95rem;">
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

        // 切换 Tab
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
                document.getElementById('name').value = data.name || 'home';
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

        // 渲染信号条
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
            try {
                const res = await fetch('/api/data');
                const data = await res.json();

                // 更新 Wi-Fi 和 MQTT 状态显示
                const wifiBadge = document.getElementById('wifi-status');
                const mqttBadge = document.getElementById('mqtt-status');
                if (wifiBadge) {
                    wifiBadge.textContent = data.wifi_connected ? '已连接' : '未连接';
                    wifiBadge.className = data.wifi_connected ? 'status-badge water' : 'status-badge dry';
                }
                if (mqttBadge) {
                    mqttBadge.textContent = data.mqtt_connected ? '已连接' : '未连接';
                    mqttBadge.className = data.mqtt_connected ? 'status-badge water' : 'status-badge dry';
                }

                // 1. 更新顶部连接信息与信号强度
                document.getElementById('info-ssid').textContent = data.ssid || "未连接";
                document.getElementById('info-station').textContent = data.station || "home";
                if (data.ip) {
                    document.getElementById('info-ip').textContent = data.ip;
                } else {
                    document.getElementById('info-ip').textContent = "未获取";
                }
                updateSignalStrength(data.rssi);

                // 如果当前激活的不是监控页，则直接返回，不渲染下方组件列表
                if (!document.getElementById('tab-monitor').classList.contains('active')) {
                    return;
                }

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

                // 3. 更新 3 个传感器输入值
                const sensorGrid = document.getElementById('sensor-grid');
                sensorGrid.innerHTML = '';
                data.sensors.forEach((s, idx) => {
                    const isWater = s.detected;
                    const chDiv = document.createElement('div');
                    chDiv.className = `status-item ${isWater ? 'water-detected' : ''}`;
                    chDiv.innerHTML = `
                        <div class="item-header">
                            <span class="item-title">传感器 #${idx + 1}</span>
                            <span class="badge ${isWater ? 'blue' : 'gray'}">${isWater ? '有水' : '无水'}</span>
                        </div>
                        <div class="item-meta">
                            <div>测得电容值: <span>${(s.raw / 100.0).toFixed(2)} pF</span></div>
                            <div style="margin-top: 4px; color: var(--text-muted);">状态字直接解码: <span>${isWater ? '有水' : '无水'}</span></div>
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
