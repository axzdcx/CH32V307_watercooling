/**
 * 数据存储模块
 * 提供数据持久化、查询和缓存功能
 */

const fs = require('fs');
const path = require('path');

class Storage {
  constructor(config, logger) {
    this.dataPath = config.dataPath || './data';
    this.retentionDays = config.retentionDays || 30;
    this.logger = logger;
    
    // 内存缓存
    this.cache = new Map();
    this.cacheTimers = new Map();
    
    // 确保数据目录存在
    if (!fs.existsSync(this.dataPath)) {
      fs.mkdirSync(this.dataPath, { recursive: true });
      this.logger.info('创建数据存储目录', { path: this.dataPath });
    }
    
    // 历史数据文件路径
    this.historyFile = path.join(this.dataPath, 'history.json');
    
    // 初始化历史数据文件
    this.initHistoryFile();
    
    // 启动定期清理任务（每天执行一次）
    this.startCleanupTask();
    
    this.logger.info('数据存储模块初始化完成');
  }

  /**
   * 初始化历史数据文件
   */
  initHistoryFile() {
    if (!fs.existsSync(this.historyFile)) {
      // 创建空的历史数据文件
      const initialData = {
        version: '1.0',
        records: []
      };
      fs.writeFileSync(this.historyFile, JSON.stringify(initialData, null, 2));
      this.logger.info('创建历史数据文件', { file: this.historyFile });
    } else {
      this.logger.info('历史数据文件已存在', { file: this.historyFile });
    }
  }

  /**
   * 保存实时数据到历史记录
   * @param {object} data - 传感器数据
   * @returns {boolean} 保存是否成功
   */
  saveData(data) {
    try {
      // 添加时间戳和数据来源标识
      const record = {
        timestamp: data.timestamp || Date.now(),
        source: data.source || 'simulator',
        cpuTemp: data.cpuTemp,
        waterTemp: data.waterTemp,
        flowRate: data.flowRate,
        pumpSpeed: data.pumpSpeed,
        fanSpeed: data.fanSpeed,
        power: data.power
      };

      // 读取现有数据
      const historyData = this.readHistoryFile();
      
      // 添加新记录
      historyData.records.push(record);
      
      // 写回文件
      fs.writeFileSync(this.historyFile, JSON.stringify(historyData, null, 2));
      
      this.logger.debug('保存数据记录', { timestamp: record.timestamp });
      
      return true;
    } catch (error) {
      this.logger.error('保存数据失败', error);
      return false;
    }
  }


  /**
   * 读取历史数据文件
   * @returns {object} 历史数据对象
   */
  readHistoryFile() {
    try {
      const content = fs.readFileSync(this.historyFile, 'utf8');
      return JSON.parse(content);
    } catch (error) {
      this.logger.error('读取历史数据文件失败', error);
      // 返回空数据结构
      return {
        version: '1.0',
        records: []
      };
    }
  }

  /**
   * 查询历史数据
   * @param {number} startTime - 开始时间戳（可选）
   * @param {number} endTime - 结束时间戳（可选）
   * @param {string} dataType - 数据类型（可选，如 'cpuTemp', 'waterTemp' 等）
   * @returns {array} 符合条件的历史数据记录
   */
  queryHistory(startTime, endTime, dataType) {
    try {
      // 尝试从缓存获取
      const cacheKey = `query_${startTime}_${endTime}_${dataType}`;
      const cached = this.getCachedData(cacheKey);
      if (cached) {
        this.logger.debug('从缓存返回查询结果', { cacheKey });
        return cached;
      }

      // 读取历史数据
      const historyData = this.readHistoryFile();
      let records = historyData.records;

      // 按时间范围过滤
      if (startTime) {
        records = records.filter(r => r.timestamp >= startTime);
      }
      if (endTime) {
        records = records.filter(r => r.timestamp <= endTime);
      }

      // 按数据类型过滤（如果指定了特定类型，只返回该字段）
      let result;
      if (dataType && dataType !== 'all') {
        result = records.map(r => ({
          timestamp: r.timestamp,
          value: r[dataType]
        }));
      } else {
        result = records;
      }

      // 缓存查询结果（缓存5分钟）
      this.setCachedData(cacheKey, result, 5 * 60 * 1000);

      this.logger.debug('查询历史数据', {
        startTime,
        endTime,
        dataType,
        resultCount: result.length
      });

      return result;
    } catch (error) {
      this.logger.error('查询历史数据失败', error);
      return [];
    }
  }

  /**
   * 获取缓存数据
   * @param {string} key - 缓存键
   * @returns {any} 缓存的数据，如果不存在或已过期则返回null
   */
  getCachedData(key) {
    const cached = this.cache.get(key);
    if (cached && cached.expireTime > Date.now()) {
      return cached.data;
    }
    // 缓存已过期，删除
    if (cached) {
      this.cache.delete(key);
      if (this.cacheTimers.has(key)) {
        clearTimeout(this.cacheTimers.get(key));
        this.cacheTimers.delete(key);
      }
    }
    return null;
  }

  /**
   * 设置缓存数据
   * @param {string} key - 缓存键
   * @param {any} value - 缓存值
   * @param {number} ttl - 生存时间（毫秒）
   */
  setCachedData(key, value, ttl) {
    const expireTime = Date.now() + ttl;
    this.cache.set(key, {
      data: value,
      expireTime
    });

    // 设置定时器自动清理过期缓存
    const timer = setTimeout(() => {
      this.cache.delete(key);
      this.cacheTimers.delete(key);
      this.logger.debug('清理过期缓存', { key });
    }, ttl);

    this.cacheTimers.set(key, timer);
    
    this.logger.debug('设置缓存数据', { key, ttl });
  }

  /**
   * 清理过期数据（保留指定天数的数据）
   * @returns {number} 删除的记录数
   */
  cleanupOldData() {
    try {
      const cutoffTime = Date.now() - (this.retentionDays * 24 * 60 * 60 * 1000);
      
      // 读取历史数据
      const historyData = this.readHistoryFile();
      const originalCount = historyData.records.length;
      
      // 过滤掉过期数据
      historyData.records = historyData.records.filter(r => r.timestamp >= cutoffTime);
      
      const deletedCount = originalCount - historyData.records.length;
      
      if (deletedCount > 0) {
        // 写回文件
        fs.writeFileSync(this.historyFile, JSON.stringify(historyData, null, 2));
        
        this.logger.info('清理过期数据完成', {
          deletedCount,
          remainingCount: historyData.records.length,
          cutoffDate: new Date(cutoffTime).toISOString()
        });
      } else {
        this.logger.debug('没有需要清理的过期数据');
      }
      
      return deletedCount;
    } catch (error) {
      this.logger.error('清理过期数据失败', error);
      return 0;
    }
  }

  /**
   * 启动定期清理任务
   */
  startCleanupTask() {
    // 每天凌晨2点执行清理任务
    const scheduleNextCleanup = () => {
      const now = new Date();
      const tomorrow = new Date(now);
      tomorrow.setDate(tomorrow.getDate() + 1);
      tomorrow.setHours(2, 0, 0, 0);
      
      const timeUntilCleanup = tomorrow - now;
      
      setTimeout(() => {
        this.cleanupOldData();
        scheduleNextCleanup(); // 安排下一次清理
      }, timeUntilCleanup);
      
      this.logger.info('已安排下一次数据清理任务', {
        nextCleanup: tomorrow.toISOString()
      });
    };
    
    scheduleNextCleanup();
  }

  /**
   * 清空所有缓存
   */
  clearCache() {
    // 清除所有定时器
    for (const timer of this.cacheTimers.values()) {
      clearTimeout(timer);
    }
    
    this.cache.clear();
    this.cacheTimers.clear();
    
    this.logger.info('清空所有缓存');
  }

  /**
   * 获取存储统计信息
   * @returns {object} 统计信息
   */
  getStats() {
    try {
      const historyData = this.readHistoryFile();
      const stats = fs.statSync(this.historyFile);
      
      return {
        recordCount: historyData.records.length,
        fileSize: stats.size,
        fileSizeKB: (stats.size / 1024).toFixed(2),
        cacheSize: this.cache.size,
        oldestRecord: historyData.records.length > 0 
          ? new Date(historyData.records[0].timestamp).toISOString()
          : null,
        newestRecord: historyData.records.length > 0
          ? new Date(historyData.records[historyData.records.length - 1].timestamp).toISOString()
          : null
      };
    } catch (error) {
      this.logger.error('获取存储统计信息失败', error);
      return null;
    }
  }
}

module.exports = Storage;
