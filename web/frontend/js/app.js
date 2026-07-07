/**
 * 主应用模块
 * 负责集成所有模块，实现页面初始化流程和整体业务逻辑
 * 
 * 功能：
 * - 集成所有模块（websocket、ui、chart、config、utils）
 * - 实现页面初始化流程
 * - 实现WebSocket消息处理和UI更新
 * - 实现控制指令发送逻辑
 * - 实现配置界面交互
 * - 实现主题切换交互
 * - 实现错误处理和异常捕获
 */

/**
 * 主应用类
 */
class Application {
  constructor() {
    this.isInitialized = false;
    this.currentMode = 'balanced'; // 当前运行模式
    this.lastDataTimestamp = null; // 最后一次数据更新时间戳
    this.dataUpdateCheckInterval = null; // 数据更新检查定时器
    this.reconnectAttempts = 0; // 重连尝试次数
    this.maxReconnectAttempts = 3; // 最大重连次数
  }

  /**
   * 初始化应用
   */
  async init() {
    try {
      console.log('开始初始化应用...');

      // 显示加载动画
      uiManager.showLoading('正在初始化系统...');

      // 1. 初始化配置管理器（已在config.js中自动初始化）
      console.log('配置管理器已初始化');

      // 2. 初始化UI管理器
      uiManager.init();
      console.log('UI管理器已初始化');

      // 3. 初始化图表管理器
      chartManager.init();
      console.log('图表管理器已初始化');

      // 4. 设置全局错误处理
      this.setupGlobalErrorHandling();

      // 5. 设置WebSocket事件处理器
      this.setupWebSocketHandlers();

      // 6. 设置UI事件处理器
      this.setupUIEventHandlers();

      // 7. 加载配置并连接WebSocket服务器
      await this.connectToServer();

      // 8. 标记为已初始化
      this.isInitialized = true;

      // 隐藏加载动画
      uiManager.hideLoading();

      console.log('应用初始化完成');
      uiManager.showNotification('success', '系统初始化成功');

    } catch (error) {
      console.error('应用初始化失败:', error);
      uiManager.hideLoading();
      uiManager.showNotification('error', '系统初始化失败: ' + error.message);
    }
  }

  /**
   * 设置全局错误处理
   */
  setupGlobalErrorHandling() {
    // 捕获未处理的Promise拒绝
    window.addEventListener('unhandledrejection', (event) => {
      console.error('未处理的Promise拒绝:', event.reason);
      // 只有在有实际错误信息时才显示通知
      if (event.reason && event.reason.message) {
        uiManager.showNotification('error', '发生错误: ' + event.reason.message);
      }
      event.preventDefault();
    });

    // 捕获全局错误
    window.addEventListener('error', (event) => {
      console.error('全局错误:', event.error);
      // 只有在有实际错误信息时才显示通知
      if (event.error && event.error.message) {
        uiManager.showNotification('error', '发生错误: ' + event.error.message);
      }
      event.preventDefault();
    });

    console.log('全局错误处理已设置');
  }

  /**
   * 设置WebSocket事件处理器
   */
  setupWebSocketHandlers() {
    // 连接成功事件
    wsClient.on('connected', () => {
      console.log('WebSocket连接成功');
      uiManager.updateConnectionStatus('connected');
      uiManager.showNotification('success', 'WebSocket连接成功');
      this.reconnectAttempts = 0;
    });

    // 连接断开事件
    wsClient.on('disconnected', (data) => {
      console.log('WebSocket连接断开:', data);
      uiManager.updateConnectionStatus('disconnected');
      
      if (data.wasClean) {
        uiManager.showNotification('info', 'WebSocket连接已断开');
      } else {
        uiManager.showNotification('warning', 'WebSocket连接意外断开');
      }
    });

    // 连接中事件
    wsClient.on('connecting', () => {
      console.log('正在连接WebSocket...');
      uiManager.updateConnectionStatus('connecting');
    });

    // 重连中事件
    wsClient.on('reconnecting', (data) => {
      console.log(`正在进行第 ${data.attempt} 次重连...`);
      uiManager.showNotification('info', `正在尝试重新连接 (${data.attempt}/${data.maxAttempts})...`);
    });

    // 重连成功事件
    wsClient.on('reconnected', (data) => {
      console.log('重连成功');
      uiManager.showNotification('success', '重新连接成功');
    });

    // 重连失败事件
    wsClient.on('reconnect_failed', (data) => {
      console.log('重连失败，已达到最大重连次数');
      uiManager.showNotification('error', '连接失败，请检查服务器状态或配置');
    });

    // 错误事件
    wsClient.on('error', (data) => {
      console.error('WebSocket错误:', data.error);
      uiManager.showNotification('error', 'WebSocket错误: ' + data.error);
    });

    // 服务器错误消息
    wsClient.on('server_error', (message) => {
      console.error('服务器错误:', message.code, message.message);
      uiManager.showNotification('error', `服务器错误: ${message.message}`);
    });

    // 实时数据消息
    wsClient.on('realtime_data', (message) => {
      this.handleRealtimeData(message);
    });

    // 健康状态消息
    wsClient.on('health_status', (message) => {
      this.handleHealthStatus(message);
    });

    // 连接确认消息
    wsClient.on('connection_ack', (message) => {
      console.log('收到连接确认，客户端ID:', message.clientId);
    });

    console.log('WebSocket事件处理器已设置');
  }

  /**
   * 处理实时数据消息
   * @param {object} message - 实时数据消息
   */
  handleRealtimeData(message) {
    try {
      if (!message.data) {
        console.warn('实时数据消息缺少data字段');
        return;
      }

      const data = message.data;
      
      // 记录最后一次数据更新时间(使用修正后的timestamp)
      this.lastDataTimestamp = message.timestamp || Date.now();

      // 更新UI显示
      uiManager.updateRealtimeData(data);

      // 更新实时图表
      // 注意: 必须先展开data,再设置timestamp,确保使用修正后的timestamp
      chartManager.updateRealtimeChart({
        ...data,
        timestamp: this.lastDataTimestamp  // 使用修正后的timestamp覆盖data中可能存在的旧timestamp
      });

      console.log('实时数据已更新, timestamp:', this.lastDataTimestamp);
    } catch (error) {
      console.error('处理实时数据失败:', error);
    }
  }

  /**
   * 处理健康状态消息
   * @param {object} message - 健康状态消息
   */
  handleHealthStatus(message) {
    try {
      if (!message.data) {
        console.warn('健康状态消息缺少data字段');
        return;
      }

      // 更新健康状态显示
      uiManager.updateHealthStatus(message.data);

      console.log('健康状态已更新');
    } catch (error) {
      console.error('处理健康状态失败:', error);
    }
  }

  /**
   * 设置UI事件处理器
   */
  setupUIEventHandlers() {
    // 控制面板 - 模式切换
    this.setupModeControl();

    // 控制面板 - PID参数调节
    this.setupPIDControl();

    // 控制面板 - 系统重置
    this.setupSystemControl();

    // 历史数据查询
    this.setupHistoryQuery();

    // 配置管理
    this.setupConfigManagement();

    console.log('UI事件处理器已设置');
  }

  /**
   * 设置模式控制
   */
  setupModeControl() {
    const modeButtons = document.querySelectorAll('.mode-btn');
    
    modeButtons.forEach(btn => {
      btn.addEventListener('click', () => {
        const mode = btn.getAttribute('data-mode');
        this.switchMode(mode);
      });
    });
  }

  /**
   * 切换运行模式
   * @param {string} mode - 运行模式：'silent' | 'balanced' | 'performance'
   */
  switchMode(mode) {
    try {
      // 验证模式
      if (!['silent', 'balanced', 'performance'].includes(mode)) {
        throw new Error('无效的运行模式');
      }

      // 检查连接状态
      if (!wsClient.isConnected()) {
        uiManager.showNotification('warning', '未连接到服务器，无法切换模式');
        return;
      }

      // 发送模式切换指令
      const success = wsClient.setMode(mode);
      
      if (success) {
        // 更新当前模式
        this.currentMode = mode;

        // 更新UI显示
        const modeButtons = document.querySelectorAll('.mode-btn');
        modeButtons.forEach(btn => {
          if (btn.getAttribute('data-mode') === mode) {
            btn.classList.add('active');
          } else {
            btn.classList.remove('active');
          }
        });

        // 更新模式指示器
        uiManager.updateModeDisplay(mode);

        // 显示成功提示
        const modeNames = {
          silent: '静音',
          balanced: '均衡',
          performance: '性能'
        };
        uiManager.showNotification('success', `已切换到${modeNames[mode]}模式`);

        console.log('模式已切换:', mode);
      } else {
        throw new Error('发送模式切换指令失败');
      }
    } catch (error) {
      console.error('切换模式失败:', error);
      uiManager.showNotification('error', '切换模式失败: ' + error.message);
    }
  }

  /**
   * 设置PID参数控制
   */
  setupPIDControl() {
    // PID参数输入框实时更新显示值
    const pidInputs = ['pidKp', 'pidKi', 'pidKd'];
    pidInputs.forEach(id => {
      const input = document.getElementById(id);
      const valueDisplay = document.getElementById(id + 'Value');
      
      if (input && valueDisplay) {
        input.addEventListener('input', () => {
          valueDisplay.textContent = input.value;
        });
      }
    });

    // 应用PID参数按钮
    const applyPidBtn = document.getElementById('applyPidBtn');
    if (applyPidBtn) {
      applyPidBtn.addEventListener('click', () => {
        this.applyPIDParams();
      });
    }

    // 重置PID参数按钮
    const resetPidBtn = document.getElementById('resetPidBtn');
    if (resetPidBtn) {
      resetPidBtn.addEventListener('click', () => {
        this.resetPIDParams();
      });
    }
  }

  /**
   * 应用PID参数
   */
  applyPIDParams() {
    try {
      // 检查连接状态
      if (!wsClient.isConnected()) {
        uiManager.showNotification('warning', '未连接到服务器，无法应用参数');
        return;
      }

      // 获取PID参数值
      const kp = parseFloat(document.getElementById('pidKp').value);
      const ki = parseFloat(document.getElementById('pidKi').value);
      const kd = parseFloat(document.getElementById('pidKd').value);

      // 验证参数
      if (isNaN(kp) || isNaN(ki) || isNaN(kd)) {
        throw new Error('PID参数必须是有效的数字');
      }

      if (kp < 0 || kp > 10) {
        throw new Error('Kp参数必须在0-10范围内');
      }

      if (ki < 0 || ki > 5) {
        throw new Error('Ki参数必须在0-5范围内');
      }

      if (kd < 0 || kd > 2) {
        throw new Error('Kd参数必须在0-2范围内');
      }

      // 发送PID参数设置指令
      const success = wsClient.setPIDParams(kp, ki, kd);
      
      if (success) {
        uiManager.showNotification('success', 'PID参数已应用');
        console.log('PID参数已应用:', { kp, ki, kd });
      } else {
        throw new Error('发送PID参数设置指令失败');
      }
    } catch (error) {
      console.error('应用PID参数失败:', error);
      uiManager.showNotification('error', '应用PID参数失败: ' + error.message);
    }
  }

  /**
   * 重置PID参数为默认值
   */
  resetPIDParams() {
    try {
      // 默认PID参数
      const defaultParams = {
        kp: 1.0,
        ki: 0.5,
        kd: 0.1
      };

      // 更新输入框
      document.getElementById('pidKp').value = defaultParams.kp;
      document.getElementById('pidKi').value = defaultParams.ki;
      document.getElementById('pidKd').value = defaultParams.kd;

      // 更新显示值
      document.getElementById('pidKpValue').textContent = defaultParams.kp;
      document.getElementById('pidKiValue').textContent = defaultParams.ki;
      document.getElementById('pidKdValue').textContent = defaultParams.kd;

      uiManager.showNotification('info', 'PID参数已重置为默认值');
      console.log('PID参数已重置为默认值');
    } catch (error) {
      console.error('重置PID参数失败:', error);
      uiManager.showNotification('error', '重置PID参数失败: ' + error.message);
    }
  }

  /**
   * 设置系统控制
   */
  setupSystemControl() {
    // 系统重置按钮
    const resetSystemBtn = document.getElementById('resetSystemBtn');
    if (resetSystemBtn) {
      resetSystemBtn.addEventListener('click', () => {
        this.resetSystem();
      });
    }
  }

  /**
   * 重置系统
   */
  resetSystem() {
    try {
      // 确认操作
      if (!confirm('确定要重置系统吗？这将重启所有子系统。')) {
        return;
      }

      // 检查连接状态
      if (!wsClient.isConnected()) {
        uiManager.showNotification('warning', '未连接到服务器，无法重置系统');
        return;
      }

      // 发送重置指令
      const success = wsClient.sendControlCommand('reset');
      
      if (success) {
        uiManager.showNotification('success', '系统重置指令已发送');
        console.log('系统重置指令已发送');
      } else {
        throw new Error('发送系统重置指令失败');
      }
    } catch (error) {
      console.error('重置系统失败:', error);
      uiManager.showNotification('error', '重置系统失败: ' + error.message);
    }
  }

  /**
   * 设置历史数据查询
   */
  setupHistoryQuery() {
    // 查询按钮
    const queryBtn = document.getElementById('queryHistoryBtn');
    if (queryBtn) {
      queryBtn.addEventListener('click', () => {
        this.queryHistoryData();
      });
    }

    // 导出CSV按钮
    const exportBtn = document.getElementById('exportCsvBtn');
    if (exportBtn) {
      exportBtn.addEventListener('click', () => {
        this.exportHistoryData();
      });
    }

    // 设置默认时间范围（最近24小时）
    this.setDefaultTimeRange();
  }

  /**
   * 设置默认时间范围
   */
  setDefaultTimeRange() {
    const now = new Date();
    const yesterday = new Date(now.getTime() - 24 * 60 * 60 * 1000);

    const startInput = document.getElementById('historyStartTime');
    const endInput = document.getElementById('historyEndTime');

    if (startInput) {
      startInput.value = this.formatDateTimeLocal(yesterday);
    }

    if (endInput) {
      endInput.value = this.formatDateTimeLocal(now);
    }
  }

  /**
   * 格式化日期时间为datetime-local输入框格式
   * @param {Date} date - 日期对象
   * @returns {string} 格式化后的字符串
   */
  formatDateTimeLocal(date) {
    const year = date.getFullYear();
    const month = String(date.getMonth() + 1).padStart(2, '0');
    const day = String(date.getDate()).padStart(2, '0');
    const hours = String(date.getHours()).padStart(2, '0');
    const minutes = String(date.getMinutes()).padStart(2, '0');
    
    return `${year}-${month}-${day}T${hours}:${minutes}`;
  }

  /**
   * 查询历史数据
   */
  async queryHistoryData() {
    try {
      // 显示加载动画
      uiManager.showLoading('正在查询历史数据...');

      // 获取查询参数
      const startInput = document.getElementById('historyStartTime');
      const endInput = document.getElementById('historyEndTime');
      const typeSelect = document.getElementById('historyDataType');

      if (!startInput || !endInput || !typeSelect) {
        throw new Error('查询参数输入框不存在');
      }

      const startTime = new Date(startInput.value).getTime();
      const endTime = new Date(endInput.value).getTime();
      const dataType = typeSelect.value;

      // 验证时间范围
      if (isNaN(startTime) || isNaN(endTime)) {
        throw new Error('请选择有效的时间范围');
      }

      if (startTime >= endTime) {
        throw new Error('开始时间必须早于结束时间');
      }

      // 构建API请求URL
      const wsConfig = configManager.getWebSocketConfig();
      const apiUrl = `http://${wsConfig.host}:3000/api/history?start=${startTime}&end=${endTime}&type=${dataType}`;

      console.log('查询历史数据:', apiUrl);

      // 发送API请求
      const response = await fetch(apiUrl);
      
      if (!response.ok) {
        throw new Error(`HTTP错误: ${response.status}`);
      }

      const result = await response.json();

      if (!result.success) {
        throw new Error(result.message || '查询失败');
      }

      // 更新历史数据图表
      if (result.data && result.data.length > 0) {
        chartManager.updateHistoryChart(result.data, dataType);
        uiManager.showNotification('success', `查询成功，共 ${result.data.length} 条数据`);
      } else {
        uiManager.showNotification('info', '该时间范围内没有数据');
        chartManager.clearHistoryData();
      }

      // 隐藏加载动画
      uiManager.hideLoading();

      console.log('历史数据查询完成');
    } catch (error) {
      console.error('查询历史数据失败:', error);
      uiManager.hideLoading();
      uiManager.showNotification('error', '查询历史数据失败: ' + error.message);
    }
  }

  /**
   * 导出历史数据为CSV
   */
  exportHistoryData() {
    try {
      const historyData = chartManager.getHistoryData();
      
      if (!historyData || historyData.length === 0) {
        uiManager.showNotification('warning', '没有可导出的数据，请先查询历史数据');
        return;
      }

      // 调用图表管理器的导出功能
      chartManager.exportHistoryDataToCSV(historyData);
      
      console.log('历史数据导出完成');
    } catch (error) {
      console.error('导出历史数据失败:', error);
      uiManager.showNotification('error', '导出历史数据失败: ' + error.message);
    }
  }

  /**
   * 设置配置管理
   */
  setupConfigManagement() {
    // 加载当前配置到输入框
    this.loadConfigToUI();

    // 保存配置按钮
    const saveConfigBtn = document.getElementById('saveConfigBtn');
    if (saveConfigBtn) {
      saveConfigBtn.addEventListener('click', () => {
        this.saveConfiguration();
      });
    }

    // 测试连接按钮
    const testConnectionBtn = document.getElementById('testConnectionBtn');
    if (testConnectionBtn) {
      testConnectionBtn.addEventListener('click', () => {
        this.testConnection();
      });
    }

    // 恢复默认配置按钮
    const resetConfigBtn = document.getElementById('resetConfigBtn');
    if (resetConfigBtn) {
      resetConfigBtn.addEventListener('click', () => {
        this.resetConfiguration();
      });
    }

    // 刷新间隔输入框
    const refreshIntervalInput = document.getElementById('refreshInterval');
    if (refreshIntervalInput) {
      refreshIntervalInput.addEventListener('change', () => {
        const interval = parseInt(refreshIntervalInput.value);
        if (interval >= 500 && interval <= 10000) {
          configManager.set('ui.refreshInterval', interval);
          uiManager.showNotification('success', '刷新间隔已更新');
        }
      });
    }

    // 图表最大数据点输入框
    const chartMaxPointsInput = document.getElementById('chartMaxPoints');
    if (chartMaxPointsInput) {
      chartMaxPointsInput.addEventListener('change', () => {
        const maxPoints = parseInt(chartMaxPointsInput.value);
        if (maxPoints >= 10 && maxPoints <= 200) {
          configManager.set('ui.chartMaxPoints', maxPoints);
          chartManager.setMaxRealtimePoints(maxPoints);
          uiManager.showNotification('success', '图表最大数据点已更新');
        }
      });
    }
  }

  /**
   * 加载配置到UI
   */
  loadConfigToUI() {
    try {
      const wsConfig = configManager.getWebSocketConfig();
      
      // 加载WebSocket配置
      const wsHostInput = document.getElementById('wsHost');
      const wsPortInput = document.getElementById('wsPort');
      
      if (wsHostInput) {
        wsHostInput.value = wsConfig.host;
      }
      
      if (wsPortInput) {
        wsPortInput.value = wsConfig.port;
      }

      // 加载UI配置
      const refreshInterval = configManager.get('ui.refreshInterval', 2000);
      const chartMaxPoints = configManager.get('ui.chartMaxPoints', 60);
      
      const refreshIntervalInput = document.getElementById('refreshInterval');
      const chartMaxPointsInput = document.getElementById('chartMaxPoints');
      
      if (refreshIntervalInput) {
        refreshIntervalInput.value = refreshInterval;
      }
      
      if (chartMaxPointsInput) {
        chartMaxPointsInput.value = chartMaxPoints;
      }

      // 加载主题配置
      const theme = configManager.getTheme();
      const themeOptions = document.querySelectorAll('.theme-option');
      themeOptions.forEach(option => {
        if (option.getAttribute('data-theme') === theme) {
          option.classList.add('active');
        } else {
          option.classList.remove('active');
        }
      });

      console.log('配置已加载到UI');
    } catch (error) {
      console.error('加载配置到UI失败:', error);
    }
  }

  /**
   * 保存配置
   */
  saveConfiguration() {
    try {
      // 获取WebSocket配置
      const wsHostInput = document.getElementById('wsHost');
      const wsPortInput = document.getElementById('wsPort');

      if (!wsHostInput || !wsPortInput) {
        throw new Error('配置输入框不存在');
      }

      const host = wsHostInput.value.trim();
      const port = parseInt(wsPortInput.value);

      // 验证配置
      if (!host) {
        throw new Error('服务器地址不能为空');
      }

      if (isNaN(port) || port < 1 || port > 65535) {
        throw new Error('端口号必须在1-65535范围内');
      }

      // 保存配置
      const success = configManager.setWebSocketConfig(host, port);
      
      if (success) {
        uiManager.showNotification('success', '配置已保存');
        console.log('配置已保存:', { host, port });

        // 询问是否重新连接
        if (confirm('配置已保存，是否立即重新连接？')) {
          this.reconnectToServer();
        }
      } else {
        throw new Error('保存配置失败');
      }
    } catch (error) {
      console.error('保存配置失败:', error);
      uiManager.showNotification('error', '保存配置失败: ' + error.message);
    }
  }

  /**
   * 测试连接
   */
  async testConnection() {
    try {
      // 获取配置
      const wsHostInput = document.getElementById('wsHost');
      const wsPortInput = document.getElementById('wsPort');

      if (!wsHostInput || !wsPortInput) {
        throw new Error('配置输入框不存在');
      }

      const host = wsHostInput.value.trim();
      const port = parseInt(wsPortInput.value);

      // 验证配置
      if (!host) {
        throw new Error('服务器地址不能为空');
      }

      if (isNaN(port) || port < 1 || port > 65535) {
        throw new Error('端口号必须在1-65535范围内');
      }

      uiManager.showLoading('正在测试连接...');

      // 创建临时WebSocket连接进行测试
      const testWs = new WebSocket(`ws://${host}:${port}`);

      // 设置超时
      const timeout = setTimeout(() => {
        testWs.close();
        uiManager.hideLoading();
        uiManager.showNotification('error', '连接超时');
      }, 5000);

      testWs.onopen = () => {
        clearTimeout(timeout);
        testWs.close();
        uiManager.hideLoading();
        uiManager.showNotification('success', '连接测试成功');
      };

      testWs.onerror = () => {
        clearTimeout(timeout);
        uiManager.hideLoading();
        uiManager.showNotification('error', '连接测试失败，请检查服务器地址和端口');
      };

    } catch (error) {
      console.error('测试连接失败:', error);
      uiManager.hideLoading();
      uiManager.showNotification('error', '测试连接失败: ' + error.message);
    }
  }

  /**
   * 重置配置为默认值
   */
  resetConfiguration() {
    try {
      if (!confirm('确定要恢复默认配置吗？')) {
        return;
      }

      // 重置配置
      const success = configManager.resetConfig();
      
      if (success) {
        // 重新加载配置到UI
        this.loadConfigToUI();
        
        // 应用主题
        uiManager.applyTheme();
        
        uiManager.showNotification('success', '配置已恢复为默认值');
        console.log('配置已恢复为默认值');
      } else {
        throw new Error('重置配置失败');
      }
    } catch (error) {
      console.error('重置配置失败:', error);
      uiManager.showNotification('error', '重置配置失败: ' + error.message);
    }
  }

  /**
   * 连接到服务器
   */
  async connectToServer() {
    try {
      // 获取WebSocket配置
      const wsConfig = configManager.getWebSocketConfig();
      
      console.log('正在连接到服务器:', wsConfig);

      // 连接WebSocket服务器
      await wsClient.connect(wsConfig.host, wsConfig.port);

      console.log('已连接到服务器');
    } catch (error) {
      console.error('连接服务器失败:', error);
      uiManager.showNotification('error', '连接服务器失败: ' + error.message);
      throw error;
    }
  }

  /**
   * 重新连接到服务器
   */
  async reconnectToServer() {
    try {
      // 断开当前连接
      wsClient.disconnect();

      // 等待一小段时间
      await new Promise(resolve => setTimeout(resolve, 500));

      // 重新连接
      await this.connectToServer();

      uiManager.showNotification('success', '重新连接成功');
    } catch (error) {
      console.error('重新连接失败:', error);
      uiManager.showNotification('error', '重新连接失败: ' + error.message);
    }
  }

  /**
   * 断开服务器连接
   */
  disconnectFromServer() {
    try {
      wsClient.disconnect();
      console.log('已断开服务器连接');
    } catch (error) {
      console.error('断开连接失败:', error);
    }
  }

  /**
   * 获取应用状态
   * @returns {object} 应用状态对象
   */
  getStatus() {
    return {
      isInitialized: this.isInitialized,
      isConnected: wsClient.isConnected(),
      connectionStatus: wsClient.getConnectionStatus(),
      currentMode: this.currentMode,
      lastDataTimestamp: this.lastDataTimestamp
    };
  }

  /**
   * 销毁应用
   */
  destroy() {
    try {
      // 断开WebSocket连接
      this.disconnectFromServer();

      // 清除定时器
      if (this.dataUpdateCheckInterval) {
        clearInterval(this.dataUpdateCheckInterval);
        this.dataUpdateCheckInterval = null;
      }

      // 销毁图表
      chartManager.destroy();

      // 清除通知
      uiManager.clearNotifications();

      this.isInitialized = false;

      console.log('应用已销毁');
    } catch (error) {
      console.error('销毁应用失败:', error);
    }
  }
}

// 创建全局应用实例
const app = new Application();

// 页面加载完成后初始化应用
document.addEventListener('DOMContentLoaded', () => {
  console.log('页面加载完成，开始初始化应用...');
  
  // 初始化应用
  app.init().catch(error => {
    console.error('应用初始化失败:', error);
  });
});

// 页面卸载前清理资源
window.addEventListener('beforeunload', () => {
  console.log('页面即将卸载，清理资源...');
  app.destroy();
});

// 导出应用实例（用于模块化）
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    Application,
    app
  };
}
