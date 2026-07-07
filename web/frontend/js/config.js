/**
 * 配置管理模块
 * 负责管理前端配置，包括WebSocket连接配置、主题配置等
 * 使用localStorage进行持久化存储
 */

/**
 * 默认配置
 */
const DEFAULT_CONFIG = {
  // WebSocket配置
  websocket: {
    host: 'localhost',
    port: 8080,
    autoReconnect: true,
    reconnectInterval: 3000,  // 重连间隔（毫秒）
    maxReconnectAttempts: 3   // 最大重连次数
  },
  
  // 主题配置
  theme: 'light',  // 'light' | 'dark' | 'auto'
  
  // 界面配置
  ui: {
    refreshInterval: 2000,    // 数据刷新间隔（毫秒）
    chartMaxPoints: 60,       // 实时图表最大数据点数
    animationEnabled: true,   // 是否启用动画
    notificationDuration: 3000 // 通知显示时长（毫秒）
  },
  
  // 数据配置
  data: {
    historySampleThreshold: 1000,  // 历史数据采样阈值
    cacheEnabled: true,            // 是否启用缓存
    cacheDuration: 60000           // 缓存有效期（毫秒）
  },
  
  // 告警阈值配置
  thresholds: {
    cpuTemp: { warning: 75, danger: 85 },
    waterTemp: { warning: 38, danger: 45 },
    flowRate: { warning: 80, danger: 70 },
    pumpSpeed: { warning: 1000, danger: 800 },
    fanSpeed: { warning: 500, danger: 300 },
    power: { warning: 35, danger: 45 }
  }
};

/**
 * 配置管理类
 */
class ConfigManager {
  constructor() {
    this.config = null;
    this.storageKey = 'water_cooling_config';
    this.listeners = new Map(); // 配置变更监听器
    this.init();
  }
  
  /**
   * 初始化配置管理器
   */
  init() {
    this.loadConfig();
    
    // 监听storage事件，实现跨标签页配置同步
    window.addEventListener('storage', (e) => {
      if (e.key === this.storageKey) {
        this.loadConfig();
        this.notifyListeners('*'); // 通知所有监听器
      }
    });
  }
  
  /**
   * 从localStorage加载配置
   * @returns {object} 配置对象
   */
  loadConfig() {
    try {
      const savedConfig = localStorage.getItem(this.storageKey);
      
      if (savedConfig) {
        // 解析保存的配置
        const parsed = JSON.parse(savedConfig);
        
        // 合并默认配置和保存的配置（保存的配置优先）
        this.config = this.mergeConfig(DEFAULT_CONFIG, parsed);
      } else {
        // 如果没有保存的配置，使用默认配置
        this.config = deepClone(DEFAULT_CONFIG);
        
        // 检测系统主题偏好
        if (this.config.theme === 'auto') {
          this.config.theme = this.detectSystemTheme();
        }
      }
      
      return this.config;
    } catch (error) {
      console.error('加载配置失败:', error);
      // 加载失败时使用默认配置
      this.config = deepClone(DEFAULT_CONFIG);
      return this.config;
    }
  }
  
  /**
   * 保存配置到localStorage
   * @param {object} config - 要保存的配置对象（可选，默认保存当前配置）
   * @returns {boolean} 是否保存成功
   */
  saveConfig(config = null) {
    try {
      const configToSave = config || this.config;
      
      // 验证配置
      if (!this.validateConfig(configToSave)) {
        console.error('配置验证失败');
        return false;
      }
      
      // 保存到localStorage
      localStorage.setItem(this.storageKey, JSON.stringify(configToSave));
      
      // 更新当前配置
      if (config) {
        this.config = deepClone(config);
      }
      
      return true;
    } catch (error) {
      console.error('保存配置失败:', error);
      return false;
    }
  }
  
  /**
   * 获取完整配置
   * @returns {object} 配置对象
   */
  getConfig() {
    return deepClone(this.config);
  }
  
  /**
   * 获取指定路径的配置值
   * @param {string} path - 配置路径，使用点号分隔，如 'websocket.host'
   * @param {*} defaultValue - 默认值（可选）
   * @returns {*} 配置值
   */
  get(path, defaultValue = undefined) {
    const keys = path.split('.');
    let value = this.config;
    
    for (const key of keys) {
      if (value && typeof value === 'object' && key in value) {
        value = value[key];
      } else {
        return defaultValue;
      }
    }
    
    return value;
  }
  
  /**
   * 设置指定路径的配置值
   * @param {string} path - 配置路径，使用点号分隔
   * @param {*} value - 配置值
   * @param {boolean} save - 是否立即保存（默认true）
   * @returns {boolean} 是否设置成功
   */
  set(path, value, save = true) {
    try {
      const keys = path.split('.');
      let target = this.config;
      
      // 遍历到倒数第二个key
      for (let i = 0; i < keys.length - 1; i++) {
        const key = keys[i];
        if (!(key in target) || typeof target[key] !== 'object') {
          target[key] = {};
        }
        target = target[key];
      }
      
      // 设置最后一个key的值
      const lastKey = keys[keys.length - 1];
      const oldValue = target[lastKey];
      target[lastKey] = value;
      
      // 保存配置
      if (save) {
        const saved = this.saveConfig();
        if (!saved) {
          // 保存失败，恢复旧值
          target[lastKey] = oldValue;
          return false;
        }
      }
      
      // 通知监听器
      this.notifyListeners(path, value, oldValue);
      
      return true;
    } catch (error) {
      console.error('设置配置失败:', error);
      return false;
    }
  }
  
  /**
   * 获取WebSocket配置
   * @returns {object} WebSocket配置对象
   */
  getWebSocketConfig() {
    return {
      host: this.get('websocket.host'),
      port: this.get('websocket.port'),
      autoReconnect: this.get('websocket.autoReconnect'),
      reconnectInterval: this.get('websocket.reconnectInterval'),
      maxReconnectAttempts: this.get('websocket.maxReconnectAttempts')
    };
  }
  
  /**
   * 设置WebSocket配置
   * @param {string} host - 服务器地址
   * @param {number} port - 服务器端口
   * @param {boolean} save - 是否立即保存（默认true）
   * @returns {boolean} 是否设置成功
   */
  setWebSocketConfig(host, port, save = true) {
    try {
      // 验证参数
      if (!host || typeof host !== 'string') {
        console.error('无效的服务器地址');
        return false;
      }
      
      if (!port || typeof port !== 'number' || port < 1 || port > 65535) {
        console.error('无效的端口号');
        return false;
      }
      
      // 设置配置
      this.config.websocket.host = host;
      this.config.websocket.port = port;
      
      // 保存配置
      if (save) {
        const saved = this.saveConfig();
        if (!saved) {
          return false;
        }
      }
      
      // 通知监听器
      this.notifyListeners('websocket', this.config.websocket);
      
      return true;
    } catch (error) {
      console.error('设置WebSocket配置失败:', error);
      return false;
    }
  }
  
  /**
   * 获取主题配置
   * @returns {string} 主题名称 ('light' | 'dark' | 'auto')
   */
  getTheme() {
    const theme = this.get('theme');
    
    // 如果是auto，返回实际应用的主题
    if (theme === 'auto') {
      return this.detectSystemTheme();
    }
    
    return theme;
  }
  
  /**
   * 设置主题
   * @param {string} theme - 主题名称 ('light' | 'dark' | 'auto')
   * @param {boolean} save - 是否立即保存（默认true）
   * @returns {boolean} 是否设置成功
   */
  setTheme(theme, save = true) {
    // 验证主题值
    if (!['light', 'dark', 'auto'].includes(theme)) {
      console.error('无效的主题值:', theme);
      return false;
    }
    
    return this.set('theme', theme, save);
  }
  
  /**
   * 切换主题（在light和dark之间切换）
   * @returns {string} 切换后的主题
   */
  toggleTheme() {
    const currentTheme = this.getTheme();
    const newTheme = currentTheme === 'light' ? 'dark' : 'light';
    this.setTheme(newTheme);
    return newTheme;
  }
  
  /**
   * 检测系统主题偏好
   * @returns {string} 'light' | 'dark'
   */
  detectSystemTheme() {
    if (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) {
      return 'dark';
    }
    return 'light';
  }
  
  /**
   * 重置配置为默认值
   * @param {boolean} save - 是否立即保存（默认true）
   * @returns {boolean} 是否重置成功
   */
  resetConfig(save = true) {
    try {
      this.config = deepClone(DEFAULT_CONFIG);
      
      if (save) {
        const saved = this.saveConfig();
        if (!saved) {
          return false;
        }
      }
      
      // 通知所有监听器
      this.notifyListeners('*');
      
      return true;
    } catch (error) {
      console.error('重置配置失败:', error);
      return false;
    }
  }
  
  /**
   * 导出配置为JSON字符串
   * @returns {string} JSON格式的配置字符串
   */
  exportConfig() {
    try {
      return JSON.stringify(this.config, null, 2);
    } catch (error) {
      console.error('导出配置失败:', error);
      return null;
    }
  }
  
  /**
   * 从JSON字符串导入配置
   * @param {string} jsonString - JSON格式的配置字符串
   * @param {boolean} save - 是否立即保存（默认true）
   * @returns {boolean} 是否导入成功
   */
  importConfig(jsonString, save = true) {
    try {
      const config = JSON.parse(jsonString);
      
      // 验证配置
      if (!this.validateConfig(config)) {
        console.error('导入的配置无效');
        return false;
      }
      
      // 合并配置
      this.config = this.mergeConfig(DEFAULT_CONFIG, config);
      
      if (save) {
        const saved = this.saveConfig();
        if (!saved) {
          return false;
        }
      }
      
      // 通知所有监听器
      this.notifyListeners('*');
      
      return true;
    } catch (error) {
      console.error('导入配置失败:', error);
      return false;
    }
  }
  
  /**
   * 验证配置对象
   * @param {object} config - 要验证的配置对象
   * @returns {boolean} 配置是否有效
   */
  validateConfig(config) {
    try {
      // 检查必需的字段
      if (!config || typeof config !== 'object') {
        return false;
      }
      
      // 验证WebSocket配置
      if (config.websocket) {
        const { host, port } = config.websocket;
        if (host && typeof host !== 'string') {
          return false;
        }
        if (port && (typeof port !== 'number' || port < 1 || port > 65535)) {
          return false;
        }
      }
      
      // 验证主题配置
      if (config.theme && !['light', 'dark', 'auto'].includes(config.theme)) {
        return false;
      }
      
      // 验证UI配置
      if (config.ui) {
        const { refreshInterval, chartMaxPoints } = config.ui;
        if (refreshInterval && (typeof refreshInterval !== 'number' || refreshInterval < 100)) {
          return false;
        }
        if (chartMaxPoints && (typeof chartMaxPoints !== 'number' || chartMaxPoints < 10)) {
          return false;
        }
      }
      
      return true;
    } catch (error) {
      console.error('验证配置时出错:', error);
      return false;
    }
  }
  
  /**
   * 合并配置对象（深度合并）
   * @param {object} defaultConfig - 默认配置
   * @param {object} userConfig - 用户配置
   * @returns {object} 合并后的配置
   */
  mergeConfig(defaultConfig, userConfig) {
    const result = deepClone(defaultConfig);
    
    for (const key in userConfig) {
      if (userConfig.hasOwnProperty(key)) {
        if (typeof userConfig[key] === 'object' && !Array.isArray(userConfig[key]) && userConfig[key] !== null) {
          // 递归合并对象
          result[key] = this.mergeConfig(result[key] || {}, userConfig[key]);
        } else {
          // 直接赋值
          result[key] = userConfig[key];
        }
      }
    }
    
    return result;
  }
  
  /**
   * 注册配置变更监听器
   * @param {string} path - 要监听的配置路径，'*' 表示监听所有变更
   * @param {Function} callback - 回调函数 (newValue, oldValue, path) => void
   * @returns {Function} 取消监听的函数
   */
  onChange(path, callback) {
    if (typeof callback !== 'function') {
      console.error('回调函数无效');
      return () => {};
    }
    
    if (!this.listeners.has(path)) {
      this.listeners.set(path, []);
    }
    
    this.listeners.get(path).push(callback);
    
    // 返回取消监听的函数
    return () => {
      const callbacks = this.listeners.get(path);
      if (callbacks) {
        const index = callbacks.indexOf(callback);
        if (index > -1) {
          callbacks.splice(index, 1);
        }
      }
    };
  }
  
  /**
   * 通知配置变更监听器
   * @param {string} path - 变更的配置路径
   * @param {*} newValue - 新值
   * @param {*} oldValue - 旧值
   */
  notifyListeners(path, newValue = null, oldValue = null) {
    // 通知特定路径的监听器
    if (this.listeners.has(path)) {
      this.listeners.get(path).forEach(callback => {
        try {
          callback(newValue, oldValue, path);
        } catch (error) {
          console.error('执行配置变更回调时出错:', error);
        }
      });
    }
    
    // 通知通配符监听器
    if (path !== '*' && this.listeners.has('*')) {
      this.listeners.get('*').forEach(callback => {
        try {
          callback(newValue, oldValue, path);
        } catch (error) {
          console.error('执行配置变更回调时出错:', error);
        }
      });
    }
  }
  
  /**
   * 清除所有保存的配置
   * @returns {boolean} 是否清除成功
   */
  clearConfig() {
    try {
      localStorage.removeItem(this.storageKey);
      this.config = deepClone(DEFAULT_CONFIG);
      this.notifyListeners('*');
      return true;
    } catch (error) {
      console.error('清除配置失败:', error);
      return false;
    }
  }
}

// 创建全局配置管理器实例
const configManager = new ConfigManager();

// 暴露到全局作用域（浏览器环境）
if (typeof window !== 'undefined') {
  window.configManager = configManager;
  window.ConfigManager = ConfigManager;
  window.DEFAULT_CONFIG = DEFAULT_CONFIG;
}

// 导出配置管理器和相关函数（用于模块化）
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    ConfigManager,
    configManager,
    DEFAULT_CONFIG
  };
}
