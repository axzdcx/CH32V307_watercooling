/**
 * 日志系统模块
 * 提供统一的日志记录功能
 */

const fs = require('fs');
const path = require('path');

// 日志级别定义
const LOG_LEVELS = {
  DEBUG: 0,
  INFO: 1,
  WARN: 2,
  ERROR: 3
};

class Logger {
  constructor(config) {
    this.logPath = config.logPath || './logs';
    this.level = LOG_LEVELS[config.level] || LOG_LEVELS.INFO;
    
    // 确保日志目录存在
    if (!fs.existsSync(this.logPath)) {
      fs.mkdirSync(this.logPath, { recursive: true });
    }
  }

  /**
   * 格式化日志消息
   * @param {string} level - 日志级别
   * @param {string} message - 日志消息
   * @param {object} metadata - 附加元数据
   * @returns {string} 格式化后的日志字符串
   */
  formatMessage(level, message, metadata) {
    const timestamp = new Date().toISOString();
    const metaStr = metadata ? ` | ${JSON.stringify(metadata)}` : '';
    return `[${timestamp}] [${level}] ${message}${metaStr}`;
  }

  /**
   * 写入日志到文件
   * @param {string} level - 日志级别
   * @param {string} message - 日志消息
   * @param {object} metadata - 附加元数据
   */
  writeLog(level, message, metadata) {
    const logMessage = this.formatMessage(level, message, metadata);
    
    // 输出到控制台
    console.log(logMessage);
    
    // 写入到日志文件
    const date = new Date().toISOString().split('T')[0];
    const logFile = path.join(this.logPath, `app-${date}.log`);
    
    fs.appendFile(logFile, logMessage + '\n', (err) => {
      if (err) {
        console.error('写入日志文件失败:', err);
      }
    });
  }

  /**
   * 记录DEBUG级别日志
   * @param {string} message - 日志消息
   * @param {object} metadata - 附加元数据
   */
  debug(message, metadata) {
    if (this.level <= LOG_LEVELS.DEBUG) {
      this.writeLog('DEBUG', message, metadata);
    }
  }

  /**
   * 记录INFO级别日志
   * @param {string} message - 日志消息
   * @param {object} metadata - 附加元数据
   */
  info(message, metadata) {
    if (this.level <= LOG_LEVELS.INFO) {
      this.writeLog('INFO', message, metadata);
    }
  }

  /**
   * 记录WARN级别日志
   * @param {string} message - 日志消息
   * @param {object} metadata - 附加元数据
   */
  warn(message, metadata) {
    if (this.level <= LOG_LEVELS.WARN) {
      this.writeLog('WARN', message, metadata);
    }
  }

  /**
   * 记录ERROR级别日志
   * @param {string} message - 日志消息
   * @param {Error} error - 错误对象
   */
  error(message, error) {
    const metadata = error ? {
      message: error.message,
      stack: error.stack
    } : null;
    
    this.writeLog('ERROR', message, metadata);
  }

  /**
   * 记录控制操作日志
   * @param {string} action - 操作类型
   * @param {object} params - 操作参数
   * @param {object} result - 操作结果
   */
  logControl(action, params, result) {
    const metadata = {
      action,
      params,
      result
    };
    this.info('控制操作', metadata);
  }
}

module.exports = Logger;
