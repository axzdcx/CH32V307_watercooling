/**
 * 工具函数模块
 * 提供通用的工具函数，包括时间格式化、数值格式化、防抖节流等
 */

/**
 * 格式化时间戳
 * @param {number} timestamp - Unix时间戳（毫秒）
 * @param {string} format - 格式字符串，支持：
 *   - 'YYYY-MM-DD HH:mm:ss' (默认)
 *   - 'YYYY-MM-DD'
 *   - 'HH:mm:ss'
 *   - 'YYYY/MM/DD HH:mm:ss'
 * @returns {string} 格式化后的时间字符串
 */
function formatTimestamp(timestamp, format = 'YYYY-MM-DD HH:mm:ss') {
  // 检查输入是否有效
  if (timestamp === null || timestamp === undefined || timestamp === '') {
    return '无效时间';
  }
  
  const date = new Date(timestamp);
  
  // 检查日期是否有效
  if (isNaN(date.getTime())) {
    return '无效时间';
  }
  
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  const hours = String(date.getHours()).padStart(2, '0');
  const minutes = String(date.getMinutes()).padStart(2, '0');
  const seconds = String(date.getSeconds()).padStart(2, '0');
  
  // 根据格式字符串替换
  return format
    .replace('YYYY', year)
    .replace('MM', month)
    .replace('DD', day)
    .replace('HH', hours)
    .replace('mm', minutes)
    .replace('ss', seconds);
}

/**
 * 格式化数值（保留指定小数位）
 * @param {number} value - 要格式化的数值
 * @param {number} decimals - 保留的小数位数，默认为2
 * @returns {string} 格式化后的数值字符串
 */
function formatNumber(value, decimals = 2) {
  // 检查是否为有效数字
  if (typeof value !== 'number' || isNaN(value)) {
    return '0.00';
  }
  
  return value.toFixed(decimals);
}

/**
 * 防抖函数
 * 在事件被触发n秒后再执行回调，如果在这n秒内又被触发，则重新计时
 * @param {Function} func - 要执行的函数
 * @param {number} delay - 延迟时间（毫秒）
 * @returns {Function} 防抖后的函数
 */
function debounce(func, delay = 300) {
  let timeoutId = null;
  
  return function(...args) {
    // 清除之前的定时器
    if (timeoutId) {
      clearTimeout(timeoutId);
    }
    
    // 设置新的定时器
    timeoutId = setTimeout(() => {
      func.apply(this, args);
      timeoutId = null;
    }, delay);
  };
}

/**
 * 节流函数
 * 规定在一个单位时间内，只能触发一次函数。如果这个单位时间内触发多次函数，只有一次生效
 * @param {Function} func - 要执行的函数
 * @param {number} interval - 时间间隔（毫秒）
 * @returns {Function} 节流后的函数
 */
function throttle(func, interval = 300) {
  let lastTime = 0;
  let timeoutId = null;
  
  return function(...args) {
    const now = Date.now();
    const remaining = interval - (now - lastTime);
    
    // 清除之前的定时器
    if (timeoutId) {
      clearTimeout(timeoutId);
      timeoutId = null;
    }
    
    if (remaining <= 0) {
      // 如果距离上次执行已经超过间隔时间，立即执行
      lastTime = now;
      func.apply(this, args);
    } else {
      // 否则设置定时器，在剩余时间后执行
      timeoutId = setTimeout(() => {
        lastTime = Date.now();
        func.apply(this, args);
        timeoutId = null;
      }, remaining);
    }
  };
}

/**
 * 深拷贝对象
 * 创建对象的深层副本，避免引用问题
 * @param {*} obj - 要拷贝的对象
 * @returns {*} 深拷贝后的对象
 */
function deepClone(obj) {
  // 处理 null 和 undefined
  if (obj === null || obj === undefined) {
    return obj;
  }
  
  // 处理基本类型
  if (typeof obj !== 'object') {
    return obj;
  }
  
  // 处理日期对象
  if (obj instanceof Date) {
    return new Date(obj.getTime());
  }
  
  // 处理数组
  if (Array.isArray(obj)) {
    return obj.map(item => deepClone(item));
  }
  
  // 处理普通对象
  const clonedObj = {};
  for (const key in obj) {
    if (obj.hasOwnProperty(key)) {
      clonedObj[key] = deepClone(obj[key]);
    }
  }
  
  return clonedObj;
}

/**
 * 生成唯一ID
 * 生成一个基于时间戳和随机数的唯一标识符
 * @param {string} prefix - ID前缀，默认为空字符串
 * @returns {string} 唯一ID字符串
 */
function generateId(prefix = '') {
  const timestamp = Date.now().toString(36); // 将时间戳转换为36进制
  const randomStr = Math.random().toString(36).substring(2, 10); // 生成随机字符串
  
  return prefix ? `${prefix}_${timestamp}_${randomStr}` : `${timestamp}_${randomStr}`;
}

/**
 * 验证数值是否在指定范围内
 * @param {number} value - 要验证的数值
 * @param {number} min - 最小值
 * @param {number} max - 最大值
 * @returns {boolean} 是否在范围内
 */
function isInRange(value, min, max) {
  return typeof value === 'number' && !isNaN(value) && value >= min && value <= max;
}

/**
 * 获取数据状态（正常/警告/危险）
 * 根据数值和阈值判断数据状态
 * @param {number} value - 数据值
 * @param {object} thresholds - 阈值对象 {warning: number, danger: number}
 * @returns {string} 状态：'normal' | 'warning' | 'danger'
 */
function getDataStatus(value, thresholds) {
  if (typeof value !== 'number' || isNaN(value)) {
    return 'normal';
  }
  
  if (thresholds.danger !== undefined && value >= thresholds.danger) {
    return 'danger';
  }
  
  if (thresholds.warning !== undefined && value >= thresholds.warning) {
    return 'warning';
  }
  
  return 'normal';
}

/**
 * 数据采样
 * 当数据点过多时，进行采样以提高性能
 * @param {Array} data - 原始数据数组
 * @param {number} maxPoints - 最大数据点数
 * @returns {Array} 采样后的数据数组
 */
function sampleData(data, maxPoints = 1000) {
  if (!Array.isArray(data) || data.length <= maxPoints) {
    return data;
  }
  
  const step = Math.ceil(data.length / maxPoints);
  const sampledData = [];
  
  for (let i = 0; i < data.length; i += step) {
    sampledData.push(data[i]);
  }
  
  return sampledData;
}

/**
 * 计算数组的平均值
 * @param {Array<number>} arr - 数值数组
 * @returns {number} 平均值
 */
function average(arr) {
  if (!Array.isArray(arr) || arr.length === 0) {
    return 0;
  }
  
  const sum = arr.reduce((acc, val) => acc + (typeof val === 'number' ? val : 0), 0);
  return sum / arr.length;
}

/**
 * 导出为CSV格式
 * 将数据数组转换为CSV格式字符串
 * @param {Array<object>} data - 数据数组
 * @param {Array<string>} headers - 表头数组
 * @returns {string} CSV格式字符串
 */
function exportToCSV(data, headers) {
  if (!Array.isArray(data) || data.length === 0) {
    return '';
  }
  
  // 生成表头行
  const headerRow = headers.join(',');
  
  // 生成数据行
  const dataRows = data.map(row => {
    return headers.map(header => {
      const value = row[header];
      // 处理包含逗号的值，用引号包裹
      if (typeof value === 'string' && value.includes(',')) {
        return `"${value}"`;
      }
      return value !== undefined && value !== null ? value : '';
    }).join(',');
  });
  
  return [headerRow, ...dataRows].join('\n');
}

// 暴露到全局作用域（浏览器环境）
if (typeof window !== 'undefined') {
  window.utils = {
    formatTimestamp,
    formatNumber,
    debounce,
    throttle,
    deepClone,
    generateId,
    isInRange,
    getDataStatus,
    sampleData,
    average,
    exportToCSV
  };
}

// 导出所有函数（用于模块化）
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    formatTimestamp,
    formatNumber,
    debounce,
    throttle,
    deepClone,
    generateId,
    isInRange,
    getDataStatus,
    sampleData,
    average,
    exportToCSV
  };
}
