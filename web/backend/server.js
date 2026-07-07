/**
 * 主服务器模块
 * 集成WebSocket服务器、RESTful API、模拟数据生成器和数据存储
 * 
 * 功能：
 * - 加载配置文件
 * - 启动WebSocket服务器和API服务器
 * - 定时生成和推送模拟数据
 * - 定时保存数据到存储
 * - 优雅关闭处理
 */

const fs = require('fs');
const path = require('path');
const Logger = require('./logger');
const WebSocketServer = require('./websocket');
const APIServer = require('./api');
const Storage = require('./storage');
const simulator = require('./simulator');

class MainServer {
  constructor() {
    this.config = null;
    this.logger = null;
    this.wsServer = null;
    this.apiServer = null;
    this.storage = null;
    
    // 定时器
    this.dataGenerationTimer = null;
    this.dataSaveTimer = null;
    
    // 服务器状态
    this.isRunning = false;
  }

  /**
   * 加载配置文件
   * @param {string} configPath - 配置文件路径
   * @returns {object} 配置对象
   */
  loadConfig(configPath) {
    try {
      const configFile = path.resolve(configPath);
      
      if (!fs.existsSync(configFile)) {
        throw new Error(`配置文件不存在: ${configFile}`);
      }

      const configContent = fs.readFileSync(configFile, 'utf8');
      const config = JSON.parse(configContent);

      console.log('配置文件加载成功:', configFile);
      return config;
    } catch (error) {
      console.error('加载配置文件失败:', error.message);
      throw error;
    }
  }

  /**
   * 初始化服务器
   * @param {string} configPath - 配置文件路径（默认为 ../config.json）
   */
  async init(configPath = '../config.json') {
    try {
      console.log('='.repeat(60));
      console.log('智能CPU水冷系统Web上位机 - 后端服务器');
      console.log('='.repeat(60));

      // 1. 加载配置
      console.log('\n[1/5] 加载配置文件...');
      this.config = this.loadConfig(configPath);
      console.log('配置加载完成');

      // 2. 初始化日志系统
      console.log('\n[2/5] 初始化日志系统...');
      this.logger = new Logger(this.config.logging);
      this.logger.info('日志系统初始化完成');

      // 3. 初始化数据存储
      console.log('\n[3/5] 初始化数据存储...');
      this.storage = new Storage(this.config.storage, this.logger);
      this.logger.info('数据存储初始化完成');

      // 4. 初始化WebSocket服务器
      console.log('\n[4/5] 启动WebSocket服务器...');
      this.wsServer = new WebSocketServer(this.logger);
      await this.wsServer.initWebSocketServer(this.config.server.websocketPort);
      this.logger.info('WebSocket服务器启动完成', {
        port: this.config.server.websocketPort
      });

      // 5. 初始化API服务器
      console.log('\n[5/5] 启动RESTful API服务器...');
      this.apiServer = new APIServer(this.config, this.storage, this.logger);
      await this.apiServer.start(this.config.server.httpPort);
      this.logger.info('API服务器启动完成', {
        port: this.config.server.httpPort
      });

      this.isRunning = true;
      
      console.log('\n' + '='.repeat(60));
      console.log('服务器启动成功！');
      console.log('='.repeat(60));
      console.log(`WebSocket服务器: ws://localhost:${this.config.server.websocketPort}`);
      console.log(`API服务器: http://localhost:${this.config.server.httpPort}`);
      console.log(`模拟模式: ${this.config.simulation.enabled ? '已启用' : '已禁用'}`);
      console.log('='.repeat(60));

      this.logger.info('主服务器初始化完成');
    } catch (error) {
      console.error('\n服务器初始化失败:', error.message);
      if (this.logger) {
        this.logger.error('服务器初始化失败', error);
      }
      throw error;
    }
  }

  /**
   * 启动模拟数据生成和推送
   */
  startDataGeneration() {
    if (!this.config.simulation.enabled) {
      this.logger.info('模拟模式未启用，跳过数据生成');
      return;
    }

    const interval = this.config.simulation.updateInterval || 2000;
    
    this.logger.info('启动模拟数据生成', { interval });

    // 定时生成并推送实时数据
    this.dataGenerationTimer = setInterval(() => {
      try {
        // 生成实时数据
        const realtimeData = simulator.generateRealtimeData();
        
        // 构造WebSocket消息
        const message = {
          type: 'realtime_data',
          timestamp: Date.now(),
          data: realtimeData
        };

        // 广播给所有客户端
        this.wsServer.broadcast(message);

        this.logger.debug('推送实时数据', {
          clientCount: this.wsServer.getClientCount()
        });

      } catch (error) {
        this.logger.error('生成或推送数据失败', error);
      }
    }, interval);

    // 定时生成并推送健康状态（每10秒一次）
    setInterval(() => {
      try {
        const healthStatus = simulator.generateHealthStatus();
        
        const message = {
          type: 'health_status',
          timestamp: Date.now(),
          data: healthStatus
        };

        this.wsServer.broadcast(message);

        this.logger.debug('推送健康状态', {
          healthScore: healthStatus.healthScore
        });

      } catch (error) {
        this.logger.error('生成或推送健康状态失败', error);
      }
    }, 10000);
  }

  /**
   * 启动数据定时保存
   */
  startDataSaving() {
    // 每10秒保存一次数据
    const saveInterval = 10000;
    
    this.logger.info('启动数据定时保存', { interval: saveInterval });

    this.dataSaveTimer = setInterval(() => {
      try {
        // 生成当前数据
        const data = simulator.generateRealtimeData();
        
        // 保存到存储
        const success = this.storage.saveData({
          ...data,
          timestamp: Date.now(),
          source: 'simulator'
        });

        if (success) {
          this.logger.debug('数据保存成功');
        }

      } catch (error) {
        this.logger.error('数据保存失败', error);
      }
    }, saveInterval);
  }

  /**
   * 启动服务器
   */
  async start(configPath = '../config.json') {
    try {
      // 初始化服务器
      await this.init(configPath);

      // 启动数据生成和推送
      this.startDataGeneration();

      // 启动数据定时保存
      this.startDataSaving();

      // 注册优雅关闭处理
      this.registerShutdownHandlers();

      this.logger.info('服务器完全启动');
    } catch (error) {
      console.error('服务器启动失败:', error);
      process.exit(1);
    }
  }

  /**
   * 优雅关闭服务器
   */
  async shutdown() {
    if (!this.isRunning) {
      return;
    }

    console.log('\n正在关闭服务器...');
    this.logger.info('开始优雅关闭服务器');

    this.isRunning = false;

    try {
      // 1. 停止定时器
      if (this.dataGenerationTimer) {
        clearInterval(this.dataGenerationTimer);
        this.dataGenerationTimer = null;
        this.logger.info('数据生成定时器已停止');
      }

      if (this.dataSaveTimer) {
        clearInterval(this.dataSaveTimer);
        this.dataSaveTimer = null;
        this.logger.info('数据保存定时器已停止');
      }

      // 2. 关闭WebSocket服务器
      if (this.wsServer) {
        await this.wsServer.close();
        this.logger.info('WebSocket服务器已关闭');
      }

      // 3. 关闭API服务器
      if (this.apiServer) {
        await this.apiServer.stop();
        this.logger.info('API服务器已关闭');
      }

      // 4. 清理存储缓存
      if (this.storage) {
        this.storage.clearCache();
        this.logger.info('存储缓存已清理');
      }

      console.log('服务器已安全关闭');
      this.logger.info('服务器优雅关闭完成');

    } catch (error) {
      console.error('关闭服务器时发生错误:', error);
      this.logger.error('关闭服务器失败', error);
    }

    // 退出进程
    process.exit(0);
  }

  /**
   * 注册优雅关闭处理器
   */
  registerShutdownHandlers() {
    // 处理 Ctrl+C
    process.on('SIGINT', () => {
      console.log('\n收到 SIGINT 信号 (Ctrl+C)');
      this.shutdown();
    });

    // 处理终止信号
    process.on('SIGTERM', () => {
      console.log('\n收到 SIGTERM 信号');
      this.shutdown();
    });

    // 处理未捕获的异常
    process.on('uncaughtException', (error) => {
      console.error('未捕获的异常:', error);
      if (this.logger) {
        this.logger.error('未捕获的异常', error);
      }
      this.shutdown();
    });

    // 处理未处理的Promise拒绝
    process.on('unhandledRejection', (reason, promise) => {
      console.error('未处理的Promise拒绝:', reason);
      if (this.logger) {
        this.logger.error('未处理的Promise拒绝', { reason });
      }
    });

    this.logger.info('优雅关闭处理器已注册');
  }
}

// 创建并启动服务器
if (require.main === module) {
  const server = new MainServer();
  // 配置文件在上一级目录
  const configPath = path.join(__dirname, '../config.json');
  server.start(configPath);
}

module.exports = MainServer;
