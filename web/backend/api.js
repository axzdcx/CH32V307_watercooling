/**
 * RESTful API模块
 * 提供历史数据查询和系统配置管理的HTTP接口
 */

const express = require('express');

class APIServer {
  constructor(config, storage, logger) {
    this.config = config;
    this.storage = storage;
    this.logger = logger;
    this.app = express();
    this.server = null;
    
    // 系统配置（内存中保存）
    this.systemConfig = {
      mode: 'balanced',
      pidParams: {
        kp: 1.0,
        ki: 0.5,
        kd: 0.1
      },
      simulationMode: config.simulation.enabled
    };
    
    // 初始化Express中间件
    this.initMiddleware();
    
    // 初始化路由
    this.initRoutes();
  }

  /**
   * 初始化Express中间件
   */
  initMiddleware() {
    // 解析JSON请求体
    this.app.use(express.json());
    
    // 配置CORS支持
    this.app.use((req, res, next) => {
      res.header('Access-Control-Allow-Origin', '*');
      res.header('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
      res.header('Access-Control-Allow-Headers', 'Content-Type, Authorization');
      
      // 处理预检请求
      if (req.method === 'OPTIONS') {
        return res.sendStatus(200);
      }
      
      next();
    });
    
    // 请求日志中间件
    this.app.use((req, res, next) => {
      this.logger.info('API请求', {
        method: req.method,
        path: req.path,
        query: req.query,
        ip: req.ip
      });
      next();
    });
    
    this.logger.info('Express中间件初始化完成');
  }

  /**
   * 初始化API路由
   */
  initRoutes() {
    // 健康检查端点
    this.app.get('/api/health', (req, res) => {
      res.json({
        success: true,
        timestamp: Date.now(),
        status: 'ok'
      });
    });
    
    // 获取历史数据
    this.app.get('/api/history', (req, res) => {
      this.handleGetHistory(req, res);
    });
    
    // 获取系统配置
    this.app.get('/api/config', (req, res) => {
      this.handleGetConfig(req, res);
    });
    
    // 更新系统配置
    this.app.post('/api/config', (req, res) => {
      this.handlePostConfig(req, res);
    });
    
    // 404处理
    this.app.use((req, res) => {
      res.status(404).json({
        success: false,
        timestamp: Date.now(),
        error: {
          code: 'NOT_FOUND',
          message: '请求的API端点不存在'
        }
      });
    });
    
    // 错误处理中间件
    this.app.use((err, req, res, next) => {
      this.logger.error('API错误', err);
      
      res.status(500).json({
        success: false,
        timestamp: Date.now(),
        error: {
          code: 'INTERNAL_ERROR',
          message: '服务器内部错误'
        }
      });
    });
    
    this.logger.info('API路由初始化完成');
  }

  /**
   * 处理获取历史数据请求
   * GET /api/history?start=<timestamp>&end=<timestamp>&type=<dataType>
   */
  handleGetHistory(req, res) {
    try {
      // 解析查询参数
      const startTime = req.query.start ? parseInt(req.query.start) : Date.now() - (24 * 60 * 60 * 1000);
      const endTime = req.query.end ? parseInt(req.query.end) : Date.now();
      const dataType = req.query.type || 'all';
      
      // 参数验证
      if (isNaN(startTime) || isNaN(endTime)) {
        return res.status(400).json({
          success: false,
          timestamp: Date.now(),
          error: {
            code: 'INVALID_PARAMS',
            message: '无效的时间参数'
          }
        });
      }
      
      if (startTime > endTime) {
        return res.status(400).json({
          success: false,
          timestamp: Date.now(),
          error: {
            code: 'INVALID_PARAMS',
            message: '开始时间不能大于结束时间'
          }
        });
      }
      
      // 查询历史数据
      const data = this.storage.queryHistory(startTime, endTime, dataType);
      
      this.logger.info('查询历史数据成功', {
        startTime,
        endTime,
        dataType,
        resultCount: data.length
      });
      
      // 返回成功响应
      res.json({
        success: true,
        timestamp: Date.now(),
        data: data
      });
      
    } catch (error) {
      this.logger.error('查询历史数据失败', error);
      
      res.status(500).json({
        success: false,
        timestamp: Date.now(),
        error: {
          code: 'QUERY_FAILED',
          message: '查询历史数据失败'
        }
      });
    }
  }

  /**
   * 处理获取系统配置请求
   * GET /api/config
   */
  handleGetConfig(req, res) {
    try {
      this.logger.info('获取系统配置');
      
      // 返回当前系统配置
      res.json({
        success: true,
        timestamp: Date.now(),
        config: this.systemConfig
      });
      
    } catch (error) {
      this.logger.error('获取系统配置失败', error);
      
      res.status(500).json({
        success: false,
        timestamp: Date.now(),
        error: {
          code: 'CONFIG_READ_FAILED',
          message: '获取系统配置失败'
        }
      });
    }
  }

  /**
   * 处理更新系统配置请求
   * POST /api/config
   */
  handlePostConfig(req, res) {
    try {
      const newConfig = req.body;
      
      // 验证配置参数
      const validationError = this.validateConfig(newConfig);
      if (validationError) {
        return res.status(400).json({
          success: false,
          timestamp: Date.now(),
          error: {
            code: 'INVALID_CONFIG',
            message: validationError
          }
        });
      }
      
      // 更新系统配置
      if (newConfig.mode) {
        this.systemConfig.mode = newConfig.mode;
      }
      
      if (newConfig.pidParams) {
        this.systemConfig.pidParams = {
          ...this.systemConfig.pidParams,
          ...newConfig.pidParams
        };
      }
      
      if (newConfig.simulationMode !== undefined) {
        this.systemConfig.simulationMode = newConfig.simulationMode;
      }
      
      this.logger.info('更新系统配置成功', { newConfig });
      
      // 返回成功响应
      res.json({
        success: true,
        timestamp: Date.now(),
        message: '配置更新成功',
        config: this.systemConfig
      });
      
    } catch (error) {
      this.logger.error('更新系统配置失败', error);
      
      res.status(500).json({
        success: false,
        timestamp: Date.now(),
        error: {
          code: 'CONFIG_UPDATE_FAILED',
          message: '更新系统配置失败'
        }
      });
    }
  }

  /**
   * 验证配置参数
   * @param {object} config - 配置对象
   * @returns {string|null} 错误消息，如果验证通过则返回null
   */
  validateConfig(config) {
    // 验证运行模式
    if (config.mode) {
      const validModes = ['silent', 'balanced', 'performance'];
      if (!validModes.includes(config.mode)) {
        return `无效的运行模式: ${config.mode}，有效值为: ${validModes.join(', ')}`;
      }
    }
    
    // 验证PID参数
    if (config.pidParams) {
      const { kp, ki, kd } = config.pidParams;
      
      // PID参数范围验证
      if (kp !== undefined && (kp < 0 || kp > 10)) {
        return 'PID参数Kp必须在0-10范围内';
      }
      
      if (ki !== undefined && (ki < 0 || ki > 5)) {
        return 'PID参数Ki必须在0-5范围内';
      }
      
      if (kd !== undefined && (kd < 0 || kd > 2)) {
        return 'PID参数Kd必须在0-2范围内';
      }
    }
    
    // 验证模拟模式
    if (config.simulationMode !== undefined && typeof config.simulationMode !== 'boolean') {
      return 'simulationMode必须是布尔值';
    }
    
    return null;
  }

  /**
   * 获取当前系统配置
   * @returns {object} 系统配置对象
   */
  getSystemConfig() {
    return { ...this.systemConfig };
  }

  /**
   * 启动API服务器
   * @param {number} port - 监听端口
   * @returns {Promise} 启动Promise
   */
  start(port) {
    return new Promise((resolve, reject) => {
      try {
        this.server = this.app.listen(port, () => {
          this.logger.info('API服务器启动成功', { port });
          resolve();
        });
        
        this.server.on('error', (error) => {
          this.logger.error('API服务器启动失败', error);
          reject(error);
        });
        
      } catch (error) {
        this.logger.error('API服务器启动异常', error);
        reject(error);
      }
    });
  }

  /**
   * 停止API服务器
   * @returns {Promise} 停止Promise
   */
  stop() {
    return new Promise((resolve, reject) => {
      if (!this.server) {
        resolve();
        return;
      }
      
      this.server.close((error) => {
        if (error) {
          this.logger.error('API服务器停止失败', error);
          reject(error);
        } else {
          this.logger.info('API服务器已停止');
          this.server = null;
          resolve();
        }
      });
    });
  }
}

module.exports = APIServer;
