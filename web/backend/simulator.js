/**
 * 模拟数据生成器模块
 * 用于在开发阶段生成符合真实范围的模拟传感器数据
 */

const Logger = require('./logger');

// 创建logger实例
const logger = new Logger({
  logPath: '../logs',
  level: 'INFO'
});

// 运行模式枚举
const MODES = {
  SILENT: 'silent',       // 静音模式
  BALANCED: 'balanced',   // 均衡模式
  PERFORMANCE: 'performance' // 性能模式
};

// 不同模式的数据范围定义
const DATA_RANGES = {
  [MODES.SILENT]: {
    cpuTemp: { min: 50, max: 70 },      // CPU温度 (°C)
    waterTemp: { min: 25, max: 35 },    // 水温 (°C)
    flowRate: { min: 80, max: 100 },    // 流量 (L/h)
    pumpSpeed: { min: 1500, max: 2000 }, // 泵转速 (RPM)
    fanSpeed: { min: 800, max: 1200 },   // 风扇转速 (RPM)
    power: { min: 15, max: 25 }          // 功耗 (W)
  },
  [MODES.BALANCED]: {
    cpuTemp: { min: 55, max: 75 },
    waterTemp: { min: 28, max: 38 },
    flowRate: { min: 100, max: 130 },
    pumpSpeed: { min: 2000, max: 2500 },
    fanSpeed: { min: 1200, max: 1800 },
    power: { min: 20, max: 30 }
  },
  [MODES.PERFORMANCE]: {
    cpuTemp: { min: 60, max: 80 },
    waterTemp: { min: 30, max: 40 },
    flowRate: { min: 120, max: 150 },
    pumpSpeed: { min: 2500, max: 3000 },
    fanSpeed: { min: 1800, max: 2500 },
    power: { min: 25, max: 40 }
  }
};

// 故障类型定义
const FAULT_TYPES = {
  PUMP_ABNORMAL: {
    code: 'PUMP_001',
    description: '水泵转速异常',
    severity: 'high',
    solution: '请检查水泵电源连接和控制信号，必要时更换水泵'
  },
  FAN_ABNORMAL: {
    code: 'FAN_001',
    description: '风扇转速异常',
    severity: 'medium',
    solution: '请检查风扇电源连接和PWM控制信号，清理风扇灰尘或更换风扇'
  },
  TEMP_HIGH: {
    code: 'TEMP_001',
    description: 'CPU温度过高',
    severity: 'high',
    solution: '请检查散热器安装是否牢固，冷却液是否充足，考虑降低CPU负载或提升散热性能'
  },
  FLOW_LOW: {
    code: 'FLOW_001',
    description: '流量过低',
    severity: 'medium',
    solution: '请检查水路是否有堵塞，水泵是否正常工作，冷却液是否充足'
  },
  SENSOR_ERROR: {
    code: 'SENSOR_001',
    description: '传感器读取错误',
    severity: 'low',
    solution: '请检查传感器连接线路，重启系统或更换故障传感器'
  }
};

class Simulator {
  constructor() {
    this.currentMode = MODES.BALANCED; // 默认均衡模式
    this.faultSimulation = null;       // 当前模拟的故障
    this.dataHistory = [];             // 数据历史（用于生成趋势）
    this.maxHistorySize = 10;          // 保留最近10个数据点用于趋势计算
    
    logger.info('模拟数据生成器初始化完成', { mode: this.currentMode });
  }

  /**
   * 生成指定范围内的随机数
   * @param {number} min - 最小值
   * @param {number} max - 最大值
   * @param {number} decimals - 小数位数
   * @returns {number} 随机数
   */
  randomInRange(min, max, decimals = 1) {
    const value = Math.random() * (max - min) + min;
    return Number(value.toFixed(decimals));
  }

  /**
   * 生成带趋势的随机数（模拟数据的周期性变化）
   * @param {number} min - 最小值
   * @param {number} max - 最大值
   * @param {number} lastValue - 上一次的值
   * @param {number} decimals - 小数位数
   * @returns {number} 随机数
   */
  randomWithTrend(min, max, lastValue, decimals = 1) {
    if (lastValue === undefined || lastValue === null) {
      return this.randomInRange(min, max, decimals);
    }

    // 在上一次值的基础上进行小幅度变化（±10%的范围）
    const range = max - min;
    const maxChange = range * 0.1;
    const change = this.randomInRange(-maxChange, maxChange, decimals);
    let newValue = lastValue + change;

    // 确保新值在有效范围内
    newValue = Math.max(min, Math.min(max, newValue));
    
    return Number(newValue.toFixed(decimals));
  }

  /**
   * 获取上一次的数据值
   * @param {string} key - 数据键名
   * @returns {number|undefined} 上一次的值
   */
  getLastValue(key) {
    if (this.dataHistory.length === 0) {
      return undefined;
    }
    return this.dataHistory[this.dataHistory.length - 1][key];
  }

  /**
   * 生成实时传感器数据
   * @returns {Object} 实时数据对象
   */
  generateRealtimeData() {
    const ranges = DATA_RANGES[this.currentMode];
    
    // 生成带趋势的数据
    const data = {
      cpuTemp: this.randomWithTrend(
        ranges.cpuTemp.min, 
        ranges.cpuTemp.max, 
        this.getLastValue('cpuTemp')
      ),
      waterTemp: this.randomWithTrend(
        ranges.waterTemp.min, 
        ranges.waterTemp.max, 
        this.getLastValue('waterTemp')
      ),
      flowRate: this.randomWithTrend(
        ranges.flowRate.min, 
        ranges.flowRate.max, 
        this.getLastValue('flowRate')
      ),
      pumpSpeed: Math.round(this.randomWithTrend(
        ranges.pumpSpeed.min, 
        ranges.pumpSpeed.max, 
        this.getLastValue('pumpSpeed'),
        0
      )),
      fanSpeed: Math.round(this.randomWithTrend(
        ranges.fanSpeed.min, 
        ranges.fanSpeed.max, 
        this.getLastValue('fanSpeed'),
        0
      )),
      power: this.randomWithTrend(
        ranges.power.min, 
        ranges.power.max, 
        this.getLastValue('power')
      )
    };

    // 如果有故障模拟，调整相应的数据
    if (this.faultSimulation) {
      this.applyFaultToData(data);
    }

    // 保存到历史记录
    this.dataHistory.push(data);
    if (this.dataHistory.length > this.maxHistorySize) {
      this.dataHistory.shift();
    }

    logger.debug('生成实时数据', { mode: this.currentMode, data });
    
    return data;
  }

  /**
   * 应用故障模拟到数据
   * @param {Object} data - 数据对象
   */
  applyFaultToData(data) {
    switch (this.faultSimulation) {
      case 'PUMP_ABNORMAL':
        // 泵转速异常：降低到正常值的50%
        data.pumpSpeed = Math.round(data.pumpSpeed * 0.5);
        data.flowRate = data.flowRate * 0.6; // 流量也会降低
        break;
      case 'FAN_ABNORMAL':
        // 风扇转速异常：降低到正常值的40%
        data.fanSpeed = Math.round(data.fanSpeed * 0.4);
        data.cpuTemp += 10; // 温度会升高
        data.waterTemp += 5;
        break;
      case 'TEMP_HIGH':
        // 温度过高
        data.cpuTemp += 15;
        data.waterTemp += 8;
        break;
      case 'FLOW_LOW':
        // 流量过低
        data.flowRate = data.flowRate * 0.5;
        break;
      case 'SENSOR_ERROR':
        // 传感器错误：随机一个传感器返回异常值
        const sensors = ['cpuTemp', 'waterTemp', 'flowRate'];
        const errorSensor = sensors[Math.floor(Math.random() * sensors.length)];
        data[errorSensor] = -999; // 错误值
        break;
    }
  }

  /**
   * 计算健康度评分
   * @param {Object} data - 实时数据
   * @returns {number} 健康度评分 (0-100)
   */
  calculateHealthScore(data) {
    let score = 100;
    const ranges = DATA_RANGES[this.currentMode];

    // 检查各项数据是否在正常范围内
    // CPU温度：超出范围每度扣2分
    if (data.cpuTemp > ranges.cpuTemp.max) {
      score -= (data.cpuTemp - ranges.cpuTemp.max) * 2;
    }

    // 水温：超出范围每度扣1.5分
    if (data.waterTemp > ranges.waterTemp.max) {
      score -= (data.waterTemp - ranges.waterTemp.max) * 1.5;
    }

    // 流量：低于最小值每单位扣1分
    if (data.flowRate < ranges.flowRate.min) {
      score -= (ranges.flowRate.min - data.flowRate) * 1;
    }

    // 泵转速：低于最小值每100RPM扣2分
    if (data.pumpSpeed < ranges.pumpSpeed.min) {
      score -= ((ranges.pumpSpeed.min - data.pumpSpeed) / 100) * 2;
    }

    // 风扇转速：低于最小值每100RPM扣1.5分
    if (data.fanSpeed < ranges.fanSpeed.min) {
      score -= ((ranges.fanSpeed.min - data.fanSpeed) / 100) * 1.5;
    }

    // 传感器错误：直接扣30分
    if (data.cpuTemp === -999 || data.waterTemp === -999 || data.flowRate === -999) {
      score -= 30;
    }

    // 确保评分在0-100范围内
    score = Math.max(0, Math.min(100, Math.round(score)));

    return score;
  }

  /**
   * 检测故障
   * @param {Object} data - 实时数据
   * @param {number} healthScore - 健康度评分
   * @returns {Array} 故障列表
   */
  detectFaults(data, healthScore) {
    const faults = [];
    const ranges = DATA_RANGES[this.currentMode];
    const timestamp = Date.now();

    // 检查泵转速
    if (data.pumpSpeed < ranges.pumpSpeed.min * 0.7) {
      faults.push({
        ...FAULT_TYPES.PUMP_ABNORMAL,
        timestamp,
        details: `当前转速: ${data.pumpSpeed} RPM, 正常范围: ${ranges.pumpSpeed.min}-${ranges.pumpSpeed.max} RPM`
      });
    }

    // 检查风扇转速
    if (data.fanSpeed < ranges.fanSpeed.min * 0.7) {
      faults.push({
        ...FAULT_TYPES.FAN_ABNORMAL,
        timestamp,
        details: `当前转速: ${data.fanSpeed} RPM, 正常范围: ${ranges.fanSpeed.min}-${ranges.fanSpeed.max} RPM`
      });
    }

    // 检查CPU温度
    if (data.cpuTemp > ranges.cpuTemp.max + 5) {
      faults.push({
        ...FAULT_TYPES.TEMP_HIGH,
        timestamp,
        details: `当前温度: ${data.cpuTemp}°C, 正常范围: ${ranges.cpuTemp.min}-${ranges.cpuTemp.max}°C`
      });
    }

    // 检查流量
    if (data.flowRate < ranges.flowRate.min * 0.7) {
      faults.push({
        ...FAULT_TYPES.FLOW_LOW,
        timestamp,
        details: `当前流量: ${data.flowRate} L/h, 正常范围: ${ranges.flowRate.min}-${ranges.flowRate.max} L/h`
      });
    }

    // 检查传感器错误
    if (data.cpuTemp === -999 || data.waterTemp === -999 || data.flowRate === -999) {
      faults.push({
        ...FAULT_TYPES.SENSOR_ERROR,
        timestamp,
        details: '传感器返回异常值'
      });
    }

    return faults;
  }

  /**
   * 生成健康状态数据
   * @returns {Object} 健康状态对象
   */
  generateHealthStatus() {
    // 先生成实时数据
    const realtimeData = this.generateRealtimeData();
    
    // 计算健康度评分
    const healthScore = this.calculateHealthScore(realtimeData);
    
    // 检测故障
    const faults = this.detectFaults(realtimeData, healthScore);
    
    // 确定各子系统状态
    const status = {
      pump: 'normal',
      fan: 'normal',
      sensor: 'normal',
      cooling: 'normal'
    };

    // 根据故障更新子系统状态
    faults.forEach(fault => {
      if (fault.code.startsWith('PUMP')) {
        status.pump = fault.severity === 'high' ? 'fault' : 'warning';
      } else if (fault.code.startsWith('FAN')) {
        status.fan = fault.severity === 'high' ? 'fault' : 'warning';
      } else if (fault.code.startsWith('SENSOR')) {
        status.sensor = fault.severity === 'high' ? 'fault' : 'warning';
      } else if (fault.code.startsWith('TEMP') || fault.code.startsWith('FLOW')) {
        status.cooling = fault.severity === 'high' ? 'fault' : 'warning';
      }
    });

    const healthStatus = {
      healthScore,
      status,
      faults
    };

    logger.debug('生成健康状态', healthStatus);

    return healthStatus;
  }

  /**
   * 设置运行模式
   * @param {string} mode - 运行模式 (silent/balanced/performance)
   * @returns {boolean} 是否设置成功
   */
  setMode(mode) {
    const normalizedMode = mode.toLowerCase();
    
    if (!Object.values(MODES).includes(normalizedMode)) {
      logger.error('无效的运行模式', { mode });
      return false;
    }

    const oldMode = this.currentMode;
    this.currentMode = normalizedMode;
    
    // 清空历史数据，因为模式变化会导致数据范围变化
    this.dataHistory = [];
    
    logger.info('运行模式已切换', { oldMode, newMode: this.currentMode });
    
    return true;
  }

  /**
   * 获取当前运行模式
   * @returns {string} 当前运行模式
   */
  getMode() {
    return this.currentMode;
  }

  /**
   * 模拟故障
   * @param {string} faultType - 故障类型
   * @returns {boolean} 是否设置成功
   */
  simulateFault(faultType) {
    if (faultType && !FAULT_TYPES[faultType]) {
      logger.error('无效的故障类型', { faultType });
      return false;
    }

    this.faultSimulation = faultType;
    
    if (faultType) {
      logger.info('开始模拟故障', { faultType });
    } else {
      logger.info('停止故障模拟');
    }
    
    return true;
  }

  /**
   * 获取当前模拟的故障
   * @returns {string|null} 故障类型
   */
  getCurrentFault() {
    return this.faultSimulation;
  }

  /**
   * 获取所有可用的运行模式
   * @returns {Object} 运行模式枚举
   */
  static getModes() {
    return MODES;
  }

  /**
   * 获取所有故障类型
   * @returns {Object} 故障类型定义
   */
  static getFaultTypes() {
    return FAULT_TYPES;
  }
}

// 导出单例实例
const simulator = new Simulator();

module.exports = simulator;
