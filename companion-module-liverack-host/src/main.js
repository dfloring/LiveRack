import { InstanceBase, InstanceStatus } from '@companion-module/base'
import dgram from 'node:dgram'
import { encodeMessage, decodePacket } from './osc.js'
import { getActions } from './actions.js'
import { getFeedbacks } from './feedbacks.js'
import { buildVariables } from './variables.js'
import { buildPresets } from './presets.js'

// Talks to LiveRack's OSC remote (Devices > Controllers > OSC remote, port 9010
// by default). LiveRack sends state back to the port this module sends from.
export default class LiveRackInstance extends InstanceBase {
	async init(config) {
		this.config = config
		this.resetState()
		this.setActionDefinitions(getActions(this))
		this.setFeedbackDefinitions(getFeedbacks(this))
		this.rebuildDefinitions()
		this.connect()
	}

	async destroy() {
		this.disconnect()
	}

	async configUpdated(config) {
		this.config = config
		this.disconnect()
		this.resetState()
		this.rebuildDefinitions()
		this.connect()
	}

	getConfigFields() {
		return [
			{
				type: 'static-text',
				id: 'info',
				width: 12,
				label: 'LiveRack',
				value: 'Turn on the OSC remote in LiveRack (Devices > Controllers > OSC remote). Use 127.0.0.1 if Companion runs on the same Mac.',
			},
			{ type: 'textinput', id: 'host', label: 'LiveRack IP address', width: 8, default: '127.0.0.1', minLength: 1 },
			{ type: 'number', id: 'port', label: 'OSC port', width: 4, default: 9010, min: 1, max: 65535 },
		]
	}

	//==========================================================================
	// State reported by LiveRack
	resetState() {
		this.state = {
			racks: [], // index 0 = rack 1: { name, bypass, colour, selected, gr, group, format, latency, slots: [] }
			keys: [], // { label, state }
			hot: [],
			pages: [],
			page: 0, // 0 = All
			scene: -1,
			bpm: 120,
			selected: 0,
		}
		this.lastMessage = 0
		this.shape = '' // rack count + names + colours + pages: presets are rebuilt when it changes
	}

	rack(n) {
		while (this.state.racks.length < n) this.state.racks.push({ name: '', bypass: 0, colour: 'ff4a90d9', selected: 0, gr: 0, group: '-', format: '', latency: 0, slots: [] })
		return this.state.racks[n - 1]
	}

	//==========================================================================
	// Network
	connect() {
		const host = (this.config.host || '').trim()
		const port = Number(this.config.port) || 9010
		if (!host) {
			this.updateStatus(InstanceStatus.BadConfig, 'Set the LiveRack IP address')
			return
		}

		this.updateStatus(InstanceStatus.Connecting)
		this.socket = dgram.createSocket('udp4')
		this.socket.on('error', (err) => {
			this.log('error', `OSC socket: ${err.message}`)
			this.updateStatus(InstanceStatus.ConnectionFailure, err.message)
		})
		this.socket.on('message', (msg) => this.onPacket(msg))
		this.socket.bind(0, () => {
			this.send('/liverack/subscribe')
		})

		// Keep feedback coming, and notice when LiveRack goes away
		this.pingTimer = setInterval(() => {
			this.send('/liverack/ping')
			const quiet = Date.now() - this.lastMessage
			if (this.lastMessage === 0 || quiet > 12000) this.updateStatus(InstanceStatus.ConnectionFailure, 'No reply from LiveRack')
		}, 4000)
	}

	disconnect() {
		clearInterval(this.pingTimer)
		clearTimeout(this.rebuildTimer)
		if (this.socket) {
			try {
				this.socket.close()
			} catch {
				// already closed
			}
			this.socket = undefined
		}
	}

	send(address, ...args) {
		if (!this.socket) return
		const host = (this.config.host || '').trim()
		const port = Number(this.config.port) || 9010
		try {
			this.socket.send(encodeMessage(address, args), port, host)
		} catch (e) {
			this.log('debug', `send failed: ${e.message}`)
		}
	}

	onPacket(buf) {
		const wasQuiet = this.lastMessage === 0 || Date.now() - this.lastMessage > 12000
		this.lastMessage = Date.now()
		if (wasQuiet) this.updateStatus(InstanceStatus.Ok)

		const changed = new Set()
		for (const m of decodePacket(buf)) this.handle(m.address, m.args, changed)

		if (changed.size > 0) {
			this.updateVariableValues()
			this.refreshFeedbacks(changed)
			this.maybeRebuild()
		}
	}

	handle(address, args, changed) {
		const p = address.split('/').filter(Boolean)
		if (p[0] !== 'liverack') return
		const v = args[0]
		const s = this.state

		if (p[1] === 'racks') {
			s.racks.length = Math.min(s.racks.length, Number(v))
			this.rack(Number(v) || 0)
		} else if (p[1] === 'selected') {
			s.selected = Number(v)
			changed.add('rack_selected')
		} else if (p[1] === 'scene') {
			s.scene = Number(v)
			changed.add('scene_is')
		} else if (p[1] === 'bpm') {
			s.bpm = Number(v)
		} else if (p[1] === 'pages') {
			s.pages.length = Math.min(s.pages.length, Number(v))
			while (s.pages.length < Number(v)) s.pages.push('')
		} else if (p[1] === 'page' && p.length === 2) {
			s.page = Number(v)
			changed.add('page_current')
		} else if (p[1] === 'page' && p[3] === 'name') {
			s.pages[Number(p[2]) - 1] = String(v)
		} else if (p[1] === 'rack' && p.length >= 4) {
			const r = this.rack(Number(p[2]))
			if (p[3] === 'slot' && p.length >= 6) {
				const n = Number(p[4])
				while (r.slots.length < n) r.slots.push({ name: '', bypass: 0 })
				r.slots[n - 1][p[5]] = p[5] === 'bypass' ? Number(v) : String(v)
				changed.add('plugin_bypassed')
			} else if (p[3] === 'bypass') {
				r.bypass = Number(v)
				changed.add('rack_bypassed')
			} else if (p[3] === 'selected') {
				r.selected = Number(v)
				changed.add('rack_selected')
			} else if (p[3] === 'gr' || p[3] === 'latency') {
				r[p[3]] = Number(v)
			} else {
				r[p[3]] = String(v)
				if (p[3] === 'colour') changed.add('rack_colour')
			}
		} else if (p[1] === 'key' && p.length >= 4) {
			const n = Number(p[2])
			while (s.keys.length < n) s.keys.push({ label: '', state: 0 })
			s.keys[n - 1][p[3]] = p[3] === 'state' ? Number(v) : String(v)
			changed.add('key_lit')
		} else if (p[1] === 'hot' && p[3] === 'name') {
			s.hot[Number(p[2]) - 1] = String(v)
		} else {
			return
		}
		changed.add('_any')
	}

	//==========================================================================
	// Variables and presets follow the racks LiveRack reports
	rebuildDefinitions() {
		const vars = buildVariables(this.state)
		this.setVariableDefinitions(vars.definitions)
		this.setVariableValues(vars.values)
		const presets = buildPresets(this)
		this.setPresetDefinitions(presets.structure, presets.presets)
	}

	updateVariableValues() {
		this.setVariableValues(buildVariables(this.state).values)
	}

	maybeRebuild() {
		const s = this.state
		const shape = JSON.stringify([s.racks.map((r) => [r.name, r.colour]), s.pages, s.keys.length])
		if (shape === this.shape) return
		this.shape = shape
		clearTimeout(this.rebuildTimer)
		this.rebuildTimer = setTimeout(() => this.rebuildDefinitions(), 400)
	}

	refreshFeedbacks(changed) {
		const known = ['rack_bypassed', 'rack_selected', 'plugin_bypassed', 'key_lit', 'page_current', 'scene_is']
		const list = known.filter((t) => changed.has(t))
		if (list.length > 0) this.checkFeedbacks(...list)
	}
}

export const UpgradeScripts = []
