import { combineRgb } from '@companion-module/base'

const AMBER = combineRgb(224, 160, 48)
const BLUE = combineRgb(36, 54, 76)
const DARK = combineRgb(29, 33, 38)
const BLACK = combineRgb(16, 18, 20)
const WHITE = combineRgb(255, 255, 255)

// LiveRack sends colours as hex "ffRRGGBB"
function rackColour(hex) {
	const h = String(hex || '').replace('#', '')
	const rgb = h.length >= 6 ? h.slice(-6) : '4a90d9'
	const n = parseInt(rgb, 16)
	return Number.isFinite(n) ? combineRgb((n >> 16) & 255, (n >> 8) & 255, n & 255) : DARK
}

function button(name, text, bgcolor, color, actionId, options, feedbacks = [], size = '14') {
	return {
		type: 'simple',
		name,
		style: { text, size, color, bgcolor },
		steps: [{ down: [{ actionId, options }], up: [] }],
		feedbacks,
	}
}

export function buildPresets(self) {
	const v = (name) => `$(${self.label}:${name})`
	const s = self.state
	const presets = {}
	const groups = { bypass: [], select: [], keys: [], hot: [], scenes: [], pages: [] }

	s.racks.forEach((r, i) => {
		const n = i + 1
		const colour = rackColour(r.colour)

		presets[`rack_${n}_bypass`] = button(`Bypass ${r.name || 'rack ' + n}`, `${v(`rack_${n}_name`)}\\n${v(`rack_${n}_gr`)}`, colour, BLACK, 'rack_bypass', { rack: n, mode: 'toggle' }, [
			{ feedbackId: 'rack_bypassed', options: { rack: n }, style: { bgcolor: AMBER, color: BLACK, text: `${v(`rack_${n}_name`)}\\nBYPASS` } },
		])
		groups.bypass.push(`rack_${n}_bypass`)

		presets[`rack_${n}_select`] = button(`Select ${r.name || 'rack ' + n}`, v(`rack_${n}_name`), DARK, WHITE, 'rack_select', { rack: n }, [
			{ feedbackId: 'rack_selected', options: { rack: n }, style: { bgcolor: colour, color: BLACK } },
		])
		groups.select.push(`rack_${n}_select`)
	})

	for (let k = 1; k <= 8; k++) {
		presets[`key_${k}`] = button(`User Key ${k}`, v(`key_${k}_label`), DARK, WHITE, 'key_press', { key: k }, [
			{ feedbackId: 'key_lit', options: { key: k }, style: { bgcolor: AMBER, color: BLACK } },
		])
		groups.keys.push(`key_${k}`)
	}

	for (let h = 1; h <= 12; h++) {
		presets[`hot_${h}`] = button(`Hot Plugin ${h}`, v(`hot_${h}_name`), DARK, WHITE, 'hot_open', { hot: h })
		groups.hot.push(`hot_${h}`)
	}

	presets.scene_prev = button('Previous scene', 'SCENE\\n<', DARK, WHITE, 'scene_prev', {}, [], '18')
	presets.scene_now = button('Store current scene', `SCENE\\n${v('scene')}`, BLUE, WHITE, 'scene_store', {}, [], '18')
	presets.scene_next = button('Next scene', 'SCENE\\n>', DARK, WHITE, 'scene_next', {}, [], '18')
	presets.tap = button('Tap tempo', `TAP\\n${v('bpm')}`, DARK, WHITE, 'tap', {}, [], '18')
	presets.sel_bypass = button('Bypass selected rack', `BYPASS\\n${v('selected_rack')}`, DARK, WHITE, 'selected_bypass', { mode: 'toggle' })
	groups.scenes.push('scene_prev', 'scene_now', 'scene_next', 'tap', 'sel_bypass')

	presets.page_all = button('Page: All', 'All', DARK, WHITE, 'page_select', { page: 0 }, [
		{ feedbackId: 'page_current', options: { page: 0 }, style: { bgcolor: BLUE, color: WHITE } },
	])
	groups.pages.push('page_all')
	s.pages.forEach((p, i) => {
		const n = i + 1
		presets[`page_${n}`] = button(`Page: ${p}`, v(`page_${n}_name`), DARK, WHITE, 'page_select', { page: n }, [
			{ feedbackId: 'page_current', options: { page: n }, style: { bgcolor: BLUE, color: WHITE } },
		])
		groups.pages.push(`page_${n}`)
	})

	const structure = [
		{
			id: 'racks',
			name: 'Racks',
			definitions: [
				{ id: 'rack_bypass', type: 'simple', name: 'Bypass (rack colour, gain reduction)', presets: groups.bypass },
				{ id: 'rack_select', type: 'simple', name: 'Select', presets: groups.select },
			],
		},
		{
			id: 'control',
			name: 'Keys and plugins',
			definitions: [
				{ id: 'user_keys', type: 'simple', name: 'User Keys', presets: groups.keys },
				{ id: 'hot_plugins', type: 'simple', name: 'Hot Plugins', presets: groups.hot },
			],
		},
		{
			id: 'show',
			name: 'Scenes, tempo and pages',
			definitions: [
				{ id: 'scenes', type: 'simple', name: 'Scenes and tempo', presets: groups.scenes },
				{ id: 'pages', type: 'simple', name: 'Pages', presets: groups.pages },
			],
		},
	]

	return { structure, presets }
}
