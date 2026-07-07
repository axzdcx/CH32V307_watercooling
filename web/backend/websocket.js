/**
 * WebSocket服务器模块
 * 提供实时双向通信功能
 * 
 * 功能：
 * - 服务器初始化和端口监听
 * - 客户端连接管理（连接、断开、客户端列表）
 * - 消息广播功能
 * - 消息转发和验证
 */

const WebSocket = require('ws');

class WebSocketServer {
  constructor(logger) {
    this.logger = logger;
    this.wss = null;
    this.clients = new Map(); // 存储客户端连接，key为clientId，value为{ws, clientId, connectedAt}
    this.port = null;
  }

  /**
   * 初始化WebSocket服务器
   * @param {number} port - 监听端口
   * @returns {Promise<void>}
   */
  initWebSocketServer(port) {
    return new Promise((resolve, reject) => {
      try {
        this.port = port;
        this.wss = new WebSocket.Server({ port });

        this.wss.on('listening', () => {
          this.logger.info(`WebSocket服务器启动成功，监听端口: ${port}`);
          resolve();
        });

        this.wss.on('error', (error) => {
          this.logger.error('WebSocket服务器错误', error);
          reject(error);
        });

        this.wss.on('connection', (ws, req) => {
          this.handleConnection(ws, req);
        });

      } catch (error) {
        this.logger.error('初始化WebSocket服务器失败', error);
        reject(error);
      }
    });
  }

  /**
   * 处理客户端连接
   * @param {WebSocket} ws - WebSocket连接对象
   * @param {object} req - HTTP请求对象
   */
  handleConnection(ws, req) {
    // 生成唯一的客户端ID
    const clientId = this.generateClientId();
    const clientIp = req.socket.remoteAddress;

    // 存储客户端信息
    this.clients.set(clientId, {
      ws,
      clientId,
      connectedAt: Date.now(),
      ip: clientIp
    });

    this.logger.info('客户端连接成功', {
      clientId,
      ip: clientIp,
      totalClients: this.clients.size
    });

    // 发送连接确认消息
    this.sendConnectionAck(clientId);

    // 监听客户端消息
    ws.on('message', (data) => {
      this.handleMessage(clientId, data);
    });

    // 监听客户端断开
    ws.on('close', () => {
      this.handleDisconnection(clientId);
    });

    // 监听错误
    ws.on('error', (error) => {
      this.logger.error(`客户端错误 [${clientId}]`, {
        message: error.message,
        code: error.code
      });
      // 不要让错误导致服务器崩溃
    });
  }

  /**
   * 发送连接确认消息
   * @param {string} clientId - 客户端ID
   */
  sendConnectionAck(clientId) {
    const message = {
      type: 'connection_ack',
      timestamp: Date.now(),
      message: 'Connected to server',
      clientId
    };

    this.sendToClient(clientId, message);
  }

  /**
   * 处理客户端消息
   * @param {string} clientId - 客户端ID
   * @param {Buffer|string} data - 接收到的数据
   */
  handleMessage(clientId, data) {
    try {
      // 记录原始数据(用于调试)
      const rawData = data.toString();
      this.logger.debug(`收到原始消息 [${clientId}]`, {
        length: rawData.length,
        preview: rawData.substring(0, 200) // 只记录前200个字符
      });

      // 解析JSON消息
      const message = JSON.parse(rawData);
      
      this.logger.debug('收到客户端消息', {
        clientId,
        messageType: message.type
      });

      // 验证消息格式
      if (!this.validateMessage(message)) {
        this.sendError(clientId, 'WS_MESSAGE_INVALID', 'Invalid message format');
        return;
      }

      // 转发消息给所有客户端（包括发送者）
      this.broadcast(message);

    } catch (error) {
      // 记录完整的错误消息和原始数据
      const rawData = data.toString();
      console.log('=== 解析消息失败 ===');
      console.log('客户端ID:', clientId);
      console.log('错误:', error.message);
      console.log('原始数据长度:', rawData.length);
      console.log('原始数据(前500字符):', rawData.substring(0, 500));
      console.log('==================');
      
      this.logger.error(`解析消息失败 [${clientId}]`, {
        error: error.message,
        stack: error.stack,
        rawDataLength: rawData.length
      });
      this.sendError(clientId, 'WS_MESSAGE_INVALID', 'Failed to parse message');
    }
  }

  /**
   * 验证消息格式
   * @param {object} message - 消息对象
   * @returns {boolean} 是否有效
   */
  validateMessage(message) {
    // 消息必须包含type字段
    if (!message || typeof message.type !== 'string') {
      return false;
    }

    // 消息必须是有效的JSON对象
    if (typeof message !== 'object') {
      return false;
    }

    return true;
  }

  /**
   * 处理客户端断开连接
   * @param {string} clientId - 客户端ID
   */
  handleDisconnection(clientId) {
    const client = this.clients.get(clientId);
    
    if (client) {
      // 清理客户端资源
      this.clients.delete(clientId);
      
      this.logger.info('客户端断开连接', {
        clientId,
        connectedDuration: Date.now() - client.connectedAt,
        remainingClients: this.clients.size
      });
    }
  }

  /**
   * 广播消息给所有已连接的客户端
   * @param {object} message - 要广播的消息对象
   */
  broadcast(message) {
    const messageStr = JSON.stringify(message);
    let successCount = 0;
    let failCount = 0;

    this.clients.forEach((client, clientId) => {
      try {
        if (client.ws.readyState === WebSocket.OPEN) {
          client.ws.send(messageStr);
          successCount++;
        }
      } catch (error) {
        failCount++;
        this.logger.error(`向客户端发送消息失败 [${clientId}]`, error);
        // 错误隔离：单个客户端发送失败不影响其他客户端
      }
    });

    this.logger.debug('消息广播完成', {
      messageType: message.type,
      successCount,
      failCount,
      totalClients: this.clients.size
    });
  }

  /**
   * 发送消息给特定客户端
   * @param {string} clientId - 客户端ID
   * @param {object} message - 消息对象
   * @returns {boolean} 是否发送成功
   */
  sendToClient(clientId, message) {
    const client = this.clients.get(clientId);
    
    if (!client) {
      this.logger.warn(`客户端不存在 [${clientId}]`);
      return false;
    }

    try {
      if (client.ws.readyState === WebSocket.OPEN) {
        client.ws.send(JSON.stringify(message));
        return true;
      } else {
        this.logger.warn(`客户端连接未就绪 [${clientId}]`, {
          readyState: client.ws.readyState
        });
        return false;
      }
    } catch (error) {
      this.logger.error(`发送消息失败 [${clientId}]`, error);
      return false;
    }
  }

  /**
   * 发送错误消息给客户端
   * @param {string} clientId - 客户端ID
   * @param {string} code - 错误代码
   * @param {string} message - 错误消息
   */
  sendError(clientId, code, message) {
    const errorMessage = {
      type: 'error',
      timestamp: Date.now(),
      code,
      message
    };

    this.sendToClient(clientId, errorMessage);
  }

  /**
   * 获取连接的客户端数量
   * @returns {number} 客户端数量
   */
  getClientCount() {
    return this.clients.size;
  }

  /**
   * 获取所有客户端信息
   * @returns {Array} 客户端信息数组
   */
  getClients() {
    const clientList = [];
    this.clients.forEach((client, clientId) => {
      clientList.push({
        clientId,
        connectedAt: client.connectedAt,
        ip: client.ip,
        readyState: client.ws.readyState
      });
    });
    return clientList;
  }

  /**
   * 生成唯一的客户端ID
   * @returns {string} 客户端ID
   */
  generateClientId() {
    // 使用时间戳和随机数生成简单的ID
    return `client_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
  }

  /**
   * 关闭WebSocket服务器
   * @returns {Promise<void>}
   */
  close() {
    return new Promise((resolve) => {
      if (!this.wss) {
        resolve();
        return;
      }

      this.logger.info('正在关闭WebSocket服务器...');

      // 关闭所有客户端连接
      this.clients.forEach((client, clientId) => {
        try {
          client.ws.close();
        } catch (error) {
          this.logger.error(`关闭客户端连接失败 [${clientId}]`, error);
        }
      });

      // 清空客户端列表
      this.clients.clear();

      // 关闭服务器
      this.wss.close(() => {
        this.logger.info('WebSocket服务器已关闭');
        resolve();
      });
    });
  }
}

module.exports = WebSocketServer;
