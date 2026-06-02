<template>
  <div :class="pageClass">
    <scroller class="settings-scroll" scroll-direction="vertical" show-scrollbar="true" over-scroll="24">
      <div :class="settingsContentClass">
        <div :class="topbarClass">
          <div>
            <text :class="titleClass">Moonlight</text>
            <text :class="subtitleClass">{{ subtitleText }}</text>
          </div>
          <text :class="statusClass">{{ statusText }}</text>
        </div>

        <div :class="mainClass">
          <div class="left-panel">
            <div class="panel-header">
              <text class="panel-title">选择设备</text>
              <text :class="selectedHostAddress ? 'delete-button' : 'delete-button disabled'" @click="deleteSelectedHost">删除</text>
            </div>

            <div class="host-list">
              <div
                v-for="host in hosts"
                :key="host.id"
                :class="host.id === selectedHostId ? 'host-card selected' : 'host-card'"
                @click="selectHost(host)"
              >
                <text class="host-name">{{ host.name }}</text>
                <text class="host-ip">{{ host.address }}</text>
              </div>

              <div v-if="hosts.length === 0" class="empty-host-card">
                <text class="empty-title">未配对主机</text>
                <text class="empty-note">自动生成 PIN 后保存</text>
              </div>
            </div>

            <div class="pair-box">
              <div class="host-field-box" @click="openNameKeyboard">
                <text v-if="newHostName" class="host-field-value">{{ newHostName }}</text>
                <text v-else class="host-field-empty">名称</text>
              </div>
              <div class="host-field-box" @click="openAddressKeyboard">
                <text v-if="newHostAddress" class="host-field-value">{{ newHostAddress }}</text>
                <text v-else class="host-field-empty">IP/主机名</text>
              </div>
              <div class="pair-row">
                <div class="pin-field-box">
                  <text v-if="pairPin" class="pin-field-value">PIN {{ pairPin }}</text>
                  <text v-else class="pin-field-empty">自动生成</text>
                </div>
                <text :class="pairing ? 'pair-button disabled' : 'pair-button'" @click="pairNewHost">{{ pairing ? '等待' : '配对' }}</text>
              </div>
              <text class="pair-status">{{ pairStatus }}</text>
            </div>
          </div>

          <div :class="rightPanelClass">
            <text class="panel-title">串流参数</text>

            <div class="bitrate-block">
              <div class="seek-row">
                <text class="label">码率</text>
                <seekbar
                  class="bitrate-seek"
                  :min="500"
                  :max="25000"
                  :step="500"
                  :value="bitrate"
                  active-color="#79d66b"
                  background-color="#303a45"
                  :track-size="5"
                  :handle-size="24"
                  handle-color="#79d66b"
                  handle-inner-color="#0d1318"
                  @changing="onBitrateChanging"
                  @change="onBitrateChange"
                />
                <text class="seek-label">{{ bitrateText }}</text>
              </div>
            </div>

            <div class="row">
              <text class="label">帧率</text>
              <text :class="fps === 30 ? 'choice active' : 'choice'" @click="setFps30">30</text>
              <text :class="fps === 60 ? 'choice active' : 'choice'" @click="setFps60">60</text>
            </div>

            <div class="orientation-block">
              <text class="label orientation-label">方向</text>
              <div class="orientation-choices">
                <div class="orientation-row">
                  <text :class="rotate === 0 ? 'orientation-choice active' : 'orientation-choice'" @click="setRotate0">{{ rotate0Text }}</text>
                  <text :class="rotate === 180 ? 'orientation-choice active' : 'orientation-choice'" @click="setRotate180">{{ rotate180Text }}</text>
                </div>
                <div class="orientation-row">
                  <text :class="rotate === 90 ? 'orientation-choice active' : 'orientation-choice'" @click="setRotate90">{{ rotate90Text }}</text>
                  <text :class="rotate === 270 ? 'orientation-choice active' : 'orientation-choice'" @click="setRotate270">{{ rotate270Text }}</text>
                </div>
              </div>
            </div>

            <div class="row">
              <text class="label">模式</text>
              <text :class="viewOnly ? 'choice active' : 'choice'" @click="toggleViewOnly">{{ viewOnly ? '仅观看' : '可输入' }}</text>
              <text :class="stretch ? 'choice active' : 'choice'" @click="toggleStretch">{{ stretch ? '拉伸' : '等比' }}</text>
            </div>

            <div class="row">
              <text class="label">触摸</text>
              <text :class="touchMode === 'screen' ? 'choice active' : 'choice'" @click="setTouchScreen">屏幕</text>
              <text :class="touchMode === 'touchpad' ? 'choice active' : 'choice'" @click="setTouchpad">触摸板</text>
            </div>

            <div class="summary">
              <text class="summary-text">{{ selectedHostName }} · {{ bitrateText }} · {{ fps }}FPS · {{ orientationText }} · {{ touchModeText }}</text>
              <text :class="selectedHostAddress ? 'start-button' : 'start-button disabled'" @click="startStream">开始串流</text>
            </div>
          </div>
        </div>
      </div>
    </scroller>
  </div>
</template>

<script>
import globalModule from 'global'
import sqlite3 from 'sqlite3'
import { rgbFramePlayer } from 'rgbframe'

const HOST_DB_NAME = 'moonlight_hosts.db'
const BITRATE_MIN = 500
const BITRATE_MAX = 25000
const BITRATE_STEP = 500
const DEFAULT_SCREEN_WIDTH = 568
const DEFAULT_SCREEN_HEIGHT = 1210
const DEFAULT_HOST_ADDRESS = '192.168.100.120'
const DEFAULT_HOST_ID = 'host-192-168-100-120'
const PAIR_TIMEOUT_MS = 120000
let hostDbInstance = null
let hostDbOpenPromise = null
let globalManager = null

function getGlobalModule() {
  if (!globalManager) {
    globalManager = new globalModule.Global()
  }
  return globalManager
}

function eventValue(event) {
  if (event && typeof event === 'object') {
    if (event.value !== undefined) {
      return event.value
    }
    if (event.detail && event.detail.value !== undefined) {
      return event.detail.value
    }
    if (event.target && event.target.value !== undefined) {
      return event.target.value
    }
  }
  return event
}

function cleanText(value) {
  if (value === undefined || value === null) {
    return ''
  }
  return String(value).trim()
}

function rowValue(row, names, fallback) {
  if (!row) {
    return fallback
  }
  for (let i = 0; i < names.length; i += 1) {
    if (row[names[i]] !== undefined && row[names[i]] !== null) {
      return row[names[i]]
    }
  }
  return fallback
}

function bitrateLabel(kbps) {
  if (kbps % 1000 === 0) {
    return `${kbps / 1000}M`
  }
  return `${(kbps / 1000).toFixed(1)}M`
}

function clampBitrate(value) {
  let parsed = Number(value)
  if (!Number.isFinite(parsed)) {
    parsed = 3000
  }
  parsed = Math.round(parsed / BITRATE_STEP) * BITRATE_STEP
  if (parsed < BITRATE_MIN) {
    parsed = BITRATE_MIN
  }
  if (parsed > BITRATE_MAX) {
    parsed = BITRATE_MAX
  }
  return parsed
}

function dataRootPath() {
  if (typeof $dataDir === 'string' && $dataDir) {
    return $dataDir
  }
  if (typeof $falcon !== 'undefined' && $falcon && typeof $falcon.$dataDir === 'string' && $falcon.$dataDir) {
    return $falcon.$dataDir
  }
  try {
    if (typeof globalThis !== 'undefined' && typeof globalThis.$dataDir === 'string' && globalThis.$dataDir) {
      return globalThis.$dataDir
    }
  } catch (err) {
    console.warn(`read global data dir failed ${err}`)
  }
  return '/tmp/moonlight-rk3562-data'
}

function workspacePath(page) {
  if (page && typeof page.$workspace === 'string' && page.$workspace) {
    return page.$workspace
  }
  try {
    if (typeof globalThis !== 'undefined' && typeof globalThis.$workspace === 'string' && globalThis.$workspace) {
      return globalThis.$workspace
    }
  } catch (err) {
    console.warn(`read global workspace failed ${err}`)
  }
  if (typeof $falcon !== 'undefined' && $falcon && typeof $falcon.$workspace === 'string' && $falcon.$workspace) {
    return $falcon.$workspace
  }
  if (typeof $workspace === 'string' && $workspace) {
    return $workspace
  }
  const dataRoot = dataRootPath()
  if (dataRoot.slice(-5) === '/data') {
    return dataRoot.slice(0, -5) + '/b'
  }
  return ''
}

function bundledRuntimePath(page) {
  const workspace = workspacePath(page)
  return workspace ? workspace + '/assets/moonlight-rk3562' : ''
}

function moonlightDataPath(name) {
  return dataRootPath() + '/moonlight/' + name
}

function hostDbPath() {
  return dataRootPath() + '/' + HOST_DB_NAME
}

function positiveInt(value, fallback) {
  const parsed = parseInt(value, 10)
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback
}

function parseSizeText(value) {
  const match = String(value || '').match(/^(\d+)x(\d+)$/)
  if (!match) {
    return null
  }
  const width = positiveInt(match[1], 0)
  const height = positiveInt(match[2], 0)
  return width > 0 && height > 0 ? { width, height } : null
}

function evenDimension(value) {
  const parsed = positiveInt(value, 2)
  return Math.max(2, parsed - (parsed % 2))
}

function readDeviceSize() {
  let width = DEFAULT_SCREEN_WIDTH
  let height = DEFAULT_SCREEN_HEIGHT
  try {
    if (typeof $falcon !== 'undefined' && $falcon && $falcon.env) {
      width = positiveInt($falcon.env.deviceWidth, width)
      height = positiveInt($falcon.env.deviceHeight, height)
    }
  } catch (err) {
    console.warn(`read device size failed ${err}`)
  }
  return { width, height }
}

function readRawDrmSize() {
  try {
    if (rgbFramePlayer && typeof rgbFramePlayer.getDrmScreenSize === 'function') {
      return parseSizeText(rgbFramePlayer.getDrmScreenSize())
    }
  } catch (err) {
    console.warn(`read raw drm size failed ${err}`)
  }
  return null
}

function moonlightGeometry(logicalWidth, logicalHeight, rotate) {
  const raw = readRawDrmSize()
  if (raw && logicalWidth > logicalHeight && raw.width < raw.height) {
    if (rotate === 0) {
      return { width: logicalHeight, height: logicalWidth, rotate: 90 }
    }
    if (rotate === 180) {
      return { width: logicalHeight, height: logicalWidth, rotate: 270 }
    }
  }
  return { width: logicalWidth, height: logicalHeight, rotate }
}

function generatePairPin() {
  return `${Math.floor(Math.random() * 10000)}`.padStart(4, '0')
}

function defaultHostRecord() {
  return {
    id: DEFAULT_HOST_ID,
    name: DEFAULT_HOST_ADDRESS,
    address: DEFAULT_HOST_ADDRESS,
    pairedAt: '',
  }
}

export default {
  name: 'index',
  data() {
    return {
      hosts: [defaultHostRecord()],
      selectedHostId: DEFAULT_HOST_ID,
      statusText: 'Ready',
      pairStatus: '',
      pairing: false,
      pairToken: 0,
      newHostName: '',
      newHostAddress: DEFAULT_HOST_ADDRESS,
      pairPin: '',
      inputTaskUuid: '',
      inputTaskField: '',
      editFinished: null,
      bitrate: 1500,
      fps: 30,
      rotate: 0,
      viewOnly: true,
      stretch: false,
      touchMode: 'screen',
      screenWidth: DEFAULT_SCREEN_WIDTH,
      screenHeight: DEFAULT_SCREEN_HEIGHT,
    }
  },
  computed: {
    isScreenLandscape() {
      return this.screenWidth > this.screenHeight
    },
    isWideLandscape() {
      return this.isScreenLandscape && this.screenWidth >= 1100
    },
    pageClass() {
      if (this.isWideLandscape) {
        return 'wrapper index-page landscape-wide'
      }
      if (this.isScreenLandscape) {
        return 'wrapper index-page landscape-compact'
      }
      return 'wrapper index-page portrait'
    },
    settingsContentClass() {
      return this.isScreenLandscape && !this.isWideLandscape ? 'settings-content-compact' : 'settings-content'
    },
    topbarClass() {
      return this.isScreenLandscape && !this.isWideLandscape ? 'topbar-compact' : 'topbar'
    },
    titleClass() {
      return this.isScreenLandscape && !this.isWideLandscape ? 'title-compact' : 'title'
    },
    subtitleClass() {
      return this.isScreenLandscape && !this.isWideLandscape ? 'subtitle-compact' : 'subtitle'
    },
    statusClass() {
      return this.isScreenLandscape && !this.isWideLandscape ? 'status-compact' : 'status'
    },
    mainClass() {
      return this.isWideLandscape ? 'main-wide' : 'main'
    },
    rightPanelClass() {
      return this.isWideLandscape ? 'right-panel-wide' : 'right-panel'
    },
    targetWidth() {
      return evenDimension(this.screenWidth)
    },
    targetHeight() {
      return evenDimension(this.screenHeight)
    },
    subtitleText() {
      return `RK3562 ${this.targetWidth}x${this.targetHeight}`
    },
    selectedHost() {
      for (let i = 0; i < this.hosts.length; i += 1) {
        if (this.hosts[i].id === this.selectedHostId) {
          return this.hosts[i]
        }
      }
      return null
    },
    selectedHostName() {
      return this.selectedHost ? this.selectedHost.name : '未选择'
    },
    selectedHostAddress() {
      return this.selectedHost ? this.selectedHost.address : ''
    },
    bitrateText() {
      return bitrateLabel(this.bitrate)
    },
    rotate0Text() {
      return this.isScreenLandscape ? '横向' : '竖向'
    },
    rotate180Text() {
      return this.isScreenLandscape ? '横向(翻转)' : '竖向(翻转)'
    },
    rotate90Text() {
      return this.isScreenLandscape ? '竖向' : '横向'
    },
    rotate270Text() {
      return this.isScreenLandscape ? '竖向(翻转)' : '横向(翻转)'
    },
    orientationText() {
      if (this.rotate === 90) {
        return this.rotate90Text
      }
      if (this.rotate === 180) {
        return this.rotate180Text
      }
      if (this.rotate === 270) {
        return this.rotate270Text
      }
      return this.rotate0Text
    },
    touchModeText() {
      return this.touchMode === 'touchpad' ? '触摸板' : '屏幕'
    },
  },
  mounted() {
    this.updateScreenSize()
    console.warn('moonlight settings page ready')
    this.registerKeyboard()
    this.cleanupMoonlight()
    this.initHostStore()
  },
  beforeDestroy() {
    this.closeKeyboard()
    this.unregisterKeyboard()
  },
  methods: {
    updateScreenSize() {
      const size = readDeviceSize()
      this.screenWidth = size.width
      this.screenHeight = size.height
      console.warn(`moonlight settings screen ${this.screenWidth}x${this.screenHeight}`)
    },
    registerKeyboard() {
      try {
        const gm = getGlobalModule()
        this.editFinished = (uuid, jsonData) => {
          if (!uuid || uuid !== this.inputTaskUuid) {
            return
          }

          const field = this.inputTaskField
          const closeUuid = this.inputTaskUuid
          this.inputTaskUuid = ''
          this.inputTaskField = ''

          try {
            const result = JSON.parse(jsonData || '{}')
            if (result && result.editConfirmed) {
              this.applyKeyboardText(field, result.text)
            }
          } catch (err) {
            console.warn(`parse keyboard result failed ${err}`)
          }

          setTimeout(() => this.closeKeyboard(closeUuid), 0)
        }

        if (gm.textEditFinished && typeof gm.textEditFinished.on === 'function') {
          gm.textEditFinished.on(this.editFinished)
        }
      } catch (err) {
        console.warn(`register keyboard failed ${err}`)
      }
    },
    unregisterKeyboard() {
      try {
        const gm = getGlobalModule()
        if (this.editFinished && gm.textEditFinished && typeof gm.textEditFinished.off === 'function') {
          gm.textEditFinished.off(this.editFinished)
        }
      } catch (err) {
        console.warn(`unregister keyboard failed ${err}`)
      }
      this.editFinished = null
    },
    closeKeyboard(uuid) {
      const taskUuid = uuid || this.inputTaskUuid
      if (!taskUuid) {
        return
      }
      try {
        const gm = getGlobalModule()
        if (gm.closeTextEdit) {
          gm.closeTextEdit(taskUuid)
        }
      } catch (err) {
        console.warn(`close keyboard failed ${err}`)
      }
      if (!uuid || uuid === this.inputTaskUuid) {
        this.inputTaskUuid = ''
        this.inputTaskField = ''
      }
    },
    openKeyboard(field, text, placeholder, inputType, maxlength) {
      try {
        this.closeKeyboard()
        const gm = getGlobalModule()
        if (!gm.startTextEdit) {
          this.pairStatus = '键盘不可用'
          return
        }

        this.inputTaskField = field
        this.inputTaskUuid = gm.startTextEdit(JSON.stringify({
          text: text || '',
          placeholder,
          placeholderColor: '#8792a1',
          autofocus: true,
          maxlength,
          showCursor: true,
          cursorColor: '#79d66b',
          cursorSize: 3,
          confirmButtonDisabledOnTextEmpty: false,
          inputType,
          multiLinesEditVisible: false,
          capsLockSwitchOn: false,
          enterButtonText: '确认',
        }))
      } catch (err) {
        console.warn(`open keyboard failed ${err}`)
        this.pairStatus = '键盘打开失败'
      }
    },
    applyKeyboardText(field, text) {
      const cleaned = cleanText(text).replace(/\n/g, '')
      if (field === 'name') {
        this.newHostName = cleaned.slice(0, 32)
      } else if (field === 'address') {
        this.newHostAddress = cleaned.replace(/\s/g, '').slice(0, 128)
      } else if (field === 'pin') {
        this.pairPin = cleaned.replace(/[^0-9]/g, '').slice(0, 4)
      }
    },
    openNameKeyboard() {
      this.openKeyboard('name', this.newHostName, '名称', 'ZhCNPreferred', 32)
    },
    openAddressKeyboard() {
      this.openKeyboard('address', this.newHostAddress, 'IP/主机名', 'EnUSPreferred', 128)
    },
    cleanupMoonlight() {
      try {
        rgbFramePlayer.stopMoonlight()
      } catch (err) {
        console.warn(`settings cleanup moonlight failed ${err}`)
      }
    },
    onShow() {
      this.updateScreenSize()
      this.cleanupMoonlight()
      if (hostDbInstance) {
        this.loadHosts()
      }
    },
    async initHostStore() {
      try {
        await this.ensureHostDb()
        await this.loadHosts()
        this.statusText = 'Ready'
      } catch (err) {
        console.warn(`init host store failed ${err}`)
        this.statusText = 'DB Error'
        this.pairStatus = '主机数据打开失败'
      }
    },
    async ensureHostDb() {
      if (hostDbInstance) {
        return hostDbInstance
      }
      if (!hostDbOpenPromise) {
        hostDbOpenPromise = (async () => {
          const db = new sqlite3.Database()
          await db.open(hostDbPath())
          await db.exec(`CREATE TABLE IF NOT EXISTS hosts(
            id TEXT PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            address TEXT NOT NULL UNIQUE,
            paired_at TEXT NOT NULL
          );`)
          await db.exec(`CREATE TABLE IF NOT EXISTS settings(
            key TEXT PRIMARY KEY NOT NULL,
            value TEXT NOT NULL
          );`)
          hostDbInstance = db
          return db
        })()
      }
      try {
        return await hostDbOpenPromise
      } finally {
        hostDbOpenPromise = null
      }
    },
    async loadSelectedHostId(db) {
      const st = await db.prepare('SELECT value FROM settings WHERE key = ?;')
      try {
        await st.bind('selected_host_id')
        const rows = await st.all()
        return rows && rows.length > 0 ? cleanText(rowValue(rows[0], ['value', 'VALUE'], '')) : ''
      } finally {
        await st.finalize()
      }
    },
    async saveSelectedHostId(id) {
      try {
        const db = await this.ensureHostDb()
        const st = await db.prepare('INSERT OR REPLACE INTO settings(key, value) VALUES(?, ?);')
        try {
          await st.bind('selected_host_id', id || '')
          await st.run()
        } finally {
          await st.finalize()
        }
      } catch (err) {
        console.warn(`save selected host failed ${err}`)
      }
    },
    async loadHosts() {
      try {
        const db = await this.ensureHostDb()
        const st = await db.prepare('SELECT id, name, address, paired_at FROM hosts ORDER BY paired_at DESC;')
        let rows = []
        try {
          rows = await st.all()
        } finally {
          await st.finalize()
        }

        const hosts = []
        for (let i = 0; rows && i < rows.length; i += 1) {
          const address = cleanText(rowValue(rows[i], ['address', 'ADDRESS'], ''))
          if (!address) {
            continue
          }
          const id = cleanText(rowValue(rows[i], ['id', 'ID'], `host-${i}`))
          hosts.push({
            id,
            name: cleanText(rowValue(rows[i], ['name', 'NAME'], address)) || address,
            address,
            pairedAt: cleanText(rowValue(rows[i], ['paired_at', 'PAIRED_AT'], '')),
          })
        }
        if (hosts.length === 0) {
          hosts.push(defaultHostRecord())
        }

        const savedSelectedId = await this.loadSelectedHostId(db)
        let selectedId = this.selectedHostId || savedSelectedId
        if (!hosts.some(host => host.id === selectedId)) {
          selectedId = hosts.length > 0 ? hosts[0].id : ''
        }

        this.hosts = hosts
        this.selectedHostId = selectedId
        await this.saveSelectedHostId(selectedId)
      } catch (err) {
        console.warn(`load hosts failed ${err}`)
        this.statusText = 'DB Error'
        this.pairStatus = '主机数据读取失败'
      }
    },
    findHostByAddress(address) {
      for (let i = 0; i < this.hosts.length; i += 1) {
        if (this.hosts[i].address === address) {
          return this.hosts[i]
        }
      }
      return null
    },
    rememberHost(address, name) {
      const existing = this.findHostByAddress(address)
      const id = existing ? existing.id : `host-${Date.now()}`
      const host = {
        id,
        name: cleanText(name) || address,
        address,
        pairedAt: String(Date.now()),
      }
      const hosts = []
      let replaced = false
      for (let i = 0; i < this.hosts.length; i += 1) {
        if (this.hosts[i].id === id || this.hosts[i].address === address) {
          if (!replaced) {
            hosts.push(host)
            replaced = true
          }
        } else {
          hosts.push(this.hosts[i])
        }
      }
      if (!replaced) {
        hosts.unshift(host)
      }
      this.hosts = hosts
      this.selectedHostId = id
      this.saveSelectedHostId(id)
      return host
    },
    async persistHost(host) {
      const db = await this.ensureHostDb()
      const st = await db.prepare('INSERT OR REPLACE INTO hosts(id, name, address, paired_at) VALUES(?, ?, ?, ?);')
      try {
        await st.bind(host.id, host.name, host.address, host.pairedAt)
        await st.run()
      } finally {
        await st.finalize()
      }
      await this.saveSelectedHostId(host.id)
    },
    async upsertHost(address, name) {
      const host = this.rememberHost(address, name)
      await this.persistHost(host)
      await this.loadHosts()
    },
    selectHost(host) {
      this.selectedHostId = host.id
      this.saveSelectedHostId(host.id)
    },
    async deleteSelectedHost() {
      if (!this.selectedHostId) {
        this.pairStatus = '未选择主机'
        return
      }
      try {
        const db = await this.ensureHostDb()
        const st = await db.prepare('DELETE FROM hosts WHERE id = ?;')
        try {
          await st.bind(this.selectedHostId)
          await st.run()
        } finally {
          await st.finalize()
        }
        this.selectedHostId = ''
        await this.saveSelectedHostId('')
        await this.loadHosts()
        this.pairStatus = '已删除'
      } catch (err) {
        console.warn(`delete host failed ${err}`)
        this.pairStatus = '删除失败'
      }
    },
    async pairNewHost() {
      if (this.pairing) {
        this.cancelPairing()
        return
      }

      const address = cleanText(this.newHostAddress)
      const name = cleanText(this.newHostName) || address
      if (!address) {
        this.pairStatus = '填写主机'
        return
      }

      const pin = generatePairPin()
      this.pairPin = pin
      this.pairing = true
      const token = this.pairToken + 1
      this.pairToken = token
      this.statusText = 'Pairing'
      this.pairStatus = `在主机输入 ${pin}`
      let lastResult = 0
      try {
        await new Promise(resolve => setTimeout(resolve, 300))

        if (!this.pairing || this.pairToken !== token) {
          return
        }
        this.pairStatus = `在主机输入 ${pin}`
        const startedAt = Date.now()
        const result = rgbFramePlayer.pairMoonlight({
          binary: '',
          runtimePath: bundledRuntimePath(this),
          workdir: moonlightDataPath('runtime'),
          logPath: '/tmp/moonlight-pair.log',
          keyDir: moonlightDataPath('keys'),
          host: address,
          pin,
        })
        lastResult = Number(result)

        if (!this.pairing || this.pairToken !== token) {
          return
        }
        if (lastResult === 0 || !Number.isFinite(lastResult)) {
          const host = this.rememberHost(address, name)
          this.persistHost(host).catch(err => console.warn(`persist host failed ${err}`))
          this.newHostName = ''
          this.newHostAddress = DEFAULT_HOST_ADDRESS
          this.pairPin = ''
          this.pairStatus = '配对成功'
          this.statusText = 'Ready'
          return
        }
        const elapsed = Date.now() - startedAt
        this.pairStatus = elapsed >= PAIR_TIMEOUT_MS ? `配对超时 ${lastResult}` : `配对失败 ${lastResult}`
        this.statusText = 'Pair Fail'
      } catch (err) {
        console.warn(`pair host failed ${err}`)
        this.pairStatus = `配对异常 · PIN ${pin}`
        this.statusText = 'Pair Fail'
      } finally {
        if (this.pairToken === token) {
          this.pairing = false
        }
      }
    },
    cancelPairing() {
      this.pairToken += 1
      this.pairing = false
      this.statusText = 'Ready'
      this.pairStatus = '已取消配对'
    },
    setBitrateValue(value) {
      this.bitrate = clampBitrate(value)
    },
    onBitrateChanging(event) {
      this.setBitrateValue(eventValue(event))
    },
    onBitrateChange(event) {
      this.setBitrateValue(eventValue(event))
    },
    setFps30() {
      this.fps = 30
    },
    setFps60() {
      this.fps = 60
    },
    setRotate0() {
      this.rotate = 0
    },
    setRotate90() {
      this.rotate = 90
    },
    setRotate180() {
      this.rotate = 180
    },
    setRotate270() {
      this.rotate = 270
    },
    toggleViewOnly() {
      this.viewOnly = !this.viewOnly
    },
    toggleStretch() {
      this.stretch = !this.stretch
    },
    setTouchScreen() {
      this.touchMode = 'screen'
    },
    setTouchpad() {
      this.touchMode = 'touchpad'
    },
    startStream() {
      if (!this.selectedHostAddress) {
        this.pairStatus = '未选择主机'
        return
      }
      const geometry = moonlightGeometry(this.targetWidth, this.targetHeight, this.rotate)
      console.warn('moonlight navigate to blank frame')
      $falcon.navTo('frame', {
        host: this.selectedHostAddress,
        app: 'Desktop',
        width: geometry.width,
        height: geometry.height,
        fps: this.fps,
        bitrate: this.bitrate,
        packetSize: 1024,
        remote: 'yes',
        rotate: geometry.rotate,
        viewOnly: this.viewOnly,
        stretch: this.stretch,
        touchMode: this.touchMode,
      })
    },
  },
}
</script>

<style lang="less" scoped>
@import "base.less";

.index-page {
  width: 100vw;
  height: 100vh;
  justify-content: flex-start;
  align-items: stretch;
  background-color: #101418;
}

.settings-scroll {
  width: 100vw;
  height: 100vh;
  flex-direction: column;
}

.settings-content {
  width: 100vw;
  min-height: 100vh;
  padding: 22px;
  background-color: #101418;
}

.settings-content-compact {
  width: 100vw;
  min-height: 100vh;
  padding: 10px;
  background-color: #101418;
}

.topbar {
  height: 68px;
  flex-direction: row;
  justify-content: space-between;
  align-items: center;
}

.topbar-compact {
  height: 58px;
  flex-direction: row;
  justify-content: space-between;
  align-items: center;
}

.title {
  font-size: 34px;
  color: #f4f8fb;
  font-weight: 700;
}

.title-compact {
  font-size: 30px;
  color: #f4f8fb;
  font-weight: 700;
}

.subtitle {
  margin-top: 2px;
  font-size: 18px;
  color: #96a2b2;
}

.subtitle-compact {
  margin-top: 2px;
  font-size: 16px;
  color: #96a2b2;
}

.status {
  width: 126px;
  height: 38px;
  line-height: 38px;
  border-radius: 6px;
  text-align: center;
  font-size: 18px;
  color: #0d1318;
  background-color: #79d66b;
  font-weight: 700;
}

.status-compact {
  width: 112px;
  height: 34px;
  line-height: 34px;
  border-radius: 6px;
  text-align: center;
  font-size: 16px;
  color: #0d1318;
  background-color: #79d66b;
  font-weight: 700;
}

.main {
  margin-top: 12px;
  height: 1086px;
  flex-direction: column;
}

.main-wide {
  margin-top: 12px;
  height: 650px;
  flex-direction: row;
}

.left-panel {
  width: 524px;
  height: 410px;
  padding: 14px;
  border-radius: 8px;
  background-color: #171d23;
}

.right-panel-wide {
  width: 524px;
  height: 650px;
  margin-left: 14px;
  padding: 14px;
  border-radius: 8px;
  background-color: #171d23;
}

.right-panel {
  width: 524px;
  height: 650px;
  margin-top: 14px;
  padding: 14px;
  border-radius: 8px;
  background-color: #171d23;
}

.panel-header {
  height: 34px;
  flex-direction: row;
  justify-content: space-between;
  align-items: center;
}

.panel-title {
  height: 32px;
  line-height: 32px;
  font-size: 24px;
  color: #dce4ee;
  font-weight: 700;
}

.delete-button {
  width: 82px;
  height: 32px;
  line-height: 32px;
  border-radius: 6px;
  text-align: center;
  font-size: 17px;
  color: #ffd1d1;
  background-color: #41252a;
}

.host-list {
  height: 178px;
  margin-top: 8px;
}

.host-card {
  height: 62px;
  margin-bottom: 8px;
  padding: 9px 12px;
  border-radius: 8px;
  border-width: 2px;
  border-style: solid;
  border-color: #222b35;
  background-color: #222b35;
}

.selected {
  border-color: #79d66b;
  background-color: #223020;
}

.host-name {
  font-size: 20px;
  color: #f2f5f8;
  font-weight: 700;
}

.host-ip {
  margin-top: 4px;
  font-size: 17px;
  color: #aeb8c5;
}

.empty-host-card {
  height: 174px;
  padding: 24px 12px;
  border-radius: 8px;
  border-width: 2px;
  border-style: solid;
  border-color: #222b35;
  background-color: #1d242c;
  justify-content: center;
}

.empty-title {
  text-align: center;
  font-size: 22px;
  color: #dce4ee;
  font-weight: 700;
}

.empty-note {
  margin-top: 10px;
  text-align: center;
  font-size: 17px;
  color: #96a2b2;
}

.pair-box {
  height: 170px;
  margin-top: 10px;
}

.host-field-box {
  width: 496px;
  height: 38px;
  margin-bottom: 8px;
  border-radius: 6px;
  background-color: #222b35;
}

.host-field-value {
  width: 472px;
  height: 38px;
  line-height: 38px;
  padding-left: 12px;
  padding-right: 12px;
  font-size: 18px;
  color: #f2f5f8;
}

.host-field-empty {
  width: 472px;
  height: 38px;
  line-height: 38px;
  padding-left: 12px;
  padding-right: 12px;
  font-size: 18px;
  color: #aeb8c5;
}

.pair-row {
  width: 496px;
  height: 40px;
  flex-direction: row;
}

.pin-field-box {
  width: 244px;
  height: 40px;
  border-radius: 6px;
  background-color: #222b35;
}

.pin-field-value {
  width: 220px;
  height: 40px;
  line-height: 40px;
  padding-left: 12px;
  padding-right: 12px;
  font-size: 18px;
  color: #f2f5f8;
}

.pin-field-empty {
  width: 220px;
  height: 40px;
  line-height: 40px;
  padding-left: 12px;
  padding-right: 12px;
  font-size: 18px;
  color: #aeb8c5;
}

.pair-button {
  width: 238px;
  height: 40px;
  line-height: 40px;
  margin-left: 14px;
  border-radius: 6px;
  text-align: center;
  font-size: 19px;
  color: #0d1318;
  background-color: #79d66b;
  font-weight: 700;
}

.pair-status {
  width: 496px;
  height: 28px;
  line-height: 28px;
  margin-top: 8px;
  font-size: 17px;
  color: #aeb8c5;
}

.row {
  height: 42px;
  margin-top: 6px;
  flex-direction: row;
  align-items: center;
}

.label {
  width: 80px;
  font-size: 20px;
  color: #96a2b2;
}

.choice {
  width: 104px;
  height: 35px;
  line-height: 35px;
  margin-right: 8px;
  border-radius: 6px;
  text-align: center;
  font-size: 18px;
  color: #c1cad6;
  background-color: #222b35;
}

.bitrate-block {
  height: 56px;
  margin-top: 6px;
}

.seek-row {
  height: 48px;
  flex-direction: row;
  align-items: center;
}

.seek-label {
  width: 68px;
  margin-left: 10px;
  font-size: 18px;
  color: #dce4ee;
  font-weight: 700;
}

.bitrate-seek {
  width: 338px;
  height: 48px;
}

.orientation-block {
  height: 86px;
  margin-top: 6px;
  flex-direction: row;
}

.orientation-label {
  height: 80px;
  line-height: 40px;
}

.orientation-choices {
  width: 416px;
  height: 84px;
}

.orientation-row {
  height: 38px;
  margin-bottom: 8px;
  flex-direction: row;
}

.orientation-choice {
  width: 194px;
  height: 38px;
  line-height: 38px;
  margin-right: 8px;
  border-radius: 6px;
  text-align: center;
  font-size: 17px;
  color: #c1cad6;
  background-color: #222b35;
}

.active {
  color: #0d1318;
  background-color: #79d66b;
  font-weight: 700;
}

.disabled {
  opacity: 0.42;
}

.summary {
  height: 92px;
  margin-top: 10px;
  flex-direction: column;
  justify-content: flex-start;
  align-items: stretch;
}

.summary-text {
  width: 496px;
  height: 30px;
  line-height: 30px;
  font-size: 17px;
  color: #aeb8c5;
}

.start-button {
  width: 496px;
  height: 52px;
  line-height: 52px;
  margin-top: 8px;
  border-radius: 8px;
  text-align: center;
  font-size: 23px;
  color: #0d1318;
  background-color: #79d66b;
  font-weight: 700;
}

.start-button:active {
  background-color: #9ae889;
}

</style>
