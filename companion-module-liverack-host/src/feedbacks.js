import { combineRgb } from '@companion-module/base'

const RACK = { type: 'number', id: 'rack', label: 'Rack number', default: 1, min: 1, max: 256 }
const AMBER = combineRgb(224, 160, 48)
const BLUE = combineRgb(36, 54, 76)
const BLACK = combineRgb(16, 18, 20)
const WHITE = combineRgb(255, 255, 255)

export function getFeedbacks(self) {
	const rack = (n) => self.state.racks[Number(n) - 1]

	return {
		rack_bypassed: {
			type: 'boolean',
			name: 'Rack is bypassed',
			defaultStyle: { bgcolor: AMBER, color: BLACK },
			options: [RACK],
			callback: (fb) => !!rack(fb.options.rack)?.bypass,
		},
		rack_selected: {
			type: 'boolean',
			name: 'Rack is selected',
			defaultStyle: { bgcolor: BLUE, color: WHITE },
			options: [RACK],
			callback: (fb) => self.state.selected === Number(fb.options.rack),
		},
		plugin_bypassed: {
			type: 'boolean',
			name: 'Plugin is bypassed',
			defaultStyle: { bgcolor: AMBER, color: BLACK },
			options: [RACK, { type: 'number', id: 'slot', label: 'Plugin position (1 = top)', default: 1, min: 1, max: 64 }],
			callback: (fb) => !!rack(fb.options.rack)?.slots?.[Number(fb.options.slot) - 1]?.bypass,
		},
		key_lit: {
			type: 'boolean',
			name: 'User Key is lit',
			defaultStyle: { bgcolor: AMBER, color: BLACK },
			options: [{ type: 'number', id: 'key', label: 'User Key', default: 1, min: 1, max: 8 }],
			callback: (fb) => !!self.state.keys[Number(fb.options.key) - 1]?.state,
		},
		page_current: {
			type: 'boolean',
			name: 'Page is showing',
			defaultStyle: { bgcolor: BLUE, color: WHITE },
			options: [{ type: 'number', id: 'page', label: 'Page (0 = All)', default: 0, min: 0, max: 64 }],
			callback: (fb) => self.state.page === Number(fb.options.page),
		},
		scene_is: {
			type: 'boolean',
			name: 'Scene is the current one',
			defaultStyle: { bgcolor: BLUE, color: WHITE },
			options: [{ type: 'number', id: 'scene', label: 'Scene number', default: 1, min: 0, max: 999 }],
			callback: (fb) => self.state.scene === Number(fb.options.scene),
		},
	}
}
