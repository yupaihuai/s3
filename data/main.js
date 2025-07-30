/**
 * @file main.js
 * @brief 用于验证ESP32-S3项目阶段2 - 前后端通信管道的客户端脚本
 * @author [ANEAK]
 * @date [2025/7]
 */

document.addEventListener('DOMContentLoaded', () => {
    const wsStatus = document.getElementById('ws-status');
    const uptimeValue = document.getElementById('uptime-value');
    const rebootBtn = document.getElementById('reboot-btn');
    const rpcResponse = document.getElementById('rpc-response');
    const cardFooter = document.querySelector('.card-footer');

    let rpcId = 1;
    let socket;

    function connect() {
        // 使用当前页面的主机名动态构建WebSocket地址
        const wsUrl = `ws://${window.location.hostname}/ws`;
        console.log(`Attempting to connect to ${wsUrl}`);
        cardFooter.textContent = `正在尝试连接到 ${wsUrl}...`;

        socket = new WebSocket(wsUrl);

        socket.onopen = () => {
            console.log('WebSocket Connected');
            wsStatus.textContent = '已连接';
            wsStatus.className = 'text-success';
            cardFooter.textContent = '连接成功！等待服务器消息...';
        };

        socket.onclose = (event) => {
            console.log('WebSocket Disconnected. Code:', event.code, 'Reason:', event.reason);
            wsStatus.textContent = '已断开';
            wsStatus.className = 'text-danger';
            cardFooter.textContent = '连接已断开。将在5秒后尝试重新连接...';
            // 简单的自动重连逻辑
            setTimeout(connect, 5000);
        };

        socket.onerror = (error) => {
            console.error('WebSocket Error:', error);
            wsStatus.textContent = '连接错误';
            wsStatus.className = 'text-danger';
        };

        socket.onmessage = (event) => {
            console.log('Received message:', event.data);
            try {
                const data = JSON.parse(event.data);

                // 处理服务器推送的通知 (没有 id)
                if (data.method) {
                    handleNotification(data);
                }
                // 处理RPC响应 (有 id)
                else if (data.id) {
                    handleRpcResponse(data);
                }

            } catch (e) {
                console.error('Failed to parse JSON:', e);
            }
        };
    }

    function handleNotification(notification) {
        const { method, params } = notification;

        if (method === 'system.stateUpdate' && params && params.uptime) {
            uptimeValue.textContent = params.uptime;
            cardFooter.textContent = `状态更新于: ${new Date().toLocaleTimeString()}`;
        } else if (method === 'server.welcome') {
            cardFooter.textContent = `来自服务器的欢迎消息: ${params.message}`;
        } else if (method === 'log.batch' && Array.isArray(params)) {
            // [新增] 处理日志批处理通知
            console.groupCollapsed(`Received log batch of ${params.length} entries`);
            params.forEach(logEntry => {
                // 简单地将每条日志打印到控制台
                console.log(logEntry.msg);
            });
            console.groupEnd();
        }
    }

    function handleRpcResponse(response) {
        if (response.result) {
            rpcResponse.textContent = JSON.stringify(response.result);
            rpcResponse.className = 'text-success';
        } else if (response.error) {
            rpcResponse.textContent = `Error ${response.error.code}: ${response.error.message}`;
            rpcResponse.className = 'text-danger';
        }
    }

    function sendRpcRequest(method, params) {
        if (socket && socket.readyState === WebSocket.OPEN) {
            const request = {
                jsonrpc: '2.0',
                method: method,
                params: params,
                id: rpcId++
            };
            const requestStr = JSON.stringify(request);
            console.log('Sending RPC request:', requestStr);
            socket.send(requestStr);
            rpcResponse.textContent = '请求已发送，等待响应...';
            rpcResponse.className = 'text-muted';
        } else {
            console.error('WebSocket is not open. Cannot send request.');
            rpcResponse.textContent = '无法发送请求：WebSocket未连接。';
            rpcResponse.className = 'text-danger';
        }
    }

    rebootBtn.addEventListener('click', () => {
        if (confirm('您确定要发送重启命令吗？')) {
            sendRpcRequest('system.reboot', null);
        }
    });

    // 启动连接
    connect();
});