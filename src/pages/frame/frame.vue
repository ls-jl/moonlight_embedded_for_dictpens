<template>
  <div class="frame-page"></div>
</template>

<script>
import { rgbFramePlayer } from 'rgbframe'
import Volume from 'volume'

const DEFAULT_MOONLIGHT_OPTIONS = {
  binary: '',
  workdir: '',
  runtimePath: '',
  logPath: '',
  keyDir: '',
  host: '',
  app: 'Desktop',
  width: 568,
  height: 1210,
  fps: 30,
  bitrate: 1500,
  packetSize: 1024,
  remote: 'yes',
  rotate: 0,
  viewOnly: true,
  stretch: false,
  takeover: true,
  otgHost: false,
  touchMode: 'screen',
  touchDevice: '',
  touchRotation: 0,
  touchOffsetX: 0,
  touchOffsetY: 0,
  volumeBaseline: -1,
}

function toInt(value, fallback) {
  const parsed = parseInt(value, 10)
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback
}

function evenDimension(value) {
  const parsed = toInt(value, 2)
  return Math.max(2, parsed - (parsed % 2))
}

function toNonNegativeInt(value, fallback) {
  const parsed = parseInt(value, 10)
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : fallback
}

function readDeviceSize() {
  let width = DEFAULT_MOONLIGHT_OPTIONS.width
  let height = DEFAULT_MOONLIGHT_OPTIONS.height
  try {
    if (typeof $falcon !== 'undefined' && $falcon && $falcon.env) {
      width = toInt($falcon.env.deviceWidth, width)
      height = toInt($falcon.env.deviceHeight, height)
    }
  } catch (err) {
    console.warn(`read frame device size failed ${err}`)
  }
  return {
    width: evenDimension(width),
    height: evenDimension(height),
  }
}

function toBool(value, fallback) {
  if (value === true || value === 'true') {
    return true
  }
  if (value === false || value === 'false') {
    return false
  }
  return fallback
}

function normalizeTouchMode(value) {
  return value === 'touchpad' ? 'touchpad' : 'screen'
}

function normalizeVolumeValue(value) {
  if (value === undefined || value === null) {
    return -1
  }

  if (typeof value === 'object') {
    const keys = ['volumeValue', 'currentVolumeValue', 'currentVolume', 'volume', 'value', 'percent', 'data', 'result']
    for (let i = 0; i < keys.length; i += 1) {
      const parsed = normalizeVolumeValue(value[keys[i]])
      if (parsed >= 0) {
        return parsed
      }
    }
    return -1
  }

  let parsed = Number(value)
  if (!Number.isFinite(parsed)) {
    return -1
  }
  if (parsed > 0 && parsed <= 1) {
    parsed *= 100
  }
  if (parsed < 0) {
    parsed = 0
  }
  if (parsed > 100) {
    parsed = 100
  }
  return Math.round(parsed)
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

export default {
  name: 'frame',
  data() {
    return {
      moonlightRunning: false,
      moonlightStarting: false,
      moonlightStartToken: 0,
      moonlightWatchdogTimer: null,
      leavingFrame: false,
    }
  },
  mounted() {
    this.startMoonlight()
  },
  methods: {
    moonlightOptions(volumeBaseline) {
      const options = this.$page && this.$page.options ? this.$page.options : {}
      const deviceSize = readDeviceSize()
      const viewOnly = toBool(options.viewOnly, DEFAULT_MOONLIGHT_OPTIONS.viewOnly)
      return {
        binary: DEFAULT_MOONLIGHT_OPTIONS.binary,
        workdir: moonlightDataPath('runtime'),
        runtimePath: bundledRuntimePath(this),
        logPath: moonlightDataPath('moonlight-drm.log'),
        keyDir: moonlightDataPath('keys'),
        host: options.host || DEFAULT_MOONLIGHT_OPTIONS.host,
        app: options.app || DEFAULT_MOONLIGHT_OPTIONS.app,
        width: evenDimension(toInt(options.width, deviceSize.width)),
        height: evenDimension(toInt(options.height, deviceSize.height)),
        fps: toInt(options.fps, DEFAULT_MOONLIGHT_OPTIONS.fps),
        bitrate: toInt(options.bitrate, DEFAULT_MOONLIGHT_OPTIONS.bitrate),
        packetSize: toInt(options.packetSize, DEFAULT_MOONLIGHT_OPTIONS.packetSize),
        remote: options.remote || DEFAULT_MOONLIGHT_OPTIONS.remote,
        rotate: toInt(options.rotate, DEFAULT_MOONLIGHT_OPTIONS.rotate),
        viewOnly,
        stretch: toBool(options.stretch, DEFAULT_MOONLIGHT_OPTIONS.stretch),
        takeover: true,
        otgHost: !viewOnly,
        touchMode: normalizeTouchMode(options.touchMode || DEFAULT_MOONLIGHT_OPTIONS.touchMode),
        touchDevice: options.touchDevice || DEFAULT_MOONLIGHT_OPTIONS.touchDevice,
        touchRotation: toNonNegativeInt(options.touchRotation, toNonNegativeInt(options.rotate, DEFAULT_MOONLIGHT_OPTIONS.touchRotation)),
        touchOffsetX: toNonNegativeInt(options.touchOffsetX, DEFAULT_MOONLIGHT_OPTIONS.touchOffsetX),
        touchOffsetY: toNonNegativeInt(options.touchOffsetY, DEFAULT_MOONLIGHT_OPTIONS.touchOffsetY),
        volumeBaseline: volumeBaseline >= 0 ? volumeBaseline : DEFAULT_MOONLIGHT_OPTIONS.volumeBaseline,
      }
    },
    readFallbackVolumeBaseline() {
      try {
        if (typeof $falcon !== 'undefined' && $falcon && $falcon.env) {
          const fromCustom = normalizeVolumeValue($falcon.env.custom && $falcon.env.custom.volumeValue)
          if (fromCustom >= 0) {
            return fromCustom
          }
          const fromEnv = normalizeVolumeValue($falcon.env.volumeValue || $falcon.env.volume)
          if (fromEnv >= 0) {
            return fromEnv
          }
        }
      } catch (err) {
        console.warn(`read env volume failed ${err}`)
      }
      return -1
    },
    resolveVolumeBaseline(done) {
      let settled = false
      const finish = (value) => {
        if (settled) {
          return
        }
        settled = true
        done(normalizeVolumeValue(value))
      }

      const fallback = this.readFallbackVolumeBaseline()
      try {
        const manager = Volume && typeof Volume.getVolumeManager === 'function'
          ? Volume.getVolumeManager()
          : null
        if (manager && typeof manager.getVolume === 'function') {
          try {
            const result = manager.getVolume({ callBackVolumeValue: finish })
            if (result && typeof result.then === 'function') {
              result.then(finish)
            }
            const parsed = normalizeVolumeValue(result)
            if (parsed >= 0) {
              finish(parsed)
            }
          } catch (err) {
            console.warn(`getVolume object callback failed ${err}`)
          }

          if (!settled) {
            try {
              const result = manager.getVolume(finish)
              if (result && typeof result.then === 'function') {
                result.then(finish)
              }
              const parsed = normalizeVolumeValue(result)
              if (parsed >= 0) {
                finish(parsed)
              }
            } catch (err) {
              console.warn(`getVolume callback failed ${err}`)
            }
          }

          setTimeout(() => finish(fallback), 300)
          return
        }
      } catch (err) {
        console.warn(`resolve volume manager failed ${err}`)
      }

      finish(fallback)
    },
    startMoonlight() {
      if (this.moonlightRunning || this.moonlightStarting) {
        try {
          if (rgbFramePlayer.isMoonlightRunning()) {
            return
          }
        } catch (err) {
          console.warn(`isMoonlightRunning failed ${err}`)
        }
        if (this.moonlightStarting) {
          return
        }
        this.moonlightRunning = false
      }

      this.moonlightStarting = true
      const startToken = this.moonlightStartToken + 1
      this.moonlightStartToken = startToken
      this.resolveVolumeBaseline((volumeBaseline) => {
        if (startToken !== this.moonlightStartToken) {
          return
        }
        try {
          console.warn(`moonlight blank frame start volume ${volumeBaseline}`)
          const pid = rgbFramePlayer.startMoonlight(this.moonlightOptions(volumeBaseline))
          this.moonlightRunning = Number(pid) > 0
          console.warn(`moonlight blank frame pid ${pid}`)
          if (this.moonlightRunning) {
            this.startMoonlightWatchdog()
          } else {
            this.leaveBlankFrame('start failed')
          }
        } catch (err) {
          console.warn(`startMoonlight failed ${err}`)
          this.moonlightRunning = false
          this.leaveBlankFrame('start exception')
        } finally {
          this.moonlightStarting = false
        }
      })
    },
    startMoonlightWatchdog() {
      this.stopMoonlightWatchdog()
      this.moonlightWatchdogTimer = setInterval(() => {
        if (this.moonlightStarting || this.leavingFrame) {
          return
        }

        let running = false
        try {
          running = rgbFramePlayer.isMoonlightRunning()
        } catch (err) {
          console.warn(`watch moonlight failed ${err}`)
        }
        if (running) {
          this.moonlightRunning = true
          return
        }

        if (this.moonlightRunning) {
          console.warn('moonlight exited, leave blank frame')
          this.moonlightRunning = false
          this.leaveBlankFrame('moonlight exited')
        }
      }, 1000)
    },
    stopMoonlightWatchdog() {
      if (this.moonlightWatchdogTimer) {
        clearInterval(this.moonlightWatchdogTimer)
        this.moonlightWatchdogTimer = null
      }
    },
    leaveBlankFrame(reason) {
      if (this.leavingFrame) {
        return
      }
      this.leavingFrame = true
      console.warn(`moonlight leave blank frame: ${reason}`)
      this.stopMoonlight()
      setTimeout(() => {
        try {
          $falcon.navTo('index', { streamStatus: reason || 'stopped' })
        } catch (err) {
          console.warn(`nav index failed ${err}`)
        }
      }, 100)
    },
    stopMoonlight() {
      this.stopMoonlightWatchdog()
      try {
        console.warn('moonlight blank frame stop')
        rgbFramePlayer.stopMoonlight()
      } catch (err) {
        console.warn(`stopMoonlight failed ${err}`)
      }
      this.moonlightStartToken += 1
      this.moonlightStarting = false
      this.moonlightRunning = false
    },
    onShow() {
      if (!this.leavingFrame) {
        this.startMoonlight()
      }
    },
    onHide() {
      console.warn('moonlight blank frame onHide')
      this.stopMoonlight()
    },
    onUnload() {
      console.warn('moonlight blank frame onUnload')
      this.stopMoonlight()
    },
  },
}
</script>

<style lang="less" scoped>
.frame-page {
  width: 100vw;
  height: 100vh;
  background-color: #000000;
}
</style>
