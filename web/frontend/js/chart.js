/**
 * 图表管理模块
 * 负责初始化和管理ECharts图表、更新图表数据、处理图表交互
 * 
 * 功能：
 * - 初始化ECharts图表
 * - 更新实时数据图表（折线图，最近60个数据点）
 * - 更新历史数据图表（支持时间范围选择）
 * - 图表数据类型切换
 * - 图表交互（悬停显示详细信息）
 * - 数据采样逻辑（超过1000个数据点）
 * - 图表导出功能
 */

// 导入工具函数
// 在浏览器环境中直接使用 window.utils
// 在Node.js环境中使用 require
if (typeof require !== 'undefined' && typeof window === 'undefined') {
  // Node.js环境
  const utils = require('./utils.js');
  var formatTimestamp = utils.formatTimestamp;
  var formatNumber = utils.formatNumber;
  var sampleData = utils.sampleData;
  var exportToCSV = utils.exportToCSV;
}
// 浏览器环境中,直接使用 window.utils.formatTimestamp() 等函数

/**
 * 图表管理类
 */
class ChartManager {
  constructor() {
    this.charts = {}; // 存储图表实例
    this.realtimeData = {}; // 实时数据缓存
    this.historyData = []; // 历史数据缓存
    this.currentRealtimeType = 'cpuTemp'; // 当前实时图表显示的数据类型
    this.currentHistoryType = 'all'; // 当前历史图表显示的数据类型
    this.maxRealtimePoints = 60; // 实时图表最大数据点数
    this.maxHistoryPoints = 1000; // 历史图表最大数据点数（超过则采样）
    this.historyQueryTimer = null; // 历史数据查询防抖定时器
    
    // 数据类型配置
    this.dataTypeConfig = {
      cpuTemp: { name: 'CPU温度', unit: '°C', color: '#ff6b6b' },
      waterTemp: { name: '水温', unit: '°C', color: '#4ecdc4' },
      flowRate: { name: '流量', unit: 'L/h', color: '#45b7d1' },
      pumpSpeed: { name: '泵转速', unit: 'RPM', color: '#96ceb4' },
      fanSpeed: { name: '风扇转速', unit: 'RPM', color: '#ffeaa7' },
      power: { name: '功耗', unit: 'W', color: '#dfe6e9' }
    };
    
    // 初始化实时数据缓存结构
    Object.keys(this.dataTypeConfig).forEach(type => {
      this.realtimeData[type] = {
        timestamps: [],
        values: []
      };
    });
  }

  /**
   * 初始化所有图表
   */
  init() {
    // 检查ECharts是否已加载
    if (typeof echarts === 'undefined') {
      console.error('ECharts库未加载');
      return;
    }

    // 初始化实时数据图表
    this.initRealtimeChart();

    // 初始化历史数据图表
    this.initHistoryChart();

    // 设置图表类型切换事件
    this.setupChartTypeSwitch();

    // 监听窗口大小变化，自动调整图表大小
    window.addEventListener('resize', () => {
      this.resizeAllCharts();
    });

    console.log('图表管理器初始化完成');
  }

  /**
   * 初始化实时数据图表
   */
  initRealtimeChart() {
    const chartElement = document.getElementById('realtimeChart');
    if (!chartElement) {
      console.error('实时图表容器不存在');
      return;
    }

    // 创建ECharts实例
    const chart = echarts.init(chartElement);
    this.charts.realtime = chart;

    // 获取当前数据类型配置
    const config = this.dataTypeConfig[this.currentRealtimeType];

    // 设置图表选项
    const option = {
      title: {
        text: `${config.name}实时趋势`,
        left: 'center',
        textStyle: {
          fontSize: 16,
          fontWeight: 'normal'
        }
      },
      tooltip: {
        trigger: 'axis',
        formatter: (params) => {
          if (!params || params.length === 0) {
            return '';
          }
          const param = params[0];
          // axisValue现在已经是格式化后的时间字符串了,直接使用
          const time = param.axisValue;
          const value = window.utils.formatNumber(param.value, 2);
          return `${time}<br/>${config.name}: ${value} ${config.unit}`;
        }
      },
      grid: {
        left: '3%',
        right: '4%',
        bottom: '3%',
        containLabel: true
      },
      xAxis: {
        type: 'category',
        boundaryGap: false,
        data: []
        // 不需要formatter,因为data中已经是格式化后的字符串
      },
      yAxis: {
        type: 'value',
        name: config.unit,
        axisLabel: {
          formatter: (value) => {
            return window.utils.formatNumber(value, 1);
          }
        }
      },
      series: [
        {
          name: config.name,
          type: 'line',
          smooth: true,
          symbol: 'circle',
          symbolSize: 6,
          lineStyle: {
            width: 2,
            color: config.color
          },
          itemStyle: {
            color: config.color
          },
          areaStyle: {
            color: {
              type: 'linear',
              x: 0,
              y: 0,
              x2: 0,
              y2: 1,
              colorStops: [
                { offset: 0, color: config.color + '80' },
                { offset: 1, color: config.color + '10' }
              ]
            }
          },
          data: []
        }
      ],
      animation: true,
      animationDuration: 300
    };

    chart.setOption(option);
    console.log('实时数据图表初始化完成');
  }

  /**
   * 初始化历史数据图表
   */
  initHistoryChart() {
    const chartElement = document.getElementById('historyChart');
    if (!chartElement) {
      console.error('历史图表容器不存在');
      return;
    }

    // 创建ECharts实例
    const chart = echarts.init(chartElement);
    this.charts.history = chart;

    // 设置图表选项
    const option = {
      title: {
        text: '历史数据趋势',
        left: 'center',
        textStyle: {
          fontSize: 16,
          fontWeight: 'normal'
        }
      },
      tooltip: {
        trigger: 'axis',
        axisPointer: {
          type: 'cross'
        }
      },
      legend: {
        data: [],
        top: 30,
        left: 'center'
      },
      grid: {
        left: '3%',
        right: '4%',
        bottom: '3%',
        top: 80,
        containLabel: true
      },
      xAxis: {
        type: 'category',
        boundaryGap: false,
        data: []
        // 不需要formatter,因为data中已经是格式化后的字符串
      },
      yAxis: {
        type: 'value',
        axisLabel: {
          formatter: (value) => {
            return window.utils.formatNumber(value, 1);
          }
        }
      },
      dataZoom: [
        {
          type: 'inside',
          start: 0,
          end: 100
        },
        {
          type: 'slider',
          start: 0,
          end: 100,
          height: 20,
          bottom: 10
        }
      ],
      series: [],
      animation: true
    };

    chart.setOption(option);
    console.log('历史数据图表初始化完成');
  }

  /**
   * 设置图表类型切换事件
   */
  setupChartTypeSwitch() {
    // 实时图表类型切换
    const realtimeSelect = document.getElementById('realtimeChartType');
    if (realtimeSelect) {
      realtimeSelect.addEventListener('change', (e) => {
        this.switchRealtimeChartType(e.target.value);
      });
    }

    // 历史图表类型切换
    const historySelect = document.getElementById('historyDataType');
    if (historySelect) {
      historySelect.addEventListener('change', (e) => {
        const newType = e.target.value;
        console.log('切换历史数据类型:', this.currentHistoryType, '->', newType);
        this.currentHistoryType = newType;
        
        // 如果已有历史数据,自动重新查询(使用防抖)
        if (this.historyData.length > 0) {
          console.log('检测到已有历史数据,准备自动重新查询...');
          
          // 清除之前的定时器
          if (this.historyQueryTimer) {
            clearTimeout(this.historyQueryTimer);
          }
          
          // 设置新的定时器(300ms防抖)
          this.historyQueryTimer = setTimeout(() => {
            console.log('执行自动查询...');
            // 检查app对象是否存在
            if (typeof app !== 'undefined' && typeof app.queryHistoryData === 'function') {
              // queryHistoryData是async函数,需要处理Promise
              app.queryHistoryData().catch(error => {
                console.error('自动查询失败:', error);
              });
            } else {
              console.warn('app对象或queryHistoryData方法不存在');
              // 降级方案:提示用户手动查询
              if (typeof uiManager !== 'undefined') {
                uiManager.showNotification('info', '请点击"查询"按钮重新获取数据');
              }
            }
          }, 300);
        }
      });
    }
  }

  /**
   * 切换实时图表数据类型
   * @param {string} type - 数据类型
   */
  switchRealtimeChartType(type) {
    if (!this.dataTypeConfig[type]) {
      console.error('无效的数据类型:', type);
      return;
    }

    this.currentRealtimeType = type;
    const config = this.dataTypeConfig[type];
    const chart = this.charts.realtime;

    if (!chart) {
      console.error('实时图表未初始化');
      return;
    }

    // 获取该类型的数据
    const data = this.realtimeData[type];

    // 将timestamps格式化为字符串数组
    const formattedTimestamps = data.timestamps.map(ts => {
      return window.utils.formatTimestamp(ts, 'HH:mm:ss');
    });

    // 更新图表配置
    chart.setOption({
      title: {
        text: `${config.name}实时趋势`
      },
      yAxis: {
        name: config.unit
      },
      series: [
        {
          name: config.name,
          lineStyle: {
            color: config.color
          },
          itemStyle: {
            color: config.color
          },
          areaStyle: {
            color: {
              type: 'linear',
              x: 0,
              y: 0,
              x2: 0,
              y2: 1,
              colorStops: [
                { offset: 0, color: config.color + '80' },
                { offset: 1, color: config.color + '10' }
              ]
            }
          },
          data: data.values
        }
      ],
      xAxis: {
        data: formattedTimestamps  // 使用格式化后的字符串
      }
    });

    console.log('切换实时图表类型:', type);
  }

  /**
   * 更新实时数据图表
   * @param {object} data - 实时数据对象
   */
  updateRealtimeChart(data) {
    if (!data || !data.timestamp) {
      console.warn('实时数据无效');
      return;
    }

    const timestamp = data.timestamp;

    // 更新所有数据类型的缓存
    Object.keys(this.dataTypeConfig).forEach(type => {
      if (data[type] !== undefined) {
        const cache = this.realtimeData[type];
        
        // 添加新数据
        cache.timestamps.push(timestamp);
        cache.values.push(data[type]);

        // 保持最大数据点数限制
        if (cache.timestamps.length > this.maxRealtimePoints) {
          cache.timestamps.shift();
          cache.values.shift();
        }
      }
    });

    // 更新当前显示的图表
    const currentData = this.realtimeData[this.currentRealtimeType];
    const chart = this.charts.realtime;

    if (!chart) {
      console.error('实时图表未初始化');
      return;
    }

    // 将timestamps格式化为字符串数组,避免formatter问题
    const formattedTimestamps = currentData.timestamps.map(ts => {
      return window.utils.formatTimestamp(ts, 'HH:mm:ss');
    });

    console.log('[Chart] 更新实时图表, timestamps数量:', currentData.timestamps.length, 
                '第一个timestamp:', currentData.timestamps[0], 
                '格式化后:', formattedTimestamps[0]);

    // 更新图表数据
    chart.setOption({
      xAxis: {
        data: formattedTimestamps  // 直接使用格式化后的字符串
      },
      series: [
        {
          data: currentData.values
        }
      ]
    });
  }

  /**
   * 更新历史数据图表
   * @param {Array} historyData - 历史数据数组
   * @param {string} dataType - 数据类型（'all' 或具体类型）
   */
  updateHistoryChart(historyData, dataType = 'all') {
    if (!historyData || historyData.length === 0) {
      console.warn('历史数据为空');
      return;
    }

    // 缓存历史数据
    this.historyData = historyData;
    this.currentHistoryDataType = dataType; // 保存当前数据类型

    const chart = this.charts.history;
    if (!chart) {
      console.error('历史图表未初始化');
      return;
    }

    // 数据采样（如果数据点超过最大限制）
    let sampledData = historyData;
    if (historyData.length > this.maxHistoryPoints) {
      sampledData = window.utils.sampleData(historyData, this.maxHistoryPoints);
      console.log(`历史数据采样: ${historyData.length} -> ${sampledData.length}`);
    }

    // 提取时间戳并格式化为字符串
    const timestamps = sampledData.map(item => item.timestamp);
    const formattedTimestamps = timestamps.map(ts => {
      return window.utils.formatTimestamp(ts, 'MM-DD HH:mm');
    });
    
    // 保存到实例中,供tooltip使用
    this.currentHistoryTimestamps = timestamps;
    
    console.log('[History Chart] 数据点数:', sampledData.length, 
                '第一个timestamp:', timestamps[0], 
                '格式化后:', formattedTimestamps[0]);

    // 确定要显示的数据类型
    let typesToShow = [];
    if (dataType === 'all') {
      typesToShow = Object.keys(this.dataTypeConfig);
    } else if (this.dataTypeConfig[dataType]) {
      typesToShow = [dataType];
    } else {
      console.error('无效的数据类型:', dataType);
      return;
    }
    
    // 保存到实例中,供tooltip使用
    this.currentHistoryTypesToShow = typesToShow;

    // 将数据类型分为两组:温度类(左Y轴)和其他类(右Y轴)
    const tempTypes = ['cpuTemp', 'waterTemp']; // 温度类,范围0-100°C
    const otherTypes = ['flowRate', 'pumpSpeed', 'fanSpeed', 'power']; // 其他类,范围更大

    // 构建系列数据
    const series = typesToShow.map(type => {
      const config = this.dataTypeConfig[type];
      // 兼容两种数据格式: {timestamp, cpuTemp, ...} 或 {timestamp, value}
      const values = sampledData.map(item => {
        // 如果是特定类型查询，后端返回的数据格式为 {timestamp, value}
        if (item.value !== undefined) {
          return item.value;
        }
        // 如果是全量查询，数据格式为 {timestamp, cpuTemp, waterTemp, ...}
        return item[type] !== undefined ? item[type] : null;
      });

      // 判断使用哪个Y轴: 温度类用左轴(yAxisIndex: 0),其他用右轴(yAxisIndex: 1)
      const yAxisIndex = tempTypes.includes(type) ? 0 : 1;

      return {
        name: config.name,
        type: 'line',
        smooth: true,
        symbol: 'circle',
        symbolSize: 4,
        lineStyle: {
          width: 2,
          color: config.color
        },
        itemStyle: {
          color: config.color
        },
        yAxisIndex: yAxisIndex, // 指定Y轴
        data: values
      };
    });

    // 构建图例数据
    const legendData = typesToShow.map(type => this.dataTypeConfig[type].name);

    // 判断是否需要双Y轴
    const needDualAxis = dataType === 'all' || 
                         (typesToShow.some(t => tempTypes.includes(t)) && 
                          typesToShow.some(t => otherTypes.includes(t)));

    // 构建Y轴配置
    const yAxisConfig = needDualAxis ? [
      {
        type: 'value',
        name: '温度 (°C)',
        position: 'left',
        axisLabel: {
          formatter: (value) => window.utils.formatNumber(value, 1)
        },
        splitLine: {
          lineStyle: {
            type: 'dashed'
          }
        }
      },
      {
        type: 'value',
        name: '转速/流量/功耗',
        position: 'right',
        axisLabel: {
          formatter: (value) => window.utils.formatNumber(value, 0)
        },
        splitLine: {
          show: false
        }
      }
    ] : [
      {
        type: 'value',
        axisLabel: {
          formatter: (value) => window.utils.formatNumber(value, 1)
        }
      }
    ];

    // 更新图表
    chart.setOption({
      legend: {
        data: legendData
      },
      xAxis: {
        data: formattedTimestamps  // 使用格式化后的字符串
      },
      yAxis: yAxisConfig,
      series: series,
      tooltip: {
        trigger: 'axis',
        axisPointer: {
          type: 'cross'
        },
        formatter: (params) => {
          if (!params || params.length === 0) {
            return '';
          }
          
          try {
            // axisValue现在是格式化后的字符串(MM-DD HH:mm),需要找到对应的原始timestamp
            const axisIndex = params[0].dataIndex;
            const originalTimestamp = this.currentHistoryTimestamps[axisIndex];
            const time = window.utils.formatTimestamp(originalTimestamp, 'YYYY-MM-DD HH:mm:ss');
            let content = `${time}<br/>`;
            
            params.forEach(param => {
              if (param.value !== null && param.value !== undefined) {
                const type = this.currentHistoryTypesToShow[param.seriesIndex];
                const config = this.dataTypeConfig[type];
                if (config) {
                  content += `${param.marker}${param.seriesName}: ${window.utils.formatNumber(param.value, 2)} ${config.unit}<br/>`;
                }
              }
            });
            
            return content;
          } catch (error) {
            console.error('Tooltip formatter错误:', error);
            return '数据加载中...';
          }
        }
      },
      grid: {
        left: '3%',
        right: needDualAxis ? '8%' : '4%', // 双Y轴时右侧留更多空间
        bottom: '3%',
        top: 80,
        containLabel: true
      }
    });

    // 更新数据点计数显示
    const countElement = document.getElementById('dataPointCount');
    if (countElement) {
      countElement.textContent = historyData.length;
    }

    // 强制调整图表大小,确保正确显示
    // 使用setTimeout确保DOM已经更新
    setTimeout(() => {
      if (chart) {
        chart.resize();
      }
    }, 100);

    console.log('历史数据图表已更新，数据点数:', historyData.length);
  }

  /**
   * 导出图表为图片
   * @param {string} chartId - 图表ID（'realtime' 或 'history'）
   * @param {string} filename - 文件名（可选）
   */
  exportChart(chartId, filename = null) {
    const chart = this.charts[chartId];
    if (!chart) {
      console.error('图表不存在:', chartId);
      return;
    }

    try {
      // 获取图表的base64图片数据
      const imageData = chart.getDataURL({
        type: 'png',
        pixelRatio: 2,
        backgroundColor: '#fff'
      });

      // 创建下载链接
      const link = document.createElement('a');
      link.href = imageData;
      
      // 生成文件名
      if (!filename) {
        const timestamp = window.utils.formatTimestamp(Date.now(), 'YYYYMMDD_HHmmss');
        filename = `chart_${chartId}_${timestamp}.png`;
      }
      
      link.download = filename;
      
      // 触发下载
      document.body.appendChild(link);
      link.click();
      document.body.removeChild(link);

      console.log('图表已导出:', filename);
      
      // 显示成功提示
      if (typeof uiManager !== 'undefined') {
        uiManager.showNotification('success', '图表导出成功');
      }
    } catch (error) {
      console.error('图表导出失败:', error);
      
      // 显示错误提示
      if (typeof uiManager !== 'undefined') {
        uiManager.showNotification('error', '图表导出失败');
      }
    }
  }

  /**
   * 导出历史数据为CSV
   * @param {Array} data - 历史数据数组（可选，默认使用缓存的数据）
   */
  exportHistoryDataToCSV(data = null) {
    const exportData = data || this.historyData;
    
    if (!exportData || exportData.length === 0) {
      console.warn('没有可导出的数据');
      if (typeof uiManager !== 'undefined') {
        uiManager.showNotification('warning', '没有可导出的数据');
      }
      return;
    }

    try {
      // 定义CSV表头
      const headers = [
        'timestamp',
        'time',
        'cpuTemp',
        'waterTemp',
        'flowRate',
        'pumpSpeed',
        'fanSpeed',
        'power'
      ];

      // 定义中文表头
      const chineseHeaders = [
        '时间戳',
        '时间',
        'CPU温度(°C)',
        '水温(°C)',
        '流量(L/h)',
        '泵转速(RPM)',
        '风扇转速(RPM)',
        '功耗(W)'
      ];

      // 转换数据格式
      const csvData = exportData.map(item => {
        return {
          timestamp: item.timestamp,
          time: window.utils.formatTimestamp(item.timestamp, 'YYYY-MM-DD HH:mm:ss'),
          cpuTemp: item.cpuTemp !== undefined ? window.utils.formatNumber(item.cpuTemp, 2) : '',
          waterTemp: item.waterTemp !== undefined ? window.utils.formatNumber(item.waterTemp, 2) : '',
          flowRate: item.flowRate !== undefined ? window.utils.formatNumber(item.flowRate, 2) : '',
          pumpSpeed: item.pumpSpeed !== undefined ? window.utils.formatNumber(item.pumpSpeed, 0) : '',
          fanSpeed: item.fanSpeed !== undefined ? window.utils.formatNumber(item.fanSpeed, 0) : '',
          power: item.power !== undefined ? window.utils.formatNumber(item.power, 2) : ''
        };
      });

      // 使用工具函数导出CSV
      const csvContent = window.utils.exportToCSV(csvData, headers);
      
      // 添加中文表头
      const csvWithChineseHeader = chineseHeaders.join(',') + '\n' + csvContent;

      // 创建Blob对象
      const blob = new Blob(['\ufeff' + csvWithChineseHeader], { type: 'text/csv;charset=utf-8;' });

      // 创建下载链接
      const link = document.createElement('a');
      const url = URL.createObjectURL(blob);
      link.href = url;
      
      // 生成文件名
      const timestamp = window.utils.formatTimestamp(Date.now(), 'YYYYMMDD_HHmmss');
      link.download = `history_data_${timestamp}.csv`;
      
      // 触发下载
      document.body.appendChild(link);
      link.click();
      document.body.removeChild(link);
      
      // 释放URL对象
      URL.revokeObjectURL(url);

      console.log('历史数据已导出为CSV');
      
      // 显示成功提示
      if (typeof uiManager !== 'undefined') {
        uiManager.showNotification('success', 'CSV文件导出成功');
      }
    } catch (error) {
      console.error('CSV导出失败:', error);
      
      // 显示错误提示
      if (typeof uiManager !== 'undefined') {
        uiManager.showNotification('error', 'CSV导出失败');
      }
    }
  }

  /**
   * 清空实时数据缓存
   */
  clearRealtimeData() {
    Object.keys(this.dataTypeConfig).forEach(type => {
      this.realtimeData[type] = {
        timestamps: [],
        values: []
      };
    });

    // 更新图表
    if (this.charts.realtime) {
      this.charts.realtime.setOption({
        xAxis: { data: [] },
        series: [{ data: [] }]
      });
    }

    console.log('实时数据缓存已清空');
  }

  /**
   * 清空历史数据缓存
   */
  clearHistoryData() {
    this.historyData = [];

    // 更新图表
    if (this.charts.history) {
      this.charts.history.setOption({
        xAxis: { data: [] },
        series: []
      });
    }

    // 更新数据点计数显示（仅在浏览器环境中）
    if (typeof document !== 'undefined') {
      const countElement = document.getElementById('dataPointCount');
      if (countElement) {
        countElement.textContent = '0';
      }
    }

    console.log('历史数据缓存已清空');
  }

  /**
   * 调整所有图表大小
   */
  resizeAllCharts() {
    Object.values(this.charts).forEach(chart => {
      if (chart) {
        chart.resize();
      }
    });
  }

  /**
   * 销毁所有图表
   */
  destroy() {
    Object.values(this.charts).forEach(chart => {
      if (chart) {
        chart.dispose();
      }
    });
    this.charts = {};
    console.log('所有图表已销毁');
  }

  /**
   * 获取实时数据缓存
   * @param {string} type - 数据类型（可选）
   * @returns {object|Array} 数据缓存
   */
  getRealtimeData(type = null) {
    if (type) {
      return this.realtimeData[type] || null;
    }
    return this.realtimeData;
  }

  /**
   * 获取历史数据缓存
   * @returns {Array} 历史数据数组
   */
  getHistoryData() {
    return this.historyData;
  }

  /**
   * 设置最大实时数据点数
   * @param {number} maxPoints - 最大数据点数
   */
  setMaxRealtimePoints(maxPoints) {
    if (typeof maxPoints === 'number' && maxPoints > 0) {
      this.maxRealtimePoints = maxPoints;
      console.log('最大实时数据点数已设置为:', maxPoints);
    }
  }

  /**
   * 设置最大历史数据点数（用于采样）
   * @param {number} maxPoints - 最大数据点数
   */
  setMaxHistoryPoints(maxPoints) {
    if (typeof maxPoints === 'number' && maxPoints > 0) {
      this.maxHistoryPoints = maxPoints;
      console.log('最大历史数据点数已设置为:', maxPoints);
    }
  }
}

// 创建全局图表管理器实例
const chartManager = new ChartManager();

// 暴露到全局作用域（浏览器环境）
if (typeof window !== 'undefined') {
  window.chartManager = chartManager;
  window.ChartManager = ChartManager;
}

// 导出图表管理器类和实例（用于模块化）
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    ChartManager,
    chartManager
  };
}
