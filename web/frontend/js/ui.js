/**
 * UI控制模块
 * 负责更新界面显示、处理用户交互、管理页面状态
 * 
 * 功能：
 * - 更新实时数据显示
 * - 更新连接状态显示
 * - 显示通知消息（成功/错误/警告/信息）
 * - 主题切换
 * - 加载动画显示/隐藏
 * - 更新健康状态显示
 * - 异常数据高亮显示
 */

/**
 * UI管理类
 */
class UIManager {
  constructor() {
    this.currentPage = 'dashboard'; // 当前页面
    this.notificationQueue = []; // 通知队列
    this.isShowingNotification = false; // 是否正在显示通知
    this.dataUpdateTimestamps = {}; // 数据更新时间戳（用于检测更新及时性）
    
    // 数据范围定义（用于判断异常）
    this.dataRanges = {
      cpuTemp: { min: 30, max: 80, warning: 75, danger: 85 },
      waterTemp: { min: 20, max: 45, warning: 38, danger: 45 },
      flowRate: { min: 80, max: 150, warning: 80, danger: 70 },
      pumpSpeed: { min: 1500, max: 3000, warning: 1000, danger: 800 },
      fanSpeed: { min: 800, max: 2500, warning: 500, danger: 300 },
      power: { min: 15, max: 40, warning: 35, danger: 45 },
      tdsPpm: { min: 0, max: 300, warning: 300, danger: 600 }
    };
  }

  /**
   * 初始化UI管理器
   */
  init() {
    this.setupPageNavigation();
    this.setupThemeToggle();
    this.setupModalHandlers();
    this.applyTheme();
    
    console.log('UI管理器初始化完成');
  }

  /**
   * 设置页面导航
   */
  setupPageNavigation() {
    const navButtons = document.querySelectorAll('.nav-btn');
    
    navButtons.forEach(btn => {
      btn.addEventListener('click', () => {
        const page = btn.getAttribute('data-page');
        this.switchPage(page);
      });
    });
  }

  /**
   * 切换页面
   * @param {string} pageName - 页面名称
   */
  switchPage(pageName) {
    // 隐藏所有页面
    const pages = document.querySelectorAll('.page');
    pages.forEach(page => {
      page.classList.remove('active');
    });

    // 显示目标页面
    const targetPage = document.getElementById(`${pageName}Page`);
    if (targetPage) {
      targetPage.classList.add('active');
      this.currentPage = pageName;
      
      // 切换到历史数据页面时,调整图表大小
      // 使用setTimeout确保页面已经完全显示
      if (pageName === 'history') {
        setTimeout(() => {
          if (typeof chartManager !== 'undefined' && chartManager.charts.history) {
            chartManager.charts.history.resize();
            console.log('历史图表已调整大小');
          }
        }, 100);
      }
    }

    // 更新导航按钮状态
    const navButtons = document.querySelectorAll('.nav-btn');
    navButtons.forEach(btn => {
      if (btn.getAttribute('data-page') === pageName) {
        btn.classList.add('active');
      } else {
        btn.classList.remove('active');
      }
    });

    console.log('切换到页面:', pageName);
  }

  /**
   * 设置主题切换
   */
  setupThemeToggle() {
    const themeToggle = document.getElementById('themeToggle');
    
    if (themeToggle) {
      themeToggle.addEventListener('click', () => {
        this.toggleTheme();
      });
    }

    // 配置页面的主题选择器
    const themeOptions = document.querySelectorAll('.theme-option');
    themeOptions.forEach(option => {
      option.addEventListener('click', () => {
        const theme = option.getAttribute('data-theme');
        this.setTheme(theme);
      });
    });
  }

  /**
   * 设置弹窗处理器
   */
  setupModalHandlers() {
    // 故障详情弹窗
    const closeFaultModal = document.getElementById('closeFaultModal');
    const closeFaultModalBtn = document.getElementById('closeFaultModalBtn');
    const faultDetailModal = document.getElementById('faultDetailModal');

    if (closeFaultModal) {
      closeFaultModal.addEventListener('click', () => {
        this.hideModal('faultDetailModal');
      });
    }

    if (closeFaultModalBtn) {
      closeFaultModalBtn.addEventListener('click', () => {
        this.hideModal('faultDetailModal');
      });
    }

    // 点击弹窗外部关闭
    if (faultDetailModal) {
      faultDetailModal.addEventListener('click', (e) => {
        if (e.target === faultDetailModal) {
          this.hideModal('faultDetailModal');
        }
      });
    }
  }

  /**
   * 更新实时数据显示
   * @param {object} data - 实时数据对象
   */
  updateRealtimeData(data) {
    if (!data) {
      console.warn('实时数据为空');
      return;
    }

    // 记录更新时间戳
    const updateTime = Date.now();
    this.dataUpdateTimestamps.realtime = updateTime;

    // 更新各个数据卡片
    const dataTypes = ['cpuTemp', 'waterTemp', 'flowRate', 'pumpSpeed', 'fanSpeed', 'power'];

    dataTypes.forEach(type => {
      if (data[type] !== undefined) {
        this.updateDataCard(type, data[type]);
      }
    });

    // 更新TDS水质数据（特殊处理）
    if (data.tdsPpm !== undefined) {
      this.updateTdsCard(data.tdsPpm, data.tdsLevel);
    }

    console.log('实时数据已更新');
  }

  /**
   * 更新单个数据卡片
   * @param {string} type - 数据类型
   * @param {number} value - 数据值
   */
  updateDataCard(type, value) {
    // 更新数值显示
    const valueElement = document.getElementById(`${type}Value`);
    if (valueElement) {
      valueElement.textContent = formatNumber(value, 1);
    }

    // 获取数据卡片
    const card = document.querySelector(`.data-card[data-type="${type}"]`);
    if (!card) {
      return;
    }

    // 判断数据状态
    const status = this.getDataStatus(type, value);

    // 更新状态显示
    const statusElement = card.querySelector('.data-status');
    if (statusElement) {
      // 移除所有状态类
      statusElement.classList.remove('normal', 'warning', 'danger');
      
      // 添加当前状态类
      statusElement.classList.add(status);
      
      // 更新状态文本
      const statusText = {
        normal: '正常',
        warning: '警告',
        danger: '危险'
      };
      statusElement.textContent = statusText[status] || '正常';
    }

    // 高亮显示异常数据
    if (status === 'warning' || status === 'danger') {
      this.highlightAbnormalData(card, status);
    } else {
      this.removeHighlight(card);
    }
  }

  /**
   * 更新TDS水质卡片
   * @param {number} tdsPpm - TDS值（ppm）
   * @param {string} tdsLevel - 水质等级
   */
  updateTdsCard(tdsPpm, tdsLevel) {
    // 更新TDS数值显示
    const tdsValueElement = document.getElementById('tdsValue');
    if (tdsValueElement) {
      tdsValueElement.textContent = tdsPpm || '--';
    }

    // 更新水质等级显示
    const tdsLevelElement = document.getElementById('tdsLevel');
    if (tdsLevelElement) {
      tdsLevelElement.textContent = tdsLevel || '未知';
    }

    // 获取TDS卡片
    const card = document.querySelector('.data-card[data-type="tds"]');
    if (!card) {
      return;
    }

    // 判断水质状态
    let status = 'normal';
    if (tdsPpm >= 600) {
      status = 'danger';  // 差水
    } else if (tdsPpm >= 300) {
      status = 'warning'; // 一般水/告警
    }

    // 更新状态显示
    const statusElement = document.getElementById('tdsStatus');
    if (statusElement) {
      // 移除所有状态类
      statusElement.classList.remove('normal', 'warning', 'danger');

      // 添加当前状态类
      statusElement.classList.add(status);

      // 更新状态文本
      const statusText = {
        normal: '正常',
        warning: '告警',
        danger: '危险'
      };
      statusElement.textContent = statusText[status] || '正常';
    }

    // 高亮显示异常数据
    if (status === 'warning' || status === 'danger') {
      this.highlightAbnormalData(card, status);
    } else {
      this.removeHighlight(card);
    }

    console.log(`TDS水质已更新: ${tdsPpm}ppm (${tdsLevel})`);
  }

  /**
   * 获取数据状态
   * @param {string} type - 数据类型
   * @param {number} value - 数据值
   * @returns {string} 状态：'normal' | 'warning' | 'danger'
   */
  getDataStatus(type, value) {
    const range = this.dataRanges[type];
    if (!range) {
      return 'normal';
    }

    // 对于流量和转速，低于阈值是异常
    if (type === 'flowRate' || type === 'pumpSpeed' || type === 'fanSpeed') {
      if (value <= range.danger) {
        return 'danger';
      }
      if (value <= range.warning) {
        return 'warning';
      }
    } else {
      // 对于温度和功耗，高于阈值是异常
      if (value >= range.danger) {
        return 'danger';
      }
      if (value >= range.warning) {
        return 'warning';
      }
    }

    return 'normal';
  }

  /**
   * 高亮显示异常数据
   * @param {HTMLElement} card - 数据卡片元素
   * @param {string} status - 状态：'warning' | 'danger'
   */
  highlightAbnormalData(card, status) {
    // 移除之前的高亮类
    card.classList.remove('highlight-warning', 'highlight-danger');
    
    // 添加对应的高亮类
    if (status === 'warning') {
      card.classList.add('highlight-warning');
    } else if (status === 'danger') {
      card.classList.add('highlight-danger');
    }

    // 添加动画效果
    card.classList.add('pulse-animation');
    
    // 3秒后移除动画
    setTimeout(() => {
      card.classList.remove('pulse-animation');
    }, 3000);
  }

  /**
   * 移除高亮显示
   * @param {HTMLElement} card - 数据卡片元素
   */
  removeHighlight(card) {
    card.classList.remove('highlight-warning', 'highlight-danger', 'pulse-animation');
  }

  /**
   * 更新连接状态显示
   * @param {string} status - 连接状态：'connected' | 'disconnected' | 'connecting'
   * @param {object} info - 额外信息（可选）
   */
  updateConnectionStatus(status, info = {}) {
    const statusElement = document.getElementById('connectionStatus');
    const statusInfoElement = document.getElementById('connectionStatusInfo');
    
    if (!statusElement) {
      return;
    }

    const statusDot = statusElement.querySelector('.status-dot');
    const statusText = statusElement.querySelector('.status-text');

    // 移除所有状态类
    statusElement.classList.remove('connected', 'disconnected', 'connecting');
    
    // 添加当前状态类
    statusElement.classList.add(status);

    // 更新状态文本
    const statusTexts = {
      connected: '已连接',
      disconnected: '未连接',
      connecting: '连接中...'
    };

    if (statusText) {
      statusText.textContent = statusTexts[status] || '未知';
    }

    // 更新配置页面的连接状态信息
    if (statusInfoElement) {
      statusInfoElement.textContent = statusTexts[status] || '未知';
    }

    console.log('连接状态已更新:', status);
  }

  /**
   * 显示通知消息
   * @param {string} type - 消息类型：'success' | 'error' | 'warning' | 'info'
   * @param {string} message - 消息内容
   * @param {number} duration - 显示时长（毫秒），默认3000
   */
  showNotification(type, message, duration = 3000) {
    // 添加到通知队列
    this.notificationQueue.push({ type, message, duration });

    // 如果当前没有显示通知，立即显示
    if (!this.isShowingNotification) {
      this.showNextNotification();
    }
  }

  /**
   * 显示下一个通知
   */
  showNextNotification() {
    // 检查队列是否为空
    if (this.notificationQueue.length === 0) {
      this.isShowingNotification = false;
      return;
    }

    this.isShowingNotification = true;

    // 从队列中取出第一个通知
    const { type, message, duration } = this.notificationQueue.shift();

    // 创建通知元素
    const notification = document.createElement('div');
    notification.className = `notification notification-${type}`;
    
    // 设置图标
    const icons = {
      success: '✅',
      error: '❌',
      warning: '⚠️',
      info: 'ℹ️'
    };
    
    notification.innerHTML = `
      <span class="notification-icon">${icons[type] || 'ℹ️'}</span>
      <span class="notification-message">${message}</span>
      <button class="notification-close">&times;</button>
    `;

    // 获取通知容器
    const container = document.getElementById('notificationContainer');
    if (!container) {
      console.error('通知容器不存在');
      this.isShowingNotification = false;
      return;
    }

    // 添加到容器
    container.appendChild(notification);

    // 添加关闭按钮事件
    const closeBtn = notification.querySelector('.notification-close');
    closeBtn.addEventListener('click', () => {
      this.hideNotification(notification);
    });

    // 触发显示动画
    setTimeout(() => {
      notification.classList.add('show');
    }, 10);

    // 自动隐藏
    setTimeout(() => {
      this.hideNotification(notification);
    }, duration);

    console.log(`显示${type}通知:`, message);
  }

  /**
   * 隐藏通知
   * @param {HTMLElement} notification - 通知元素
   */
  hideNotification(notification) {
    notification.classList.remove('show');
    
    // 等待动画完成后移除元素
    setTimeout(() => {
      if (notification.parentNode) {
        notification.parentNode.removeChild(notification);
      }
      
      // 显示下一个通知
      this.showNextNotification();
    }, 300);
  }

  /**
   * 切换主题
   */
  toggleTheme() {
    if (!configManager) {
      console.error('配置管理器未初始化');
      return;
    }

    const newTheme = configManager.toggleTheme();
    this.applyTheme(newTheme);
    
    this.showNotification('success', `已切换到${newTheme === 'dark' ? '深色' : '浅色'}模式`);
  }

  /**
   * 设置主题
   * @param {string} theme - 主题名称：'light' | 'dark'
   */
  setTheme(theme) {
    if (!configManager) {
      console.error('配置管理器未初始化');
      return;
    }

    configManager.setTheme(theme);
    this.applyTheme(theme);

    // 更新主题选择器状态
    const themeOptions = document.querySelectorAll('.theme-option');
    themeOptions.forEach(option => {
      if (option.getAttribute('data-theme') === theme) {
        option.classList.add('active');
      } else {
        option.classList.remove('active');
      }
    });
  }

  /**
   * 应用主题
   * @param {string} theme - 主题名称（可选，默认从配置读取）
   */
  applyTheme(theme = null) {
    const actualTheme = theme || (configManager ? configManager.getTheme() : 'light');
    
    // 设置body的data-theme属性
    document.body.setAttribute('data-theme', actualTheme);

    // 更新主题切换按钮图标
    const themeToggle = document.getElementById('themeToggle');
    if (themeToggle) {
      const icon = themeToggle.querySelector('.icon');
      if (icon) {
        icon.textContent = actualTheme === 'dark' ? '☀️' : '🌙';
      }
    }

    console.log('应用主题:', actualTheme);
  }

  /**
   * 显示加载动画
   * @param {string} text - 加载文本（可选）
   */
  showLoading(text = '加载中...') {
    const overlay = document.getElementById('loadingOverlay');
    if (!overlay) {
      return;
    }

    const loadingText = overlay.querySelector('.loading-text');
    if (loadingText) {
      loadingText.textContent = text;
    }

    overlay.classList.add('show');
  }

  /**
   * 隐藏加载动画
   */
  hideLoading() {
    const overlay = document.getElementById('loadingOverlay');
    if (!overlay) {
      return;
    }

    overlay.classList.remove('show');
  }

  /**
   * 更新健康状态显示
   * @param {object} healthData - 健康状态数据
   */
  updateHealthStatus(healthData) {
    if (!healthData) {
      console.warn('健康状态数据为空');
      return;
    }

    // 更新健康度评分
    this.updateHealthScore(healthData.healthScore);

    // 更新子系统状态
    if (healthData.status) {
      this.updateSubsystemStatus(healthData.status);
    }

    // 更新故障列表
    if (healthData.faults) {
      this.updateFaultList(healthData.faults);
    }

    // 如果健康度低于60分，显示警告
    if (healthData.healthScore < 60) {
      this.showNotification('warning', `系统健康度较低（${healthData.healthScore}分），请检查系统状态`);
    }

    console.log('健康状态已更新');
  }

  /**
   * 更新健康度评分显示
   * @param {number} score - 健康度评分（0-100）
   */
  updateHealthScore(score) {
    // 更新评分数值
    const scoreValue = document.getElementById('healthScoreValue');
    if (scoreValue) {
      scoreValue.textContent = score !== undefined ? Math.round(score) : '--';
    }

    // 更新进度圆环
    const scoreProgress = document.getElementById('scoreProgress');
    if (scoreProgress) {
      const circumference = 2 * Math.PI * 90; // 半径为90
      const offset = circumference - (score / 100) * circumference;
      scoreProgress.style.strokeDasharray = `${circumference} ${circumference}`;
      scoreProgress.style.strokeDashoffset = offset;

      // 根据评分设置颜色
      if (score >= 80) {
        scoreProgress.style.stroke = '#4caf50'; // 绿色
      } else if (score >= 60) {
        scoreProgress.style.stroke = '#ff9800'; // 橙色
      } else {
        scoreProgress.style.stroke = '#f44336'; // 红色
      }
    }

    // 更新状态文本
    const statusText = document.querySelector('#healthScoreStatus .status-text');
    if (statusText) {
      if (score >= 80) {
        statusText.textContent = '系统运行正常';
      } else if (score >= 60) {
        statusText.textContent = '系统运行良好，有轻微问题';
      } else {
        statusText.textContent = '系统存在问题，需要关注';
      }
    }

    // 更新状态图标
    const statusIcon = document.querySelector('#healthScoreStatus .status-icon');
    if (statusIcon) {
      if (score >= 80) {
        statusIcon.textContent = '✅';
      } else if (score >= 60) {
        statusIcon.textContent = '⚠️';
      } else {
        statusIcon.textContent = '❌';
      }
    }
  }

  /**
   * 更新子系统状态显示
   * @param {object} status - 子系统状态对象
   */
  updateSubsystemStatus(status) {
    const subsystems = ['pump', 'fan', 'sensor', 'cooling'];
    
    subsystems.forEach(subsystem => {
      const statusItem = document.querySelector(`.status-item[data-subsystem="${subsystem}"]`);
      if (!statusItem) {
        return;
      }

      const badge = statusItem.querySelector('.status-badge');
      if (!badge) {
        return;
      }

      const subsystemStatus = status[subsystem] || 'normal';

      // 移除所有状态类
      badge.classList.remove('normal', 'warning', 'fault');
      
      // 添加当前状态类
      badge.classList.add(subsystemStatus);

      // 更新状态文本
      const statusTexts = {
        normal: '正常',
        warning: '警告',
        fault: '故障'
      };
      badge.textContent = statusTexts[subsystemStatus] || '正常';
    });
  }

  /**
   * 更新故障列表显示
   * @param {Array} faults - 故障数组
   */
  updateFaultList(faults) {
    const faultList = document.getElementById('faultList');
    if (!faultList) {
      return;
    }

    // 清空列表
    faultList.innerHTML = '';

    // 如果没有故障，显示无故障提示
    if (!faults || faults.length === 0) {
      faultList.innerHTML = `
        <div class="no-faults">
          <span class="icon">✅</span>
          <span>暂无故障记录</span>
        </div>
      `;
      return;
    }

    // 显示故障列表
    faults.forEach(fault => {
      const faultItem = document.createElement('div');
      faultItem.className = `fault-item ${fault.severity || 'low'}`;
      
      faultItem.innerHTML = `
        <span class="fault-icon">⚠️</span>
        <div class="fault-content">
          <div class="fault-code">${fault.code}</div>
          <div class="fault-description">${fault.description}</div>
          <div class="fault-time">${formatTimestamp(fault.timestamp)}</div>
        </div>
        <span class="severity-badge ${fault.severity || 'low'}">${this.getSeverityText(fault.severity)}</span>
      `;

      // 点击显示详情
      faultItem.addEventListener('click', () => {
        this.showFaultDetail(fault);
      });

      faultList.appendChild(faultItem);
    });
  }

  /**
   * 获取严重程度文本
   * @param {string} severity - 严重程度：'low' | 'medium' | 'high'
   * @returns {string} 严重程度文本
   */
  getSeverityText(severity) {
    const texts = {
      low: '低',
      medium: '中',
      high: '高'
    };
    return texts[severity] || '未知';
  }

  /**
   * 显示故障详情
   * @param {object} fault - 故障对象
   */
  showFaultDetail(fault) {
    const modal = document.getElementById('faultDetailModal');
    const body = document.getElementById('faultDetailBody');
    
    if (!modal || !body) {
      return;
    }

    // 构建详情内容
    body.innerHTML = `
      <div class="fault-detail">
        <div class="detail-row">
          <span class="detail-label">故障代码：</span>
          <span class="detail-value">${fault.code}</span>
        </div>
        <div class="detail-row">
          <span class="detail-label">发生时间：</span>
          <span class="detail-value">${formatTimestamp(fault.timestamp)}</span>
        </div>
        <div class="detail-row">
          <span class="detail-label">严重程度：</span>
          <span class="detail-value">
            <span class="severity-badge ${fault.severity || 'low'}">${this.getSeverityText(fault.severity)}</span>
          </span>
        </div>
        <div class="detail-row">
          <span class="detail-label">故障描述：</span>
          <span class="detail-value">${fault.description}</span>
        </div>
        ${fault.solution ? `
          <div class="detail-row">
            <span class="detail-label">建议处理：</span>
            <span class="detail-value">${fault.solution}</span>
          </div>
        ` : ''}
      </div>
    `;

    // 显示弹窗
    this.showModal('faultDetailModal');
  }

  /**
   * 显示弹窗
   * @param {string} modalId - 弹窗ID
   */
  showModal(modalId) {
    const modal = document.getElementById(modalId);
    if (modal) {
      modal.classList.add('show');
    }
  }

  /**
   * 隐藏弹窗
   * @param {string} modalId - 弹窗ID
   */
  hideModal(modalId) {
    const modal = document.getElementById(modalId);
    if (modal) {
      modal.classList.remove('show');
    }
  }

  /**
   * 更新运行模式显示
   * @param {string} mode - 运行模式：'silent' | 'balanced' | 'performance'
   */
  updateModeDisplay(mode) {
    const modeIndicator = document.getElementById('modeIndicator');
    if (!modeIndicator) {
      return;
    }

    const modeValue = modeIndicator.querySelector('.mode-value');
    if (!modeValue) {
      return;
    }

    const modeTexts = {
      silent: '静音',
      balanced: '均衡',
      performance: '性能'
    };

    modeValue.textContent = modeTexts[mode] || '未知';
  }

  /**
   * 获取当前页面
   * @returns {string} 当前页面名称
   */
  getCurrentPage() {
    return this.currentPage;
  }

  /**
   * 清除所有通知
   */
  clearNotifications() {
    this.notificationQueue = [];
    const container = document.getElementById('notificationContainer');
    if (container) {
      container.innerHTML = '';
    }
    this.isShowingNotification = false;
  }
}

// 创建全局UI管理器实例
const uiManager = new UIManager();

// 暴露到全局作用域（浏览器环境）
if (typeof window !== 'undefined') {
  window.uiManager = uiManager;
  window.UIManager = UIManager;
}

// 导出UI管理器类和实例（用于模块化）
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    UIManager,
    uiManager
  };
}
