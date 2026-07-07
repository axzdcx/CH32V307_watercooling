/**
 * WebSocket客户端模块
 * 负责与后端WebSocket服务器建立连接，发送和接收消息
 * 
 * 功能：
 * - WebSocket连接建立和断开
 * - 消息发送和接收
 * - 消息类型分发（realtime_data、health_status、error等）
 * - 连接状态管理
 * - 自动重连机制（最多3次）
 * - 事件系统（connected、disconnected、message、error）
 */

/**
 * WebSocket客户端类
 */
class WebSocketClient {
  constructor() {
    this.ws = null;
    this.config = null;
    this.status = 'disconnected'; // 连接状态：disconnected | connecting | connected
    this.reconnectAttempts = 0; // 当前重连尝试次数
    this.reconnectTimer = null; // 重连定时器
    this.eventHandlers = new Map(); // 事件处理器映射
    this.messageHandlers = new Map(); // 消息类型处理器映射
    this.clientId = null; // 服务器分配的客户端ID
    this.lastError = null; // 最后一次错误信息
  }

  /**
   * 连接到WebSocket服务器
   * @param {string} host - 服务器地址
   * @param {number} port - 服务器端口
   * @returns {Promise<void>}
   */
  connect(host, port) {
    return new Promise((resolve, reject) => {
      try {
        // 如果已经连接，先断开
        if (this.ws && this.status !== 'disconnected') {
          this.disconnect();
        }

        // 保存配置
        this.config = { host, port };

        // 重置阻止重连标志
        this.preventReconnect = false;

        // 构建WebSocket URL
        const url = `ws://${host}:${port}`;

        // 更新状态
        this.status = 'connecting';
        this.emit('connecting', { url });

        // 创建WebSocket连接
        this.ws = new WebSocket(url);

        // 连接成功
        this.ws.onopen = () => {
          this.status = 'connected';
          this.reconnectAttempts = 0; // 重置重连次数
          this.lastError = null;
          
          console.log('WebSocket连接成功:', url);
          this.emit('connected', { url });
          resolve();
        };

        // 接收消息
        this.ws.onmessage = (event) => {
          this.handleMessage(event.data);
        };

        // 连接关闭
        this.ws.onclose = (event) => {
          this.handleClose(event);
        };

        // 连接错误
        this.ws.onerror = (error) => {
          this.handleError(error);
          
          // 如果是在连接阶段出错，reject Promise
          if (this.status === 'connecting') {
            reject(new Error('WebSocket连接失败'));
          }
        };

      } catch (error) {
        this.status = 'disconnected';
        this.lastError = error.message;
        console.error('创建WebSocket连接失败:', error);
        this.emit('error', { error: error.message });
        reject(error);
      }
    });
  }

  /**
   * 断开WebSocket连接
   * @param {boolean} preventReconnect - 是否阻止自动重连
   */
  disconnect(preventReconnect = true) {
    // 清除重连定时器
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }

    // 重置重连次数
    this.reconnectAttempts = 0;

    // 标记是否阻止重连
    this.preventReconnect = preventReconnect;

    // 关闭WebSocket连接
    if (this.ws) {
      try {
        this.ws.close();
      } catch (error) {
        console.error('关闭WebSocket连接失败:', error);
      }
      this.ws = null;
    }

    // 更新状态
    this.status = 'disconnected';
    this.clientId = null;

    console.log('WebSocket连接已断开');
    this.emit('disconnected', {});
  }

  /**
   * 发送消息
   * @param {object} message - 要发送的消息对象
   * @returns {boolean} 是否发送成功
   */
  send(message) {
    // 检查连接状态
    if (!this.ws || this.status !== 'connected') {
      console.error('WebSocket未连接，无法发送消息');
      this.emit('error', { error: 'WebSocket未连接' });
      return false;
    }

    try {
      // 添加时间戳（如果消息中没有）
      if (!message.timestamp) {
        message.timestamp = Date.now();
      }

      // 发送JSON格式的消息
      this.ws.send(JSON.stringify(message));
      
      console.log('发送消息:', message.type);
      return true;
    } catch (error) {
      console.error('发送消息失败:', error);
      this.emit('error', { error: error.message });
      return false;
    }
  }

  /**
   * 处理接收到的消息
   * @param {string} data - 接收到的数据
   */
  handleMessage(data) {
    try {
      // 解析JSON消息
      const message = JSON.parse(data);

      console.log('收到消息:', message.type);

      // 修复timestamp: 如果timestamp太小(小于1000000000000,即2001年之前),用当前时间替换
      // 单片机发送的timestamp是启动后的毫秒数,不是真实的Unix时间戳
      if (message.timestamp && message.timestamp < 1000000000000) {
        const oldTimestamp = message.timestamp;
        message.timestamp = Date.now();
        console.log('[WS] 修正message.timestamp:', oldTimestamp, '->', message.timestamp);
      }
      
      // 同时修正data对象中的timestamp(如果存在)
      if (message.data && message.data.timestamp && message.data.timestamp < 1000000000000) {
        const oldDataTimestamp = message.data.timestamp;
        message.data.timestamp = Date.now();
        console.log('[WS] 修正data.timestamp:', oldDataTimestamp, '->', message.data.timestamp);
      }

      // 触发通用消息事件
      this.emit('message', message);

      // 根据消息类型分发处理
      switch (message.type) {
        case 'connection_ack':
          // 连接确认消息
          this.clientId = message.clientId;
          console.log('收到连接确认，客户端ID:', this.clientId);
          this.emit('connection_ack', message);
          break;

        case 'realtime_data':
          // 实时数据消息
          this.emit('realtime_data', message);
          break;

        case 'health_status':
          // 健康状态消息
          this.emit('health_status', message);
          break;

        case 'error':
          // 错误消息
          console.error('收到服务器错误:', message.code, message.message);
          this.lastError = message.message;
          this.emit('server_error', message);
          break;

        default:
          // 其他类型的消息
          console.log('收到未知类型的消息:', message.type);
          this.emit('unknown_message', message);
          break;
      }

      // 调用注册的消息类型处理器
      if (this.messageHandlers.has(message.type)) {
        const handlers = this.messageHandlers.get(message.type);
        handlers.forEach(handler => {
          try {
            handler(message);
          } catch (error) {
            console.error('执行消息处理器时出错:', error);
          }
        });
      }

    } catch (error) {
      console.error('解析消息失败:', error);
      this.emit('error', { error: '消息解析失败' });
    }
  }

  /**
   * 处理连接关闭
   * @param {CloseEvent} event - 关闭事件
   */
  handleClose(event) {
    const wasConnected = this.status === 'connected';
    this.status = 'disconnected';
    this.clientId = null;

    console.log('WebSocket连接关闭:', event.code, event.reason);
    this.emit('disconnected', {
      code: event.code,
      reason: event.reason,
      wasClean: event.wasClean
    });

    // 如果之前是连接状态，尝试自动重连
    if (wasConnected && this.config) {
      this.attemptReconnect();
    }
  }

  /**
   * 处理连接错误
   * @param {Event} error - 错误事件
   */
  handleError(error) {
    console.error('WebSocket错误:', error);
    this.lastError = 'WebSocket连接错误';
    this.emit('error', { error: this.lastError });
  }

  /**
   * 尝试重新连接
   */
  attemptReconnect() {
    // 检查是否被阻止重连
    if (this.preventReconnect) {
      console.log('重连已被阻止');
      return;
    }

    // 检查配置是否存在
    if (!this.config) {
      console.log('配置不存在,无法重连');
      return;
    }

    // 检查是否启用自动重连
    const autoReconnect = configManager ? configManager.get('websocket.autoReconnect', true) : true;
    if (!autoReconnect) {
      console.log('自动重连已禁用');
      return;
    }

    // 检查重连次数
    const maxAttempts = configManager ? configManager.get('websocket.maxReconnectAttempts', 3) : 3;
    if (this.reconnectAttempts >= maxAttempts) {
      console.log('已达到最大重连次数，停止重连');
      this.emit('reconnect_failed', {
        attempts: this.reconnectAttempts,
        maxAttempts
      });
      return;
    }

    // 增加重连次数
    this.reconnectAttempts++;

    // 获取重连间隔
    const reconnectInterval = configManager ? configManager.get('websocket.reconnectInterval', 3000) : 3000;

    console.log(`将在 ${reconnectInterval}ms 后进行第 ${this.reconnectAttempts} 次重连...`);
    this.emit('reconnecting', {
      attempt: this.reconnectAttempts,
      maxAttempts,
      delay: reconnectInterval
    });

    // 设置重连定时器
    this.reconnectTimer = setTimeout(() => {
      console.log(`开始第 ${this.reconnectAttempts} 次重连...`);
      
      this.connect(this.config.host, this.config.port)
        .then(() => {
          console.log('重连成功');
          this.emit('reconnected', {
            attempt: this.reconnectAttempts
          });
        })
        .catch((error) => {
          console.error('重连失败:', error);
          // 继续尝试重连（在handleClose中会再次调用attemptReconnect）
        });
    }, reconnectInterval);
  }

  /**
   * 获取连接状态
   * @returns {string} 连接状态：'disconnected' | 'connecting' | 'connected'
   */
  getConnectionStatus() {
    return this.status;
  }

  /**
   * 获取客户端ID
   * @returns {string|null} 客户端ID
   */
  getClientId() {
    return this.clientId;
  }

  /**
   * 获取最后一次错误信息
   * @returns {string|null} 错误信息
   */
  getLastError() {
    return this.lastError;
  }

  /**
   * 检查是否已连接
   * @returns {boolean} 是否已连接
   */
  isConnected() {
    return this.status === 'connected' && this.ws && this.ws.readyState === WebSocket.OPEN;
  }

  /**
   * 注册事件处理器
   * @param {string} event - 事件名称
   * @param {Function} handler - 事件处理函数
   * @returns {Function} 取消注册的函数
   */
  on(event, handler) {
    if (typeof handler !== 'function') {
      console.error('事件处理器必须是函数');
      return () => {};
    }

    if (!this.eventHandlers.has(event)) {
      this.eventHandlers.set(event, []);
    }

    this.eventHandlers.get(event).push(handler);

    // 返回取消注册的函数
    return () => {
      this.off(event, handler);
    };
  }

  /**
   * 取消事件处理器
   * @param {string} event - 事件名称
   * @param {Function} handler - 事件处理函数
   */
  off(event, handler) {
    if (!this.eventHandlers.has(event)) {
      return;
    }

    const handlers = this.eventHandlers.get(event);
    const index = handlers.indexOf(handler);
    
    if (index > -1) {
      handlers.splice(index, 1);
    }
  }

  /**
   * 触发事件
   * @param {string} event - 事件名称
   * @param {*} data - 事件数据
   */
  emit(event, data) {
    if (!this.eventHandlers.has(event)) {
      return;
    }

    const handlers = this.eventHandlers.get(event);
    handlers.forEach(handler => {
      try {
        handler(data);
      } catch (error) {
        console.error('执行事件处理器时出错:', error);
      }
    });
  }

  /**
   * 注册消息类型处理器
   * @param {string} messageType - 消息类型
   * @param {Function} handler - 处理函数
   * @returns {Function} 取消注册的函数
   */
  onMessage(messageType, handler) {
    if (typeof handler !== 'function') {
      console.error('消息处理器必须是函数');
      return () => {};
    }

    if (!this.messageHandlers.has(messageType)) {
      this.messageHandlers.set(messageType, []);
    }

    this.messageHandlers.get(messageType).push(handler);

    // 返回取消注册的函数
    return () => {
      this.offMessage(messageType, handler);
    };
  }

  /**
   * 取消消息类型处理器
   * @param {string} messageType - 消息类型
   * @param {Function} handler - 处理函数
   */
  offMessage(messageType, handler) {
    if (!this.messageHandlers.has(messageType)) {
      return;
    }

    const handlers = this.messageHandlers.get(messageType);
    const index = handlers.indexOf(handler);
    
    if (index > -1) {
      handlers.splice(index, 1);
    }
  }

  /**
   * 发送控制指令
   * @param {string} action - 操作类型
   * @param {object} params - 操作参数
   * @returns {boolean} 是否发送成功
   */
  sendControlCommand(action, params = {}) {
    const message = {
      type: 'control_command',
      timestamp: Date.now(),
      command: {
        action,
        ...params
      }
    };

    return this.send(message);
  }

  /**
   * 发送模式切换指令
   * @param {string} mode - 运行模式：'silent' | 'balanced' | 'performance'
   * @returns {boolean} 是否发送成功
   */
  setMode(mode) {
    // 验证模式值
    if (!['silent', 'balanced', 'performance'].includes(mode)) {
      console.error('无效的运行模式:', mode);
      return false;
    }

    return this.sendControlCommand('set_mode', { mode });
  }

  /**
   * 发送PID参数设置指令
   * @param {number} kp - 比例系数
   * @param {number} ki - 积分系数
   * @param {number} kd - 微分系数
   * @returns {boolean} 是否发送成功
   */
  setPIDParams(kp, ki, kd) {
    // 验证参数
    if (typeof kp !== 'number' || typeof ki !== 'number' || typeof kd !== 'number') {
      console.error('PID参数必须是数字');
      return false;
    }

    return this.sendControlCommand('set_pid', {
      pidParams: { kp, ki, kd }
    });
  }

  /**
   * 清除所有事件处理器
   */
  clearEventHandlers() {
    this.eventHandlers.clear();
    this.messageHandlers.clear();
  }

  /**
   * 销毁WebSocket客户端
   */
  destroy() {
    this.disconnect();
    this.clearEventHandlers();
    this.config = null;
    this.lastError = null;
  }
}

// 创建全局WebSocket客户端实例
const wsClient = new WebSocketClient();

// 暴露到全局作用域（浏览器环境）
if (typeof window !== 'undefined') {
  window.wsClient = wsClient;
  window.WebSocketClient = WebSocketClient;
}

// 导出WebSocket客户端类和实例（用于模块化）
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    WebSocketClient,
    wsClient
  };
}
